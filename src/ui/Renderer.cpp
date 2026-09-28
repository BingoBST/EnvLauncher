#include "ui/Renderer.h"
#include <algorithm>

namespace envlauncher {

Renderer::Renderer() = default;

bool Renderer::Initialize() {
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        d2dFactory_.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    const wchar_t* fontFamily = L"Segoe UI";

    // Title format (20 DIPs, Semi-Bold, Centered Vertically)
    dwriteFactory_->CreateTextFormat(
        fontFamily,
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        20.0f,
        L"en-US",
        titleFormat_.GetAddressOf()
    );

    if (titleFormat_) {
        titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Card title format (15 DIPs, Semi-Bold)
    dwriteFactory_->CreateTextFormat(
        fontFamily,
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        15.0f,
        L"en-US",
        cardTitleFormat_.GetAddressOf()
    );

    // Body format (13 DIPs, Regular)
    dwriteFactory_->CreateTextFormat(
        fontFamily,
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"en-US",
        bodyFormat_.GetAddressOf()
    );

    // Secondary text format (11.5 DIPs, Regular)
    dwriteFactory_->CreateTextFormat(
        fontFamily,
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"en-US",
        secondaryFormat_.GetAddressOf()
    );

    // Button format (12.5 DIPs, Medium)
    dwriteFactory_->CreateTextFormat(
        fontFamily,
        nullptr,
        DWRITE_FONT_WEIGHT_MEDIUM,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.5f,
        L"en-US",
        buttonFormat_.GetAddressOf()
    );

    if (cardTitleFormat_) {
        cardTitleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(cardTitleFormat_.Get(), ellipsis.GetAddressOf()))) {
            cardTitleFormat_->SetTrimming(&trimming, ellipsis.Get());
        }
    }

    if (secondaryFormat_) {
        secondaryFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(secondaryFormat_.Get(), ellipsis.GetAddressOf()))) {
            secondaryFormat_->SetTrimming(&trimming, ellipsis.Get());
        }
    }

    if (buttonFormat_) {
        buttonFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        buttonFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    return true;
}

bool Renderer::CreateDeviceResources(HWND hwnd, UINT dpi) {
    if (!renderTarget_) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi),
            static_cast<float>(dpi)
        );
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(hwnd, size);

        HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
            rtProps,
            hwndProps,
            renderTarget_.GetAddressOf()
        );
        if (FAILED(hr)) return false;

        renderTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));

        hr = renderTarget_->CreateSolidColorBrush(
            theme_.textPrimary,
            brush_.GetAddressOf()
        );
        if (FAILED(hr)) return false;
    }
    return true;
}

void Renderer::SetDpi(UINT dpi) {
    if (renderTarget_) {
        renderTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    }
}

void Renderer::DiscardDeviceResources() {
    brush_.Reset();
    renderTarget_.Reset();
}

void Renderer::Resize(UINT width, UINT height) {
    if (renderTarget_) {
        renderTarget_->Resize(D2D1::SizeU(width, height));
    }
}

bool Renderer::BeginDraw() {
    if (renderTarget_) {
        renderTarget_->BeginDraw();
        return true;
    }
    return false;
}

HRESULT Renderer::EndDraw() {
    if (renderTarget_) {
        HRESULT hr = renderTarget_->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            DiscardDeviceResources();
        }
        return hr;
    }
    return S_OK;
}

void Renderer::ClearBackground() {
    if (renderTarget_) {
        renderTarget_->Clear(theme_.background);
    }
}

void Renderer::DrawHeader(const std::wstring& title, float clientWidth) {
    DrawHeaderBar(title, clientWidth, 60.0f);
}

void Renderer::DrawHeaderBar(const std::wstring& title, float clientWidth, float headerHeight) {
    if (!renderTarget_ || !brush_) return;

    // 1. Draw structured App Bar background
    brush_->SetColor(theme_.headerBackground);
    renderTarget_->FillRectangle(
        D2D1::RectF(0.0f, 0.0f, clientWidth, headerHeight),
        brush_.Get()
    );

    // 2. Draw subtle hairline bottom border
    brush_->SetColor(theme_.headerBorder);
    renderTarget_->DrawLine(
        D2D1::Point2F(0.0f, headerHeight),
        D2D1::Point2F(clientWidth, headerHeight),
        brush_.Get(),
        1.0f
    );

    // 3. Draw Title text centered vertically in header bar
    if (titleFormat_) {
        brush_->SetColor(theme_.textPrimary);
        D2D1_RECT_F rect = D2D1::RectF(28.0f, 0.0f, clientWidth - 180.0f, headerHeight);
        renderTarget_->DrawText(
            title.c_str(),
            static_cast<UINT32>(title.length()),
            titleFormat_.Get(),
            rect,
            brush_.Get()
        );
    }
}

