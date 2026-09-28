#include "process/EnvironmentBuilder.h"
#include "model/EnvironmentProfile.h"
#include <algorithm>

namespace envlauncher {

EnvironmentSnapshot EnvironmentBuilder::CaptureCurrentEnvironment() {
    EnvironmentSnapshot snapshot;
    LPWCH envStrings = GetEnvironmentStringsW();
    if (!envStrings) {
        return snapshot;
    }

    LPWCH current = envStrings;
    while (*current != L'\0') {
        std::wstring entry(current);
        if (!entry.empty()) {
            if (entry[0] == L'=') {
                snapshot.specialEntries.push_back(std::move(entry));
            } else {
                size_t eqPos = entry.find(L'=');
                if (eqPos != std::wstring::npos) {
                    std::wstring name = entry.substr(0, eqPos);
                    std::wstring value = entry.substr(eqPos + 1);
                    snapshot.variables[std::move(name)] = std::move(value);
                }
            }
        }
        current += entry.length() + 1;
    }

    FreeEnvironmentStringsW(envStrings);
    return snapshot;
}

EnvironmentSnapshot EnvironmentBuilder::MergeEnvironment(
    const EnvironmentSnapshot& baseSnapshot,
    const EnvironmentProfile* profile,
    const EnvMap& overrides
) {
    EnvironmentSnapshot merged;
    merged.specialEntries = baseSnapshot.specialEntries;
    merged.variables = baseSnapshot.variables;

    if (profile != nullptr) {
        for (const auto& [k, v] : profile->variables) {
            merged.variables[k] = v;
        }
    }

    for (const auto& [k, v] : overrides) {
        merged.variables[k] = v;
    }

    return merged;
}

std::vector<wchar_t> EnvironmentBuilder::BuildEnvironmentBlock(const EnvironmentSnapshot& snapshot) {
    if (snapshot.specialEntries.empty() && snapshot.variables.empty()) {
        return { L'\0', L'\0' };
    }

    std::vector<std::wstring> allEntries;
    allEntries.reserve(snapshot.specialEntries.size() + snapshot.variables.size());

    for (const auto& special : snapshot.specialEntries) {
        allEntries.push_back(special);
    }

    for (const auto& [k, v] : snapshot.variables) {
        allEntries.push_back(k + L"=" + v);
    }

    std::sort(allEntries.begin(), allEntries.end(), [](const std::wstring& lhs, const std::wstring& rhs) {
        return CompareStringOrdinal(
            lhs.data(), static_cast<int>(lhs.size()),
            rhs.data(), static_cast<int>(rhs.size()),
            TRUE
        ) == CSTR_LESS_THAN;
    });

    std::vector<wchar_t> block;
    // Estimate size to avoid reallocations
    size_t totalLen = 2; // for double null terminator
    for (const auto& entry : allEntries) {
        totalLen += entry.length() + 1;
    }
    block.reserve(totalLen);

    for (const auto& entry : allEntries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');

    return block;
}

std::vector<wchar_t> EnvironmentBuilder::BuildEnvironmentBlock(const EnvMap& variables) {
    EnvironmentSnapshot snapshot;
    snapshot.variables = variables;
    return BuildEnvironmentBlock(snapshot);
}

} // namespace envlauncher
