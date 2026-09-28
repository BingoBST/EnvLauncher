#pragma once

#include "model/ConfigModel.h"
#include <filesystem>
#include <string>

namespace envlauncher {

struct ConfigResult {
    bool success{false};
    std::wstring errorMessage;
};

class ConfigManager {
public:
    ConfigManager();
    explicit ConfigManager(std::filesystem::path configPath);

    // Loads configuration from disk.
    // If file does not exist, automatically creates directories and saves a valid default config.
    // Preserves in-memory state and leaves disk file untouched if validation/parsing fails.
    ConfigResult Load();

    // Saves current in-memory configuration to disk using atomic temp file replacement.
    ConfigResult Save();

    ConfigModel& GetConfig() noexcept { return config_; }
    const ConfigModel& GetConfig() const noexcept { return config_; }

    const std::filesystem::path& GetConfigPath() const noexcept { return configPath_; }
    void SetConfigPath(std::filesystem::path path) { configPath_ = std::move(path); }

    // Returns standard default path: %LOCALAPPDATA%\EnvLauncher\config.json
    static std::filesystem::path GetDefaultConfigPath();

    // Semantic validation of ConfigModel.
    static ConfigResult ValidateConfig(const ConfigModel& config);

private:
    ConfigModel config_;
    std::filesystem::path configPath_;
};

} // namespace envlauncher
