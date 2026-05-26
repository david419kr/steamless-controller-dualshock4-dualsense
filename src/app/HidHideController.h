#pragma once
#include <string>
#include <vector>

class HidHideController {
public:
    HidHideController();

    bool IsInstalled() const { return m_installed; }
    bool EnsureAppRegistered();
    bool HideSteamController();
    bool RevealSteamController();
    bool RevealSteamControllerNow();

private:
    bool LocateCli();
    bool CanOpenControlDevice() const;
    bool RunCli(const std::vector<std::wstring>& args, std::wstring* output = nullptr) const;
    bool QueryCloakState(bool& active) const;
    bool SetCloakState(bool active) const;
    std::vector<std::wstring> EnumerateSteamControllerInstanceIds() const;
    std::vector<std::wstring> QueryHiddenSteamControllerInstanceIds() const;

    std::wstring m_cliPath;
    bool m_installed = false;
    bool m_appRegistered = false;
    bool m_hidden = false;
    bool m_savedCloakState = false;
    bool m_previousCloakActive = false;
    std::vector<std::wstring> m_hiddenInstanceIds;
};
