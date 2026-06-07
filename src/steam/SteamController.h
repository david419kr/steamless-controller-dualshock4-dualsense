#pragma once
#include "hid/HidDevice.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

struct SteamControllerState {
    uint8_t  reportId = 0;
    uint8_t  sequence = 0;
    uint32_t buttons = 0;

    int16_t  leftTrigger = 0;
    int16_t  rightTrigger = 0;
    int16_t  leftStickX = 0;
    int16_t  leftStickY = 0;
    int16_t  rightStickX = 0;
    int16_t  rightStickY = 0;

    int16_t  leftPadX = 0;
    int16_t  leftPadY = 0;
    uint16_t leftPadPressure = 0;
    int16_t  rightPadX = 0;
    int16_t  rightPadY = 0;
    uint16_t rightPadPressure = 0;

    bool     hasImu = false;
    uint32_t imuTimestamp = 0;
    int16_t  accelX = 0;
    int16_t  accelY = 0;
    int16_t  accelZ = 0;
    int16_t  gyroX = 0;
    int16_t  gyroY = 0;
    int16_t  gyroZ = 0;
};

struct SteamControllerBatteryState {
    bool     valid = false;
    uint8_t  levelPercent = 100;
    uint8_t  chargeState = 0;
};

struct SteamControllerDualSenseHaptics {
    uint8_t enableBits1 = 0;
    uint8_t enableBits2 = 0;
    uint8_t rumbleRight = 0;
    uint8_t rumbleLeft = 0;
    uint8_t enableBits3 = 0;
    std::array<uint8_t, 11> rightTriggerEffect{};
    std::array<uint8_t, 11> leftTriggerEffect{};
};

struct SteamControllerDualSenseAudioHaptics {
    uint32_t sequence = 0;
    uint16_t leftEnergy = 0;
    uint16_t rightEnergy = 0;
    uint16_t leftPeak = 0;
    uint16_t rightPeak = 0;
    uint16_t leftTransient = 0;
    uint16_t rightTransient = 0;
};

class SteamController {
public:
    static constexpr double DUALSENSE_AUDIO_RUMBLE_THRESHOLD_DEFAULT = 0.45;
    static constexpr double DUALSENSE_AUDIO_RUMBLE_THRESHOLD_MIN = 0.0;
    static constexpr double DUALSENSE_AUDIO_RUMBLE_THRESHOLD_MAX = 1.0;

    static constexpr uint16_t VALVE_VID        = 0x28DE;
    static constexpr uint16_t SC2026_PID       = 0x1302;  // wired USB
    static constexpr uint16_t SC2026_DONGLE_PID = 0x1304; // wireless dongle ("Steam Controller Puck")

    // HID Usage Page for the vendor collection that carries all game input.
    static constexpr uint16_t VENDOR_USAGE_PAGE = 0xFF00;

    // Input report IDs (device → host)
    static constexpr uint8_t REPORT_STATE         = 0x45;  // BLE/no-quaternion state report
    static constexpr uint8_t REPORT_STATE_LEGACY  = 0x42;  // USB/full state report
    static constexpr uint8_t REPORT_BATTERY_STATUS = 0x43;  // TritonBatteryStatus_t
    static constexpr uint8_t REPORT_STATUS         = 0x44;  //  5 bytes: battery / connection
    static constexpr uint8_t REPORT_UNKNOWN_7B     = 0x7B;  // 12 bytes: TBD
    static constexpr uint8_t REPORT_UNKNOWN_79     = 0x79;  //  1 byte:  TBD

    static constexpr uint8_t CHARGE_STATE_DISCHARGING  = 1;
    static constexpr uint8_t CHARGE_STATE_CHARGING     = 2;
    static constexpr uint8_t CHARGE_STATE_CHARGING_DONE = 4;

    // Feature report IDs — the command channel to the firmware.
    // Commands are wrapped inside Feature Report 0x01 (or 0x02 as fallback).
    // Buffer layout: [feature_report_id | cmd_byte | payload_size | payload...]
    static constexpr uint8_t FEATURE_REPORT_CMD  = 0x01;
    static constexpr uint8_t FEATURE_REPORT_CMD2 = 0x02;  // fallback if 0x01 fails

