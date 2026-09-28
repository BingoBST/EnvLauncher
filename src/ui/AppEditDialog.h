#pragma once

#include "model/AppProfile.h"
#include "model/EnvironmentProfile.h"

#include <windows.h>
#include <vector>
#include <string>

namespace envlauncher {

class AppEditDialog {
public:
    // Displays the modal Add / Edit Application dialog.
    // Returns true if user clicked Save and data is valid, false if canceled.
    static bool Show(
        HWND parent,
        const std::vector<EnvironmentProfile>& profiles,
        AppProfile& appInOut,
        bool isNew
    );
};

} // namespace envlauncher
