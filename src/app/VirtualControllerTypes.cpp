#include "VirtualControllerTypes.h"
#include "steam/SteamController.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr uint32_t X360_DPAD_UP = 0x0001;
constexpr uint32_t X360_DPAD_DOWN = 0x0002;
constexpr uint32_t X360_DPAD_LEFT = 0x0004;
constexpr uint32_t X360_DPAD_RIGHT = 0x0008;
constexpr uint32_t X360_START = 0x0010;
constexpr uint32_t X360_BACK = 0x0020;
constexpr uint32_t X360_LEFT_THUMB = 0x0040;
constexpr uint32_t X360_RIGHT_THUMB = 0x0080;
constexpr uint32_t X360_LEFT_SHOULDER = 0x0100;
constexpr uint32_t X360_RIGHT_SHOULDER = 0x0200;
constexpr uint32_t X360_GUIDE = 0x0400;
constexpr uint32_t X360_A = 0x1000;
constexpr uint32_t X360_B = 0x2000;
constexpr uint32_t X360_X = 0x4000;
constexpr uint32_t X360_Y = 0x8000;

constexpr uint16_t DS4_BUTTON_PS = 0x0001;
constexpr uint16_t DS4_BUTTON_TOUCHPAD_CLICK = 0x0002;
constexpr uint16_t DS4_BUTTON_SQUARE = 0x0010;
constexpr uint16_t DS4_BUTTON_CROSS = 0x0020;
constexpr uint16_t DS4_BUTTON_CIRCLE = 0x0040;
constexpr uint16_t DS4_BUTTON_TRIANGLE = 0x0080;
constexpr uint16_t DS4_BUTTON_L1 = 0x0100;
constexpr uint16_t DS4_BUTTON_R1 = 0x0200;
constexpr uint16_t DS4_BUTTON_L2 = 0x0400;
constexpr uint16_t DS4_BUTTON_R2 = 0x0800;
constexpr uint16_t DS4_BUTTON_SHARE = 0x1000;
constexpr uint16_t DS4_BUTTON_OPTIONS = 0x2000;
constexpr uint16_t DS4_BUTTON_L3 = 0x4000;
constexpr uint16_t DS4_BUTTON_R3 = 0x8000;

constexpr uint8_t DS4_DPAD_UP = 0x01;
constexpr uint8_t DS4_DPAD_DOWN = 0x02;
constexpr uint8_t DS4_DPAD_LEFT = 0x04;
constexpr uint8_t DS4_DPAD_RIGHT = 0x08;

constexpr uint16_t DS5_BUTTON_PS = 0x0001;
constexpr uint16_t DS5_BUTTON_TOUCHPAD_CLICK = 0x0002;
constexpr uint16_t DS5_BUTTON_MUTE = 0x0004;
constexpr uint16_t DS5_BUTTON_SQUARE = 0x0010;
constexpr uint16_t DS5_BUTTON_CROSS = 0x0020;
constexpr uint16_t DS5_BUTTON_CIRCLE = 0x0040;
constexpr uint16_t DS5_BUTTON_TRIANGLE = 0x0080;
constexpr uint16_t DS5_BUTTON_L1 = 0x0100;
constexpr uint16_t DS5_BUTTON_R1 = 0x0200;
constexpr uint16_t DS5_BUTTON_L2 = 0x0400;
constexpr uint16_t DS5_BUTTON_R2 = 0x0800;
constexpr uint16_t DS5_BUTTON_CREATE = 0x1000;
constexpr uint16_t DS5_BUTTON_OPTIONS = 0x2000;
constexpr uint16_t DS5_BUTTON_L3 = 0x4000;
constexpr uint16_t DS5_BUTTON_R3 = 0x8000;

constexpr uint8_t DS5_DPAD_UP = DS4_DPAD_UP;
constexpr uint8_t DS5_DPAD_DOWN = DS4_DPAD_DOWN;
constexpr uint8_t DS5_DPAD_LEFT = DS4_DPAD_LEFT;
constexpr uint8_t DS5_DPAD_RIGHT = DS4_DPAD_RIGHT;

constexpr uint32_t NS2_BUTTON_B = 1u << 0;
constexpr uint32_t NS2_BUTTON_A = 1u << 1;
constexpr uint32_t NS2_BUTTON_Y = 1u << 2;
constexpr uint32_t NS2_BUTTON_X = 1u << 3;
constexpr uint32_t NS2_BUTTON_R = 1u << 4;
constexpr uint32_t NS2_BUTTON_ZR = 1u << 5;
constexpr uint32_t NS2_BUTTON_PLUS = 1u << 6;
constexpr uint32_t NS2_BUTTON_RIGHT_STICK = 1u << 7;
constexpr uint32_t NS2_BUTTON_DOWN = 1u << 8;
constexpr uint32_t NS2_BUTTON_RIGHT = 1u << 9;
constexpr uint32_t NS2_BUTTON_LEFT = 1u << 10;
constexpr uint32_t NS2_BUTTON_UP = 1u << 11;
constexpr uint32_t NS2_BUTTON_L = 1u << 12;
constexpr uint32_t NS2_BUTTON_ZL = 1u << 13;
constexpr uint32_t NS2_BUTTON_MINUS = 1u << 14;
constexpr uint32_t NS2_BUTTON_LEFT_STICK = 1u << 15;
constexpr uint32_t NS2_BUTTON_HOME = 1u << 16;
constexpr uint32_t NS2_BUTTON_CAPTURE = 1u << 17;
constexpr uint32_t NS2_BUTTON_GR = 1u << 18;
constexpr uint32_t NS2_BUTTON_GL = 1u << 19;
constexpr uint32_t NS2_BUTTON_C = 1u << 20;

enum class DpadDirection : uint8_t {
    None,
    North,
    Northeast,
    East,
    Southeast,
    South,
    Southwest,
    West,
    Northwest,
};

