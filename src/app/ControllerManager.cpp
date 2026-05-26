#include "ControllerManager.h"
#include "VirtualController.h"
#include "steam/SteamController.h"
#include <memory>

static std::unique_ptr<SteamController> g_ctrl;

static uint8_t ButtonByte(const SteamControllerState& state, int index) {
    return static_cast<uint8_t>((state.buttons >> (index * 8)) & 0xFF);
}

ControllerManager::ControllerManager(StateChangedFn onStateChanged)
    : m_onStateChanged(std::move(onStateChanged))
{
    m_hidHide.EnsureAppRegistered();
    TryOpen();
}

ControllerManager::~ControllerManager() {
    Close(/*restoreLizard=*/true);
}

void ControllerManager::OnDeviceChange() {
    if (!m_connected)
        TryOpen();
    else if (g_ctrl && !g_ctrl->IsOpen())
        Close(/*restoreLizard=*/false);
}

void ControllerManager::EnableGameMode() {
    if (!m_connected || m_gameModeActive) return;
    if (!g_ctrl->DisableLizardMode()) return;
    if (!g_ctrl->SetImuEnabled(m_outputMode == VirtualControllerMode::DualShock4)) {
        g_ctrl->EnableLizardMode();
        return;
    }
    if (m_hideOriginalController && m_hidHide.IsInstalled() && !m_originalHidden) {
        if (m_hidHide.HideSteamController())
            m_originalHidden = true;
    }

    m_virtual = std::make_unique<VirtualController>(
        m_outputMode,
        [mode = m_outputMode](uint8_t largeMotor, uint8_t smallMotor) {
            if (!g_ctrl)
                return;
            if (mode == VirtualControllerMode::DualShock4)
                g_ctrl->SetDs4EnhancedRumble(largeMotor, smallMotor);
            else
                g_ctrl->SetRumble(largeMotor, smallMotor);
        });
    if (!m_virtual->IsValid()) {
        bool missing = m_virtual->IsDriverMissing();
        m_virtual.reset();
        g_ctrl->SetImuEnabled(false);
        g_ctrl->EnableLizardMode();
        if (m_originalHidden) {
            m_hidHide.RevealSteamController();
            m_originalHidden = false;
        }
        if (missing) m_onStateChanged(m_connected, m_gameModeActive, /*vigemMissing=*/true);
        return;
    }

    m_gameModeActive = true;
    m_trackpad.Reset();
    ApplyTrackpadRuntimeSettings();
    StartReadLoop();
    m_onStateChanged(m_connected, m_gameModeActive, false);
}

void ControllerManager::DisableGameMode() {
    if (!m_gameModeActive) return;
    StopReadLoop();
    m_trackpad.Reset();
    if (g_ctrl)
        g_ctrl->ClearTrackpadHaptics();
    m_virtual.reset();
    g_ctrl->SetImuEnabled(false);
    g_ctrl->EnableLizardMode();
    if (m_originalHidden) {
        m_hidHide.RevealSteamController();
        m_originalHidden = false;
    }
    m_gameModeActive = false;
    m_onStateChanged(m_connected, m_gameModeActive, false);
}

void ControllerManager::SetTrackpadMouseEnabled(bool enabled) {
    m_trackpadMouseEnabled = enabled;
    if (enabled)
        LinkMouseToDpadSideForXbox();
    ApplyTrackpadRuntimeSettings();
}

void ControllerManager::SetBackButtonsEnabled(bool enabled) {
    m_backButtonsEnabled = enabled;
    ApplyTrackpadRuntimeSettings();
}

void ControllerManager::SetUseLeftTrackpad(bool enabled) {
    m_useLeftTrackpad = enabled;
    LinkDpadToMouseSideForXbox();
    ApplyTrackpadRuntimeSettings();
}

void ControllerManager::SetTrackpadDpadEnabled(bool enabled) {
    m_trackpadDpadEnabled = enabled;
    if (enabled)
        LinkDpadToMouseSideForXbox();
    ApplyTrackpadRuntimeSettings();
}

