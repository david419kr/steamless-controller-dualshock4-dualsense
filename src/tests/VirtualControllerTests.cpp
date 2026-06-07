#include "app/ViiperClient.h"
#include "app/VirtualControllerTypes.h"
#include "steam/SteamController.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void ExpectEq(uint32_t actual, uint32_t expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " actual=" << actual
                  << " expected=" << expected << "\n";
        std::exit(1);
    }
}

void SetButtons(SteamControllerState& state,
                uint8_t b0,
                uint8_t b1,
                uint8_t b2,
                uint8_t b3) {
    state.buttons = static_cast<uint32_t>(b0) |
                    (static_cast<uint32_t>(b1) << 8) |
                    (static_cast<uint32_t>(b2) << 16) |
                    (static_cast<uint32_t>(b3) << 24);
}

std::string NullTerminated(std::string value) {
    value.push_back('\0');
    return value;
}

void TestViiperRequestHelpers() {
    Expect(BuildViiperRequest("ping") == NullTerminated("ping"),
           "VIIPER ping request framing");
    Expect(BuildViiperRequest("bus/create", "0") == NullTerminated("bus/create 0"),
           "VIIPER payload request framing");
    Expect(BuildViiperStreamPath(7, "3") == NullTerminated("bus/7/3"),
           "VIIPER stream path framing");

    std::string server;
    std::string version;
    Expect(ParseViiperPingResponse("{\"server\":\"VIIPER\",\"version\":\"v0.6.1\"}",
                                   server,
                                   version),
           "VIIPER ping JSON parse");
    Expect(server == "VIIPER" && version == "v0.6.1", "VIIPER ping fields");
    Expect(IsViiperVersionSupported("v0.6.1"), "VIIPER v0.6.1 supported");
    Expect(IsViiperVersionSupported("0.6.1-steamless1"),
           "VIIPER patched v0.6.1 supported");
    Expect(!IsViiperVersionSupported("v0.6.0"), "VIIPER v0.6.0 unsupported");
    Expect(!IsViiperDualShock4CompatibleVersion("v0.6.1"),
           "Stock VIIPER v0.6.1 is not DS4-compatible");
    Expect(!IsViiperDualShock4CompatibleVersion("0.6.1-steamless1"),
           "Old Steamless-patched VIIPER v0.6.1 is not DS4-compatible");
    Expect(!IsViiperDualShock4CompatibleVersion("0.6.1-steamless2"),
           "Old Steamless-patched VIIPER v0.6.1 steamless2 is not DS4-compatible");
    Expect(IsViiperDualShock4CompatibleVersion("0.6.1-steamless3"),
           "Steamless-patched VIIPER v0.6.1 steamless3 is DS4-compatible");
    Expect(IsViiperDualShock4CompatibleVersion("0.6.1-steamless4"),
           "Steamless-patched VIIPER v0.6.1 steamless4 remains DS4-compatible");
    Expect(IsViiperDualShock4CompatibleVersion("0.6.1-steamless5"),
           "Steamless-patched VIIPER v0.6.1 steamless5 remains DS4-compatible");
    Expect(IsViiperDualShock4CompatibleVersion("0.6.1-steamless8"),
           "Steamless-patched VIIPER v0.6.1 steamless8 remains DS4-compatible");
    Expect(IsViiperDualShock4CompatibleVersion("0.6.1-steamless9"),
           "Future Steamless-patched VIIPER versions remain DS4-compatible");
    Expect(IsViiperDualShock4CompatibleVersion("v0.6.2"),
           "Future VIIPER versions are treated as DS4-compatible");
    Expect(!IsViiperDualSenseCompatibleVersion("v0.6.1"),
           "Stock VIIPER v0.6.1 is not DualSense-compatible");
    Expect(!IsViiperDualSenseCompatibleVersion("0.6.1-steamless3"),
           "Steamless3 VIIPER v0.6.1 is not DualSense-compatible");
    Expect(!IsViiperDualSenseCompatibleVersion("0.6.1-steamless4"),
           "Steamless4 VIIPER v0.6.1 lacks DualSense audio haptics");
    Expect(IsViiperDualSenseCompatibleVersion("0.6.1-steamless5"),
           "Steamless5 VIIPER v0.6.1 is DualSense-compatible");
    Expect(IsViiperDualSenseCompatibleVersion("0.6.1-steamless7"),
           "Steamless7 VIIPER v0.6.1 remains DualSense-compatible");
    Expect(IsViiperDualSenseCompatibleVersion("0.6.1-steamless8"),
           "Steamless8 VIIPER v0.6.1 remains DualSense-compatible");
    Expect(IsViiperDualSenseCompatibleVersion("0.6.1-steamless9"),
           "Future Steamless-patched VIIPER versions remain DualSense-compatible");
    Expect(IsViiperDualSenseCompatibleVersion("v0.6.2"),
           "Future VIIPER versions are treated as DualSense-compatible");
    Expect(!IsViiperSwitch2ProCompatibleVersion("v0.6.1"),
           "Stock VIIPER v0.6.1 is not Switch 2 Pro-compatible");
    Expect(!IsViiperSwitch2ProCompatibleVersion("0.6.1-steamless6"),
           "Steamless6 VIIPER v0.6.1 lacks Switch 2 Pro support");
    Expect(IsViiperSwitch2ProCompatibleVersion("0.6.1-steamless7"),
           "Steamless7 VIIPER v0.6.1 is Switch 2 Pro-compatible");
    Expect(IsViiperSwitch2ProCompatibleVersion("0.6.1-steamless8"),
           "Steamless8 VIIPER v0.6.1 remains Switch 2 Pro-compatible");
    Expect(IsViiperSwitch2ProCompatibleVersion("0.6.1-steamless9"),
           "Future Steamless-patched VIIPER versions remain Switch 2 Pro-compatible");
    Expect(IsViiperSwitch2ProCompatibleVersion("v0.7.0"),
           "Upstream VIIPER v0.7.0 is treated as Switch 2 Pro-compatible");
    Expect(!IsViiperSwitchProCompatibleVersion("0.6.1-steamless7"),
           "Steamless7 VIIPER v0.6.1 lacks Switch Pro support");
    Expect(IsViiperSwitchProCompatibleVersion("0.6.1-steamless8"),
           "Steamless8 VIIPER v0.6.1 is Switch Pro-compatible");
    Expect(IsViiperSwitchProCompatibleVersion("0.6.1-steamless9"),
           "Future Steamless-patched VIIPER versions remain Switch Pro-compatible");
    Expect(!IsViiperSwitchProCompatibleVersion("v0.7.0"),
           "Upstream VIIPER v0.7.0 is not treated as Switch Pro-compatible");

    uint32_t busId = 0;
    std::string devId;
    Expect(ParseViiperBusIdResponse("{\"busId\":12}", busId) && busId == 12,
           "VIIPER bus JSON parse");
    Expect(ParseViiperDeviceResponse("{\"busId\":12,\"devId\":\"5\",\"type\":\"dualshock4\"}",
                                     busId,
                                     devId) &&
               busId == 12 && devId == "5",
           "VIIPER device JSON parse");
    Expect(IsViiperUsbIpDriverMissingResponse(
               "{\"status\":409,\"title\":\"Conflict\",\"detail\":\"Failed to auto-attach device: Discovery: Discovery: usbip-win2 driver not found: No more data is available.\"}"),
           "VIIPER usbip-win2 missing response parse");
}

