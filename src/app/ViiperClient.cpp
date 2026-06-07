#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include "ViiperClient.h"
#include "VirtualControllerTypes.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uintptr_t kInvalidSocketValue = ~static_cast<std::uintptr_t>(0);
constexpr uint16_t kViiperPort = 3242;

SOCKET ToSocket(std::uintptr_t value) {
    return static_cast<SOCKET>(value);
}

std::uintptr_t FromSocket(SOCKET value) {
    return static_cast<std::uintptr_t>(value);
}

bool EnsureWinsock() {
    static bool initialized = []() {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialized;
}

void CloseSocket(std::uintptr_t& socketValue) {
    if (socketValue == kInvalidSocketValue)
        return;
    SOCKET s = ToSocket(socketValue);
    shutdown(s, SD_BOTH);
    closesocket(s);
    socketValue = kInvalidSocketValue;
}

bool SendAll(SOCKET s, const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        const int chunk = static_cast<int>(std::min<size_t>(size - sent, 16 * 1024));
        const int n = send(s, reinterpret_cast<const char*>(data + sent), chunk, 0);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool SendAll(SOCKET s, const std::string& data) {
    return SendAll(s, reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

bool ReceiveUntilClose(SOCKET s, std::string& out) {
    out.clear();
    std::array<char, 4096> buffer{};
    for (;;) {
        const int n = recv(s, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (n > 0) {
            out.append(buffer.data(), static_cast<size_t>(n));
            continue;
        }
        if (n == 0)
            break;
        return false;
    }
    if (!out.empty() && out.back() == '\n')
        out.pop_back();
    return !out.empty();
}

bool ReceiveExact(SOCKET s, uint8_t* data, size_t size) {
    size_t received = 0;
    while (received < size) {
        const int chunk = static_cast<int>(std::min<size_t>(size - received, 4096));
        const int n = recv(s, reinterpret_cast<char*>(data + received), chunk, 0);
        if (n <= 0)
            return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

SOCKET ConnectLoopback(int timeoutMs) {
    if (!EnsureWinsock())
        return INVALID_SOCKET;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return INVALID_SOCKET;

    const int noDelay = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kViiperPort);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    return s;
}

std::wstring GetExecutableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len == 0 || len >= buffer.size())
        return {};

    std::wstring path(buffer.data(), len);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};
    return path.substr(0, slash);
}

bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring GetEnvironmentValue(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
        return {};

    std::vector<wchar_t> buffer(needed);
    const DWORD len = GetEnvironmentVariableW(name, buffer.data(), needed);
    if (len == 0 || len >= needed)
        return {};
    return std::wstring(buffer.data(), len);
}

std::wstring QuoteCommandLineArg(const std::wstring& arg) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(ch);
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(ch);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring BuildWritableViiperLogPath(const wchar_t* fileName) {
    std::wstring base = GetEnvironmentValue(L"LOCALAPPDATA");
    if (base.empty()) {
        std::vector<wchar_t> tempPath(MAX_PATH);
        const DWORD len = GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());
        if (len > 0 && len < tempPath.size())
            base.assign(tempPath.data(), len);
    }
    if (base.empty())
        return {};

    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
        base.pop_back();

    const std::wstring logDir = base + L"\\SteamlessController";
    if (!CreateDirectoryW(logDir.c_str(), nullptr)) {
        const DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
            return {};
    }
    return logDir + L"\\" + fileName;
}

std::wstring ErrorMessageFor(VirtualControllerError error) {
    switch (error) {
    case VirtualControllerError::ViiperUnavailable:
        return L"VIIPER server is not reachable.";
    case VirtualControllerError::ViiperExeMissing:
        return L"viiper.exe was not found next to SteamlessController.exe.";
    case VirtualControllerError::ViiperStartFailed:
        return L"Failed to start bundled viiper.exe.";
    case VirtualControllerError::ViiperUnsupported:
        return L"VIIPER server is not compatible. Use the SteamlessController patched sidecar.";
    case VirtualControllerError::UsbIpDriverMissing:
        return L"usbip-win2 driver is required for VIIPER virtual USB attachment on Windows.";
    case VirtualControllerError::BusCreateFailed:
        return L"Failed to create VIIPER virtual USB bus.";
    case VirtualControllerError::DeviceCreateFailed:
        return L"Failed to create VIIPER virtual controller device.";
    case VirtualControllerError::StreamConnectFailed:
        return L"Failed to connect VIIPER device stream.";
    case VirtualControllerError::StreamWriteFailed:
        return L"Failed to send controller state to VIIPER.";
    default:
        return {};
    }
}

size_t FindJsonValueStart(const std::string& json, const char* key) {
    const std::string token = std::string("\"") + key + "\"";
    size_t pos = json.find(token);
    if (pos == std::string::npos)
        return std::string::npos;
    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos)
        return std::string::npos;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                 json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }
    return pos;
}

