#include "config/ConfigManager.h"
#include "util/StringUtils.h"
#include "util/Win32Error.h"

#include <yyjson.h>
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <set>
#include <cstdlib>

namespace envlauncher {

static ConfigResult ValidateJsonStructure(const yyjson_val* root) {
    if (!yyjson_is_obj(root)) {
        return { false, L"Root JSON value must be an object." };
    }
    const yyjson_val* vVal = yyjson_obj_get(root, "version");
    if (!vVal || !yyjson_is_int(vVal)) {
        return { false, L"Configuration root must contain an integer 'version' field." };
    }
    const yyjson_val* pArr = yyjson_obj_get(root, "environmentProfiles");
    if (!pArr || !yyjson_is_arr(pArr)) {
        return { false, L"Configuration root must contain an array 'environmentProfiles' field." };
    }
    const yyjson_val* aArr = yyjson_obj_get(root, "applications");
    if (!aArr || !yyjson_is_arr(aArr)) {
        return { false, L"Configuration root must contain an array 'applications' field." };
    }

    size_t pSize = yyjson_arr_size(pArr);
    for (size_t i = 0; i < pSize; ++i) {
        const yyjson_val* p = yyjson_arr_get(pArr, i);
        if (!yyjson_is_obj(p)) {
            return { false, L"environmentProfiles[" + std::to_wstring(i) + L"] must be an object." };
        }
        const yyjson_val* pId = yyjson_obj_get(p, "id");
        if (!pId || !yyjson_is_str(pId)) {
            return { false, L"environmentProfiles[" + std::to_wstring(i) + L"] missing string 'id'." };
        }
        const yyjson_val* pName = yyjson_obj_get(p, "name");
        if (!pName || !yyjson_is_str(pName)) {
            return { false, L"environmentProfiles[" + std::to_wstring(i) + L"] missing string 'name'." };
        }
        const yyjson_val* pVars = yyjson_obj_get(p, "variables");
        if (!pVars || !yyjson_is_obj(pVars)) {
            return { false, L"environmentProfiles[" + std::to_wstring(i) + L"] missing object 'variables'." };
        }

        std::set<std::wstring, CaseInsensitiveLess> seenKeys;
        yyjson_obj_iter iter = yyjson_obj_iter_with(pVars);
        yyjson_val* key;
        while ((key = yyjson_obj_iter_next(&iter))) {
            yyjson_val* val = yyjson_obj_iter_get_val(key);
            const char* keyCStr = yyjson_get_str(key);
            if (!yyjson_is_str(val)) {
                return { false, L"Variable value for key '" + StringUtils::Utf8ToWide(keyCStr ? keyCStr : "") + L"' must be a string." };
            }
            std::wstring keyW;
            try {
                keyW = StringUtils::Utf8ToWide(keyCStr ? keyCStr : "");
            } catch (const std::exception& e) {
                return { false, L"Invalid UTF-8 in variable key: " + StringUtils::Utf8ToWide(e.what()) };
            }
            if (!seenKeys.insert(keyW).second) {
                return { false, L"Duplicate environment variable key '" + keyW + L"' collides case-insensitively in profile." };
            }
        }
    }

    size_t aSize = yyjson_arr_size(aArr);
    for (size_t i = 0; i < aSize; ++i) {
        const yyjson_val* app = yyjson_arr_get(aArr, i);
        if (!yyjson_is_obj(app)) {
            return { false, L"applications[" + std::to_wstring(i) + L"] must be an object." };
        }
        const yyjson_val* aId = yyjson_obj_get(app, "id");
        if (!aId || !yyjson_is_str(aId)) {
            return { false, L"applications[" + std::to_wstring(i) + L"] missing string 'id'." };
        }
        const yyjson_val* aName = yyjson_obj_get(app, "name");
        if (!aName || !yyjson_is_str(aName)) {
            return { false, L"applications[" + std::to_wstring(i) + L"] missing string 'name'." };
        }
        const yyjson_val* aExe = yyjson_obj_get(app, "executable");
        if (!aExe || !yyjson_is_str(aExe)) {
            return { false, L"applications[" + std::to_wstring(i) + L"] missing string 'executable'." };
        }
        const yyjson_val* aArgs = yyjson_obj_get(app, "arguments");
        if (aArgs && !yyjson_is_str(aArgs)) {
            return { false, L"applications[" + std::to_wstring(i) + L"] 'arguments' must be a string." };
        }
        const yyjson_val* aWorkDir = yyjson_obj_get(app, "workingDirectory");
        if (aWorkDir && !yyjson_is_str(aWorkDir)) {
            return { false, L"applications[" + std::to_wstring(i) + L"] 'workingDirectory' must be a string." };
        }
        const yyjson_val* aProfId = yyjson_obj_get(app, "environmentProfileId");
        if (aProfId && !yyjson_is_str(aProfId)) {
            return { false, L"applications[" + std::to_wstring(i) + L"] 'environmentProfileId' must be a string." };
        }
        const yyjson_val* aOverrides = yyjson_obj_get(app, "environmentOverrides");
        if (aOverrides) {
            if (!yyjson_is_obj(aOverrides)) {
                return { false, L"applications[" + std::to_wstring(i) + L"] 'environmentOverrides' must be an object." };
            }
            std::set<std::wstring, CaseInsensitiveLess> seenKeys;
            yyjson_obj_iter iter = yyjson_obj_iter_with(aOverrides);
            yyjson_val* key;
            while ((key = yyjson_obj_iter_next(&iter))) {
                yyjson_val* val = yyjson_obj_iter_get_val(key);
                const char* keyCStr = yyjson_get_str(key);
                if (!yyjson_is_str(val)) {
                    return { false, L"Override value for key '" + StringUtils::Utf8ToWide(keyCStr ? keyCStr : "") + L"' must be a string." };
                }
                std::wstring keyW;
                try {
                    keyW = StringUtils::Utf8ToWide(keyCStr ? keyCStr : "");
                } catch (const std::exception& e) {
                    return { false, L"Invalid UTF-8 in override key: " + StringUtils::Utf8ToWide(e.what()) };
                }
                if (!seenKeys.insert(keyW).second) {
                    return { false, L"Duplicate override key '" + keyW + L"' collides case-insensitively in application." };
                }
            }
        }
    }

    return { true, L"" };
}

ConfigResult ConfigManager::ValidateConfig(const ConfigModel& config) {
    if (config.version != 1) {
        if (config.version > 1) {
            return { false, L"Unsupported config version " + std::to_wstring(config.version) + L". This build supports version 1." };
        }
        return { false, L"Invalid config version: " + std::to_wstring(config.version) };
    }

    std::set<std::wstring> profileIds;
    for (const auto& profile : config.environmentProfiles) {
        if (profile.id.empty()) {
            return { false, L"Profile ID cannot be empty." };
        }
        if (StringUtils::ContainsEmbeddedNull(profile.id)) {
            return { false, L"Profile ID cannot contain embedded NUL characters." };
        }
        if (profile.name.empty()) {
            return { false, L"Profile name cannot be empty." };
        }
        if (StringUtils::ContainsEmbeddedNull(profile.name)) {
            return { false, L"Profile name cannot contain embedded NUL characters." };
        }
        if (!profileIds.insert(profile.id).second) {
            return { false, L"Duplicate profile ID detected: " + profile.id };
        }

        for (const auto& [k, v] : profile.variables) {
            if (k.empty()) {
                return { false, L"Environment variable name in profile '" + profile.name + L"' cannot be empty." };
            }
            if (k.find(L'=') != std::wstring::npos) {
                return { false, L"Environment variable name '" + k + L"' cannot contain '='." };
            }
            if (StringUtils::ContainsEmbeddedNull(k)) {
                return { false, L"Environment variable name cannot contain embedded NUL characters." };
            }
            if (StringUtils::ContainsEmbeddedNull(v)) {
                return { false, L"Environment variable value for '" + k + L"' cannot contain embedded NUL characters." };
            }
        }
    }

    std::set<std::wstring> appIds;
    for (const auto& app : config.applications) {
        if (app.id.empty()) {
            return { false, L"Application ID cannot be empty." };
        }
        if (StringUtils::ContainsEmbeddedNull(app.id)) {
            return { false, L"Application ID cannot contain embedded NUL characters." };
        }
        if (app.name.empty()) {
            return { false, L"Application name cannot be empty." };
        }
        if (StringUtils::ContainsEmbeddedNull(app.name)) {
            return { false, L"Application name cannot contain embedded NUL characters." };
        }
        if (app.executable.empty()) {
            return { false, L"Application executable path cannot be empty." };
        }
        if (StringUtils::ContainsEmbeddedNull(app.executable)) {
            return { false, L"Application executable path cannot contain embedded NUL characters." };
        }
        if (StringUtils::ContainsEmbeddedNull(app.arguments)) {
            return { false, L"Application arguments cannot contain embedded NUL characters." };
        }
        if (StringUtils::ContainsEmbeddedNull(app.workingDirectory)) {
            return { false, L"Application working directory cannot contain embedded NUL characters." };
        }
        if (StringUtils::ContainsEmbeddedNull(app.environmentProfileId)) {
            return { false, L"Application environment profile ID cannot contain embedded NUL characters." };
        }
        if (!appIds.insert(app.id).second) {
            return { false, L"Duplicate application ID detected: " + app.id };
        }

        if (!app.environmentProfileId.empty()) {
            if (config.FindProfileById(app.environmentProfileId) == nullptr) {
                return { false, L"Application '" + app.name + L"' references non-existent environment profile ID: " + app.environmentProfileId };
            }
        }

        for (const auto& [k, v] : app.environmentOverrides) {
            if (k.empty()) {
                return { false, L"Environment override variable name in app '" + app.name + L"' cannot be empty." };
            }
            if (k.find(L'=') != std::wstring::npos) {
                return { false, L"Environment override variable name '" + k + L"' cannot contain '='." };
            }
            if (StringUtils::ContainsEmbeddedNull(k)) {
                return { false, L"Environment override variable name cannot contain embedded NUL characters." };
            }
            if (StringUtils::ContainsEmbeddedNull(v)) {
                return { false, L"Environment override variable value for '" + k + L"' cannot contain embedded NUL characters." };
            }
        }
    }

    return { true, L"" };
}

ConfigManager::ConfigManager()
    : configPath_(GetDefaultConfigPath()) {}

ConfigManager::ConfigManager(std::filesystem::path configPath)
    : configPath_(std::move(configPath)) {}

std::filesystem::path ConfigManager::GetDefaultConfigPath() {
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path);
    if (SUCCEEDED(hr) && path) {
        std::filesystem::path result(path);
        CoTaskMemFree(path);
        return result / L"EnvLauncher" / L"config.json";
    }
    return std::filesystem::path(L"config.json");
}

