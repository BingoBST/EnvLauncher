#pragma once

#include <windows.h>
#include <string>
#include <map>
#include <vector>

namespace envlauncher {

// Case-insensitive comparator for environment variable names using Win32 CompareStringOrdinal.
// Windows environment variable names are case-insensitive and independent of locale.
struct CaseInsensitiveLess {
    bool operator()(const std::wstring& lhs, const std::wstring& rhs) const noexcept {
        const int result = CompareStringOrdinal(
            lhs.data(),
            static_cast<int>(lhs.size()),
            rhs.data(),
            static_cast<int>(rhs.size()),
            TRUE
        );
        return result == CSTR_LESS_THAN;
    }
};

using EnvMap = std::map<std::wstring, std::wstring, CaseInsensitiveLess>;

// Represents a captured environment snapshot separating normal variables from
// special Windows drive current-directory entries (e.g. "=C:=C:\path").
struct EnvironmentSnapshot {
    EnvMap variables;
    std::vector<std::wstring> specialEntries;
};

struct EnvironmentProfile;

class EnvironmentBuilder {
public:
    // Captures the current process's environment block via GetEnvironmentStringsW().
    // Correctly parses regular KEY=VALUE entries and preserves special entries (starting with '=').
    static EnvironmentSnapshot CaptureCurrentEnvironment();

    // Merges parent environment snapshot with an optional profile and application-specific overrides.
    // Precedence: Overrides > Profile > Parent base.
    // Special entries from baseSnapshot are preserved untouched.
    static EnvironmentSnapshot MergeEnvironment(
        const EnvironmentSnapshot& baseSnapshot,
        const EnvironmentProfile* profile,
        const EnvMap& overrides
    );

    // Serializes an EnvironmentSnapshot into a double-null terminated UTF-16 environment block
    // suitable for CreateProcessW with CREATE_UNICODE_ENVIRONMENT.
    // If entries are empty, returns { L'\0', L'\0' }.
    // All entries are sorted in case-insensitive ordinal order.
    static std::vector<wchar_t> BuildEnvironmentBlock(const EnvironmentSnapshot& snapshot);

    // Convenience overload for building an environment block from an EnvMap directly.
    static std::vector<wchar_t> BuildEnvironmentBlock(const EnvMap& variables);
};

} // namespace envlauncher
