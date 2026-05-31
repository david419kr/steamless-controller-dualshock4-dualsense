#include "SteamController.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Internal helper: build a 64-byte feature report command buffer.
//
// The 2026 Steam Controller routes firmware commands through Feature Report
// 0x01 (same channel the original SC used, now with an explicit report ID).
//
// Buffer layout:
//   [0] FEATURE_REPORT_CMD (0x01)  — HID feature report ID
//   [1] cmd                         — command byte (0x81, 0x87, etc.)
//   [2] payloadSize                 — number of payload bytes that follow
//   [3..3+payloadSize-1] payload    — command arguments
//   [rest] zeros
// ---------------------------------------------------------------------------

static void BuildCmd(uint8_t (&buf)[64], uint8_t cmd,
                     const uint8_t* payload = nullptr, uint8_t payloadSize = 0) {
    std::memset(buf, 0, 64);
    buf[0] = SteamController::FEATURE_REPORT_CMD;
    buf[1] = cmd;
    buf[2] = payloadSize;
    if (payload && payloadSize)
        std::memcpy(buf + 3, payload, payloadSize);
}

static int16_t ReadS16LE(const uint8_t* p) {
    int16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static uint16_t ReadU16LE(const uint8_t* p) {
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t ReadU32LE(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static void WriteU16LE(uint8_t* p, uint16_t v) {
    std::memcpy(p, &v, sizeof(v));
}

static double ClampUnit(double value) {
    return std::clamp(value, 0.0, 1.0);
}

static double MotorByteToUnit(uint8_t value) {
    static constexpr double kDeadzone = 6.0;
    if (value <= kDeadzone)
        return 0.0;
    return (static_cast<double>(value) - kDeadzone) / (255.0 - kDeadzone);
}

static uint16_t UnitToHapticSpeed(double value, uint16_t minimumSpeed) {
    value = ClampUnit(value);
    if (value <= 0.0)
        return 0;

    const double curved = std::pow(value, 0.68);
    const double speed = static_cast<double>(minimumSpeed)
                       + curved * static_cast<double>(0xFFFFu - minimumSpeed);
    return static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::lround(speed)), 0, 0xFFFF));
}

static constexpr uint8_t HAPTIC_COMMAND_TICK = 1;
static constexpr uint8_t HAPTIC_COMMAND_CLICK = 2;
static constexpr uint16_t TRACKPAD_CLICK_PULSE_US = 5000;
static constexpr int8_t TRACKPAD_TOUCH_GAIN_DB = -45;
static constexpr int8_t TRACKPAD_CLICK_COMMAND_GAIN_DB = 9;
static constexpr int16_t TRACKPAD_CLICK_PULSE_GAIN_DB = 40;
static constexpr uint8_t TRACKPAD_HAPTIC_OUTPUT_LEFT = 0x00;
static constexpr uint8_t TRACKPAD_HAPTIC_OUTPUT_RIGHT = 0x01;
static constexpr uint8_t TRACKPAD_FEATURE_PAD_LEFT = 0x00;
static constexpr uint8_t TRACKPAD_FEATURE_PAD_RIGHT = 0x01;
static constexpr uint8_t HAPTIC_PULSE_HIGH_PRIORITY = 0x01;

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool SteamController::Open(uint32_t activeReportTimeoutMs) {
    for (uint16_t pid : { SC2026_PID, SC2026_DONGLE_PID }) {
        auto paths = HidDevice::Enumerate(VALVE_VID, pid, VENDOR_USAGE_PAGE);
        if (paths.empty()) continue;

        // For the wired controller there is only one interface; for the dongle
        // there are up to four slots (one per paired controller). Try each in
        // order and use the first that produces a live input report.
        for (auto const& path : paths) {
            if (!m_device.Open(path)) continue;

            uint8_t buf[64];
            size_t n = m_device.ReadInputReport(buf, sizeof(buf), activeReportTimeoutMs);
            if (n > 0 && IsStateReportId(buf[0])) {
                printf("Active interface found for PID=%04X.\n", pid);
                return true;
            }

            if (n > 0)
                printf("Unexpected report ID 0x%02X on PID=%04X (expected 0x%02X or 0x%02X) — possible firmware mismatch.\n",
                       buf[0], pid, REPORT_STATE, REPORT_STATE_LEGACY);
            else
                printf("Read timeout on PID=%04X vendor interface — no active controller on this slot.\n", pid);

            m_device.Close();
        }
    }

    printf("No Steam Controller found (wired PID=%04X or dongle PID=%04X).\n",
           SC2026_PID, SC2026_DONGLE_PID);
    return false;
}

