#pragma once
#include "BackButtonMapping.h"
#include "VirtualControllerTypes.h"
#include <Windows.h>
#include <dbt.h>
#include <cstddef>
#include <memory>

class ControllerManager;

class TrayApp {
public:
    TrayApp();
    ~TrayApp();

    bool Init(HINSTANCE hInstance);
    int  Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayIcon(bool connected,
                        bool gameModeActive,
                        VirtualControllerError virtualControllerError = VirtualControllerError::None);
    void ShowVirtualControllerBalloon(VirtualControllerError error);
    void ShowTrayBalloon(const wchar_t* title, const wchar_t* info, DWORD infoFlags);
    void ShowContextMenu();
    void RestartSteam(bool launchIfNotRunning);
    void MaybeAutoEnableSteamlessMode();
    void LoadSettings();
    void SaveSettings();
    void LoadBackButtonMappingsForCurrentMode();
    void LoadBackButtonMappingsForCurrentMode(HKEY key);
    void SaveBackButtonMappingsForCurrentMode(HKEY key);
    bool IsStartupEnabled() const;
    void SetStartupEnabled(bool enabled);
    void ShowBackButtonMappingWindow();
    void CreateBackButtonMappingControls();
    void RefreshBackButtonMappingWindow();
    void PopulateBackButtonCombo(HWND combo, BackButtonAction selected);
    void OnBackButtonMappingChanged(UINT controlId);
    void ShowDualSenseSettingsWindow();
    void CreateDualSenseSettingsControls();
    void RefreshDualSenseSettingsWindow();
    void OnDualSenseRumbleThresholdChanged();
    void ResetDualSenseRumbleThreshold();
    void ShowProCon2SettingsWindow();
    void CreateProCon2SettingsControls();
    void RefreshProCon2SettingsWindow();
    void OnProCon2RumbleThresholdChanged();
    void ResetProCon2RumbleThreshold();

    HWND                               m_hwnd      = nullptr;
    HWND                               m_backButtonHwnd = nullptr;
    HWND                               m_backButtonCombos[static_cast<size_t>(BackButtonId::Count)]{};
    HWND                               m_dualSenseSettingsHwnd = nullptr;
    HWND                               m_dualSenseThresholdSlider = nullptr;
    HWND                               m_dualSenseThresholdValue = nullptr;
    HWND                               m_dualSenseThresholdReset = nullptr;
    HWND                               m_proCon2SettingsHwnd = nullptr;
    HWND                               m_proCon2ThresholdSlider = nullptr;
    HWND                               m_proCon2ThresholdValue = nullptr;
    HWND                               m_proCon2ThresholdReset = nullptr;
    HDEVNOTIFY                         m_deviceNotify = nullptr;
    HINSTANCE                          m_hInstance = nullptr;
    UINT                               m_wmTaskbar = 0;
    HICON                              m_iconOff   = nullptr;
    HICON                              m_iconOn    = nullptr;
    std::unique_ptr<ControllerManager> m_controller;
    VirtualControllerError             m_lastVirtualControllerError = VirtualControllerError::None;
    bool                               m_autoEnable = false;
    bool                               m_autoRestartSteam = false;
    bool                               m_autoEnableSuppressedUntilReconnect = false;
    bool                               m_autoEnableAttemptedForConnection = false;

    static constexpr UINT IDM_TOGGLE        = 1001;
    static constexpr UINT IDM_EXIT          = 1002;
    static constexpr UINT IDM_TRACKPAD      = 1003;
    static constexpr UINT IDM_BACKBUTTONS   = 1004;
    static constexpr UINT IDM_LEFT_TRACKPAD = 1005;
    static constexpr UINT IDM_STARTUP       = 1006;
    static constexpr UINT IDM_OUTPUT_X360   = 1007;
    static constexpr UINT IDM_OUTPUT_DS4    = 1008;
    static constexpr UINT IDM_OUTPUT_DSENSE = 1015;
    static constexpr UINT IDM_OUTPUT_SWITCH2PRO = 1017;
    static constexpr UINT IDM_OUTPUT_SWITCHPRO = 1018;
    static constexpr UINT IDM_PROCON2_SETTINGS = 1019;
    static constexpr UINT IDM_HIDE_ORIGINAL = 1009;
    static constexpr UINT IDM_REVEAL_ORIGINAL = 1010;
    static constexpr UINT IDM_TRACKPAD_DPAD = 1011;
    static constexpr UINT IDM_TRACKPAD_DPAD_RIGHT = 1012;
    static constexpr UINT IDM_BACKBUTTON_MAPPINGS = 1013;
    static constexpr UINT IDM_RESTART_STEAM = 1014;
    static constexpr UINT IDM_DUALSENSE_SETTINGS = 1016;
    static constexpr UINT IDM_AUTO_ENABLE = 1020;
    static constexpr UINT IDM_AUTO_RESTART_STEAM = 1021;
    static constexpr UINT IDC_BACKMAP_L4 = 2001;
    static constexpr UINT IDC_BACKMAP_L5 = 2002;
    static constexpr UINT IDC_BACKMAP_R4 = 2003;
    static constexpr UINT IDC_BACKMAP_R5 = 2004;
    static constexpr UINT IDC_DS_RUMBLE_THRESHOLD = 2101;
    static constexpr UINT IDC_DS_RUMBLE_THRESHOLD_VALUE = 2102;
    static constexpr UINT IDC_DS_RUMBLE_THRESHOLD_RESET = 2103;
    static constexpr UINT IDC_PROCON2_RUMBLE_THRESHOLD = 2111;
    static constexpr UINT IDC_PROCON2_RUMBLE_THRESHOLD_VALUE = 2112;
    static constexpr UINT IDC_PROCON2_RUMBLE_THRESHOLD_RESET = 2113;
    static constexpr UINT WM_TRAY          = WM_APP + 1;
    static constexpr UINT TRAY_UID         = 1;
    static constexpr UINT_PTR DEVICE_POLL_TIMER_ID = 3001;
    static constexpr UINT DEVICE_POLL_INTERVAL_MS = 1000;
};
