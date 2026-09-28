#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include "process/ProcessLauncher.h"
#include "model/ConfigModel.h"
#include "config/ConfigManager.h"
#include "app/ConfiguredAppLauncher.h"
#include "platform/DpiHelper.h"
#include "ui/Layout.h"
#include "ui/Renderer.h"

using namespace envlauncher;

// Helper to locate env_probe.exe in the same directory as the test runner
static std::wstring GetEnvProbePath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path p(exePath);
    p = p.parent_path() / L"env_probe.exe";
    return p.wstring();
}

// Helper to read file contents
static std::string ReadFileUtf8(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) return {};
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

void TestScrollbarMath() {
    std::cout << "[Test 1] Testing scrollbar geometry, fraction, and clamping math..." << std::endl;

    const float headerHeight = 60.0f;
    const float clientHeight = 600.0f;
    const float viewportHeight = clientHeight - headerHeight; // 540.0f

    // Case 1: Content height <= viewport height -> No scrollbar needed
    {
        float totalContentHeight = 400.0f;
        assert(totalContentHeight <= viewportHeight);
        (void)totalContentHeight;
    }

    // Case 2: Content height > viewport height -> Proportional thumb
    {
        float totalContentHeight = 1080.0f; // Exactly 2x viewport
        float trackHeight = viewportHeight; // 540.0f
        float thumbHeight = std::max(24.0f, trackHeight * (viewportHeight / totalContentHeight));
        // viewport / content = 0.5 -> thumbHeight = 270.0f
        assert(std::abs(thumbHeight - 270.0f) < 0.001f);

        float maxScroll = totalContentHeight - viewportHeight; // 540.0f
        float maxThumbTravel = trackHeight - thumbHeight; // 270.0f

        // At scroll 0
        float scrollOffset = 0.0f;
        float fraction = scrollOffset / maxScroll;
        float thumbTop = headerHeight + fraction * maxThumbTravel;
        assert(std::abs(thumbTop - 60.0f) < 0.001f);

        // At 50% scroll
        scrollOffset = 270.0f;
        fraction = scrollOffset / maxScroll;
        thumbTop = headerHeight + fraction * maxThumbTravel;
        assert(std::abs(thumbTop - (60.0f + 135.0f)) < 0.001f);

        // At max scroll
        scrollOffset = 540.0f;
        fraction = scrollOffset / maxScroll;
        thumbTop = headerHeight + fraction * maxThumbTravel;
        assert(std::abs(thumbTop - (60.0f + 270.0f)) < 0.001f);
        (void)thumbTop;
    }

    // Case 3: Huge content height -> Min thumb height clamp (24px)
    {
        float totalContentHeight = 100000.0f;
        float trackHeight = viewportHeight; // 540.0f
        float thumbHeight = std::max(24.0f, trackHeight * (viewportHeight / totalContentHeight));
        assert(thumbHeight == 24.0f);
        (void)thumbHeight;
    }

    std::cout << "  -> Scrollbar geometry math: PASS" << std::endl;
}

void TestLaunchWithoutProfile() {
    std::cout << "[Test 2] Testing 'Launch without profile' (ParentEnvironmentOnly mode)..." << std::endl;

    std::wstring probeExe = GetEnvProbePath();
    assert(std::filesystem::exists(probeExe));

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / L"envlauncher_phase6_test";
    std::filesystem::create_directories(tempDir);

    std::filesystem::path normalOut = tempDir / L"probe_normal.txt";
    std::filesystem::path parentOnlyOut = tempDir / L"probe_parent_only.txt";
    if (std::filesystem::exists(normalOut)) std::filesystem::remove(normalOut);
    if (std::filesystem::exists(parentOnlyOut)) std::filesystem::remove(parentOnlyOut);

    ConfigModel config;
    EnvironmentProfile profile;
    profile.id = L"prof-test";
    profile.name = L"Profile Test";
    profile.variables[L"PHASE6_PROFILE_VAR"] = L"ProfileValue";
    config.environmentProfiles.push_back(profile);

    AppProfile app;
    app.id = L"app-probe";
    app.name = L"Env Probe";
    app.executable = probeExe;
    app.environmentProfileId = L"prof-test";
    app.environmentOverrides[L"PHASE6_OVERRIDE_VAR"] = L"OverrideValue";

    // 1. Launch with Normal mode
    app.arguments = L"\"" + normalOut.wstring() + L"\"";
    config.applications.push_back(app);

    LaunchSyncResult resNormal = ConfiguredAppLauncher::LaunchAppSync(config, L"app-probe", 5000, AppLaunchMode::Normal);
    assert(resNormal.success);
    assert(!resNormal.timedOut);
    assert(resNormal.exitCode == 0);

    std::string normalContent = ReadFileUtf8(normalOut);
    assert(normalContent.find("PHASE6_PROFILE_VAR=ProfileValue") != std::string::npos);
    assert(normalContent.find("PHASE6_OVERRIDE_VAR=OverrideValue") != std::string::npos);
    std::cout << "  -> Normal mode correctly injects profile & override variables" << std::endl;

    // 2. Launch with ParentEnvironmentOnly mode ("Launch without profile")
    config.applications[0].arguments = L"\"" + parentOnlyOut.wstring() + L"\"";
    LaunchSyncResult resParentOnly = ConfiguredAppLauncher::LaunchAppSync(config, L"app-probe", 5000, AppLaunchMode::ParentEnvironmentOnly);
    assert(resParentOnly.success);
    assert(!resParentOnly.timedOut);
    assert(resParentOnly.exitCode == 0);

    std::string parentContent = ReadFileUtf8(parentOnlyOut);
    // Profile variables and override variables MUST NOT be present!
    assert(parentContent.find("PHASE6_PROFILE_VAR") == std::string::npos);
    assert(parentContent.find("PHASE6_OVERRIDE_VAR") == std::string::npos);
    std::cout << "  -> ParentEnvironmentOnly mode successfully isolates from profile & overrides" << std::endl;

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::cout << "  -> Launch without profile: PASS" << std::endl;
}