void ControllerManager::SetTrackpadDpadUseRight(bool enabled) {
    m_trackpadDpadUseRight = enabled;
    LinkMouseToDpadSideForXbox();
    ApplyTrackpadRuntimeSettings();
}

void ControllerManager::SetOutputMode(VirtualControllerMode mode) {
    if (m_outputMode == mode) return;

    const bool restart = m_gameModeActive;
    if (restart)
        DisableGameMode();

    m_outputMode = mode;
    LinkMouseToDpadSideForXbox();

    if (restart)
        EnableGameMode();
    else
        ApplyTrackpadRuntimeSettings();
}

void ControllerManager::SetHideOriginalControllerEnabled(bool enabled) {
    if (m_hideOriginalController == enabled) return;
    m_hideOriginalController = enabled;

    if (!m_gameModeActive || !m_hidHide.IsInstalled())
        return;

    if (enabled) {
        if (m_hidHide.HideSteamController())
            m_originalHidden = true;
    } else if (m_originalHidden) {
        m_hidHide.RevealSteamController();
        m_originalHidden = false;
    }
}

void ControllerManager::RevealOriginalControllerNow() {
    if (m_hidHide.RevealSteamControllerNow())
        m_originalHidden = false;
}

bool ControllerManager::IsTrackpadDpadActive() const {
    return m_trackpadDpadEnabled;
}

bool ControllerManager::ShouldTrackpadDpadLockMouse() const {
    return m_outputMode == VirtualControllerMode::DualShock4 && IsTrackpadDpadActive();
}

bool ControllerManager::ShouldLinkTrackpadSidesForXbox() const {
    return m_outputMode == VirtualControllerMode::Xbox360 &&
           m_trackpadMouseEnabled &&
           m_trackpadDpadEnabled;
}

void ControllerManager::LinkDpadToMouseSideForXbox() {
    if (ShouldLinkTrackpadSidesForXbox())
        m_trackpadDpadUseRight = m_useLeftTrackpad;
}

void ControllerManager::LinkMouseToDpadSideForXbox() {
    if (ShouldLinkTrackpadSidesForXbox())
        m_useLeftTrackpad = m_trackpadDpadUseRight;
}

void ControllerManager::ApplyTrackpadRuntimeSettings() {
    const bool dpadActive = IsTrackpadDpadActive();
    const bool dpadLocksMouse = ShouldTrackpadDpadLockMouse();
    const bool effectiveTrackpadMouse = m_trackpadMouseEnabled && !dpadLocksMouse;
    const bool effectiveBackButtons = m_backButtonsEnabled && !dpadLocksMouse;

    if (!effectiveTrackpadMouse || !effectiveBackButtons)
        m_trackpad.Reset();

    m_prevHapticLeftClick = false;
    m_prevHapticRightClick = false;
    if (g_ctrl && m_gameModeActive)
        g_ctrl->ClearTrackpadHaptics();

    m_trackpad.SetTrackpadEnabled(effectiveTrackpadMouse);
    m_trackpad.SetBackButtonsEnabled(effectiveBackButtons);
    m_trackpad.SetUseLeftTrackpad(m_useLeftTrackpad);

    if (m_virtual) {
        m_virtual->SetTrackpadMouseClaim(effectiveTrackpadMouse, m_useLeftTrackpad);
        m_virtual->SetTrackpadDpadClaim(dpadActive, m_trackpadDpadUseRight);
    }
}