void TestXbox360Mapping() {
    SteamControllerState state{};
    SetButtons(state,
               SteamController::BTN_A | SteamController::BTN_MENU,
               SteamController::BTN_DPAD_UP | SteamController::BTN_RB |
                   SteamController::BTN_VIEW | SteamController::BTN_LS,
               SteamController::BTN_LB | SteamController::BTN_STEAM,
               0);
    state.leftTrigger = 32767;
    state.rightTrigger = 16384;
    state.leftStickX = -1234;
    state.leftStickY = 2345;
    state.rightStickX = -32768;
    state.rightStickY = 32767;

    VirtualControllerRuntimeSettings settings{};
    const ViiperXbox360InputState input = BuildViiperXbox360Input(state, settings);
    ExpectEq(input.buttons,
             0x0001 | 0x0010 | 0x0020 | 0x0040 | 0x0100 |
                 0x0200 | 0x0400 | 0x1000,
             "Xbox360 button mapping");
    ExpectEq(input.leftTrigger, 255, "Xbox360 left trigger");
    ExpectEq(input.rightTrigger, 128, "Xbox360 right trigger");
    Expect(input.leftStickX == -1234 && input.leftStickY == 2345,
           "Xbox360 left stick raw values");
    Expect(input.rightStickX == -32768 && input.rightStickY == 32767,
           "Xbox360 right stick raw values");

    const auto serialized = input.Serialize();
    ExpectEq(serialized[0], input.buttons & 0xFF, "Xbox360 serialize buttons byte 0");
    ExpectEq(serialized[4], 255, "Xbox360 serialize LT");
    ExpectEq(serialized[5], 128, "Xbox360 serialize RT");
}

