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

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool SteamController::Open() {
    for (uint16_t pid : { SC2026_PID, SC2026_DONGLE_PID }) {
        auto paths = HidDevice::Enumerate(VALVE_VID, pid, VENDOR_USAGE_PAGE);
        if (paths.empty()) continue;

        // For the wired controller there is only one interface; for the dongle
        // there are up to four slots (one per paired controller). Try each in
        // order and use the first that produces a live input report.
        for (auto const& path : paths) {
            if (!m_device.Open(path)) continue;

            uint8_t buf[64];
            size_t n = m_device.ReadInputReport(buf, sizeof(buf), /*timeoutMs=*/500);
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

    // Keep raw IMU disabled until the selected virtual output mode needs it.
    const uint8_t imuPayload[] = {
        SETTING_IMU_MODE, 0x00, 0x00,
    };
    BuildCmd(buf, CMD_SET_SETTINGS, imuPayload, sizeof(imuPayload));
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        if (!m_device.SendFeatureReport(buf, sizeof(buf))) {
            printf("Failed to disable IMU mode.\n");
            return false;
        }
    }

    // Step 2: SET_SETTINGS — set both trackpads to TRACKPAD_NONE.
    // Payload: pairs of [setting_id, val_lo, val_hi].
    const uint8_t settingsPayload[] = {
        SETTING_LEFT_TRACKPAD_MODE,  0x00, 0x00,
        SETTING_RIGHT_TRACKPAD_MODE, 0x00, 0x00,
    };
    BuildCmd(buf, CMD_SET_SETTINGS, settingsPayload, sizeof(settingsPayload));
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        if (!m_device.SendFeatureReport(buf, sizeof(buf))) {
            printf("Failed to send SET_SETTINGS_VALUES.\n");
            return false;
        }
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
    s.levelPercent = static_cast<uint8_t>(std::min<uint8_t>(buffer[2], 100));
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

    uint8_t buf[64];
    BuildCmd(buf, CMD_SET_SETTINGS, payload, sizeof(payload));

    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (!m_device.SendFeatureReport(buf, sizeof(buf))) {
        printf("Failed to %s IMU mode.\n", enabled ? "enable" : "disable");
        return false;
    }
    return true;
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
    const uint8_t side = left ? 0x01 : 0x02;
    const uint8_t command = strongClick ? HAPTIC_COMMAND_CLICK : HAPTIC_COMMAND_TICK;
    const int8_t gainDb = strongClick ? TRACKPAD_CLICK_COMMAND_GAIN_DB : TRACKPAD_TOUCH_GAIN_DB;
    SendTrackpadCommandOutput(side, command, gainDb);
    if (strongClick)
        SendTrackpadPulseOutput(side, TRACKPAD_CLICK_PULSE_US, 0, 1, TRACKPAD_CLICK_PULSE_GAIN_DB);
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
    uint8_t buf[64];
    BuildCmd(buf, CMD_CLEAR_DIGITAL_MAPPINGS);

    while (m_running.load()) {
        {
            std::lock_guard<std::mutex> lock(m_writeMutex);
            m_device.SendFeatureReport(buf, sizeof(buf));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }
}