uint8_t ButtonByte(const SteamControllerState& state, int index) {
    return static_cast<uint8_t>((state.buttons >> (index * 8)) & 0xFF);
}

uint8_t TriggerToByte(int16_t raw) {
    return static_cast<uint8_t>(std::clamp<int>(raw >> 7, 0, 255));
}

uint8_t AxisToDs4Byte(int16_t raw, bool invert) {
    int v = raw;
    if (invert)
        v = (v == -32768) ? 32767 : -v;
    return static_cast<uint8_t>(std::clamp<int>((v + 32768) >> 8, 0, 255));
}

int8_t AxisToViiperDs4(int16_t raw, bool invert) {
    return static_cast<int8_t>(static_cast<int>(AxisToDs4Byte(raw, invert)) - 128);
}

uint16_t AxisToSwitchStick(int16_t raw, bool invert) {
    int v = raw;
    if (invert)
        v = (v == -32768) ? 32767 : -v;

    const int normalized = static_cast<int>(
        ((static_cast<int64_t>(v) + 32768) * 4096) / 65536);
    return static_cast<uint16_t>(std::clamp(normalized, 0, 0x0FFF));
}

int16_t NegI16(int16_t raw) {
    return static_cast<int16_t>((raw == -32768) ? 32767 : -raw);
}

int NormalizePadAxis(int16_t raw, int maxValue, bool invert) {
    int v = raw;
    if (invert)
        v = (v == -32768) ? 32767 : -v;

    const int normalized = static_cast<int>(
        ((static_cast<int64_t>(v) + 32768) * maxValue) / 65535);
    return std::clamp(normalized, 0, maxValue);
}

DpadDirection PhysicalDpadDirection(const SteamControllerState& state) {
    const uint8_t b1 = ButtonByte(state, 1);
    const bool up = (b1 & SteamController::BTN_DPAD_UP) != 0;
    const bool down = (b1 & SteamController::BTN_DPAD_DN) != 0;
    const bool left = (b1 & SteamController::BTN_DPAD_LT) != 0;
    const bool right = (b1 & SteamController::BTN_DPAD_RT) != 0;

    if (up && right) return DpadDirection::Northeast;
    if (right && down) return DpadDirection::Southeast;
    if (down && left) return DpadDirection::Southwest;
    if (left && up) return DpadDirection::Northwest;
    if (up) return DpadDirection::North;
    if (right) return DpadDirection::East;
    if (down) return DpadDirection::South;
    if (left) return DpadDirection::West;
    return DpadDirection::None;
}

DpadDirection TrackpadDpadDirection(int16_t x, int16_t y, bool active) {
    if (!active)
        return DpadDirection::None;

    const int ix = static_cast<int>(x);
    const int iy = static_cast<int>(y);
    const int ax = ix < 0 ? -ix : ix;
    const int ay = iy < 0 ? -iy : iy;
    constexpr int DEADZONE = 9000;

    if (ax < DEADZONE && ay < DEADZONE)
        return DpadDirection::None;

    constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;
    double degrees = std::atan2(static_cast<double>(iy), static_cast<double>(ix)) * RAD_TO_DEG;
    if (degrees < 0.0)
        degrees += 360.0;

    if (degrees < 40.0 || degrees >= 320.0) return DpadDirection::East;
    if (degrees < 50.0) return DpadDirection::Northeast;
    if (degrees < 130.0) return DpadDirection::North;
    if (degrees < 140.0) return DpadDirection::Northwest;
    if (degrees < 220.0) return DpadDirection::West;
    if (degrees < 230.0) return DpadDirection::Southwest;
    if (degrees < 310.0) return DpadDirection::South;
    return DpadDirection::Southeast;
}

uint32_t X360DpadButtons(DpadDirection direction) {
    switch (direction) {
    case DpadDirection::North:
        return X360_DPAD_UP;
    case DpadDirection::Northeast:
        return X360_DPAD_UP | X360_DPAD_RIGHT;
    case DpadDirection::East:
        return X360_DPAD_RIGHT;
    case DpadDirection::Southeast:
        return X360_DPAD_DOWN | X360_DPAD_RIGHT;
    case DpadDirection::South:
        return X360_DPAD_DOWN;
    case DpadDirection::Southwest:
        return X360_DPAD_DOWN | X360_DPAD_LEFT;
    case DpadDirection::West:
        return X360_DPAD_LEFT;
    case DpadDirection::Northwest:
        return X360_DPAD_UP | X360_DPAD_LEFT;
    default:
        return 0;
    }
}

uint8_t Ds4DpadMask(DpadDirection direction) {
    switch (direction) {
    case DpadDirection::North:
        return DS4_DPAD_UP;
    case DpadDirection::Northeast:
        return DS4_DPAD_UP | DS4_DPAD_RIGHT;
    case DpadDirection::East:
        return DS4_DPAD_RIGHT;
    case DpadDirection::Southeast:
        return DS4_DPAD_DOWN | DS4_DPAD_RIGHT;
    case DpadDirection::South:
        return DS4_DPAD_DOWN;
    case DpadDirection::Southwest:
        return DS4_DPAD_DOWN | DS4_DPAD_LEFT;
    case DpadDirection::West:
        return DS4_DPAD_LEFT;
    case DpadDirection::Northwest:
        return DS4_DPAD_UP | DS4_DPAD_LEFT;
    default:
        return 0;
    }
}

uint8_t Ds5DpadMask(DpadDirection direction) {
    return Ds4DpadMask(direction);
}

uint32_t SwitchDpadButtons(DpadDirection direction) {
    switch (direction) {
    case DpadDirection::North:
        return NS2_BUTTON_UP;
    case DpadDirection::Northeast:
        return NS2_BUTTON_UP | NS2_BUTTON_RIGHT;
    case DpadDirection::East:
        return NS2_BUTTON_RIGHT;
    case DpadDirection::Southeast:
        return NS2_BUTTON_DOWN | NS2_BUTTON_RIGHT;
    case DpadDirection::South:
        return NS2_BUTTON_DOWN;
    case DpadDirection::Southwest:
        return NS2_BUTTON_DOWN | NS2_BUTTON_LEFT;
    case DpadDirection::West:
        return NS2_BUTTON_LEFT;
    case DpadDirection::Northwest:
        return NS2_BUTTON_UP | NS2_BUTTON_LEFT;
    default:
        return 0;
    }
}