ConfigResult ConfigManager::Load() {
    std::error_code ec;
    if (!std::filesystem::exists(configPath_, ec)) {
        std::filesystem::path parentDir = configPath_.parent_path();
        if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
            if (!std::filesystem::create_directories(parentDir, ec)) {
                return { false, L"Failed to create configuration directory: " + parentDir.wstring() };
            }
        }

        config_ = ConfigModel{};
        config_.version = 1;
        auto saveRes = Save();
        if (!saveRes.success) {
            return { false, L"Failed to initialize default configuration: " + saveRes.errorMessage };
        }
        return { true, L"" };
    }

    std::ifstream file(configPath_, std::ios::binary);
    if (!file.is_open()) {
        return { false, L"Failed to open configuration file for reading: " + configPath_.wstring() };
    }

    std::string utf8Content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_opts(utf8Content.data(), utf8Content.size(), YYJSON_READ_NOFLAG, nullptr, &err);
    if (!doc) {
        return { false, L"Failed to parse configuration file as valid JSON." };
    }

    struct DocGuard {
        yyjson_doc* d;
        ~DocGuard() { if (d) yyjson_doc_free(d); }
    } docGuard{ doc };

    yyjson_val* root = yyjson_doc_get_root(doc);
    auto structRes = ValidateJsonStructure(root);
    if (!structRes.success) {
        return structRes;
    }

    ConfigModel temp;
    try {
        temp.version = static_cast<int>(yyjson_get_int(yyjson_obj_get(root, "version")));

        const yyjson_val* pArr = yyjson_obj_get(root, "environmentProfiles");
        size_t pSize = yyjson_arr_size(pArr);
        for (size_t i = 0; i < pSize; ++i) {
            const yyjson_val* pJson = yyjson_arr_get(pArr, i);
            EnvironmentProfile profile;
            profile.id = StringUtils::Utf8ToWide(yyjson_get_str(yyjson_obj_get(pJson, "id")));
            profile.name = StringUtils::Utf8ToWide(yyjson_get_str(yyjson_obj_get(pJson, "name")));
            const yyjson_val* varsObj = yyjson_obj_get(pJson, "variables");
            yyjson_obj_iter vIter = yyjson_obj_iter_with(varsObj);
            yyjson_val* kVal;
            while ((kVal = yyjson_obj_iter_next(&vIter))) {
                yyjson_val* valVal = yyjson_obj_iter_get_val(kVal);
                std::wstring k = StringUtils::Utf8ToWide(yyjson_get_str(kVal));
                std::wstring v = StringUtils::Utf8ToWide(yyjson_get_str(valVal));
                profile.variables[std::move(k)] = std::move(v);
            }
            temp.environmentProfiles.push_back(std::move(profile));
        }

        const yyjson_val* aArr = yyjson_obj_get(root, "applications");
        size_t aSize = yyjson_arr_size(aArr);
        for (size_t i = 0; i < aSize; ++i) {
            const yyjson_val* aJson = yyjson_arr_get(aArr, i);
            AppProfile app;
            app.id = StringUtils::Utf8ToWide(yyjson_get_str(yyjson_obj_get(aJson, "id")));
            app.name = StringUtils::Utf8ToWide(yyjson_get_str(yyjson_obj_get(aJson, "name")));
            app.executable = StringUtils::Utf8ToWide(yyjson_get_str(yyjson_obj_get(aJson, "executable")));

            const yyjson_val* argVal = yyjson_obj_get(aJson, "arguments");
            if (argVal && yyjson_is_str(argVal)) {
                app.arguments = StringUtils::Utf8ToWide(yyjson_get_str(argVal));
            }
            const yyjson_val* wdVal = yyjson_obj_get(aJson, "workingDirectory");
            if (wdVal && yyjson_is_str(wdVal)) {
                app.workingDirectory = StringUtils::Utf8ToWide(yyjson_get_str(wdVal));
            }
            const yyjson_val* profVal = yyjson_obj_get(aJson, "environmentProfileId");
            if (profVal && yyjson_is_str(profVal)) {
                app.environmentProfileId = StringUtils::Utf8ToWide(yyjson_get_str(profVal));
            }
            const yyjson_val* ovVal = yyjson_obj_get(aJson, "environmentOverrides");
            if (ovVal && yyjson_is_obj(ovVal)) {
                yyjson_obj_iter oIter = yyjson_obj_iter_with(ovVal);
                yyjson_val* kVal;
                while ((kVal = yyjson_obj_iter_next(&oIter))) {
                    yyjson_val* valVal = yyjson_obj_iter_get_val(kVal);
                    std::wstring k = StringUtils::Utf8ToWide(yyjson_get_str(kVal));
                    std::wstring v = StringUtils::Utf8ToWide(yyjson_get_str(valVal));
                    app.environmentOverrides[std::move(k)] = std::move(v);
                }
            }
            temp.applications.push_back(std::move(app));
        }
    } catch (const std::exception& e) {
        return { false, L"Deserialization error: " + StringUtils::Utf8ToWide(e.what()) };
    }

    auto semanticRes = ValidateConfig(temp);
    if (!semanticRes.success) {
        return semanticRes;
    }

    config_ = std::move(temp);
    return { true, L"" };
}