void SteamController::Close() {
    if (m_running.exchange(false) && m_heartbeat.joinable())
        m_heartbeat.join();
    ClearTrackpadHaptics();
    SetRumble(0, 0);
    m_device.Close();
}

// ---------------------------------------------------------------------------
// Lizard mode
// ---------------------------------------------------------------------------

bool SteamController::DisableLizardMode() {
    uint8_t buf[64];

    // Step 1: CLEAR_DIGITAL_MAPPINGS — kills keyboard/mouse button emulation.
    BuildCmd(buf, CMD_CLEAR_DIGITAL_MAPPINGS);
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        if (!m_device.SendFeatureReport(buf, sizeof(buf))) {
            printf("Failed to send CLEAR_DIGITAL_MAPPINGS.\n");
            return false;
        }
    }

    // Step 2: SET_SETTINGS — keep raw IMU off until DS4 mode asks for it and
    // set both trackpads to TRACKPAD_NONE.
    const uint8_t settingsPayload[] = {
        SETTING_IMU_MODE,             0x00, 0x00,
        SETTING_LEFT_TRACKPAD_MODE,  0x00, 0x00,
        SETTING_RIGHT_TRACKPAD_MODE, 0x00, 0x00,
    };
    m_desiredImuEnabled.store(false, std::memory_order_relaxed);
    if (!SendSettingsPayload(settingsPayload, sizeof(settingsPayload))) {
        printf("Failed to send SET_SETTINGS_VALUES.\n");
        return false;
    }

    if (!m_running.exchange(true))
        m_heartbeat = std::thread(&SteamController::HeartbeatLoop, this);

    return true;
}

