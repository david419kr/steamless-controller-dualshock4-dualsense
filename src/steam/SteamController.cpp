#include "SteamController.h"
#include <algorithm>
#include <chrono>
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

    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);
        m_rumbleLarge = large;
        m_rumbleSmall = small;
        m_lastRumbleSent = std::chrono::steady_clock::now();
    }

    SendRumbleOutput(large, small);
}

void SteamController::MaintainRumble() {
    uint16_t large = 0;
    uint16_t small = 0;
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);
        if (m_rumbleLarge == 0 && m_rumbleSmall == 0)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastRumbleSent < std::chrono::milliseconds(40))
            return;

        m_lastRumbleSent = now;
        large = m_rumbleLarge;
        small = m_rumbleSmall;
    }

    SendRumbleOutput(large, small);
}

bool SteamController::SendRumbleOutput(uint16_t largeMotor, uint16_t smallMotor) {
    if (!m_device.IsOpen())
        return false;

    uint8_t buf[10] = {};
    buf[0] = 0x80; // ID_OUT_REPORT_HAPTIC_RUMBLE
    buf[1] = 0x00; // type
    WriteU16LE(buf + 2, 0x0000); // intensity
    WriteU16LE(buf + 4, largeMotor);
    buf[6] = 0x00; // left gain
    WriteU16LE(buf + 7, smallMotor);
    buf[9] = 0x00; // right gain

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
