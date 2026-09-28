#pragma once

#include <d2d1.h>
#include <string>

namespace envlauncher {

enum class InteractionState {
    Normal,
    Hover,
    Pressed,
    Disabled
};

struct ButtonControl {
    std::wstring id;
    std::wstring targetId;
    std::wstring text;
    D2D1_RECT_F bounds{0, 0, 0, 0};
    InteractionState state{InteractionState::Normal};
    bool isAccent{true};

    bool HitTest(float x, float y) const noexcept {
        return (state != InteractionState::Disabled) &&
               (x >= bounds.left && x <= bounds.right &&
                y >= bounds.top && y <= bounds.bottom);
    }
};

struct AppCardView {
    std::wstring id;
    std::wstring name;
    std::wstring environmentName;
    std::wstring executablePath;
    bool executableExists{true};

    D2D1_RECT_F bounds{0, 0, 0, 0};
    bool isHovered{false};
    ButtonControl launchButton;
    ButtonControl editButton;
    ButtonControl deleteButton;

    bool HitTest(float x, float y) const noexcept {
        return (x >= bounds.left && x <= bounds.right &&
                y >= bounds.top && y <= bounds.bottom);
    }
};

} // namespace envlauncher