bool SteamController::EnableLizardMode() {
    if (m_running.exchange(false) && m_heartbeat.joinable())
        m_heartbeat.join();

    ClearTrackpadHaptics();
    SetRumble(0, 0);
    SetImuEnabled(false);

    uint8_t buf[64];
    BuildCmd(buf, CMD_SET_DEFAULT_MAPPINGS);
    std::lock_guard<std::mutex> lock(m_writeMutex);
    return m_device.SendFeatureReport(buf, sizeof(buf));
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

size_t SteamController::ReadReport(uint8_t* buffer, size_t size, uint32_t timeoutMs) {
    return m_device.ReadInputReport(buffer, size, timeoutMs);
}

bool SteamController::IsStateReportId(uint8_t reportId) {
    return reportId == REPORT_STATE || reportId == REPORT_STATE_LEGACY;
}

bool SteamController::IsBatteryReportId(uint8_t reportId) {
    return reportId == REPORT_BATTERY_STATUS;
}

bool SteamController::ParseStateReport(const uint8_t* buffer, size_t size, SteamControllerState& state) {
    if (!buffer || size < 30 || !IsStateReportId(buffer[0]))
        return false;

    SteamControllerState s{};
    s.reportId = buffer[0];
    s.sequence = buffer[1];
    s.buttons = static_cast<uint32_t>(buffer[2])
              | (static_cast<uint32_t>(buffer[3]) << 8)
              | (static_cast<uint32_t>(buffer[4]) << 16)
              | (static_cast<uint32_t>(buffer[5]) << 24);

    s.leftTrigger = ReadS16LE(buffer + 6);
    s.rightTrigger = ReadS16LE(buffer + 8);
    s.leftStickX = ReadS16LE(buffer + 10);
    s.leftStickY = ReadS16LE(buffer + 12);
    s.rightStickX = ReadS16LE(buffer + 14);
    s.rightStickY = ReadS16LE(buffer + 16);
    s.leftPadX = ReadS16LE(buffer + 18);
    s.leftPadY = ReadS16LE(buffer + 20);
    s.leftPadPressure = ReadU16LE(buffer + 22);
    s.rightPadX = ReadS16LE(buffer + 24);
    s.rightPadY = ReadS16LE(buffer + 26);
    s.rightPadPressure = ReadU16LE(buffer + 28);

    if (size >= 46) {
        s.hasImu = true;
        s.imuTimestamp = ReadU32LE(buffer + 30);
        s.accelX = ReadS16LE(buffer + 34);
        s.accelY = ReadS16LE(buffer + 36);
        s.accelZ = ReadS16LE(buffer + 38);
        s.gyroX = ReadS16LE(buffer + 40);
        s.gyroY = ReadS16LE(buffer + 42);
        s.gyroZ = ReadS16LE(buffer + 44);
    }

    state = s;
    return true;
}

bool SteamController::ParseBatteryReport(const uint8_t* buffer, size_t size, SteamControllerBatteryState& state) {
    if (!buffer || size < 3 || !IsBatteryReportId(buffer[0]))
        return false;

    SteamControllerBatteryState s{};
    s.valid = true;
    s.chargeState = buffer[1];
    s.levelPercent = buffer[2] > 100 ? 100 : buffer[2];
    state = s;
    return true;
}

bool SteamController::SetImuEnabled(bool enabled) {
    const uint16_t value = enabled ? IMU_MODE_RAW_ACCEL_GYRO : IMU_MODE_OFF;
    const uint8_t payload[] = {
        SETTING_IMU_MODE,
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
    };

    if (!SendSettingsPayload(payload, sizeof(payload))) {
        printf("Failed to %s IMU mode.\n", enabled ? "enable" : "disable");
        return false;
    }
    m_desiredImuEnabled.store(enabled, std::memory_order_relaxed);
    return true;
}

bool SteamController::SendSettingsPayload(const uint8_t* payload, uint8_t payloadSize) {
    uint8_t buf[64];
    BuildCmd(buf, CMD_SET_SETTINGS, payload, payloadSize);

    std::lock_guard<std::mutex> lock(m_writeMutex);
    return m_device.SendFeatureReport(buf, sizeof(buf));
}

void SteamController::SetRumble(uint8_t largeMotor, uint8_t smallMotor) {
    const uint16_t large = static_cast<uint16_t>(largeMotor) * 257u;
    const uint16_t small = static_cast<uint16_t>(smallMotor) * 257u;
    const auto now = std::chrono::steady_clock::now();
    RumbleFrame frame{};

    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);
        m_rumbleBaseLeft = large;
        m_rumbleBaseRight = small;
        m_rumbleBoostLeft = 0;
        m_rumbleBoostRight = 0;
        m_rumbleBoostUntil = {};
        m_lastRumbleSent = now;
        frame = CurrentRumbleFrameLocked(now);
    }

    SendRumbleOutput(frame.left, frame.right);
}

void SteamController::SetDs4EnhancedRumble(uint8_t largeMotor, uint8_t smallMotor) {
    const double low = MotorByteToUnit(largeMotor);
    const double high = MotorByteToUnit(smallMotor);

    const double leftDemand = ClampUnit(low * 1.00 + high * 0.30);
    const double rightDemand = ClampUnit(low * 0.78 + high * 0.88);
    const uint16_t baseLeft = UnitToHapticSpeed(leftDemand, 0x1200);
    const uint16_t baseRight = UnitToHapticSpeed(rightDemand, 0x1200);

    const auto now = std::chrono::steady_clock::now();
    RumbleFrame frame{};
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);

        const int largeRise = m_hasDs4RumbleState
            ? static_cast<int>(largeMotor) - static_cast<int>(m_lastDs4LargeMotor)
            : static_cast<int>(largeMotor);
        const int smallRise = m_hasDs4RumbleState
            ? static_cast<int>(smallMotor) - static_cast<int>(m_lastDs4SmallMotor)
            : static_cast<int>(smallMotor);

        m_lastDs4LargeMotor = largeMotor;
        m_lastDs4SmallMotor = smallMotor;
        m_hasDs4RumbleState = true;
        m_rumbleBaseLeft = baseLeft;
        m_rumbleBaseRight = baseRight;

        if (baseLeft == 0 && baseRight == 0) {
            m_rumbleBoostLeft = 0;
            m_rumbleBoostRight = 0;
            m_rumbleBoostUntil = {};
        } else {
            const double largePunch = (largeRise > 0 ? largeRise : 0) / 255.0 * 0.55;
            const double smallPunch = (smallRise > 0 ? smallRise : 0) / 255.0 * 0.38;
            const double punch = largePunch > smallPunch ? largePunch : smallPunch;

            if (punch >= 0.08) {
                const double boostLeft = ClampUnit(leftDemand + punch * 0.70 + low * 0.08);
                const double boostRight = ClampUnit(rightDemand + punch * 0.62 + high * 0.10);
                m_rumbleBoostLeft = UnitToHapticSpeed(boostLeft, 0x2200);
                m_rumbleBoostRight = UnitToHapticSpeed(boostRight, 0x2200);
                m_rumbleBoostUntil = now + std::chrono::milliseconds(55 + (largeMotor >= 180 ? 25 : 0));
            }
        }

        m_lastRumbleSent = now;
        frame = CurrentRumbleFrameLocked(now);
    }

    SendRumbleOutput(frame.left, frame.right);
}

