#include "VirtualController.h"

#include <cstdio>
#include <utility>

VirtualController::VirtualController(VirtualControllerMode mode, RumbleFn rumbleFn)
    : m_mode(mode), m_rumbleFn(std::move(rumbleFn)) {
    const bool opened = m_viiper.Open(
        m_mode,
        [this](uint8_t largeMotor, uint8_t smallMotor) {
            OnRumble(largeMotor, smallMotor);
        });
    m_valid.store(opened, std::memory_order_relaxed);
    if (!opened) {
        m_error = m_viiper.Error();
        std::printf("[VIIPER] Virtual controller failed: %d\n",
                    static_cast<int>(m_error));
        return;
    }

    std::printf("[VIIPER] Virtual %s controller connected\n",
                m_mode == VirtualControllerMode::DualShock4 ? "DualShock 4" : "Xbox 360");
}

VirtualController::~VirtualController() {
    m_valid.store(false, std::memory_order_relaxed);
    m_viiper.Close();
}

void VirtualController::OnRumble(uint8_t largeMotor, uint8_t smallMotor) {
    if (m_rumbleFn)
        m_rumbleFn(largeMotor, smallMotor);
}

void VirtualController::SetBatteryState(uint8_t levelPercent, uint8_t chargeState) {
    (void)levelPercent;
    (void)chargeState;
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
