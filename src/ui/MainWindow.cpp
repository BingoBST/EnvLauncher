#include "ui/MainWindow.h"
#include "ui/AppEditDialog.h"
#include "ui/ProfileManagerDialog.h"
#include "platform/DpiHelper.h"
#include "platform/DwmHelper.h"
#include "platform/IconHelper.h"
#include <windowsx.h>
#include <algorithm>

namespace envlauncher {

static const wchar_t* const kWindowClassName = L"EnvLauncherMainWindowClass";

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (hIconSmall_) {
        DestroyIcon(hIconSmall_);
        hIconSmall_ = nullptr;
    }
    if (hIconBig_) {
        DestroyIcon(hIconBig_);
        hIconBig_ = nullptr;
    }
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    hInstance_ = hInstance;

    // Create crisp custom application icons (small for title bar, big for taskbar/Alt+Tab)
    int smIconSize = GetSystemMetrics(SM_CXSMICON);
    int bgIconSize = GetSystemMetrics(SM_CXICON);
    hIconSmall_ = IconHelper::CreateAppIcon(smIconSize > 0 ? smIconSize : 16);
    hIconBig_ = IconHelper::CreateAppIcon(bgIconSize > 0 ? bgIconSize : 32);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = hIconBig_;
    wc.hIconSm = hIconSmall_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Direct2D handles all background rendering
    wc.lpszClassName = kWindowClassName;

    RegisterClassExW(&wc);

    dpi_ = DpiHelper::GetSystemDpi();

    // Default window dimensions: 840 x 640 DIPs
    int initialWidth = static_cast<int>(DpiHelper::Scale(840.0f, dpi_));
    int initialHeight = static_cast<int>(DpiHelper::Scale(640.0f, dpi_));

