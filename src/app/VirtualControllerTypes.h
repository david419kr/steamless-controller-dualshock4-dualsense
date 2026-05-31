#pragma once

#include "BackButtonMapping.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct SteamControllerState;

enum class VirtualControllerMode {
    Xbox360 = 0,
    DualShock4 = 1,
    DualSense = 2,
};

enum class VirtualControllerError {
    None = 0,
    ViiperUnavailable,
    ViiperExeMissing,
    ViiperStartFailed,
    ViiperUnsupported,
    UsbIpDriverMissing,
    BusCreateFailed,
    DeviceCreateFailed,
    StreamConnectFailed,
    StreamWriteFailed,
};

struct VirtualControllerRuntimeSettings {
    bool trackpadMouseEnabled = false;
    bool useLeftTrackpadForMouse = false;
    bool trackpadDpadEnabled = false;
    bool useRightTrackpadForDpad = false;
    BackButtonMappings backButtonMappings;
};

struct ViiperXbox360InputState {
    uint32_t buttons = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    int16_t leftStickX = 0;
    int16_t leftStickY = 0;
    int16_t rightStickX = 0;
    int16_t rightStickY = 0;
    std::array<uint8_t, 6> reserved{};

    std::array<uint8_t, 20> Serialize() const;
};

struct ViiperDualShock4InputState {
    int8_t leftStickX = 0;
    int8_t leftStickY = 0;
    int8_t rightStickX = 0;
    int8_t rightStickY = 0;
    uint16_t buttons = 0;
    uint8_t dpad = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    uint16_t touch1X = 0;
    uint16_t touch1Y = 0;
    bool touch1Active = false;
    uint16_t touch2X = 0;
    uint16_t touch2Y = 0;
    bool touch2Active = false;
    int16_t gyroX = 0;
    int16_t gyroY = 0;
    int16_t gyroZ = 0;
    int16_t accelX = 0;
    int16_t accelY = 0;
    int16_t accelZ = 0;

    std::array<uint8_t, 31> Serialize() const;
};

struct ViiperDualSenseInputState {
    uint8_t leftStickX = 0x80;
    uint8_t leftStickY = 0x80;
    uint8_t rightStickX = 0x80;
    uint8_t rightStickY = 0x80;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    uint8_t dpad = 0;
    uint16_t buttons = 0;
    uint16_t touch1X = 0;
    uint16_t touch1Y = 0;
    bool touch1Active = false;
    uint16_t touch2X = 0;
    uint16_t touch2Y = 0;
    bool touch2Active = false;
    int16_t gyroX = 0;
    int16_t gyroY = 0;
    int16_t gyroZ = 0;
    int16_t accelX = 0;
    int16_t accelY = 0;
    int16_t accelZ = 0;
    uint8_t batteryLevelPercent = 100;
    uint8_t chargeState = 0;

    std::array<uint8_t, 33> Serialize() const;
};

struct ViiperDualSenseFeedbackState {
    uint8_t enableBits1 = 0;
    uint8_t enableBits2 = 0;
    uint8_t rumbleRight = 0;
    uint8_t rumbleLeft = 0;
    uint8_t enableBits3 = 0;
    std::array<uint8_t, 11> rightTriggerEffect{};
    std::array<uint8_t, 11> leftTriggerEffect{};

    uint8_t LargeMotor() const { return rumbleLeft; }
    uint8_t SmallMotor() const { return rumbleRight; }
};

struct ViiperDualSenseAudioHapticsState {
    uint32_t sequence = 0;
    uint16_t leftEnergy = 0;
    uint16_t rightEnergy = 0;
    uint16_t leftPeak = 0;
    uint16_t rightPeak = 0;
    uint16_t leftTransient = 0;
    uint16_t rightTransient = 0;
};

struct ViiperFeedbackState {
    VirtualControllerMode mode = VirtualControllerMode::Xbox360;
    uint8_t largeMotor = 0;
    uint8_t smallMotor = 0;
    bool isDualSenseAudio = false;
    ViiperDualSenseFeedbackState dualSense;
    ViiperDualSenseAudioHapticsState dualSenseAudio;
};

ViiperXbox360InputState BuildViiperXbox360Input(
    const SteamControllerState& state,
    const VirtualControllerRuntimeSettings& settings);

ViiperDualShock4InputState BuildViiperDualShock4Input(
    const SteamControllerState& state,
    const VirtualControllerRuntimeSettings& settings);

ViiperDualSenseInputState BuildViiperDualSenseInput(
    const SteamControllerState& state,
    const VirtualControllerRuntimeSettings& settings);

bool DecodeViiperXbox360Feedback(const uint8_t* data,
                                 size_t size,
                                 uint8_t& largeMotor,
                                 uint8_t& smallMotor);

bool DecodeViiperDualShock4Feedback(const uint8_t* data,
                                    size_t size,
                                    uint8_t& largeMotor,
                                    uint8_t& smallMotor);

bool DecodeViiperDualSenseFeedback(const uint8_t* data,
                                   size_t size,
                                   ViiperDualSenseFeedbackState& feedback);

bool DecodeViiperDualSenseAudioHaptics(const uint8_t* data,
                                       size_t size,
                                       ViiperDualSenseAudioHapticsState& feedback);
