#pragma once

#include <windows.h>

namespace envlauncher {

class IconHelper {
public:
    // Generates a 32-bit ARGB custom application icon with anti-aliasing.
    // Caller is responsible for calling DestroyIcon(hIcon) when finished.
    static HICON CreateAppIcon(int size);
};

} // namespace envlauncher
