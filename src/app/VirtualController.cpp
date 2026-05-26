#include "VirtualController.h"
#include "steam/SteamController.h"
#include <ViGEm/Client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>

static_assert(sizeof(DS4_REPORT_EX) == 63, "DS4_REPORT_EX must match the DS4 USB input report payload size");

static uint8_t ButtonByte(const SteamControllerState& state, int index) {
    return static_cast<uint8_t>((state.buttons >> (index * 8)) & 0xFF);
}

static uint8_t TriggerToByte(int16_t raw) {
    return static_cast<uint8_t>(std::clamp<int>(raw >> 7, 0, 255));
}

static uint8_t AxisToDs4(int16_t raw, bool invert) {
    int v = raw;
    if (invert)
        v = (v == -32768) ? 32767 : -v;
    return static_cast<uint8_t>(std::clamp<int>((v + 32768) >> 8, 0, 255));
}

static int16_t NegI16(int16_t raw) {
    return static_cast<int16_t>((raw == -32768) ? 32767 : -raw);
}

static uint8_t PercentToDs4BatteryLevel(uint8_t percent, uint8_t chargeState) {
    if (chargeState == SteamController::CHARGE_STATE_CHARGING_DONE || percent >= 95)
        return 0x0B;

    const int level = (static_cast<int>(percent) * 10 + 50) / 100;
    return static_cast<uint8_t>(std::clamp(level, 0, 10));
}

static int NormalizePadAxis(int16_t raw, int maxValue, bool invert) {
    int v = raw;
    if (invert)
        v = (v == -32768) ? 32767 : -v;

    const int normalized = static_cast<int>(
        ((static_cast<int64_t>(v) + 32768) * maxValue) / 65535);
    return std::clamp(normalized, 0, maxValue);
}

