#include "TrayApp.h"
#include "ControllerManager.h"
#include "resource.h"
#include <shellapi.h>
#include <dbt.h>
#include <winreg.h>

static TrayApp* g_app = nullptr;

static constexpr wchar_t WNDCLASS_NAME[] = L"SteamlessControllerTray";
static constexpr wchar_t BACKBUTTON_WNDCLASS_NAME[] = L"SteamlessControllerBackButtonMappings";
static constexpr UINT BACKMAP_L4_ID = 2001;
static constexpr UINT BACKMAP_L5_ID = 2002;
static constexpr UINT BACKMAP_R4_ID = 2003;
static constexpr UINT BACKMAP_R5_ID = 2004;

static bool IsBackButtonComboId(UINT id) {
    return id >= BACKMAP_L4_ID && id <= BACKMAP_R5_ID;
}

static BackButtonId BackButtonIdFromComboId(UINT id) {
    switch (id) {
    case BACKMAP_L4_ID: return BackButtonId::L4;
    case BACKMAP_L5_ID: return BackButtonId::L5;
    case BACKMAP_R4_ID: return BackButtonId::R4;
    case BACKMAP_R5_ID: return BackButtonId::R5;
    default: return BackButtonId::L4;
    }
}

static size_t BackButtonIndex(BackButtonId id) {
    return static_cast<size_t>(id);
}

static bool IsValidBackButtonAction(DWORD value) {
    return value <= static_cast<DWORD>(BackButtonAction::Guide);
}

TrayApp::TrayApp() {
    g_app = this;
}

TrayApp::~TrayApp() {
    RemoveTrayIcon();
    g_app = nullptr;
}

