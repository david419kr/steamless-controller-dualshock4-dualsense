#include "HidHideController.h"
#include "steam/SteamController.h"
#include <Windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <utility>

static std::wstring QuoteArg(const std::wstring& arg) {
    std::wstring quoted = L"\"";
    for (wchar_t ch : arg) {
        if (ch == L'\"')
            quoted += L'\\';
        quoted += ch;
    }
    quoted += L"\"";
    return quoted;
}

static std::wstring CurrentExePath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (len == path.size()) {
        path.resize(path.size() * 2);
        len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(len);
    return path;
}

static std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty())
        return {};

    int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (len == 0)
        len = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (len == 0)
        return {};

    std::wstring wide(static_cast<size_t>(len), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), len))
        MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), len);
    return wide;
}

static std::wstring ToUpper(std::wstring text) {
    for (wchar_t& ch : text)
        ch = static_cast<wchar_t>(std::towupper(ch));
    return text;
}

static bool IsSteamControllerInstanceId(const std::wstring& id) {
    std::wstring upper = ToUpper(id);
    return upper.find(L"VID_28DE") != std::wstring::npos
        && (upper.find(L"PID_1302") != std::wstring::npos
         || upper.find(L"PID_1304") != std::wstring::npos);
}

static void AddUnique(std::vector<std::wstring>& ids, std::wstring id) {
    if (std::find(ids.begin(), ids.end(), id) == ids.end())
        ids.push_back(std::move(id));
}

static std::vector<std::wstring> EnumeratePresentSteamControllerPnPInstanceIds() {
    HDEVINFO devInfo = SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE)
        return {};

    std::vector<std::wstring> ids;
    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA devInfoData{};
        devInfoData.cbSize = sizeof(devInfoData);
        if (!SetupDiEnumDeviceInfo(devInfo, i, &devInfoData))
            break;

        WCHAR instanceId[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(devInfoData.DevInst, instanceId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
            continue;

        std::wstring id(instanceId);
        if (IsSteamControllerInstanceId(id))
            AddUnique(ids, std::move(id));
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return ids;
}

HidHideController::HidHideController() {
    m_installed = LocateCli() && CanOpenControlDevice();
}

bool HidHideController::LocateCli() {
    const wchar_t* candidates[] = {
        L"C:\\Program Files\\Nefarius Software Solutions\\HidHide\\x64\\HidHideCLI.exe",
        L"C:\\Program Files\\Nefarius Software Solutions\\HidHide\\HidHideCLI.exe",
        L"C:\\Program Files\\Nefarius Software Solutions e.U\\HidHide\\x64\\HidHideCLI.exe",
        L"C:\\Program Files\\Nefarius Software Solutions e.U\\HidHide\\HidHideCLI.exe",
    };

    for (const wchar_t* candidate : candidates) {
        DWORD attrs = GetFileAttributesW(candidate);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            m_cliPath = candidate;
            return true;
        }
    }

    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"HidHideCLI.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
        m_cliPath = found;
        return true;
    }

    return false;
}

bool HidHideController::CanOpenControlDevice() const {
    HANDLE h = CreateFileW(L"\\\\.\\HidHide",
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(h);
    return true;
}

bool HidHideController::RunCli(const std::vector<std::wstring>& args, std::wstring* output) const {
    if (m_cliPath.empty())
        return false;

    std::wstring command = QuoteArg(m_cliPath);
    for (const auto& arg : args) {
        command += L' ';
        command += QuoteArg(arg);
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);

    if (!ok) {
        CloseHandle(readPipe);
        return false;
    }

    std::string bytes;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        bytes.append(buffer, buffer + read);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);

    if (output)
        *output = Utf8ToWide(bytes);

    if (exitCode != 0)
        printf("[HidHide] CLI failed with exit code %lu\n", exitCode);
    return exitCode == 0;
}