bool BackButtonPressed(const SteamControllerState& state, BackButtonId id) {
    const uint8_t b0 = ButtonByte(state, 0);
    const uint8_t b1 = ButtonByte(state, 1);
    const uint8_t b2 = ButtonByte(state, 2);

    switch (id) {
    case BackButtonId::L4:
        return (b2 & SteamController::BTN_L4) != 0;
    case BackButtonId::L5:
        return (b2 & SteamController::BTN_L5) != 0;
    case BackButtonId::R4:
        return (b0 & SteamController::BTN_R4) != 0;
    case BackButtonId::R5:
        return (b1 & SteamController::BTN_R5) != 0;
    default:
        return false;
    }
}

void ApplyBackButtonActionX360(BackButtonAction action, ViiperXbox360InputState& state) {
    switch (action) {
    case BackButtonAction::DpadUp:
        state.buttons |= X360_DPAD_UP;
        break;
    case BackButtonAction::DpadDown:
        state.buttons |= X360_DPAD_DOWN;
        break;
    case BackButtonAction::DpadLeft:
        state.buttons |= X360_DPAD_LEFT;
        break;
    case BackButtonAction::DpadRight:
        state.buttons |= X360_DPAD_RIGHT;
        break;
    case BackButtonAction::South:
        state.buttons |= X360_A;
        break;
    case BackButtonAction::East:
        state.buttons |= X360_B;
        break;
    case BackButtonAction::West:
        state.buttons |= X360_X;
        break;
    case BackButtonAction::North:
        state.buttons |= X360_Y;
        break;
    case BackButtonAction::LeftBumper:
        state.buttons |= X360_LEFT_SHOULDER;
        break;
    case BackButtonAction::RightBumper:
        state.buttons |= X360_RIGHT_SHOULDER;
        break;
    case BackButtonAction::LeftTrigger:
        state.leftTrigger = 255;
        break;
    case BackButtonAction::RightTrigger:
        state.rightTrigger = 255;
        break;
    case BackButtonAction::LeftStick:
        state.buttons |= X360_LEFT_THUMB;
        break;
    case BackButtonAction::RightStick:
        state.buttons |= X360_RIGHT_THUMB;
        break;
    case BackButtonAction::Back:
        state.buttons |= X360_BACK;
        break;
    case BackButtonAction::Start:
        state.buttons |= X360_START;
        break;
    case BackButtonAction::Guide:
        state.buttons |= X360_GUIDE;
        break;
    default:
        break;
    }
}

void ApplyBackButtonActionDs4(BackButtonAction action,
                              ViiperDualShock4InputState& state,
                              uint8_t& dpadMask) {
    switch (action) {
    case BackButtonAction::DpadUp:
        dpadMask |= DS4_DPAD_UP;
        break;
    case BackButtonAction::DpadDown:
        dpadMask |= DS4_DPAD_DOWN;
        break;
    case BackButtonAction::DpadLeft:
        dpadMask |= DS4_DPAD_LEFT;
        break;
    case BackButtonAction::DpadRight:
        dpadMask |= DS4_DPAD_RIGHT;
        break;
    case BackButtonAction::South:
        state.buttons |= DS4_BUTTON_CROSS;
        break;
    case BackButtonAction::East:
        state.buttons |= DS4_BUTTON_CIRCLE;
        break;
    case BackButtonAction::West:
        state.buttons |= DS4_BUTTON_SQUARE;
        break;
    case BackButtonAction::North:
        state.buttons |= DS4_BUTTON_TRIANGLE;
        break;
    case BackButtonAction::LeftBumper:
        state.buttons |= DS4_BUTTON_L1;
        break;
    case BackButtonAction::RightBumper:
        state.buttons |= DS4_BUTTON_R1;
        break;
    case BackButtonAction::LeftTrigger:
        state.leftTrigger = 255;
        state.buttons |= DS4_BUTTON_L2;
        break;
    case BackButtonAction::RightTrigger:
        state.rightTrigger = 255;
        state.buttons |= DS4_BUTTON_R2;
        break;
    case BackButtonAction::LeftStick:
        state.buttons |= DS4_BUTTON_L3;
        break;
    case BackButtonAction::RightStick:
        state.buttons |= DS4_BUTTON_R3;
        break;
    case BackButtonAction::Back:
        state.buttons |= DS4_BUTTON_SHARE;
        break;
    case BackButtonAction::Start:
        state.buttons |= DS4_BUTTON_OPTIONS;
        break;
    case BackButtonAction::Guide:
        state.buttons |= DS4_BUTTON_PS;
        break;
    default:
        break;
    }
}