void TestXbox360TrackpadDpadAndBackButtons() {
    SteamControllerState state{};
    SetButtons(state,
               0,
               SteamController::BTN_DPAD_UP,
               SteamController::BTN_L4,
               SteamController::BTN_TP_LT_CLICK);
    state.leftPadX = -20000;
    state.leftPadY = 0;

    VirtualControllerRuntimeSettings settings{};
    settings.trackpadDpadEnabled = true;
    settings.useRightTrackpadForDpad = false;
    settings.backButtonMappings.Set(BackButtonId::L4, BackButtonAction::RightTrigger);

    const ViiperXbox360InputState input = BuildViiperXbox360Input(state, settings);
    Expect((input.buttons & 0x0001) == 0, "Xbox360 trackpad D-pad overrides physical up");
    Expect((input.buttons & 0x0004) != 0, "Xbox360 left trackpad maps to D-pad left");
    ExpectEq(input.rightTrigger, 255, "Xbox360 back button can map to RT");
}

void TestDualShock4Mapping() {
    SteamControllerState state{};
    SetButtons(state,
               SteamController::BTN_A | SteamController::BTN_B |
                   SteamController::BTN_X | SteamController::BTN_Y |
                   SteamController::BTN_MENU,
               SteamController::BTN_VIEW | SteamController::BTN_LS |
                   SteamController::BTN_RB,
               SteamController::BTN_LB | SteamController::BTN_STEAM,
               0);
    state.leftTrigger = 32767;
    state.leftStickX = 0;
    state.leftStickY = 0;
    state.rightStickX = 0;
    state.rightStickY = 0;
    state.hasImu = true;
    state.gyroX = 11;
    state.gyroY = 22;
    state.gyroZ = 33;
    state.accelX = 44;
    state.accelY = 55;
    state.accelZ = 66;

    VirtualControllerRuntimeSettings settings{};
    const ViiperDualShock4InputState input = BuildViiperDualShock4Input(state, settings);
    ExpectEq(input.buttons,
             0x0001 | 0x0010 | 0x0020 | 0x0040 | 0x0080 |
                 0x0100 | 0x0200 | 0x0400 | 0x1000 |
                 0x2000 | 0x4000,
             "DS4 button mapping");
    ExpectEq(static_cast<uint8_t>(input.leftStickX), 0, "DS4 neutral LX");
    ExpectEq(static_cast<uint8_t>(input.leftStickY), 0, "DS4 neutral LY");
    ExpectEq(input.leftTrigger, 255, "DS4 left trigger");
    Expect(input.gyroX == 11 && input.gyroY == 33 && input.gyroZ == -22,
           "DS4 gyro axis mapping");
    Expect(input.accelX == 44 && input.accelY == 66 && input.accelZ == -55,
           "DS4 accel axis mapping");

    const auto serialized = input.Serialize();
    ExpectEq(serialized[4], input.buttons & 0xFF, "DS4 serialize buttons low");
    ExpectEq(serialized[7], 255, "DS4 serialize L2");
}