bool HidHideController::EnsureAppRegistered() {
    if (!m_installed)
        return false;
    if (m_appRegistered)
        return true;

    m_appRegistered = RunCli({L"--app-reg", CurrentExePath()});
    return m_appRegistered;
}

bool HidHideController::QueryCloakState(bool& active) const {
    std::wstring output;
    if (!RunCli({L"--cloak-state"}, &output))
        return false;
    active = output.find(L"--cloak-on") != std::wstring::npos;
    return true;
}

bool HidHideController::SetCloakState(bool active) const {
    return RunCli({active ? L"--cloak-on" : L"--cloak-off"});
}

std::vector<std::wstring> HidHideController::EnumerateSteamControllerInstanceIds() const {
    std::vector<std::wstring> ids = EnumeratePresentSteamControllerPnPInstanceIds();

    std::vector<std::wstring> wired = HidDevice::EnumerateInstanceIds(SteamController::VALVE_VID,
                                                                      SteamController::SC2026_PID);
    for (auto& id : wired)
        AddUnique(ids, std::move(id));

    std::vector<std::wstring> dongle = HidDevice::EnumerateInstanceIds(SteamController::VALVE_VID,
                                                                       SteamController::SC2026_DONGLE_PID);
    for (auto& id : dongle)
        AddUnique(ids, std::move(id));

    return ids;
}

std::vector<std::wstring> HidHideController::QueryHiddenSteamControllerInstanceIds() const {
    std::wstring output;
    if (!RunCli({L"--dev-list"}, &output))
        return {};

    std::vector<std::wstring> ids;
    size_t pos = 0;
    while (pos < output.size()) {
        size_t start = output.find(L'\"', pos);
        if (start == std::wstring::npos)
            break;
        size_t end = output.find(L'\"', start + 1);
        if (end == std::wstring::npos)
            break;

        std::wstring id = output.substr(start + 1, end - start - 1);
        if (IsSteamControllerInstanceId(id)
            && std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(std::move(id));
        }
        pos = end + 1;
    }

    return ids;
}

bool HidHideController::HideSteamController() {
    if (!m_installed)
        return false;
    if (!EnsureAppRegistered())
        return false;

    bool cloakActive = false;
    if (!m_savedCloakState) {
        if (!QueryCloakState(cloakActive))
            return false;
        m_previousCloakActive = cloakActive;
        m_savedCloakState = true;
    }

    auto ids = EnumerateSteamControllerInstanceIds();
    if (ids.empty()) {
        printf("[HidHide] no Steam Controller HID instance IDs found\n");
        return false;
    }

    std::vector<std::wstring> hiddenIds;
    for (const auto& id : ids) {
        if (RunCli({L"--dev-hide", id}))
            hiddenIds.push_back(id);
        else
            wprintf(L"[HidHide] failed to hide %s\n", id.c_str());
    }

    if (hiddenIds.empty())
        return false;

    if (!SetCloakState(true))
        return false;

    m_hiddenInstanceIds = std::move(hiddenIds);
    m_hidden = true;
    return true;
}

bool HidHideController::RevealSteamController() {
    if (!m_installed)
        return true;

    std::vector<std::wstring> ids = m_hiddenInstanceIds;
    auto currentIds = EnumerateSteamControllerInstanceIds();
    for (auto& id : currentIds) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(std::move(id));
    }
    auto configuredIds = QueryHiddenSteamControllerInstanceIds();
    for (auto& id : configuredIds) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(std::move(id));
    }

    for (const auto& id : ids)
        RunCli({L"--dev-unhide", id});

    if (m_savedCloakState && !m_previousCloakActive)
        SetCloakState(false);

    m_hiddenInstanceIds.clear();
    m_hidden = false;
    m_savedCloakState = false;
    m_previousCloakActive = false;
    return true;
}

bool HidHideController::RevealSteamControllerNow() {
    if (!m_installed)
        return false;
    EnsureAppRegistered();
    return RevealSteamController();
}
