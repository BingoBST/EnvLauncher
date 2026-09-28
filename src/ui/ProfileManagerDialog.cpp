#include "ui/ProfileManagerDialog.h"
#include "ui/ProfileEditDialog.h"
#include "ui/Renderer.h"
#include "platform/DpiHelper.h"
#include "platform/DwmHelper.h"

#include <windows.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <algorithm>

namespace envlauncher {

struct ProfileMgrContext {
    ConfigManager* configMgr{nullptr};
    bool modified{false};
    UINT dpi{96};
    float scale{1.0f};

    Renderer renderer;
    HWND hwnd{nullptr};

    float dlgWidth{660.0f};
    float dlgHeight{460.0f};

    int selectedIndex{0};
    int hoveredIndex{-1};
    DWORD lastClickTime{0};
    int lastClickIndex{-1};

    // Layout (in DIPs)
    D2D1_RECT_F listContainer{};
    D2D1_RECT_F previewContainer{};
    std::vector<D2D1_RECT_F> itemBounds;

    ButtonControl btnAdd;
    ButtonControl btnEdit;
    ButtonControl btnDelete;
    ButtonControl btnClose;
    ButtonControl* pressedBtn{nullptr};
    bool mouseTracking{false};
};

static void UpdateButtonStates(ProfileMgrContext* ctx) {
    const auto& profiles = ctx->configMgr->GetConfig().environmentProfiles;
    bool hasSel = (ctx->selectedIndex >= 0 && ctx->selectedIndex < static_cast<int>(profiles.size()));

    ctx->btnEdit.state = hasSel ? InteractionState::Normal : InteractionState::Disabled;
    ctx->btnDelete.state = hasSel ? InteractionState::Normal : InteractionState::Disabled;
}

static void CalculateItemBounds(ProfileMgrContext* ctx) {
    const auto& profiles = ctx->configMgr->GetConfig().environmentProfiles;
    ctx->itemBounds.clear();

    const float itemH = 46.0f;
    const float gap = 4.0f;
    float curY = ctx->listContainer.top + 6.0f;

    for (size_t i = 0; i < profiles.size(); ++i) {
        D2D1_RECT_F rect = D2D1::RectF(
            ctx->listContainer.left + 6.0f,
            curY,
            ctx->listContainer.right - 6.0f,
            curY + itemH
        );
        ctx->itemBounds.push_back(rect);
        curY += itemH + gap;
    }

    if (ctx->selectedIndex >= static_cast<int>(profiles.size())) {
        ctx->selectedIndex = profiles.empty() ? -1 : static_cast<int>(profiles.size()) - 1;
    }
    UpdateButtonStates(ctx);
}

static void TriggerEdit(HWND hwnd, ProfileMgrContext* ctx) {
    auto& profiles = ctx->configMgr->GetConfig().environmentProfiles;
    if (ctx->selectedIndex >= 0 && ctx->selectedIndex < static_cast<int>(profiles.size())) {
        EnvironmentProfile editedProf = profiles[ctx->selectedIndex];
        if (ProfileEditDialog::Show(hwnd, editedProf, false)) {
            profiles[ctx->selectedIndex] = std::move(editedProf);
            ctx->configMgr->Save();
            ctx->modified = true;
            CalculateItemBounds(ctx);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
}

static void TriggerDelete(HWND hwnd, ProfileMgrContext* ctx) {
    const auto& config = ctx->configMgr->GetConfig();
    if (ctx->selectedIndex < 0 || ctx->selectedIndex >= static_cast<int>(config.environmentProfiles.size())) {
        return;
    }

    const auto& targetProfile = config.environmentProfiles[ctx->selectedIndex];

    // Safety guard: Check if any app references this profile
    std::vector<std::wstring> referencingApps;
    for (const auto& app : config.applications) {
        if (app.environmentProfileId == targetProfile.id) {
            referencingApps.push_back(app.name);
        }
    }

    if (!referencingApps.empty()) {
        std::wstring warnMsg = L"This environment profile is currently used by:\n\n";
        for (const auto& appName : referencingApps) {
            warnMsg += L"  - " + appName + L"\n";
        }
        warnMsg += L"\nPlease remove or change the profile in these applications before deleting it.";
        MessageBoxW(hwnd, warnMsg.c_str(), L"Cannot Delete Profile", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring confirmMsg = L"Are you sure you want to delete profile \"" + targetProfile.name + L"\"?";
    int ans = MessageBoxW(hwnd, confirmMsg.c_str(), L"Delete Profile", MB_YESNO | MB_ICONQUESTION);
    if (ans == IDYES) {
        ctx->configMgr->GetConfig().environmentProfiles.erase(
            ctx->configMgr->GetConfig().environmentProfiles.begin() + ctx->selectedIndex
        );
        ctx->configMgr->Save();
        ctx->modified = true;
        if (ctx->selectedIndex >= static_cast<int>(ctx->configMgr->GetConfig().environmentProfiles.size())) {
            ctx->selectedIndex = static_cast<int>(ctx->configMgr->GetConfig().environmentProfiles.size()) - 1;
        }
        CalculateItemBounds(ctx);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static LRESULT CALLBACK ProfileMgrWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<ProfileMgrContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_ERASEBKGND:
            return 1; // Handled by D2D

        case WM_PAINT: {
            if (!ctx) break;
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);

            if (ctx->renderer.CreateDeviceResources(hwnd, ctx->dpi)) {
                if (ctx->renderer.BeginDraw()) {
                    ctx->renderer.GetRenderTarget()->SetTransform(D2D1::Matrix3x2F::Identity());
                    ctx->renderer.ClearBackground();

                    const auto& theme = ctx->renderer.GetTheme();
                    const float headerH = 50.0f;

                    // 1. Header Bar
                    ctx->renderer.DrawHeaderBar(L"Manage Environment Profiles", ctx->dlgWidth, headerH);

                    // 2. Section Labels
                    ctx->renderer.DrawLabel(L"Profiles:", D2D1::RectF(ctx->listContainer.left, 66.0f, ctx->listContainer.right, 84.0f), theme.textSecondary);
                    ctx->renderer.DrawLabel(L"Variables Preview:", D2D1::RectF(ctx->previewContainer.left, 66.0f, ctx->previewContainer.right, 84.0f), theme.textSecondary);

                    // 3. Left: Profiles List Container
                    ctx->renderer.DrawInputBoxFrame(ctx->listContainer, false);

                    const auto& profiles = ctx->configMgr->GetConfig().environmentProfiles;
                    if (profiles.empty()) {
                        ctx->renderer.DrawLabel(
                            L"(No profiles configured)",
                            D2D1::RectF(ctx->listContainer.left + 16.0f, ctx->listContainer.top + 16.0f, ctx->listContainer.right - 16.0f, ctx->listContainer.top + 36.0f),
                            theme.textMuted
                        );
                    } else {
                        for (size_t i = 0; i < ctx->itemBounds.size() && i < profiles.size(); ++i) {
                            std::wstring subtitle = std::to_wstring(profiles[i].variables.size()) + L" variable(s)";
                            ctx->renderer.DrawListItem(
                                ctx->itemBounds[i],
                                profiles[i].name,
                                subtitle,
                                ctx->selectedIndex == static_cast<int>(i),
                                ctx->hoveredIndex == static_cast<int>(i)
                            );
                        }
                    }

                    // 4. Right: Variables Preview Container
                    ctx->renderer.DrawInputBoxFrame(ctx->previewContainer, false);

                    if (ctx->selectedIndex >= 0 && ctx->selectedIndex < static_cast<int>(profiles.size())) {
                        const auto& prof = profiles[ctx->selectedIndex];
                        float px = ctx->previewContainer.left + 16.0f;
                        float pr = ctx->previewContainer.right - 16.0f;
                        float py = ctx->previewContainer.top + 14.0f;

                        // Profile Title
                        ctx->renderer.DrawLabel(prof.name, D2D1::RectF(px, py, pr, py + 22.0f), theme.textPrimary);
                        py += 22.0f;

                        // Profile ID
                        ctx->renderer.DrawLabel(L"ID: " + prof.id, D2D1::RectF(px, py, pr, py + 18.0f), theme.textMuted);
                        py += 24.0f;

                        // Separator line
                        ctx->renderer.DrawSeparator(px, pr, py);
                        py += 10.0f;

                        // Variables
                        if (prof.variables.empty()) {
                            ctx->renderer.DrawLabel(L"(No variables configured)", D2D1::RectF(px, py, pr, py + 20.0f), theme.textMuted);
                        } else {
                            for (const auto& [k, v] : prof.variables) {
                                if (py + 20.0f > ctx->previewContainer.bottom - 10.0f) {
                                    ctx->renderer.DrawLabel(L"...", D2D1::RectF(px, py, pr, py + 16.0f), theme.textMuted);
                                    break;
                                }
                                std::wstring line = k + L" = " + v;
                                ctx->renderer.DrawLabel(line, D2D1::RectF(px, py, pr, py + 20.0f), theme.textSecondary);
                                py += 20.0f;
                            }
                        }
                    } else {
                        ctx->renderer.DrawLabel(
                            L"No profile selected.",
                            D2D1::RectF(ctx->previewContainer.left + 16.0f, ctx->previewContainer.top + 16.0f, ctx->previewContainer.right - 16.0f, ctx->previewContainer.top + 36.0f),
                            theme.textMuted
                        );
                    }

                    // 5. Buttons
                    ctx->renderer.DrawButton(ctx->btnAdd);
                    ctx->renderer.DrawButton(ctx->btnEdit);
                    ctx->renderer.DrawButton(ctx->btnDelete);
                    ctx->renderer.DrawButton(ctx->btnClose);

                    ctx->renderer.EndDraw();
                }
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!ctx) break;
            float mx = DpiHelper::Unscale(static_cast<float>(GET_X_LPARAM(lParam)), ctx->dpi);
            float my = DpiHelper::Unscale(static_cast<float>(GET_Y_LPARAM(lParam)), ctx->dpi);

            if (!ctx->mouseTracking) {
                TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                ctx->mouseTracking = true;
            }

            bool needRepaint = false;

            // Check list items hover
            int newHoverItem = -1;
            for (size_t i = 0; i < ctx->itemBounds.size(); ++i) {
                const auto& r = ctx->itemBounds[i];
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    newHoverItem = static_cast<int>(i);
                    break;
                }
            }
            if (ctx->hoveredIndex != newHoverItem) {
                ctx->hoveredIndex = newHoverItem;
                needRepaint = true;
            }

            // Check buttons hover
            auto updateBtn = [&](ButtonControl& btn) {
                if (btn.state == InteractionState::Disabled) return;
                bool hit = btn.HitTest(mx, my);
                auto newState = hit ? (ctx->pressedBtn == &btn ? InteractionState::Pressed : InteractionState::Hover)
                                    : InteractionState::Normal;
                if (btn.state != newState) {
                    btn.state = newState;
                    needRepaint = true;
                }
            };

            updateBtn(ctx->btnAdd);
            updateBtn(ctx->btnEdit);
            updateBtn(ctx->btnDelete);
            updateBtn(ctx->btnClose);

            if (needRepaint) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (!ctx) break;
            ctx->mouseTracking = false;
            ctx->hoveredIndex = -1;
            if (ctx->btnAdd.state != InteractionState::Disabled) ctx->btnAdd.state = InteractionState::Normal;
            if (ctx->btnEdit.state != InteractionState::Disabled) ctx->btnEdit.state = InteractionState::Normal;
            if (ctx->btnDelete.state != InteractionState::Disabled) ctx->btnDelete.state = InteractionState::Normal;
            if (ctx->btnClose.state != InteractionState::Disabled) ctx->btnClose.state = InteractionState::Normal;
            ctx->pressedBtn = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (!ctx) break;
            float mx = DpiHelper::Unscale(static_cast<float>(GET_X_LPARAM(lParam)), ctx->dpi);
            float my = DpiHelper::Unscale(static_cast<float>(GET_Y_LPARAM(lParam)), ctx->dpi);

            // 1. Check list items click
            for (size_t i = 0; i < ctx->itemBounds.size(); ++i) {
                const auto& r = ctx->itemBounds[i];
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    DWORD now = GetTickCount();
                    if (ctx->selectedIndex == static_cast<int>(i) && (now - ctx->lastClickTime < GetDoubleClickTime())) {
                        // Double click -> Edit
                        TriggerEdit(hwnd, ctx);
                        return 0;
                    }
                    ctx->lastClickTime = now;
                    ctx->lastClickIndex = static_cast<int>(i);
                    ctx->selectedIndex = static_cast<int>(i);
                    UpdateButtonStates(ctx);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // 2. Check buttons click
            auto checkPress = [&](ButtonControl& btn) -> bool {
                if (btn.state == InteractionState::Disabled) return false;
                if (btn.HitTest(mx, my)) {
                    btn.state = InteractionState::Pressed;
                    ctx->pressedBtn = &btn;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return true;
                }
                return false;
            };

            if (checkPress(ctx->btnAdd)) return 0;
            if (checkPress(ctx->btnEdit)) return 0;
            if (checkPress(ctx->btnDelete)) return 0;
            if (checkPress(ctx->btnClose)) return 0;

            return 0;
        }

        case WM_LBUTTONUP: {
            if (!ctx || !ctx->pressedBtn) break;
            float mx = DpiHelper::Unscale(static_cast<float>(GET_X_LPARAM(lParam)), ctx->dpi);
            float my = DpiHelper::Unscale(static_cast<float>(GET_Y_LPARAM(lParam)), ctx->dpi);

            ButtonControl* clicked = ctx->pressedBtn;
            ctx->pressedBtn = nullptr;
            ReleaseCapture();

            if (clicked->state != InteractionState::Disabled && clicked->HitTest(mx, my)) {
                clicked->state = InteractionState::Hover;
                InvalidateRect(hwnd, nullptr, FALSE);

                if (clicked == &ctx->btnAdd) {
                    EnvironmentProfile newProf;
                    if (ProfileEditDialog::Show(hwnd, newProf, true)) {
                        ctx->configMgr->GetConfig().environmentProfiles.push_back(std::move(newProf));
                        ctx->configMgr->Save();
                        ctx->modified = true;
                        ctx->selectedIndex = static_cast<int>(ctx->configMgr->GetConfig().environmentProfiles.size()) - 1;
                        CalculateItemBounds(ctx);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                } else if (clicked == &ctx->btnEdit) {
                    TriggerEdit(hwnd, ctx);
                } else if (clicked == &ctx->btnDelete) {
                    TriggerDelete(hwnd, ctx);
                } else if (clicked == &ctx->btnClose) {
                    DestroyWindow(hwnd);
                }
            } else if (clicked->state != InteractionState::Disabled) {
                clicked->state = InteractionState::Normal;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

bool ProfileManagerDialog::Show(HWND parent, ConfigManager& configManager) {
    UINT dpi = DpiHelper::GetSystemDpi();
    if (parent) {
        dpi = DpiHelper::GetWindowDpi(parent);
    }

    ProfileMgrContext ctx;
    ctx.configMgr = &configManager;
    ctx.dpi = dpi;
    ctx.scale = static_cast<float>(dpi) / 96.0f;

    if (!ctx.renderer.Initialize()) {
        return false;
    }

    const wchar_t* kClassName = L"EnvLauncherD2DProfileMgrDlgClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ProfileMgrWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    DWORD exStyle = WS_EX_DLGMODALFRAME;

    int dlgPixelW = static_cast<int>(ctx.dlgWidth * ctx.scale);
    int dlgPixelH = static_cast<int>(ctx.dlgHeight * ctx.scale);

    RECT winRect = { 0, 0, dlgPixelW, dlgPixelH };
    AdjustWindowRectEx(&winRect, style, FALSE, exStyle);
    int winW = winRect.right - winRect.left;
    int winH = winRect.bottom - winRect.top;

    RECT parentRect;
    if (parent && GetWindowRect(parent, &parentRect)) {
        int px = parentRect.left + (parentRect.right - parentRect.left - winW) / 2;
        int py = parentRect.top + (parentRect.bottom - parentRect.top - winH) / 2;
        parentRect.left = std::max(0, px);
        parentRect.top = std::max(0, py);
    } else {
        parentRect.left = CW_USEDEFAULT;
        parentRect.top = CW_USEDEFAULT;
    }

    HWND hwnd = CreateWindowExW(
        exStyle,
        kClassName,
        L"Manage Environment Profiles",
        style,
        parentRect.left, parentRect.top, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hwnd) {
        return false;
    }

    ctx.hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));

    DwmHelper::EnableDarkMode(hwnd);
    DwmHelper::EnableRoundedCorners(hwnd);
    DwmHelper::SetTitleBarColor(hwnd, RGB(28, 28, 32), RGB(234, 234, 235));

    // DIP Layout
    const float left = 24.0f;
    const float colW = 290.0f;
    const float gap = 16.0f;
    const float rightColX = left + colW + gap;
    const float previewW = ctx.dlgWidth - rightColX - left;
    const float listH = 286.0f;

    ctx.listContainer = D2D1::RectF(left, 88.0f, left + colW, 88.0f + listH);
    ctx.previewContainer = D2D1::RectF(rightColX, 88.0f, rightColX + previewW, 88.0f + listH);

    const float btnY = 88.0f + listH + 14.0f;
    const float actBtnW = 86.0f;
    const float actBtnH = 32.0f;

    ctx.btnAdd = {L"btn_add", L"", L"+ Add", D2D1::RectF(left, btnY, left + actBtnW, btnY + actBtnH), InteractionState::Normal, false};
    ctx.btnEdit = {L"btn_edit", L"", L"Edit", D2D1::RectF(left + actBtnW + 8.0f, btnY, left + (actBtnW + 8.0f) + actBtnW, btnY + actBtnH), InteractionState::Normal, false};
    ctx.btnDelete = {L"btn_delete", L"", L"Delete", D2D1::RectF(left + (actBtnW + 8.0f) * 2.0f, btnY, left + (actBtnW + 8.0f) * 2.0f + actBtnW, btnY + actBtnH), InteractionState::Normal, false};

    const float closeBtnW = 90.0f;
    ctx.btnClose = {L"btn_close", L"", L"Close", D2D1::RectF(rightColX + previewW - closeBtnW, btnY, rightColX + previewW, btnY + actBtnH), InteractionState::Normal, true};

    CalculateItemBounds(&ctx);

    if (parent) {
        EnableWindow(parent, FALSE);
    }

    MSG msg{};
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (parent) {
        EnableWindow(parent, TRUE);
        SetActiveWindow(parent);
    }

    return ctx.modified;
}

} // namespace envlauncher