    // Command bytes (go in buffer[1] inside the feature report)
    static constexpr uint8_t CMD_SET_DIGITAL_MAPPINGS   = 0x80;
    static constexpr uint8_t CMD_CLEAR_DIGITAL_MAPPINGS = 0x81;  // ← lizard off
    static constexpr uint8_t CMD_GET_DIGITAL_MAPPINGS   = 0x82;
    static constexpr uint8_t CMD_SET_DEFAULT_MAPPINGS   = 0x85;  // ← lizard on
    static constexpr uint8_t CMD_SET_SETTINGS           = 0x87;
    static constexpr uint8_t CMD_GET_SETTINGS           = 0x89;
    static constexpr uint8_t CMD_TRIGGER_HAPTIC_PULSE   = 0x8F;

    // Triton output report IDs.
    static constexpr uint8_t OUT_HAPTIC_RUMBLE          = 0x80;
    static constexpr uint8_t OUT_HAPTIC_PULSE           = 0x81;
    static constexpr uint8_t OUT_HAPTIC_COMMAND         = 0x82;

    // Setting key IDs (go in the payload of CMD_SET_SETTINGS)
    static constexpr uint8_t SETTING_RIGHT_TRACKPAD_MODE = 0x07;
    static constexpr uint8_t SETTING_LEFT_TRACKPAD_MODE  = 0x08;
    static constexpr uint8_t SETTING_IMU_MODE            = 0x30;
    static constexpr uint8_t TRACKPAD_NONE               = 0x00;
    static constexpr uint16_t IMU_MODE_OFF               = 0x0000;
    static constexpr uint16_t IMU_MODE_RAW_ACCEL_GYRO    = 0x0018;

    // ---------------------------------------------------------------------------
    // Input report layout — 0x45 STATE report (buf[0] = 0x45)
    // ---------------------------------------------------------------------------

    // buf[01]       — 8-bit sequence counter (wraps 0xFF → 0x00)

    // buf[02]       — button bitmask byte 0
    static constexpr uint8_t BTN_A        = 0x01;  // bit 0
    static constexpr uint8_t BTN_B        = 0x02;  // bit 1
    static constexpr uint8_t BTN_X        = 0x04;  // bit 2
    static constexpr uint8_t BTN_Y        = 0x08;  // bit 3
    // bit 4 (0x10): TBD
    static constexpr uint8_t BTN_RS       = 0x20;  // bit 5 — right stick click
    static constexpr uint8_t BTN_MENU     = 0x40;  // bit 6 — ≡ Menu / Start
    static constexpr uint8_t BTN_R4       = 0x80;  // bit 7 — back paddle R4

    // buf[03]       — button bitmask byte 1
    static constexpr uint8_t BTN_R5       = 0x01;  // bit 0 — back paddle R5
    static constexpr uint8_t BTN_RB       = 0x02;  // bit 1
    static constexpr uint8_t BTN_DPAD_DN  = 0x04;  // bit 2
    static constexpr uint8_t BTN_DPAD_RT  = 0x08;  // bit 3
    static constexpr uint8_t BTN_DPAD_LT  = 0x10;  // bit 4
    static constexpr uint8_t BTN_DPAD_UP  = 0x20;  // bit 5
    static constexpr uint8_t BTN_VIEW     = 0x40;  // bit 6 — ⧉ View / Back
    static constexpr uint8_t BTN_LS       = 0x80;  // bit 7 — left stick click

    // buf[04]       — button bitmask byte 2
    static constexpr uint8_t BTN_STEAM    = 0x01;  // bit 0 — Steam / Guide
    static constexpr uint8_t BTN_L4       = 0x02;  // bit 1 — back paddle L4
    static constexpr uint8_t BTN_L5       = 0x04;  // bit 2 — back paddle L5
    static constexpr uint8_t BTN_LB       = 0x08;  // bit 3
    static constexpr uint8_t BTN_RS_TOUCH  = 0x10;  // bit 4 — right stick capacitive touch
    static constexpr uint8_t BTN_TP_RT    = 0x20;  // bit 5 — right trackpad active (touch or click)
    static constexpr uint8_t BTN_TP_RT_CLICK = 0x40; // bit 6 — right trackpad hard press
    static constexpr uint8_t BTN_RT_FULL  = 0x80;  // bit 7 — right trigger fully pressed (digital threshold)

