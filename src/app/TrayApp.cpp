#include "TrayApp.h"
#include "ControllerManager.h"
#include "resource.h"
#include <CommCtrl.h>
#include <shellapi.h>
#include <dbt.h>
#include <winreg.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <utility>

static TrayApp* g_app = nullptr;
static constexpr wchar_t GITHUB_API_HOST[] = L"api.github.com";
static constexpr wchar_t GITHUB_CURRENT_RELEASE_API_PATH_PREFIX[] =
    L"/repos/david419kr/steamless-controller-XB-PS-NS/releases/tags/Build-";
static constexpr wchar_t GITHUB_LATEST_RELEASE_API_PATH[] =
    L"/repos/david419kr/steamless-controller-XB-PS-NS/releases/latest";
static constexpr wchar_t GITHUB_LATEST_RELEASE_URL[] =
    L"https://github.com/david419kr/steamless-controller-XB-PS-NS/releases/latest";

static constexpr wchar_t WNDCLASS_NAME[] = L"SteamlessControllerTray";
static constexpr wchar_t BACKBUTTON_WNDCLASS_NAME[] = L"SteamlessControllerBackButtonMappings";
static constexpr wchar_t DUALSENSE_SETTINGS_WNDCLASS_NAME[] = L"SteamlessControllerDualSenseSettings";
static constexpr wchar_t PROCON2_SETTINGS_WNDCLASS_NAME[] = L"SteamlessControllerProCon2Settings";
static constexpr UINT BACKMAP_L4_ID = 2001;
static constexpr UINT BACKMAP_L5_ID = 2002;
static constexpr UINT BACKMAP_R4_ID = 2003;
static constexpr UINT BACKMAP_R5_ID = 2004;
static constexpr int DUALSENSE_RUMBLE_THRESHOLD_MIN = 0;
static constexpr int DUALSENSE_RUMBLE_THRESHOLD_MAX = 100;
static constexpr int DUALSENSE_RUMBLE_THRESHOLD_DEFAULT = 45;
static constexpr int PROCON2_RUMBLE_THRESHOLD_DEFAULT = 34;

static int BuildMonthFromDate(const char* date) {
    switch (date[0]) {
    case 'A': return date[1] == 'p' ? 4 : 8;
    case 'D': return 12;
    case 'F': return 2;
    case 'J': return date[1] == 'a' ? 1 : (date[2] == 'n' ? 6 : 7);
    case 'M': return date[2] == 'r' ? 3 : 5;
    case 'N': return 11;
    case 'O': return 10;
    case 'S': return 9;
    default: return 1;
    }
}

static void AppendTwoDigits(std::wstring& text, int value) {
    text.push_back(static_cast<wchar_t>(L'0' + (value / 10) % 10));
    text.push_back(static_cast<wchar_t>(L'0' + value % 10));
}

static int BuildDateCode() {
    const char* date = __DATE__;
    const int year = (date[9] - '0') * 10 + (date[10] - '0');
    const int month = BuildMonthFromDate(date);
    const int day = date[4] == ' '
                  ? date[5] - '0'
                  : (date[4] - '0') * 10 + (date[5] - '0');
    return year * 10000 + month * 100 + day;
}

static std::wstring BuildLabel() {
    const int code = BuildDateCode();
    std::wstring label = L"Build-";
    AppendTwoDigits(label, code / 10000);
    AppendTwoDigits(label, (code / 100) % 100);
    AppendTwoDigits(label, code % 100);
    return label;
}

static std::wstring BuildMenuLabel() {
    return BuildLabel() + L" (Check Update)";
}

static bool ParseBuildTag(const std::string& tag, int& code) {
    static constexpr char prefix[] = "Build-";
    if (tag.size() != std::strlen(prefix) + 6 ||
        tag.compare(0, std::strlen(prefix), prefix) != 0) {
        return false;
    }

    int parsed = 0;
    for (size_t i = std::strlen(prefix); i < tag.size(); ++i) {
        if (tag[i] < '0' || tag[i] > '9')
            return false;
        parsed = parsed * 10 + (tag[i] - '0');
    }

    code = parsed;
    return true;
}

static bool FindJsonStringValue(const std::string& json, const char* key, std::string& value) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";

    size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return false;

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return false;

    ++pos;
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\r' || json[pos] == '\n' || json[pos] == '\t')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"')
        return false;

    value.clear();
    ++pos;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"')
            return true;
        if (ch == '\\' && pos < json.size()) {
            value.push_back(json[pos++]);
        } else {
            value.push_back(ch);
        }
    }

    return false;
}

class WinHttpHandle {
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : m_handle(handle) {}
    ~WinHttpHandle() { Reset(); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    HINTERNET Get() const { return m_handle; }
    explicit operator bool() const { return m_handle != nullptr; }

    void Reset(HINTERNET handle = nullptr) {
        if (m_handle)
            WinHttpCloseHandle(m_handle);
        m_handle = handle;
    }

private:
    HINTERNET m_handle = nullptr;
};

struct HttpResponse {
    DWORD status = 0;
    DWORD error = ERROR_SUCCESS;
    std::string body;
};

static HttpResponse HttpGetGitHubApi(const wchar_t* path) {
    HttpResponse response{};

    WinHttpHandle session(WinHttpOpen(L"SteamlessController/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS,
                                      0));
    if (!session) {
        response.error = GetLastError();
        return response;
    }
    WinHttpSetTimeouts(session.Get(), 3000, 3000, 5000, 5000);

    WinHttpHandle connection(WinHttpConnect(session.Get(),
                                           GITHUB_API_HOST,
                                           INTERNET_DEFAULT_HTTPS_PORT,
                                           0));
    if (!connection) {
        response.error = GetLastError();
        return response;
    }

    const wchar_t* acceptTypes[] = {L"application/vnd.github+json", nullptr};
    WinHttpHandle request(WinHttpOpenRequest(connection.Get(),
                                            L"GET",
                                            path,
                                            nullptr,
                                            WINHTTP_NO_REFERER,
                                            acceptTypes,
                                            WINHTTP_FLAG_SECURE));
    if (!request) {
        response.error = GetLastError();
        return response;
    }

    static constexpr wchar_t headers[] =
        L"User-Agent: SteamlessController\r\n"
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";

    if (!WinHttpSendRequest(request.Get(),
                            headers,
                            static_cast<DWORD>((sizeof(headers) / sizeof(headers[0])) - 1),
                            WINHTTP_NO_REQUEST_DATA,
                            0,
                            0,
                            0) ||
        !WinHttpReceiveResponse(request.Get(), nullptr)) {
        response.error = GetLastError();
        return response;
    }

    DWORD statusSize = sizeof(response.status);
    if (!WinHttpQueryHeaders(request.Get(),
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &response.status,
                             &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        response.error = GetLastError();
        return response;
    }

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.Get(), &available)) {
            response.error = GetLastError();
            return response;
        }
        if (available == 0)
            break;

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.Get(), chunk.data(), available, &read)) {
            response.error = GetLastError();
            return response;
        }
        response.body.append(chunk.data(), read);
    }

    return response;
}