void ApplyBackButtonActionDs5(BackButtonAction action,
                              ViiperDualSenseInputState& state,
                              uint8_t& dpadMask) {
    switch (action) {
    case BackButtonAction::DpadUp:
        dpadMask |= DS5_DPAD_UP;
        break;
    case BackButtonAction::DpadDown:
        dpadMask |= DS5_DPAD_DOWN;
        break;
    case BackButtonAction::DpadLeft:
        dpadMask |= DS5_DPAD_LEFT;
        break;
    case BackButtonAction::DpadRight:
        dpadMask |= DS5_DPAD_RIGHT;
        break;
    case BackButtonAction::South:
        state.buttons |= DS5_BUTTON_CROSS;
        break;
    case BackButtonAction::East:
        state.buttons |= DS5_BUTTON_CIRCLE;
        break;
    case BackButtonAction::West:
        state.buttons |= DS5_BUTTON_SQUARE;
        break;
    case BackButtonAction::North:
        state.buttons |= DS5_BUTTON_TRIANGLE;
        break;
    case BackButtonAction::LeftBumper:
        state.buttons |= DS5_BUTTON_L1;
        break;
    case BackButtonAction::RightBumper:
        state.buttons |= DS5_BUTTON_R1;
        break;
    case BackButtonAction::LeftTrigger:
        state.leftTrigger = 255;
        state.buttons |= DS5_BUTTON_L2;
        break;
    case BackButtonAction::RightTrigger:
        state.rightTrigger = 255;
        state.buttons |= DS5_BUTTON_R2;
        break;
    case BackButtonAction::LeftStick:
        state.buttons |= DS5_BUTTON_L3;
        break;
    case BackButtonAction::RightStick:
        state.buttons |= DS5_BUTTON_R3;
        break;
    case BackButtonAction::Back:
        state.buttons |= DS5_BUTTON_CREATE;
        break;
    case BackButtonAction::Start:
        state.buttons |= DS5_BUTTON_OPTIONS;
        break;
    case BackButtonAction::Guide:
        state.buttons |= DS5_BUTTON_PS;
        break;
    default:
        break;
    }
}

void ApplyBackButtonActionSwitch2Pro(BackButtonAction action,
                                     ViiperSwitch2ProInputState& state) {
    switch (action) {
    case BackButtonAction::DpadUp:
        state.buttons |= NS2_BUTTON_UP;
        break;
    case BackButtonAction::DpadDown:
        state.buttons |= NS2_BUTTON_DOWN;
        break;
    case BackButtonAction::DpadLeft:
        state.buttons |= NS2_BUTTON_LEFT;
        break;
    case BackButtonAction::DpadRight:
        state.buttons |= NS2_BUTTON_RIGHT;
        break;
    case BackButtonAction::South:
        state.buttons |= NS2_BUTTON_B;
        break;
    case BackButtonAction::East:
        state.buttons |= NS2_BUTTON_A;
        break;
    case BackButtonAction::West:
        state.buttons |= NS2_BUTTON_Y;
        break;
    case BackButtonAction::North:
        state.buttons |= NS2_BUTTON_X;
        break;
    case BackButtonAction::LeftBumper:
        state.buttons |= NS2_BUTTON_L;
        break;
    case BackButtonAction::RightBumper:
        state.buttons |= NS2_BUTTON_R;
        break;
    case BackButtonAction::LeftTrigger:
        state.buttons |= NS2_BUTTON_ZL;
        break;
    case BackButtonAction::RightTrigger:
        state.buttons |= NS2_BUTTON_ZR;
        break;
    case BackButtonAction::LeftStick:
        state.buttons |= NS2_BUTTON_LEFT_STICK;
        break;
    case BackButtonAction::RightStick:
        state.buttons |= NS2_BUTTON_RIGHT_STICK;
        break;
    case BackButtonAction::Back:
        state.buttons |= NS2_BUTTON_MINUS;
        break;
    case BackButtonAction::Start:
        state.buttons |= NS2_BUTTON_PLUS;
        break;
    case BackButtonAction::Guide:
        state.buttons |= NS2_BUTTON_HOME;
        break;
    case BackButtonAction::GL:
        state.buttons |= NS2_BUTTON_GL;
        break;
    case BackButtonAction::GR:
        state.buttons |= NS2_BUTTON_GR;
        break;
    default:
        break;
    }
}

void ApplyBackButtonMappingsX360(const SteamControllerState& rawState,
                                 const BackButtonMappings& mappings,
                                 ViiperXbox360InputState& state) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(BackButtonId::Count); ++i) {
        const auto id = static_cast<BackButtonId>(i);
        const BackButtonAction action = mappings.Get(id);
        if (action != BackButtonAction::None && BackButtonPressed(rawState, id))
            ApplyBackButtonActionX360(action, state);
    }
}

void ApplyBackButtonMappingsDs4(const SteamControllerState& rawState,
                                const BackButtonMappings& mappings,
                                ViiperDualShock4InputState& state,
                                uint8_t& dpadMask) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(BackButtonId::Count); ++i) {
        const auto id = static_cast<BackButtonId>(i);
        const BackButtonAction action = mappings.Get(id);
        if (action != BackButtonAction::None && BackButtonPressed(rawState, id))
            ApplyBackButtonActionDs4(action, state, dpadMask);
    }
}

void ApplyBackButtonMappingsDs5(const SteamControllerState& rawState,
                                const BackButtonMappings& mappings,
                                ViiperDualSenseInputState& state,
                                uint8_t& dpadMask) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(BackButtonId::Count); ++i) {
        const auto id = static_cast<BackButtonId>(i);
        const BackButtonAction action = mappings.Get(id);
        if (action != BackButtonAction::None && BackButtonPressed(rawState, id))
            ApplyBackButtonActionDs5(action, state, dpadMask);
    }
}

void ApplyBackButtonMappingsSwitch2Pro(const SteamControllerState& rawState,
                                       const BackButtonMappings& mappings,
                                       ViiperSwitch2ProInputState& state) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(BackButtonId::Count); ++i) {
        const auto id = static_cast<BackButtonId>(i);
        const BackButtonAction action = mappings.Get(id);
        if (action != BackButtonAction::None && BackButtonPressed(rawState, id))
            ApplyBackButtonActionSwitch2Pro(action, state);
    }
}

void PutU16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void PutI16(uint8_t* data, int16_t value) {
    PutU16(data, static_cast<uint16_t>(value));
}

void PutU32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

} // namespace

std::array<uint8_t, 20> ViiperXbox360InputState::Serialize() const {
    std::array<uint8_t, 20> data{};
    PutU32(data.data(), buttons);
    data[4] = leftTrigger;
    data[5] = rightTrigger;
    PutI16(data.data() + 6, leftStickX);
    PutI16(data.data() + 8, leftStickY);
    PutI16(data.data() + 10, rightStickX);
    PutI16(data.data() + 12, rightStickY);
    std::copy(reserved.begin(), reserved.end(), data.begin() + 14);
    return data;
}

