#include "config/ConfigManager.h"
#include "app/ConfiguredAppLauncher.h"
#include "util/StringUtils.h"

#include <windows.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cassert>

using namespace envlauncher;

// Helper: prints test banner
static void PrintTestHeader(const char* name) {
    std::cout << "[TEST] " << name << " ... ";
}

static void PrintTestPass() {
    std::cout << "PASS" << std::endl;
}

static void PrintTestFail(const char* reason) {
    std::cout << "FAIL: " << reason << std::endl;
}

// Helper RAII to safely manage parent environment variables in tests
class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(const wchar_t* name, const wchar_t* value)
        : name_(name) {
        DWORD len = GetEnvironmentVariableW(name, nullptr, 0);
        if (len > 0) {
            hadValue_ = true;
            originalValue_.resize(len);
            GetEnvironmentVariableW(name, originalValue_.data(), len);
            if (!originalValue_.empty() && originalValue_.back() == L'\0') {
                originalValue_.pop_back();
            }
        }
        SetEnvironmentVariableW(name, value);
    }

    ~ScopedEnvironmentVariable() {
        if (hadValue_) {
            SetEnvironmentVariableW(name_.c_str(), originalValue_.c_str());
        } else {
            SetEnvironmentVariableW(name_.c_str(), nullptr);
        }
    }

private:
    std::wstring name_;
    bool hadValue_{false};
    std::wstring originalValue_;
};