bool TrayApp::Init(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    m_iconOff   = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_OFF));
    m_iconOn    = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_ON));
    m_wmTaskbar = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = WNDCLASS_NAME;
    if (!RegisterClassExW(&wc)) return false;

    WNDCLASSEXW mappingWc{};
    mappingWc.cbSize        = sizeof(mappingWc);
    mappingWc.lpfnWndProc   = WndProc;
    mappingWc.hInstance     = hInstance;
    mappingWc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    mappingWc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    mappingWc.lpszClassName = BACKBUTTON_WNDCLASS_NAME;
    if (!RegisterClassExW(&mappingWc)) return false;

    // Message-only window — invisible, never shown.
    m_hwnd = CreateWindowExW(0, WNDCLASS_NAME, L"SteamlessController",
                             0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!m_hwnd) return false;

    // Register for HID device arrival/removal notifications.
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size       = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    // HID device interface GUID
    filter.dbcc_classguid  = {0x4D1E55B2, 0xF16F, 0x11CF,
                              {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    RegisterDeviceNotificationW(m_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    m_controller = std::make_unique<ControllerManager>(
        [this](bool connected, bool gameModeActive, bool vigemMissing) {
            UpdateTrayIcon(connected, gameModeActive, vigemMissing);
        });

    LoadSettings();
    AddTrayIcon();
    return true;
}

int TrayApp::Run() {
    MSG msg;
    BOOL ret;
    while ((ret = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (ret == -1) return -1;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK TrayApp::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_app) return g_app->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrayApp::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == m_wmTaskbar) {
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == NIN_BALLOONUSERCLICK)
            ShellExecuteW(nullptr, L"open", L"https://github.com/nefarius/ViGEmBus/releases/latest",
                          nullptr, nullptr, SW_SHOWNORMAL);
        else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP)
            ShowContextMenu();
        return 0;

    case WM_COMMAND:
        if (IsBackButtonComboId(LOWORD(wp)) && HIWORD(wp) == CBN_SELCHANGE) {
            OnBackButtonMappingChanged(LOWORD(wp));
            return 0;
        }

        switch (LOWORD(wp)) {
        case IDM_TOGGLE:
            if (m_controller->IsGameModeActive())
                m_controller->DisableGameMode();
            else
                m_controller->EnableGameMode();
            break;
        case IDM_TRACKPAD:
            m_controller->SetTrackpadMouseEnabled(!m_controller->IsTrackpadMouseEnabled());
            SaveSettings();
            break;
        case IDM_BACKBUTTONS:
            m_controller->SetBackButtonsEnabled(!m_controller->IsBackButtonsEnabled());
            SaveSettings();
            break;
        case IDM_BACKBUTTON_MAPPINGS:
            ShowBackButtonMappingWindow();
            break;
        case IDM_LEFT_TRACKPAD:
            m_controller->SetUseLeftTrackpad(!m_controller->IsUseLeftTrackpad());
            SaveSettings();
            break;
        case IDM_TRACKPAD_DPAD:
            m_controller->SetTrackpadDpadEnabled(!m_controller->IsTrackpadDpadEnabled());
            SaveSettings();
            break;
        case IDM_TRACKPAD_DPAD_RIGHT:
            if (m_controller->IsTrackpadDpadEnabled()) {
                m_controller->SetTrackpadDpadUseRight(!m_controller->IsTrackpadDpadUseRight());
                SaveSettings();
            }
            break;
        case IDM_OUTPUT_X360:
            m_controller->SetOutputMode(VirtualControllerMode::Xbox360);
            RefreshBackButtonMappingWindow();
            SaveSettings();
            break;
        case IDM_OUTPUT_DS4:
            m_controller->SetOutputMode(VirtualControllerMode::DualShock4);
            RefreshBackButtonMappingWindow();
            SaveSettings();
            break;
        case IDM_HIDE_ORIGINAL:
            m_controller->SetHideOriginalControllerEnabled(!m_controller->IsHideOriginalControllerEnabled());
            SaveSettings();
            break;
        case IDM_REVEAL_ORIGINAL:
            m_controller->RevealOriginalControllerNow();
            break;
        case IDM_STARTUP:
            SetStartupEnabled(!IsStartupEnabled());
            break;
        case IDM_EXIT:
            m_controller->DisableGameMode();
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DEVICECHANGE:
        if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE)
            m_controller->OnDeviceChange();
        return TRUE;

    case WM_CLOSE:
        if (hwnd == m_backButtonHwnd) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (hwnd == m_backButtonHwnd) {
            m_backButtonHwnd = nullptr;
            for (HWND& combo : m_backButtonCombos)
                combo = nullptr;
            return 0;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void TrayApp::AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = m_iconOff;
    wcscpy_s(nid.szTip, L"Steamless Controller");
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void TrayApp::RemoveTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_UID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void TrayApp::UpdateTrayIcon(bool connected, bool gameModeActive, bool vigemMissing) {
    if (vigemMissing) { ShowViGEmBalloon(); return; }
    bool gameModeOn = gameModeActive;

    const wchar_t* tip = gameModeOn  ? L"Steamless Controller — Steamless Mode ON"
                       : connected   ? L"Steamless Controller — Connected (Steamless Mode OFF)"
                                     : L"Steamless Controller — No controller found";

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_UID;
    nid.uFlags = NIF_TIP | NIF_ICON;
    nid.hIcon  = gameModeOn ? m_iconOn : m_iconOff;
    wcscpy_s(nid.szTip, tip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayApp::ShowViGEmBalloon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_INFO;
    nid.dwInfoFlags      = NIIF_WARNING;
    wcscpy_s(nid.szInfoTitle, L"Driver required");
    wcscpy_s(nid.szInfo,      L"ViGEmBus is not installed. Click here to download it.");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static constexpr wchar_t REG_KEY[]     = L"Software\\SteamlessController";
static constexpr wchar_t REG_RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr wchar_t APP_NAME[]    = L"SteamlessController";

bool TrayApp::IsStartupEnabled() const {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    bool exists = RegQueryValueExW(key, APP_NAME, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return exists;
}

void TrayApp::SetStartupEnabled(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_WRITE, &key) != ERROR_SUCCESS)
        return;

    if (enabled) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        RegSetValueExW(key, APP_NAME, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(path),
                       static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, APP_NAME);
    }

    RegCloseKey(key);
}

void TrayApp::LoadSettings() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;

    auto readBool = [&](const wchar_t* name, bool def) -> bool {
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(key, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS)
            return val != 0;
        return def;
    };
    auto readAction = [&](const wchar_t* name) -> BackButtonAction {
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(key, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS &&
            IsValidBackButtonAction(val)) {
            return static_cast<BackButtonAction>(val);
        }
        return BackButtonAction::None;
    };

    DWORD outputMode = 0, outputModeSize = sizeof(outputMode);
    if (RegQueryValueExW(key, L"OutputMode", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&outputMode), &outputModeSize) == ERROR_SUCCESS) {
        m_controller->SetOutputMode(outputMode == 1
            ? VirtualControllerMode::DualShock4
            : VirtualControllerMode::Xbox360);
    }

    m_controller->SetTrackpadMouseEnabled(readBool(L"TrackpadMouse",   false));
    m_controller->SetBackButtonsEnabled  (readBool(L"BackButtons",     false));
    m_controller->SetUseLeftTrackpad     (readBool(L"UseLeftTrackpad", false));
    m_controller->SetTrackpadDpadEnabled (readBool(L"TrackpadDpad",    false));
    m_controller->SetTrackpadDpadUseRight(readBool(L"TrackpadDpadRight", false));
    m_controller->SetHideOriginalControllerEnabled(readBool(L"HideOriginalController", true));
    m_controller->SetBackButtonMapping(BackButtonId::L4, readAction(L"BackMapL4"));
    m_controller->SetBackButtonMapping(BackButtonId::L5, readAction(L"BackMapL5"));
    m_controller->SetBackButtonMapping(BackButtonId::R4, readAction(L"BackMapR4"));
    m_controller->SetBackButtonMapping(BackButtonId::R5, readAction(L"BackMapR5"));

    RegCloseKey(key);
}

void TrayApp::SaveSettings() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return;

    auto writeBool = [&](const wchar_t* name, bool val) {
        DWORD dw = val ? 1 : 0;
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
    };
    auto writeAction = [&](const wchar_t* name, BackButtonAction action) {
        DWORD dw = static_cast<DWORD>(action);
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
    };

    writeBool(L"TrackpadMouse",   m_controller->IsTrackpadMouseEnabled());
    writeBool(L"BackButtons",     m_controller->IsBackButtonsEnabled());
    writeBool(L"UseLeftTrackpad", m_controller->IsUseLeftTrackpad());
    writeBool(L"TrackpadDpad",    m_controller->IsTrackpadDpadEnabled());
    writeBool(L"TrackpadDpadRight", m_controller->IsTrackpadDpadUseRight());
    writeBool(L"HideOriginalController", m_controller->IsHideOriginalControllerEnabled());
    DWORD outputMode = m_controller->GetOutputMode() == VirtualControllerMode::DualShock4 ? 1u : 0u;
    RegSetValueExW(key, L"OutputMode", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&outputMode), sizeof(outputMode));
    writeAction(L"BackMapL4", m_controller->GetBackButtonMapping(BackButtonId::L4));
    writeAction(L"BackMapL5", m_controller->GetBackButtonMapping(BackButtonId::L5));
    writeAction(L"BackMapR4", m_controller->GetBackButtonMapping(BackButtonId::R4));
    writeAction(L"BackMapR5", m_controller->GetBackButtonMapping(BackButtonId::R5));

    RegCloseKey(key);
}