bool FindJsonString(const std::string& json, const char* key, std::string& value) {
    size_t pos = FindJsonValueStart(json, key);
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '"')
        return false;
    ++pos;
    std::string out;
    bool escaped = false;
    for (; pos < json.size(); ++pos) {
        const char c = json[pos];
        if (escaped) {
            out.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            value = out;
            return true;
        }
        out.push_back(c);
    }
    return false;
}

bool FindJsonUint(const std::string& json, const char* key, uint32_t& value) {
    size_t pos = FindJsonValueStart(json, key);
    if (pos == std::string::npos || pos >= json.size())
        return false;
    uint64_t v = 0;
    bool foundDigit = false;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        foundDigit = true;
        v = v * 10 + static_cast<uint64_t>(json[pos] - '0');
        if (v > 0xFFFFFFFFULL)
            return false;
        ++pos;
    }
    if (!foundDigit)
        return false;
    value = static_cast<uint32_t>(v);
    return true;
}

bool ParseVersionParts(const std::string& version, int& major, int& minor, int& patch) {
    std::string v = version;
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V'))
        v.erase(v.begin());

    std::array<int, 3> parts{};
    size_t start = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (start >= v.size())
            return false;
        size_t end = v.find('.', start);
        if (i == parts.size() - 1)
            end = v.size();
        std::string token = v.substr(start, end - start);
        size_t digitEnd = 0;
        while (digitEnd < token.size() && token[digitEnd] >= '0' && token[digitEnd] <= '9')
            ++digitEnd;
        if (digitEnd == 0)
            return false;
        parts[i] = std::stoi(token.substr(0, digitEnd));
        if (i < parts.size() - 1) {
            if (end == std::string::npos)
                return false;
            start = end + 1;
        }
    }

    major = parts[0];
    minor = parts[1];
    patch = parts[2];
    return true;
}

} // namespace

std::string BuildViiperRequest(const std::string& path, const std::string& payload) {
    std::string request = path;
    if (!payload.empty()) {
        request.push_back(' ');
        request += payload;
    }
    request.push_back('\0');
    return request;
}

std::string BuildViiperStreamPath(uint32_t busId, const std::string& devId) {
    std::ostringstream stream;
    stream << "bus/" << busId << "/" << devId;
    std::string path = stream.str();
    path.push_back('\0');
    return path;
}

bool ParseViiperPingResponse(const std::string& json, std::string& server, std::string& version) {
    return FindJsonString(json, "server", server) &&
           FindJsonString(json, "version", version);
}

bool ParseViiperBusIdResponse(const std::string& json, uint32_t& busId) {
    return FindJsonUint(json, "busId", busId);
}

bool ParseViiperDeviceResponse(const std::string& json, uint32_t& busId, std::string& devId) {
    return FindJsonUint(json, "busId", busId) &&
           FindJsonString(json, "devId", devId);
}

bool IsViiperUsbIpDriverMissingResponse(const std::string& json) {
    std::string title;
    std::string detail;
    (void)FindJsonString(json, "title", title);
    (void)FindJsonString(json, "detail", detail);

    std::string text = title + " " + detail + " " + json;
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text.find("usbip-win2 driver not found") != std::string::npos ||
           text.find("native ioctl auto-attach requires the usbip-win2 driver") != std::string::npos ||
           text.find("failed to auto-attach device") != std::string::npos;
}

bool IsViiperVersionSupported(const std::string& version) {
    int major = 0;
    int minor = 0;
    int patch = 0;
    if (!ParseVersionParts(version, major, minor, patch))
        return false;
    if (major != 0)
        return major > 0;
    if (minor != 6)
        return minor > 6;
    return patch >= 1;
}

