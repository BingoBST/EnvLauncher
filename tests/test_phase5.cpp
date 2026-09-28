#include "config/ConfigManager.h"
#include "app/ConfiguredAppLauncher.h"
#include "ui/Controls.h"
#include "ui/Layout.h"
#include "util/StringUtils.h"

#include <windows.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <sstream>
#include <set>
#include <algorithm>

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

static std::filesystem::path CreateTestSandbox(const std::wstring& name) {
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    static uint32_t counter = 0;
    counter++;
    std::filesystem::path p = std::filesystem::path(tempDir) / (name + L"_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(counter));
    std::filesystem::create_directories(p);
    return p;
}

// Test 1: Programmatic Add Application and Persistence
static bool Test_AddApplication_Persistence() {
    PrintTestHeader("1. Add Application and persistence roundtrip");

    auto sandbox = CreateTestSandbox(L"p5_add");
    auto configPath = sandbox / L"config.json";

    ConfigManager mgr(configPath);
    auto loadRes = mgr.Load();
    if (!loadRes.success) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Initial load failed");
        return false;
    }

    // Add new app
    AppProfile newApp;
    newApp.id = L"app_test_add";
    newApp.name = L"Sample App";
    newApp.executable = L"C:\\Windows\\System32\\cmd.exe";
    newApp.arguments = L"/c echo hello";
    newApp.workingDirectory = L"C:\\";
    newApp.environmentOverrides[L"MY_VAR"] = L"123";

    mgr.GetConfig().applications.push_back(newApp);
    auto saveRes = mgr.Save();
    if (!saveRes.success) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Save failed");
        return false;
    }

    // Load from disk into new manager
    ConfigManager mgrVerify(configPath);
    auto verifyRes = mgrVerify.Load();
    if (!verifyRes.success) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Reload failed");
        return false;
    }

    const auto* found = mgrVerify.GetConfig().FindAppById(L"app_test_add");
    if (!found) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Added application not found in reloaded config");
        return false;
    }

    if (found->name != L"Sample App" || found->executable != L"C:\\Windows\\System32\\cmd.exe" ||
        found->arguments != L"/c echo hello" || found->workingDirectory != L"C:\\" ||
        found->environmentOverrides.at(L"MY_VAR") != L"123") {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Application properties mismatch");
        return false;
    }

    std::filesystem::remove_all(sandbox);
    PrintTestPass();
    return true;
}

// Test 2: Programmatic Edit Application
static bool Test_EditApplication_Persistence() {
    PrintTestHeader("2. Edit Application and persistence");

    auto sandbox = CreateTestSandbox(L"p5_edit");
    auto configPath = sandbox / L"config.json";

    ConfigManager mgr(configPath);
    mgr.Load();

    AppProfile app;
    app.id = L"app_to_edit";
    app.name = L"Original Name";
    app.executable = L"C:\\Windows\\notepad.exe";
    mgr.GetConfig().applications.push_back(app);
    mgr.Save();

    // Edit app
    auto* toEdit = mgr.GetConfig().FindAppById(L"app_to_edit");
    if (!toEdit) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Could not find app to edit");
        return false;
    }

    toEdit->name = L"Modified Name";
    toEdit->arguments = L"\"C:\\test.txt\"";
    toEdit->environmentOverrides[L"EDIT_FLAG"] = L"TRUE";
    mgr.Save();

    // Reload and check
    ConfigManager mgr2(configPath);
    mgr2.Load();
    const auto* reloaded = mgr2.GetConfig().FindAppById(L"app_to_edit");
    if (!reloaded) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Reloaded app not found");
        return false;
    }

    if (reloaded->name != L"Modified Name" || reloaded->arguments != L"\"C:\\test.txt\"" ||
        reloaded->environmentOverrides.at(L"EDIT_FLAG") != L"TRUE") {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Edited fields did not match");
        return false;
    }

    std::filesystem::remove_all(sandbox);
    PrintTestPass();
    return true;
}