void TestDualShock4TouchSuppressionAndBackButtons() {
    SteamControllerState state{};
    SetButtons(state,
               0,
               0,
               SteamController::BTN_TP_RT,
               SteamController::BTN_TP_LT | SteamController::BTN_TP_LT_CLICK);
    state.rightPadX = 0;
    state.rightPadY = 0;
    state.leftPadX = -20000;
    state.leftPadY = 0;

    VirtualControllerRuntimeSettings settings{};
    settings.trackpadDpadEnabled = true;
    settings.useRightTrackpadForDpad = false;
    settings.backButtonMappings.Set(BackButtonId::R5, BackButtonAction::Guide);

    const ViiperDualShock4InputState input = BuildViiperDualShock4Input(state, settings);
    ExpectEq(input.dpad, 0x04, "DS4 left trackpad maps to D-pad left");
    Expect(input.touch1Active, "DS4 right trackpad remains touch slot 1");
    Expect(!input.touch2Active, "DS4 left trackpad suppressed when used as D-pad");
    Expect((input.buttons & 0x0002) == 0,
           "DS4 suppressed left trackpad click is not touchpad click");

    SteamControllerState clickOnly{};
    SetButtons(clickOnly,
               SteamController::BTN_MENU,
               0,
               SteamController::BTN_TP_RT_CLICK,
               0);
    clickOnly.rightPadX = 12000;
    clickOnly.rightPadY = -8000;
    VirtualControllerRuntimeSettings noSuppression{};
    const ViiperDualShock4InputState clickInput =
        BuildViiperDualShock4Input(clickOnly, noSuppression);
    Expect(clickInput.touch1Active,
           "DS4 touchpad click forces touch slot active even if touch bit is absent");
    Expect((clickInput.buttons & 0x0002) != 0, "DS4 touchpad click button set");
    Expect((clickInput.buttons & 0x2000) == 0,
           "DS4 OPTIONS is suppressed during a trackpad click");

    SetButtons(state, 0, SteamController::BTN_R5, 0, 0);
    const ViiperDualShock4InputState mapped = BuildViiperDualShock4Input(state, settings);
    Expect((mapped.buttons & 0x0001) != 0, "DS4 back button can map to PS");
}

void TestDualSenseMapping() {
    SteamControllerState state{};
    SetButtons(state,
               SteamController::BTN_A | SteamController::BTN_B |
                   SteamController::BTN_X | SteamController::BTN_Y |
                   SteamController::BTN_MENU,
               SteamController::BTN_VIEW | SteamController::BTN_LS |
                   SteamController::BTN_RB,
               SteamController::BTN_LB | SteamController::BTN_STEAM,
               0);
    state.leftTrigger = 32767;
    state.rightTrigger = 16384;
    state.hasImu = true;
    state.gyroX = 11;
    state.gyroY = 22;
    state.gyroZ = 33;
    state.accelX = 44;
    state.accelY = 55;
    state.accelZ = 66;

    VirtualControllerRuntimeSettings settings{};
    const ViiperDualSenseInputState input = BuildViiperDualSenseInput(state, settings);
    ExpectEq(input.buttons,
             0x0001 | 0x0010 | 0x0020 | 0x0040 | 0x0080 |
                 0x0100 | 0x0200 | 0x0400 | 0x0800 | 0x1000 |
                 0x2000 | 0x4000,
             "DualSense button mapping");
    ExpectEq(input.leftStickX, 128, "DualSense neutral LX");
    ExpectEq(input.leftStickY, 128, "DualSense neutral LY");
    ExpectEq(input.leftTrigger, 255, "DualSense left trigger");
    ExpectEq(input.rightTrigger, 128, "DualSense right trigger");
    Expect(input.gyroX == 11 && input.gyroY == 33 && input.gyroZ == -22,
           "DualSense gyro axis mapping");
    Expect(input.accelX == 44 && input.accelY == 66 && input.accelZ == -55,
           "DualSense accel axis mapping");

    const auto serialized = input.Serialize();
    ExpectEq(serialized[0], 128, "DualSense serialize LX");
    ExpectEq(serialized[4], 255, "DualSense serialize L2");
    ExpectEq(serialized[7], input.buttons & 0xFF, "DualSense serialize buttons low");
}

void TestDualSenseTouchSuppressionAndBackButtons() {
    SteamControllerState state{};
    SetButtons(state,
               0,
               SteamController::BTN_R5,
               SteamController::BTN_TP_RT,
               SteamController::BTN_TP_LT | SteamController::BTN_TP_LT_CLICK);
    state.rightPadX = 0;
    state.rightPadY = 0;
    state.leftPadX = -20000;
    state.leftPadY = 0;

    VirtualControllerRuntimeSettings settings{};
    settings.trackpadDpadEnabled = true;
    settings.useRightTrackpadForDpad = false;
    settings.backButtonMappings.Set(BackButtonId::R5, BackButtonAction::Guide);

    const ViiperDualSenseInputState input = BuildViiperDualSenseInput(state, settings);
    ExpectEq(input.dpad, 0x04, "DualSense left trackpad maps to D-pad left");
    Expect(input.touch1Active, "DualSense right trackpad remains touch slot 1");
    Expect(!input.touch2Active, "DualSense left trackpad suppressed when used as D-pad");
    Expect((input.buttons & 0x0002) == 0,
           "DualSense suppressed left trackpad click is not touchpad click");
    Expect((input.buttons & 0x0001) != 0, "DualSense back button can map to PS");
}