void TrayApp::ShowBackButtonMappingWindow() {
    if (m_backButtonHwnd) {
        RefreshBackButtonMappingWindow();
        ShowWindow(m_backButtonHwnd, SW_SHOWNORMAL);
        SetForegroundWindow(m_backButtonHwnd);
        return;
    }

    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    constexpr DWORD exStyle = WS_EX_TOOLWINDOW;
    RECT rect{0, 0, 340, 185};
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    HWND hwnd = CreateWindowExW(
        exStyle,
        BACKBUTTON_WNDCLASS_NAME,
        L"Back Button Mappings",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        m_hInstance,
        nullptr);
    if (!hwnd)
        return;

    m_backButtonHwnd = hwnd;
    CreateBackButtonMappingControls();
    RefreshBackButtonMappingWindow();
    ShowWindow(m_backButtonHwnd, SW_SHOWNORMAL);
    SetForegroundWindow(m_backButtonHwnd);
}

void TrayApp::CreateBackButtonMappingControls() {
    if (!m_backButtonHwnd)
        return;

    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const wchar_t* labels[] = {L"L4", L"L5", L"R4", L"R5"};
    const UINT ids[] = {IDC_BACKMAP_L4, IDC_BACKMAP_L5, IDC_BACKMAP_R4, IDC_BACKMAP_R5};

    for (size_t i = 0; i < static_cast<size_t>(BackButtonId::Count); ++i) {
        HWND label = CreateWindowExW(
            0,
            L"STATIC",
            labels[i],
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            18,
            22 + static_cast<int>(i) * 34,
            34,
            22,
            m_backButtonHwnd,
            nullptr,
            m_hInstance,
            nullptr);
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        HWND combo = CreateWindowExW(
            0,
            L"COMBOBOX",
            nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            68,
            18 + static_cast<int>(i) * 34,
            238,
            220,
            m_backButtonHwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ids[i])),
            m_hInstance,
            nullptr);
        SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        m_backButtonCombos[i] = combo;
    }
}