uint16_t SteamController::DualSenseTriggerPulseSpeed(const std::array<uint8_t, 11>& effect,
                                                     uint8_t triggerPosition,
                                                     DualSenseTriggerRuntime& runtime,
                                                     bool& clickPulse) {
    clickPulse = false;

    const uint8_t mode = effect[0];
    if (mode == 0) {
        if (runtime.active)
            clickPulse = true;
        runtime = {};
        return 0;
    }

    uint8_t maxParam = 0;
    for (size_t i = 1; i < effect.size(); ++i)
        if (effect[i] > maxParam)
            maxParam = effect[i];

    uint8_t region = static_cast<uint8_t>(triggerPosition / 26);
    if (region > 9)
        region = 9;
    const uint8_t startParam = effect[1];
    const uint8_t endParam = effect[2];
    const uint8_t start = startParam <= 9
        ? static_cast<uint8_t>(startParam * 26 > 255 ? 255 : startParam * 26)
        : startParam;
    const uint8_t end = endParam <= 9
        ? static_cast<uint8_t>((endParam + 1) * 26 > 255 ? 255 : (endParam + 1) * 26)
        : endParam;

    bool active = triggerPosition >= start;
    uint8_t strength = 0;
    if (mode == 2 || mode == 0x02) {
        active = triggerPosition >= start && (end <= start || triggerPosition <= end);
        strength = effect[3] > maxParam ? effect[3] : maxParam;
    } else {
        strength = effect[1 + region];
        if (strength == 0)
            strength = effect[2] > maxParam ? effect[2] : maxParam;
    }
    if (strength == 0)
        strength = 96;

    if (mode != runtime.mode || active != runtime.active ||
        (active && region != runtime.region && (mode >= 3 || mode >= 0x20))) {
        clickPulse = active || runtime.active;
    }

    runtime.mode = mode;
    runtime.region = region;
    runtime.active = active;

    if (!active)
        return 0;

    const double unit = std::clamp(static_cast<double>(strength) / 255.0, 0.0, 1.0);
    return UnitToHapticSpeed(unit, 0x1400);
}

