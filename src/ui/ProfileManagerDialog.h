#pragma once

#include "config/ConfigManager.h"
#include <windows.h>

namespace envlauncher {

class ProfileManagerDialog {
public:
    // Displays the modal Environment Profiles manager dialog.
    // Automatically saves changes via configManager.
    // Returns true if any changes were made and saved.
    static bool Show(HWND parent, ConfigManager& configManager);
};

} // namespace envlauncher
