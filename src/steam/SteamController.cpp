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

static double AudioValueToUnit(uint16_t value, uint16_t floor) {
    if (value <= floor)
        return 0.0;
    return ClampUnit(static_cast<double>(value - floor) / static_cast<double>(0xFFFFu - floor));
}

static int16_t AudioPulseGainDb(double intensity) {
    intensity = std::pow(ClampUnit(intensity), 0.62);
    const double gain = -30.0 + intensity * 36.0;
    return static_cast<int16_t>(std::clamp<int>(static_cast<int>(std::lround(gain)), -30, 6));
}

static int8_t AudioPulseTickGainDb(double intensity) {
    intensity = std::pow(ClampUnit(intensity), 0.58);
    const double gain = -28.0 + intensity * 32.0;
    return static_cast<int8_t>(std::clamp<int>(static_cast<int>(std::lround(gain)), -28, 4));
}

static int8_t AudioPulseClickGainDb(double intensity) {
    intensity = std::pow(ClampUnit(intensity), 0.58);
    const double gain = -8.0 + intensity * 16.0;
    return static_cast<int8_t>(std::clamp<int>(static_cast<int>(std::lround(gain)), -8, 8));
}

static uint16_t AudioPulseOnUs(double intensity) {
    intensity = std::pow(ClampUnit(intensity), 0.78);
    const double onUs = 1100.0 + intensity * 5000.0;
    return static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::lround(onUs)), 1100, 6100));
}

static double AudioBodyRumbleUnit(double energy, double peak, double transient, double threshold) {
    const double body = ClampUnit(peak * 0.50 + transient * 0.34 + energy * 0.16);
    threshold = std::clamp(threshold,
                           SteamController::DUALSENSE_AUDIO_RUMBLE_THRESHOLD_MIN,
                           SteamController::DUALSENSE_AUDIO_RUMBLE_THRESHOLD_MAX);
    if (threshold >= 0.995 || body <= threshold)
        return 0.0;

    const double unit = threshold <= 0.0
        ? body
        : (body - threshold) / (1.0 - threshold);
    return ClampUnit(std::pow(unit, 1.35) * 0.46);
}

static double DualSenseTriggerEngagement(uint8_t triggerPosition) {
    static constexpr uint8_t kTriggerDeadzone = 16;
    static constexpr uint8_t kTriggerFullEngage = 72;
    if (triggerPosition <= kTriggerDeadzone)
        return 0.0;
    if (triggerPosition >= kTriggerFullEngage)
        return 1.0;

    const double unit = static_cast<double>(triggerPosition - kTriggerDeadzone)
                      / static_cast<double>(kTriggerFullEngage - kTriggerDeadzone);
    return std::pow(ClampUnit(unit), 0.65);
}

static bool DualSenseTriggerEffectIsContinuous(uint8_t mode) {
    // Known automatic/vibration trigger modes. Feedback/weapon/resistance
    // modes such as 0x21 and 0x25 are positional force curves and should not
    // keep buzzing once the trigger is held at a fixed depth.
    return mode == 0x06 || // Simple_Vibration
           mode == 0x26 || // Vibration
           mode == 0x27;   // Machine
}

