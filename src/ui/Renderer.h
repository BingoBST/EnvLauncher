#pragma once

#include "ui/Theme.h"
#include "ui/Controls.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

namespace envlauncher {

class Renderer {
public:
    Renderer();
    ~Renderer() = default;

    // Initializes Direct2D and DirectWrite factories.
    bool Initialize();

    // Creates or re-creates HWND render target for the specified window and DPI.
    bool CreateDeviceResources(HWND hwnd, UINT dpi = 96);

    // Updates render target DPI upon DPI change.
    void SetDpi(UINT dpi);

    // Releases device-dependent resources when target is lost.
    void DiscardDeviceResources();

    // Resizes render target to match window client dimensions.
    void Resize(UINT width, UINT height);

    // Drawing lifecycle
    bool BeginDraw();
    HRESULT EndDraw();

    // Visual elements
    void ClearBackground();
    void DrawHeader(const std::wstring& title, float clientWidth);
    void DrawHeaderBar(const std::wstring& title, float clientWidth, float headerHeight = 60.0f);
    void DrawSeparator(float y, float clientWidth);
    void DrawSeparator(float x1, float x2, float y);
    void DrawCard(const AppCardView& card);
    void DrawButton(const ButtonControl& button);
    void DrawEmptyState(float clientWidth, float headerHeight, float clientHeight);
    void DrawScrollBar(
        float clientWidth,
        float headerHeight,
        float clientHeight,
        float totalContentHeight,
        float scrollOffset,
        bool isDragging = false
    );

    // Dialog & Control drawing helpers
    void DrawLabel(const std::wstring& text, const D2D1_RECT_F& bounds, D2D1_COLOR_F color);
    void DrawInputBoxFrame(const D2D1_RECT_F& bounds, bool isFocused, bool isHovered = false);
    void DrawDropdownButton(const ButtonControl& button, const std::wstring& text);
    void DrawListItem(
        const D2D1_RECT_F& bounds,
        const std::wstring& title,
        const std::wstring& subtitle,
        bool isSelected,
        bool isHovered
    );

    const Theme& GetTheme() const noexcept { return theme_; }
    void SetTheme(const Theme& theme) noexcept { theme_ = theme; }

    ID2D1HwndRenderTarget* GetRenderTarget() const noexcept { return renderTarget_.Get(); }

private:
    Theme theme_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> cardTitleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> secondaryFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> buttonFormat_;
};

} // namespace envlauncher
