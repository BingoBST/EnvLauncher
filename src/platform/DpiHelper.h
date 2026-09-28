#pragma once

#include <windows.h>

namespace envlauncher {

class DpiHelper {
public:
    // Configures Per-Monitor v2 DPI awareness for the process.
    static void InitializeDpiAwareness();

    // Queries current DPI for the given window (defaults to 96 if unable to determine).
    static UINT GetWindowDpi(HWND hwnd);

    // Queries system/primary monitor DPI.
    static UINT GetSystemDpi();

    // Scales DIP (Device-Independent Pixels) to physical pixels given a DPI value.
    static float Scale(float dip, UINT dpi) noexcept {
        return dip * (static_cast<float>(dpi) / 96.0f);
    }

    // Scales a physical pixel dimension back to DIPs.
    static float Unscale(float pixels, UINT dpi) noexcept {
        return (dpi > 0) ? (pixels * (96.0f / static_cast<float>(dpi))) : pixels;
    }
};

} // namespace envlauncher