bool IsViiperDualShock4CompatibleVersion(const std::string& version) {
    int major = 0;
    int minor = 0;
    int patch = 0;
    if (!ParseVersionParts(version, major, minor, patch))
        return false;

    if (major != 0)
        return major > 0;
    if (minor != 6)
        return minor > 6;
    if (patch != 1)
        return patch > 1;

    std::string lower = version;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("steamless3") != std::string::npos ||
           lower.find("steamless4") != std::string::npos ||
           lower.find("steamless5") != std::string::npos ||
           lower.find("steamless6") != std::string::npos ||
           lower.find("steamless7") != std::string::npos;
}

bool IsViiperDualSenseCompatibleVersion(const std::string& version) {
    int major = 0;
    int minor = 0;
    int patch = 0;
    if (!ParseVersionParts(version, major, minor, patch))
        return false;

    if (major != 0)
        return major > 0;
    if (minor != 6)
        return minor > 6;
    if (patch != 1)
        return patch > 1;

    std::string lower = version;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("steamless5") != std::string::npos ||
           lower.find("steamless6") != std::string::npos ||
           lower.find("steamless7") != std::string::npos;
}

bool IsViiperSwitch2ProCompatibleVersion(const std::string& version) {
    int major = 0;
    int minor = 0;
    int patch = 0;
    if (!ParseVersionParts(version, major, minor, patch))
        return false;

    if (major != 0)
        return major > 0;
    if (minor != 6)
        return minor > 6;
    if (patch != 1)
        return patch > 1;

    std::string lower = version;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("steamless7") != std::string::npos;
}

ViiperClient::~ViiperClient() {
    Close();
}

bool ViiperClient::Open(VirtualControllerMode mode, FeedbackFn feedbackFn) {
    Close();
    m_error = VirtualControllerError::None;
    m_errorMessage.clear();
    m_feedbackFn = std::move(feedbackFn);

    if (!EnsureServerReady(mode))
        return false;
    if (!CreateBus())
        return false;
    if (!AddDevice(mode)) {
        RemoveDeviceAndBus();
        return false;
    }
    if (!OpenStream(mode)) {
        RemoveDeviceAndBus();
        return false;
    }

    m_open = true;
    return true;
}

void ViiperClient::Close() {
    m_feedbackRunning.store(false, std::memory_order_relaxed);
    CloseSocket(m_streamSocket);

    if (m_feedbackThread.joinable())
        m_feedbackThread.join();

    RemoveDeviceAndBus();

    if (m_spawnedServer && m_serverProcess) {
        HANDLE process = static_cast<HANDLE>(m_serverProcess);
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 2000);
        CloseHandle(process);
    }

    m_serverProcess = nullptr;
    m_spawnedServer = false;
    m_open = false;
    m_feedbackFn = {};
}

bool ViiperClient::SendInput(const uint8_t* data, size_t size) {
    if (!m_open || m_streamSocket == kInvalidSocketValue || !data || size == 0)
        return false;

    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (!SendAll(ToSocket(m_streamSocket), data, size)) {
        SetError(VirtualControllerError::StreamWriteFailed, nullptr);
        return false;
    }
    return true;
}

bool ViiperClient::EnsureServerReady(VirtualControllerMode mode) {
    std::string version;
    if (PingServer(mode, &version))
        return true;
    if (m_error == VirtualControllerError::ViiperUnsupported)
        return false;

    const std::wstring dir = GetExecutableDirectory();
    const std::wstring viiperPath = dir.empty() ? L"" : (dir + L"\\viiper.exe");
    if (viiperPath.empty() || !FileExists(viiperPath)) {
        SetError(VirtualControllerError::ViiperExeMissing, nullptr);
        return false;
    }

    if (!SpawnBundledServer())
        return false;

    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (PingServer(mode, &version))
            return true;
        if (m_error == VirtualControllerError::ViiperUnsupported)
            return false;
    }

    SetError(VirtualControllerError::ViiperStartFailed, nullptr);
    return false;
}