void TrayApp::RefreshBackButtonMappingWindow() {
    if (!m_backButtonHwnd)
        return;

    const bool ds4Mode = m_controller->GetOutputMode() == VirtualControllerMode::DualShock4;
    SetWindowTextW(m_backButtonHwnd,
                   ds4Mode ? L"Back Button Mappings - DualShock 4"
                           : L"Back Button Mappings - Xbox 360");

    for (uint8_t i = 0; i < static_cast<uint8_t>(BackButtonId::Count); ++i) {
        const auto id = static_cast<BackButtonId>(i);
        HWND combo = m_backButtonCombos[BackButtonIndex(id)];
        if (combo)
            PopulateBackButtonCombo(combo, m_controller->GetBackButtonMapping(id));
    }
}

void TrayApp::PopulateBackButtonCombo(HWND combo, BackButtonAction selected) {
    if (!combo)
        return;

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    const bool ds4Mode = m_controller->GetOutputMode() == VirtualControllerMode::DualShock4;
    int selectedIndex = 0;
    auto add = [&](const wchar_t* label, BackButtonAction action) {
        const int index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                        reinterpret_cast<LPARAM>(label)));
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index),
                     static_cast<LPARAM>(static_cast<int>(action)));
        if (action == selected)
            selectedIndex = index;
    };

    add(L"None", BackButtonAction::None);
    add(L"D-pad Up", BackButtonAction::DpadUp);
    add(L"D-pad Down", BackButtonAction::DpadDown);
    add(L"D-pad Left", BackButtonAction::DpadLeft);
    add(L"D-pad Right", BackButtonAction::DpadRight);
    add(ds4Mode ? L"Cross" : L"A", BackButtonAction::South);
    add(ds4Mode ? L"Circle" : L"B", BackButtonAction::East);
    add(ds4Mode ? L"Square" : L"X", BackButtonAction::West);
    add(ds4Mode ? L"Triangle" : L"Y", BackButtonAction::North);
    add(ds4Mode ? L"L1" : L"LB", BackButtonAction::LeftBumper);
    add(ds4Mode ? L"R1" : L"RB", BackButtonAction::RightBumper);
    add(ds4Mode ? L"L2" : L"LT", BackButtonAction::LeftTrigger);
    add(ds4Mode ? L"R2" : L"RT", BackButtonAction::RightTrigger);
    add(ds4Mode ? L"L3" : L"Left Stick Click", BackButtonAction::LeftStick);
    add(ds4Mode ? L"R3" : L"Right Stick Click", BackButtonAction::RightStick);
    add(ds4Mode ? L"Share" : L"Back", BackButtonAction::Back);
    add(ds4Mode ? L"Options" : L"Start", BackButtonAction::Start);
    add(ds4Mode ? L"PS" : L"Guide", BackButtonAction::Guide);

    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
}

void TrayApp::OnBackButtonMappingChanged(UINT controlId) {
    BackButtonId id = BackButtonIdFromComboId(controlId);
    HWND combo = m_backButtonCombos[BackButtonIndex(id)];
    if (!combo)
        return;

    const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR)
        return;

    const LRESULT itemData = SendMessageW(combo, CB_GETITEMDATA,
                                          static_cast<WPARAM>(selection),
                                          0);
    if (itemData == CB_ERR)
        return;

    const auto action = static_cast<BackButtonAction>(itemData);
    m_controller->SetBackButtonMapping(id, action);
    SaveSettings();
}