void Renderer::DrawSeparator(float y, float clientWidth) {
    DrawSeparator(28.0f, clientWidth - 28.0f, y);
}

void Renderer::DrawSeparator(float x1, float x2, float y) {
    if (!renderTarget_ || !brush_) return;

    brush_->SetColor(theme_.separator);
    renderTarget_->DrawLine(
        D2D1::Point2F(x1, y),
        D2D1::Point2F(x2, y),
        brush_.Get(),
        1.0f
    );
}

void Renderer::DrawCard(const AppCardView& card) {
    if (!renderTarget_ || !brush_) return;

    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(card.bounds, 8.0f, 8.0f);

    // Card background fill
    brush_->SetColor(card.isHovered ? theme_.cardHover : theme_.card);
    renderTarget_->FillRoundedRectangle(rrect, brush_.Get());

    // Card border stroke
    brush_->SetColor(theme_.cardBorder);
    renderTarget_->DrawRoundedRectangle(rrect, brush_.Get(), 1.0f);

    const float left = card.bounds.left + 16.0f;
    const float contentRight = card.deleteButton.bounds.left - 12.0f;

    // 1. Application name
    if (cardTitleFormat_) {
        brush_->SetColor(theme_.textPrimary);
        D2D1_RECT_F nameRect = D2D1::RectF(left, card.bounds.top + 14.0f, contentRight, card.bounds.top + 36.0f);
        renderTarget_->DrawText(
            card.name.c_str(),
            static_cast<UINT32>(card.name.length()),
            cardTitleFormat_.Get(),
            nameRect,
            brush_.Get()
        );
    }

    // 2. Environment profile name
    if (secondaryFormat_) {
        brush_->SetColor(theme_.textSecondary);
        std::wstring envText = L"Environment: " + (card.environmentName.empty() ? L"(None)" : card.environmentName);
        D2D1_RECT_F envRect = D2D1::RectF(left, card.bounds.top + 40.0f, contentRight, card.bounds.top + 60.0f);
        renderTarget_->DrawText(
            envText.c_str(),
            static_cast<UINT32>(envText.length()),
            secondaryFormat_.Get(),
            envRect,
            brush_.Get()
        );
    }

    // 3. Executable path or missing error
    if (secondaryFormat_) {
        D2D1_RECT_F pathRect = D2D1::RectF(left, card.bounds.top + 64.0f, contentRight, card.bounds.top + 90.0f);
        if (card.executableExists) {
            brush_->SetColor(theme_.textMuted);
            renderTarget_->DrawText(
                card.executablePath.c_str(),
                static_cast<UINT32>(card.executablePath.length()),
                secondaryFormat_.Get(),
                pathRect,
                brush_.Get()
            );
        } else {
            brush_->SetColor(theme_.danger);
            std::wstring warnText = L"Executable not found: " + card.executablePath;
            renderTarget_->DrawText(
                warnText.c_str(),
                static_cast<UINT32>(warnText.length()),
                secondaryFormat_.Get(),
                pathRect,
                brush_.Get()
            );
        }
    }

    // 4. Action buttons
    DrawButton(card.deleteButton);
    DrawButton(card.editButton);
    DrawButton(card.launchButton);
}

void Renderer::DrawButton(const ButtonControl& button) {
    if (!renderTarget_ || !brush_ || !buttonFormat_) return;

    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(button.bounds, 5.0f, 5.0f);

    // Determine background color
    D2D1_COLOR_F bgColor;
    D2D1_COLOR_F textColor = theme_.buttonText;

    if (button.state == InteractionState::Disabled) {
        bgColor = D2D1::ColorF(0.20f, 0.20f, 0.22f, 0.6f);
        textColor = theme_.textMuted;
    } else if (button.isAccent) {
        switch (button.state) {
            case InteractionState::Hover:   bgColor = theme_.accentHover; break;
            case InteractionState::Pressed: bgColor = theme_.accentPressed; break;
            default:                        bgColor = theme_.accent; break;
        }
    } else {
        switch (button.state) {
            case InteractionState::Hover:   bgColor = theme_.buttonHover; break;
            case InteractionState::Pressed: bgColor = theme_.buttonPressed; break;
            default:                        bgColor = theme_.buttonNormal; break;
        }
    }

    // Fill button
    brush_->SetColor(bgColor);
    renderTarget_->FillRoundedRectangle(rrect, brush_.Get());

    // Outline for non-accent buttons
    if (!button.isAccent && button.state != InteractionState::Disabled) {
        brush_->SetColor(theme_.cardBorder);
        renderTarget_->DrawRoundedRectangle(rrect, brush_.Get(), 1.0f);
    }

    // Draw text centered
    brush_->SetColor(textColor);
    renderTarget_->DrawText(
        button.text.c_str(),
        static_cast<UINT32>(button.text.length()),
        buttonFormat_.Get(),
        button.bounds,
        brush_.Get()
    );
}