    // buf[05]       — flags byte
    static constexpr uint8_t BTN_LS_TOUCH    = 0x01;  // bit 0 — left stick capacitive touch
    static constexpr uint8_t BTN_TP_LT      = 0x02;  // bit 1 — left trackpad active (touch or click)
    static constexpr uint8_t BTN_TP_LT_CLICK = 0x04; // bit 2 — left trackpad hard press
    // bit 3 (0x08): TBD
    static constexpr uint8_t FLAG_GRIP_RT = 0x10;  // bit 4 — right grip sensor active
    static constexpr uint8_t FLAG_GRIP_LT = 0x20;  // bit 5 — left grip sensor active
    // other bits TBD

    // buf[06..07]   — left trigger,  16-bit LE signed, 0x0000 (released) – 0x7FFF (full)
    // buf[08..09]   — right trigger, 16-bit LE signed, 0x0000 (released) – 0x7FFF (full)

    // buf[10..11]   — left joystick X,  16-bit LE signed; center ≈ 0x0000, +0x7FFF = right, −0x8000 = left
    // buf[12..13]   — left joystick Y,  16-bit LE signed; center ≈ 0x0000, +0x7FFF = up, −0x8000 = down
    // buf[14..15]   — right joystick X, 16-bit LE signed; center ≈ 0x0000, +0x7FFF = right, −0x8000 = left
    // buf[16..17]   — right joystick Y, 16-bit LE signed; center ≈ 0x0000, +0x7FFF = up, −0x8000 = down

    // buf[18..19]   — left trackpad X, 16-bit LE signed (0x0000 when not touching)
    // buf[20..21]   — left trackpad Y, 16-bit LE signed (0x0000 when not touching)
    // buf[22..23]   — left trackpad contact area, 16-bit LE (0 = no contact; higher = more area/pressure)

    // buf[24..25]   — right trackpad X, 16-bit LE signed (0x0000 when not touching)
    // buf[26..27]   — right trackpad Y, 16-bit LE signed (0x0000 when not touching)
    // buf[28..29]   — right trackpad contact area, 16-bit LE (0 = no contact; higher = more area/pressure)

    // buf[30..31]   — 0x03 0x46 constant (firmware info?)
    // buf[32..39]   — IMU quaternion (4× 16-bit LE, little-endian)
    // buf[40..43]   — unknown / always zero
    // buf[44..45]   — 0xFF 0xFF at full charge (battery level)
    // buf[46..53]   — gyro at rest (constant when stationary)

    // ---------------------------------------------------------------------------

    SteamController() = default;
    ~SteamController() { Close(); }
    SteamController(const SteamController&) = delete;
    SteamController& operator=(const SteamController&) = delete;

    // Finds and opens the vendor HID interface. Returns false if not found.
    bool Open(uint32_t activeReportTimeoutMs = 500);
    void Close();
    bool IsOpen() const { return m_device.IsOpen(); }

    // Two-step sequence: clears digital mappings + sets trackpads to NONE.
    // Starts the background heartbeat thread on first call.
    bool DisableLizardMode();

    // Restores default mappings. Should be called before process exit.
    bool EnableLizardMode();

    // Read the next raw input report. buffer[0] = report ID on return.
    // Returns 0 on timeout.
    size_t ReadReport(uint8_t* buffer, size_t size, uint32_t timeoutMs = 16);

    static bool IsStateReportId(uint8_t reportId);
    static bool ParseStateReport(const uint8_t* buffer, size_t size, SteamControllerState& state);
    static bool IsBatteryReportId(uint8_t reportId);
    static bool ParseBatteryReport(const uint8_t* buffer, size_t size, SteamControllerBatteryState& state);