static constexpr auto DUALSENSE_TRIGGER_TRANSIENT_TAIL = std::chrono::milliseconds(34);
static constexpr double DUALSENSE_TRIGGER_TRANSIENT_TAIL_SCALE = 0.42;
static constexpr uint8_t HAPTIC_COMMAND_TICK = 1;
static constexpr uint8_t HAPTIC_COMMAND_CLICK = 2;
static constexpr uint16_t TRACKPAD_CLICK_PULSE_US = 5000;
static constexpr int8_t TRACKPAD_TOUCH_GAIN_DB = -45;
static constexpr int8_t TRACKPAD_CLICK_COMMAND_GAIN_DB = 9;
static constexpr int16_t TRACKPAD_CLICK_PULSE_GAIN_DB = 40;
static constexpr uint8_t DS5_ENABLE_RUMBLE_EMULATION = 0x01;
static constexpr uint8_t DS5_ENABLE_RIGHT_TRIGGER_EFFECT = 0x04;
static constexpr uint8_t DS5_ENABLE_LEFT_TRIGGER_EFFECT = 0x08;
static constexpr uint8_t DS5_ENABLE_IMPROVED_RUMBLE = 0x04;
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
        m_dualSenseAudioLeft = 0;
        m_dualSenseAudioRight = 0;
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
        m_dualSenseAudioLeft = 0;
        m_dualSenseAudioRight = 0;

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
    const auto now = std::chrono::steady_clock::now();

    const uint8_t mode = effect[0];
    if (mode == 0) {
        if (runtime.active)
            clickPulse = true;
        runtime = {};
        return 0;
    }

    const double engagement = DualSenseTriggerEngagement(triggerPosition);
    if (engagement <= 0.0) {
        runtime.mode = mode;
        runtime.region = 0;
        runtime.position = triggerPosition;
        runtime.tailSpeed = 0;
        runtime.tailUntil = {};
        runtime.active = false;
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

    const bool continuous = DualSenseTriggerEffectIsContinuous(mode);
    const int pullDelta = static_cast<int>(triggerPosition) - static_cast<int>(runtime.position);
    const bool pullProgress = pullDelta >= 3;
    const bool regionProgress = region > runtime.region && pullDelta >= 0;

    if (mode != runtime.mode || active != runtime.active ||
        (active && region != runtime.region && continuous)) {
        clickPulse = active || runtime.active;
    }

    const bool transientPulse = active &&
                                !continuous &&
                                (mode != runtime.mode ||
                                 !runtime.active ||
                                 regionProgress ||
                                 pullProgress);
    runtime.mode = mode;
    runtime.region = region;
    runtime.position = triggerPosition;
    runtime.active = active;

    if (!active) {
        runtime.tailSpeed = 0;
        runtime.tailUntil = {};
        return 0;
    }

    const double unit = std::clamp(static_cast<double>(strength) / 255.0, 0.0, 1.0)
                      * engagement;
    const uint16_t speed = UnitToHapticSpeed(unit, 0x1400);
    if (!continuous && transientPulse) {
        runtime.tailSpeed = static_cast<uint16_t>(std::clamp<int>(
            static_cast<int>(std::lround(static_cast<double>(speed) * DUALSENSE_TRIGGER_TRANSIENT_TAIL_SCALE)),
            0,
            0xFFFF));
        runtime.tailUntil = now + DUALSENSE_TRIGGER_TRANSIENT_TAIL;
        return speed;
    }
    if (!continuous && !transientPulse) {
        if (runtime.tailSpeed != 0 && now < runtime.tailUntil)
            return runtime.tailSpeed;
        runtime.tailSpeed = 0;
        runtime.tailUntil = {};
        return 0;
    }
    return speed;
}

