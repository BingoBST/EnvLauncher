#include "platform/DpiHelper.h"
#include "platform/DwmHelper.h"
#include "platform/IconHelper.h"
#include "ui/Theme.h"
#include "ui/Controls.h"
#include "ui/Layout.h"
#include "ui/Renderer.h"

#include <windows.h>
#include <iostream>
#include <vector>
#include <cmath>

using namespace envlauncher;

static void PrintTestHeader(const char* name) {
    std::cout << "[TEST] " << name << " ... ";
}

static void PrintTestPass() {
    std::cout << "PASS" << std::endl;
}

static void PrintTestFail(const char* reason) {
    std::cout << "FAIL: " << reason << std::endl;
}

// Test 1: DpiHelper scaling mathematics
static bool Test_DpiHelper_Scaling() {
    PrintTestHeader("1. DpiHelper scaling");

    float dip = 100.0f;
    float px96 = DpiHelper::Scale(dip, 96);
    float px144 = DpiHelper::Scale(dip, 144);
    float px192 = DpiHelper::Scale(dip, 192);

    if (std::abs(px96 - 100.0f) > 0.001f ||
        std::abs(px144 - 150.0f) > 0.001f ||
        std::abs(px192 - 200.0f) > 0.001f) {
        PrintTestFail("Scale math incorrect");
        return false;
    }

    float unscaled = DpiHelper::Unscale(px144, 144);
    if (std::abs(unscaled - 100.0f) > 0.001f) {
        PrintTestFail("Unscale math incorrect");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 2: DpiHelper system DPI query
static bool Test_DpiHelper_GetSystemDpi() {
    PrintTestHeader("2. DpiHelper system DPI");

    UINT dpi = DpiHelper::GetSystemDpi();
    if (dpi < 96 || dpi > 480) {
        PrintTestFail("System DPI is outside expected range [96, 480]");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 3: DwmHelper window attribute calls
static bool Test_DwmHelper_Calls() {
    PrintTestHeader("3. DwmHelper calls");

    HWND hwnd = CreateWindowExW(
        0, L"STATIC", L"DwmTestWindow", WS_POPUP,
        0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hwnd) {
        PrintTestFail("Failed to create dummy window");
        return false;
    }

    // Call DwmHelper methods (must not crash)
    DwmHelper::EnableDarkMode(hwnd);
    DwmHelper::EnableRoundedCorners(hwnd);
    DwmHelper::SetTitleBarColor(hwnd, RGB(18, 18, 20), RGB(234, 234, 235));

    DestroyWindow(hwnd);
    PrintTestPass();
    return true;
}

// Test 4: IconHelper generation
static bool Test_IconHelper_Creation() {
    PrintTestHeader("4. IconHelper generation");

    HICON hSmall = IconHelper::CreateAppIcon(16);
    if (!hSmall) {
        PrintTestFail("Failed to create 16x16 icon");
        return false;
    }

    HICON hBig = IconHelper::CreateAppIcon(32);
    if (!hBig) {
        DestroyIcon(hSmall);
        PrintTestFail("Failed to create 32x32 icon");
        return false;
    }

    HICON hZero = IconHelper::CreateAppIcon(0);
    if (hZero != nullptr) {
        DestroyIcon(hSmall);
        DestroyIcon(hBig);
        PrintTestFail("CreateAppIcon(0) should return nullptr");
        return false;
    }

    DestroyIcon(hSmall);
    DestroyIcon(hBig);
    PrintTestPass();
    return true;
}

// Test 5: Theme palette color values and contrast
static bool Test_Theme_Colors() {
    PrintTestHeader("5. Theme color palette");

    Theme theme;

    auto checkColor = [](const D2D1_COLOR_F& c) {
        return c.r >= 0.0f && c.r <= 1.0f &&
               c.g >= 0.0f && c.g <= 1.0f &&
               c.b >= 0.0f && c.b <= 1.0f &&
               c.a >= 0.0f && c.a <= 1.0f;
    };

    if (!checkColor(theme.background) ||
        !checkColor(theme.headerBackground) ||
        !checkColor(theme.headerBorder) ||
        !checkColor(theme.card) ||
        !checkColor(theme.cardHover) ||
        !checkColor(theme.textPrimary) ||
        !checkColor(theme.accent) ||
        !checkColor(theme.danger)) {
        PrintTestFail("Theme contains invalid normalized color values");
        return false;
    }

    // Card should be elevated (brighter) compared to background
    float bgLum = (theme.background.r + theme.background.g + theme.background.b) / 3.0f;
    float cardLum = (theme.card.r + theme.card.g + theme.card.b) / 3.0f;

    if (cardLum <= bgLum) {
        PrintTestFail("Card surface is not elevated relative to background");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 6: Layout calculations and positioning
static bool Test_Layout_Calculations() {
    PrintTestHeader("6. Layout calculations");

    ButtonControl addBtn;
    ButtonControl manageBtn;
    std::vector<AppCardView> cards;

    AppCardView c1; c1.id = L"1"; c1.name = L"App 1"; c1.executableExists = true; cards.push_back(c1);
    AppCardView c2; c2.id = L"2"; c2.name = L"App 2"; c2.executableExists = false; cards.push_back(c2);

    float totalHeight = 0.0f;
    float maxScroll = 0.0f;

    Layout::CalculateLayout(
        800.0f, 600.0f, 0.0f,
        addBtn, cards, manageBtn,
        totalHeight, maxScroll
    );

    // Verify Add Button bounds
    if (addBtn.bounds.right <= addBtn.bounds.left || addBtn.bounds.bottom <= addBtn.bounds.top) {
        PrintTestFail("Add button has invalid dimensions");
        return false;
    }

    // Verify Cards and their internal Launch button bounds
    for (const auto& card : cards) {
        if (card.bounds.right <= card.bounds.left || card.bounds.bottom <= card.bounds.top) {
            PrintTestFail("Card has invalid dimensions");
            return false;
        }

        // Launch button must be contained inside card bounds
        if (card.launchButton.bounds.right > card.bounds.right ||
            card.launchButton.bounds.bottom > card.bounds.bottom ||
            card.launchButton.bounds.left < card.bounds.left ||
            card.launchButton.bounds.top < card.bounds.top) {
            PrintTestFail("Launch button overflows card bounds");
            return false;
        }
    }

    // Missing exe should disable launch button
    if (cards[1].launchButton.state != InteractionState::Disabled) {
        PrintTestFail("Missing executable did not disable launch button");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 7: Controls hit testing
static bool Test_Controls_HitTest() {
    PrintTestHeader("7. Controls hit testing");

    ButtonControl btn;
    btn.bounds = D2D1::RectF(100.0f, 100.0f, 200.0f, 140.0f);
    btn.state = InteractionState::Normal;

    if (!btn.HitTest(150.0f, 120.0f)) {
        PrintTestFail("Point inside button returned false");
        return false;
    }

    if (btn.HitTest(50.0f, 50.0f) || btn.HitTest(250.0f, 120.0f)) {
        PrintTestFail("Point outside button returned true");
        return false;
    }

    btn.state = InteractionState::Disabled;
    if (btn.HitTest(150.0f, 120.0f)) {
        PrintTestFail("Disabled button returned true on HitTest");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 8: Direct2D & DirectWrite renderer initialization and DPI configuration
static bool Test_Renderer_Initialization() {
    PrintTestHeader("8. Renderer initialization and DPI");

    Renderer renderer;
    if (!renderer.Initialize()) {
        PrintTestFail("Renderer::Initialize failed");
        return false;
    }

    HWND hwnd = CreateWindowExW(
        0, L"STATIC", L"RendererTestWindow", WS_POPUP,
        0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hwnd) {
        PrintTestFail("Failed to create test window");
        return false;
    }

    if (!renderer.CreateDeviceResources(hwnd, 192)) {
        DestroyWindow(hwnd);
        PrintTestFail("CreateDeviceResources failed");
        return false;
    }

    renderer.SetDpi(144);

    if (!renderer.BeginDraw()) {
        DestroyWindow(hwnd);
        PrintTestFail("BeginDraw failed");
        return false;
    }

    renderer.ClearBackground();
    renderer.DrawHeaderBar(L"Test Title", 400.0f, 60.0f);
    renderer.DrawSeparator(70.0f, 400.0f);

    HRESULT hr = renderer.EndDraw();
    DestroyWindow(hwnd);

    if (FAILED(hr)) {
        PrintTestFail("EndDraw failed");
        return false;
    }

    PrintTestPass();
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    (void)argc;
    (void)argv;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::cout << "========================================" << std::endl;
    std::cout << " EnvLauncher Phase 3 Verification Suite " << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0;
    int total = 8;

    if (Test_DpiHelper_Scaling()) passed++;
    if (Test_DpiHelper_GetSystemDpi()) passed++;
    if (Test_DwmHelper_Calls()) passed++;
    if (Test_IconHelper_Creation()) passed++;
    if (Test_Theme_Colors()) passed++;
    if (Test_Layout_Calculations()) passed++;
    if (Test_Controls_HitTest()) passed++;
    if (Test_Renderer_Initialization()) passed++;

    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << passed << " / " << total << " tests passed." << std::endl;
    std::cout << "========================================" << std::endl;

    CoUninitialize();

    return (passed == total) ? 0 : 1;
}
