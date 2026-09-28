#pragma once

#include <windows.h>

namespace envlauncher {

class DwmHelper {
public:
    // Enables immersive dark mode on the window's title bar.
    // Safe to call on all versions of Windows; gracefully fails on older versions.
    static bool EnableDarkMode(HWND hwnd);

    // Enables rounded window corners on Windows 11 (DWMWCP_ROUND).
    static bool EnableRoundedCorners(HWND hwnd);

    // Sets custom caption (title bar) and text colors (Windows 11 build 22000+).
    // Fails gracefully on older Windows without side effects.
    static bool SetTitleBarColor(HWND hwnd, COLORREF captionColor, COLORREF textColor);
};

} // namespace envlauncher