void ControllerManager::UpdateTrackpadHaptics(const SteamControllerState& state) {
    if (!g_ctrl)
        return;

    const uint8_t b2 = ButtonByte(state, 2);
    const uint8_t b3 = ButtonByte(state, 3);

    const bool rawRightTouching = (b2 & SteamController::BTN_TP_RT) != 0;
    const bool rawLeftTouching = (b3 & SteamController::BTN_TP_LT) != 0;
    const bool rawRightClick = (b2 & SteamController::BTN_TP_RT_CLICK) != 0;
    const bool rawLeftClick = (b3 & SteamController::BTN_TP_LT_CLICK) != 0;

    const bool dpadActive = IsTrackpadDpadActive();
    const bool dpadLeft = dpadActive && !m_trackpadDpadUseRight;
    const bool dpadRight = dpadActive && m_trackpadDpadUseRight;
    const bool dpadLocksMouse = ShouldTrackpadDpadLockMouse();
    const bool mouseActive = m_trackpadMouseEnabled && !dpadLocksMouse;
    const bool mouseLeft = mouseActive && m_useLeftTrackpad;
    const bool mouseRight = mouseActive && !m_useLeftTrackpad;

    bool leftTouchHaptic = false;
    bool rightTouchHaptic = false;
    bool leftClickEligible = false;
    bool rightClickEligible = false;

    if (mouseLeft) {
        leftTouchHaptic = rawLeftTouching;
        leftClickEligible = true;
    }
    if (mouseRight) {
        rightTouchHaptic = rawRightTouching;
        rightClickEligible = true;
    }

    if (m_outputMode == VirtualControllerMode::DualShock4) {
        const bool nativeLeftTouchpad = !mouseLeft && !dpadLeft;
        const bool nativeRightTouchpad = !mouseRight && !dpadRight;

        if (nativeLeftTouchpad) {
            leftTouchHaptic = leftTouchHaptic || rawLeftTouching;
            leftClickEligible = true;
        }
        if (nativeRightTouchpad) {
            rightTouchHaptic = rightTouchHaptic || rawRightTouching;
            rightClickEligible = true;
        }
    }

    if (dpadLeft)
        leftClickEligible = true;
    if (dpadRight)
        rightClickEligible = true;

    const bool leftClickNow = leftClickEligible && rawLeftClick;
    const bool rightClickNow = rightClickEligible && rawRightClick;
    const bool leftClickPulse = leftClickNow && !m_prevHapticLeftClick;
    const bool rightClickPulse = rightClickNow && !m_prevHapticRightClick;

    m_prevHapticLeftClick = leftClickNow;
    m_prevHapticRightClick = rightClickNow;

    g_ctrl->SetTrackpadHaptics(leftTouchHaptic, rightTouchHaptic,
                               leftClickPulse, rightClickPulse);
}

void ControllerManager::TryOpen() {
    if (!g_ctrl) g_ctrl = std::make_unique<SteamController>();
    if (g_ctrl->Open()) {
        m_connected = true;
        m_onStateChanged(m_connected, m_gameModeActive, false);
    }
}

void ControllerManager::Close(bool restoreLizard) {
    StopReadLoop();
    m_virtual.reset();
    if (g_ctrl) {
        if (restoreLizard && m_gameModeActive)
            g_ctrl->EnableLizardMode();
        g_ctrl->Close();
    }
    if (m_originalHidden) {
        m_hidHide.RevealSteamController();
        m_originalHidden = false;
    }
    m_connected      = false;
    m_gameModeActive = false;
    m_onStateChanged(m_connected, m_gameModeActive, false);
}

void ControllerManager::StartReadLoop() {
    m_readRunning = true;
    m_readThread  = std::thread(&ControllerManager::ReadLoop, this);
}

void ControllerManager::StopReadLoop() {
    m_readRunning = false;
    if (m_readThread.joinable())
        m_readThread.join();
}

void ControllerManager::ReadLoop() {
    uint8_t buf[64];
    while (m_readRunning) {
        if (g_ctrl) g_ctrl->MaintainRumble();
        size_t n = g_ctrl->ReadReport(buf, sizeof(buf), /*timeoutMs=*/32);
        if (n == 0) continue;
        if (SteamController::IsBatteryReportId(buf[0])) {
            SteamControllerBatteryState battery;
            if (SteamController::ParseBatteryReport(buf, n, battery) && m_virtual)
                m_virtual->SetBatteryState(battery.levelPercent, battery.chargeState);
            continue;
        }
        if (!SteamController::IsStateReportId(buf[0])) continue;
        SteamControllerState state;
        if (!SteamController::ParseStateReport(buf, n, state)) continue;
        if (m_virtual) m_virtual->Update(state);
        UpdateTrackpadHaptics(state);
        m_trackpad.Update(buf, n);
    }
}