void SteamController::SetDualSenseHaptics(const SteamControllerDualSenseHaptics& haptics,
                                          uint8_t leftTriggerPosition,
                                          uint8_t rightTriggerPosition) {
    bool leftTriggerClick = false;
    bool rightTriggerClick = false;
    RumbleFrame frame{};
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);

        if ((haptics.enableBits1 & DS5_ENABLE_LEFT_TRIGGER_EFFECT) != 0)
            m_dualSenseLeftTriggerEffect = haptics.leftTriggerEffect;
        if ((haptics.enableBits1 & DS5_ENABLE_RIGHT_TRIGGER_EFFECT) != 0)
            m_dualSenseRightTriggerEffect = haptics.rightTriggerEffect;

        const uint16_t leftTriggerSpeed = DualSenseTriggerPulseSpeed(
            m_dualSenseLeftTriggerEffect,
            leftTriggerPosition,
            m_dualSenseLeftTrigger,
            leftTriggerClick);
        const uint16_t rightTriggerSpeed = DualSenseTriggerPulseSpeed(
            m_dualSenseRightTriggerEffect,
            rightTriggerPosition,
            m_dualSenseRightTrigger,
            rightTriggerClick);

        const double leftTriggerDemand = static_cast<double>(leftTriggerSpeed) / 65535.0;
        const double rightTriggerDemand = static_cast<double>(rightTriggerSpeed) / 65535.0;
        const bool hidRumbleEnabled =
            (haptics.enableBits1 & DS5_ENABLE_RUMBLE_EMULATION) != 0 ||
            (haptics.enableBits3 & DS5_ENABLE_IMPROVED_RUMBLE) != 0;
        const uint8_t rumbleLeft = hidRumbleEnabled ? haptics.rumbleLeft : 0;
        const uint8_t rumbleRight = hidRumbleEnabled ? haptics.rumbleRight : 0;
        const double low = MotorByteToUnit(rumbleLeft);
        const double high = MotorByteToUnit(rumbleRight);
        const double leftDemand = ClampUnit(low * 1.00 + high * 0.28 + leftTriggerDemand * 0.90);
        const double rightDemand = ClampUnit(low * 0.72 + high * 0.95 + rightTriggerDemand * 0.90);

        m_rumbleBaseLeft = UnitToHapticSpeed(leftDemand, 0x1000);
        m_rumbleBaseRight = UnitToHapticSpeed(rightDemand, 0x1000);

        const int leftRise = m_hasDualSenseRumbleState
            ? static_cast<int>(rumbleLeft) - static_cast<int>(m_lastDualSenseLeftRumble)
            : static_cast<int>(rumbleLeft);
        const int rightRise = m_hasDualSenseRumbleState
            ? static_cast<int>(rumbleRight) - static_cast<int>(m_lastDualSenseRightRumble)
            : static_cast<int>(rumbleRight);
        m_lastDualSenseLeftRumble = rumbleLeft;
        m_lastDualSenseRightRumble = rumbleRight;
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
            m_rumbleBoostUntil = now + std::chrono::milliseconds(45);
        } else if (m_rumbleBaseLeft == 0 && m_rumbleBaseRight == 0) {
            m_rumbleBoostLeft = 0;
            m_rumbleBoostRight = 0;
            m_rumbleBoostUntil = {};
        }

        m_lastRumbleSent = now;
        frame = CurrentRumbleFrameLocked(now);
    }

    if (leftTriggerClick)
        PulseTrackpadHaptic(true, true);
    if (rightTriggerClick)
        PulseTrackpadHaptic(false, true);
    SendRumbleOutput(frame.left, frame.right);
}

