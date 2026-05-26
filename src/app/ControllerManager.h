#pragma once
#include "TrackpadMouse.h"
#include "VirtualController.h"
#include <functional>
#include <thread>
#include <atomic>
#include <memory>

// Manages the Steam Controller lifecycle: device discovery, lizard mode
// disable/enable, and the heartbeat that keeps lizard mode off.
// All public methods are safe to call from the UI thread.
class ControllerManager {
public:
    using StateChangedFn = std::function<void(bool connected, bool gameModeActive, bool vigemMissing)>;

    explicit ControllerManager(StateChangedFn onStateChanged);
    ~ControllerManager();
    ControllerManager(const ControllerManager&) = delete;
    ControllerManager& operator=(const ControllerManager&) = delete;

    // Called when Windows reports a device arrival or removal (WM_DEVICECHANGE).
    void OnDeviceChange();

    // Toggle game mode on/off. No-op if controller is not connected.
    void EnableGameMode();
    void DisableGameMode();

    void SetTrackpadMouseEnabled(bool enabled);
    void SetBackButtonsEnabled(bool enabled);
    void SetUseLeftTrackpad(bool enabled);
    void SetTrackpadDpadEnabled(bool enabled);
    void SetTrackpadDpadUseRight(bool enabled);
    void SetOutputMode(VirtualControllerMode mode);

    bool IsConnected()             const { return m_connected; }
    bool IsGameModeActive()        const { return m_gameModeActive; }
    bool IsTrackpadMouseEnabled()  const { return m_trackpadMouseEnabled; }
    bool IsBackButtonsEnabled()    const { return m_backButtonsEnabled; }
    bool IsUseLeftTrackpad()       const { return m_useLeftTrackpad; }
    bool IsTrackpadDpadEnabled()   const { return m_trackpadDpadEnabled; }
    bool IsTrackpadDpadUseRight()  const { return m_trackpadDpadUseRight; }
    VirtualControllerMode GetOutputMode() const { return m_outputMode; }

private:
    void TryOpen();
    void Close(bool restoreLizard);
    void StartReadLoop();
    void StopReadLoop();
    void ReadLoop();
    void ApplyTrackpadRuntimeSettings();
    bool IsTrackpadDpadActive() const;
    bool ShouldTrackpadDpadLockMouse() const;
    bool ShouldLinkTrackpadSidesForXbox() const;
    void LinkDpadToMouseSideForXbox();
    void LinkMouseToDpadSideForXbox();

    StateChangedFn                     m_onStateChanged;
    bool                               m_connected            = false;
    bool                               m_gameModeActive       = false;
    bool                               m_trackpadMouseEnabled = false;
    bool                               m_backButtonsEnabled   = false;
    bool                               m_useLeftTrackpad      = false;
    bool                               m_trackpadDpadEnabled  = false;
    bool                               m_trackpadDpadUseRight = false;
    VirtualControllerMode              m_outputMode           = VirtualControllerMode::Xbox360;
    std::unique_ptr<VirtualController> m_virtual;
    TrackpadMouse                      m_trackpad;
    std::thread                        m_readThread;
    std::atomic<bool>                  m_readRunning{false};
};
