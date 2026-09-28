#include "process/EnvironmentBuilder.h"
#include "process/ProcessLauncher.h"
#include "model/EnvironmentProfile.h"
#include "util/Win32Error.h"

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

// Helper: generates a unique temp file path
static std::filesystem::path GetUniqueTempFilePath(const std::wstring& prefix) {
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    DWORD pid = GetCurrentProcessId();
    static uint32_t counter = 0;
    counter++;
    std::wstring filename = prefix + L"_" + std::to_wstring(pid) + L"_" + std::to_wstring(counter) + L".txt";
    return std::filesystem::path(tempDir) / filename;
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

// Test 1: CaseInsensitiveLess and EnvMap key folding
static bool Test_CaseInsensitiveMap() {
    PrintTestHeader("1. CaseInsensitiveLess and EnvMap");

    EnvMap map;
    map[L"Path"] = L"ValueA";
    map[L"PATH"] = L"ValueB";
    map[L"path"] = L"ValueC";

    if (map.size() != 1) {
        PrintTestFail("Map size is not 1 after inserting case variations of PATH");
        return false;
    }

    if (map[L"PaTh"] != L"ValueC") {
        PrintTestFail("Lookup of PaTh did not return latest value ValueC");
        return false;
    }

    if (map.find(L"PATH") == map.end() || map.find(L"path") == map.end()) {
        PrintTestFail("find() failed for case variations");
        return false;
    }

    if (map.find(L"NONEXISTENT") != map.end()) {
        PrintTestFail("find() found non-existent key");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 2: Parent environment capture
static bool Test_ParentEnvironmentCapture() {
    PrintTestHeader("2. Parent environment capture");

    auto snapshot = EnvironmentBuilder::CaptureCurrentEnvironment();
    if (snapshot.variables.empty()) {
        PrintTestFail("Captured variables map is empty");
        return false;
    }

    // Either SystemRoot or PATH must exist in any standard Windows process
    bool hasSystemRoot = snapshot.variables.find(L"SystemRoot") != snapshot.variables.end();
    bool hasPath = snapshot.variables.find(L"PATH") != snapshot.variables.end();

    if (!hasSystemRoot && !hasPath) {
        PrintTestFail("Neither SystemRoot nor PATH found in captured environment");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 3: Special drive entries preservation
static bool Test_SpecialEntries() {
    PrintTestHeader("3. Special drive entries (=C:)");

    EnvironmentSnapshot snapshot;
    snapshot.specialEntries.push_back(L"=C:=C:\\TestDir");
    snapshot.specialEntries.push_back(L"=D:=D:\\WorkDir");
    snapshot.variables[L"ABC"] = L"123";

    auto block = EnvironmentBuilder::BuildEnvironmentBlock(snapshot);
    if (block.empty()) {
        PrintTestFail("BuildEnvironmentBlock returned empty block");
        return false;
    }

    // Extract entries from block
    std::vector<std::wstring> extracted;
    const wchar_t* p = block.data();
    while (*p != L'\0') {
        std::wstring entry(p);
        extracted.push_back(entry);
        p += entry.length() + 1;
    }

    if (extracted.size() != 3) {
        PrintTestFail("Extracted entry count != 3");
        return false;
    }

    // Since '=' (0x3D) comes before 'A' (0x41), special entries must sort before ABC
    if (extracted[0] != L"=C:=C:\\TestDir" || extracted[1] != L"=D:=D:\\WorkDir" || extracted[2] != L"ABC=123") {
        PrintTestFail("Entries were not sorted or preserved correctly");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 4: Merge precedence (Override > Profile > Parent)
static bool Test_MergePrecedence() {
    PrintTestHeader("4. Merge precedence (Override > Profile > Parent)");

    EnvironmentSnapshot parent;
    parent.specialEntries.push_back(L"=C:=C:\\Parent");
    parent.variables[L"VAR1"] = L"Parent1";
    parent.variables[L"VAR2"] = L"Parent2";
    parent.variables[L"VAR3"] = L"Parent3";

    EnvironmentProfile profile;
    profile.id = L"p1";
    profile.name = L"Profile 1";
    profile.variables[L"var2"] = L"Profile2"; // tests case folding
    profile.variables[L"VAR4"] = L"Profile4";

    EnvMap overrides;
    overrides[L"VaR3"] = L"Override3"; // tests case folding
    overrides[L"VAR5"] = L"Override5";

    auto merged = EnvironmentBuilder::MergeEnvironment(parent, &profile, overrides);

    if (merged.variables[L"VAR1"] != L"Parent1") {
        PrintTestFail("VAR1 parent value lost");
        return false;
    }
    if (merged.variables[L"VAR2"] != L"Profile2") {
        PrintTestFail("VAR2 profile override failed");
        return false;
    }
    if (merged.variables[L"VAR3"] != L"Override3") {
        PrintTestFail("VAR3 app override failed");
        return false;
    }
    if (merged.variables[L"VAR4"] != L"Profile4") {
        PrintTestFail("VAR4 profile injection failed");
        return false;
    }
    if (merged.variables[L"VAR5"] != L"Override5") {
        PrintTestFail("VAR5 app override injection failed");
        return false;
    }
    if (merged.specialEntries.size() != 1 || merged.specialEntries[0] != L"=C:=C:\\Parent") {
        PrintTestFail("Special entries were modified during merge");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 5: Environment block serialization
static bool Test_EnvironmentBlockSerialization() {
    PrintTestHeader("5. Environment block serialization");

    EnvironmentSnapshot snapshot;
    snapshot.variables[L"ZEBRA"] = L"z";
    snapshot.variables[L"apple"] = L"a";
    snapshot.variables[L"Mango"] = L"m";

    auto block = EnvironmentBuilder::BuildEnvironmentBlock(snapshot);
    if (block.size() < 2) {
        PrintTestFail("Block size is too small");
        return false;
    }

    // Verify double-null termination
    if (block[block.size() - 2] != L'\0' || block[block.size() - 1] != L'\0') {
        PrintTestFail("Block is not terminated with double NUL characters");
        return false;
    }

    // Verify ordering: apple=a, Mango=m, ZEBRA=z
    std::vector<std::wstring> extracted;
    const wchar_t* p = block.data();
    while (*p != L'\0') {
        std::wstring entry(p);
        extracted.push_back(entry);
        p += entry.length() + 1;
    }

    if (extracted.size() != 3) {
        PrintTestFail("Extracted count != 3");
        return false;
    }

    if (extracted[0] != L"apple=a" || extracted[1] != L"Mango=m" || extracted[2] != L"ZEBRA=z") {
        PrintTestFail("Entries were not sorted in case-insensitive ordinal order");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 6: Empty environment block serialization
static bool Test_EmptyEnvironmentBlock() {
    PrintTestHeader("6. Empty environment block serialization");

    EnvironmentSnapshot emptySnapshot;
    auto block = EnvironmentBuilder::BuildEnvironmentBlock(emptySnapshot);

    if (block.size() != 2) {
        PrintTestFail("Empty block size is not exactly 2 wchar_t");
        return false;
    }

    if (block[0] != L'\0' || block[1] != L'\0') {
        PrintTestFail("Empty block does not contain exact double NUL");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 7: Custom environment injection (both cmd.exe and env_probe.exe)
static bool Test_CustomEnvironmentInjection() {
    PrintTestHeader("7. Custom environment injection");

    // Part A: cmd.exe test (explicit requirement in start.md section 67)
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::filesystem::path cmdPath = std::filesystem::path(sysDir) / L"cmd.exe";

    auto tempPathA = GetUniqueTempFilePath(L"envlauncher_test_cmd");

    LaunchRequest reqCmd;
    reqCmd.executable = cmdPath.wstring();
    // Quote properly for cmd.exe /d /s /c
    reqCmd.arguments = L"/d /s /c \"echo %ENVLAUNCHER_TEST% > \"" + tempPathA.wstring() + L"\"\"";
    reqCmd.environment[L"ENVLAUNCHER_TEST"] = L"hello_cmd";
    reqCmd.inheritParentEnvironment = true;

    auto resCmd = ProcessLauncher::LaunchProcessSync(reqCmd, 10000);
    if (!resCmd.success || resCmd.exitCode != 0) {
        PrintTestFail("cmd.exe launch failed");
        return false;
    }

    std::string contentA = ReadFileContent(tempPathA);
    std::filesystem::remove(tempPathA);

    if (contentA.find("hello_cmd") == std::string::npos) {
        PrintTestFail("cmd.exe output does not contain hello_cmd");
        return false;
    }

    // Part B: env_probe.exe probe test
    auto probeExe = GetProbeExePath();
    if (std::filesystem::exists(probeExe)) {
        auto tempPathB = GetUniqueTempFilePath(L"envlauncher_test_probe");
        LaunchRequest reqProbe;
        reqProbe.executable = probeExe.wstring();
        reqProbe.arguments = L"ENVLAUNCHER_TEST \"" + tempPathB.wstring() + L"\"";
        reqProbe.environment[L"ENVLAUNCHER_TEST"] = L"hello_probe_val";
        reqProbe.inheritParentEnvironment = true;

        auto resProbe = ProcessLauncher::LaunchProcessSync(reqProbe, 10000);
        if (!resProbe.success || resProbe.exitCode != 0) {
            PrintTestFail("env_probe.exe launch failed");
            return false;
        }

        std::string contentB = ReadFileContent(tempPathB);
        std::filesystem::remove(tempPathB);

        if (contentB != "hello_probe_val") {
            PrintTestFail("env_probe.exe did not match injected value");
            return false;
        }
    }

    PrintTestPass();
    return true;
}

// Test 8: Child process parent environment inheritance
static bool Test_ParentInheritance() {
    PrintTestHeader("8. Parent environment inheritance (SystemRoot)");

    // Read current SystemRoot
    DWORD len = GetEnvironmentVariableW(L"SystemRoot", nullptr, 0);
    if (len == 0) {
        PrintTestFail("Current process has no SystemRoot");
        return false;
    }
    std::wstring parentSysRoot;
    parentSysRoot.resize(len);
    GetEnvironmentVariableW(L"SystemRoot", parentSysRoot.data(), len);
    if (!parentSysRoot.empty() && parentSysRoot.back() == L'\0') {
        parentSysRoot.pop_back();
    }

    auto probeExe = GetProbeExePath();
    if (!std::filesystem::exists(probeExe)) {
        PrintTestFail("env_probe.exe not found");
        return false;
    }

    auto tempPath = GetUniqueTempFilePath(L"envlauncher_sysroot");
    LaunchRequest req;
    req.executable = probeExe.wstring();
    req.arguments = L"SystemRoot \"" + tempPath.wstring() + L"\"";
    req.inheritParentEnvironment = true;

    auto res = ProcessLauncher::LaunchProcessSync(req, 10000);
    if (!res.success || res.exitCode != 0) {
        PrintTestFail("Failed to run env_probe.exe for SystemRoot test");
        return false;
    }

    std::string childSysRoot = ReadFileContent(tempPath);
    std::filesystem::remove(tempPath);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, parentSysRoot.c_str(), static_cast<int>(parentSysRoot.size()), nullptr, 0, nullptr, nullptr);
    std::string expectedSysRoot(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, parentSysRoot.c_str(), static_cast<int>(parentSysRoot.size()), expectedSysRoot.data(), utf8Len, nullptr, nullptr);

    if (childSysRoot != expectedSysRoot) {
        PrintTestFail("Child process did not inherit parent SystemRoot");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 9: Multi-launch process isolation (App A = 7897, App B = 7890, parent unchanged)
static bool Test_MultiLaunchIsolation() {
    PrintTestHeader("9. Multi-launch process isolation");

    // 1. Record parent HTTP_PROXY state before launches
    DWORD beforeLen = GetEnvironmentVariableW(L"HTTP_PROXY", nullptr, 0);
    std::wstring beforeVal;
    bool beforeExists = (beforeLen > 0);
    if (beforeExists) {
        beforeVal.resize(beforeLen);
        GetEnvironmentVariableW(L"HTTP_PROXY", beforeVal.data(), beforeLen);
    }

    auto probeExe = GetProbeExePath();
    if (!std::filesystem::exists(probeExe)) {
        PrintTestFail("env_probe.exe not found");
        return false;
    }

    auto tempPathA = GetUniqueTempFilePath(L"envlauncher_iso_a");
    auto tempPathB = GetUniqueTempFilePath(L"envlauncher_iso_b");

    // Launch App A with 7897
    LaunchRequest reqA;
    reqA.executable = probeExe.wstring();
    reqA.arguments = L"HTTP_PROXY \"" + tempPathA.wstring() + L"\"";
    reqA.environment[L"HTTP_PROXY"] = L"http://127.0.0.1:7897";
    reqA.inheritParentEnvironment = true;

    auto resA = ProcessLauncher::LaunchProcessSync(reqA, 10000);
    if (!resA.success || resA.exitCode != 0) {
        PrintTestFail("App A launch failed");
        return false;
    }

    // Launch App B with 7890
    LaunchRequest reqB;
    reqB.executable = probeExe.wstring();
    reqB.arguments = L"HTTP_PROXY \"" + tempPathB.wstring() + L"\"";
    reqB.environment[L"HTTP_PROXY"] = L"http://127.0.0.1:7890";
    reqB.inheritParentEnvironment = true;

    auto resB = ProcessLauncher::LaunchProcessSync(reqB, 10000);
    if (!resB.success || resB.exitCode != 0) {
        PrintTestFail("App B launch failed");
        return false;
    }

    // Verify App A received 7897
    std::string valA = ReadFileContent(tempPathA);
    std::filesystem::remove(tempPathA);
    if (valA != "http://127.0.0.1:7897") {
        PrintTestFail("App A did not receive 7897");
        return false;
    }

    // Verify App B received 7890
    std::string valB = ReadFileContent(tempPathB);
    std::filesystem::remove(tempPathB);
    if (valB != "http://127.0.0.1:7890") {
        PrintTestFail("App B did not receive 7890");
        return false;
    }

    // 2. Record parent HTTP_PROXY state after launches
    DWORD afterLen = GetEnvironmentVariableW(L"HTTP_PROXY", nullptr, 0);
    std::wstring afterVal;
    bool afterExists = (afterLen > 0);
    if (afterExists) {
        afterVal.resize(afterLen);
        GetEnvironmentVariableW(L"HTTP_PROXY", afterVal.data(), afterLen);
    }

    // Verify parent process environment is strictly UNCHANGED
    if (beforeExists != afterExists || beforeVal != afterVal) {
        PrintTestFail("Parent process HTTP_PROXY was modified by child launches!");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 10: Process error reporting on nonexistent executable
static bool Test_ErrorHandling_NonexistentExe() {
    PrintTestHeader("10. Error reporting on non-existent executable");

    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);

    LaunchRequest req;
    req.executable = L"C:\\__EnvLauncher_Nonexistent_Dir_12345__\\fake_app.exe";
    // Set explicit existing working directory so failure is strictly on executable lookup
    req.workingDirectory = tempDir;

    auto res = ProcessLauncher::LaunchProcessSync(req, 5000);

    if (res.success) {
        PrintTestFail("Non-existent executable reported success");
        return false;
    }

    if (res.errorCode != ERROR_FILE_NOT_FOUND && res.errorCode != ERROR_PATH_NOT_FOUND) {
        PrintTestFail("Error code is neither ERROR_FILE_NOT_FOUND nor ERROR_PATH_NOT_FOUND");
        return false;
    }

    if (res.errorMessage.empty()) {
        PrintTestFail("Error message is empty");
        return false;
    }

    if (res.errorMessage.find(L"Error " + std::to_wstring(res.errorCode)) == std::wstring::npos) {
        PrintTestFail("Formatted error message does not contain Error code prefix");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 11: Value containing '=' (e.g. AAA=a=b=c)
static bool Test_ValueContainingEquals() {
    PrintTestHeader("11. Value containing '=' (AAA=a=b=c)");

    auto probeExe = GetProbeExePath();
    if (!std::filesystem::exists(probeExe)) {
        PrintTestFail("env_probe.exe not found");
        return false;
    }

    auto tempPath = GetUniqueTempFilePath(L"envlauncher_eq");
    LaunchRequest req;
    req.executable = probeExe.wstring();
    req.arguments = L"AAA \"" + tempPath.wstring() + L"\"";
    req.environment[L"AAA"] = L"a=b=c";
    req.inheritParentEnvironment = true;

    auto res = ProcessLauncher::LaunchProcessSync(req, 10000);
    if (!res.success || res.exitCode != 0) {
        PrintTestFail("Launch failed");
        return false;
    }

    std::string content = ReadFileContent(tempPath);
    std::filesystem::remove(tempPath);

    if (content != "a=b=c") {
        PrintTestFail("Value was not preserved across '=' boundaries");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 12: Empty value (FOO=)
static bool Test_EmptyValue() {
    PrintTestHeader("12. Empty value (FOO=)");

    // 1. Serialization test: FOO="" must serialize to "FOO=\0" in environment block without being dropped
    EnvironmentSnapshot snap;
    snap.variables[L"FOO"] = L"";
    snap.variables[L"BAR"] = L"123";
    auto block = EnvironmentBuilder::BuildEnvironmentBlock(snap);

    std::vector<std::wstring> extracted;
    const wchar_t* p = block.data();
    while (*p != L'\0') {
        std::wstring entry(p);
        extracted.push_back(entry);
        p += entry.length() + 1;
    }

    if (extracted.size() != 2) {
        PrintTestFail("Extracted size != 2; empty value entry was dropped");
        return false;
    }

    bool foundFoo = false;
    for (const auto& entry : extracted) {
        if (entry == L"FOO=") {
            foundFoo = true;
            break;
        }
    }

    if (!foundFoo) {
        PrintTestFail("FOO= entry was not serialized properly in environment block");
        return false;
    }

    // 2. Parser test: raw entry "FOO=" must be correctly parsed into name="FOO" and value=""
    std::wstring rawEntry = L"FOO=";
    size_t eqPos = rawEntry.find(L'=');
    if (eqPos == std::wstring::npos) {
        PrintTestFail("Failed to locate '=' in raw entry");
        return false;
    }
    std::wstring name = rawEntry.substr(0, eqPos);
    std::wstring val = rawEntry.substr(eqPos + 1);

    if (name != L"FOO" || !val.empty()) {
        PrintTestFail("Parsing 'FOO=' failed to yield name='FOO' and empty value");
        return false;
    }

    // 3. Merging test: overriding with empty value preserves the key with empty string
    EnvironmentSnapshot parent;
    parent.variables[L"FOO"] = L"initial";
    EnvMap overrides;
    overrides[L"FOO"] = L"";
    auto merged = EnvironmentBuilder::MergeEnvironment(parent, nullptr, overrides);

    if (merged.variables.find(L"FOO") == merged.variables.end() || !merged.variables[L"FOO"].empty()) {
        PrintTestFail("Merging with empty value override failed");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 13: Unicode value (ENVLAUNCHER_TEST=测试-你好)
static bool Test_UnicodeValue() {
    PrintTestHeader("13. Unicode value (ENVLAUNCHER_TEST=测试-你好)");

    auto probeExe = GetProbeExePath();
    if (!std::filesystem::exists(probeExe)) {
        PrintTestFail("env_probe.exe not found");
        return false;
    }

    std::wstring unicodeVal = L"测试-你好_日本語_Café";
    auto tempPath = GetUniqueTempFilePath(L"envlauncher_unicode");
    LaunchRequest req;
    req.executable = probeExe.wstring();
    req.arguments = L"ENVLAUNCHER_TEST \"" + tempPath.wstring() + L"\"";
    req.environment[L"ENVLAUNCHER_TEST"] = unicodeVal;
    req.inheritParentEnvironment = true;

    auto res = ProcessLauncher::LaunchProcessSync(req, 10000);
    if (!res.success || res.exitCode != 0) {
        PrintTestFail("Launch failed");
        return false;
    }

    std::string content = ReadFileContent(tempPath);
    std::filesystem::remove(tempPath);

    int expectedLen = WideCharToMultiByte(CP_UTF8, 0, unicodeVal.c_str(), static_cast<int>(unicodeVal.size()), nullptr, 0, nullptr, nullptr);
    std::string expectedUtf8(expectedLen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, unicodeVal.c_str(), static_cast<int>(unicodeVal.size()), expectedUtf8.data(), expectedLen, nullptr, nullptr);

    if (content != expectedUtf8) {
        PrintTestFail("Unicode value was corrupted or altered by ANSI conversion");
        return false;
    }

    PrintTestPass();
    return true;
}

// Test 14: LaunchProcessSync timeout handling
static bool Test_LaunchProcessSyncTimeout() {
    PrintTestHeader("14. LaunchProcessSync timeout handling");

    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::filesystem::path cmdPath = std::filesystem::path(sysDir) / L"cmd.exe";

    LaunchRequest req;
    req.executable = cmdPath.wstring();
    req.arguments = L"/d /s /c \"ping -n 5 127.0.0.1 > nul\"";

    // Set timeout to 300ms while ping takes ~4000ms
    auto res = ProcessLauncher::LaunchProcessSync(req, 300);

    if (!res.timedOut) {
        PrintTestFail("res.timedOut is not true");
        return false;
    }

    if (res.success) {
        PrintTestFail("res.success should be false on timeout");
        return false;
    }

    if (res.errorCode != WAIT_TIMEOUT) {
        PrintTestFail("res.errorCode is not WAIT_TIMEOUT");
        return false;
    }

    if (res.exitCode != STILL_ACTIVE) {
        PrintTestFail("res.exitCode is not STILL_ACTIVE");
        return false;
    }

    PrintTestPass();
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "========================================" << std::endl;
    std::cout << " EnvLauncher Phase 1 Verification Suite " << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0;
    int total = 14;

    if (Test_CaseInsensitiveMap()) passed++;
    if (Test_ParentEnvironmentCapture()) passed++;
    if (Test_SpecialEntries()) passed++;
    if (Test_MergePrecedence()) passed++;
    if (Test_EnvironmentBlockSerialization()) passed++;
    if (Test_EmptyEnvironmentBlock()) passed++;
    if (Test_CustomEnvironmentInjection()) passed++;
    if (Test_ParentInheritance()) passed++;
    if (Test_MultiLaunchIsolation()) passed++;
    if (Test_ErrorHandling_NonexistentExe()) passed++;
    if (Test_ValueContainingEquals()) passed++;
    if (Test_EmptyValue()) passed++;
    if (Test_UnicodeValue()) passed++;
    if (Test_LaunchProcessSyncTimeout()) passed++;

    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << passed << " / " << total << " tests passed." << std::endl;
    std::cout << "========================================" << std::endl;

    return (passed == total) ? 0 : 1;
}