void TestSwitch2ProMapping() {
    SteamControllerState state{};
    SetButtons(state,
               SteamController::BTN_A | SteamController::BTN_B |
                   SteamController::BTN_X | SteamController::BTN_Y |
                   SteamController::BTN_MENU,
               SteamController::BTN_VIEW | SteamController::BTN_LS |
                   SteamController::BTN_RB,
               SteamController::BTN_LB | SteamController::BTN_STEAM |
                   SteamController::BTN_TP_RT_CLICK,
               0);
    state.leftTrigger = 32767;
    state.rightTrigger = 16384;
    state.leftStickX = 0;
    state.leftStickY = 0;
    state.rightStickX = 32767;
    state.rightStickY = -32768;
    state.hasImu = true;
    state.gyroX = 11;
    state.gyroY = 22;
    state.gyroZ = 33;
    state.accelX = 44;
    state.accelY = 55;
    state.accelZ = 66;

    VirtualControllerRuntimeSettings settings{};
    settings.backButtonMappings.Set(BackButtonId::L4, BackButtonAction::GL);
    settings.backButtonMappings.Set(BackButtonId::R4, BackButtonAction::GR);

    ViiperSwitch2ProInputState input = BuildViiperSwitch2ProInput(state, settings);
    ExpectEq(input.buttons,
             (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) |
                 (1u << 4) | (1u << 5) |
                 (1u << 12) | (1u << 13) | (1u << 14) |
                 (1u << 15) | (1u << 16) | (1u << 17) |
                 (1u << 20),
             "Switch 2 Pro base button mapping");
    ExpectEq(input.leftStickX, 2048, "Switch 2 Pro neutral LX");
    ExpectEq(input.leftStickY, 2048, "Switch 2 Pro neutral LY");
    ExpectEq(input.rightStickX, 4095, "Switch 2 Pro max RX");
    ExpectEq(input.rightStickY, 4095, "Switch 2 Pro inverted min RY");
    Expect(input.gyroX == 11 && input.gyroY == 33 && input.gyroZ == -22,
           "Switch 2 Pro gyro axis mapping");
    Expect(input.accelX == 44 && input.accelY == 66 && input.accelZ == -55,
           "Switch 2 Pro accel axis mapping");

    const auto serialized = input.Serialize();
    ExpectEq(serialized[0], input.buttons & 0xFF, "Switch 2 Pro serialize buttons low");
    ExpectEq(serialized[4], input.leftStickX & 0xFF, "Switch 2 Pro serialize LX");
    ExpectEq(serialized[18], 11, "Switch 2 Pro serialize gyro X");

    settings.trackpadMouseEnabled = true;
    settings.useLeftTrackpadForMouse = false;
    input = BuildViiperSwitch2ProInput(state, settings);
    Expect((input.buttons & (1u << 20)) == 0,
           "Switch 2 Pro C button is suppressed while right trackpad is mouse");
}

void TestSwitch2ProTrackpadDpadAndBackButtons() {
    SteamControllerState state{};
    SetButtons(state,
               0,
               SteamController::BTN_DPAD_UP | SteamController::BTN_R5,
               SteamController::BTN_L4,
               SteamController::BTN_TP_LT_CLICK);
    state.leftPadX = -20000;
    state.leftPadY = 0;

    VirtualControllerRuntimeSettings settings{};
    settings.trackpadDpadEnabled = true;
    settings.useRightTrackpadForDpad = false;
    settings.backButtonMappings.Set(BackButtonId::L4, BackButtonAction::GL);
    settings.backButtonMappings.Set(BackButtonId::R5, BackButtonAction::GR);

    const ViiperSwitch2ProInputState input = BuildViiperSwitch2ProInput(state, settings);
    Expect((input.buttons & (1u << 11)) == 0,
           "Switch 2 Pro trackpad D-pad overrides physical up");
    Expect((input.buttons & (1u << 10)) != 0,
           "Switch 2 Pro left trackpad maps to D-pad left");
    Expect((input.buttons & (1u << 19)) != 0,
           "Switch 2 Pro L4 maps to GL");
    Expect((input.buttons & (1u << 18)) != 0,
           "Switch 2 Pro R5 can map to GR");
}