static void PackDs4Touch(uint8_t data[3], int x, int y) {
    data[0] = static_cast<uint8_t>(x & 0xFF);
    data[1] = static_cast<uint8_t>(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
    data[2] = static_cast<uint8_t>((y >> 4) & 0xFF);
}

static DS4_DPAD_DIRECTIONS DpadDirection(const SteamControllerState& state) {
    const uint8_t b1 = ButtonByte(state, 1);
    const bool up = (b1 & SteamController::BTN_DPAD_UP) != 0;
    const bool down = (b1 & SteamController::BTN_DPAD_DN) != 0;
    const bool left = (b1 & SteamController::BTN_DPAD_LT) != 0;
    const bool right = (b1 & SteamController::BTN_DPAD_RT) != 0;

    if (up && right) return DS4_BUTTON_DPAD_NORTHEAST;
    if (right && down) return DS4_BUTTON_DPAD_SOUTHEAST;
    if (down && left) return DS4_BUTTON_DPAD_SOUTHWEST;
    if (left && up) return DS4_BUTTON_DPAD_NORTHWEST;
    if (up) return DS4_BUTTON_DPAD_NORTH;
    if (right) return DS4_BUTTON_DPAD_EAST;
    if (down) return DS4_BUTTON_DPAD_SOUTH;
    if (left) return DS4_BUTTON_DPAD_WEST;
    return DS4_BUTTON_DPAD_NONE;
}

static DS4_DPAD_DIRECTIONS TrackpadDpadDirection(int16_t x, int16_t y, bool active) {
    if (!active)
        return DS4_BUTTON_DPAD_NONE;

    const int ix = static_cast<int>(x);
    const int iy = static_cast<int>(y);
    const int ax = ix < 0 ? -ix : ix;
    const int ay = iy < 0 ? -iy : iy;
    constexpr int DEADZONE = 9000;

    if (ax < DEADZONE && ay < DEADZONE)
        return DS4_BUTTON_DPAD_NONE;

    const bool east = ix > DEADZONE;
    const bool west = ix < -DEADZONE;
    const bool north = iy > DEADZONE;
    const bool south = iy < -DEADZONE;

    if (north && east) return DS4_BUTTON_DPAD_NORTHEAST;
    if (east && south) return DS4_BUTTON_DPAD_SOUTHEAST;
    if (south && west) return DS4_BUTTON_DPAD_SOUTHWEST;
    if (west && north) return DS4_BUTTON_DPAD_NORTHWEST;
    if (north) return DS4_BUTTON_DPAD_NORTH;
    if (east) return DS4_BUTTON_DPAD_EAST;
    if (south) return DS4_BUTTON_DPAD_SOUTH;
    if (west) return DS4_BUTTON_DPAD_WEST;
    return ax >= ay
        ? (ix >= 0 ? DS4_BUTTON_DPAD_EAST : DS4_BUTTON_DPAD_WEST)
        : (iy >= 0 ? DS4_BUTTON_DPAD_NORTH : DS4_BUTTON_DPAD_SOUTH);
}

static USHORT XusbDpadButtons(DS4_DPAD_DIRECTIONS direction) {
    switch (direction) {
    case DS4_BUTTON_DPAD_NORTH:
        return XUSB_GAMEPAD_DPAD_UP;
    case DS4_BUTTON_DPAD_NORTHEAST:
        return XUSB_GAMEPAD_DPAD_UP | XUSB_GAMEPAD_DPAD_RIGHT;
    case DS4_BUTTON_DPAD_EAST:
        return XUSB_GAMEPAD_DPAD_RIGHT;
    case DS4_BUTTON_DPAD_SOUTHEAST:
        return XUSB_GAMEPAD_DPAD_DOWN | XUSB_GAMEPAD_DPAD_RIGHT;
    case DS4_BUTTON_DPAD_SOUTH:
        return XUSB_GAMEPAD_DPAD_DOWN;
    case DS4_BUTTON_DPAD_SOUTHWEST:
        return XUSB_GAMEPAD_DPAD_DOWN | XUSB_GAMEPAD_DPAD_LEFT;
    case DS4_BUTTON_DPAD_WEST:
        return XUSB_GAMEPAD_DPAD_LEFT;
    case DS4_BUTTON_DPAD_NORTHWEST:
        return XUSB_GAMEPAD_DPAD_UP | XUSB_GAMEPAD_DPAD_LEFT;
    default:
        return 0;
    }
}

static bool ParseDs4OutputRumble(const DS4_OUTPUT_BUFFER& output,
                                 uint8_t& largeMotor,
                                 uint8_t& smallMotor) {
    const uint8_t* b = output.Buffer;

    if (b[0] == 0x05) {
        smallMotor = b[3];
        largeMotor = b[4];
        return true;
    }

    if (b[0] == 0x11) {
        smallMotor = b[6];
        largeMotor = b[7];
        return true;
    }

    return false;
}

static XUSB_REPORT TranslateXusb(const SteamControllerState& state,
                                  bool trackpadDpadEnabled,
                                  bool useRightTrackpadForDpad) {
    XUSB_REPORT r{};

    const uint8_t b0 = ButtonByte(state, 0);
    const uint8_t b1 = ButtonByte(state, 1);
    const uint8_t b2 = ButtonByte(state, 2);
    const uint8_t b3 = ButtonByte(state, 3);

    if (b0 & SteamController::BTN_A) r.wButtons |= XUSB_GAMEPAD_A;
    if (b0 & SteamController::BTN_B) r.wButtons |= XUSB_GAMEPAD_B;
    if (b0 & SteamController::BTN_X) r.wButtons |= XUSB_GAMEPAD_X;
    if (b0 & SteamController::BTN_Y) r.wButtons |= XUSB_GAMEPAD_Y;

    if (b2 & SteamController::BTN_LB) r.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (b1 & SteamController::BTN_RB) r.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;

    if (b0 & SteamController::BTN_MENU) r.wButtons |= XUSB_GAMEPAD_START;
    if (b1 & SteamController::BTN_VIEW) r.wButtons |= XUSB_GAMEPAD_BACK;

    if (b1 & SteamController::BTN_LS) r.wButtons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (b0 & SteamController::BTN_RS) r.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB;

    if (b2 & SteamController::BTN_STEAM) r.wButtons |= XUSB_GAMEPAD_GUIDE;

    if (b1 & SteamController::BTN_DPAD_UP) r.wButtons |= XUSB_GAMEPAD_DPAD_UP;
    if (b1 & SteamController::BTN_DPAD_DN) r.wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (b1 & SteamController::BTN_DPAD_LT) r.wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (b1 & SteamController::BTN_DPAD_RT) r.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;

    if (trackpadDpadEnabled) {
        const bool rawRightClick = (b2 & 0x40) != 0;
        const bool rawLeftClick = (b3 & SteamController::BTN_TP_LT_CLICK) != 0;
        const DS4_DPAD_DIRECTIONS trackpadDpad = useRightTrackpadForDpad
            ? TrackpadDpadDirection(state.rightPadX, state.rightPadY, rawRightClick)
            : TrackpadDpadDirection(state.leftPadX, state.leftPadY, rawLeftClick);
        const USHORT xusbDpad = XusbDpadButtons(trackpadDpad);
        if (xusbDpad != 0) {
            r.wButtons &= static_cast<USHORT>(~(XUSB_GAMEPAD_DPAD_UP |
                                                XUSB_GAMEPAD_DPAD_DOWN |
                                                XUSB_GAMEPAD_DPAD_LEFT |
                                                XUSB_GAMEPAD_DPAD_RIGHT));
            r.wButtons |= xusbDpad;
        }
    }

    r.bLeftTrigger = TriggerToByte(state.leftTrigger);
    r.bRightTrigger = TriggerToByte(state.rightTrigger);
    r.sThumbLX = state.leftStickX;
    r.sThumbLY = state.leftStickY;
    r.sThumbRX = state.rightStickX;
    r.sThumbRY = state.rightStickY;

    return r;
}

static VOID CALLBACK X360Notification(
    PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR largeMotor, UCHAR smallMotor, UCHAR, LPVOID userData) {
    auto* self = static_cast<VirtualController*>(userData);
    if (self) self->OnRumble(largeMotor, smallMotor);
}

VirtualController::VirtualController(VirtualControllerMode mode, RumbleFn rumbleFn)
    : m_mode(mode), m_rumbleFn(std::move(rumbleFn)) {
    m_client = vigem_alloc();
    if (!m_client) {
        printf("[ViGEm] alloc failed\n");
        return;
    }

    VIGEM_ERROR err = vigem_connect(static_cast<PVIGEM_CLIENT>(m_client));
    if (!VIGEM_SUCCESS(err)) {
        if (err == VIGEM_ERROR_BUS_NOT_FOUND || err == VIGEM_ERROR_BUS_ACCESS_FAILED)
            m_driverMissing = true;
        vigem_free(static_cast<PVIGEM_CLIENT>(m_client));
        m_client = nullptr;
        return;
    }

    m_target = (m_mode == VirtualControllerMode::DualShock4)
        ? vigem_target_ds4_alloc()
        : vigem_target_x360_alloc();
    if (!m_target) {
        printf("[ViGEm] target alloc failed\n");
        return;
    }

    err = vigem_target_add(static_cast<PVIGEM_CLIENT>(m_client),
                           static_cast<PVIGEM_TARGET>(m_target));
    if (!VIGEM_SUCCESS(err)) {
        printf("[ViGEm] target_add failed: 0x%08X\n", err);
        vigem_target_free(static_cast<PVIGEM_TARGET>(m_target));
        m_target = nullptr;
        return;
    }

    if (m_mode == VirtualControllerMode::DualShock4) {
        err = VIGEM_ERROR_NONE;
        StartDs4OutputThread();
    } else {
        err = vigem_target_x360_register_notification(
            static_cast<PVIGEM_CLIENT>(m_client),
            static_cast<PVIGEM_TARGET>(m_target),
            X360Notification,
            this);
    }

    if (!VIGEM_SUCCESS(err)) {
        printf("[ViGEm] notification registration failed: 0x%08X\n", err);
        vigem_target_remove(static_cast<PVIGEM_CLIENT>(m_client),
                            static_cast<PVIGEM_TARGET>(m_target));
        vigem_target_free(static_cast<PVIGEM_TARGET>(m_target));
        m_target = nullptr;
        return;
    }

    printf("[ViGEm] Virtual %s controller connected\n",
           m_mode == VirtualControllerMode::DualShock4 ? "DualShock 4" : "Xbox 360");
    m_valid = true;
}

VirtualController::~VirtualController() {
    StopDs4OutputThread();

    if (m_target && m_mode == VirtualControllerMode::Xbox360) {
        vigem_target_x360_unregister_notification(static_cast<PVIGEM_TARGET>(m_target));
    }

    if (m_client && m_target) {
        vigem_target_remove(static_cast<PVIGEM_CLIENT>(m_client),
                            static_cast<PVIGEM_TARGET>(m_target));
    }
    if (m_target) vigem_target_free(static_cast<PVIGEM_TARGET>(m_target));
    if (m_client) {
        vigem_disconnect(static_cast<PVIGEM_CLIENT>(m_client));
        vigem_free(static_cast<PVIGEM_CLIENT>(m_client));
    }
}

void VirtualController::OnRumble(uint8_t largeMotor, uint8_t smallMotor) {
    if (m_rumbleFn)
        m_rumbleFn(largeMotor, smallMotor);
}

void VirtualController::StartDs4OutputThread() {
    if (m_ds4OutputRunning.exchange(true))
        return;
    m_ds4OutputThread = std::thread(&VirtualController::Ds4OutputLoop, this);
}

void VirtualController::StopDs4OutputThread() {
    m_ds4OutputRunning = false;
    if (m_ds4OutputThread.joinable())
        m_ds4OutputThread.join();
}

void VirtualController::Ds4OutputLoop() {
    while (m_ds4OutputRunning && m_client && m_target) {
        DS4_OUTPUT_BUFFER output{};
        VIGEM_ERROR err = vigem_target_ds4_await_output_report_timeout(
            static_cast<PVIGEM_CLIENT>(m_client),
            static_cast<PVIGEM_TARGET>(m_target),
            100,
            &output);

        if (!m_ds4OutputRunning)
            break;

        if (err == VIGEM_ERROR_TIMED_OUT)
            continue;

        if (!VIGEM_SUCCESS(err)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        uint8_t largeMotor = 0;
        uint8_t smallMotor = 0;
        if (ParseDs4OutputRumble(output, largeMotor, smallMotor))
            OnRumble(largeMotor, smallMotor);
    }
}

void VirtualController::SetBatteryState(uint8_t levelPercent, uint8_t chargeState) {
    m_ds4BatteryLevel = PercentToDs4BatteryLevel(levelPercent, chargeState);
    m_ds4BatterySpecial = static_cast<uint8_t>(0x10 | m_ds4BatteryLevel);
}

void VirtualController::SetTrackpadMouseClaim(bool enabled, bool useLeftTrackpad) {
    m_trackpadMouseEnabled = enabled;
    m_useLeftTrackpadForMouse = useLeftTrackpad;
}

void VirtualController::SetTrackpadDpadClaim(bool enabled, bool useRightTrackpad) {
    m_trackpadDpadEnabled = enabled;
    m_useRightTrackpadForDpad = useRightTrackpad;
}

void VirtualController::Update(const SteamControllerState& state) {
    if (!m_valid) return;

    if (m_mode == VirtualControllerMode::DualShock4) {
        DS4_REPORT_EX report{};
        report.Report.bThumbLX = AxisToDs4(state.leftStickX, false);
        report.Report.bThumbLY = AxisToDs4(state.leftStickY, true);
        report.Report.bThumbRX = AxisToDs4(state.rightStickX, false);
        report.Report.bThumbRY = AxisToDs4(state.rightStickY, true);
        report.Report.wButtons = static_cast<USHORT>(DS4_BUTTON_DPAD_NONE);
        report.Report.bTriggerL = TriggerToByte(state.leftTrigger);
        report.Report.bTriggerR = TriggerToByte(state.rightTrigger);
        report.Report.bBatteryLvl = m_ds4BatteryLevel;
        report.Report.bBatteryLvlSpecial = m_ds4BatterySpecial;

        const uint8_t b0 = ButtonByte(state, 0);
        const uint8_t b1 = ButtonByte(state, 1);
        const uint8_t b2 = ButtonByte(state, 2);
        const uint8_t b3 = ButtonByte(state, 3);

        const bool rawRightTouching = (b2 & SteamController::BTN_TP_RT) != 0;
        const bool rawLeftTouching = (b3 & SteamController::BTN_TP_LT) != 0;
        const bool rawRightClick = (b2 & 0x40) != 0;
        const bool rawLeftClick = (b3 & SteamController::BTN_TP_LT_CLICK) != 0;

        DS4_DPAD_DIRECTIONS dpad = DpadDirection(state);
        if (m_trackpadDpadEnabled) {
            const bool useRight = m_useRightTrackpadForDpad;
            const DS4_DPAD_DIRECTIONS trackpadDpad = useRight
                ? TrackpadDpadDirection(state.rightPadX, state.rightPadY, rawRightClick)
                : TrackpadDpadDirection(state.leftPadX, state.leftPadY, rawLeftClick);
            if (trackpadDpad != DS4_BUTTON_DPAD_NONE)
                dpad = trackpadDpad;
        }

        report.Report.wButtons &= ~0xF;
        report.Report.wButtons |= static_cast<USHORT>(dpad);

        if (b0 & SteamController::BTN_A) report.Report.wButtons |= DS4_BUTTON_CROSS;
        if (b0 & SteamController::BTN_B) report.Report.wButtons |= DS4_BUTTON_CIRCLE;
        if (b0 & SteamController::BTN_X) report.Report.wButtons |= DS4_BUTTON_SQUARE;
        if (b0 & SteamController::BTN_Y) report.Report.wButtons |= DS4_BUTTON_TRIANGLE;
        if (b2 & SteamController::BTN_LB) report.Report.wButtons |= DS4_BUTTON_SHOULDER_LEFT;
        if (b1 & SteamController::BTN_RB) report.Report.wButtons |= DS4_BUTTON_SHOULDER_RIGHT;
        if (b1 & SteamController::BTN_VIEW) report.Report.wButtons |= DS4_BUTTON_SHARE;
        if (b0 & SteamController::BTN_MENU) report.Report.wButtons |= DS4_BUTTON_OPTIONS;
        if (b1 & SteamController::BTN_LS) report.Report.wButtons |= DS4_BUTTON_THUMB_LEFT;
        if (b0 & SteamController::BTN_RS) report.Report.wButtons |= DS4_BUTTON_THUMB_RIGHT;
        if (report.Report.bTriggerL > 0) report.Report.wButtons |= DS4_BUTTON_TRIGGER_LEFT;
        if (report.Report.bTriggerR > 0) report.Report.wButtons |= DS4_BUTTON_TRIGGER_RIGHT;
        if (b2 & SteamController::BTN_STEAM) report.Report.bSpecial |= DS4_SPECIAL_BUTTON_PS;

        if (state.hasImu) {
            if (!m_hasLastImuTimestamp || state.imuTimestamp != m_lastImuTimestamp) {
                const uint32_t delta = m_hasLastImuTimestamp
                    ? (state.imuTimestamp - m_lastImuTimestamp)
                    : 0;
                m_ds4Timestamp = static_cast<uint16_t>(
                    m_ds4Timestamp + static_cast<uint16_t>(std::max<uint32_t>(1, delta / 16)));
                m_lastImuTimestamp = state.imuTimestamp;
                m_hasLastImuTimestamp = true;
            }

            report.Report.wGyroX = state.gyroX;
            report.Report.wGyroY = state.gyroZ;
            report.Report.wGyroZ = NegI16(state.gyroY);
            report.Report.wAccelX = state.accelX;
            report.Report.wAccelY = state.accelZ;
            report.Report.wAccelZ = NegI16(state.accelY);
        } else {
            ++m_ds4Timestamp;
        }
        report.Report.wTimestamp = m_ds4Timestamp;

        const bool suppressRightTouch =
            (m_trackpadMouseEnabled && !m_useLeftTrackpadForMouse) ||
            (m_trackpadDpadEnabled && m_useRightTrackpadForDpad);
        const bool suppressLeftTouch =
            (m_trackpadMouseEnabled && m_useLeftTrackpadForMouse) ||
            (m_trackpadDpadEnabled && !m_useRightTrackpadForDpad);
        const bool rightTouching = rawRightTouching && !suppressRightTouch;
        const bool leftTouching = rawLeftTouching && !suppressLeftTouch;
        const bool rightClick = rawRightClick && !suppressRightTouch;
        const bool leftClick = rawLeftClick && !suppressLeftTouch;
        if (rightClick || leftClick)
            report.Report.bSpecial |= DS4_SPECIAL_BUTTON_TOUCHPAD;

        if (rightTouching && !m_wasRightTouching)
            m_rightTracking = static_cast<uint8_t>((m_rightTracking + 1) & 0x7F);
        if (leftTouching && !m_wasLeftTouching)
            m_leftTracking = static_cast<uint8_t>((m_leftTracking + 1) & 0x7F);
        m_wasRightTouching = rightTouching;
        m_wasLeftTouching = leftTouching;

        DS4_TOUCH& touch = report.Report.sCurrentTouch;
        touch.bPacketCounter = ++m_touchPacketCounter;
        touch.bIsUpTrackingNum1 = static_cast<uint8_t>(m_rightTracking | (rightTouching ? 0x00 : 0x80));
        PackDs4Touch(touch.bTouchData1,
                     rightTouching ? NormalizePadAxis(state.rightPadX, 1919, false) : 0,
                     rightTouching ? NormalizePadAxis(state.rightPadY, 942, true) : 0);
        touch.bIsUpTrackingNum2 = static_cast<uint8_t>(m_leftTracking | (leftTouching ? 0x00 : 0x80));
        PackDs4Touch(touch.bTouchData2,
                     leftTouching ? NormalizePadAxis(state.leftPadX, 1919, false) : 0,
                     leftTouching ? NormalizePadAxis(state.leftPadY, 942, true) : 0);
        report.Report.bTouchPacketsN = 1;
        report.Report.sPreviousTouch[0] = touch;
        report.Report.sPreviousTouch[1] = touch;

        vigem_target_ds4_update_ex(static_cast<PVIGEM_CLIENT>(m_client),
                                   static_cast<PVIGEM_TARGET>(m_target),
                                   report);
        return;
    }

    XUSB_REPORT report = TranslateXusb(state, m_trackpadDpadEnabled, m_useRightTrackpadForDpad);
    vigem_target_x360_update(static_cast<PVIGEM_CLIENT>(m_client),
                             static_cast<PVIGEM_TARGET>(m_target),
                             report);
}