void SteamController::SetDualSenseAudioHaptics(const SteamControllerDualSenseAudioHaptics& haptics) {
    constexpr uint16_t kAudioHapticEnergyDeadzone = 500;
    constexpr uint16_t kAudioHapticPeakDeadzone = 700;
    constexpr uint16_t kAudioHapticTransientDeadzone = 250;

    const bool leftSilent = haptics.leftEnergy < kAudioHapticEnergyDeadzone &&
                            haptics.leftPeak < kAudioHapticPeakDeadzone &&
                            haptics.leftTransient < kAudioHapticTransientDeadzone;
    const bool rightSilent = haptics.rightEnergy < kAudioHapticEnergyDeadzone &&
                             haptics.rightPeak < kAudioHapticPeakDeadzone &&
                             haptics.rightTransient < kAudioHapticTransientDeadzone;
    if (leftSilent && rightSilent) {
        RumbleFrame frame{};
        bool hadAudio = false;
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(m_rumbleMutex);
            hadAudio = m_dualSenseAudioLeft != 0 || m_dualSenseAudioRight != 0;
            m_lastDualSenseAudioAt = now;
            if (!hadAudio)
                return;
            m_dualSenseAudioLeft = 0;
            m_dualSenseAudioRight = 0;
            frame = CurrentRumbleFrameLocked(now);
            m_lastRumbleSent = now;
        }
        SendRumbleOutput(frame.left, frame.right);
        return;
    }

    const double leftEnergy = AudioValueToUnit(haptics.leftEnergy, kAudioHapticEnergyDeadzone);
    const double rightEnergy = AudioValueToUnit(haptics.rightEnergy, kAudioHapticEnergyDeadzone);
    const double leftPeak = AudioValueToUnit(haptics.leftPeak, kAudioHapticPeakDeadzone);
    const double rightPeak = AudioValueToUnit(haptics.rightPeak, kAudioHapticPeakDeadzone);
    const double leftTransient = AudioValueToUnit(haptics.leftTransient, kAudioHapticTransientDeadzone);
    const double rightTransient = AudioValueToUnit(haptics.rightTransient, kAudioHapticTransientDeadzone);

    const double leftTexture = ClampUnit(leftEnergy * 0.60 + leftPeak * 0.25 + leftTransient * 0.15);
    const double rightTexture = ClampUnit(rightEnergy * 0.60 + rightPeak * 0.25 + rightTransient * 0.15);
    const double leftImpact = ClampUnit(leftTransient * 0.78 + leftPeak * 0.22);
    const double rightImpact = ClampUnit(rightTransient * 0.78 + rightPeak * 0.22);

    const auto now = std::chrono::steady_clock::now();

    bool pulseLeft = false;
    bool pulseRight = false;
    double pulseLeftIntensity = 0.0;
    double pulseRightIntensity = 0.0;
    RumbleFrame frame{};
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);

        // Keep texture on native pulses/ticks, but add body rumble for only
        // the strongest audio-haptic impacts so subtle effects stay clean.
        const double threshold = m_dualSenseAudioRumbleThreshold.load(std::memory_order_relaxed);
        const double leftBodyRumble = AudioBodyRumbleUnit(leftEnergy, leftPeak, leftTransient, threshold);
        const double rightBodyRumble = AudioBodyRumbleUnit(rightEnergy, rightPeak, rightTransient, threshold);
        m_dualSenseAudioLeft = leftBodyRumble > 0.0
            ? UnitToHapticSpeed(leftBodyRumble, 0x0E00)
            : 0;
        m_dualSenseAudioRight = rightBodyRumble > 0.0
            ? UnitToHapticSpeed(rightBodyRumble, 0x0E00)
            : 0;
        m_lastDualSenseAudioAt = now;

        pulseLeftIntensity = ClampUnit(std::pow(leftImpact, 0.55) * 0.68
                                     + std::pow(leftPeak, 0.68) * 0.20
                                     + std::pow(leftTexture, 0.82) * 0.12);
        pulseRightIntensity = ClampUnit(std::pow(rightImpact, 0.55) * 0.68
                                      + std::pow(rightPeak, 0.68) * 0.20
                                      + std::pow(rightTexture, 0.82) * 0.12);
        const bool leftPulseCandidate = pulseLeftIntensity > 0.018 || leftPeak > 0.020 || leftEnergy > 0.025;
        const bool rightPulseCandidate = pulseRightIntensity > 0.018 || rightPeak > 0.020 || rightEnergy > 0.025;
        const auto leftInterval = pulseLeftIntensity > 0.52
            ? std::chrono::milliseconds(22)
            : (pulseLeftIntensity > 0.12 ? std::chrono::milliseconds(44) : std::chrono::milliseconds(78));
        const auto rightInterval = pulseRightIntensity > 0.52
            ? std::chrono::milliseconds(22)
            : (pulseRightIntensity > 0.12 ? std::chrono::milliseconds(44) : std::chrono::milliseconds(78));
        if (leftPulseCandidate && now - m_lastDualSenseAudioLeftPulse >= leftInterval) {
            pulseLeft = true;
            m_lastDualSenseAudioLeftPulse = now;
        }
        if (rightPulseCandidate && now - m_lastDualSenseAudioRightPulse >= rightInterval) {
            pulseRight = true;
            m_lastDualSenseAudioRightPulse = now;
        }

        m_lastRumbleSent = now;
        frame = CurrentRumbleFrameLocked(now);
    }

    if (pulseLeft) {
        SendTrackpadPulseOutput(TRACKPAD_HAPTIC_OUTPUT_LEFT,
                                AudioPulseOnUs(pulseLeftIntensity),
                                0,
                                1,
                                AudioPulseGainDb(pulseLeftIntensity));
        SendTrackpadCommandOutput(TRACKPAD_HAPTIC_OUTPUT_LEFT,
                                  HAPTIC_COMMAND_TICK,
                                  AudioPulseTickGainDb(pulseLeftIntensity));
        if (pulseLeftIntensity > 0.50)
            SendTrackpadFeaturePulse(TRACKPAD_FEATURE_PAD_LEFT,
                                     AudioPulseOnUs(pulseLeftIntensity),
                                     0,
                                     1,
                                     AudioPulseGainDb(pulseLeftIntensity));
        if (pulseLeftIntensity > 0.58)
            SendTrackpadCommandOutput(TRACKPAD_HAPTIC_OUTPUT_LEFT,
                                      HAPTIC_COMMAND_CLICK,
                                      AudioPulseClickGainDb(pulseLeftIntensity));
    }
    if (pulseRight) {
        SendTrackpadPulseOutput(TRACKPAD_HAPTIC_OUTPUT_RIGHT,
                                AudioPulseOnUs(pulseRightIntensity),
                                0,
                                1,
                                AudioPulseGainDb(pulseRightIntensity));
        SendTrackpadCommandOutput(TRACKPAD_HAPTIC_OUTPUT_RIGHT,
                                  HAPTIC_COMMAND_TICK,
                                  AudioPulseTickGainDb(pulseRightIntensity));
        if (pulseRightIntensity > 0.50)
            SendTrackpadFeaturePulse(TRACKPAD_FEATURE_PAD_RIGHT,
                                     AudioPulseOnUs(pulseRightIntensity),
                                     0,
                                     1,
                                     AudioPulseGainDb(pulseRightIntensity));
        if (pulseRightIntensity > 0.58)
            SendTrackpadCommandOutput(TRACKPAD_HAPTIC_OUTPUT_RIGHT,
                                      HAPTIC_COMMAND_CLICK,
                                      AudioPulseClickGainDb(pulseRightIntensity));
    }
    SendRumbleOutput(frame.left, frame.right);
}

