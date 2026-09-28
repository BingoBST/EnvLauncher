#include "ui/ProfileEditDialog.h"
#include "ui/Renderer.h"
#include "platform/DpiHelper.h"
#include "platform/DwmHelper.h"
#include "util/StringUtils.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <sstream>
#include <set>
#include <algorithm>

namespace envlauncher {

static constexpr UINT_PTR kProfileEditSubclassId = 5002;

struct ProfileEditContext {
    EnvironmentProfile* profile{nullptr};
    bool isNew{false};
    bool saved{false};
    UINT dpi{96};
    float scale{1.0f};

    Renderer renderer;
    HFONT hFont{nullptr};
    HBRUSH hEditBgBrush{nullptr};

    HWND hwnd{nullptr};
    HWND hEditName{nullptr};
    HWND hEditVars{nullptr};
    HWND focusedEdit{nullptr};

    // D2D Frame Bounds (in DIPs)
    D2D1_RECT_F nameFrame{};
    D2D1_RECT_F varsFrame{};

    // D2D Buttons
    ButtonControl btnSave;
    ButtonControl btnCancel;
    ButtonControl* pressedBtn{nullptr};
    bool mouseTracking{false};

    float dlgWidth{500.0f};
    float dlgHeight{400.0f};
};

static std::wstring TrimStr(const std::wstring& s) {
    auto start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    auto end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

static LRESULT CALLBACK SeamlessProfileEditSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
{
    auto* ctx = reinterpret_cast<ProfileEditContext*>(dwRefData);

    switch (uMsg) {
        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hWnd, &rc);
            if (ctx && ctx->hEditBgBrush) {
                FillRect(hdc, &rc, ctx->hEditBgBrush);
            }
            return 1; // Suppress white flash
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
            RemoveWindowSubclass(hWnd, SeamlessProfileEditSubclassProc, kProfileEditSubclassId);
            break;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void HandleProfileSave(HWND hwnd, ProfileEditContext* ctx) {
    // 1. Validate Name
    int nameLen = GetWindowTextLengthW(ctx->hEditName);
    std::wstring name(nameLen + 1, L'\0');
    GetWindowTextW(ctx->hEditName, name.data(), nameLen + 1);
    name.resize(nameLen);
    name = TrimStr(name);

    if (name.empty()) {
        MessageBoxW(hwnd, L"Profile name cannot be empty.", L"Validation Error", MB_OK | MB_ICONWARNING);
        SetFocus(ctx->hEditName);
        return;
    }

    // 2. Parse Variables
    int varsLen = GetWindowTextLengthW(ctx->hEditVars);
    std::wstring varsText(varsLen + 1, L'\0');
    GetWindowTextW(ctx->hEditVars, varsText.data(), varsLen + 1);
    varsText.resize(varsLen);
    EnvMap variables;
    std::set<std::wstring, CaseInsensitiveLess> seenKeys;
    std::wstringstream ss(varsText);
    std::wstring line;

    while (std::getline(ss, line)) {
        std::wstring trimmed = TrimStr(line);
        if (trimmed.empty() || trimmed[0] == L'#') continue;

        size_t eqPos = trimmed.find(L'=');
        if (eqPos == std::wstring::npos || eqPos == 0) {
            std::wstring err = L"Invalid variable format:\n\"" + trimmed +
                               L"\"\n\nEach line must follow the format KEY=VALUE.";
            MessageBoxW(hwnd, err.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
            SetFocus(ctx->hEditVars);
            return;
        }

        std::wstring key = TrimStr(trimmed.substr(0, eqPos));
        std::wstring val = trimmed.substr(eqPos + 1);

        if (key.find(L'=') != std::wstring::npos) {
            MessageBoxW(hwnd, L"Variable name cannot contain '='.", L"Validation Error", MB_OK | MB_ICONWARNING);
            SetFocus(ctx->hEditVars);
            return;
        }

        if (!seenKeys.insert(key).second) {
            std::wstring err = L"Duplicate environment variable key '" + key + L"' in profile.";
            MessageBoxW(hwnd, err.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
            SetFocus(ctx->hEditVars);
            return;
        }

        variables[key] = val;
    }

    if (ctx->isNew && ctx->profile->id.empty()) {
        ctx->profile->id = L"profile_" + std::to_wstring(GetTickCount64());
    }
    ctx->profile->name = std::move(name);
    ctx->profile->variables = std::move(variables);

    ctx->saved = true;
    DestroyWindow(hwnd);
}

static LRESULT CALLBACK ProfileEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<ProfileEditContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

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
            return 1; // Suppress flicker, D2D paints background

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
                        ctx->isNew ? L"Add Environment Profile" : L"Edit Environment Profile",
                        ctx->dlgWidth,
                        headerH
                    );

                    // 2. Labels
                    ctx->renderer.DrawLabel(L"Profile Name:", D2D1::RectF(24.0f, 66.0f, 476.0f, 84.0f), theme.textSecondary);
                    ctx->renderer.DrawLabel(L"Variables (KEY=VALUE per line):", D2D1::RectF(24.0f, 128.0f, 476.0f, 146.0f), theme.textSecondary);

                    // 3. Input Frames
                    ctx->renderer.DrawInputBoxFrame(ctx->nameFrame, ctx->focusedEdit == ctx->hEditName);
                    ctx->renderer.DrawInputBoxFrame(ctx->varsFrame, ctx->focusedEdit == ctx->hEditVars);

                    // 4. Buttons
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

                if (clicked == &ctx->btnSave) {
                    HandleProfileSave(hwnd, ctx);
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
                if (ctx) HandleProfileSave(hwnd, ctx);
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

bool ProfileEditDialog::Show(
    HWND parent,
    EnvironmentProfile& profileInOut,
    bool isNew
) {
    UINT dpi = DpiHelper::GetSystemDpi();
    if (parent) {
        dpi = DpiHelper::GetWindowDpi(parent);
    }

    ProfileEditContext ctx;
    ctx.profile = &profileInOut;
    ctx.isNew = isNew;
    ctx.dpi = dpi;
    ctx.scale = static_cast<float>(dpi) / 96.0f;
    ctx.hEditBgBrush = CreateSolidBrush(RGB(34, 34, 38)); // Matches theme.card

    if (!ctx.renderer.Initialize()) {
        DeleteObject(ctx.hEditBgBrush);
        return false;
    }

    int fontHeight = -MulDiv(10, static_cast<int>(dpi), 72);
    ctx.hFont = CreateFontW(
        fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    const wchar_t* kClassName = L"EnvLauncherD2DProfileEditDlgClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ProfileEditWndProc;
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
        isNew ? L"Add Environment Profile" : L"Edit Environment Profile",
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

    // DIP coordinates
    const float left = 24.0f;
    const float fullW = 452.0f;
    const float editH = 28.0f;

    ctx.nameFrame = D2D1::RectF(left, 86.0f, left + fullW, 86.0f + editH);

    const float varsH = 170.0f;
    ctx.varsFrame = D2D1::RectF(left, 148.0f, left + fullW, 148.0f + varsH);

    const float bottomBtnY = 336.0f;
    const float bottomBtnW = 90.0f;
    const float bottomBtnH = 32.0f;
    float cancelX = left + fullW - bottomBtnW;
    float saveX = cancelX - bottomBtnW - 12.0f;

    ctx.btnSave = {L"btn_save", L"", L"Save", D2D1::RectF(saveX, bottomBtnY, saveX + bottomBtnW, bottomBtnY + bottomBtnH), InteractionState::Normal, true};
    ctx.btnCancel = {L"btn_cancel", L"", L"Cancel", D2D1::RectF(cancelX, bottomBtnY, cancelX + bottomBtnW, bottomBtnY + bottomBtnH), InteractionState::Normal, false};

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

        SetWindowSubclass(hCtrl, SeamlessProfileEditSubclassProc, kProfileEditSubclassId, reinterpret_cast<DWORD_PTR>(&ctx));
        return hCtrl;
    };

    ctx.hEditName = makeSeamlessEdit(profileInOut.name.c_str(), ES_AUTOHSCROLL, ctx.nameFrame, false);

    std::wstring varsFormatted;
    for (const auto& [k, v] : profileInOut.variables) {
        varsFormatted += k + L"=" + v + L"\r\n";
    }
    ctx.hEditVars = makeSeamlessEdit(
        varsFormatted.c_str(),
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        ctx.varsFrame,
        true
    );

    SetFocus(ctx.hEditName);

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