void TestSwitchProMapping() {
    SteamControllerState state{};
    SetButtons(state,
               SteamController::BTN_A | SteamController::BTN_B |
                   SteamController::BTN_X | SteamController::BTN_Y |
                   SteamController::BTN_MENU,
               SteamController::BTN_VIEW | SteamController::BTN_LS |
                   SteamController::BTN_RB,
               SteamController::BTN_LB | SteamController::BTN_STEAM |
                   SteamController::BTN_TP_RT_CLICK,
               0);
    state.leftTrigger = 32767;
    state.rightTrigger = 16384;
    state.leftStickX = 0;
    state.leftStickY = 0;
    state.rightStickX = 32767;
    state.rightStickY = -32768;
    state.hasImu = true;
    state.gyroX = 11;
    state.gyroY = 22;
    state.gyroZ = 33;
    state.accelX = 44;
    state.accelY = 55;
    state.accelZ = 66;

    VirtualControllerRuntimeSettings settings{};
    settings.backButtonMappings.Set(BackButtonId::L4, BackButtonAction::GL);
    settings.backButtonMappings.Set(BackButtonId::R4, BackButtonAction::GR);

    const ViiperSwitchProInputState input = BuildViiperSwitchProInput(state, settings);
    ExpectEq(input.buttons,
             (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) |
                 (1u << 4) | (1u << 5) |
                 (1u << 12) | (1u << 13) | (1u << 14) |
                 (1u << 15) | (1u << 16) | (1u << 17),
             "Switch Pro base button mapping has no C/GL/GR");
    ExpectEq(input.leftStickX, 2048, "Switch Pro neutral LX");
    ExpectEq(input.leftStickY, 2048, "Switch Pro neutral LY");
    ExpectEq(input.rightStickX, 4095, "Switch Pro max RX");
    ExpectEq(input.rightStickY, 4095, "Switch Pro inverted min RY");
    Expect(input.gyroX == 11 && input.gyroY == 33 && input.gyroZ == -22,
           "Switch Pro gyro axis mapping");
    Expect(input.accelX == 44 && input.accelY == 66 && input.accelZ == -55,
           "Switch Pro accel axis mapping");

    const auto serialized = input.Serialize();
    ExpectEq(serialized[0], input.buttons & 0xFF, "Switch Pro serialize buttons low");
    ExpectEq(serialized[4], input.leftStickX & 0xFF, "Switch Pro serialize LX");
    ExpectEq(serialized[18], 11, "Switch Pro serialize gyro X");
}

void TestSwitchProTrackpadDpadAndBackButtons() {
    SteamControllerState state{};
    SetButtons(state,
               0,
               SteamController::BTN_DPAD_UP | SteamController::BTN_R5,
               SteamController::BTN_L4,
               SteamController::BTN_TP_LT_CLICK);
    state.leftPadX = -20000;
    state.leftPadY = 0;

    VirtualControllerRuntimeSettings settings{};
    settings.trackpadDpadEnabled = true;
    settings.useRightTrackpadForDpad = false;
    settings.backButtonMappings.Set(BackButtonId::L4, BackButtonAction::GL);
    settings.backButtonMappings.Set(BackButtonId::R5, BackButtonAction::RightTrigger);

    const ViiperSwitchProInputState input = BuildViiperSwitchProInput(state, settings);
    Expect((input.buttons & (1u << 11)) == 0,
           "Switch Pro trackpad D-pad overrides physical up");
    Expect((input.buttons & (1u << 10)) != 0,
           "Switch Pro left trackpad maps to D-pad left");
    Expect((input.buttons & (1u << 19)) == 0,
           "Switch Pro GL action is no-op");
    Expect((input.buttons & (1u << 18)) == 0,
           "Switch Pro GR action is no-op");
    Expect((input.buttons & (1u << 5)) != 0,
           "Switch Pro back button can map to ZR");
}

