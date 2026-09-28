#include "config/ConfigManager.h"
#include "app/ConfiguredAppLauncher.h"
#include "ui/Controls.h"
#include "ui/Layout.h"
#include "platform/DpiHelper.h"

#include <windows.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
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

// Helper: gets env_probe.exe path in the same directory as test executable
static std::filesystem::path GetProbeExePath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path() / L"env_probe.exe";
}

// Helper: reads entire file content as string
static std::string ReadFileContent(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// Test 1: Dynamic Card Generation from ConfigModel
static bool Test_CardGeneration_FromConfig() {
    PrintTestHeader("1. Card generation and profile binding");

    ConfigModel config;
    config.version = 1;

    EnvironmentProfile prof;
    prof.id = L"prof_dev";
    prof.name = L"Dev Profile";
    prof.variables[L"HTTP_PROXY"] = L"http://127.0.0.1:7897";
    prof.variables[L"JAVA_HOME"] = L"C:\\jdk-21";
    config.environmentProfiles.push_back(prof);

    AppProfile app1;
    app1.id = L"app_cmd";
    app1.name = L"Command Prompt";
    app1.executable = L"C:\\Windows\\System32\\cmd.exe";
    app1.environmentProfileId = L"prof_dev";
    config.applications.push_back(app1);

    AppProfile app2;
    app2.id = L"app_missing";
    app2.name = L"Nonexistent App";
    app2.executable = L"C:\\InvalidDirectory\\NonExistentExe_12345.exe";
    app2.environmentProfileId = L"";
    config.applications.push_back(app2);

    // Replicate MainWindow::ReloadCardsFromConfig logic
    std::vector<AppCardView> cards;
    for (const auto& app : config.applications) {
        AppCardView card;
        card.id = app.id;
        card.name = app.name;
        card.executablePath = app.executable;

        const EnvironmentProfile* p = config.FindProfileById(app.environmentProfileId);
        if (p) {
            std::wstring envDesc = p->name;
            if (!p->variables.empty()) {
                envDesc += L" (";
                size_t count = 0;
                for (const auto& [k, v] : p->variables) {
                    if (count > 0) envDesc += L", ";
                    envDesc += k;
                    if (++count >= 2 && p->variables.size() > 2) {
                        envDesc += L", ...";
                        break;
                    }
                }
                envDesc += L")";
            }
            card.environmentName = std::move(envDesc);
        } else {
            card.environmentName = L"(None)";
        }

        std::error_code ec;
        card.executableExists = !app.executable.empty() && std::filesystem::exists(app.executable, ec);
        cards.push_back(std::move(card));
    }

    if (cards.size() != 2) {
        PrintTestFail("Expected 2 cards generated");
        return false;
    }

    // Verify card 1
    if (cards[0].id != L"app_cmd" || cards[0].name != L"Command Prompt") {
        PrintTestFail("Card 1 name/id mismatch");
        return false;
    }
    if (cards[0].environmentName.find(L"Dev Profile") == std::wstring::npos ||
        cards[0].environmentName.find(L"HTTP_PROXY") == std::wstring::npos) {
        PrintTestFail("Card 1 environmentName did not format profile and variables correctly");
        return false;
    }
    if (!cards[0].executableExists) {
        PrintTestFail("cmd.exe was incorrectly flagged as not existing");
        return false;
    }

    // Verify card 2
    if (cards[1].id != L"app_missing" || cards[1].environmentName != L"(None)") {
        PrintTestFail("Card 2 name/env mismatch");
        return false;
    }
    if (cards[1].executableExists) {
        PrintTestFail("Nonexistent exe was incorrectly flagged as existing");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 2: Layout calculation disables launch button for missing executables and assigns targetId
static bool Test_Layout_ButtonTargetAndDisabled() {
    PrintTestHeader("2. Layout targetId binding and disabled state");

    ButtonControl addBtn;
    ButtonControl manageBtn;
    std::vector<AppCardView> cards;

    AppCardView c1;
    c1.id = L"id_valid";
    c1.name = L"Valid App";
    c1.executableExists = true;
    cards.push_back(c1);

    AppCardView c2;
    c2.id = L"id_invalid";
    c2.name = L"Invalid App";
    c2.executableExists = false;
    cards.push_back(c2);

    float totalHeight = 0.0f;
    float maxScroll = 0.0f;

    Layout::CalculateLayout(
        800.0f, 600.0f, 0.0f,
        addBtn, cards, manageBtn,
        totalHeight, maxScroll
    );

    // Check targetId assignment
    if (cards[0].launchButton.targetId != L"id_valid") {
        PrintTestFail("Card 1 launch button targetId was not assigned");
        return false;
    }
    if (cards[1].launchButton.targetId != L"id_invalid") {
        PrintTestFail("Card 2 launch button targetId was not assigned");
        return false;
    }

    // Check button enabled/disabled states
    if (cards[0].launchButton.state == InteractionState::Disabled) {
        PrintTestFail("Valid app launch button is erroneously disabled");
        return false;
    }
    if (cards[1].launchButton.state != InteractionState::Disabled) {
        PrintTestFail("Invalid app launch button is not disabled");
        return false;
    }

    // HitTest on disabled button must return false
    float midX = (cards[1].launchButton.bounds.left + cards[1].launchButton.bounds.right) * 0.5f;
    float midY = (cards[1].launchButton.bounds.top + cards[1].launchButton.bounds.bottom) * 0.5f;
    if (cards[1].launchButton.HitTest(midX, midY)) {
        PrintTestFail("HitTest succeeded on disabled launch button");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 3: Live process launch with injected environment via ConfiguredAppLauncher
static bool Test_LiveAppLaunch_WithEnvironment() {
    PrintTestHeader("3. ConfiguredAppLauncher live execution");

    auto probeExe = GetProbeExePath();
    if (!std::filesystem::exists(probeExe)) {
        PrintTestFail("env_probe.exe not found");
        return false;
    }

    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::filesystem::path outPath = std::filesystem::path(tempDir) / (L"p4_probe_" + std::to_wstring(GetCurrentProcessId()) + L".txt");

    ConfigModel config;
    config.version = 1;

    EnvironmentProfile prof;
    prof.id = L"p_phase4";
    prof.name = L"Phase 4 Environment";
    prof.variables[L"PHASE4_ENV_KEY"] = L"CUSTOM_SUCCESS_PHASE4";
    config.environmentProfiles.push_back(prof);

    AppProfile app;
    app.id = L"app_probe";
    app.name = L"Probe Application";
    app.executable = probeExe.wstring();
    app.arguments = L"PHASE4_ENV_KEY \"" + outPath.wstring() + L"\"";
    app.environmentProfileId = L"p_phase4";
    config.applications.push_back(app);

    LaunchSyncResult res = ConfiguredAppLauncher::LaunchAppSync(
        config,
        L"app_probe",
        8000,
        AppLaunchMode::Normal
    );

    if (!res.success) {
        PrintTestFail("LaunchAppSync reported failure");
        return false;
    }

    if (res.exitCode != 0) {
        PrintTestFail("Process exited with non-zero exit code");
        return false;
    }

    std::string captured = ReadFileContent(outPath);
    std::error_code ec;
    std::filesystem::remove(outPath, ec);

    if (captured != "CUSTOM_SUCCESS_PHASE4") {
        PrintTestFail("Injected environment variable value mismatch");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 4: Missing executable launch rejection with Win32 error
static bool Test_MissingExecutable_LaunchFailure() {
    PrintTestHeader("4. Missing executable launch failure handling");

    ConfigModel config;
    config.version = 1;

    AppProfile app;
    app.id = L"app_bad";
    app.name = L"Nonexistent";
    app.executable = L"C:\\DirectoryDoesNotExist_9999\\FakeApp.exe";
    config.applications.push_back(app);

    LaunchResult res = ConfiguredAppLauncher::LaunchApp(
        config,
        L"app_bad",
        AppLaunchMode::Normal
    );

    if (res.success) {
        PrintTestFail("LaunchApp succeeded on non-existent executable");
        return false;
    }

    if (res.errorCode == 0) {
        PrintTestFail("Expected non-zero Win32 error code for missing executable");
        return false;
    }

    if (res.errorMessage.empty()) {
        PrintTestFail("Error message is empty");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 5: Empty state layout behavior
static bool Test_EmptyState_Layout() {
    PrintTestHeader("5. Empty state layout behavior");

    ButtonControl addBtn;
    ButtonControl manageBtn;
    std::vector<AppCardView> cards; // Empty list

    float totalHeight = 0.0f;
    float maxScroll = 0.0f;

    Layout::CalculateLayout(
        800.0f, 600.0f, 0.0f,
        addBtn, cards, manageBtn,
        totalHeight, maxScroll
    );

    // Manage environments button should still be placed properly below empty state
    if (manageBtn.bounds.top < 60.0f + 120.0f) {
        PrintTestFail("Manage environments button bounds unexpected for empty state");
        return false;
    }

    if (maxScroll != 0.0f) {
        PrintTestFail("Empty state with large client window should not require scrolling");
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
    std::cout << " EnvLauncher Phase 4 Verification Suite " << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0;
    int total = 5;

    if (Test_CardGeneration_FromConfig()) passed++;
    if (Test_Layout_ButtonTargetAndDisabled()) passed++;
    if (Test_LiveAppLaunch_WithEnvironment()) passed++;
    if (Test_MissingExecutable_LaunchFailure()) passed++;
    if (Test_EmptyState_Layout()) passed++;

    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << passed << " / " << total << " tests passed." << std::endl;
    std::cout << "========================================" << std::endl;

    CoUninitialize();

    return (passed == total) ? 0 : 1;
}