ConfigResult ConfigManager::Save() {
    auto validRes = ValidateConfig(config_);
    if (!validRes.success) {
        return validRes;
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        return { false, L"Failed to allocate JSON document." };
    }
    struct MutDocGuard {
        yyjson_mut_doc* d;
        ~MutDocGuard() { if (d) yyjson_mut_doc_free(d); }
    } docGuard{ doc };

    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_int(doc, root, "version", config_.version);

    yyjson_mut_val* profilesArr = yyjson_mut_obj_add_arr(doc, root, "environmentProfiles");

    for (const auto& profile : config_.environmentProfiles) {
        yyjson_mut_val* pObj = yyjson_mut_arr_add_obj(doc, profilesArr);
        std::string pIdUtf8 = StringUtils::WideToUtf8(profile.id);
        std::string pNameUtf8 = StringUtils::WideToUtf8(profile.name);
        yyjson_mut_obj_add_strcpy(doc, pObj, "id", pIdUtf8.c_str());
        yyjson_mut_obj_add_strcpy(doc, pObj, "name", pNameUtf8.c_str());

        yyjson_mut_val* varsObj = yyjson_mut_obj_add_obj(doc, pObj, "variables");

        for (const auto& [k, v] : profile.variables) {
            std::string kUtf8 = StringUtils::WideToUtf8(k);
            std::string vUtf8 = StringUtils::WideToUtf8(v);
            yyjson_mut_val* kVal = yyjson_mut_strcpy(doc, kUtf8.c_str());
            yyjson_mut_val* vVal = yyjson_mut_strcpy(doc, vUtf8.c_str());
            yyjson_mut_obj_add(varsObj, kVal, vVal);
        }
    }

    yyjson_mut_val* appsArr = yyjson_mut_obj_add_arr(doc, root, "applications");

    for (const auto& app : config_.applications) {
        yyjson_mut_val* aObj = yyjson_mut_arr_add_obj(doc, appsArr);
        std::string aIdUtf8 = StringUtils::WideToUtf8(app.id);
        std::string aNameUtf8 = StringUtils::WideToUtf8(app.name);
        std::string aExeUtf8 = StringUtils::WideToUtf8(app.executable);
        std::string aArgsUtf8 = StringUtils::WideToUtf8(app.arguments);
        std::string aWdUtf8 = StringUtils::WideToUtf8(app.workingDirectory);
        std::string aProfIdUtf8 = StringUtils::WideToUtf8(app.environmentProfileId);

        yyjson_mut_obj_add_strcpy(doc, aObj, "id", aIdUtf8.c_str());
        yyjson_mut_obj_add_strcpy(doc, aObj, "name", aNameUtf8.c_str());
        yyjson_mut_obj_add_strcpy(doc, aObj, "executable", aExeUtf8.c_str());
        yyjson_mut_obj_add_strcpy(doc, aObj, "arguments", aArgsUtf8.c_str());
        yyjson_mut_obj_add_strcpy(doc, aObj, "workingDirectory", aWdUtf8.c_str());
        yyjson_mut_obj_add_strcpy(doc, aObj, "environmentProfileId", aProfIdUtf8.c_str());

        yyjson_mut_val* ovObj = yyjson_mut_obj_add_obj(doc, aObj, "environmentOverrides");

        for (const auto& [k, v] : app.environmentOverrides) {
            std::string kUtf8 = StringUtils::WideToUtf8(k);
            std::string vUtf8 = StringUtils::WideToUtf8(v);
            yyjson_mut_val* kVal = yyjson_mut_strcpy(doc, kUtf8.c_str());
            yyjson_mut_val* vVal = yyjson_mut_strcpy(doc, vUtf8.c_str());
            yyjson_mut_obj_add(ovObj, kVal, vVal);
        }
    }

    size_t jsonLen = 0;
    char* jsonStr = yyjson_mut_write_opts(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, nullptr, &jsonLen, nullptr);
    if (!jsonStr) {
        return { false, L"Failed to serialize configuration to JSON." };
    }
    struct JsonStrGuard {
        char* s;
        ~JsonStrGuard() { if (s) free(s); }
    } strGuard{ jsonStr };

    std::error_code ec;
    std::filesystem::path parentDir = configPath_.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
        if (!std::filesystem::create_directories(parentDir, ec)) {
            return { false, L"Failed to create configuration directory: " + parentDir.wstring() };
        }
    }

    std::filesystem::path tempPath = parentDir / (configPath_.filename().wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp");

    {
        std::ofstream outFile(tempPath, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            return { false, L"Failed to open temporary file for writing: " + tempPath.wstring() };
        }
        outFile.write(jsonStr, jsonLen);
        outFile.flush();
        if (!outFile.good()) {
            outFile.close();
            std::filesystem::remove(tempPath, ec);
            return { false, L"Failed to write configuration data to temporary file." };
        }
    }

    if (std::filesystem::exists(configPath_, ec)) {
        if (!ReplaceFileW(configPath_.c_str(), tempPath.c_str(), nullptr, 0, nullptr, nullptr)) {
            DWORD err = GetLastError();
            std::filesystem::remove(tempPath, ec);
            return { false, L"ReplaceFileW failed: " + Win32Error::Format(err) };
        }
    } else {
        if (!MoveFileExW(tempPath.c_str(), configPath_.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DWORD err = GetLastError();
            std::filesystem::remove(tempPath, ec);
            return { false, L"MoveFileExW failed: " + Win32Error::Format(err) };
        }
    }

    return { true, L"" };
}

} // namespace envlauncher
