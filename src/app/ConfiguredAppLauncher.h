#pragma once

#include "model/ConfigModel.h"
#include "process/ProcessLauncher.h"
#include <string_view>

namespace envlauncher {

enum class AppLaunchMode {
    Normal,               // Merges parent environment + profile + app overrides
    ParentEnvironmentOnly // Uses parent environment only, bypassing profile and overrides
};

class ConfiguredAppLauncher {
public:
    // Asynchronously launches an application configured in ConfigModel by ID.
    static LaunchResult LaunchApp(
        const ConfigModel& config,
        std::wstring_view appId,
        AppLaunchMode mode = AppLaunchMode::Normal
    );

    // Synchronously launches an application configured in ConfigModel by ID and waits for termination.
    // Testing helper.
    static LaunchSyncResult LaunchAppSync(
        const ConfigModel& config,
        std::wstring_view appId,
        DWORD timeoutMs = 5000,
        AppLaunchMode mode = AppLaunchMode::Normal
    );
};

} // namespace envlauncher