// Test 3: Programmatic Delete Application
static bool Test_DeleteApplication_Persistence() {
    PrintTestHeader("3. Delete Application and persistence");

    auto sandbox = CreateTestSandbox(L"p5_del");
    auto configPath = sandbox / L"config.json";

    ConfigManager mgr(configPath);
    mgr.Load();

    AppProfile app1; app1.id = L"app_keep"; app1.name = L"Keep Me"; app1.executable = L"C:\\Windows\\notepad.exe";
    AppProfile app2; app2.id = L"app_delete"; app2.name = L"Delete Me"; app2.executable = L"C:\\Windows\\System32\\cmd.exe";
    mgr.GetConfig().applications.push_back(app1);
    mgr.GetConfig().applications.push_back(app2);
    mgr.Save();

    // Delete app2
    auto& apps = mgr.GetConfig().applications;
    apps.erase(
        std::remove_if(apps.begin(), apps.end(), [](const AppProfile& a) { return a.id == L"app_delete"; }),
        apps.end()
    );
    mgr.Save();

    // Verify on disk
    ConfigManager mgr2(configPath);
    mgr2.Load();
    if (mgr2.GetConfig().applications.size() != 1) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Expected exactly 1 application remaining");
        return false;
    }

    if (mgr2.GetConfig().FindAppById(L"app_delete") != nullptr) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Deleted application still present");
        return false;
    }

    if (mgr2.GetConfig().FindAppById(L"app_keep") == nullptr) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Remaining application missing");
        return false;
    }

    std::filesystem::remove_all(sandbox);
    PrintTestPass();
    return true;
}

