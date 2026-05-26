#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <functional>
#include <thread>

struct SteamControllerState;

enum class VirtualControllerMode {
    Xbox360 = 0,
    DualShock4 = 1,
};

class VirtualController {
public:
    using RumbleFn = std::function<void(uint8_t largeMotor, uint8_t smallMotor)>;

    explicit VirtualController(VirtualControllerMode mode = VirtualControllerMode::Xbox360,
                               RumbleFn rumbleFn = {});
    ~VirtualController();
    VirtualController(const VirtualController&) = delete;
    VirtualController& operator=(const VirtualController&) = delete;

    bool IsValid()          const { return m_valid; }
    bool IsDriverMissing()  const { return m_driverMissing; }

    VirtualControllerMode Mode() const { return m_mode; }

    void Update(const SteamControllerState& state);
    void SetBatteryState(uint8_t levelPercent, uint8_t chargeState);
    void SetTrackpadMouseClaim(bool enabled, bool useLeftTrackpad);
    void SetTrackpadDpadClaim(bool enabled, bool useRightTrackpad);
    void OnRumble(uint8_t largeMotor, uint8_t smallMotor);

private:
    void* m_client       = nullptr;
    void* m_target       = nullptr;
    VirtualControllerMode m_mode = VirtualControllerMode::Xbox360;
    RumbleFn m_rumbleFn;
    bool  m_valid        = false;
    bool  m_driverMissing = false;
    uint16_t m_ds4Timestamp = 0;
    uint32_t m_lastImuTimestamp = 0;
    bool m_hasLastImuTimestamp = false;
    uint8_t m_touchPacketCounter = 0;
    uint8_t m_rightTracking = 0;
    uint8_t m_leftTracking = 0;
    bool m_wasRightTouching = false;
    bool m_wasLeftTouching = false;
    uint8_t m_ds4BatteryLevel = 0x0B;
    uint8_t m_ds4BatterySpecial = 0x1B;
    bool m_trackpadMouseEnabled = false;
    bool m_useLeftTrackpadForMouse = false;
    bool m_trackpadDpadEnabled = false;
    bool m_useRightTrackpadForDpad = false;
    std::atomic<bool> m_ds4OutputRunning{false};
    std::thread m_ds4OutputThread;
};