ViiperDualShock4InputState BuildLeftTrackpadDpadDs4(int16_t x, int16_t y) {
    SteamControllerState state{};
    SetButtons(state, 0, 0, 0, SteamController::BTN_TP_LT_CLICK);
    state.leftPadX = x;
    state.leftPadY = y;

    VirtualControllerRuntimeSettings settings{};
    settings.trackpadDpadEnabled = true;
    settings.useRightTrackpadForDpad = false;
    return BuildViiperDualShock4Input(state, settings);
}

void TestTrackpadDpadSectorWidths() {
    ExpectEq(BuildLeftTrackpadDpadDs4(30000, 15000).dpad,
             0x08,
             "Trackpad D-pad east keeps 80 degree cardinal sector");
    ExpectEq(BuildLeftTrackpadDpadDs4(30000, 25000).dpad,
             0x08,
             "Trackpad D-pad east owns angle just below northeast");
    ExpectEq(BuildLeftTrackpadDpadDs4(20000, 20000).dpad,
             0x01 | 0x08,
             "Trackpad D-pad northeast uses 10 degree diagonal sector");
    ExpectEq(BuildLeftTrackpadDpadDs4(25000, 30000).dpad,
             0x01,
             "Trackpad D-pad north owns angle just above northeast");
    ExpectEq(BuildLeftTrackpadDpadDs4(15000, 30000).dpad,
             0x01,
             "Trackpad D-pad north keeps 80 degree cardinal sector");
    ExpectEq(BuildLeftTrackpadDpadDs4(-20000, 20000).dpad,
             0x01 | 0x04,
             "Trackpad D-pad northwest uses 10 degree diagonal sector");
    ExpectEq(BuildLeftTrackpadDpadDs4(-30000, 15000).dpad,
             0x04,
             "Trackpad D-pad west keeps 80 degree cardinal sector");
    ExpectEq(BuildLeftTrackpadDpadDs4(-20000, -20000).dpad,
             0x02 | 0x04,
             "Trackpad D-pad southwest uses 10 degree diagonal sector");
    ExpectEq(BuildLeftTrackpadDpadDs4(15000, -30000).dpad,
             0x02,
             "Trackpad D-pad south keeps 80 degree cardinal sector");
    ExpectEq(BuildLeftTrackpadDpadDs4(20000, -20000).dpad,
             0x02 | 0x08,
             "Trackpad D-pad southeast uses 10 degree diagonal sector");
}