    bool SetImuEnabled(bool enabled);
    void SetRumble(uint8_t largeMotor, uint8_t smallMotor);
    void SetDs4EnhancedRumble(uint8_t largeMotor, uint8_t smallMotor);
    void SetDualSenseHaptics(const SteamControllerDualSenseHaptics& haptics,
                             uint8_t leftTriggerPosition,
                             uint8_t rightTriggerPosition);
    void SetDualSenseAudioHaptics(const SteamControllerDualSenseAudioHaptics& haptics);
    void SetDualSenseAudioRumbleThreshold(double threshold);
    double GetDualSenseAudioRumbleThreshold() const;
    void ClearDualSenseHaptics();
    void PulseTrackpadHaptic(bool left, bool strongClick);
    void ClearTrackpadHaptics();
    void MaintainRumble();

private:
    struct RumbleFrame {
        uint16_t left = 0;
        uint16_t right = 0;
    };

    void HeartbeatLoop();
    bool SendSettingsPayload(const uint8_t* payload, uint8_t payloadSize);
    RumbleFrame CurrentRumbleFrameLocked(std::chrono::steady_clock::time_point now) const;
    void NoteRumbleOutputLocked(const RumbleFrame& frame, std::chrono::steady_clock::time_point now);
    bool ShouldSendRumbleOutputLocked(const RumbleFrame& frame,
                                      std::chrono::steady_clock::time_point now,
                                      std::chrono::milliseconds maxInterval,
                                      uint16_t minDelta);
    bool SendRumbleOutput(uint16_t leftSpeed, uint16_t rightSpeed);
    bool SendTrackpadFeaturePulse(uint8_t pad, uint16_t onUs, uint16_t offUs, uint16_t repeatCount, int16_t gainDb);
    bool SendTrackpadPulseOutput(uint8_t side, uint16_t onUs, uint16_t offUs, uint16_t repeatCount, int16_t gainDb);
    bool SendTrackpadCommandOutput(uint8_t side, uint8_t command, int8_t gainDb);

    struct DualSenseTriggerRuntime {
        uint8_t mode = 0;
        uint8_t region = 0;
        uint8_t position = 0;
        uint16_t tailSpeed = 0;
        std::chrono::steady_clock::time_point tailUntil{};
        bool active = false;
    };

    static uint16_t DualSenseTriggerPulseSpeed(const std::array<uint8_t, 11>& effect,
                                               uint8_t triggerPosition,
                                               DualSenseTriggerRuntime& runtime,
                                               bool& clickPulse);

    HidDevice       m_device;
    std::thread     m_heartbeat;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_desiredImuEnabled{false};
    std::mutex      m_writeMutex;
    std::mutex      m_rumbleMutex;
    uint16_t        m_rumbleBaseLeft = 0;
    uint16_t        m_rumbleBaseRight = 0;
    uint16_t        m_rumbleBoostLeft = 0;
    uint16_t        m_rumbleBoostRight = 0;
    std::chrono::steady_clock::time_point m_rumbleBoostUntil{};
    std::chrono::steady_clock::time_point m_lastRumbleSent{};
    uint16_t        m_lastRumbleOutputLeft = 0;
    uint16_t        m_lastRumbleOutputRight = 0;
    bool            m_hasRumbleOutput = false;
    uint8_t         m_lastDs4LargeMotor = 0;
    uint8_t         m_lastDs4SmallMotor = 0;
    bool            m_hasDs4RumbleState = false;
    uint8_t         m_lastDualSenseLeftRumble = 0;
    uint8_t         m_lastDualSenseRightRumble = 0;
    bool            m_hasDualSenseRumbleState = false;
    DualSenseTriggerRuntime m_dualSenseLeftTrigger;
    DualSenseTriggerRuntime m_dualSenseRightTrigger;
    std::array<uint8_t, 11> m_dualSenseLeftTriggerEffect{};
    std::array<uint8_t, 11> m_dualSenseRightTriggerEffect{};
    std::atomic<double> m_dualSenseAudioRumbleThreshold{DUALSENSE_AUDIO_RUMBLE_THRESHOLD_DEFAULT};
    uint16_t        m_dualSenseAudioLeft = 0;
    uint16_t        m_dualSenseAudioRight = 0;
    std::chrono::steady_clock::time_point m_lastDualSenseAudioAt{};
    std::chrono::steady_clock::time_point m_lastDualSenseAudioLeftPulse{};
    std::chrono::steady_clock::time_point m_lastDualSenseAudioRightPulse{};
};
