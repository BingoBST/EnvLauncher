#pragma once

#include "ui/Renderer.h"
#include "ui/Layout.h"
#include "ui/Controls.h"

#include <windows.h>
#include <vector>
#include <memory>

#include "config/ConfigManager.h"
#include "app/ConfiguredAppLauncher.h"

namespace envlauncher {

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    // Registers window class and creates the Win32 window.
    bool Create(HINSTANCE hInstance, int nCmdShow);

    HWND GetHwnd() const noexcept { return hwnd_; }

    ConfigManager& GetConfigManager() noexcept { return configManager_; }
    const ConfigManager& GetConfigManager() const noexcept { return configManager_; }

    // Reloads applications from ConfigManager and updates layout/rendering
    void ReloadCardsFromConfig();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnResize(UINT width, UINT height);
    void OnDpiChanged(UINT newDpi, const RECT* newRect);
    void OnPaint();
    void OnMouseMove(int x, int y);
    void OnMouseLeave();
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnRButtonUp(int x, int y);
    void OnMouseWheel(short delta);

    void TriggerButtonClick(const ButtonControl& button);
    void LaunchAppById(std::wstring_view appId, AppLaunchMode mode);
    bool IsPointInScrollThumb(float dipX, float dipY) const;

    HWND hwnd_{nullptr};
    HINSTANCE hInstance_{nullptr};
    HICON hIconSmall_{nullptr};
    HICON hIconBig_{nullptr};

    Renderer renderer_;
    ConfigManager configManager_;

    // Layout and State
    float clientWidth_{820.0f};
    float clientHeight_{620.0f};
    float scrollOffset_{0.0f};
    float totalContentHeight_{0.0f};
    float maxScrollOffset_{0.0f};
    UINT dpi_{96};

    bool mouseTracking_{false};
    ButtonControl* pressedButton_{nullptr};

    bool isDraggingScrollbar_{false};
    float dragStartMouseY_{0.0f};
    float dragStartScrollOffset_{0.0f};
    std::wstring contextMenuAppId_;

    ButtonControl addButton_;
    ButtonControl manageEnvsButton_;
    std::vector<AppCardView> cards_;
};

} // namespace envlauncher