struct UpdateCheckResult {
    bool openLatestRelease = false;
    std::wstring title;
    std::wstring info;
};

static std::wstring CurrentReleaseTagApiPath() {
    const int code = BuildDateCode();
    std::wstring path = GITHUB_CURRENT_RELEASE_API_PATH_PREFIX;
    AppendTwoDigits(path, code / 10000);
    AppendTwoDigits(path, (code / 100) % 100);
    AppendTwoDigits(path, code % 100);
    return path;
}

static UpdateCheckResult CheckGitHubReleaseUpdate() {
    const int currentBuild = BuildDateCode();
    const std::wstring currentPath = CurrentReleaseTagApiPath();
    const HttpResponse currentRelease = HttpGetGitHubApi(currentPath.c_str());

    if (currentRelease.status == 404) {
        return {false,
                L"Up to date",
                L"This build tag is not published yet, so it is treated as current."};
    }
    if (currentRelease.status != 200) {
        return {false,
                L"Update check failed",
                L"Could not check the current build tag on GitHub."};
    }

    const HttpResponse latestRelease = HttpGetGitHubApi(GITHUB_LATEST_RELEASE_API_PATH);
    if (latestRelease.status == 404) {
        return {false, L"Up to date", L"No public GitHub release was found."};
    }
    if (latestRelease.status != 200) {
        return {false,
                L"Update check failed",
                L"Could not check the latest GitHub release."};
    }

    std::string latestTag;
    int latestBuild = 0;
    if (!FindJsonStringValue(latestRelease.body, "tag_name", latestTag) ||
        !ParseBuildTag(latestTag, latestBuild)) {
        return {false,
                L"Update check failed",
                L"The latest release tag is not in the Build-YYMMDD format."};
    }

    if (latestBuild > currentBuild) {
        return {true,
                L"Update available",
                L"A newer release is available. Opening GitHub releases."};
    }

    return {false, L"Up to date", L"You are running the latest release."};
}

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
    return value <= static_cast<DWORD>(BackButtonAction::GR);
}

static const wchar_t* BackButtonMappingPrefixForMode(VirtualControllerMode mode) {
    if (mode == VirtualControllerMode::Switch2Pro)
        return L"SwitchBackMap";
    if (mode == VirtualControllerMode::SwitchPro)
        return L"SwitchProBackMap";
    return L"BackMap";
}

static const wchar_t* BackButtonMappingSuffix(BackButtonId id) {
    switch (id) {
    case BackButtonId::L4: return L"L4";
    case BackButtonId::L5: return L"L5";
    case BackButtonId::R4: return L"R4";
    case BackButtonId::R5: return L"R5";
    default: return L"L4";
    }
}

static std::wstring BackButtonMappingValueName(VirtualControllerMode mode, BackButtonId id) {
    std::wstring name = BackButtonMappingPrefixForMode(mode);
    name += BackButtonMappingSuffix(id);
    return name;
}

static bool IsPlayStationOutputMode(VirtualControllerMode mode) {
    return mode == VirtualControllerMode::DualShock4 ||
           mode == VirtualControllerMode::DualSense;
}

static bool IsSwitchOutputMode(VirtualControllerMode mode) {
    return mode == VirtualControllerMode::Switch2Pro ||
           mode == VirtualControllerMode::SwitchPro;
}

static bool IsSwitch2OutputMode(VirtualControllerMode mode) {
    return mode == VirtualControllerMode::Switch2Pro;
}

static bool IsTrackpadDpadLockingOutputMode(VirtualControllerMode mode) {
    return IsPlayStationOutputMode(mode);
}

static int ThresholdToSliderPosition(double threshold) {
    const double clamped = std::clamp(threshold, 0.0, 1.0);
    return std::clamp<int>(static_cast<int>(std::lround(clamped * 100.0)),
                           DUALSENSE_RUMBLE_THRESHOLD_MIN,
                           DUALSENSE_RUMBLE_THRESHOLD_MAX);
}

static double SliderPositionToThreshold(int position) {
    const int clamped = std::clamp(position,
                                   DUALSENSE_RUMBLE_THRESHOLD_MIN,
                                   DUALSENSE_RUMBLE_THRESHOLD_MAX);
    return static_cast<double>(clamped) / 100.0;
}

TrayApp::TrayApp() {
    g_app = this;
}

TrayApp::~TrayApp() {
    if (m_hwnd)
        KillTimer(m_hwnd, DEVICE_POLL_TIMER_ID);
    if (m_deviceNotify) {
        UnregisterDeviceNotification(m_deviceNotify);
        m_deviceNotify = nullptr;
    }
    RemoveTrayIcon();
    g_app = nullptr;
}