// Helper: generates an isolated temporary sandbox directory for test
static std::filesystem::path CreateTestSandboxDir(const std::wstring& prefix) {
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    static uint32_t counter = 0;
    counter++;
    std::filesystem::path p = std::filesystem::path(tempDir) / (prefix + L"_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(counter));
    std::filesystem::create_directories(p);
    return p;
}

// Helper: reads entire file content as string
static std::string ReadFileContent(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// Helper: gets env_probe.exe path in the same directory as test executable
static std::filesystem::path GetProbeExePath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path() / L"env_probe.exe";
}

// Test 1: StringUtils valid conversions (ASCII, Chinese, Japanese, emoji surrogate pairs)
static bool Test_StringUtils_ValidConversions() {
    PrintTestHeader("1. StringUtils valid conversions");

    std::wstring original = L"Hello, 世界！こんにちは 🚀✨ Test";
    try {
        std::string utf8 = StringUtils::WideToUtf8(original);
        std::wstring roundtrip = StringUtils::Utf8ToWide(utf8);
        if (roundtrip != original) {
            PrintTestFail("Roundtrip string does not match original");
            return false;
        }
    } catch (const std::exception& e) {
        PrintTestFail(e.what());
        return false;
    }

    // Empty string test
    try {
        if (!StringUtils::WideToUtf8(L"").empty() || !StringUtils::Utf8ToWide("").empty()) {
            PrintTestFail("Empty string conversion did not produce empty string");
            return false;
        }
    } catch (const std::exception& e) {
        PrintTestFail(e.what());
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 2: StringUtils invalid UTF-8 rejection (MB_ERR_INVALID_CHARS)
static bool Test_StringUtils_InvalidUtf8() {
    PrintTestHeader("2. StringUtils invalid UTF-8 rejection");

    // Invalid truncated UTF-8 sequence
    std::string invalidUtf8 = "\xE4\xBD"; // Missing 3rd byte for a 3-byte Chinese char
    bool threw = false;
    try {
        StringUtils::Utf8ToWide(invalidUtf8);
    } catch (const StringConversionError&) {
        threw = true;
    }

    if (!threw) {
        PrintTestFail("Invalid UTF-8 byte sequence was not rejected");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 3: StringUtils invalid UTF-16 rejection (WC_ERR_INVALID_CHARS)
static bool Test_StringUtils_InvalidUtf16() {
    PrintTestHeader("3. StringUtils invalid UTF-16 rejection");

    // Unpaired high surrogate
    std::wstring invalidUtf16(1, static_cast<wchar_t>(0xD83D));
    bool threw = false;
    try {
        StringUtils::WideToUtf8(invalidUtf16);
    } catch (const StringConversionError&) {
        threw = true;
    }

    if (!threw) {
        PrintTestFail("Unpaired UTF-16 surrogate was not rejected");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 4: Embedded NUL rejection
static bool Test_EmbeddedNull_Rejection() {
    PrintTestHeader("4. Embedded NUL rejection");

    std::string sWithNull("app\0name", 8);
    if (!StringUtils::ContainsEmbeddedNull(sWithNull)) {
        PrintTestFail("ContainsEmbeddedNull failed for string_view");
        return false;
    }

    std::wstring wsWithNull(L"c:\\bin\\exe\0evil.exe", 18);
    if (!StringUtils::ContainsEmbeddedNull(wsWithNull)) {
        PrintTestFail("ContainsEmbeddedNull failed for wstring_view");
        return false;
    }

    ConfigModel model;
    model.version = 1;
    AppProfile app;
    app.id = L"app1";
    app.name = wsWithNull;
    app.executable = L"C:\\Windows\\notepad.exe";
    model.applications.push_back(app);

    auto res = ConfigManager::ValidateConfig(model);
    if (res.success) {
        PrintTestFail("ValidateConfig accepted embedded NUL in application name");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 5: Case-insensitive duplicate key rejection in JSON variables
static bool Test_CaseInsensitiveCollision_Rejection() {
    PrintTestHeader("5. Case-insensitive duplicate key rejection");

    auto sandbox = CreateTestSandboxDir(L"p2_collision");
    auto configPath = sandbox / L"config.json";

    std::string jsonContent = R"({
  "version": 1,
  "environmentProfiles": [
    {
      "id": "p1",
      "name": "Profile 1",
      "variables": {
        "PATH": "C:\\bin1",
        "Path": "C:\\bin2"
      }
    }
  ],
  "applications": []
})";

    {
        std::ofstream f(configPath, std::ios::binary);
        f.write(jsonContent.data(), jsonContent.size());
    }

    ConfigManager mgr(configPath);
    auto res = mgr.Load();

    std::filesystem::remove_all(sandbox);

    if (res.success) {
        PrintTestFail("Load succeeded despite colliding keys 'PATH' and 'Path'");
        return false;
    }

    if (res.errorMessage.find(L"collides case-insensitively") == std::wstring::npos) {
        PrintTestFail("Error message did not mention case-insensitive collision");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 6: JSON structural validation before model binding
static bool Test_JsonStructuralValidation() {
    PrintTestHeader("6. JSON structural validation");

    auto sandbox = CreateTestSandboxDir(L"p2_struct");
    auto configPath = sandbox / L"config.json";

    // 1. Version is string instead of int
    {
        std::string badJson = R"({"version": "1", "environmentProfiles": [], "applications": []})";
        std::ofstream f(configPath, std::ios::binary | std::ios::trunc);
        f.write(badJson.data(), badJson.size());
    }
    ConfigManager mgr(configPath);
    auto res1 = mgr.Load();
    if (res1.success) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Accepted string for version");
        return false;
    }

    // 2. environmentProfiles is number instead of array
    {
        std::string badJson = R"({"version": 1, "environmentProfiles": 42, "applications": []})";
        std::ofstream f(configPath, std::ios::binary | std::ios::trunc);
        f.write(badJson.data(), badJson.size());
    }
    auto res2 = mgr.Load();
    if (res2.success) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Accepted number for environmentProfiles");
        return false;
    }

    std::filesystem::remove_all(sandbox);
    PrintTestPass();
    return true;
}

// Test 7: Semantic validation (version mismatch, duplicate IDs, invalid references)
static bool Test_SemanticValidation() {
    PrintTestHeader("7. Semantic validation");

    // 1. Version 2 rejection
    ConfigModel m1;
    m1.version = 2;
    auto res1 = ConfigManager::ValidateConfig(m1);
    if (res1.success || res1.errorMessage.find(L"Unsupported config version") == std::wstring::npos) {
        PrintTestFail("Version 2 was not rejected with proper message");
        return false;
    }

    // 2. Duplicate Profile IDs
    ConfigModel m2;
    m2.version = 1;
    EnvironmentProfile p1;
    p1.id = L"dup";
    p1.name = L"Profile 1";
    EnvironmentProfile p2;
    p2.id = L"dup";
    p2.name = L"Profile 2";
    m2.environmentProfiles.push_back(p1);
    m2.environmentProfiles.push_back(p2);
    auto res2 = ConfigManager::ValidateConfig(m2);
    if (res2.success || res2.errorMessage.find(L"Duplicate profile ID") == std::wstring::npos) {
        PrintTestFail("Duplicate profile ID was not rejected");
        return false;
    }

    // 3. Application referencing non-existent profile
    ConfigModel m3;
    m3.version = 1;
    AppProfile app;
    app.id = L"app1";
    app.name = L"App 1";
    app.executable = L"C:\\test.exe";
    app.environmentProfileId = L"nonexistent_profile";
    m3.applications.push_back(app);
    auto res3 = ConfigManager::ValidateConfig(m3);
    if (res3.success || res3.errorMessage.find(L"references non-existent") == std::wstring::npos) {
        PrintTestFail("Reference to non-existent profile was not rejected");
        return false;
    }

    // 4. Variable name containing '='
    ConfigModel m4;
    m4.version = 1;
    EnvironmentProfile pInvalid;
    pInvalid.id = L"p_bad";
    pInvalid.name = L"P Bad";
    pInvalid.variables[L"BAD=NAME"] = L"val";
    m4.environmentProfiles.push_back(pInvalid);
    auto res4 = ConfigManager::ValidateConfig(m4);
    if (res4.success || res4.errorMessage.find(L"cannot contain '='") == std::wstring::npos) {
        PrintTestFail("Variable name containing '=' was not rejected");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 8: Default config creation on first run
static bool Test_DefaultConfig_FirstRun() {
    PrintTestHeader("8. First-run default config creation");

    auto sandbox = CreateTestSandboxDir(L"p2_firstrun");
    auto configPath = sandbox / L"sub_dir" / L"config.json";

    ConfigManager mgr(configPath);
    auto res = mgr.Load();

    if (!res.success) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("First run Load failed");
        return false;
    }

    if (!std::filesystem::exists(configPath)) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Default config file was not created on disk");
        return false;
    }

    std::string content = ReadFileContent(configPath);
    std::filesystem::remove_all(sandbox);

    if (content.find("\"version\": 1") == std::string::npos ||
        content.find("\"environmentProfiles\": []") == std::string::npos ||
        content.find("\"applications\": []") == std::string::npos) {
        PrintTestFail("Default config JSON content does not match expected structure");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 9: Config save and load roundtrip
static bool Test_Config_SaveAndLoad_Roundtrip() {
    PrintTestHeader("9. Config save and load roundtrip");

    auto sandbox = CreateTestSandboxDir(L"p2_roundtrip");
    auto configPath = sandbox / L"config.json";

    ConfigManager mgr1(configPath);
    auto& cfg = mgr1.GetConfig();
    cfg.version = 1;

    EnvironmentProfile prof;
    prof.id = L"p_clash";
    prof.name = L"Clash 7897";
    prof.variables[L"HTTP_PROXY"] = L"http://127.0.0.1:7897";
    prof.variables[L"NO_PROXY"] = L"localhost,127.0.0.1";
    cfg.environmentProfiles.push_back(prof);

    AppProfile app;
    app.id = L"app_idea";
    app.name = L"IntelliJ IDEA";
    app.executable = L"C:\\Program Files\\JetBrains\\idea64.exe";
    app.arguments = L"--nosplash";
    app.workingDirectory = L"C:\\work";
    app.environmentProfileId = L"p_clash";
    app.environmentOverrides[L"IDEA_VM_OPTIONS"] = L"C:\\idea.vmoptions";
    cfg.applications.push_back(app);

    auto saveRes = mgr1.Save();
    if (!saveRes.success) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Save failed");
        return false;
    }

    // Load into completely new ConfigManager instance
    ConfigManager mgr2(configPath);
    auto loadRes = mgr2.Load();
    if (!loadRes.success) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Load in second instance failed");
        return false;
    }

    const auto& loaded = mgr2.GetConfig();
    if (loaded.environmentProfiles.size() != 1 || loaded.applications.size() != 1) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Loaded item counts mismatch");
        return false;
    }

    const auto& pLoaded = loaded.environmentProfiles[0];
    if (pLoaded.id != prof.id || pLoaded.name != prof.name || pLoaded.variables.at(L"HTTP_PROXY") != L"http://127.0.0.1:7897") {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Profile fields mismatch");
        return false;
    }

    const auto& aLoaded = loaded.applications[0];
    if (aLoaded.id != app.id || aLoaded.name != app.name || aLoaded.executable != app.executable ||
        aLoaded.arguments != app.arguments || aLoaded.workingDirectory != app.workingDirectory ||
        aLoaded.environmentProfileId != app.environmentProfileId ||
        aLoaded.environmentOverrides.at(L"IDEA_VM_OPTIONS") != L"C:\\idea.vmoptions") {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Application fields mismatch");
        return false;
    }

    std::filesystem::remove_all(sandbox);
    PrintTestPass();
    return true;
}

// Test 10: Corrupted JSON file preservation and in-memory safety
static bool Test_CorruptedJson_FilePreservation() {
    PrintTestHeader("10. Corrupted JSON file preservation");

    auto sandbox = CreateTestSandboxDir(L"p2_corrupt");
    auto configPath = sandbox / L"config.json";

    std::string corruptData = "{\n  \"version\": 1,\n  \"broken_json_syntax: true,\n";
    {
        std::ofstream f(configPath, std::ios::binary);
        f.write(corruptData.data(), corruptData.size());
    }

    ConfigManager mgr(configPath);
    auto loadRes = mgr.Load();

    if (loadRes.success) {
        std::filesystem::remove_all(sandbox);
        PrintTestFail("Load succeeded on corrupted JSON");
        return false;
    }

    // Verify file on disk is byte-for-byte untouched
    std::string onDiskData = ReadFileContent(configPath);
    std::filesystem::remove_all(sandbox);

    if (onDiskData != corruptData) {
        PrintTestFail("Corrupted JSON file was modified or overwritten!");
        return false;
    }

    // Verify in-memory config remains empty default
    if (!mgr.GetConfig().applications.empty() || !mgr.GetConfig().environmentProfiles.empty()) {
        PrintTestFail("In-memory config was corrupted despite load failure");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 11: End-to-end launch with ConfiguredAppLauncher (Override > Profile > Parent)
static bool Test_Config_EndToEndLaunch() {
    PrintTestHeader("11. End-to-end launch (Override > Profile > Parent)");

    auto probeExe = GetProbeExePath();
    if (!std::filesystem::exists(probeExe)) {
        PrintTestFail("env_probe.exe not found");
        return false;
    }

    // Set up parent variables using RAII
    ScopedEnvironmentVariable scopedParent(L"ENV_TEST_PARENT", L"parent_val");
    ScopedEnvironmentVariable scopedProfile(L"ENV_TEST_PROFILE", L"parent_val");
    ScopedEnvironmentVariable scopedOverride(L"ENV_TEST_OVERRIDE", L"parent_val");

    auto sandbox = CreateTestSandboxDir(L"p2_e2e");

    ConfigModel config;
    config.version = 1;

    EnvironmentProfile profile;
    profile.id = L"prof_test";
    profile.name = L"Test Profile";
    profile.variables[L"ENV_TEST_PROFILE"] = L"profile_won";
    profile.variables[L"ENV_TEST_OVERRIDE"] = L"profile_val";
    config.environmentProfiles.push_back(profile);

    AppProfile app;
    app.id = L"app_test";
    app.name = L"Test Probe App";
    app.executable = probeExe.wstring();
    app.environmentProfileId = L"prof_test";
    app.environmentOverrides[L"ENV_TEST_OVERRIDE"] = L"override_won";
    config.applications.push_back(app);

    // Query each variable via probe
    auto checkVar = [&](const std::wstring& varName) -> std::string {
        auto outPath = sandbox / (varName + L".txt");
        AppProfile testApp = app;
        testApp.arguments = varName + L" \"" + outPath.wstring() + L"\"";
        ConfigModel m = config;
        m.applications[0] = testApp;

        auto res = ConfiguredAppLauncher::LaunchAppSync(m, L"app_test", 10000, AppLaunchMode::Normal);
        if (!res.success || res.exitCode != 0) {
            return "__LAUNCH_FAILED__";
        }
        return ReadFileContent(outPath);
    };

    std::string valParent = checkVar(L"ENV_TEST_PARENT");
    std::string valProfile = checkVar(L"ENV_TEST_PROFILE");
    std::string valOverride = checkVar(L"ENV_TEST_OVERRIDE");

    std::filesystem::remove_all(sandbox);

    if (valParent != "parent_val") {
        PrintTestFail("ENV_TEST_PARENT was not inherited from parent");
        return false;
    }
    if (valProfile != "profile_won") {
        PrintTestFail("ENV_TEST_PROFILE profile value did not override parent");
        return false;
    }
    if (valOverride != "override_won") {
        PrintTestFail("ENV_TEST_OVERRIDE app override did not override profile and parent");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 12: Parent-only launch mode (AppLaunchMode::ParentEnvironmentOnly)
static bool Test_ParentOnlyLaunchMode() {
    PrintTestHeader("12. Parent-only launch mode");

    auto probeExe = GetProbeExePath();
    if (!std::filesystem::exists(probeExe)) {
        PrintTestFail("env_probe.exe not found");
        return false;
    }

    ScopedEnvironmentVariable scopedParent(L"ENV_TEST_PARENT", L"parent_val");
    ScopedEnvironmentVariable scopedProfile(L"ENV_TEST_PROFILE", L"parent_val");
    ScopedEnvironmentVariable scopedOverride(L"ENV_TEST_OVERRIDE", L"parent_val");

    auto sandbox = CreateTestSandboxDir(L"p2_parentonly");

    ConfigModel config;
    config.version = 1;

    EnvironmentProfile profile;
    profile.id = L"prof_test";
    profile.name = L"Test Profile";
    profile.variables[L"ENV_TEST_PROFILE"] = L"profile_val";
    config.environmentProfiles.push_back(profile);

    AppProfile app;
    app.id = L"app_test";
    app.name = L"Test Probe App";
    app.executable = probeExe.wstring();
    app.environmentProfileId = L"prof_test";
    app.environmentOverrides[L"ENV_TEST_OVERRIDE"] = L"override_val";
    config.applications.push_back(app);

    auto checkVar = [&](const std::wstring& varName) -> std::string {
        auto outPath = sandbox / (varName + L".txt");
        AppProfile testApp = app;
        testApp.arguments = varName + L" \"" + outPath.wstring() + L"\"";
        ConfigModel m = config;
        m.applications[0] = testApp;

        auto res = ConfiguredAppLauncher::LaunchAppSync(m, L"app_test", 10000, AppLaunchMode::ParentEnvironmentOnly);
        if (!res.success || res.exitCode != 0) {
            return "__LAUNCH_FAILED__";
        }
        return ReadFileContent(outPath);
    };

    std::string valParent = checkVar(L"ENV_TEST_PARENT");
    std::string valProfile = checkVar(L"ENV_TEST_PROFILE");
    std::string valOverride = checkVar(L"ENV_TEST_OVERRIDE");

    std::filesystem::remove_all(sandbox);

    if (valParent != "parent_val") {
        PrintTestFail("ENV_TEST_PARENT was not inherited");
        return false;
    }
    if (valProfile != "parent_val") {
        PrintTestFail("ENV_TEST_PROFILE was modified despite ParentEnvironmentOnly mode");
        return false;
    }
    if (valOverride != "parent_val") {
        PrintTestFail("ENV_TEST_OVERRIDE was modified despite ParentEnvironmentOnly mode");
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
    std::cout << " EnvLauncher Phase 2 Verification Suite " << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0;
    int total = 12;

    if (Test_StringUtils_ValidConversions()) passed++;
    if (Test_StringUtils_InvalidUtf8()) passed++;
    if (Test_StringUtils_InvalidUtf16()) passed++;
    if (Test_EmbeddedNull_Rejection()) passed++;
    if (Test_CaseInsensitiveCollision_Rejection()) passed++;
    if (Test_JsonStructuralValidation()) passed++;
    if (Test_SemanticValidation()) passed++;
    if (Test_DefaultConfig_FirstRun()) passed++;
    if (Test_Config_SaveAndLoad_Roundtrip()) passed++;
    if (Test_CorruptedJson_FilePreservation()) passed++;
    if (Test_Config_EndToEndLaunch()) passed++;
    if (Test_ParentOnlyLaunchMode()) passed++;

    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << passed << " / " << total << " tests passed." << std::endl;
    std::cout << "========================================" << std::endl;

    CoUninitialize();

    return (passed == total) ? 0 : 1;
}