void TestFeedbackDecoding() {
    uint8_t large = 0;
    uint8_t small = 0;
    const uint8_t x360[] = {200, 40};
    Expect(DecodeViiperXbox360Feedback(x360, sizeof(x360), large, small),
           "Xbox360 feedback decode");
    ExpectEq(large, 200, "Xbox360 feedback large");
    ExpectEq(small, 40, "Xbox360 feedback small");

    const uint8_t ds4[] = {10, 240, 1, 2, 3, 4, 5};
    Expect(DecodeViiperDualShock4Feedback(ds4, sizeof(ds4), large, small),
           "DS4 feedback decode");
    ExpectEq(large, 240, "DS4 feedback large");
    ExpectEq(small, 10, "DS4 feedback small");

    ViiperDualSenseFeedbackState dualSense{};
    uint8_t ds5[27] = {};
    ds5[0] = 0x03;
    ds5[1] = 0x04;
    ds5[2] = 33;
    ds5[3] = 220;
    ds5[4] = 0x04;
    ds5[5] = 0x02;
    ds5[16] = 0x03;
    ds5[26] = 0x7F;
    Expect(DecodeViiperDualSenseFeedback(ds5, sizeof(ds5), dualSense),
           "DualSense feedback decode");
    ExpectEq(dualSense.enableBits1, 0x03, "DualSense feedback enable bits 1");
    ExpectEq(dualSense.rumbleLeft, 220, "DualSense feedback left rumble");
    ExpectEq(dualSense.rumbleRight, 33, "DualSense feedback right rumble");
    ExpectEq(dualSense.enableBits3, 0x04, "DualSense feedback enable bits 3");
    ExpectEq(dualSense.rightTriggerEffect[0], 0x02, "DualSense right trigger effect");
    ExpectEq(dualSense.leftTriggerEffect[0], 0x03, "DualSense left trigger effect");
    ExpectEq(dualSense.leftTriggerEffect[10], 0x7F, "DualSense left trigger effect tail");

    uint8_t ds5Audio[16] = {};
    ds5Audio[0] = 0x2A;
    ds5Audio[4] = 0x34;
    ds5Audio[5] = 0x12;
    ds5Audio[6] = 0x78;
    ds5Audio[7] = 0x56;
    ds5Audio[8] = 0xAB;
    ds5Audio[9] = 0x89;
    ds5Audio[10] = 0xEF;
    ds5Audio[11] = 0xCD;
    ds5Audio[12] = 0x11;
    ds5Audio[13] = 0x22;
    ds5Audio[14] = 0x33;
    ds5Audio[15] = 0x44;
    ViiperDualSenseAudioHapticsState audio{};
    Expect(DecodeViiperDualSenseAudioHaptics(ds5Audio, sizeof(ds5Audio), audio),
           "DualSense audio haptics decode");
    ExpectEq(audio.sequence, 0x2A, "DualSense audio sequence");
    ExpectEq(audio.leftEnergy, 0x1234, "DualSense audio left energy");
    ExpectEq(audio.rightEnergy, 0x5678, "DualSense audio right energy");
    ExpectEq(audio.leftPeak, 0x89AB, "DualSense audio left peak");
    ExpectEq(audio.rightPeak, 0xCDEF, "DualSense audio right peak");
    ExpectEq(audio.leftTransient, 0x2211, "DualSense audio left transient");
    ExpectEq(audio.rightTransient, 0x4433, "DualSense audio right transient");

    uint8_t ns2[34] = {};
    ns2[0] = 0x10;
    ns2[15] = 0x20;
    ns2[16] = 0x30;
    ns2[31] = 0x40;
    ns2[32] = 0x01;
    ns2[33] = 0x0F;
    ViiperSwitch2ProFeedbackState switch2{};
    Expect(DecodeViiperSwitch2ProFeedback(ns2, sizeof(ns2), switch2),
           "Switch 2 Pro feedback decode");
    ExpectEq(switch2.leftRumble[0], 0x10, "Switch 2 Pro left rumble head");
    ExpectEq(switch2.leftRumble[15], 0x20, "Switch 2 Pro left rumble tail");
    ExpectEq(switch2.rightRumble[0], 0x30, "Switch 2 Pro right rumble head");
    ExpectEq(switch2.rightRumble[15], 0x40, "Switch 2 Pro right rumble tail");
    ExpectEq(switch2.flags, 0x01, "Switch 2 Pro feedback flags");
    ExpectEq(switch2.playerLedMask, 0x0F, "Switch 2 Pro player LED mask");

    uint8_t ns[10] = {};
    ns[0] = 0x00;
    ns[1] = 0x01;
    ns[2] = 0x40;
    ns[3] = 0x40;
    ns[4] = 0x22;
    ns[5] = 0x33;
    ns[6] = 0x44;
    ns[7] = 0x55;
    ns[8] = 0x01;
    ns[9] = 0x03;
    ViiperSwitchProFeedbackState switchPro{};
    Expect(DecodeViiperSwitchProFeedback(ns, sizeof(ns), switchPro),
           "Switch Pro feedback decode");
    ExpectEq(switchPro.leftRumble[0], 0x00, "Switch Pro left rumble head");
    ExpectEq(switchPro.leftRumble[3], 0x40, "Switch Pro left rumble tail");
    ExpectEq(switchPro.rightRumble[0], 0x22, "Switch Pro right rumble head");
    ExpectEq(switchPro.rightRumble[3], 0x55, "Switch Pro right rumble tail");
    ExpectEq(switchPro.flags, 0x01, "Switch Pro feedback flags");
    ExpectEq(switchPro.playerLedMask, 0x03, "Switch Pro player LED mask");
}

} // namespace

int main() {
    TestViiperRequestHelpers();
    TestXbox360Mapping();
    TestXbox360TrackpadDpadAndBackButtons();
    TestDualShock4Mapping();
    TestDualShock4TouchSuppressionAndBackButtons();
    TestDualSenseMapping();
    TestDualSenseTouchSuppressionAndBackButtons();
    TestSwitch2ProMapping();
    TestSwitch2ProTrackpadDpadAndBackButtons();
    TestSwitchProMapping();
    TestSwitchProTrackpadDpadAndBackButtons();
    TestTrackpadDpadSectorWidths();
    TestFeedbackDecoding();

    std::cout << "SteamlessControllerTests passed\n";
    return 0;
}
