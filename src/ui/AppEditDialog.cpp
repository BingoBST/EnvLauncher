#include "ui/AppEditDialog.h"
#include "ui/FileDialog.h"
#include "ui/Renderer.h"
#include "platform/DpiHelper.h"
#include "platform/DwmHelper.h"
#include "util/StringUtils.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <set>

namespace envlauncher {

static constexpr UINT_PTR kEditSubclassId = 5001;

struct AppEditContext {
    const std::vector<EnvironmentProfile>* profiles{nullptr};
    AppProfile* app{nullptr};
    bool isNew{false};
    bool saved{false};
    UINT dpi{96};
    float scale{1.0f};

    Renderer renderer;
    HFONT hFont{nullptr};
    HBRUSH hEditBgBrush{nullptr};

    HWND hwnd{nullptr};
    HWND hEditName{nullptr};
    HWND hEditExe{nullptr};
    HWND hEditArgs{nullptr};
    HWND hEditWorkDir{nullptr};
    HWND hEditOverrides{nullptr};
    HWND focusedEdit{nullptr};

    std::wstring selectedProfileId;
    std::wstring selectedProfileName;

    // D2D Frame Bounds (in DIPs)
    D2D1_RECT_F nameFrame{};
    D2D1_RECT_F exeFrame{};
    D2D1_RECT_F argsFrame{};
    D2D1_RECT_F workDirFrame{};
    D2D1_RECT_F overridesFrame{};

    // D2D Buttons
    ButtonControl btnBrowseExe;
    ButtonControl btnBrowseDir;
    ButtonControl btnProfileDropdown;
    ButtonControl btnSave;
    ButtonControl btnCancel;
    ButtonControl* pressedBtn{nullptr};
    bool mouseTracking{false};

