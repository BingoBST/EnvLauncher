#pragma once

#include "process/EnvironmentBuilder.h"
#include <string>

namespace envlauncher {

struct EnvironmentProfile {
    std::wstring id;
    std::wstring name;
    EnvMap variables;
};

} // namespace envlauncher