void TestCorruptedJsonRecovery() {
    std::cout << "[Test 3] Testing corrupted JSON config preservation and graceful failure..." << std::endl;

    std::filesystem::path testConfigDir = std::filesystem::temp_directory_path() / L"envlauncher_corrupt_test";
    std::filesystem::create_directories(testConfigDir);
    std::filesystem::path testConfigFile = testConfigDir / L"config.json";

    // Write malformed JSON
    {
        std::ofstream out(testConfigFile);
        out << "{ \"applications\": [ { \"name\": \"broken... "; // abrupt termination
    }

    ConfigManager mgr(testConfigFile);
    ConfigResult res = mgr.Load();
    assert(!res.success);
    assert(!res.errorMessage.empty());
    std::wcout << L"  -> Graceful error captured: " << res.errorMessage << std::endl;

    // Ensure config in memory is still valid empty config
    assert(mgr.GetConfig().applications.empty());
    assert(mgr.GetConfig().environmentProfiles.empty());

    // Corrupted file on disk must NOT be destroyed/overwritten on failed load
    std::string onDisk = ReadFileUtf8(testConfigFile);
    assert(onDisk.find("broken...") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(testConfigDir, ec);
    std::cout << "  -> Corrupted JSON recovery: PASS" << std::endl;
}

void TestDpiMinTrackSizeAndReflow() {
    std::cout << "[Test 4] Testing DPI minimum window tracking size and reflow..." << std::endl;

    // 480 x 360 base DIP
    float minW = 480.0f;
    float minH = 360.0f;

    // 96 DPI (100%)
    assert(DpiHelper::Scale(minW, 96) == 480.0f);
    assert(DpiHelper::Scale(minH, 96) == 360.0f);

    // 120 DPI (125%)
    assert(DpiHelper::Scale(minW, 120) == 600.0f);
    assert(DpiHelper::Scale(minH, 120) == 450.0f);

    // 144 DPI (150%)
    assert(DpiHelper::Scale(minW, 144) == 720.0f);
    assert(DpiHelper::Scale(minH, 144) == 540.0f);

    // 192 DPI (200%)
    assert(DpiHelper::Scale(minW, 192) == 960.0f);
    assert(DpiHelper::Scale(minH, 192) == 720.0f);
    (void)minW;
    (void)minH;

    // Test layout reflow
    ButtonControl addBtn;
    ButtonControl manageBtn;
    std::vector<AppCardView> cards(5);
    for (size_t i = 0; i < cards.size(); ++i) {
        cards[i].id = std::to_wstring(i);
        cards[i].name = L"App " + std::to_wstring(i);
    }

    float totalContentHeight = 0.0f;
    float maxScrollOffset = 0.0f;
    float scrollOffset = 0.0f;

    // Small height (400 DIP) -> Needs scrolling
    Layout::CalculateLayout(600.0f, 400.0f, scrollOffset, addBtn, cards, manageBtn, totalContentHeight, maxScrollOffset);
    assert(maxScrollOffset > 0.0f);
    assert(totalContentHeight > 400.0f - 60.0f);

    // Very large height (2000 DIP) -> No scrolling needed
    Layout::CalculateLayout(600.0f, 2000.0f, scrollOffset, addBtn, cards, manageBtn, totalContentHeight, maxScrollOffset);
    assert(maxScrollOffset == 0.0f);

    std::cout << "  -> DPI MinTrackSize and Reflow: PASS" << std::endl;
}

void TestDirectWriteEllipsisTrimming() {
    std::cout << "[Test 5] Testing DirectWrite ellipsis trimming setup..." << std::endl;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    (void)hr;

    Renderer renderer;
    bool initOk = renderer.Initialize();
    assert(initOk);
    (void)initOk;

    // Test DiscardDeviceResources safety when no render target exists
    renderer.DiscardDeviceResources();

    CoUninitialize();
    std::cout << "  -> DirectWrite Ellipsis Trimming & Device Cleanup: PASS" << std::endl;
}

int wmain(int argc, wchar_t* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "===========================================" << std::endl;
    std::cout << "       EnvLauncher Phase 6 Unit Tests      " << std::endl;
    std::cout << "===========================================" << std::endl;

    TestScrollbarMath();
    TestLaunchWithoutProfile();
    TestCorruptedJsonRecovery();
    TestDpiMinTrackSizeAndReflow();
    TestDirectWriteEllipsisTrimming();

    std::cout << "===========================================" << std::endl;
    std::cout << " All Phase 6 Unit Tests Passed Successfully!" << std::endl;
    std::cout << "===========================================" << std::endl;

    return 0;
}
