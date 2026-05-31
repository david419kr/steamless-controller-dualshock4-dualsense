#pragma once

#include "VirtualControllerTypes.h"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

std::string BuildViiperRequest(const std::string& path, const std::string& payload = {});
std::string BuildViiperStreamPath(uint32_t busId, const std::string& devId);
bool ParseViiperPingResponse(const std::string& json, std::string& server, std::string& version);
bool ParseViiperBusIdResponse(const std::string& json, uint32_t& busId);
bool ParseViiperDeviceResponse(const std::string& json, uint32_t& busId, std::string& devId);
bool IsViiperUsbIpDriverMissingResponse(const std::string& json);
bool IsViiperVersionSupported(const std::string& version);
bool IsViiperDualShock4CompatibleVersion(const std::string& version);
bool IsViiperDualSenseCompatibleVersion(const std::string& version);

class ViiperClient {
public:
    using FeedbackFn = std::function<void(const ViiperFeedbackState& feedback)>;

    ViiperClient() = default;
    ~ViiperClient();

    ViiperClient(const ViiperClient&) = delete;
    ViiperClient& operator=(const ViiperClient&) = delete;

    bool Open(VirtualControllerMode mode, FeedbackFn feedbackFn);
    void Close();
    bool SendInput(const uint8_t* data, size_t size);

    VirtualControllerError Error() const { return m_error; }
    std::wstring ErrorMessage() const { return m_errorMessage; }

private:
    bool EnsureServerReady(VirtualControllerMode mode);
    bool PingServer(VirtualControllerMode mode, std::string* version = nullptr);
    bool SpawnBundledServer();
    bool Request(const std::string& path, const std::string& payload, std::string& response);
    bool CreateBus();
    bool AddDevice(VirtualControllerMode mode);
    bool OpenStream(VirtualControllerMode mode);
    void RemoveDeviceAndBus();
    void FeedbackLoop(VirtualControllerMode mode, std::uintptr_t streamSocket);
    void SetError(VirtualControllerError error, const wchar_t* message);

    std::uintptr_t m_streamSocket = ~static_cast<std::uintptr_t>(0);
    uint32_t m_busId = 0;
    std::string m_devId;
    FeedbackFn m_feedbackFn;
    VirtualControllerError m_error = VirtualControllerError::None;
    std::wstring m_errorMessage;
    void* m_serverProcess = nullptr;
    bool m_spawnedServer = false;
    bool m_open = false;
    std::atomic<bool> m_feedbackRunning{false};
    std::mutex m_sendMutex;
    std::thread m_feedbackThread;
};
