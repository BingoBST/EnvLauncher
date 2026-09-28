#pragma once

#include <d2d1.h>

namespace envlauncher {

struct Theme {
    // Windows 10 / 11 Harmonized Dark Theme palette
    D2D1_COLOR_F background       = D2D1::ColorF(0.086f, 0.086f, 0.098f, 1.0f); // #161619
    D2D1_COLOR_F headerBackground = D2D1::ColorF(0.125f, 0.125f, 0.141f, 1.0f); // #202024
    D2D1_COLOR_F headerBorder     = D2D1::ColorF(0.176f, 0.176f, 0.204f, 1.0f); // #2d2d34
    D2D1_COLOR_F card             = D2D1::ColorF(0.133f, 0.133f, 0.149f, 1.0f); // #222226
    D2D1_COLOR_F cardHover        = D2D1::ColorF(0.165f, 0.165f, 0.188f, 1.0f); // #2a2a30
    D2D1_COLOR_F cardBorder       = D2D1::ColorF(0.200f, 0.200f, 0.231f, 1.0f); // #33333b
    D2D1_COLOR_F textPrimary      = D2D1::ColorF(0.961f, 0.961f, 0.969f, 1.0f); // #f5f5f7
    D2D1_COLOR_F textSecondary    = D2D1::ColorF(0.631f, 0.631f, 0.667f, 1.0f); // #a1a1aa
    D2D1_COLOR_F textMuted        = D2D1::ColorF(0.443f, 0.443f, 0.478f, 1.0f); // #71717a
    D2D1_COLOR_F accent           = D2D1::ColorF(0.000f, 0.471f, 0.831f, 1.0f); // #0078d4
    D2D1_COLOR_F accentHover      = D2D1::ColorF(0.102f, 0.525f, 0.863f, 1.0f); // #1a86dc
    D2D1_COLOR_F accentPressed    = D2D1::ColorF(0.000f, 0.424f, 0.745f, 1.0f); // #006cbe
    D2D1_COLOR_F buttonNormal     = D2D1::ColorF(0.196f, 0.196f, 0.220f, 1.0f); // #323238
    D2D1_COLOR_F buttonHover      = D2D1::ColorF(0.243f, 0.243f, 0.275f, 1.0f); // #3e3e46
    D2D1_COLOR_F buttonPressed    = D2D1::ColorF(0.165f, 0.165f, 0.188f, 1.0f); // #2a2a30
    D2D1_COLOR_F buttonText       = D2D1::ColorF(1.000f, 1.000f, 1.000f, 1.0f);
    D2D1_COLOR_F danger           = D2D1::ColorF(0.937f, 0.267f, 0.267f, 1.0f); // #ef4444
    D2D1_COLOR_F separator        = D2D1::ColorF(0.165f, 0.165f, 0.188f, 1.0f);
};

} // namespace envlauncher