bool ViiperClient::PingServer(VirtualControllerMode mode, std::string* version) {
    std::string response;
    if (!Request("ping", {}, response)) {
        SetError(VirtualControllerError::ViiperUnavailable, nullptr);
        return false;
    }

    std::string server;
    std::string ver;
    if (!ParseViiperPingResponse(response, server, ver)) {
        SetError(VirtualControllerError::ViiperUnavailable, nullptr);
        return false;
    }
    if (server != "VIIPER") {
        SetError(VirtualControllerError::ViiperUnsupported, nullptr);
        return false;
    }
    if (!IsViiperVersionSupported(ver)) {
        SetError(VirtualControllerError::ViiperUnsupported, nullptr);
        return false;
    }
    if (mode == VirtualControllerMode::DualShock4 &&
        !IsViiperDualShock4CompatibleVersion(ver)) {
        SetError(VirtualControllerError::ViiperUnsupported, nullptr);
        return false;
    }
    if (mode == VirtualControllerMode::DualSense &&
        !IsViiperDualSenseCompatibleVersion(ver)) {
        SetError(VirtualControllerError::ViiperUnsupported, nullptr);
        return false;
    }
    if (mode == VirtualControllerMode::Switch2Pro &&
        !IsViiperSwitch2ProCompatibleVersion(ver)) {
        SetError(VirtualControllerError::ViiperUnsupported, nullptr);
        return false;
    }
    if (version)
        *version = ver;
    m_error = VirtualControllerError::None;
    m_errorMessage.clear();
    return true;
}

bool ViiperClient::SpawnBundledServer() {
    const std::wstring dir = GetExecutableDirectory();
    if (dir.empty()) {
        SetError(VirtualControllerError::ViiperStartFailed, nullptr);
        return false;
    }
    const std::wstring viiperPath = dir + L"\\viiper.exe";
    if (!FileExists(viiperPath)) {
        SetError(VirtualControllerError::ViiperExeMissing, nullptr);
        return false;
    }

    std::wstring command = QuoteCommandLineArg(viiperPath) + L" server --update-notify none";
    const std::wstring logPath = BuildWritableViiperLogPath(L"viiper.log");
    if (!logPath.empty())
        command += L" " + QuoteCommandLineArg(L"--log.file=" + logPath);
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};

    if (!CreateProcessW(viiperPath.c_str(),
                        commandLine.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        dir.c_str(),
                        &startup,
                        &process)) {
        SetError(VirtualControllerError::ViiperStartFailed, nullptr);
        return false;
    }

    CloseHandle(process.hThread);
    m_serverProcess = process.hProcess;
    m_spawnedServer = true;
    std::printf("[VIIPER] Started bundled server process\n");
    return true;
}

bool ViiperClient::Request(const std::string& path,
                           const std::string& payload,
                           std::string& response) {
    SOCKET s = ConnectLoopback(5000);
    if (s == INVALID_SOCKET)
        return false;

    const std::string request = BuildViiperRequest(path, payload);
    const bool sent = SendAll(s, request);
    bool received = false;
    if (sent)
        received = ReceiveUntilClose(s, response);
    closesocket(s);
    return sent && received;
}

bool ViiperClient::CreateBus() {
    std::string response;
    if (!Request("bus/create", "0", response)) {
        SetError(VirtualControllerError::BusCreateFailed, nullptr);
        return false;
    }
    uint32_t busId = 0;
    if (!ParseViiperBusIdResponse(response, busId) || busId == 0) {
        SetError(VirtualControllerError::BusCreateFailed, nullptr);
        return false;
    }
    m_busId = busId;
    return true;
}

bool ViiperClient::AddDevice(VirtualControllerMode mode) {
    const char* type = "xbox360";
    if (mode == VirtualControllerMode::DualShock4)
        type = "dualshock4";
    else if (mode == VirtualControllerMode::DualSense)
        type = "dualsense";
    else if (mode == VirtualControllerMode::Switch2Pro)
        type = "ns2pro";
    const std::string payload = std::string("{\"type\":\"") + type + "\"}";
    std::ostringstream path;
    path << "bus/" << m_busId << "/add";

    std::string response;
    if (!Request(path.str(), payload, response)) {
        SetError(VirtualControllerError::DeviceCreateFailed, nullptr);
        return false;
    }

    if (IsViiperUsbIpDriverMissingResponse(response)) {
        SetError(VirtualControllerError::UsbIpDriverMissing, nullptr);
        return false;
    }

    uint32_t busId = 0;
    std::string devId;
    if (!ParseViiperDeviceResponse(response, busId, devId) || busId != m_busId || devId.empty()) {
        SetError(VirtualControllerError::DeviceCreateFailed, nullptr);
        return false;
    }

    m_devId = std::move(devId);
    return true;
}