bool TrayApp::Init(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    m_iconOff   = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_OFF));
    m_iconOn    = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_ON));
    m_wmTaskbar = RegisterWindowMessageW(L"TaskbarCreated");

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

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

    WNDCLASSEXW dualSenseWc{};
    dualSenseWc.cbSize        = sizeof(dualSenseWc);
    dualSenseWc.lpfnWndProc   = WndProc;
    dualSenseWc.hInstance     = hInstance;
    dualSenseWc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    dualSenseWc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    dualSenseWc.lpszClassName = DUALSENSE_SETTINGS_WNDCLASS_NAME;
    if (!RegisterClassExW(&dualSenseWc)) return false;

    WNDCLASSEXW proCon2Wc{};
    proCon2Wc.cbSize        = sizeof(proCon2Wc);
    proCon2Wc.lpfnWndProc   = WndProc;
    proCon2Wc.hInstance     = hInstance;
    proCon2Wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    proCon2Wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    proCon2Wc.lpszClassName = PROCON2_SETTINGS_WNDCLASS_NAME;
    if (!RegisterClassExW(&proCon2Wc)) return false;

    // Hidden top-level window so it receives broadcast shell notifications including TaskbarCreated when Explorer restarts.
    m_hwnd = CreateWindowExW(0, WNDCLASS_NAME, L"SteamlessController",
                             0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!m_hwnd) return false;

    // Register for HID device arrival/removal notifications.
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size       = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    // HID device interface GUID
    filter.dbcc_classguid  = {0x4D1E55B2, 0xF16F, 0x11CF,
                              {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    m_deviceNotify = RegisterDeviceNotificationW(m_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    m_controller = std::make_unique<ControllerManager>(
        [this](bool connected, bool gameModeActive, VirtualControllerError virtualError) {
            UpdateTrayIcon(connected, gameModeActive, virtualError);
        });

    LoadSettings();
    AddTrayIcon();
    MaybeAutoEnableSteamlessMode();
    SetTimer(m_hwnd, DEVICE_POLL_TIMER_ID, DEVICE_POLL_INTERVAL_MS, nullptr);
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
    if (msg == WM_UPDATE_CHECK_RESULT) {
        HandleUpdateCheckResult(lp);
        return 0;
    }

    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == NIN_BALLOONUSERCLICK &&
            m_lastVirtualControllerError != VirtualControllerError::None)
            ShellExecuteW(nullptr, L"open", GITHUB_LATEST_RELEASE_URL, nullptr, nullptr, SW_SHOWNORMAL);
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
            if (m_controller->IsGameModeActive()) {
                m_controller->DisableGameMode();
                if (m_autoEnable && m_controller->IsConnected())
                    m_autoEnableSuppressedUntilReconnect = true;
            } else {
                m_controller->EnableGameMode();
            }
            break;
        case IDM_AUTO_ENABLE:
            m_autoEnable = !m_autoEnable;
            m_autoEnableSuppressedUntilReconnect = false;
            m_autoEnableAttemptedForConnection = false;
            SaveSettings();
            MaybeAutoEnableSteamlessMode();
            break;
        case IDM_AUTO_RESTART_STEAM:
            if (m_autoEnable) {
                m_autoRestartSteam = !m_autoRestartSteam;
                SaveSettings();
            }
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
            LoadBackButtonMappingsForCurrentMode();
            RefreshBackButtonMappingWindow();
            RefreshDualSenseSettingsWindow();
            RefreshProCon2SettingsWindow();
            SaveSettings();
            break;
        case IDM_OUTPUT_DS4:
            m_controller->SetOutputMode(VirtualControllerMode::DualShock4);
            LoadBackButtonMappingsForCurrentMode();
            RefreshBackButtonMappingWindow();
            RefreshDualSenseSettingsWindow();
            RefreshProCon2SettingsWindow();
            SaveSettings();
            break;
        case IDM_OUTPUT_DSENSE:
            m_controller->SetOutputMode(VirtualControllerMode::DualSense);
            LoadBackButtonMappingsForCurrentMode();
            RefreshBackButtonMappingWindow();
            RefreshDualSenseSettingsWindow();
            RefreshProCon2SettingsWindow();
            SaveSettings();
            break;
        case IDM_OUTPUT_SWITCH2PRO:
            m_controller->SetOutputMode(VirtualControllerMode::Switch2Pro);
            LoadBackButtonMappingsForCurrentMode();
            RefreshBackButtonMappingWindow();
            RefreshDualSenseSettingsWindow();
            RefreshProCon2SettingsWindow();
            SaveSettings();
            break;
        case IDM_OUTPUT_SWITCHPRO:
            m_controller->SetOutputMode(VirtualControllerMode::SwitchPro);
            LoadBackButtonMappingsForCurrentMode();
            RefreshBackButtonMappingWindow();
            RefreshDualSenseSettingsWindow();
            RefreshProCon2SettingsWindow();
            SaveSettings();
            break;
        case IDM_DUALSENSE_SETTINGS:
            if (m_controller->GetOutputMode() == VirtualControllerMode::DualSense)
                ShowDualSenseSettingsWindow();
            break;
        case IDM_PROCON2_SETTINGS:
            if (m_controller->GetOutputMode() == VirtualControllerMode::Switch2Pro)
                ShowProCon2SettingsWindow();
            break;
        case IDM_HIDE_ORIGINAL:
            m_controller->SetHideOriginalControllerEnabled(!m_controller->IsHideOriginalControllerEnabled());
            SaveSettings();
            break;
        case IDM_REVEAL_ORIGINAL:
            m_controller->RevealOriginalControllerNow();
            break;
        case IDM_RESTART_STEAM:
            RestartSteam(/*launchIfNotRunning=*/true);
            break;
        case IDM_BUILD_INFO:
            CheckForUpdates();
            break;
        case IDC_DS_RUMBLE_THRESHOLD_RESET:
            ResetDualSenseRumbleThreshold();
            break;
        case IDC_PROCON2_RUMBLE_THRESHOLD_RESET:
            ResetProCon2RumbleThreshold();
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
        if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) {
            m_controller->OnDeviceChange();
            MaybeAutoEnableSteamlessMode();
        }
        return TRUE;

    case WM_TIMER:
        if (wp == DEVICE_POLL_TIMER_ID) {
            m_controller->PollForController();
            MaybeAutoEnableSteamlessMode();
            return 0;
        }
        break;

    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lp) == m_dualSenseThresholdSlider) {
            OnDualSenseRumbleThresholdChanged();
            return 0;
        }
        if (reinterpret_cast<HWND>(lp) == m_proCon2ThresholdSlider) {
            OnProCon2RumbleThresholdChanged();
            return 0;
        }
        break;

    case WM_CLOSE:
        if (hwnd == m_backButtonHwnd) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (hwnd == m_dualSenseSettingsHwnd) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (hwnd == m_proCon2SettingsHwnd) {
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
        if (hwnd == m_dualSenseSettingsHwnd) {
            m_dualSenseSettingsHwnd = nullptr;
            m_dualSenseThresholdSlider = nullptr;
            m_dualSenseThresholdValue = nullptr;
            m_dualSenseThresholdReset = nullptr;
            return 0;
        }
        if (hwnd == m_proCon2SettingsHwnd) {
            m_proCon2SettingsHwnd = nullptr;
            m_proCon2ThresholdSlider = nullptr;
            m_proCon2ThresholdValue = nullptr;
            m_proCon2ThresholdReset = nullptr;
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

void TrayApp::UpdateTrayIcon(bool connected,
                             bool gameModeActive,
                             VirtualControllerError virtualControllerError) {
    m_lastVirtualControllerError = virtualControllerError;
    if (virtualControllerError != VirtualControllerError::None) {
        ShowVirtualControllerBalloon(virtualControllerError);
        return;
    }
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

static const wchar_t* VirtualControllerErrorTitle(VirtualControllerError error) {
    switch (error) {
    case VirtualControllerError::ViiperExeMissing:
        return L"VIIPER sidecar required";
    case VirtualControllerError::ViiperUnsupported:
        return L"Unsupported VIIPER version";
    case VirtualControllerError::UsbIpDriverMissing:
        return L"USBIP driver required";
    default:
        return L"VIIPER backend unavailable";
    }
}

static const wchar_t* VirtualControllerErrorInfo(VirtualControllerError error) {
    switch (error) {
    case VirtualControllerError::ViiperExeMissing:
        return L"Build or install the patched viiper.exe sidecar next to SteamlessController.exe.";
    case VirtualControllerError::ViiperUnsupported:
        return L"Use the SteamlessController patched VIIPER sidecar; stock v0.6.1 is rejected in PlayStation modes.";
    case VirtualControllerError::UsbIpDriverMissing:
        return L"Install usbip-win2; VIIPER needs it to attach virtual USB devices on Windows.";
    case VirtualControllerError::DeviceCreateFailed:
        return L"VIIPER could not create or attach the virtual controller.";
    case VirtualControllerError::StreamConnectFailed:
        return L"SteamlessController could not connect to the VIIPER device stream.";
    default:
        return L"SteamlessController could not reach VIIPER. Click here to open SteamlessController releases.";
    }
}

void TrayApp::ShowVirtualControllerBalloon(VirtualControllerError error) {
    ShowTrayBalloon(VirtualControllerErrorTitle(error),
                    VirtualControllerErrorInfo(error),
                    NIIF_WARNING);
}

void TrayApp::ShowTrayBalloon(const wchar_t* title, const wchar_t* info, DWORD infoFlags) {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_INFO;
    nid.dwInfoFlags      = infoFlags;
    wcscpy_s(nid.szInfoTitle, title);
    wcscpy_s(nid.szInfo,      info);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayApp::CheckForUpdates() {
    if (m_updateCheckInProgress) {
        ShowTrayBalloon(L"Update check",
                        L"An update check is already running.",
                        NIIF_INFO);
        return;
    }

    m_updateCheckInProgress = true;
    ShowTrayBalloon(L"Checking for updates",
                    L"Checking GitHub releases...",
                    NIIF_INFO);

    HWND hwnd = m_hwnd;
    const UINT resultMessage = WM_UPDATE_CHECK_RESULT;
    std::thread([hwnd, resultMessage]() {
        auto* result = new UpdateCheckResult(CheckGitHubReleaseUpdate());
        if (!PostMessageW(hwnd, resultMessage, 0, reinterpret_cast<LPARAM>(result)))
            delete result;
    }).detach();
}

void TrayApp::HandleUpdateCheckResult(LPARAM lp) {
    std::unique_ptr<UpdateCheckResult> result(reinterpret_cast<UpdateCheckResult*>(lp));
    m_updateCheckInProgress = false;
    if (!result)
        return;

    ShowTrayBalloon(result->title.c_str(), result->info.c_str(), NIIF_INFO);
    if (result->openLatestRelease)
        ShellExecuteW(nullptr, L"open", GITHUB_LATEST_RELEASE_URL, nullptr, nullptr, SW_SHOWNORMAL);
}

static constexpr wchar_t REG_KEY[]     = L"Software\\SteamlessController";
static constexpr wchar_t REG_RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr wchar_t APP_NAME[]    = L"SteamlessController";
static constexpr wchar_t STEAM_PROCESS_NAME[] = L"steam.exe";

static std::wstring TrimRegistryString(std::wstring value) {
    while (!value.empty() && value.back() == L'\0')
        value.pop_back();
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
        value = value.substr(1, value.size() - 2);
    for (wchar_t& ch : value) {
        if (ch == L'/')
            ch = L'\\';
    }
    return value;
}

static bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::wstring ReadRegistryString(HKEY root,
                                       const wchar_t* subkey,
                                       const wchar_t* valueName,
                                       REGSAM viewFlags = 0) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | viewFlags, &key) != ERROR_SUCCESS)
        return {};

    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) ||
        size == 0) {
        RegCloseKey(key);
        return {};
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, valueName, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(value.data()), &size) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return {};
    }
    RegCloseKey(key);

    value = TrimRegistryString(value);
    if (type == REG_EXPAND_SZ) {
        DWORD expandedSize = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (expandedSize > 0) {
            std::wstring expanded(expandedSize, L'\0');
            ExpandEnvironmentStringsW(value.c_str(), expanded.data(), expandedSize);
            value = TrimRegistryString(expanded);
        }
    }
    return value;
}

