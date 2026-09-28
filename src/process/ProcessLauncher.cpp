#include "process/ProcessLauncher.h"
#include "util/Win32Error.h"
#include <filesystem>

namespace envlauncher {

ProcessCreationResult ProcessLauncher::CreateProcessInternal(const LaunchRequest& request) {
    // 1. Build command line
    // Ensure executable is properly quoted
    std::wstring quotedExe = request.executable;
    if (quotedExe.empty() || quotedExe.front() != L'"') {
        quotedExe = L"\"" + quotedExe + L"\"";
    }

    std::wstring commandLine = quotedExe;
    if (!request.arguments.empty()) {
        commandLine += L" ";
        commandLine += request.arguments;
    }

    // 2. Determine working directory
    std::wstring workingDirStr;
    const wchar_t* workingDirPtr = nullptr;
    if (!request.workingDirectory.empty()) {
        workingDirStr = request.workingDirectory;
        workingDirPtr = workingDirStr.c_str();
    } else if (!request.executable.empty()) {
        std::error_code ec;
        std::filesystem::path p(request.executable);
        std::filesystem::path parent = p.parent_path();
        if (!parent.empty()) {
            workingDirStr = parent.wstring();
            workingDirPtr = workingDirStr.c_str();
        }
        // If parent is empty, workingDirPtr remains nullptr (inherits parent directory)
    }

    // 3. Build environment block
    std::vector<wchar_t> envBlock;
    if (request.inheritParentEnvironment) {
        EnvironmentSnapshot parentSnapshot = EnvironmentBuilder::CaptureCurrentEnvironment();
        EnvironmentSnapshot mergedSnapshot = EnvironmentBuilder::MergeEnvironment(parentSnapshot, nullptr, request.environment);
        envBlock = EnvironmentBuilder::BuildEnvironmentBlock(mergedSnapshot);
    } else {
        envBlock = EnvironmentBuilder::BuildEnvironmentBlock(request.environment);
    }

    // 4. CreateProcessW
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // Note: commandLine.data() is a non-const wchar_t* in C++17/C++20 as required by CreateProcessW
    BOOL ok = CreateProcessW(
        request.executable.empty() ? nullptr : request.executable.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT,
        envBlock.data(),
        workingDirPtr,
        &si,
        &pi
    );

    if (!ok) {
        DWORD err = GetLastError();
        return ProcessCreationResult{
            .success = false,
            .errorCode = err,
            .errorMessage = Win32Error::Format(err),
            .processId = 0,
            .process = UniqueHandle(),
            .thread = UniqueHandle()
        };
    }

    return ProcessCreationResult{
        .success = true,
        .errorCode = 0,
        .errorMessage = L"",
        .processId = pi.dwProcessId,
        .process = UniqueHandle(pi.hProcess),
        .thread = UniqueHandle(pi.hThread)
    };
}

LaunchResult ProcessLauncher::LaunchProcess(const LaunchRequest& request) {
    auto res = CreateProcessInternal(request);
    return LaunchResult{
        .success = res.success,
        .errorCode = res.errorCode,
        .errorMessage = std::move(res.errorMessage),
        .processId = res.processId
    };
}

LaunchSyncResult ProcessLauncher::LaunchProcessSync(const LaunchRequest& request, DWORD timeoutMs) {
    auto res = CreateProcessInternal(request);
    if (!res.success) {
        return LaunchSyncResult{
            .success = false,
            .errorCode = res.errorCode,
            .errorMessage = std::move(res.errorMessage),
            .processId = 0,
            .exitCode = 0,
            .timedOut = false
        };
    }

    DWORD waitRes = WaitForSingleObject(res.process.get(), timeoutMs);
    if (waitRes == WAIT_TIMEOUT) {
        // Explicitly terminate the timed-out process to avoid leaking background tasks
        TerminateProcess(res.process.get(), 1);
        return LaunchSyncResult{
            .success = false,
            .errorCode = WAIT_TIMEOUT,
            .errorMessage = L"Process execution timed out",
            .processId = res.processId,
            .exitCode = STILL_ACTIVE,
            .timedOut = true
        };
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(res.process.get(), &exitCode);

    return LaunchSyncResult{
        .success = true,
        .errorCode = 0,
        .errorMessage = L"",
        .processId = res.processId,
        .exitCode = exitCode,
        .timedOut = false
    };
}

} // namespace envlauncher