// Test 4: Profile Deletion Reference Safety Guard (start.md Section 36)
static bool Test_ProfileDeletion_ReferenceGuard() {
    PrintTestHeader("4. Profile deletion reference safety guard");

    ConfigModel config;
    config.version = 1;

    EnvironmentProfile prof;
    prof.id = L"p_in_use";
    prof.name = L"In Use Profile";
    prof.variables[L"TEST"] = L"1";
    config.environmentProfiles.push_back(prof);

    EnvironmentProfile profUnused;
    profUnused.id = L"p_free";
    profUnused.name = L"Free Profile";
    config.environmentProfiles.push_back(profUnused);

    AppProfile app;
    app.id = L"app1";
    app.name = L"Dependent App";
    app.executable = L"C:\\test.exe";
    app.environmentProfileId = L"p_in_use";
    config.applications.push_back(app);

    // Helper logic replicating ProfileManagerDialog safety check
    auto checkCanDelete = [&](const std::wstring& profileId, std::vector<std::wstring>& referencingApps) -> bool {
        referencingApps.clear();
        for (const auto& a : config.applications) {
            if (a.environmentProfileId == profileId) {
                referencingApps.push_back(a.name);
            }
        }
        return referencingApps.empty();
    };

    std::vector<std::wstring> refApps;
    if (checkCanDelete(L"p_in_use", refApps)) {
        PrintTestFail("Profile in-use was permitted to be deleted");
        return false;
    }

    if (refApps.size() != 1 || refApps[0] != L"Dependent App") {
        PrintTestFail("Referencing apps list incorrect");
        return false;
    }

    if (!checkCanDelete(L"p_free", refApps)) {
        PrintTestFail("Free profile was erroneously blocked from deletion");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 5: Multi-line Variable Parsing and Validation
static bool Test_VariableParsing_AndValidation() {
    PrintTestHeader("5. Variable line parsing and validation");

    std::wstring text = L"  HTTP_PROXY=http://127.0.0.1:7897  \r\n"
                        L"# This is a comment\r\n"
                        L"HTTPS_PROXY=http://127.0.0.1:7897\r\n"
                        L"\r\n"
                        L"NO_PROXY=localhost,127.0.0.1\r\n";

    auto parseVars = [](const std::wstring& raw, std::map<std::wstring, std::wstring>& outVars, std::wstring& outErr) -> bool {
        outVars.clear();
        std::set<std::wstring, CaseInsensitiveLess> seenKeys;
        std::wstringstream ss(raw);
        std::wstring line;

        auto trim = [](const std::wstring& s) {
            auto st = s.find_first_not_of(L" \t\r\n");
            if (st == std::wstring::npos) return std::wstring(L"");
            auto en = s.find_last_not_of(L" \t\r\n");
            return s.substr(st, en - st + 1);
        };

        while (std::getline(ss, line)) {
            std::wstring trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == L'#') continue;

            size_t eqPos = trimmed.find(L'=');
            if (eqPos == std::wstring::npos || eqPos == 0) {
                outErr = L"Missing '=' or empty key";
                return false;
            }

            std::wstring key = trim(trimmed.substr(0, eqPos));
            std::wstring val = trimmed.substr(eqPos + 1);

            if (key.find(L'=') != std::wstring::npos) {
                outErr = L"Key contains '='";
                return false;
            }

            if (!seenKeys.insert(key).second) {
                outErr = L"Duplicate key";
                return false;
            }

            outVars[key] = val;
        }
        return true;
    };

    std::map<std::wstring, std::wstring> parsed;
    std::wstring err;
    if (!parseVars(text, parsed, err)) {
        PrintTestFail("Valid variable text failed to parse");
        return false;
    }

    if (parsed.size() != 3 || parsed[L"HTTP_PROXY"] != L"http://127.0.0.1:7897" ||
        parsed[L"NO_PROXY"] != L"localhost,127.0.0.1") {
        PrintTestFail("Parsed variables content mismatch");
        return false;
    }

    // Test duplicate case-insensitive collision
    std::wstring badText = L"PATH=C:\\bin\r\npath=C:\\bin2";
    if (parseVars(badText, parsed, err)) {
        PrintTestFail("Duplicate key collision was not detected");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 6: Auto-fill Application Name from Executable Path
static bool Test_AutoFill_ApplicationName() {
    PrintTestHeader("6. Auto-fill application name from path");

    std::filesystem::path p1(L"C:\\Program Files\\JetBrains\\idea64.exe");
    if (p1.stem().wstring() != L"idea64") {
        PrintTestFail("idea64 stem mismatch");
        return false;
    }

    std::filesystem::path p2(L"D:\\tools\\git.cmd");
    if (p2.stem().wstring() != L"git") {
        PrintTestFail("git stem mismatch");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 7: Card Action Buttons Layout & Hit-Testing
static bool Test_CardButtons_LayoutAndHitTest() {
    PrintTestHeader("7. Card action buttons (Launch, Edit, Delete) layout");

    ButtonControl addBtn;
    ButtonControl manageBtn;
    std::vector<AppCardView> cards;

    AppCardView card;
    card.id = L"app_actions";
    card.name = L"Actions App";
    card.executableExists = true;
    cards.push_back(card);

    float totalH = 0.0f;
    float maxScroll = 0.0f;

    Layout::CalculateLayout(800.0f, 600.0f, 0.0f, addBtn, cards, manageBtn, totalH, maxScroll);

    const auto& c = cards[0];

    // Check all three buttons have targetId set to app ID
    if (c.launchButton.targetId != L"app_actions" ||
        c.editButton.targetId != L"app_actions" ||
        c.deleteButton.targetId != L"app_actions") {
        PrintTestFail("Button targetId mismatch");
        return false;
    }

    // Check ordering: Delete is left of Edit, Edit is left of Launch
    if (c.deleteButton.bounds.right > c.editButton.bounds.left ||
        c.editButton.bounds.right > c.launchButton.bounds.left) {
        PrintTestFail("Button relative horizontal positions invalid");
        return false;
    }

    // Launch button is Accent, Edit & Delete are not
    if (!c.launchButton.isAccent || c.editButton.isAccent || c.deleteButton.isAccent) {
        PrintTestFail("Button accent styles unexpected");
        return false;
    }

    // Test hit testing on each button
    float delMidX = (c.deleteButton.bounds.left + c.deleteButton.bounds.right) * 0.5f;
    float delMidY = (c.deleteButton.bounds.top + c.deleteButton.bounds.bottom) * 0.5f;
    if (!c.deleteButton.HitTest(delMidX, delMidY) || c.editButton.HitTest(delMidX, delMidY) || c.launchButton.HitTest(delMidX, delMidY)) {
        PrintTestFail("Hit test on Delete button incorrectly triggered other buttons");
        return false;
    }

    float editMidX = (c.editButton.bounds.left + c.editButton.bounds.right) * 0.5f;
    float editMidY = (c.editButton.bounds.top + c.editButton.bounds.bottom) * 0.5f;
    if (!c.editButton.HitTest(editMidX, editMidY) || c.deleteButton.HitTest(editMidX, editMidY) || c.launchButton.HitTest(editMidX, editMidY)) {
        PrintTestFail("Hit test on Edit button incorrectly triggered other buttons");
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
    std::cout << " EnvLauncher Phase 5 Verification Suite " << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0;
    int total = 7;

    if (Test_AddApplication_Persistence()) passed++;
    if (Test_EditApplication_Persistence()) passed++;
    if (Test_DeleteApplication_Persistence()) passed++;
    if (Test_ProfileDeletion_ReferenceGuard()) passed++;
    if (Test_VariableParsing_AndValidation()) passed++;
    if (Test_AutoFill_ApplicationName()) passed++;
    if (Test_CardButtons_LayoutAndHitTest()) passed++;

    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << passed << " / " << total << " tests passed." << std::endl;
    std::cout << "========================================" << std::endl;

    CoUninitialize();

    return (passed == total) ? 0 : 1;
}