void SteamController::SetDualSenseHaptics(const SteamControllerDualSenseHaptics& haptics,
                                          uint8_t leftTriggerPosition,
                                          uint8_t rightTriggerPosition) {
    const double low = MotorByteToUnit(haptics.rumbleLeft);
    const double high = MotorByteToUnit(haptics.rumbleRight);

    bool leftTriggerClick = false;
    bool rightTriggerClick = false;
    RumbleFrame frame{};
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);

        const uint16_t leftTriggerSpeed = DualSenseTriggerPulseSpeed(
            haptics.leftTriggerEffect,
            leftTriggerPosition,
            m_dualSenseLeftTrigger,
            leftTriggerClick);
        const uint16_t rightTriggerSpeed = DualSenseTriggerPulseSpeed(
            haptics.rightTriggerEffect,
            rightTriggerPosition,
            m_dualSenseRightTrigger,
            rightTriggerClick);

        const double leftTriggerDemand = static_cast<double>(leftTriggerSpeed) / 65535.0;
        const double rightTriggerDemand = static_cast<double>(rightTriggerSpeed) / 65535.0;
        const double leftDemand = ClampUnit(low * 1.00 + high * 0.28 + leftTriggerDemand * 0.45);
        const double rightDemand = ClampUnit(low * 0.72 + high * 0.95 + rightTriggerDemand * 0.45);

        m_rumbleBaseLeft = UnitToHapticSpeed(leftDemand, 0x1000);
        m_rumbleBaseRight = UnitToHapticSpeed(rightDemand, 0x1000);

        const int leftRise = m_hasDualSenseRumbleState
            ? static_cast<int>(haptics.rumbleLeft) - static_cast<int>(m_lastDualSenseLeftRumble)
            : static_cast<int>(haptics.rumbleLeft);
        const int rightRise = m_hasDualSenseRumbleState
            ? static_cast<int>(haptics.rumbleRight) - static_cast<int>(m_lastDualSenseRightRumble)
            : static_cast<int>(haptics.rumbleRight);
        m_lastDualSenseLeftRumble = haptics.rumbleLeft;
        m_lastDualSenseRightRumble = haptics.rumbleRight;
        m_hasDualSenseRumbleState = true;

        int strongestRise = leftRise > rightRise ? leftRise : rightRise;
        if (strongestRise < 0)
            strongestRise = 0;
        if (strongestRise > 16 || leftTriggerClick || rightTriggerClick) {
            const double punch = ClampUnit(static_cast<double>(strongestRise) / 255.0 + 0.18);
            const uint16_t boostedLeft = UnitToHapticSpeed(leftDemand + punch * 0.45, 0x2200);
            const uint16_t boostedRight = UnitToHapticSpeed(rightDemand + punch * 0.45, 0x2200);
            m_rumbleBoostLeft = m_rumbleBaseLeft > boostedLeft ? m_rumbleBaseLeft : boostedLeft;
            m_rumbleBoostRight = m_rumbleBaseRight > boostedRight ? m_rumbleBaseRight : boostedRight;
            m_rumbleBoostUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(45);
        } else if (m_rumbleBaseLeft == 0 && m_rumbleBaseRight == 0) {
            m_rumbleBoostLeft = 0;
            m_rumbleBoostRight = 0;
            m_rumbleBoostUntil = {};
        }

        m_lastRumbleSent = std::chrono::steady_clock::now();
        frame = CurrentRumbleFrameLocked(m_lastRumbleSent);
    }

    if (leftTriggerClick)
        PulseTrackpadHaptic(true, true);
    if (rightTriggerClick)
        PulseTrackpadHaptic(false, true);
    SendRumbleOutput(frame.left, frame.right);
}

SteamController::RumbleFrame SteamController::CurrentRumbleFrameLocked(std::chrono::steady_clock::time_point now) const {
    RumbleFrame frame{m_rumbleBaseLeft, m_rumbleBaseRight};
    if (m_rumbleBoostUntil > now) {
        if (m_rumbleBoostLeft > frame.left)
            frame.left = m_rumbleBoostLeft;
        if (m_rumbleBoostRight > frame.right)
            frame.right = m_rumbleBoostRight;
    }
    return frame;
}

void SteamController::PulseTrackpadHaptic(bool left, bool strongClick) {
    const uint8_t side = left ? TRACKPAD_HAPTIC_OUTPUT_LEFT : TRACKPAD_HAPTIC_OUTPUT_RIGHT;
    const uint8_t command = strongClick ? HAPTIC_COMMAND_CLICK : HAPTIC_COMMAND_TICK;
    const int8_t gainDb = strongClick ? TRACKPAD_CLICK_COMMAND_GAIN_DB : TRACKPAD_TOUCH_GAIN_DB;
    SendTrackpadCommandOutput(side, command, gainDb);
    if (strongClick) {
        const uint8_t pad = left ? TRACKPAD_FEATURE_PAD_LEFT : TRACKPAD_FEATURE_PAD_RIGHT;
        SendTrackpadFeaturePulse(pad, TRACKPAD_CLICK_PULSE_US, 0, 1, TRACKPAD_CLICK_PULSE_GAIN_DB);
    }
}

void SteamController::ClearTrackpadHaptics() {
    // Haptic pulse reports are one-shots; there is no persistent trackpad state
    // to cancel here. Kept as a lifecycle hook for callers.
}

void SteamController::MaintainRumble() {
    RumbleFrame frame{};
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);
        const auto now = std::chrono::steady_clock::now();

        frame = CurrentRumbleFrameLocked(now);
        if (frame.left == 0 && frame.right == 0)
            return;

        if (now - m_lastRumbleSent < std::chrono::milliseconds(40))
            return;

        m_lastRumbleSent = now;
    }

    SendRumbleOutput(frame.left, frame.right);
}

