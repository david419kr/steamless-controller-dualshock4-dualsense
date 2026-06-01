#include "ControllerManager.h"
#include "VirtualController.h"
#include "steam/SteamController.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

static std::unique_ptr<SteamController> g_ctrl;
static constexpr uint32_t POLL_OPEN_REPORT_TIMEOUT_MS = 75;

static uint8_t ButtonByte(const SteamControllerState& state, int index) {
    return static_cast<uint8_t>((state.buttons >> (index * 8)) & 0xFF);
}

static uint8_t TriggerToByte(int16_t raw) {
    return static_cast<uint8_t>(std::clamp<int>(raw >> 7, 0, 255));
}

static bool IsMotionOutputMode(VirtualControllerMode mode) {
    return mode == VirtualControllerMode::DualShock4 ||
           mode == VirtualControllerMode::DualSense;
}

static SteamControllerDualSenseHaptics ToSteamHaptics(const ViiperDualSenseFeedbackState& feedback) {
    SteamControllerDualSenseHaptics haptics{};
    haptics.enableBits1 = feedback.enableBits1;
    haptics.enableBits2 = feedback.enableBits2;
    haptics.rumbleRight = feedback.rumbleRight;
    haptics.rumbleLeft = feedback.rumbleLeft;
    haptics.enableBits3 = feedback.enableBits3;
    haptics.rightTriggerEffect = feedback.rightTriggerEffect;
    haptics.leftTriggerEffect = feedback.leftTriggerEffect;
    return haptics;
}

static SteamControllerDualSenseAudioHaptics ToSteamAudioHaptics(
    const ViiperDualSenseAudioHapticsState& feedback) {
    SteamControllerDualSenseAudioHaptics haptics{};
    haptics.sequence = feedback.sequence;
    haptics.leftEnergy = feedback.leftEnergy;
    haptics.rightEnergy = feedback.rightEnergy;
    haptics.leftPeak = feedback.leftPeak;
    haptics.rightPeak = feedback.rightPeak;
    haptics.leftTransient = feedback.leftTransient;
    haptics.rightTransient = feedback.rightTransient;
    return haptics;
}