std::array<uint8_t, 31> ViiperDualShock4InputState::Serialize() const {
    std::array<uint8_t, 31> data{};
    data[0] = static_cast<uint8_t>(leftStickX);
    data[1] = static_cast<uint8_t>(leftStickY);
    data[2] = static_cast<uint8_t>(rightStickX);
    data[3] = static_cast<uint8_t>(rightStickY);
    PutU16(data.data() + 4, buttons);
    data[6] = dpad;
    data[7] = leftTrigger;
    data[8] = rightTrigger;
    PutU16(data.data() + 9, touch1X);
    PutU16(data.data() + 11, touch1Y);
    data[13] = touch1Active ? 1 : 0;
    PutU16(data.data() + 14, touch2X);
    PutU16(data.data() + 16, touch2Y);
    data[18] = touch2Active ? 1 : 0;
    PutI16(data.data() + 19, gyroX);
    PutI16(data.data() + 21, gyroY);
    PutI16(data.data() + 23, gyroZ);
    PutI16(data.data() + 25, accelX);
    PutI16(data.data() + 27, accelY);
    PutI16(data.data() + 29, accelZ);
    return data;
}

std::array<uint8_t, 33> ViiperDualSenseInputState::Serialize() const {
    std::array<uint8_t, 33> data{};
    data[0] = leftStickX;
    data[1] = leftStickY;
    data[2] = rightStickX;
    data[3] = rightStickY;
    data[4] = leftTrigger;
    data[5] = rightTrigger;
    data[6] = dpad;
    PutU16(data.data() + 7, buttons);
    PutU16(data.data() + 9, touch1X);
    PutU16(data.data() + 11, touch1Y);
    data[13] = touch1Active ? 1 : 0;
    PutU16(data.data() + 14, touch2X);
    PutU16(data.data() + 16, touch2Y);
    data[18] = touch2Active ? 1 : 0;
    PutI16(data.data() + 19, gyroX);
    PutI16(data.data() + 21, gyroY);
    PutI16(data.data() + 23, gyroZ);
    PutI16(data.data() + 25, accelX);
    PutI16(data.data() + 27, accelY);
    PutI16(data.data() + 29, accelZ);
    data[31] = batteryLevelPercent;
    data[32] = chargeState;
    return data;
}

std::array<uint8_t, 24> ViiperSwitch2ProInputState::Serialize() const {
    std::array<uint8_t, 24> data{};
    PutU32(data.data(), buttons);
    PutU16(data.data() + 4, leftStickX);
    PutU16(data.data() + 6, leftStickY);
    PutU16(data.data() + 8, rightStickX);
    PutU16(data.data() + 10, rightStickY);
    PutI16(data.data() + 12, accelX);
    PutI16(data.data() + 14, accelY);
    PutI16(data.data() + 16, accelZ);
    PutI16(data.data() + 18, gyroX);
    PutI16(data.data() + 20, gyroY);
    PutI16(data.data() + 22, gyroZ);
    return data;
}

ViiperXbox360InputState BuildViiperXbox360Input(
    const SteamControllerState& state,
    const VirtualControllerRuntimeSettings& settings) {
    ViiperXbox360InputState out{};

    const uint8_t b0 = ButtonByte(state, 0);
    const uint8_t b1 = ButtonByte(state, 1);
    const uint8_t b2 = ButtonByte(state, 2);
    const uint8_t b3 = ButtonByte(state, 3);

    if (b0 & SteamController::BTN_A) out.buttons |= X360_A;
    if (b0 & SteamController::BTN_B) out.buttons |= X360_B;
    if (b0 & SteamController::BTN_X) out.buttons |= X360_X;
    if (b0 & SteamController::BTN_Y) out.buttons |= X360_Y;
    if (b2 & SteamController::BTN_LB) out.buttons |= X360_LEFT_SHOULDER;
    if (b1 & SteamController::BTN_RB) out.buttons |= X360_RIGHT_SHOULDER;
    if (b0 & SteamController::BTN_MENU) out.buttons |= X360_START;
    if (b1 & SteamController::BTN_VIEW) out.buttons |= X360_BACK;
    if (b1 & SteamController::BTN_LS) out.buttons |= X360_LEFT_THUMB;
    if (b0 & SteamController::BTN_RS) out.buttons |= X360_RIGHT_THUMB;
    if (b2 & SteamController::BTN_STEAM) out.buttons |= X360_GUIDE;

    if (b1 & SteamController::BTN_DPAD_UP) out.buttons |= X360_DPAD_UP;
    if (b1 & SteamController::BTN_DPAD_DN) out.buttons |= X360_DPAD_DOWN;
    if (b1 & SteamController::BTN_DPAD_LT) out.buttons |= X360_DPAD_LEFT;
    if (b1 & SteamController::BTN_DPAD_RT) out.buttons |= X360_DPAD_RIGHT;

    if (settings.trackpadDpadEnabled) {
        const bool rawRightClick = (b2 & SteamController::BTN_TP_RT_CLICK) != 0;
        const bool rawLeftClick = (b3 & SteamController::BTN_TP_LT_CLICK) != 0;
        const DpadDirection trackpadDpad = settings.useRightTrackpadForDpad
            ? TrackpadDpadDirection(state.rightPadX, state.rightPadY, rawRightClick)
            : TrackpadDpadDirection(state.leftPadX, state.leftPadY, rawLeftClick);
        const uint32_t x360Dpad = X360DpadButtons(trackpadDpad);
        if (x360Dpad != 0) {
            out.buttons &= ~(X360_DPAD_UP | X360_DPAD_DOWN |
                             X360_DPAD_LEFT | X360_DPAD_RIGHT);
            out.buttons |= x360Dpad;
        }
    }

    out.leftTrigger = TriggerToByte(state.leftTrigger);
    out.rightTrigger = TriggerToByte(state.rightTrigger);
    out.leftStickX = state.leftStickX;
    out.leftStickY = state.leftStickY;
    out.rightStickX = state.rightStickX;
    out.rightStickY = state.rightStickY;

    ApplyBackButtonMappingsX360(state, settings.backButtonMappings, out);
    return out;
}

