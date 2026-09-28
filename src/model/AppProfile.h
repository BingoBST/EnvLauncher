#pragma once

#include "process/EnvironmentBuilder.h"
#include <string>

namespace envlauncher {

struct AppProfile {
    std::wstring id;
    std::wstring name;
    std::wstring executable;
    std::wstring arguments;
    std::wstring workingDirectory;
    std::wstring environmentProfileId;
    EnvMap environmentOverrides;
};

} // namespace envlauncher