void SteamController::SetDualSenseAudioRumbleThreshold(double threshold) {
    m_dualSenseAudioRumbleThreshold.store(
        std::clamp(threshold,
                   DUALSENSE_AUDIO_RUMBLE_THRESHOLD_MIN,
                   DUALSENSE_AUDIO_RUMBLE_THRESHOLD_MAX),
        std::memory_order_relaxed);
}

double SteamController::GetDualSenseAudioRumbleThreshold() const {
    return m_dualSenseAudioRumbleThreshold.load(std::memory_order_relaxed);
}

void SteamController::ClearDualSenseHaptics() {
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);
        m_rumbleBaseLeft = 0;
        m_rumbleBaseRight = 0;
        m_rumbleBoostLeft = 0;
        m_rumbleBoostRight = 0;
        m_rumbleBoostUntil = {};
        m_dualSenseAudioLeft = 0;
        m_dualSenseAudioRight = 0;
        m_lastDualSenseLeftRumble = 0;
        m_lastDualSenseRightRumble = 0;
        m_hasDualSenseRumbleState = false;
        m_dualSenseLeftTrigger = {};
        m_dualSenseRightTrigger = {};
        m_dualSenseLeftTriggerEffect.fill(0);
        m_dualSenseRightTriggerEffect.fill(0);
        m_lastDualSenseAudioAt = {};
        m_lastRumbleSent = now;
    }
    SendRumbleOutput(0, 0);
}

SteamController::RumbleFrame SteamController::CurrentRumbleFrameLocked(std::chrono::steady_clock::time_point now) const {
    RumbleFrame frame{m_rumbleBaseLeft, m_rumbleBaseRight};
    if (m_dualSenseAudioLeft > frame.left)
        frame.left = m_dualSenseAudioLeft;
    if (m_dualSenseAudioRight > frame.right)
        frame.right = m_dualSenseAudioRight;
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

        if ((m_dualSenseAudioLeft != 0 || m_dualSenseAudioRight != 0) &&
            now - m_lastDualSenseAudioAt >= std::chrono::milliseconds(100)) {
            m_dualSenseAudioLeft = 0;
            m_dualSenseAudioRight = 0;
        }

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
