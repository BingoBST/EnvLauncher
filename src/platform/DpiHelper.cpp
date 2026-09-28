#include "platform/DpiHelper.h"

namespace envlauncher {

typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);

void DpiHelper::InitializeDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto setDpiContext = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
        );
        if (setDpiContext) {
            setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }

    // Fallback for older Windows
    SetProcessDPIAware();
}

UINT DpiHelper::GetWindowDpi(HWND hwnd) {
    if (hwnd) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            auto getDpiWindow = reinterpret_cast<PFN_GetDpiForWindow>(
                reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow"))
            );
            if (getDpiWindow) {
                UINT dpi = getDpiWindow(hwnd);
                if (dpi > 0) {
                    return dpi;
                }
            }
        }

        HDC hdc = GetDC(hwnd);
        if (hdc) {
            int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(hwnd, hdc);
            if (dpiX > 0) {
                return static_cast<UINT>(dpiX);
            }
        }
    }
    return 96;
}

UINT DpiHelper::GetSystemDpi() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT(WINAPI* PFN_GetDpiForSystem)();
        auto getDpiSystem = reinterpret_cast<PFN_GetDpiForSystem>(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForSystem"))
        );
        if (getDpiSystem) {
            UINT dpi = getDpiSystem();
            if (dpi > 0) {
                return dpi;
            }
        }
    }

    HDC hdc = GetDC(nullptr);
    if (hdc) {
        int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        if (dpiX > 0) {
            return static_cast<UINT>(dpiX);
        }
    }
    return 96;
}

} // namespace envlauncher