static uint8_t TrackpadDpadMask(int16_t x, int16_t y, bool active) {
    if (!active)
        return 0;

    const int ix = static_cast<int>(x);
    const int iy = static_cast<int>(y);
    const int ax = std::abs(ix);
    const int ay = std::abs(iy);
    constexpr int DEADZONE = 9000;

    if (ax < DEADZONE && ay < DEADZONE)
        return 0;

    constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;
    double degrees = std::atan2(static_cast<double>(iy), static_cast<double>(ix)) * RAD_TO_DEG;
    if (degrees < 0.0)
        degrees += 360.0;

    if (degrees < 40.0 || degrees >= 320.0) return 0x02;        // right
    if (degrees < 50.0) return 0x01 | 0x02;                     // up-right
    if (degrees < 130.0) return 0x01;                           // up
    if (degrees < 140.0) return 0x01 | 0x08;                    // up-left
    if (degrees < 220.0) return 0x08;                           // left
    if (degrees < 230.0) return 0x04 | 0x08;                    // down-left
    if (degrees < 310.0) return 0x04;                           // down
    return 0x04 | 0x02;                                         // down-right
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

void ControllerManager::PollForController() {
    if (!m_connected)
        TryOpen(POLL_OPEN_REPORT_TIMEOUT_MS);
    else if (g_ctrl && !g_ctrl->IsOpen())
        Close(/*restoreLizard=*/false);
}

void ControllerManager::EnableGameMode() {
    if (!m_connected || m_gameModeActive) return;
    if (!g_ctrl->DisableLizardMode()) return;
    if (!g_ctrl->SetImuEnabled(IsMotionOutputMode(m_outputMode))) {
        g_ctrl->EnableLizardMode();
        return;
    }
    if (m_hideOriginalController && m_hidHide.IsInstalled() && !m_originalHidden) {
        if (m_hidHide.HideSteamController())
            m_originalHidden = true;
    }

    m_virtual = std::make_unique<VirtualController>(
        m_outputMode,
        [this](const ViiperFeedbackState& feedback) {
            HandleVirtualFeedback(feedback);
        });
    if (!m_virtual->IsValid()) {
        const VirtualControllerError error = m_virtual->Error();
        m_virtual.reset();
        g_ctrl->SetImuEnabled(false);
        g_ctrl->EnableLizardMode();
        if (m_originalHidden) {
            m_hidHide.RevealSteamController();
            m_originalHidden = false;
        }
        if (error != VirtualControllerError::None)
            m_onStateChanged(m_connected, m_gameModeActive, error);
        return;
    }

    m_gameModeActive = true;
    m_trackpad.Reset();
    m_hasLastImuTimestamp = false;
    {
        std::lock_guard<std::mutex> lock(m_dualSenseFeedbackMutex);
        m_hasDualSenseFeedback = false;
        m_lastDualSenseFeedbackAt = {};
    }
    m_lastLeftTriggerPosition.store(0, std::memory_order_relaxed);
    m_lastRightTriggerPosition.store(0, std::memory_order_relaxed);
    m_lastImuProgress = std::chrono::steady_clock::now();
    m_lastImuReassert = m_lastImuProgress;
    ApplyTrackpadRuntimeSettings();
    StartReadLoop();
    m_onStateChanged(m_connected, m_gameModeActive, VirtualControllerError::None);
}

void ControllerManager::DisableGameMode() {
    if (!m_gameModeActive) return;
    StopReadLoop();
    m_trackpad.Reset();
    if (g_ctrl)
        g_ctrl->ClearTrackpadHaptics();
    if (g_ctrl)
        g_ctrl->ClearDualSenseHaptics();
    m_hasLastImuTimestamp = false;
    {
        std::lock_guard<std::mutex> lock(m_dualSenseFeedbackMutex);
        m_hasDualSenseFeedback = false;
        m_lastDualSenseFeedbackAt = {};
    }
    m_virtual.reset();
    g_ctrl->SetImuEnabled(false);
    g_ctrl->EnableLizardMode();
    if (m_originalHidden) {
        m_hidHide.RevealSteamController();
        m_originalHidden = false;
    }
    m_gameModeActive = false;
    m_onStateChanged(m_connected, m_gameModeActive, VirtualControllerError::None);
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

void ControllerManager::SetBackButtonMapping(BackButtonId id, BackButtonAction action) {
    if (m_backButtonMappings.Get(id) == action)
        return;

    m_backButtonMappings.Set(id, action);
    ApplyTrackpadRuntimeSettings();
}

void ControllerManager::SetDualSenseAudioRumbleThreshold(double threshold) {
    m_dualSenseAudioRumbleThreshold = std::clamp(
        threshold,
        SteamController::DUALSENSE_AUDIO_RUMBLE_THRESHOLD_MIN,
        SteamController::DUALSENSE_AUDIO_RUMBLE_THRESHOLD_MAX);
    if (g_ctrl)
        g_ctrl->SetDualSenseAudioRumbleThreshold(m_dualSenseAudioRumbleThreshold);
}

void ControllerManager::RevealOriginalControllerNow() {
    if (m_hidHide.RevealSteamControllerNow())
        m_originalHidden = false;
}

bool ControllerManager::IsTrackpadDpadActive() const {
    return m_trackpadDpadEnabled;
}

bool ControllerManager::ShouldTrackpadDpadLockMouse() const {
    return IsMotionOutputMode(m_outputMode) && IsTrackpadDpadActive();
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
    const bool backButtonMappingsActive = m_backButtonMappings.AnyAssigned();
    const bool effectiveTrackpadMouse = m_trackpadMouseEnabled && !dpadLocksMouse;
    const bool effectiveBackButtons = m_backButtonsEnabled &&
                                      !dpadLocksMouse &&
                                      !backButtonMappingsActive;

    if (!effectiveTrackpadMouse || !effectiveBackButtons)
        m_trackpad.Reset();

    m_prevHapticLeftClick = false;
    m_prevHapticRightClick = false;
    m_prevHapticLeftDpadMask = 0;
    m_prevHapticRightDpadMask = 0;
    m_prevHapticLeftTouch = false;
    m_prevHapticRightTouch = false;
    if (g_ctrl && m_gameModeActive)
        g_ctrl->ClearTrackpadHaptics();

    m_trackpad.SetTrackpadEnabled(effectiveTrackpadMouse);
    m_trackpad.SetBackButtonsEnabled(effectiveBackButtons);
    m_trackpad.SetUseLeftTrackpad(m_useLeftTrackpad);

    if (m_virtual) {
        m_virtual->SetTrackpadMouseClaim(effectiveTrackpadMouse, m_useLeftTrackpad);
        m_virtual->SetTrackpadDpadClaim(dpadActive, m_trackpadDpadUseRight);
        m_virtual->SetBackButtonMappings(m_backButtonMappings);
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

    if (IsMotionOutputMode(m_outputMode)) {
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
    const bool leftClickPulse = leftClickNow != m_prevHapticLeftClick;
    const bool rightClickPulse = rightClickNow != m_prevHapticRightClick;
    const uint8_t leftDpadMask = dpadLeft
        ? TrackpadDpadMask(state.leftPadX, state.leftPadY, leftClickNow)
        : 0;
    const uint8_t rightDpadMask = dpadRight
        ? TrackpadDpadMask(state.rightPadX, state.rightPadY, rightClickNow)
        : 0;
    const bool leftDpadDirectionPulse =
        dpadLeft && leftClickNow && !leftClickPulse &&
        leftDpadMask != m_prevHapticLeftDpadMask;
    const bool rightDpadDirectionPulse =
        dpadRight && rightClickNow && !rightClickPulse &&
        rightDpadMask != m_prevHapticRightDpadMask;

    m_prevHapticLeftClick = leftClickNow;
    m_prevHapticRightClick = rightClickNow;
    m_prevHapticLeftDpadMask = leftClickNow ? leftDpadMask : 0;
    m_prevHapticRightDpadMask = rightClickNow ? rightDpadMask : 0;

    constexpr int MOVE_PULSE_THRESHOLD = 1800;
    constexpr auto MOVE_PULSE_INTERVAL = std::chrono::milliseconds(22);
    const auto now = std::chrono::steady_clock::now();

    auto pulseTouchIfMoved = [&](bool left,
                                 bool active,
                                 int16_t x,
                                 int16_t y,
                                 bool& prevActive,
                                 int16_t& prevX,
                                 int16_t& prevY,
                                 std::chrono::steady_clock::time_point& lastPulse,
                                 bool skipForClick) {
        if (!active) {
            prevActive = false;
            return;
        }

        bool moved = false;
        if (prevActive) {
            const int dx = std::abs(static_cast<int>(x) - static_cast<int>(prevX));
            const int dy = std::abs(static_cast<int>(y) - static_cast<int>(prevY));
            moved = dx + dy >= MOVE_PULSE_THRESHOLD;
        }

        if (moved && !skipForClick && now - lastPulse >= MOVE_PULSE_INTERVAL) {
            g_ctrl->PulseTrackpadHaptic(left, false);
            lastPulse = now;
            prevX = x;
            prevY = y;
        } else if (!prevActive) {
            prevX = x;
            prevY = y;
        }
        prevActive = true;
    };

    const bool leftStrongPulse = leftClickPulse || leftDpadDirectionPulse;
    const bool rightStrongPulse = rightClickPulse || rightDpadDirectionPulse;

    if (leftStrongPulse) {
        g_ctrl->PulseTrackpadHaptic(true, true);
        m_lastHapticLeftPulse = now;
    }
    if (rightStrongPulse) {
        g_ctrl->PulseTrackpadHaptic(false, true);
        m_lastHapticRightPulse = now;
    }

    pulseTouchIfMoved(true, leftTouchHaptic, state.leftPadX, state.leftPadY,
                      m_prevHapticLeftTouch, m_prevHapticLeftX, m_prevHapticLeftY,
                      m_lastHapticLeftPulse, leftStrongPulse);
    pulseTouchIfMoved(false, rightTouchHaptic, state.rightPadX, state.rightPadY,
                      m_prevHapticRightTouch, m_prevHapticRightX, m_prevHapticRightY,
                      m_lastHapticRightPulse, rightStrongPulse);
}

void ControllerManager::MaintainMotionImu(const SteamControllerState& state) {
    if (!g_ctrl || !m_gameModeActive || !IsMotionOutputMode(m_outputMode))
        return;

    const auto now = std::chrono::steady_clock::now();
    bool shouldReassert = false;

    if (!state.hasImu) {
        shouldReassert = true;
    } else if (!m_hasLastImuTimestamp || state.imuTimestamp != m_lastImuTimestamp) {
        m_hasLastImuTimestamp = true;
        m_lastImuTimestamp = state.imuTimestamp;
        m_lastImuProgress = now;
        return;
    } else if (now - m_lastImuProgress >= std::chrono::milliseconds(900)) {
        shouldReassert = true;
    }

    if (!shouldReassert || now - m_lastImuReassert < std::chrono::milliseconds(1000))
        return;

    if (g_ctrl->SetImuEnabled(true))
        m_lastImuReassert = now;
}

void ControllerManager::HandleVirtualFeedback(const ViiperFeedbackState& feedback) {
    if (!g_ctrl)
        return;

    if (feedback.mode == VirtualControllerMode::DualShock4) {
        g_ctrl->SetDs4EnhancedRumble(feedback.largeMotor, feedback.smallMotor);
    } else if (feedback.mode == VirtualControllerMode::DualSense) {
        if (feedback.isDualSenseAudio) {
            {
                std::lock_guard<std::mutex> lock(m_dualSenseFeedbackMutex);
                if (m_hasDualSenseFeedback)
                    m_lastDualSenseFeedbackAt = std::chrono::steady_clock::now();
            }
            g_ctrl->SetDualSenseAudioHaptics(ToSteamAudioHaptics(feedback.dualSenseAudio));
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_dualSenseFeedbackMutex);
            m_lastDualSenseFeedback = feedback.dualSense;
            m_lastDualSenseFeedbackAt = std::chrono::steady_clock::now();
            m_hasDualSenseFeedback = true;
        }
        g_ctrl->SetDualSenseHaptics(ToSteamHaptics(feedback.dualSense),
                                    m_lastLeftTriggerPosition.load(std::memory_order_relaxed),
                                    m_lastRightTriggerPosition.load(std::memory_order_relaxed));
    } else {
        g_ctrl->SetRumble(feedback.largeMotor, feedback.smallMotor);
    }
}

void ControllerManager::ApplyDualSenseHaptics(const SteamControllerState& state) {
    if (!g_ctrl || !m_gameModeActive || m_outputMode != VirtualControllerMode::DualSense)
        return;

    const uint8_t leftTriggerPosition = TriggerToByte(state.leftTrigger);
    const uint8_t rightTriggerPosition = TriggerToByte(state.rightTrigger);
    m_lastLeftTriggerPosition.store(leftTriggerPosition, std::memory_order_relaxed);
    m_lastRightTriggerPosition.store(rightTriggerPosition, std::memory_order_relaxed);

    ViiperDualSenseFeedbackState feedback{};
    bool feedbackStale = false;
    {
        std::lock_guard<std::mutex> lock(m_dualSenseFeedbackMutex);
        if (!m_hasDualSenseFeedback)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastDualSenseFeedbackAt > std::chrono::milliseconds(1500)) {
            m_hasDualSenseFeedback = false;
            m_lastDualSenseFeedbackAt = {};
            feedbackStale = true;
        } else {
            feedback = m_lastDualSenseFeedback;
        }
    }

    if (feedbackStale) {
        g_ctrl->ClearDualSenseHaptics();
        return;
    }

    g_ctrl->SetDualSenseHaptics(ToSteamHaptics(feedback),
                                leftTriggerPosition,
                                rightTriggerPosition);
}

void ControllerManager::TryOpen(uint32_t activeReportTimeoutMs) {
    if (!g_ctrl) g_ctrl = std::make_unique<SteamController>();
    if (g_ctrl->Open(activeReportTimeoutMs)) {
        g_ctrl->SetDualSenseAudioRumbleThreshold(m_dualSenseAudioRumbleThreshold);
        m_connected = true;
        m_onStateChanged(m_connected, m_gameModeActive, VirtualControllerError::None);
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
    m_onStateChanged(m_connected, m_gameModeActive, VirtualControllerError::None);
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
        MaintainMotionImu(state);
        ApplyDualSenseHaptics(state);
        if (m_virtual) m_virtual->Update(state);
        UpdateTrackpadHaptics(state);
        m_trackpad.Update(buf, n);
    }
}