void Renderer::DrawEmptyState(float clientWidth, float headerHeight, float clientHeight) {
    (void)clientHeight;
    if (!renderTarget_ || !brush_) return;

    const float marginX = 28.0f;
    const float contentWidth = std::max(200.0f, clientWidth - marginX * 2.0f);
    const float startY = headerHeight + 20.0f;
    const float boxHeight = 120.0f;

    D2D1_RECT_F boxRect = D2D1::RectF(marginX, startY, marginX + contentWidth, startY + boxHeight);
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(boxRect, 8.0f, 8.0f);

    // Subtle background
    brush_->SetColor(theme_.card);
    renderTarget_->FillRoundedRectangle(rrect, brush_.Get());

    brush_->SetColor(theme_.cardBorder);
    renderTarget_->DrawRoundedRectangle(rrect, brush_.Get(), 1.0f);

    // 1. Title
    if (cardTitleFormat_) {
        brush_->SetColor(theme_.textPrimary);
        D2D1_RECT_F titleRect = D2D1::RectF(marginX + 20.0f, startY + 22.0f, marginX + contentWidth - 20.0f, startY + 48.0f);
        const std::wstring titleText = L"No Applications Configured";
        renderTarget_->DrawText(
            titleText.c_str(),
            static_cast<UINT32>(titleText.length()),
            cardTitleFormat_.Get(),
            titleRect,
            brush_.Get()
        );
    }

    // 2. Explanatory hint
    if (bodyFormat_) {
        brush_->SetColor(theme_.textSecondary);
        D2D1_RECT_F hintRect = D2D1::RectF(marginX + 20.0f, startY + 54.0f, marginX + contentWidth - 20.0f, startY + 78.0f);
        const std::wstring hintText = L"Click [+ Add Application] in the top toolbar to configure your first application.";
        renderTarget_->DrawText(
            hintText.c_str(),
            static_cast<UINT32>(hintText.length()),
            bodyFormat_.Get(),
            hintRect,
            brush_.Get()
        );
    }

    // 3. Sub-hint
    if (secondaryFormat_) {
        brush_->SetColor(theme_.textMuted);
        D2D1_RECT_F subRect = D2D1::RectF(marginX + 20.0f, startY + 80.0f, marginX + contentWidth - 20.0f, startY + 104.0f);
        const std::wstring subText = L"Or click [Manage Environments] below to set up reusable environment profiles.";
        renderTarget_->DrawText(
            subText.c_str(),
            static_cast<UINT32>(subText.length()),
            secondaryFormat_.Get(),
            subRect,
            brush_.Get()
        );
    }
}

void Renderer::DrawScrollBar(
    float clientWidth,
    float headerHeight,
    float clientHeight,
    float totalContentHeight,
    float scrollOffset,
    bool isDragging
) {
    if (!renderTarget_ || !brush_) return;
    if (totalContentHeight <= clientHeight) return;

    const float trackTop = headerHeight + 2.0f;
    const float trackBottom = clientHeight - 2.0f;
    const float trackHeight = trackBottom - trackTop;
    if (trackHeight <= 24.0f) return;

    const float trackWidth = 5.0f;
    const float trackRight = clientWidth - 3.0f;
    const float trackLeft = trackRight - trackWidth;

    float thumbHeight = std::max(30.0f, (clientHeight / totalContentHeight) * trackHeight);
    float maxScroll = totalContentHeight - clientHeight;
    float scrollFraction = std::clamp(scrollOffset / maxScroll, 0.0f, 1.0f);
    float thumbTop = trackTop + scrollFraction * (trackHeight - thumbHeight);
    float thumbBottom = thumbTop + thumbHeight;

    D2D1_ROUNDED_RECT thumbRect = D2D1::RoundedRect(
        D2D1::RectF(trackLeft, thumbTop, trackRight, thumbBottom),
        2.5f, 2.5f
    );

    // Subtle modern dark scroll indicator thumb
    D2D1_COLOR_F thumbColor = isDragging
        ? D2D1::ColorF(0.55f, 0.55f, 0.60f, 0.85f)
        : D2D1::ColorF(0.35f, 0.35f, 0.40f, 0.55f);

    brush_->SetColor(thumbColor);
    renderTarget_->FillRoundedRectangle(thumbRect, brush_.Get());
}

void Renderer::DrawLabel(const std::wstring& text, const D2D1_RECT_F& bounds, D2D1_COLOR_F color) {
    if (!renderTarget_ || !brush_ || !bodyFormat_) return;
    brush_->SetColor(color);
    renderTarget_->DrawText(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        bodyFormat_.Get(),
        bounds,
        brush_.Get()
    );
}