void TrayApp::ShowContextMenu() {
    bool connected      = m_controller->IsConnected();
    bool gameModeOn     = m_controller->IsGameModeActive();
    bool trackpadOn     = m_controller->IsTrackpadMouseEnabled();
    bool backButtonsOn  = m_controller->IsBackButtonsEnabled();
    bool leftTrackpad   = m_controller->IsUseLeftTrackpad();
    bool trackpadDpadOn = m_controller->IsTrackpadDpadEnabled();
    bool trackpadDpadRight = m_controller->IsTrackpadDpadUseRight();
    bool startupOn      = IsStartupEnabled();
    bool hideOriginal   = m_controller->IsHideOriginalControllerEnabled();
    bool hidHideAvailable = m_controller->IsHidHideAvailable();
    bool backButtonMappingsActive = m_controller->HasBackButtonMappings();
    VirtualControllerMode outputMode = m_controller->GetOutputMode();
    const bool ds4Mode = outputMode == VirtualControllerMode::DualShock4;
    const bool dpadLocksMouse = ds4Mode && trackpadDpadOn;

    HMENU menu = CreatePopupMenu();

    UINT toggleFlags = MF_STRING | (connected ? MF_ENABLED : MF_GRAYED);
    AppendMenuW(menu, toggleFlags, IDM_TOGGLE,
                gameModeOn ? L"Disable Steamless Mode" : L"Enable Steamless Mode");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU trackpadMenu = CreatePopupMenu();
    const UINT mouseModeFlags = dpadLocksMouse ? MF_GRAYED : MF_ENABLED;
    UINT trackpadFlags = MF_STRING | mouseModeFlags
                       | (trackpadOn && !dpadLocksMouse ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(trackpadMenu, trackpadFlags, IDM_TRACKPAD, L"Enable Trackpad Mouse");

    const UINT backMouseFlags = (dpadLocksMouse || backButtonMappingsActive) ? MF_GRAYED : MF_ENABLED;
    UINT backFlags = MF_STRING | backMouseFlags
                   | (backButtonsOn && !dpadLocksMouse && !backButtonMappingsActive
                      ? MF_CHECKED
                      : MF_UNCHECKED);
    AppendMenuW(trackpadMenu, backFlags, IDM_BACKBUTTONS, L"Enable Back Buttons for Clicking");

    UINT leftFlags = MF_STRING | mouseModeFlags
                   | (leftTrackpad && !dpadLocksMouse ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(trackpadMenu, leftFlags, IDM_LEFT_TRACKPAD, L"Use Left Trackpad Instead");

    AppendMenuW(trackpadMenu, MF_SEPARATOR, 0, nullptr);

    UINT dpadFlags = MF_STRING | (trackpadDpadOn ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(trackpadMenu, dpadFlags, IDM_TRACKPAD_DPAD, L"Use Trackpad as D-pad");

    UINT dpadRightFlags = MF_STRING | (trackpadDpadOn ? MF_ENABLED : MF_GRAYED)
                        | (trackpadDpadRight ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(trackpadMenu, dpadRightFlags, IDM_TRACKPAD_DPAD_RIGHT, L"Use Right Trackpad for D-pad");

    AppendMenuW(menu, MF_STRING, IDM_BACKBUTTON_MAPPINGS, L"Back Button Mappings...");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(trackpadMenu), L"Trackpad Settings");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU outputMenu = CreatePopupMenu();
    AppendMenuW(outputMenu,
                MF_STRING | (outputMode == VirtualControllerMode::Xbox360 ? MF_CHECKED : MF_UNCHECKED),
                IDM_OUTPUT_X360, L"Xbox 360");
    AppendMenuW(outputMenu,
                MF_STRING | (outputMode == VirtualControllerMode::DualShock4 ? MF_CHECKED : MF_UNCHECKED),
                IDM_OUTPUT_DS4, L"DualShock 4");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(outputMenu), L"Output Mode");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT hideFlags = MF_STRING | (hidHideAvailable ? MF_ENABLED : MF_GRAYED)
                   | (hideOriginal ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, hideFlags, IDM_HIDE_ORIGINAL, L"Hide Original Controller");

    UINT revealFlags = MF_STRING | (hidHideAvailable ? MF_ENABLED : MF_GRAYED);
    AppendMenuW(menu, revealFlags, IDM_REVEAL_ORIGINAL, L"Reveal Original Now");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT startupFlags = MF_STRING | (startupOn ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, startupFlags, IDM_STARTUP, L"Start with Windows");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    // SetForegroundWindow is required for the menu to dismiss on click-away.
    SetForegroundWindow(m_hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON,
                   pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}
