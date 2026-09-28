#pragma once

#include "process/EnvironmentBuilder.h"
#include <windows.h>
#include <string>

namespace envlauncher {

// Move-only RAII wrapper around Win32 HANDLE
class UniqueHandle {
public:
    UniqueHandle() noexcept : handle_(nullptr) {}
    explicit UniqueHandle(HANDLE h) noexcept : handle_(h) {}
    ~UniqueHandle() noexcept { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.handle_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    HANDLE get() const noexcept { return handle_; }
    bool isValid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    void reset(HANDLE h = nullptr) noexcept {
        if (isValid()) {
            CloseHandle(handle_);
        }
        handle_ = h;
    }

    HANDLE release() noexcept {
        HANDLE h = handle_;
        handle_ = nullptr;
        return h;
    }

private:
    HANDLE handle_{nullptr};
};

struct LaunchRequest {
    std::wstring executable;
    std::wstring arguments;
    std::wstring workingDirectory;
    EnvMap environment;
    bool inheritParentEnvironment{true};
};

struct LaunchResult {
    bool success{false};
    DWORD errorCode{0};
    std::wstring errorMessage;
    DWORD processId{0};
};

// Result structure for synchronous testing helper
struct LaunchSyncResult {
    bool success{false};
    DWORD errorCode{0};
    std::wstring errorMessage;
    DWORD processId{0};
    DWORD exitCode{0};
    bool timedOut{false};
};

struct ProcessCreationResult {
    bool success{false};
    DWORD errorCode{0};
    std::wstring errorMessage;
    DWORD processId{0};
    UniqueHandle process;
    UniqueHandle thread;
};

class ProcessLauncher {
public:
    // Asynchronously launches the requested process.
    // Process and thread handles are closed automatically on return.
    static LaunchResult LaunchProcess(const LaunchRequest& request);

    // Synchronously launches the process, waits for completion (up to timeoutMs),
    // and queries exitCode. Used strictly as a testing/verification helper.
    static LaunchSyncResult LaunchProcessSync(const LaunchRequest& request, DWORD timeoutMs = 5000);

    // Internal helper creating the process and exposing RAII handles before closing.
    static ProcessCreationResult CreateProcessInternal(const LaunchRequest& request);
};

} // namespace envlauncher
