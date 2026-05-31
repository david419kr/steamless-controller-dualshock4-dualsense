#pragma once
#include "BackButtonMapping.h"
#include "ViiperClient.h"
#include "VirtualControllerTypes.h"
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <array>
#include <functional>

struct SteamControllerState;

class VirtualController {
public:
    using FeedbackFn = std::function<void(const ViiperFeedbackState& feedback)>;

    explicit VirtualController(VirtualControllerMode mode = VirtualControllerMode::Xbox360,
                               FeedbackFn feedbackFn = {});
    ~VirtualController();
    VirtualController(const VirtualController&) = delete;
    VirtualController& operator=(const VirtualController&) = delete;

    bool IsValid() const { return m_valid.load(std::memory_order_relaxed); }
    bool IsDriverMissing() const { return m_error != VirtualControllerError::None; }
    VirtualControllerError Error() const { return m_error; }

    VirtualControllerMode Mode() const { return m_mode; }

    void Update(const SteamControllerState& state);
    void SetBatteryState(uint8_t levelPercent, uint8_t chargeState);
    void SetTrackpadMouseClaim(bool enabled, bool useLeftTrackpad);
    void SetTrackpadDpadClaim(bool enabled, bool useRightTrackpad);
    void SetBackButtonMappings(const BackButtonMappings& mappings);
    void OnFeedback(const ViiperFeedbackState& feedback);

private:
    VirtualControllerRuntimeSettings CurrentSettings() const;

    VirtualControllerMode m_mode = VirtualControllerMode::Xbox360;
    FeedbackFn m_feedbackFn;
    ViiperClient m_viiper;
    std::atomic<bool> m_valid{false};
    VirtualControllerError m_error = VirtualControllerError::None;
    std::atomic<bool> m_trackpadMouseEnabled{false};
    std::atomic<bool> m_useLeftTrackpadForMouse{false};
    std::atomic<bool> m_trackpadDpadEnabled{false};
    std::atomic<bool> m_useRightTrackpadForDpad{false};
    std::array<std::atomic<uint8_t>, static_cast<size_t>(BackButtonId::Count)> m_backButtonMappings{};
    std::atomic<uint8_t> m_batteryLevelPercent{100};
    std::atomic<uint8_t> m_chargeState{0};
};