bool ViiperClient::OpenStream(VirtualControllerMode mode) {
    SOCKET s = ConnectLoopback(0);
    if (s == INVALID_SOCKET) {
        SetError(VirtualControllerError::StreamConnectFailed, nullptr);
        return false;
    }

    const std::string streamPath = BuildViiperStreamPath(m_busId, m_devId);
    if (!SendAll(s, streamPath)) {
        closesocket(s);
        SetError(VirtualControllerError::StreamConnectFailed, nullptr);
        return false;
    }

    m_streamSocket = FromSocket(s);
    m_feedbackRunning.store(true, std::memory_order_relaxed);
    m_feedbackThread = std::thread(&ViiperClient::FeedbackLoop, this, mode, m_streamSocket);
    return true;
}

void ViiperClient::RemoveDeviceAndBus() {
    if (m_busId != 0 && !m_devId.empty()) {
        std::ostringstream path;
        path << "bus/" << m_busId << "/remove";
        std::string response;
        (void)Request(path.str(), m_devId, response);
        m_devId.clear();
    }

    if (m_busId != 0) {
        std::string response;
        (void)Request("bus/remove", std::to_string(m_busId), response);
        m_busId = 0;
    }
}

void ViiperClient::FeedbackLoop(VirtualControllerMode mode, std::uintptr_t streamSocket) {
    size_t feedbackSize = 2u;
    if (mode == VirtualControllerMode::DualShock4)
        feedbackSize = 7u;
    else if (mode == VirtualControllerMode::Switch2Pro)
        feedbackSize = 34u;
    std::array<uint8_t, 64> buffer{};
    const SOCKET socket = ToSocket(streamSocket);

    while (m_feedbackRunning.load(std::memory_order_relaxed)) {
        if (mode == VirtualControllerMode::DualSense) {
            std::array<uint8_t, 3> header{};
            if (!ReceiveExact(socket, header.data(), header.size()))
                break;

            const uint8_t frameType = header[0];
            const uint16_t payloadSize = static_cast<uint16_t>(header[1]) |
                                         (static_cast<uint16_t>(header[2]) << 8);
            if (payloadSize == 0 || payloadSize > buffer.size())
                break;
            if (!ReceiveExact(socket, buffer.data(), payloadSize))
                break;

            ViiperFeedbackState feedback{};
            feedback.mode = mode;
            bool ok = false;
            if (frameType == 0x01) {
                ok = DecodeViiperDualSenseFeedback(buffer.data(), payloadSize,
                                                   feedback.dualSense);
                feedback.largeMotor = feedback.dualSense.LargeMotor();
                feedback.smallMotor = feedback.dualSense.SmallMotor();
            } else if (frameType == 0x02) {
                ok = DecodeViiperDualSenseAudioHaptics(buffer.data(), payloadSize,
                                                       feedback.dualSenseAudio);
                feedback.isDualSenseAudio = true;
            }
            if (ok && m_feedbackFn)
                m_feedbackFn(feedback);
            continue;
        }

        if (!ReceiveExact(socket, buffer.data(), feedbackSize))
            break;

        ViiperFeedbackState feedback{};
        feedback.mode = mode;
        bool ok = false;
        if (mode == VirtualControllerMode::DualShock4) {
            ok = DecodeViiperDualShock4Feedback(buffer.data(), feedbackSize,
                                                feedback.largeMotor,
                                                feedback.smallMotor);
        } else if (mode == VirtualControllerMode::Switch2Pro) {
            ok = DecodeViiperSwitch2ProFeedback(buffer.data(), feedbackSize,
                                                feedback.switch2Pro);
        } else {
            ok = DecodeViiperXbox360Feedback(buffer.data(), feedbackSize,
                                             feedback.largeMotor,
                                             feedback.smallMotor);
        }
        if (ok && m_feedbackFn)
            m_feedbackFn(feedback);
    }
}

void ViiperClient::SetError(VirtualControllerError error, const wchar_t* message) {
    m_error = error;
    m_errorMessage = message ? std::wstring(message) : ErrorMessageFor(error);
}
