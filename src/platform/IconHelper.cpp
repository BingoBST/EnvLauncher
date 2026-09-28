#include "platform/IconHelper.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

namespace envlauncher {

static float DistToSegment(float px, float py, float ax, float ay, float bx, float by) {
    float pax = px - ax;
    float pay = py - ay;
    float bax = bx - ax;
    float bay = by - ay;
    float lenSq = bax * bax + bay * bay;
    if (lenSq < 0.0001f) {
        return std::sqrt(pax * pax + pay * pay);
    }
    float t = std::clamp((pax * bax + pay * bay) / lenSq, 0.0f, 1.0f);
    float dx = pax - bax * t;
    float dy = pay - bay * t;
    return std::sqrt(dx * dx + dy * dy);
}

static float DistToRoundedBox(float px, float py, float cx, float cy, float halfW, float halfH, float radius) {
    float qx = std::abs(px - cx) - (halfW - radius);
    float qy = std::abs(py - cy) - (halfH - radius);
    float ox = std::max(qx, 0.0f);
    float oy = std::max(qy, 0.0f);
    float outsideDist = std::sqrt(ox * ox + oy * oy);
    float insideDist = std::min(std::max(qx, qy), 0.0f);
    return outsideDist + insideDist - radius;
}

HICON IconHelper::CreateAppIcon(int size) {
    if (size <= 0) return nullptr;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size; // Top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    uint32_t* pixels = nullptr;
    HDC hdc = GetDC(nullptr);
    if (!hdc) return nullptr;

    HBITMAP hbmColor = CreateDIBSection(
        hdc,
        &bi,
        DIB_RGB_COLORS,
        reinterpret_cast<void**>(&pixels),
        nullptr,
        0
    );
    ReleaseDC(nullptr, hdc);

    if (!hbmColor || !pixels) {
        if (hbmColor) DeleteObject(hbmColor);
        return nullptr;
    }

    const float cx = (size - 1) * 0.5f;
    const float cy = (size - 1) * 0.5f;
    const float halfDim = size * 0.44f;
    const float cornerRadius = size * 0.20f;
    const float strokeRadius = std::max(0.85f, size * 0.045f);

    // Chevron points: >
    const float chevronTopX = cx - size * 0.20f;
    const float chevronTopY = cy - size * 0.20f;
    const float chevronMidX = cx - size * 0.01f;
    const float chevronMidY = cy;
    const float chevronBotX = cx - size * 0.20f;
    const float chevronBotY = cy + size * 0.20f;

    // Underscore points: _
    const float barLeftX = cx + size * 0.06f;
    const float barRightX = cx + size * 0.26f;
    const float barY = cy + size * 0.20f;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            // 1. Badge SDF and coverage
            float boxDist = DistToRoundedBox(fx, fy, cx, cy, halfDim, halfDim, cornerRadius);
            float badgeCoverage = std::clamp(0.5f - boxDist, 0.0f, 1.0f);

            if (badgeCoverage <= 0.001f) {
                pixels[y * size + x] = 0;
                continue;
            }

            // Subtle vertical gradient on badge surface
            float tY = std::clamp((fy - (cy - halfDim)) / (halfDim * 2.0f), 0.0f, 1.0f);
            float bgR = (36.0f * (1.0f - tY) + 24.0f * tY) / 255.0f;
            float bgG = (36.0f * (1.0f - tY) + 24.0f * tY) / 255.0f;
            float bgB = (42.0f * (1.0f - tY) + 28.0f * tY) / 255.0f;

            // Badge border stroke (1px subtle highlight around edge)
            if (boxDist > -1.2f && boxDist <= 0.0f) {
                float borderFactor = std::clamp((boxDist + 1.2f) / 1.2f, 0.0f, 1.0f);
                bgR = bgR * (1.0f - borderFactor * 0.5f) + (65.0f / 255.0f) * (borderFactor * 0.5f);
                bgG = bgG * (1.0f - borderFactor * 0.5f) + (65.0f / 255.0f) * (borderFactor * 0.5f);
                bgB = bgB * (1.0f - borderFactor * 0.5f) + (75.0f / 255.0f) * (borderFactor * 0.5f);
            }

            // 2. Chevron SDF
            float d1 = DistToSegment(fx, fy, chevronTopX, chevronTopY, chevronMidX, chevronMidY);
            float d2 = DistToSegment(fx, fy, chevronMidX, chevronMidY, chevronBotX, chevronBotY);
            float chevronDist = std::min(d1, d2) - strokeRadius;
            float chevronCoverage = std::clamp(0.5f - chevronDist, 0.0f, 1.0f);

            // 3. Underscore SDF
            float barDist = DistToSegment(fx, fy, barLeftX, barY, barRightX, barY) - strokeRadius;
            float barCoverage = std::clamp(0.5f - barDist, 0.0f, 1.0f);

            // Composite colors:
            // Chevron: Vibrant Electric Blue (59, 130, 246)
            // Underscore: Bright Cyan Accent (96, 165, 250)
            float finalR = bgR;
            float finalG = bgG;
            float finalB = bgB;

            if (chevronCoverage > 0.0f) {
                float fgR = 59.0f / 255.0f;
                float fgG = 130.0f / 255.0f;
                float fgB = 246.0f / 255.0f;
                finalR = finalR * (1.0f - chevronCoverage) + fgR * chevronCoverage;
                finalG = finalG * (1.0f - chevronCoverage) + fgG * chevronCoverage;
                finalB = finalB * (1.0f - chevronCoverage) + fgB * chevronCoverage;
            }

            if (barCoverage > 0.0f) {
                float fgR = 96.0f / 255.0f;
                float fgG = 165.0f / 255.0f;
                float fgB = 250.0f / 255.0f;
                finalR = finalR * (1.0f - barCoverage) + fgR * barCoverage;
                finalG = finalG * (1.0f - barCoverage) + fgG * barCoverage;
                finalB = finalB * (1.0f - barCoverage) + fgB * barCoverage;
            }

            // Output premultiplied ARGB
            uint32_t a = static_cast<uint32_t>(badgeCoverage * 255.0f + 0.5f);
            uint32_t r = static_cast<uint32_t>(finalR * badgeCoverage * 255.0f + 0.5f);
            uint32_t g = static_cast<uint32_t>(finalG * badgeCoverage * 255.0f + 0.5f);
            uint32_t b = static_cast<uint32_t>(finalB * badgeCoverage * 255.0f + 0.5f);

            pixels[y * size + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, nullptr);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    if (hbmMask) DeleteObject(hbmMask);

    return hIcon;
}

} // namespace envlauncher