    hwnd_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"EnvLauncher",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        initialWidth,
        initialHeight,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!hwnd_) {
        return false;
    }

    if (hIconSmall_) {
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall_));
    }
    if (hIconBig_) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig_));
    }

    // Apply DPI and DWM Attributes
    dpi_ = DpiHelper::GetWindowDpi(hwnd_);
    DwmHelper::EnableDarkMode(hwnd_);
    DwmHelper::EnableRoundedCorners(hwnd_);
    // Seamless immersive title bar (#121214 matches client area background)
    DwmHelper::SetTitleBarColor(hwnd_, RGB(18, 18, 20), RGB(234, 234, 235));

    // Initialize Renderer
    if (!renderer_.Initialize()) {
        return false;
    }

    if (!renderer_.CreateDeviceResources(hwnd_, dpi_)) {
        return false;
    }

    // Load configuration from %LOCALAPPDATA%\EnvLauncher\config.json
    ConfigResult loadRes = configManager_.Load();
    if (!loadRes.success) {
        std::wstring warn = L"Failed to load configuration:\n" + loadRes.errorMessage +
                            L"\nUsing default empty configuration.";
        MessageBoxW(hwnd_, warn.c_str(), L"EnvLauncher - Config Warning", MB_OK | MB_ICONWARNING);
    }

    ReloadCardsFromConfig();

    RECT rc;
    GetClientRect(hwnd_, &rc);
    OnResize(rc.right - rc.left, rc.bottom - rc.top);

    int show = (nCmdShow == SW_HIDE || nCmdShow == 0) ? SW_SHOWNORMAL : nCmdShow;
    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);

    return true;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = static_cast<LONG>(DpiHelper::Scale(480.0f, dpi_));
            mmi->ptMinTrackSize.y = static_cast<LONG>(DpiHelper::Scale(360.0f, dpi_));
            return 0;
        }

        case WM_SIZE: {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            OnResize(width, height);
            return 0;
        }

        case WM_DPICHANGED: {
            UINT newDpi = HIWORD(wParam);
            const RECT* newRect = reinterpret_cast<RECT*>(lParam);
            OnDpiChanged(newDpi, newRect);
            return 0;
        }

        case WM_PAINT: {
            OnPaint();
            ValidateRect(hwnd_, nullptr);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1; // Suppress flicker

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnMouseMove(x, y);
            return 0;
        }

        case WM_MOUSELEAVE: {
            OnMouseLeave();
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnLButtonDown(x, y);
            return 0;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnLButtonUp(x, y);
            return 0;
        }

        case WM_RBUTTONUP: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnRButtonUp(x, y);
            return 0;
        }

        case WM_COMMAND: {
            WORD cmdId = LOWORD(wParam);
            if (!contextMenuAppId_.empty()) {
                std::wstring appId = contextMenuAppId_;
                contextMenuAppId_.clear();
                if (cmdId == 2001) { // Launch
                    LaunchAppById(appId, AppLaunchMode::Normal);
                } else if (cmdId == 2002) { // Launch without profile
                    LaunchAppById(appId, AppLaunchMode::ParentEnvironmentOnly);
                } else if (cmdId == 2003) { // Edit
                    auto* app = configManager_.GetConfig().FindAppById(appId);
                    if (app) {
                        AppProfile editedApp = *app;
                        if (AppEditDialog::Show(hwnd_, configManager_.GetConfig().environmentProfiles, editedApp, false)) {
                            *app = std::move(editedApp);
                            configManager_.Save();
                            ReloadCardsFromConfig();
                        }
                    }
                } else if (cmdId == 2004) { // Delete
                    auto* app = configManager_.GetConfig().FindAppById(appId);
                    if (app) {
                        std::wstring msg = L"Delete \"" + app->name + L"\" from EnvLauncher?\n\nThis will remove the application configuration. It will not uninstall or delete the executable file.";
                        int ans = MessageBoxW(hwnd_, msg.c_str(), L"Delete Application", MB_YESNO | MB_ICONQUESTION);
                        if (ans == IDYES) {
                            auto& apps = configManager_.GetConfig().applications;
                            apps.erase(
                                std::remove_if(apps.begin(), apps.end(), [&](const AppProfile& a) { return a.id == appId; }),
                                apps.end()
                            );
                            configManager_.Save();
                            ReloadCardsFromConfig();
                        }
                    }
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            OnMouseWheel(delta);
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

void MainWindow::ReloadCardsFromConfig() {
    cards_.clear();

    const auto& config = configManager_.GetConfig();

    for (const auto& app : config.applications) {
        AppCardView card;
        card.id = app.id;
        card.name = app.name;
        card.executablePath = app.executable;

        const EnvironmentProfile* prof = config.FindProfileById(app.environmentProfileId);
        if (prof) {
            std::wstring envDesc = prof->name;
            if (!prof->variables.empty()) {
                envDesc += L" (";
                size_t count = 0;
                for (const auto& [k, v] : prof->variables) {
                    if (count > 0) envDesc += L", ";
                    envDesc += k;
                    if (++count >= 2 && prof->variables.size() > 2) {
                        envDesc += L", ...";
                        break;
                    }
                }
                envDesc += L")";
            }
            card.environmentName = std::move(envDesc);
        } else {
            card.environmentName = L"(None)";
        }

        std::error_code ec;
        card.executableExists = !app.executable.empty() && std::filesystem::exists(app.executable, ec);

        cards_.push_back(std::move(card));
    }

    Layout::CalculateLayout(
        clientWidth_,
        clientHeight_,
        scrollOffset_,
        addButton_,
        cards_,
        manageEnvsButton_,
        totalContentHeight_,
        maxScrollOffset_
    );

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnResize(UINT width, UINT height) {
    clientWidth_ = DpiHelper::Unscale(static_cast<float>(width), dpi_);
    clientHeight_ = DpiHelper::Unscale(static_cast<float>(height), dpi_);

    renderer_.Resize(width, height);

    Layout::CalculateLayout(
        clientWidth_,
        clientHeight_,
        scrollOffset_,
        addButton_,
        cards_,
        manageEnvsButton_,
        totalContentHeight_,
        maxScrollOffset_
    );

    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnDpiChanged(UINT newDpi, const RECT* newRect) {
    dpi_ = newDpi;
    renderer_.SetDpi(newDpi);
    if (newRect) {
        SetWindowPos(
            hwnd_,
            nullptr,
            newRect->left,
            newRect->top,
            newRect->right - newRect->left,
            newRect->bottom - newRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE
        );
    }
}

void MainWindow::OnPaint() {
    if (!renderer_.CreateDeviceResources(hwnd_, dpi_)) {
        return;
    }

    if (!renderer_.BeginDraw()) {
        return;
    }

    // Direct2D internally scales DIP coordinates to device pixels based on render target DPI.
    // Use Identity transform so all DIP coordinates map 1:1 without double-scaling.
    renderer_.GetRenderTarget()->SetTransform(D2D1::Matrix3x2F::Identity());

    renderer_.ClearBackground();

    const float headerHeight = 60.0f;

    if (cards_.empty()) {
        renderer_.DrawEmptyState(clientWidth_, headerHeight, clientHeight_);
    } else {
        // 1. Draw Application Cards
        // Clip to content region below header bar so cards slide cleanly underneath when scrolled
        renderer_.GetRenderTarget()->PushAxisAlignedClip(
            D2D1::RectF(0.0f, headerHeight, clientWidth_, clientHeight_),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
        );

        for (const auto& card : cards_) {
            if (card.bounds.bottom >= headerHeight && card.bounds.top <= clientHeight_) {
                renderer_.DrawCard(card);
            }
        }

        renderer_.GetRenderTarget()->PopAxisAlignedClip();
    }

    if (manageEnvsButton_.bounds.bottom >= headerHeight && manageEnvsButton_.bounds.top <= clientHeight_) {
        renderer_.DrawButton(manageEnvsButton_);
    }

    // 2. Draw Sticky Top App Bar over scrolled cards
    renderer_.DrawHeaderBar(L"Applications", clientWidth_, headerHeight);
    renderer_.DrawButton(addButton_);

    // 3. Draw ScrollBar
    renderer_.DrawScrollBar(
        clientWidth_,
        headerHeight,
        clientHeight_,
        totalContentHeight_,
        scrollOffset_,
        isDraggingScrollbar_
    );

    HRESULT hr = renderer_.EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        renderer_.DiscardDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnMouseMove(int x, int y) {
    if (!mouseTracking_) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd_;
        TrackMouseEvent(&tme);
        mouseTracking_ = true;
    }

    float dipX = DpiHelper::Unscale(static_cast<float>(x), dpi_);
    float dipY = DpiHelper::Unscale(static_cast<float>(y), dpi_);

    if (isDraggingScrollbar_) {
        float deltaY = dipY - dragStartMouseY_;
        const float headerHeight = 60.0f;
        float viewportHeight = clientHeight_ - headerHeight;
        float trackHeight = clientHeight_ - headerHeight;
        float thumbHeight = std::max(24.0f, trackHeight * (viewportHeight / totalContentHeight_));
        float maxThumbTravel = trackHeight - thumbHeight;
        if (maxThumbTravel > 0.0f && maxScrollOffset_ > 0.0f) {
            float deltaScroll = (deltaY / maxThumbTravel) * maxScrollOffset_;
            float newScroll = std::clamp(dragStartScrollOffset_ + deltaScroll, 0.0f, maxScrollOffset_);
            if (newScroll != scrollOffset_) {
                scrollOffset_ = newScroll;
                Layout::CalculateLayout(
                    clientWidth_,
                    clientHeight_,
                    scrollOffset_,
                    addButton_,
                    cards_,
                    manageEnvsButton_,
                    totalContentHeight_,
                    maxScrollOffset_
                );
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
        return;
    }

    bool needRepaint = false;

    // Check Add button (always accessible in header)
    bool addHover = addButton_.HitTest(dipX, dipY);
    InteractionState newAddState = addHover ? (pressedButton_ == &addButton_ ? InteractionState::Pressed : InteractionState::Hover)
                                           : InteractionState::Normal;
    if (addButton_.state != newAddState) {
        addButton_.state = newAddState;
        needRepaint = true;
    }

    // Check Manage Environments button
    bool manageHover = (dipY >= 60.0f) && manageEnvsButton_.HitTest(dipX, dipY);
    InteractionState newManageState = manageHover ? (pressedButton_ == &manageEnvsButton_ ? InteractionState::Pressed : InteractionState::Hover)
                                                 : InteractionState::Normal;
    if (manageEnvsButton_.state != newManageState) {
        manageEnvsButton_.state = newManageState;
        needRepaint = true;
    }

    // Check Cards and Action buttons (Launch, Edit, Delete) (only when cursor is below header bar)
    for (auto& card : cards_) {
        bool cardHover = (dipY >= 60.0f) && card.HitTest(dipX, dipY);
        if (card.isHovered != cardHover) {
            card.isHovered = cardHover;
            needRepaint = true;
        }

        // Launch button
        if (card.executableExists) {
            bool btnHover = (dipY >= 60.0f) && card.launchButton.HitTest(dipX, dipY);
            InteractionState newBtnState = btnHover ? (pressedButton_ == &card.launchButton ? InteractionState::Pressed : InteractionState::Hover)
                                                   : InteractionState::Normal;
            if (card.launchButton.state != newBtnState) {
                card.launchButton.state = newBtnState;
                needRepaint = true;
            }
        }

        // Edit button
        bool editHover = (dipY >= 60.0f) && card.editButton.HitTest(dipX, dipY);
        InteractionState newEditState = editHover ? (pressedButton_ == &card.editButton ? InteractionState::Pressed : InteractionState::Hover)
                                                 : InteractionState::Normal;
        if (card.editButton.state != newEditState) {
            card.editButton.state = newEditState;
            needRepaint = true;
        }

        // Delete button
        bool delHover = (dipY >= 60.0f) && card.deleteButton.HitTest(dipX, dipY);
        InteractionState newDelState = delHover ? (pressedButton_ == &card.deleteButton ? InteractionState::Pressed : InteractionState::Hover)
                                               : InteractionState::Normal;
        if (card.deleteButton.state != newDelState) {
            card.deleteButton.state = newDelState;
            needRepaint = true;
        }
    }

    if (needRepaint) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnMouseLeave() {
    mouseTracking_ = false;
    bool needRepaint = false;

    if (addButton_.state != InteractionState::Normal) {
        addButton_.state = InteractionState::Normal;
        needRepaint = true;
    }
    if (manageEnvsButton_.state != InteractionState::Normal) {
        manageEnvsButton_.state = InteractionState::Normal;
        needRepaint = true;
    }

    for (auto& card : cards_) {
        if (card.isHovered) {
            card.isHovered = false;
            needRepaint = true;
        }
        if (card.launchButton.state != InteractionState::Disabled && card.launchButton.state != InteractionState::Normal) {
            card.launchButton.state = InteractionState::Normal;
            needRepaint = true;
        }
        if (card.editButton.state != InteractionState::Normal) {
            card.editButton.state = InteractionState::Normal;
            needRepaint = true;
        }
        if (card.deleteButton.state != InteractionState::Normal) {
            card.deleteButton.state = InteractionState::Normal;
            needRepaint = true;
        }
    }

    if (needRepaint) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnLButtonDown(int x, int y) {
    float dipX = DpiHelper::Unscale(static_cast<float>(x), dpi_);
    float dipY = DpiHelper::Unscale(static_cast<float>(y), dpi_);

    if (IsPointInScrollThumb(dipX, dipY)) {
        isDraggingScrollbar_ = true;
        dragStartMouseY_ = dipY;
        dragStartScrollOffset_ = scrollOffset_;
        SetCapture(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    pressedButton_ = nullptr;

    if (addButton_.HitTest(dipX, dipY)) {
        pressedButton_ = &addButton_;
        addButton_.state = InteractionState::Pressed;
    } else if (dipY >= 60.0f && manageEnvsButton_.HitTest(dipX, dipY)) {
        pressedButton_ = &manageEnvsButton_;
        manageEnvsButton_.state = InteractionState::Pressed;
    } else if (dipY >= 60.0f) {
        for (auto& card : cards_) {
            if (card.executableExists && card.launchButton.HitTest(dipX, dipY)) {
                pressedButton_ = &card.launchButton;
                card.launchButton.state = InteractionState::Pressed;
                break;
            } else if (card.editButton.HitTest(dipX, dipY)) {
                pressedButton_ = &card.editButton;
                card.editButton.state = InteractionState::Pressed;
                break;
            } else if (card.deleteButton.HitTest(dipX, dipY)) {
                pressedButton_ = &card.deleteButton;
                card.deleteButton.state = InteractionState::Pressed;
                break;
            }
        }
    }

    if (pressedButton_) {
        SetCapture(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnLButtonUp(int x, int y) {
    if (isDraggingScrollbar_) {
        isDraggingScrollbar_ = false;
        ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (pressedButton_) {
        ReleaseCapture();

        float dipX = DpiHelper::Unscale(static_cast<float>(x), dpi_);
        float dipY = DpiHelper::Unscale(static_cast<float>(y), dpi_);

        if (pressedButton_->HitTest(dipX, dipY)) {
            TriggerButtonClick(*pressedButton_);
        }

        pressedButton_->state = InteractionState::Normal;
        pressedButton_ = nullptr;

        // Refresh hover state at current mouse position
        OnMouseMove(x, y);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnRButtonUp(int x, int y) {
    float dipX = DpiHelper::Unscale(static_cast<float>(x), dpi_);
    float dipY = DpiHelper::Unscale(static_cast<float>(y), dpi_);

    if (dipY < 60.0f) {
        return;
    }

    for (const auto& card : cards_) {
        if (card.HitTest(dipX, dipY)) {
            contextMenuAppId_ = card.id;

            HMENU hMenu = CreatePopupMenu();
            if (!hMenu) return;

            UINT launchFlags = card.executableExists ? MF_STRING : (MF_STRING | MF_GRAYED);
            AppendMenuW(hMenu, launchFlags, 2001, L"Launch");
            AppendMenuW(hMenu, launchFlags, 2002, L"Launch without profile");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 2003, L"Edit");
            AppendMenuW(hMenu, MF_STRING, 2004, L"Delete");

            POINT pt{x, y};
            ClientToScreen(hwnd_, &pt);

            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
            DestroyMenu(hMenu);
            break;
        }
    }
}

void MainWindow::OnMouseWheel(short delta) {
    float scrollDelta = static_cast<float>(delta) * 0.4f;
    float newScroll = scrollOffset_ - scrollDelta;
    scrollOffset_ = std::clamp(newScroll, 0.0f, maxScrollOffset_);

    Layout::CalculateLayout(
        clientWidth_,
        clientHeight_,
        scrollOffset_,
        addButton_,
        cards_,
        manageEnvsButton_,
        totalContentHeight_,
        maxScrollOffset_
    );

    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::TriggerButtonClick(const ButtonControl& button) {
    if (button.text == L"+ Add Application") {
        AppProfile newApp;
        if (AppEditDialog::Show(hwnd_, configManager_.GetConfig().environmentProfiles, newApp, true)) {
            configManager_.GetConfig().applications.push_back(std::move(newApp));
            configManager_.Save();
            ReloadCardsFromConfig();
        }
    } else if (button.text == L"Manage Environments") {
        if (ProfileManagerDialog::Show(hwnd_, configManager_)) {
            ReloadCardsFromConfig();
        }
    } else if (button.text == L"Edit") {
        if (button.targetId.empty()) return;

        auto* app = configManager_.GetConfig().FindAppById(button.targetId);
        if (app) {
            AppProfile editedApp = *app;
            if (AppEditDialog::Show(hwnd_, configManager_.GetConfig().environmentProfiles, editedApp, false)) {
                *app = std::move(editedApp);
                configManager_.Save();
                ReloadCardsFromConfig();
            }
        }
    } else if (button.text == L"Delete") {
        if (button.targetId.empty()) return;

        auto* app = configManager_.GetConfig().FindAppById(button.targetId);
        if (app) {
            std::wstring msg = L"Delete \"" + app->name + L"\" from EnvLauncher?\n\nThis will remove the application configuration. It will not uninstall or delete the executable file.";
            int ans = MessageBoxW(hwnd_, msg.c_str(), L"Delete Application", MB_YESNO | MB_ICONQUESTION);
            if (ans == IDYES) {
                auto& apps = configManager_.GetConfig().applications;
                apps.erase(
                    std::remove_if(apps.begin(), apps.end(), [&](const AppProfile& a) { return a.id == button.targetId; }),
                    apps.end()
                );
                configManager_.Save();
                ReloadCardsFromConfig();
            }
        }
    } else if (button.text == L"Launch") {
        LaunchAppById(button.targetId, AppLaunchMode::Normal);
    }
}

void MainWindow::LaunchAppById(std::wstring_view appId, AppLaunchMode mode) {
    if (appId.empty()) {
        return;
    }

    LaunchResult res = ConfiguredAppLauncher::LaunchApp(
        configManager_.GetConfig(),
        appId,
        mode
    );

    if (!res.success) {
        std::wstring errorMsg = L"Failed to launch application.\n\n"
                                L"Error: " + res.errorMessage;
        MessageBoxW(
            hwnd_,
            errorMsg.c_str(),
            L"EnvLauncher - Launch Error",
            MB_OK | MB_ICONERROR
        );
    }
}

bool MainWindow::IsPointInScrollThumb(float dipX, float dipY) const {
    const float headerHeight = 60.0f;
    float viewportHeight = clientHeight_ - headerHeight;
    if (totalContentHeight_ <= viewportHeight || totalContentHeight_ <= 0.0f) {
        return false;
    }

    const float scrollBarWidth = 14.0f; // Click tolerance
    const float trackTop = headerHeight;
    const float trackBottom = clientHeight_;
    const float trackHeight = trackBottom - trackTop;
    const float trackLeft = clientWidth_ - scrollBarWidth;
    const float trackRight = clientWidth_;

    float thumbHeight = std::max(24.0f, trackHeight * (viewportHeight / totalContentHeight_));
    float maxThumbTravel = trackHeight - thumbHeight;
    float maxScroll = totalContentHeight_ - viewportHeight;
    float scrollFraction = (maxScroll > 0.0f) ? std::clamp(scrollOffset_ / maxScroll, 0.0f, 1.0f) : 0.0f;
    float thumbTop = trackTop + scrollFraction * maxThumbTravel;
    float thumbBottom = thumbTop + thumbHeight;

    return (dipX >= trackLeft && dipX <= trackRight && dipY >= thumbTop && dipY <= thumbBottom);
}

} // namespace envlauncher