    float dlgWidth{560.0f};
    float dlgHeight{536.0f};
};

static std::wstring Trim(const std::wstring& s) {
    auto start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    auto end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

static LRESULT CALLBACK SeamlessEditSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
{
    auto* ctx = reinterpret_cast<AppEditContext*>(dwRefData);

    switch (uMsg) {
        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hWnd, &rc);
            if (ctx && ctx->hEditBgBrush) {
                FillRect(hdc, &rc, ctx->hEditBgBrush);
            }
            return 1; // Handled, completely prevents white flash
        }
        case WM_SETFOCUS: {
            if (ctx) {
                ctx->focusedEdit = hWnd;
                InvalidateRect(ctx->hwnd, nullptr, FALSE);
            }
            break;
        }
        case WM_KILLFOCUS: {
            if (ctx && ctx->focusedEdit == hWnd) {
                ctx->focusedEdit = nullptr;
                InvalidateRect(ctx->hwnd, nullptr, FALSE);
            }
            break;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_RETURN && !(GetWindowLongPtrW(hWnd, GWL_STYLE) & ES_MULTILINE)) {
                if (ctx) {
                    SendMessageW(ctx->hwnd, WM_COMMAND, IDOK, 0);
                    return 0;
                }
            } else if (wParam == VK_ESCAPE) {
                if (ctx) {
                    SendMessageW(ctx->hwnd, WM_COMMAND, IDCANCEL, 0);
                    return 0;
                }
            }
            break;
        }
        case WM_NCDESTROY: {
            RemoveWindowSubclass(hWnd, SeamlessEditSubclassProc, kEditSubclassId);
            break;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void HandleSave(HWND hwnd, AppEditContext* ctx) {
    // 1. Validate Name
    int nameLen = GetWindowTextLengthW(ctx->hEditName);
    std::wstring name(nameLen + 1, L'\0');
    GetWindowTextW(ctx->hEditName, name.data(), nameLen + 1);
    name.resize(nameLen);
    name = Trim(name);

    if (name.empty()) {
        MessageBoxW(hwnd, L"Application name cannot be empty.", L"Validation Error", MB_OK | MB_ICONWARNING);
        SetFocus(ctx->hEditName);
        return;
    }

    // 2. Validate Executable Path
    int exeLen = GetWindowTextLengthW(ctx->hEditExe);
    std::wstring exe(exeLen + 1, L'\0');
    GetWindowTextW(ctx->hEditExe, exe.data(), exeLen + 1);
    exe.resize(exeLen);
    exe = Trim(exe);

    if (exe.empty()) {
        MessageBoxW(hwnd, L"Executable path cannot be empty.", L"Validation Error", MB_OK | MB_ICONWARNING);
        SetFocus(ctx->hEditExe);
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(exe, ec)) {
        int ans = MessageBoxW(
            hwnd,
            L"The specified executable does not exist on disk.\n\nDo you want to save anyway?",
            L"Executable Not Found",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2
        );
        if (ans != IDYES) {
            SetFocus(ctx->hEditExe);
            return;
        }
    }

    // 3. Arguments & Working Directory
    int argsLen = GetWindowTextLengthW(ctx->hEditArgs);
    std::wstring args(argsLen + 1, L'\0');
    GetWindowTextW(ctx->hEditArgs, args.data(), argsLen + 1);
    args.resize(argsLen);

    int dirLen = GetWindowTextLengthW(ctx->hEditWorkDir);
    std::wstring workDir(dirLen + 1, L'\0');
    GetWindowTextW(ctx->hEditWorkDir, workDir.data(), dirLen + 1);
    workDir.resize(dirLen);
    workDir = Trim(workDir);

    // 4. Environment Overrides parsing
    int ovLen = GetWindowTextLengthW(ctx->hEditOverrides);
    std::wstring ovText(ovLen + 1, L'\0');
    GetWindowTextW(ctx->hEditOverrides, ovText.data(), ovLen + 1);
    ovText.resize(ovLen);
    EnvMap overrides;
    std::set<std::wstring, CaseInsensitiveLess> seenKeys;
    std::wstringstream ss(ovText);
    std::wstring line;
    while (std::getline(ss, line)) {
        std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L'#') continue;

        size_t eqPos = trimmed.find(L'=');
        if (eqPos == std::wstring::npos || eqPos == 0) {
            std::wstring err = L"Invalid environment override format:\n\"" + trimmed +
                               L"\"\n\nEach line must follow the format KEY=VALUE.";
            MessageBoxW(hwnd, err.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
            SetFocus(ctx->hEditOverrides);
            return;
        }

        std::wstring key = Trim(trimmed.substr(0, eqPos));
        std::wstring val = trimmed.substr(eqPos + 1);

        if (key.find(L'=') != std::wstring::npos) {
            MessageBoxW(hwnd, L"Variable key cannot contain '='.", L"Validation Error", MB_OK | MB_ICONWARNING);
            SetFocus(ctx->hEditOverrides);
            return;
        }

        if (!seenKeys.insert(key).second) {
            std::wstring err = L"Duplicate environment variable key '" + key + L"' in overrides.";
            MessageBoxW(hwnd, err.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
            SetFocus(ctx->hEditOverrides);
            return;
        }

        overrides[key] = val;
    }

    // Commit back to app
    if (ctx->isNew && ctx->app->id.empty()) {
        ctx->app->id = L"app_" + std::to_wstring(GetTickCount64());
    }
    ctx->app->name = std::move(name);
    ctx->app->executable = std::move(exe);
    ctx->app->arguments = std::move(args);
    ctx->app->workingDirectory = std::move(workDir);
    ctx->app->environmentProfileId = ctx->selectedProfileId;
    ctx->app->environmentOverrides = std::move(overrides);

    ctx->saved = true;
    DestroyWindow(hwnd);
}

struct DropdownPopupItem {
    std::wstring id;
    std::wstring name;
    bool isSelected{false};
};

struct DropdownPopupContext {
    std::vector<DropdownPopupItem> items;
    int hoveredIndex{-1};
    UINT dpi{96};
    float scale{1.0f};
    float width{512.0f};
    float itemH{32.0f};
    float totalH{0.0f};

    Renderer renderer;
    HWND hwnd{nullptr};
    bool closed{false};
    bool selected{false};
    std::wstring selectedId;
    std::wstring selectedName;
};

static LRESULT CALLBACK DropdownPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<DropdownPopupContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            if (!ctx) break;
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);

            if (ctx->renderer.CreateDeviceResources(hwnd, ctx->dpi)) {
                if (ctx->renderer.BeginDraw()) {
                    ctx->renderer.GetRenderTarget()->SetTransform(D2D1::Matrix3x2F::Identity());
                    ctx->renderer.ClearBackground();

                    const auto& theme = ctx->renderer.GetTheme();
                    D2D1_RECT_F bgRect = D2D1::RectF(0.0f, 0.0f, ctx->width, ctx->totalH);

                    // Draw outer box with accent border
                    ctx->renderer.DrawInputBoxFrame(bgRect, true);

                    // Draw items
                    for (size_t i = 0; i < ctx->items.size(); ++i) {
                        float iy = 4.0f + i * ctx->itemH;
                        D2D1_RECT_F itemRect = D2D1::RectF(4.0f, iy, ctx->width - 4.0f, iy + ctx->itemH);
                        bool isHover = (ctx->hoveredIndex == static_cast<int>(i));
                        bool isSel = ctx->items[i].isSelected;

                        if (isHover) {
                            ctx->renderer.DrawListItem(itemRect, L"", L"", false, true);
                        }

                        // Checkmark if selected
                        if (isSel) {
                            ctx->renderer.DrawLabel(
                                L"\u2713",
                                D2D1::RectF(itemRect.left + 10.0f, iy + 6.0f, itemRect.left + 26.0f, iy + ctx->itemH),
                                theme.accent
                            );
                        }

                        // Text
                        D2D1_COLOR_F textColor = isSel ? theme.accent : (isHover ? theme.textPrimary : theme.textSecondary);
                        ctx->renderer.DrawLabel(
                            ctx->items[i].name,
                            D2D1::RectF(itemRect.left + 30.0f, iy + 6.0f, itemRect.right - 10.0f, iy + ctx->itemH),
                            textColor
                        );
                    }

                    ctx->renderer.EndDraw();
                }
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!ctx) break;
            float my = DpiHelper::Unscale(static_cast<float>(GET_Y_LPARAM(lParam)), ctx->dpi);
            int newHover = -1;
            for (size_t i = 0; i < ctx->items.size(); ++i) {
                float iy = 4.0f + i * ctx->itemH;
                if (my >= iy && my < iy + ctx->itemH) {
                    newHover = static_cast<int>(i);
                    break;
                }
            }
            if (ctx->hoveredIndex != newHover) {
                ctx->hoveredIndex = newHover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!ctx) break;
            float mx = DpiHelper::Unscale(static_cast<float>(GET_X_LPARAM(lParam)), ctx->dpi);
            float my = DpiHelper::Unscale(static_cast<float>(GET_Y_LPARAM(lParam)), ctx->dpi);

            if (mx >= 0 && mx <= ctx->width && my >= 0 && my <= ctx->totalH) {
                for (size_t i = 0; i < ctx->items.size(); ++i) {
                    float iy = 4.0f + i * ctx->itemH;
                    if (my >= iy && my < iy + ctx->itemH) {
                        ctx->selected = true;
                        ctx->selectedId = ctx->items[i].id;
                        ctx->selectedName = ctx->items[i].name;
                        break;
                    }
                }
            }
            ctx->closed = true;
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_KEYDOWN: {
            if (!ctx) break;
            if (wParam == VK_ESCAPE) {
                ctx->closed = true;
                DestroyWindow(hwnd);
                return 0;
            } else if (wParam == VK_RETURN) {
                if (ctx->hoveredIndex >= 0 && ctx->hoveredIndex < static_cast<int>(ctx->items.size())) {
                    ctx->selected = true;
                    ctx->selectedId = ctx->items[ctx->hoveredIndex].id;
                    ctx->selectedName = ctx->items[ctx->hoveredIndex].name;
                }
                ctx->closed = true;
                DestroyWindow(hwnd);
                return 0;
            } else if (wParam == VK_DOWN) {
                ctx->hoveredIndex = std::min(static_cast<int>(ctx->items.size()) - 1, ctx->hoveredIndex + 1);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            } else if (wParam == VK_UP) {
                ctx->hoveredIndex = std::max(0, ctx->hoveredIndex - 1);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }

        case WM_KILLFOCUS: {
            if (ctx && !ctx->closed) {
                ctx->closed = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

static void ShowProfileDropdownMenu(HWND hwnd, AppEditContext* ctx) {
    DropdownPopupContext pctx;
    pctx.dpi = ctx->dpi;
    pctx.scale = ctx->scale;
    pctx.width = ctx->btnProfileDropdown.bounds.right - ctx->btnProfileDropdown.bounds.left;
    pctx.itemH = 32.0f;

    // Items
    pctx.items.push_back({L"", L"(None)", ctx->selectedProfileId.empty()});
    for (const auto& prof : *ctx->profiles) {
        pctx.items.push_back({prof.id, prof.name, prof.id == ctx->selectedProfileId});
    }

    pctx.totalH = pctx.items.size() * pctx.itemH + 8.0f;

    if (!pctx.renderer.Initialize()) {
        return;
    }

    const wchar_t* kPopupClass = L"EnvLauncherD2DDropdownPopupClass";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DropdownPopupWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPopupClass;
    RegisterClassExW(&wc);

    POINT pt{
        static_cast<LONG>(ctx->btnProfileDropdown.bounds.left * ctx->scale),
        static_cast<LONG>((ctx->btnProfileDropdown.bounds.bottom + 2.0f) * ctx->scale)
    };
    ClientToScreen(hwnd, &pt);

    int pw = static_cast<int>(pctx.width * ctx->scale);
    int ph = static_cast<int>(pctx.totalH * ctx->scale);

    HWND hPopup = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kPopupClass,
        L"",
        WS_POPUP | WS_VISIBLE,
        pt.x, pt.y, pw, ph,
        hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hPopup) return;

    pctx.hwnd = hPopup;
    SetWindowLongPtrW(hPopup, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&pctx));
    DwmHelper::EnableRoundedCorners(hPopup);

    SetCapture(hPopup);
    SetFocus(hPopup);

    MSG msg{};
    while (IsWindow(hPopup) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_LBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN ||
            msg.message == WM_RBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN) {
            POINT clickPt = msg.pt;
            RECT rc;
            GetWindowRect(hPopup, &rc);
            if (!PtInRect(&rc, clickPt)) {
                ReleaseCapture();
                DestroyWindow(hPopup);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseCapture();

    if (pctx.selected) {
        ctx->selectedProfileId = pctx.selectedId;
        ctx->selectedProfileName = pctx.selectedName;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static LRESULT CALLBACK AppEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<AppEditContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(34, 34, 38)); // Matches theme.card
            SetTextColor(hdc, RGB(245, 245, 247));
            if (ctx && ctx->hEditBgBrush) {
                return reinterpret_cast<LRESULT>(ctx->hEditBgBrush);
            }
            break;
        }

        case WM_ERASEBKGND:
            return 1; // Suppress flicker, D2D paints entire background

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

                    // 1. Header bar
                    ctx->renderer.DrawHeaderBar(
                        ctx->isNew ? L"Add Application" : L"Edit Application",
                        ctx->dlgWidth,
                        headerH
                    );

                    // 2. Labels
                    ctx->renderer.DrawLabel(L"Application Name:", D2D1::RectF(24.0f, 66.0f, 536.0f, 84.0f), theme.textSecondary);
                    ctx->renderer.DrawLabel(L"Executable Path (*.exe):", D2D1::RectF(24.0f, 124.0f, 536.0f, 142.0f), theme.textSecondary);
                    ctx->renderer.DrawLabel(L"Arguments (optional):", D2D1::RectF(24.0f, 182.0f, 536.0f, 200.0f), theme.textSecondary);
                    ctx->renderer.DrawLabel(L"Working Directory (optional):", D2D1::RectF(24.0f, 240.0f, 536.0f, 258.0f), theme.textSecondary);
                    ctx->renderer.DrawLabel(L"Environment Profile:", D2D1::RectF(24.0f, 298.0f, 536.0f, 316.0f), theme.textSecondary);
                    ctx->renderer.DrawLabel(L"Environment Overrides (KEY=VALUE per line):", D2D1::RectF(24.0f, 356.0f, 536.0f, 374.0f), theme.textSecondary);

                    // 3. Input Box Frames
                    ctx->renderer.DrawInputBoxFrame(ctx->nameFrame, ctx->focusedEdit == ctx->hEditName);
                    ctx->renderer.DrawInputBoxFrame(ctx->exeFrame, ctx->focusedEdit == ctx->hEditExe);
                    ctx->renderer.DrawInputBoxFrame(ctx->argsFrame, ctx->focusedEdit == ctx->hEditArgs);
                    ctx->renderer.DrawInputBoxFrame(ctx->workDirFrame, ctx->focusedEdit == ctx->hEditWorkDir);
                    ctx->renderer.DrawInputBoxFrame(ctx->overridesFrame, ctx->focusedEdit == ctx->hEditOverrides);

                    // 4. Buttons
                    ctx->renderer.DrawButton(ctx->btnBrowseExe);
                    ctx->renderer.DrawButton(ctx->btnBrowseDir);
                    ctx->renderer.DrawDropdownButton(ctx->btnProfileDropdown, ctx->selectedProfileName);
                    ctx->renderer.DrawButton(ctx->btnSave);
                    ctx->renderer.DrawButton(ctx->btnCancel);

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
            auto updateHover = [&](ButtonControl& btn) {
                bool hit = btn.HitTest(mx, my);
                auto newState = hit ? (ctx->pressedBtn == &btn ? InteractionState::Pressed : InteractionState::Hover)
                                    : InteractionState::Normal;
                if (btn.state != newState) {
                    btn.state = newState;
                    needRepaint = true;
                }
            };

            updateHover(ctx->btnBrowseExe);
            updateHover(ctx->btnBrowseDir);
            updateHover(ctx->btnProfileDropdown);
            updateHover(ctx->btnSave);
            updateHover(ctx->btnCancel);

            if (needRepaint) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (!ctx) break;
            ctx->mouseTracking = false;
            ctx->btnBrowseExe.state = InteractionState::Normal;
            ctx->btnBrowseDir.state = InteractionState::Normal;
            ctx->btnProfileDropdown.state = InteractionState::Normal;
            ctx->btnSave.state = InteractionState::Normal;
            ctx->btnCancel.state = InteractionState::Normal;
            ctx->pressedBtn = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (!ctx) break;
            float mx = DpiHelper::Unscale(static_cast<float>(GET_X_LPARAM(lParam)), ctx->dpi);
            float my = DpiHelper::Unscale(static_cast<float>(GET_Y_LPARAM(lParam)), ctx->dpi);

            auto checkPress = [&](ButtonControl& btn) -> bool {
                if (btn.HitTest(mx, my)) {
                    btn.state = InteractionState::Pressed;
                    ctx->pressedBtn = &btn;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return true;
                }
                return false;
            };

            if (checkPress(ctx->btnBrowseExe)) return 0;
            if (checkPress(ctx->btnBrowseDir)) return 0;
            if (checkPress(ctx->btnProfileDropdown)) return 0;
            if (checkPress(ctx->btnSave)) return 0;
            if (checkPress(ctx->btnCancel)) return 0;

            return 0;
        }

        case WM_LBUTTONUP: {
            if (!ctx || !ctx->pressedBtn) break;
            float mx = DpiHelper::Unscale(static_cast<float>(GET_X_LPARAM(lParam)), ctx->dpi);
            float my = DpiHelper::Unscale(static_cast<float>(GET_Y_LPARAM(lParam)), ctx->dpi);

            ButtonControl* clicked = ctx->pressedBtn;
            ctx->pressedBtn = nullptr;
            ReleaseCapture();

            if (clicked->HitTest(mx, my)) {
                clicked->state = InteractionState::Hover;
                InvalidateRect(hwnd, nullptr, FALSE);

                if (clicked == &ctx->btnBrowseExe) {
                    std::wstring selectedPath;
                    if (FileDialog::ChooseExecutableFile(hwnd, selectedPath)) {
                        SetWindowTextW(ctx->hEditExe, selectedPath.c_str());
                        wchar_t nameBuf[256]{};
                        GetWindowTextW(ctx->hEditName, nameBuf, 256);
                        if (wcslen(nameBuf) == 0) {
                            std::filesystem::path p(selectedPath);
                            SetWindowTextW(ctx->hEditName, p.stem().wstring().c_str());
                        }
                        wchar_t dirBuf[MAX_PATH]{};
                        GetWindowTextW(ctx->hEditWorkDir, dirBuf, MAX_PATH);
                        if (wcslen(dirBuf) == 0) {
                            std::filesystem::path p(selectedPath);
                            SetWindowTextW(ctx->hEditWorkDir, p.parent_path().wstring().c_str());
                        }
                    }
                } else if (clicked == &ctx->btnBrowseDir) {
                    std::wstring selectedDir;
                    if (FileDialog::ChooseDirectory(hwnd, selectedDir)) {
                        SetWindowTextW(ctx->hEditWorkDir, selectedDir.c_str());
                    }
                } else if (clicked == &ctx->btnProfileDropdown) {
                    ShowProfileDropdownMenu(hwnd, ctx);
                } else if (clicked == &ctx->btnSave) {
                    HandleSave(hwnd, ctx);
                } else if (clicked == &ctx->btnCancel) {
                    DestroyWindow(hwnd);
                }
            } else {
                clicked->state = InteractionState::Normal;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDOK) {
                if (ctx) HandleSave(hwnd, ctx);
                return 0;
            }
            if (wmId == IDCANCEL) {
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

bool AppEditDialog::Show(
    HWND parent,
    const std::vector<EnvironmentProfile>& profiles,
    AppProfile& appInOut,
    bool isNew
) {
    UINT dpi = DpiHelper::GetSystemDpi();
    if (parent) {
        dpi = DpiHelper::GetWindowDpi(parent);
    }

    AppEditContext ctx;
    ctx.profiles = &profiles;
    ctx.app = &appInOut;
    ctx.isNew = isNew;
    ctx.dpi = dpi;
    ctx.scale = static_cast<float>(dpi) / 96.0f;
    ctx.hEditBgBrush = CreateSolidBrush(RGB(34, 34, 38)); // Matches theme.card

    if (!ctx.renderer.Initialize()) {
        DeleteObject(ctx.hEditBgBrush);
        return false;
    }

    // Resolve initial profile name
    ctx.selectedProfileId = appInOut.environmentProfileId;
    ctx.selectedProfileName = L"(None)";
    for (const auto& prof : profiles) {
        if (prof.id == ctx.selectedProfileId) {
            ctx.selectedProfileName = prof.name;
            break;
        }
    }

    int fontHeight = -MulDiv(10, static_cast<int>(dpi), 72);
    ctx.hFont = CreateFontW(
        fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    const wchar_t* kClassName = L"EnvLauncherD2DAppEditDlgClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = AppEditWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // D2D renders entire background
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
        isNew ? L"Add Application" : L"Edit Application",
        style,
        parentRect.left, parentRect.top, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hwnd) {
        DeleteObject(ctx.hFont);
        DeleteObject(ctx.hEditBgBrush);
        return false;
    }

    ctx.hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));

    DwmHelper::EnableDarkMode(hwnd);
    DwmHelper::EnableRoundedCorners(hwnd);
    DwmHelper::SetTitleBarColor(hwnd, RGB(28, 28, 32), RGB(234, 234, 235));

    // Setup DIP Layout Coordinates
    const float left = 24.0f;
    const float fullW = 512.0f;
    const float btnW = 84.0f;
    const float editWithBtnW = fullW - btnW - 8.0f;
    const float editH = 28.0f;

    ctx.nameFrame = D2D1::RectF(left, 86.0f, left + fullW, 86.0f + editH);
    ctx.exeFrame = D2D1::RectF(left, 144.0f, left + editWithBtnW, 144.0f + editH);
    ctx.btnBrowseExe = {L"btn_browse_exe", L"", L"Browse", D2D1::RectF(left + editWithBtnW + 8.0f, 144.0f, left + fullW, 144.0f + editH), InteractionState::Normal, false};

    ctx.argsFrame = D2D1::RectF(left, 202.0f, left + fullW, 202.0f + editH);
    ctx.workDirFrame = D2D1::RectF(left, 260.0f, left + editWithBtnW, 260.0f + editH);
    ctx.btnBrowseDir = {L"btn_browse_dir", L"", L"Browse", D2D1::RectF(left + editWithBtnW + 8.0f, 260.0f, left + fullW, 260.0f + editH), InteractionState::Normal, false};

    ctx.btnProfileDropdown = {L"btn_profile", L"", ctx.selectedProfileName, D2D1::RectF(left, 318.0f, left + fullW, 318.0f + editH), InteractionState::Normal, false};

    const float overridesH = 76.0f;
    ctx.overridesFrame = D2D1::RectF(left, 376.0f, left + fullW, 376.0f + overridesH);

    const float bottomBtnY = 472.0f;
    const float bottomBtnW = 90.0f;
    const float bottomBtnH = 32.0f;
    float cancelX = left + fullW - bottomBtnW;
    float saveX = cancelX - bottomBtnW - 12.0f;

    ctx.btnSave = {L"btn_save", L"", L"Save", D2D1::RectF(saveX, bottomBtnY, saveX + bottomBtnW, bottomBtnY + bottomBtnH), InteractionState::Normal, true};
    ctx.btnCancel = {L"btn_cancel", L"", L"Cancel", D2D1::RectF(cancelX, bottomBtnY, cancelX + bottomBtnW, bottomBtnY + bottomBtnH), InteractionState::Normal, false};

    // Helper to create borderless seamless edit child controls
    auto makeSeamlessEdit = [&](const wchar_t* text, DWORD style, const D2D1_RECT_F& frame, bool multiline) -> HWND {
        float padX = 8.0f;
        float padY = multiline ? 6.0f : 4.0f;
        int x = static_cast<int>((frame.left + padX) * ctx.scale);
        int y = static_cast<int>((frame.top + padY) * ctx.scale);
        int w = static_cast<int>((frame.right - frame.left - padX * 2.0f) * ctx.scale);
        int h = static_cast<int>((frame.bottom - frame.top - padY * 2.0f) * ctx.scale);

        HWND hCtrl = CreateWindowExW(
            0, L"EDIT", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
            x, y, w, h,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
        );

        if (ctx.hFont) {
            SendMessageW(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(ctx.hFont), TRUE);
        }

        if (multiline) {
            SetWindowTheme(hCtrl, L"DarkMode_Explorer", nullptr);
        }

        SetWindowSubclass(hCtrl, SeamlessEditSubclassProc, kEditSubclassId, reinterpret_cast<DWORD_PTR>(&ctx));
        return hCtrl;
    };

    ctx.hEditName = makeSeamlessEdit(appInOut.name.c_str(), ES_AUTOHSCROLL, ctx.nameFrame, false);
    ctx.hEditExe = makeSeamlessEdit(appInOut.executable.c_str(), ES_AUTOHSCROLL, ctx.exeFrame, false);
    ctx.hEditArgs = makeSeamlessEdit(appInOut.arguments.c_str(), ES_AUTOHSCROLL, ctx.argsFrame, false);
    ctx.hEditWorkDir = makeSeamlessEdit(appInOut.workingDirectory.c_str(), ES_AUTOHSCROLL, ctx.workDirFrame, false);

    std::wstring ovFormatted;
    for (const auto& [k, v] : appInOut.environmentOverrides) {
        ovFormatted += k + L"=" + v + L"\r\n";
    }
    ctx.hEditOverrides = makeSeamlessEdit(
        ovFormatted.c_str(),
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        ctx.overridesFrame,
        true
    );

    SetFocus(ctx.hEditName);

    // Modal message loop
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

    if (ctx.hFont) DeleteObject(ctx.hFont);
    if (ctx.hEditBgBrush) DeleteObject(ctx.hEditBgBrush);

    return ctx.saved;
}

} // namespace envlauncher