void Renderer::DrawInputBoxFrame(const D2D1_RECT_F& bounds, bool isFocused, bool isHovered) {
    if (!renderTarget_ || !brush_) return;
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(bounds, 6.0f, 6.0f);
    // Fill background
    brush_->SetColor(theme_.card);
    renderTarget_->FillRoundedRectangle(rrect, brush_.Get());

    // Border stroke
    if (isFocused) {
        brush_->SetColor(theme_.accent);
        renderTarget_->DrawRoundedRectangle(rrect, brush_.Get(), 1.5f);
    } else if (isHovered) {
        brush_->SetColor(theme_.textMuted);
        renderTarget_->DrawRoundedRectangle(rrect, brush_.Get(), 1.0f);
    } else {
        brush_->SetColor(theme_.cardBorder);
        renderTarget_->DrawRoundedRectangle(rrect, brush_.Get(), 1.0f);
    }
}

void Renderer::DrawDropdownButton(const ButtonControl& button, const std::wstring& text) {
    if (!renderTarget_ || !brush_ || !bodyFormat_) return;
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(button.bounds, 6.0f, 6.0f);

    D2D1_COLOR_F bgColor;
    switch (button.state) {
        case InteractionState::Hover:   bgColor = theme_.cardHover; break;
        case InteractionState::Pressed: bgColor = theme_.buttonPressed; break;
        default:                        bgColor = theme_.card; break;
    }
    brush_->SetColor(bgColor);
    renderTarget_->FillRoundedRectangle(rrect, brush_.Get());

    brush_->SetColor(button.state == InteractionState::Hover ? theme_.textMuted : theme_.cardBorder);
    renderTarget_->DrawRoundedRectangle(rrect, brush_.Get(), 1.0f);

    // Text
    brush_->SetColor(theme_.textPrimary);
    D2D1_RECT_F textRect = D2D1::RectF(
        button.bounds.left + 12.0f,
        button.bounds.top,
        button.bounds.right - 28.0f,
        button.bounds.bottom
    );
    renderTarget_->DrawText(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        bodyFormat_.Get(),
        textRect,
        brush_.Get()
    );

    // Down arrow glyph (▼)
    const wchar_t arrow[] = L"\u25BE";
    D2D1_RECT_F arrowRect = D2D1::RectF(
        button.bounds.right - 26.0f,
        button.bounds.top,
        button.bounds.right - 8.0f,
        button.bounds.bottom
    );
    brush_->SetColor(theme_.textSecondary);
    renderTarget_->DrawText(
        arrow,
        1,
        bodyFormat_.Get(),
        arrowRect,
        brush_.Get()
    );
}

void Renderer::DrawListItem(
    const D2D1_RECT_F& bounds,
    const std::wstring& title,
    const std::wstring& subtitle,
    bool isSelected,
    bool isHovered
) {
    if (!renderTarget_ || !brush_ || !bodyFormat_ || !secondaryFormat_) return;

    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(bounds, 6.0f, 6.0f);

    if (isSelected) {
        brush_->SetColor(theme_.accent);
        renderTarget_->FillRoundedRectangle(rrect, brush_.Get());
    } else if (isHovered) {
        brush_->SetColor(theme_.cardHover);
        renderTarget_->FillRoundedRectangle(rrect, brush_.Get());
        brush_->SetColor(theme_.cardBorder);
        renderTarget_->DrawRoundedRectangle(rrect, brush_.Get(), 1.0f);
    } else {
        brush_->SetColor(theme_.card);
        renderTarget_->FillRoundedRectangle(rrect, brush_.Get());
        brush_->SetColor(theme_.cardBorder);
        renderTarget_->DrawRoundedRectangle(rrect, brush_.Get(), 1.0f);
    }

    // Title
    brush_->SetColor(isSelected ? theme_.buttonText : theme_.textPrimary);
    D2D1_RECT_F titleRect = D2D1::RectF(
        bounds.left + 12.0f,
        bounds.top + 6.0f,
        bounds.right - 12.0f,
        bounds.top + 26.0f
    );
    renderTarget_->DrawText(
        title.c_str(),
        static_cast<UINT32>(title.length()),
        bodyFormat_.Get(),
        titleRect,
        brush_.Get()
    );

    // Subtitle
    if (!subtitle.empty()) {
        brush_->SetColor(isSelected ? D2D1::ColorF(0.9f, 0.9f, 1.0f, 0.9f) : theme_.textSecondary);
        D2D1_RECT_F subRect = D2D1::RectF(
            bounds.left + 12.0f,
            bounds.top + 26.0f,
            bounds.right - 12.0f,
            bounds.bottom - 4.0f
        );
        renderTarget_->DrawText(
            subtitle.c_str(),
            static_cast<UINT32>(subtitle.length()),
            secondaryFormat_.Get(),
            subRect,
            brush_.Get()
        );
    }
}

} // namespace envlauncher