bool SteamController::SendRumbleOutput(uint16_t leftSpeed, uint16_t rightSpeed) {
    if (!m_device.IsOpen())
        return false;

    uint8_t buf[10] = {};
    buf[0] = OUT_HAPTIC_RUMBLE;
    buf[1] = 0x00; // type
    WriteU16LE(buf + 2, 0x0000); // intensity
    WriteU16LE(buf + 4, leftSpeed);
    buf[6] = 0x00; // left gain
    WriteU16LE(buf + 7, rightSpeed);
    buf[9] = 0x00; // right gain

    std::lock_guard<std::mutex> lock(m_writeMutex);
    return m_device.SendOutputReport(buf, sizeof(buf));
}

bool SteamController::SendTrackpadFeaturePulse(uint8_t pad,
                                               uint16_t onUs,
                                               uint16_t offUs,
                                               uint16_t repeatCount,
                                               int16_t gainDb) {
    if (!m_device.IsOpen())
        return false;

    uint8_t payload[10] = {};
    payload[0] = pad;
    WriteU16LE(payload + 1, onUs);
    WriteU16LE(payload + 3, offUs);
    WriteU16LE(payload + 5, repeatCount);
    WriteU16LE(payload + 7, static_cast<uint16_t>(gainDb));
    payload[9] = HAPTIC_PULSE_HIGH_PRIORITY;

    uint8_t buf[64];
    BuildCmd(buf, CMD_TRIGGER_HAPTIC_PULSE, payload, sizeof(payload));

    std::lock_guard<std::mutex> lock(m_writeMutex);
    return m_device.SendFeatureReport(buf, sizeof(buf));
}

bool SteamController::SendTrackpadPulseOutput(uint8_t side,
                                              uint16_t onUs,
                                              uint16_t offUs,
                                              uint16_t repeatCount,
                                              int16_t gainDb) {
    if (!m_device.IsOpen())
        return false;

    uint8_t buf[10] = {};
    buf[0] = OUT_HAPTIC_PULSE;
    buf[1] = side;
    WriteU16LE(buf + 2, onUs);
    WriteU16LE(buf + 4, offUs);
    WriteU16LE(buf + 6, repeatCount);
    WriteU16LE(buf + 8, static_cast<uint16_t>(gainDb));

    std::lock_guard<std::mutex> lock(m_writeMutex);
    return m_device.SendOutputReport(buf, sizeof(buf));
}

bool SteamController::SendTrackpadCommandOutput(uint8_t side, uint8_t command, int8_t gainDb) {
    if (!m_device.IsOpen())
        return false;

    uint8_t buf[4] = {};
    buf[0] = OUT_HAPTIC_COMMAND;
    buf[1] = side;
    buf[2] = command;
    buf[3] = static_cast<uint8_t>(gainDb);

    std::lock_guard<std::mutex> lock(m_writeMutex);
    return m_device.SendOutputReport(buf, sizeof(buf));
}

// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------

void SteamController::HeartbeatLoop() {
    uint8_t clearMappings[64];
    BuildCmd(clearMappings, CMD_CLEAR_DIGITAL_MAPPINGS);

    while (m_running.load()) {
        const uint16_t imuMode = m_desiredImuEnabled.load(std::memory_order_relaxed)
            ? IMU_MODE_RAW_ACCEL_GYRO
            : IMU_MODE_OFF;
        const uint8_t settingsPayload[] = {
            SETTING_IMU_MODE,
            static_cast<uint8_t>(imuMode & 0xFF),
            static_cast<uint8_t>((imuMode >> 8) & 0xFF),
            SETTING_LEFT_TRACKPAD_MODE,  0x00, 0x00,
            SETTING_RIGHT_TRACKPAD_MODE, 0x00, 0x00,
        };
        uint8_t settings[64];
        BuildCmd(settings, CMD_SET_SETTINGS, settingsPayload, sizeof(settingsPayload));

        {
            std::lock_guard<std::mutex> lock(m_writeMutex);
            m_device.SendFeatureReport(clearMappings, sizeof(clearMappings));
            m_device.SendFeatureReport(settings, sizeof(settings));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }
}