ViiperDualShock4InputState BuildViiperDualShock4Input(
    const SteamControllerState& state,
    const VirtualControllerRuntimeSettings& settings) {
    ViiperDualShock4InputState out{};
    out.leftStickX = AxisToViiperDs4(state.leftStickX, false);
    out.leftStickY = AxisToViiperDs4(state.leftStickY, true);
    out.rightStickX = AxisToViiperDs4(state.rightStickX, false);
    out.rightStickY = AxisToViiperDs4(state.rightStickY, true);
    out.leftTrigger = TriggerToByte(state.leftTrigger);
    out.rightTrigger = TriggerToByte(state.rightTrigger);

    const uint8_t b0 = ButtonByte(state, 0);
    const uint8_t b1 = ButtonByte(state, 1);
    const uint8_t b2 = ButtonByte(state, 2);
    const uint8_t b3 = ButtonByte(state, 3);

    const bool rawRightClick = (b2 & SteamController::BTN_TP_RT_CLICK) != 0;
    const bool rawLeftClick = (b3 & SteamController::BTN_TP_LT_CLICK) != 0;
    const bool rawRightTouching = ((b2 & SteamController::BTN_TP_RT) != 0) || rawRightClick;
    const bool rawLeftTouching = ((b3 & SteamController::BTN_TP_LT) != 0) || rawLeftClick;

    DpadDirection dpad = PhysicalDpadDirection(state);
    if (settings.trackpadDpadEnabled) {
        const DpadDirection trackpadDpad = settings.useRightTrackpadForDpad
            ? TrackpadDpadDirection(state.rightPadX, state.rightPadY, rawRightClick)
            : TrackpadDpadDirection(state.leftPadX, state.leftPadY, rawLeftClick);
        if (trackpadDpad != DpadDirection::None)
            dpad = trackpadDpad;
    }
    uint8_t dpadMask = Ds4DpadMask(dpad);

    if (b0 & SteamController::BTN_A) out.buttons |= DS4_BUTTON_CROSS;
    if (b0 & SteamController::BTN_B) out.buttons |= DS4_BUTTON_CIRCLE;
    if (b0 & SteamController::BTN_X) out.buttons |= DS4_BUTTON_SQUARE;
    if (b0 & SteamController::BTN_Y) out.buttons |= DS4_BUTTON_TRIANGLE;
    if (b2 & SteamController::BTN_LB) out.buttons |= DS4_BUTTON_L1;
    if (b1 & SteamController::BTN_RB) out.buttons |= DS4_BUTTON_R1;
    if (b1 & SteamController::BTN_VIEW) out.buttons |= DS4_BUTTON_SHARE;
    if ((b0 & SteamController::BTN_MENU) && !rawRightClick && !rawLeftClick)
        out.buttons |= DS4_BUTTON_OPTIONS;
    if (b1 & SteamController::BTN_LS) out.buttons |= DS4_BUTTON_L3;
    if (b0 & SteamController::BTN_RS) out.buttons |= DS4_BUTTON_R3;
    if (out.leftTrigger > 0) out.buttons |= DS4_BUTTON_L2;
    if (out.rightTrigger > 0) out.buttons |= DS4_BUTTON_R2;
    if (b2 & SteamController::BTN_STEAM) out.buttons |= DS4_BUTTON_PS;

    ApplyBackButtonMappingsDs4(state, settings.backButtonMappings, out, dpadMask);
    out.dpad = dpadMask;

    if (state.hasImu) {
        out.gyroX = state.gyroX;
        out.gyroY = state.gyroZ;
        out.gyroZ = NegI16(state.gyroY);
        out.accelX = state.accelX;
        out.accelY = state.accelZ;
        out.accelZ = NegI16(state.accelY);
    }

    const bool suppressRightTouch =
        (settings.trackpadMouseEnabled && !settings.useLeftTrackpadForMouse) ||
        (settings.trackpadDpadEnabled && settings.useRightTrackpadForDpad);
    const bool suppressLeftTouch =
        (settings.trackpadMouseEnabled && settings.useLeftTrackpadForMouse) ||
        (settings.trackpadDpadEnabled && !settings.useRightTrackpadForDpad);
    const bool rightTouching = rawRightTouching && !suppressRightTouch;
    const bool leftTouching = rawLeftTouching && !suppressLeftTouch;
    const bool rightClick = rawRightClick && !suppressRightTouch;
    const bool leftClick = rawLeftClick && !suppressLeftTouch;

    if (rightClick || leftClick)
        out.buttons |= DS4_BUTTON_TOUCHPAD_CLICK;

    out.touch1Active = rightTouching;
    out.touch1X = rightTouching
        ? static_cast<uint16_t>(NormalizePadAxis(state.rightPadX, 1919, false))
        : 0;
    out.touch1Y = rightTouching
        ? static_cast<uint16_t>(NormalizePadAxis(state.rightPadY, 942, true))
        : 0;
    out.touch2Active = leftTouching;
    out.touch2X = leftTouching
        ? static_cast<uint16_t>(NormalizePadAxis(state.leftPadX, 1919, false))
        : 0;
    out.touch2Y = leftTouching
        ? static_cast<uint16_t>(NormalizePadAxis(state.leftPadY, 942, true))
        : 0;

    return out;
}

