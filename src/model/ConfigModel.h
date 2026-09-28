#pragma once

#include "model/EnvironmentProfile.h"
#include "model/AppProfile.h"
#include <vector>
#include <string_view>

namespace envlauncher {

struct ConfigModel {
    int version{1};
    std::vector<EnvironmentProfile> environmentProfiles;
    std::vector<AppProfile> applications;

    const EnvironmentProfile* FindProfileById(std::wstring_view id) const noexcept {
        for (const auto& profile : environmentProfiles) {
            if (profile.id == id) {
                return &profile;
            }
        }
        return nullptr;
    }

    EnvironmentProfile* FindProfileById(std::wstring_view id) noexcept {
        for (auto& profile : environmentProfiles) {
            if (profile.id == id) {
                return &profile;
            }
        }
        return nullptr;
    }

    const AppProfile* FindAppById(std::wstring_view id) const noexcept {
        for (const auto& app : applications) {
            if (app.id == id) {
                return &app;
            }
        }
        return nullptr;
    }

    AppProfile* FindAppById(std::wstring_view id) noexcept {
        for (auto& app : applications) {
            if (app.id == id) {
                return &app;
            }
        }
        return nullptr;
    }
};

} // namespace envlauncher
