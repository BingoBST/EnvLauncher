#include "platform/DwmHelper.h"
#include <dwmapi.h>

namespace envlauncher {

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

bool DwmHelper::EnableDarkMode(HWND hwnd) {
    if (!hwnd) return false;

    BOOL enable = TRUE;
    // Try standard Windows 10 2004+ / Windows 11 attribute first (20)
    HRESULT hr = DwmSetWindowAttribute(
        hwnd,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &enable,
        sizeof(enable)
    );

    if (FAILED(hr)) {
        // Fallback for older Windows 10 builds (19)
        hr = DwmSetWindowAttribute(
            hwnd,
            DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1,
            &enable,
            sizeof(enable)
        );
    }

    // Enable dark mode for Win32 menus and controls via uxtheme
    enum PreferredAppMode {
        Default = 0,
        AllowDark = 1,
        ForceDark = 2,
        ForceLight = 3,
        Max = 4
    };
    using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
    using fnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND, BOOL);
    using fnFlushMenuThemes = void(WINAPI*)();

    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (!hUxTheme) hUxTheme = LoadLibraryW(L"uxtheme.dll");
    if (hUxTheme) {
        auto setPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
        if (setPreferredAppMode) {
            setPreferredAppMode(ForceDark);
        }
        auto allowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
        if (allowDarkModeForWindow) {
            allowDarkModeForWindow(hwnd, TRUE);
        }
        auto flushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136)));
        if (flushMenuThemes) {
            flushMenuThemes();
        }
    }

    return SUCCEEDED(hr);
}

bool DwmHelper::EnableRoundedCorners(HWND hwnd) {
    if (!hwnd) return false;

    DWORD preference = DWMWCP_ROUND;
    HRESULT hr = DwmSetWindowAttribute(
        hwnd,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &preference,
        sizeof(preference)
    );

    return SUCCEEDED(hr);
}

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

bool DwmHelper::SetTitleBarColor(HWND hwnd, COLORREF captionColor, COLORREF textColor) {
    if (!hwnd) return false;

    HRESULT hr1 = DwmSetWindowAttribute(
        hwnd,
        DWMWA_CAPTION_COLOR,
        &captionColor,
        sizeof(captionColor)
    );

    HRESULT hr2 = DwmSetWindowAttribute(
        hwnd,
        DWMWA_TEXT_COLOR,
        &textColor,
        sizeof(textColor)
    );

    return SUCCEEDED(hr1) && SUCCEEDED(hr2);
}

} // namespace envlauncher