ViiperDualSenseInputState BuildViiperDualSenseInput(
    const SteamControllerState& state,
    const VirtualControllerRuntimeSettings& settings) {
    ViiperDualSenseInputState out{};
    out.leftStickX = AxisToDs4Byte(state.leftStickX, false);
    out.leftStickY = AxisToDs4Byte(state.leftStickY, true);
    out.rightStickX = AxisToDs4Byte(state.rightStickX, false);
    out.rightStickY = AxisToDs4Byte(state.rightStickY, true);
    out.leftTrigger = TriggerToByte(state.leftTrigger);
    out.rightTrigger = TriggerToByte(state.rightTrigger);

    const uint8_t b0 = ButtonByte(state, 0);
    const uint8_t b1 = ButtonByte(state, 1);
    const uint8_t b2 = ButtonByte(state, 2);
    const uint8_t b3 = ButtonByte(state, 3);

    const bool rawRightClick = (b2 & SteamController::BTN_TP_RT_CLICK) != 0;
    const bool rawLeftClick = (b3 & SteamController::BTN_TP_LT_CLICK) != 0;
    const bool rawRightTouching = ((b2 & SteamController::BTN_TP_RT) != 0) || rawRightClick;
    const bool rawLeftTouching = ((b3 & SteamController::BTN_TP_LT) != 0) || rawLeftClick;

    DpadDirection dpad = PhysicalDpadDirection(state);
    if (settings.trackpadDpadEnabled) {
        const DpadDirection trackpadDpad = settings.useRightTrackpadForDpad
            ? TrackpadDpadDirection(state.rightPadX, state.rightPadY, rawRightClick)
            : TrackpadDpadDirection(state.leftPadX, state.leftPadY, rawLeftClick);
        if (trackpadDpad != DpadDirection::None)
            dpad = trackpadDpad;
    }
    uint8_t dpadMask = Ds5DpadMask(dpad);

    if (b0 & SteamController::BTN_A) out.buttons |= DS5_BUTTON_CROSS;
    if (b0 & SteamController::BTN_B) out.buttons |= DS5_BUTTON_CIRCLE;
    if (b0 & SteamController::BTN_X) out.buttons |= DS5_BUTTON_SQUARE;
    if (b0 & SteamController::BTN_Y) out.buttons |= DS5_BUTTON_TRIANGLE;
    if (b2 & SteamController::BTN_LB) out.buttons |= DS5_BUTTON_L1;
    if (b1 & SteamController::BTN_RB) out.buttons |= DS5_BUTTON_R1;
    if (b1 & SteamController::BTN_VIEW) out.buttons |= DS5_BUTTON_CREATE;
    if ((b0 & SteamController::BTN_MENU) && !rawRightClick && !rawLeftClick)
        out.buttons |= DS5_BUTTON_OPTIONS;
    if (b1 & SteamController::BTN_LS) out.buttons |= DS5_BUTTON_L3;
    if (b0 & SteamController::BTN_RS) out.buttons |= DS5_BUTTON_R3;
    if (out.leftTrigger > 0) out.buttons |= DS5_BUTTON_L2;
    if (out.rightTrigger > 0) out.buttons |= DS5_BUTTON_R2;
    if (b2 & SteamController::BTN_STEAM) out.buttons |= DS5_BUTTON_PS;

    ApplyBackButtonMappingsDs5(state, settings.backButtonMappings, out, dpadMask);
    out.dpad = dpadMask;

    if (state.hasImu) {
        out.gyroX = state.gyroX;
        out.gyroY = state.gyroZ;
        out.gyroZ = NegI16(state.gyroY);
        out.accelX = state.accelX;
        out.accelY = state.accelZ;
        out.accelZ = NegI16(state.accelY);
    }

    const bool suppressRightTouch =
        (settings.trackpadMouseEnabled && !settings.useLeftTrackpadForMouse) ||
        (settings.trackpadDpadEnabled && settings.useRightTrackpadForDpad);
    const bool suppressLeftTouch =
        (settings.trackpadMouseEnabled && settings.useLeftTrackpadForMouse) ||
        (settings.trackpadDpadEnabled && !settings.useRightTrackpadForDpad);
    const bool rightTouching = rawRightTouching && !suppressRightTouch;
    const bool leftTouching = rawLeftTouching && !suppressLeftTouch;
    const bool rightClick = rawRightClick && !suppressRightTouch;
    const bool leftClick = rawLeftClick && !suppressLeftTouch;

    if (rightClick || leftClick)
        out.buttons |= DS5_BUTTON_TOUCHPAD_CLICK;

    out.touch1Active = rightTouching;
    out.touch1X = rightTouching
        ? static_cast<uint16_t>(NormalizePadAxis(state.rightPadX, 1919, false))
        : 0;
    out.touch1Y = rightTouching
        ? static_cast<uint16_t>(NormalizePadAxis(state.rightPadY, 1069, true))
        : 0;
    out.touch2Active = leftTouching;
    out.touch2X = leftTouching
        ? static_cast<uint16_t>(NormalizePadAxis(state.leftPadX, 1919, false))
        : 0;
    out.touch2Y = leftTouching
        ? static_cast<uint16_t>(NormalizePadAxis(state.leftPadY, 1069, true))
        : 0;

    return out;
}

