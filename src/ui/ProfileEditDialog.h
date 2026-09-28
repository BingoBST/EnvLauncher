#pragma once

#include "model/EnvironmentProfile.h"
#include <windows.h>
#include <string>

namespace envlauncher {

class ProfileEditDialog {
public:
    // Displays modal dialog to Add or Edit an EnvironmentProfile.
    // Returns true if saved, false if canceled.
    static bool Show(
        HWND parent,
        EnvironmentProfile& profileInOut,
        bool isNew
    );
};

} // namespace envlauncher
