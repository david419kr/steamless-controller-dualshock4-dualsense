#include "VirtualController.h"

#include <cstdio>
#include <utility>

namespace {

const char* ModeName(VirtualControllerMode mode) {
    switch (mode) {
    case VirtualControllerMode::DualShock4:
        return "DualShock 4";
    case VirtualControllerMode::DualSense:
        return "DualSense";
    default:
        return "Xbox 360";
    }
}

} // namespace

VirtualController::VirtualController(VirtualControllerMode mode, FeedbackFn feedbackFn)
    : m_mode(mode), m_feedbackFn(std::move(feedbackFn)) {
    const bool opened = m_viiper.Open(
        m_mode,
        [this](const ViiperFeedbackState& feedback) {
            OnFeedback(feedback);
        });
    m_valid.store(opened, std::memory_order_relaxed);
    if (!opened) {
        m_error = m_viiper.Error();
        std::printf("[VIIPER] Virtual controller failed: %d\n",
                    static_cast<int>(m_error));
        return;
    }

    std::printf("[VIIPER] Virtual %s controller connected\n", ModeName(m_mode));
}

VirtualController::~VirtualController() {
    m_valid.store(false, std::memory_order_relaxed);
    m_viiper.Close();
}

void VirtualController::OnFeedback(const ViiperFeedbackState& feedback) {
    if (m_feedbackFn)
        m_feedbackFn(feedback);
}

void VirtualController::SetBatteryState(uint8_t levelPercent, uint8_t chargeState) {
    m_batteryLevelPercent.store(levelPercent, std::memory_order_relaxed);
    m_chargeState.store(chargeState, std::memory_order_relaxed);
}

void VirtualController::SetTrackpadMouseClaim(bool enabled, bool useLeftTrackpad) {
    m_trackpadMouseEnabled.store(enabled, std::memory_order_relaxed);
    m_useLeftTrackpadForMouse.store(useLeftTrackpad, std::memory_order_relaxed);
}

void VirtualController::SetTrackpadDpadClaim(bool enabled, bool useRightTrackpad) {
    m_trackpadDpadEnabled.store(enabled, std::memory_order_relaxed);
    m_useRightTrackpadForDpad.store(useRightTrackpad, std::memory_order_relaxed);
}

void VirtualController::SetBackButtonMappings(const BackButtonMappings& mappings) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(BackButtonId::Count); ++i) {
        const auto id = static_cast<BackButtonId>(i);
        m_backButtonMappings[i].store(static_cast<uint8_t>(mappings.Get(id)),
                                      std::memory_order_relaxed);
    }
}

VirtualControllerRuntimeSettings VirtualController::CurrentSettings() const {
    VirtualControllerRuntimeSettings settings{};
    settings.trackpadMouseEnabled =
        m_trackpadMouseEnabled.load(std::memory_order_relaxed);
    settings.useLeftTrackpadForMouse =
        m_useLeftTrackpadForMouse.load(std::memory_order_relaxed);
    settings.trackpadDpadEnabled =
        m_trackpadDpadEnabled.load(std::memory_order_relaxed);
    settings.useRightTrackpadForDpad =
        m_useRightTrackpadForDpad.load(std::memory_order_relaxed);

    for (uint8_t i = 0; i < static_cast<uint8_t>(BackButtonId::Count); ++i) {
        const auto id = static_cast<BackButtonId>(i);
        const auto action = static_cast<BackButtonAction>(
            m_backButtonMappings[i].load(std::memory_order_relaxed));
        settings.backButtonMappings.Set(id, action);
    }

    return settings;
}

void VirtualController::Update(const SteamControllerState& state) {
    if (!m_valid.load(std::memory_order_relaxed))
        return;

    const VirtualControllerRuntimeSettings settings = CurrentSettings();

    bool ok = false;
    if (m_mode == VirtualControllerMode::DualShock4) {
        const ViiperDualShock4InputState input = BuildViiperDualShock4Input(state, settings);
        const auto data = input.Serialize();
        ok = m_viiper.SendInput(data.data(), data.size());
    } else if (m_mode == VirtualControllerMode::DualSense) {
        ViiperDualSenseInputState input = BuildViiperDualSenseInput(state, settings);
        input.batteryLevelPercent = m_batteryLevelPercent.load(std::memory_order_relaxed);
        input.chargeState = m_chargeState.load(std::memory_order_relaxed);
        const auto data = input.Serialize();
        ok = m_viiper.SendInput(data.data(), data.size());
    } else {
        const ViiperXbox360InputState input = BuildViiperXbox360Input(state, settings);
        const auto data = input.Serialize();
        ok = m_viiper.SendInput(data.data(), data.size());
    }

    if (!ok) {
        m_error = m_viiper.Error();
        m_valid.store(false, std::memory_order_relaxed);
    }
}