ViiperSwitch2ProInputState BuildViiperSwitch2ProInput(
    const SteamControllerState& state,
    const VirtualControllerRuntimeSettings& settings) {
    ViiperSwitch2ProInputState out{};
    out.leftStickX = AxisToSwitchStick(state.leftStickX, false);
    out.leftStickY = AxisToSwitchStick(state.leftStickY, true);
    out.rightStickX = AxisToSwitchStick(state.rightStickX, false);
    out.rightStickY = AxisToSwitchStick(state.rightStickY, true);

    const uint8_t b0 = ButtonByte(state, 0);
    const uint8_t b1 = ButtonByte(state, 1);
    const uint8_t b2 = ButtonByte(state, 2);
    const uint8_t b3 = ButtonByte(state, 3);

    const bool rawRightClick = (b2 & SteamController::BTN_TP_RT_CLICK) != 0;
    const bool rawLeftClick = (b3 & SteamController::BTN_TP_LT_CLICK) != 0;
    const bool rightPadConsumedByMouse =
        settings.trackpadMouseEnabled && !settings.useLeftTrackpadForMouse;

    // Nintendo labels are physically opposite from Xbox-style face labels.
    // Preserve physical position: south/east/west/north -> B/A/Y/X.
    if (b0 & SteamController::BTN_A) out.buttons |= NS2_BUTTON_B;
    if (b0 & SteamController::BTN_B) out.buttons |= NS2_BUTTON_A;
    if (b0 & SteamController::BTN_X) out.buttons |= NS2_BUTTON_Y;
    if (b0 & SteamController::BTN_Y) out.buttons |= NS2_BUTTON_X;
    if (b2 & SteamController::BTN_LB) out.buttons |= NS2_BUTTON_L;
    if (b1 & SteamController::BTN_RB) out.buttons |= NS2_BUTTON_R;
    if (b1 & SteamController::BTN_VIEW) out.buttons |= NS2_BUTTON_MINUS;
    if (b0 & SteamController::BTN_MENU) out.buttons |= NS2_BUTTON_CAPTURE;
    if (b1 & SteamController::BTN_LS) out.buttons |= NS2_BUTTON_LEFT_STICK;
    if (b0 & SteamController::BTN_RS) out.buttons |= NS2_BUTTON_RIGHT_STICK;
    if (b2 & SteamController::BTN_STEAM) out.buttons |= NS2_BUTTON_HOME;
    if (TriggerToByte(state.leftTrigger) > 0) out.buttons |= NS2_BUTTON_ZL;
    if (TriggerToByte(state.rightTrigger) > 0) out.buttons |= NS2_BUTTON_ZR;
    if (rawRightClick && !rightPadConsumedByMouse) out.buttons |= NS2_BUTTON_C;

    DpadDirection dpad = PhysicalDpadDirection(state);
    if (settings.trackpadDpadEnabled) {
        const DpadDirection trackpadDpad = settings.useRightTrackpadForDpad
            ? TrackpadDpadDirection(state.rightPadX, state.rightPadY, rawRightClick)
            : TrackpadDpadDirection(state.leftPadX, state.leftPadY, rawLeftClick);
        if (trackpadDpad != DpadDirection::None)
            dpad = trackpadDpad;
    }
    out.buttons &= ~(NS2_BUTTON_UP | NS2_BUTTON_DOWN |
                     NS2_BUTTON_LEFT | NS2_BUTTON_RIGHT);
    out.buttons |= SwitchDpadButtons(dpad);

    if (state.hasImu) {
        out.gyroX = state.gyroX;
        out.gyroY = state.gyroZ;
        out.gyroZ = NegI16(state.gyroY);
        out.accelX = state.accelX;
        out.accelY = state.accelZ;
        out.accelZ = NegI16(state.accelY);
    }

    ApplyBackButtonMappingsSwitch2Pro(state, settings.backButtonMappings, out);
    return out;
}

bool DecodeViiperXbox360Feedback(const uint8_t* data,
                                 size_t size,
                                 uint8_t& largeMotor,
                                 uint8_t& smallMotor) {
    if (!data || size < 2)
        return false;
    largeMotor = data[0];
    smallMotor = data[1];
    return true;
}

bool DecodeViiperDualShock4Feedback(const uint8_t* data,
                                    size_t size,
                                    uint8_t& largeMotor,
                                    uint8_t& smallMotor) {
    if (!data || size < 7)
        return false;
    smallMotor = data[0];
    largeMotor = data[1];
    return true;
}

bool DecodeViiperDualSenseFeedback(const uint8_t* data,
                                   size_t size,
                                   ViiperDualSenseFeedbackState& feedback) {
    if (!data || size < 27)
        return false;

    feedback.enableBits1 = data[0];
    feedback.enableBits2 = data[1];
    feedback.rumbleRight = data[2];
    feedback.rumbleLeft = data[3];
    feedback.enableBits3 = data[4];
    std::copy(data + 5, data + 16, feedback.rightTriggerEffect.begin());
    std::copy(data + 16, data + 27, feedback.leftTriggerEffect.begin());
    return true;
}

bool DecodeViiperDualSenseAudioHaptics(const uint8_t* data,
                                       size_t size,
                                       ViiperDualSenseAudioHapticsState& feedback) {
    if (!data || size < 16)
        return false;

    feedback.sequence = static_cast<uint32_t>(data[0])
                      | (static_cast<uint32_t>(data[1]) << 8)
                      | (static_cast<uint32_t>(data[2]) << 16)
                      | (static_cast<uint32_t>(data[3]) << 24);
    feedback.leftEnergy = static_cast<uint16_t>(data[4])
                        | (static_cast<uint16_t>(data[5]) << 8);
    feedback.rightEnergy = static_cast<uint16_t>(data[6])
                         | (static_cast<uint16_t>(data[7]) << 8);
    feedback.leftPeak = static_cast<uint16_t>(data[8])
                      | (static_cast<uint16_t>(data[9]) << 8);
    feedback.rightPeak = static_cast<uint16_t>(data[10])
                       | (static_cast<uint16_t>(data[11]) << 8);
    feedback.leftTransient = static_cast<uint16_t>(data[12])
                           | (static_cast<uint16_t>(data[13]) << 8);
    feedback.rightTransient = static_cast<uint16_t>(data[14])
                            | (static_cast<uint16_t>(data[15]) << 8);
    return true;
}

bool DecodeViiperSwitch2ProFeedback(const uint8_t* data,
                                    size_t size,
                                    ViiperSwitch2ProFeedbackState& feedback) {
    if (!data || size < 34)
        return false;

    std::copy(data, data + 16, feedback.leftRumble.begin());
    std::copy(data + 16, data + 32, feedback.rightRumble.begin());
    feedback.flags = data[32];
    feedback.playerLedMask = data[33];
    return true;
}
