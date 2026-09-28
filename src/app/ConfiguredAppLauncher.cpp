#include "app/ConfiguredAppLauncher.h"

namespace envlauncher {

static bool PrepareLaunchRequest(
    const ConfigModel& config,
    std::wstring_view appId,
    AppLaunchMode mode,
    LaunchRequest& outRequest,
    std::wstring& outError
) {
    const AppProfile* app = config.FindAppById(appId);
    if (!app) {
        outError = L"Application not found with ID: " + std::wstring(appId);
        return false;
    }

    outRequest.executable = app->executable;
    outRequest.arguments = app->arguments;
    outRequest.workingDirectory = app->workingDirectory;
    outRequest.inheritParentEnvironment = true;

    if (mode == AppLaunchMode::Normal) {
        EnvMap combinedOverrides;
        if (!app->environmentProfileId.empty()) {
            const EnvironmentProfile* profile = config.FindProfileById(app->environmentProfileId);
            if (profile) {
                combinedOverrides = profile->variables;
            }
        }
        for (const auto& [k, v] : app->environmentOverrides) {
            combinedOverrides[k] = v;
        }
        outRequest.environment = std::move(combinedOverrides);
    } else {
        outRequest.environment.clear();
    }

    return true;
}

LaunchResult ConfiguredAppLauncher::LaunchApp(
    const ConfigModel& config,
    std::wstring_view appId,
    AppLaunchMode mode
) {
    LaunchRequest req;
    std::wstring error;
    if (!PrepareLaunchRequest(config, appId, mode, req, error)) {
        return LaunchResult{
            .success = false,
            .errorCode = ERROR_NOT_FOUND,
            .errorMessage = std::move(error),
            .processId = 0
        };
    }
    return ProcessLauncher::LaunchProcess(req);
}

LaunchSyncResult ConfiguredAppLauncher::LaunchAppSync(
    const ConfigModel& config,
    std::wstring_view appId,
    DWORD timeoutMs,
    AppLaunchMode mode
) {
    LaunchRequest req;
    std::wstring error;
    if (!PrepareLaunchRequest(config, appId, mode, req, error)) {
        return LaunchSyncResult{
            .success = false,
            .errorCode = ERROR_NOT_FOUND,
            .errorMessage = std::move(error),
            .processId = 0,
            .exitCode = 0,
            .timedOut = false
        };
    }
    return ProcessLauncher::LaunchProcessSync(req, timeoutMs);
}

} // namespace envlauncher