static std::wstring SteamExeFromPath(std::wstring path) {
    path = TrimRegistryString(std::move(path));
    if (path.empty())
        return {};
    if (FileExists(path))
        return path;
    if (path.back() != L'\\')
        path.push_back(L'\\');
    path += STEAM_PROCESS_NAME;
    return FileExists(path) ? path : std::wstring{};
}

static std::wstring FindSteamExecutablePath() {
    struct RegistryProbe {
        HKEY root;
        const wchar_t* subkey;
        const wchar_t* valueName;
        REGSAM viewFlags;
        bool valueIsDirectory;
    };

    const RegistryProbe probes[] = {
        {HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe", 0, false},
        {HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", 0, true},
        {HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", L"InstallPath", KEY_WOW64_32KEY, true},
        {HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", L"InstallPath", KEY_WOW64_64KEY, true},
    };

    for (const auto& probe : probes) {
        std::wstring value = ReadRegistryString(probe.root, probe.subkey, probe.valueName, probe.viewFlags);
        if (value.empty())
            continue;
        std::wstring exe = probe.valueIsDirectory ? SteamExeFromPath(value) : TrimRegistryString(value);
        if (FileExists(exe))
            return exe;
    }

    wchar_t programFilesX86[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::wstring exe = std::wstring(programFilesX86) + L"\\Steam\\steam.exe";
        if (FileExists(exe))
            return exe;
    }

    wchar_t programFiles[MAX_PATH]{};
    len = GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::wstring exe = std::wstring(programFiles) + L"\\Steam\\steam.exe";
        if (FileExists(exe))
            return exe;
    }

    return {};
}

static bool IsSteamRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, STEAM_PROCESS_NAME) == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return running;
}

static void RestartSteamProcess(std::wstring steamExe, bool launchIfNotRunning) {
    const bool wasRunning = IsSteamRunning();
    if (wasRunning) {
        ShellExecuteW(nullptr, L"open", steamExe.c_str(), L"-shutdown", nullptr, SW_HIDE);
        for (int i = 0; i < 60 && IsSteamRunning(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    if (wasRunning || launchIfNotRunning)
        ShellExecuteW(nullptr, L"open", steamExe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void TrayApp::RestartSteam(bool launchIfNotRunning) {
    if (!launchIfNotRunning && !IsSteamRunning())
        return;

    std::wstring steamExe = FindSteamExecutablePath();
    if (steamExe.empty()) {
        ShowTrayBalloon(L"Steam not found",
                        L"SteamlessController could not find steam.exe in the registry or default install paths.",
                        NIIF_WARNING);
        return;
    }

    std::thread(RestartSteamProcess, std::move(steamExe), launchIfNotRunning).detach();
}

void TrayApp::MaybeAutoEnableSteamlessMode() {
    if (!m_controller->IsConnected()) {
        m_autoEnableSuppressedUntilReconnect = false;
        m_autoEnableAttemptedForConnection = false;
        return;
    }

    if (!m_autoEnable ||
        m_controller->IsGameModeActive() ||
        m_autoEnableSuppressedUntilReconnect ||
        m_autoEnableAttemptedForConnection) {
        return;
    }

    m_autoEnableAttemptedForConnection = true;
    m_controller->EnableGameMode();
    if (m_controller->IsGameModeActive() && m_autoRestartSteam)
        RestartSteam(/*launchIfNotRunning=*/false);
}

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
    auto readThreshold = [&](const wchar_t* name, int defaultValue) -> double {
        DWORD val = static_cast<DWORD>(defaultValue), size = sizeof(val);
        if (RegQueryValueExW(key, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
            return SliderPositionToThreshold(static_cast<int>(val));
        }
        return SliderPositionToThreshold(defaultValue);
    };

    DWORD outputMode = 0, outputModeSize = sizeof(outputMode);
    if (RegQueryValueExW(key, L"OutputMode", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&outputMode), &outputModeSize) == ERROR_SUCCESS) {
        VirtualControllerMode mode = VirtualControllerMode::Xbox360;
        if (outputMode == 1)
            mode = VirtualControllerMode::DualShock4;
        else if (outputMode == 2)
            mode = VirtualControllerMode::DualSense;
        else if (outputMode == 3)
            mode = VirtualControllerMode::Switch2Pro;
        else if (outputMode == 4)
            mode = VirtualControllerMode::SwitchPro;
        m_controller->SetOutputMode(mode);
    }

    m_controller->SetTrackpadMouseEnabled(readBool(L"TrackpadMouse",   false));
    m_controller->SetBackButtonsEnabled  (readBool(L"BackButtons",     false));
    m_controller->SetUseLeftTrackpad     (readBool(L"UseLeftTrackpad", false));
    m_controller->SetTrackpadDpadEnabled (readBool(L"TrackpadDpad",    false));
    m_controller->SetTrackpadDpadUseRight(readBool(L"TrackpadDpadRight", false));
    m_controller->SetHideOriginalControllerEnabled(readBool(L"HideOriginalController", true));
    m_autoEnable = readBool(L"AutoEnable", false);
    m_autoRestartSteam = readBool(L"AutoRestartSteam", false);
    LoadBackButtonMappingsForCurrentMode(key);
    m_controller->SetDualSenseAudioRumbleThreshold(
        readThreshold(L"DualSenseRumbleThreshold", DUALSENSE_RUMBLE_THRESHOLD_DEFAULT));
    m_controller->SetSwitch2ProRumbleImpactThreshold(
        readThreshold(L"Switch2ProRumbleThreshold", PROCON2_RUMBLE_THRESHOLD_DEFAULT));

    RegCloseKey(key);
}

void TrayApp::LoadBackButtonMappingsForCurrentMode() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        m_controller->SetBackButtonMappingsFromSettings(BackButtonMappings{}, false);
        return;
    }

    LoadBackButtonMappingsForCurrentMode(key);
    RegCloseKey(key);
}

void TrayApp::LoadBackButtonMappingsForCurrentMode(HKEY key) {
    const auto mode = m_controller->GetOutputMode();
    auto readAction = [&](BackButtonId id, bool& found) -> BackButtonAction {
        const std::wstring name = BackButtonMappingValueName(mode, id);
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(key, name.c_str(), nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
            found = true;
            if (IsValidBackButtonAction(val))
                return static_cast<BackButtonAction>(val);
        }
        return BackButtonAction::None;
    };

    bool hasBackMapL4 = false;
    bool hasBackMapL5 = false;
    bool hasBackMapR4 = false;
    bool hasBackMapR5 = false;
    BackButtonMappings mappings{};
    mappings.Set(BackButtonId::L4, readAction(BackButtonId::L4, hasBackMapL4));
    mappings.Set(BackButtonId::L5, readAction(BackButtonId::L5, hasBackMapL5));
    mappings.Set(BackButtonId::R4, readAction(BackButtonId::R4, hasBackMapR4));
    mappings.Set(BackButtonId::R5, readAction(BackButtonId::R5, hasBackMapR5));
    m_controller->SetBackButtonMappingsFromSettings(
        mappings,
        hasBackMapL4 || hasBackMapL5 || hasBackMapR4 || hasBackMapR5);
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
    auto writeThreshold = [&](const wchar_t* name, double threshold) {
        DWORD dw = static_cast<DWORD>(ThresholdToSliderPosition(threshold));
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
    };

    writeBool(L"TrackpadMouse",   m_controller->IsTrackpadMouseEnabled());
    writeBool(L"BackButtons",     m_controller->IsBackButtonsEnabled());
    writeBool(L"UseLeftTrackpad", m_controller->IsUseLeftTrackpad());
    writeBool(L"TrackpadDpad",    m_controller->IsTrackpadDpadEnabled());
    writeBool(L"TrackpadDpadRight", m_controller->IsTrackpadDpadUseRight());
    writeBool(L"HideOriginalController", m_controller->IsHideOriginalControllerEnabled());
    writeBool(L"AutoEnable", m_autoEnable);
    writeBool(L"AutoRestartSteam", m_autoRestartSteam);
    DWORD outputMode = 0u;
    if (m_controller->GetOutputMode() == VirtualControllerMode::DualShock4)
        outputMode = 1u;
    else if (m_controller->GetOutputMode() == VirtualControllerMode::DualSense)
        outputMode = 2u;
    else if (m_controller->GetOutputMode() == VirtualControllerMode::Switch2Pro)
        outputMode = 3u;
    else if (m_controller->GetOutputMode() == VirtualControllerMode::SwitchPro)
        outputMode = 4u;
    RegSetValueExW(key, L"OutputMode", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&outputMode), sizeof(outputMode));
    SaveBackButtonMappingsForCurrentMode(key);
    writeThreshold(L"DualSenseRumbleThreshold", m_controller->GetDualSenseAudioRumbleThreshold());
    writeThreshold(L"Switch2ProRumbleThreshold", m_controller->GetSwitch2ProRumbleImpactThreshold());

    RegCloseKey(key);
}

void TrayApp::SaveBackButtonMappingsForCurrentMode(HKEY key) {
    const auto mode = m_controller->GetOutputMode();
    auto writeAction = [&](BackButtonId id) {
        const std::wstring name = BackButtonMappingValueName(mode, id);
        DWORD dw = static_cast<DWORD>(m_controller->GetBackButtonMapping(id));
        RegSetValueExW(key, name.c_str(), 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
    };

    writeAction(BackButtonId::L4);
    writeAction(BackButtonId::L5);
    writeAction(BackButtonId::R4);
    writeAction(BackButtonId::R5);
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

    const VirtualControllerMode mode = m_controller->GetOutputMode();
    const bool playStationMode = IsPlayStationOutputMode(mode);
    SetWindowTextW(m_backButtonHwnd,
                   mode == VirtualControllerMode::DualSense
                       ? L"Back Button Mappings - DualSense"
                       : mode == VirtualControllerMode::Switch2Pro
                       ? L"Back Button Mappings - Switch 2 Pro"
                       : mode == VirtualControllerMode::SwitchPro
                       ? L"Back Button Mappings - Switch Pro"
                       : playStationMode ? L"Back Button Mappings - DualShock 4"
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

    const VirtualControllerMode mode = m_controller->GetOutputMode();
    const bool playStationMode = IsPlayStationOutputMode(mode);
    const bool switchMode = IsSwitchOutputMode(mode);
    const bool switch2Mode = IsSwitch2OutputMode(mode);
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
    add(playStationMode ? L"Cross" : (switchMode ? L"B" : L"A"), BackButtonAction::South);
    add(playStationMode ? L"Circle" : (switchMode ? L"A" : L"B"), BackButtonAction::East);
    add(playStationMode ? L"Square" : (switchMode ? L"Y" : L"X"), BackButtonAction::West);
    add(playStationMode ? L"Triangle" : (switchMode ? L"X" : L"Y"), BackButtonAction::North);
    add(playStationMode ? L"L1" : (switchMode ? L"L" : L"LB"), BackButtonAction::LeftBumper);
    add(playStationMode ? L"R1" : (switchMode ? L"R" : L"RB"), BackButtonAction::RightBumper);
    add(playStationMode ? L"L2" : (switchMode ? L"ZL" : L"LT"), BackButtonAction::LeftTrigger);
    add(playStationMode ? L"R2" : (switchMode ? L"ZR" : L"RT"), BackButtonAction::RightTrigger);
    add(playStationMode ? L"L3" : L"Left Stick Click", BackButtonAction::LeftStick);
    add(playStationMode ? L"R3" : L"Right Stick Click", BackButtonAction::RightStick);
    add(playStationMode ? L"Create / Share" : (switchMode ? L"Minus" : L"Back"), BackButtonAction::Back);
    add(playStationMode ? L"Options" : (switchMode ? L"Plus" : L"Start"), BackButtonAction::Start);
    add(playStationMode ? L"PS" : (switchMode ? L"Home" : L"Guide"), BackButtonAction::Guide);
    if (switch2Mode) {
        add(L"GL", BackButtonAction::GL);
        add(L"GR", BackButtonAction::GR);
    }

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

void TrayApp::ShowDualSenseSettingsWindow() {
    if (m_dualSenseSettingsHwnd) {
        RefreshDualSenseSettingsWindow();
        ShowWindow(m_dualSenseSettingsHwnd, SW_SHOWNORMAL);
        SetForegroundWindow(m_dualSenseSettingsHwnd);
        return;
    }

    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    constexpr DWORD exStyle = WS_EX_TOOLWINDOW;
    RECT rect{0, 0, 430, 220};
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    HWND hwnd = CreateWindowExW(
        exStyle,
        DUALSENSE_SETTINGS_WNDCLASS_NAME,
        L"DualSense Settings",
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

    m_dualSenseSettingsHwnd = hwnd;
    CreateDualSenseSettingsControls();
    RefreshDualSenseSettingsWindow();
    ShowWindow(m_dualSenseSettingsHwnd, SW_SHOWNORMAL);
    SetForegroundWindow(m_dualSenseSettingsHwnd);
}

void TrayApp::CreateDualSenseSettingsControls() {
    if (!m_dualSenseSettingsHwnd)
        return;

    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    HWND label = CreateWindowExW(
        0, L"STATIC", L"Rumble Threshold",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        18, 20, 150, 20,
        m_dualSenseSettingsHwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    m_dualSenseThresholdValue = CreateWindowExW(
        0, L"STATIC", L"0.62",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        350, 20, 54, 20,
        m_dualSenseSettingsHwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DS_RUMBLE_THRESHOLD_VALUE)),
        m_hInstance,
        nullptr);
    SendMessageW(m_dualSenseThresholdValue, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    m_dualSenseThresholdSlider = CreateWindowExW(
        0, TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
        18, 48, 386, 34,
        m_dualSenseSettingsHwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DS_RUMBLE_THRESHOLD)),
        m_hInstance,
        nullptr);
    SendMessageW(m_dualSenseThresholdSlider, TBM_SETRANGE, TRUE,
                 MAKELPARAM(DUALSENSE_RUMBLE_THRESHOLD_MIN, DUALSENSE_RUMBLE_THRESHOLD_MAX));
    SendMessageW(m_dualSenseThresholdSlider, TBM_SETTICFREQ, 10, 0);
    SendMessageW(m_dualSenseThresholdSlider, TBM_SETPAGESIZE, 0, 5);
    SendMessageW(m_dualSenseThresholdSlider, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND minLabel = CreateWindowExW(
        0, L"STATIC", L"0.00",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        18, 78, 48, 18,
        m_dualSenseSettingsHwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(minLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND maxLabel = CreateWindowExW(
        0, L"STATIC", L"1.00",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        356, 78, 48, 18,
        m_dualSenseSettingsHwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(maxLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND description = CreateWindowExW(
        0,
        L"STATIC",
        L"Lower values allow strong haptics to blend into more rumble.\r\n"
        L"The best value can vary by game. Too high or too low can screw up the haptic experience.\r\n"
        L"If unsure, use the default value.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        18, 104, 386, 54,
        m_dualSenseSettingsHwnd,
        nullptr,
        m_hInstance,
        nullptr);
    SendMessageW(description, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    m_dualSenseThresholdReset = CreateWindowExW(
        0, L"BUTTON", L"Reset To Default",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        268, 166, 136, 28,
        m_dualSenseSettingsHwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DS_RUMBLE_THRESHOLD_RESET)),
        m_hInstance,
        nullptr);
    SendMessageW(m_dualSenseThresholdReset, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void TrayApp::RefreshDualSenseSettingsWindow() {
    if (!m_dualSenseSettingsHwnd)
        return;

    const bool enabled = m_controller->GetOutputMode() == VirtualControllerMode::DualSense;
    EnableWindow(m_dualSenseThresholdSlider, enabled);
    EnableWindow(m_dualSenseThresholdReset, enabled);

    const double threshold = m_controller->GetDualSenseAudioRumbleThreshold();
    const int sliderPos = ThresholdToSliderPosition(threshold);
    if (m_dualSenseThresholdSlider)
        SendMessageW(m_dualSenseThresholdSlider, TBM_SETPOS, TRUE, sliderPos);

    if (m_dualSenseThresholdValue) {
        wchar_t text[32]{};
        swprintf_s(text, L"%.2f", SliderPositionToThreshold(sliderPos));
        SetWindowTextW(m_dualSenseThresholdValue, text);
    }
}

void TrayApp::OnDualSenseRumbleThresholdChanged() {
    if (!m_dualSenseThresholdSlider ||
        m_controller->GetOutputMode() != VirtualControllerMode::DualSense)
        return;

    const int sliderPos = static_cast<int>(SendMessageW(m_dualSenseThresholdSlider, TBM_GETPOS, 0, 0));
    m_controller->SetDualSenseAudioRumbleThreshold(SliderPositionToThreshold(sliderPos));
    RefreshDualSenseSettingsWindow();
    SaveSettings();
}

void TrayApp::ResetDualSenseRumbleThreshold() {
    if (m_controller->GetOutputMode() != VirtualControllerMode::DualSense)
        return;

    m_controller->SetDualSenseAudioRumbleThreshold(
        SliderPositionToThreshold(DUALSENSE_RUMBLE_THRESHOLD_DEFAULT));
    RefreshDualSenseSettingsWindow();
    SaveSettings();
}

void TrayApp::ShowProCon2SettingsWindow() {
    if (m_proCon2SettingsHwnd) {
        RefreshProCon2SettingsWindow();
        ShowWindow(m_proCon2SettingsHwnd, SW_SHOWNORMAL);
        SetForegroundWindow(m_proCon2SettingsHwnd);
        return;
    }

    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    constexpr DWORD exStyle = WS_EX_TOOLWINDOW;
    RECT rect{0, 0, 424, 214};
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    HWND hwnd = CreateWindowExW(
        exStyle,
        PROCON2_SETTINGS_WNDCLASS_NAME,
        L"Pro Con 2 Settings",
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

    m_proCon2SettingsHwnd = hwnd;
    CreateProCon2SettingsControls();
    RefreshProCon2SettingsWindow();
    ShowWindow(m_proCon2SettingsHwnd, SW_SHOWNORMAL);
    SetForegroundWindow(m_proCon2SettingsHwnd);
}

void TrayApp::CreateProCon2SettingsControls() {
    if (!m_proCon2SettingsHwnd)
        return;

    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    HWND label = CreateWindowExW(
        0, L"STATIC", L"Rumble Threshold",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        18, 20, 150, 20,
        m_proCon2SettingsHwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    m_proCon2ThresholdValue = CreateWindowExW(
        0, L"STATIC", L"0.34",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        350, 20, 54, 20,
        m_proCon2SettingsHwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PROCON2_RUMBLE_THRESHOLD_VALUE)),
        m_hInstance,
        nullptr);
    SendMessageW(m_proCon2ThresholdValue, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    m_proCon2ThresholdSlider = CreateWindowExW(
        0, TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
        18, 48, 386, 34,
        m_proCon2SettingsHwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PROCON2_RUMBLE_THRESHOLD)),
        m_hInstance,
        nullptr);
    SendMessageW(m_proCon2ThresholdSlider, TBM_SETRANGE, TRUE,
                 MAKELPARAM(DUALSENSE_RUMBLE_THRESHOLD_MIN, DUALSENSE_RUMBLE_THRESHOLD_MAX));
    SendMessageW(m_proCon2ThresholdSlider, TBM_SETTICFREQ, 10, 0);
    SendMessageW(m_proCon2ThresholdSlider, TBM_SETPAGESIZE, 0, 5);
    SendMessageW(m_proCon2ThresholdSlider, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND minLabel = CreateWindowExW(
        0, L"STATIC", L"0.00",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        18, 78, 48, 18,
        m_proCon2SettingsHwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(minLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND maxLabel = CreateWindowExW(
        0, L"STATIC", L"1.00",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        356, 78, 48, 18,
        m_proCon2SettingsHwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(maxLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND description = CreateWindowExW(
        0,
        L"STATIC",
        L"Lower values allow strong haptics to blend into more rumble.\r\n"
        L"The best value can vary by game. Too high or too low can screw up the haptic experience.\r\n"
        L"If unsure, use the default value.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        18, 104, 386, 54,
        m_proCon2SettingsHwnd,
        nullptr,
        m_hInstance,
        nullptr);
    SendMessageW(description, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    m_proCon2ThresholdReset = CreateWindowExW(
        0, L"BUTTON", L"Reset To Default",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        268, 166, 136, 28,
        m_proCon2SettingsHwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PROCON2_RUMBLE_THRESHOLD_RESET)),
        m_hInstance,
        nullptr);
    SendMessageW(m_proCon2ThresholdReset, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void TrayApp::RefreshProCon2SettingsWindow() {
    if (!m_proCon2SettingsHwnd)
        return;

    const bool enabled = m_controller->GetOutputMode() == VirtualControllerMode::Switch2Pro;
    EnableWindow(m_proCon2ThresholdSlider, enabled);
    EnableWindow(m_proCon2ThresholdReset, enabled);

    const double threshold = m_controller->GetSwitch2ProRumbleImpactThreshold();
    const int sliderPos = ThresholdToSliderPosition(threshold);
    if (m_proCon2ThresholdSlider)
        SendMessageW(m_proCon2ThresholdSlider, TBM_SETPOS, TRUE, sliderPos);

    if (m_proCon2ThresholdValue) {
        wchar_t text[32]{};
        swprintf_s(text, L"%.2f", SliderPositionToThreshold(sliderPos));
        SetWindowTextW(m_proCon2ThresholdValue, text);
    }
}

void TrayApp::OnProCon2RumbleThresholdChanged() {
    if (!m_proCon2ThresholdSlider ||
        m_controller->GetOutputMode() != VirtualControllerMode::Switch2Pro)
        return;

    const int sliderPos = static_cast<int>(SendMessageW(m_proCon2ThresholdSlider, TBM_GETPOS, 0, 0));
    m_controller->SetSwitch2ProRumbleImpactThreshold(SliderPositionToThreshold(sliderPos));
    RefreshProCon2SettingsWindow();
    SaveSettings();
}

void TrayApp::ResetProCon2RumbleThreshold() {
    if (m_controller->GetOutputMode() != VirtualControllerMode::Switch2Pro)
        return;

    m_controller->SetSwitch2ProRumbleImpactThreshold(
        SliderPositionToThreshold(PROCON2_RUMBLE_THRESHOLD_DEFAULT));
    RefreshProCon2SettingsWindow();
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
    const bool dpadLocksMouse = IsTrackpadDpadLockingOutputMode(outputMode) && trackpadDpadOn;

    HMENU menu = CreatePopupMenu();

    UINT toggleFlags = MF_STRING | (connected ? MF_ENABLED : MF_GRAYED);
    AppendMenuW(menu, toggleFlags, IDM_TOGGLE,
                gameModeOn ? L"Disable Steamless Mode" : L"Enable Steamless Mode");

    HMENU autoEnableMenu = CreatePopupMenu();
    UINT autoEnableFlags = MF_STRING | (m_autoEnable ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(autoEnableMenu, autoEnableFlags, IDM_AUTO_ENABLE, L"Auto Enable");
    UINT autoRestartFlags = MF_STRING
                          | (m_autoEnable ? MF_ENABLED : MF_GRAYED)
                          | (m_autoRestartSteam ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(autoEnableMenu, autoRestartFlags, IDM_AUTO_RESTART_STEAM, L"Auto Restart Steam");
    AppendMenuW(menu, MF_POPUP | MF_STRING,
                reinterpret_cast<UINT_PTR>(autoEnableMenu), L"Auto Enable Mode");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_RESTART_STEAM, L"Restart Steam");

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
    AppendMenuW(outputMenu,
                MF_STRING | (outputMode == VirtualControllerMode::DualSense ? MF_CHECKED : MF_UNCHECKED),
                IDM_OUTPUT_DSENSE, L"DualSense");
    AppendMenuW(outputMenu,
                MF_STRING | (outputMode == VirtualControllerMode::SwitchPro ? MF_CHECKED : MF_UNCHECKED),
                IDM_OUTPUT_SWITCHPRO, L"Switch Pro Controller");
    AppendMenuW(outputMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(outputMenu,
                MF_STRING | (outputMode == VirtualControllerMode::DualSense ? MF_ENABLED : MF_GRAYED),
                IDM_DUALSENSE_SETTINGS, L"DualSense Settings...");
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
    const std::wstring buildLabel = BuildMenuLabel();
    AppendMenuW(menu, MF_STRING, IDM_BUILD_INFO, buildLabel.c_str());
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    // SetForegroundWindow is required for the menu to dismiss on click-away.
    SetForegroundWindow(m_hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON,
                   pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}
