#pragma once

#include <string>
#include <string_view>
#include <stdexcept>

namespace envlauncher {

class StringConversionError : public std::runtime_error {
public:
    explicit StringConversionError(const std::string& message)
        : std::runtime_error(message) {}
};

class StringUtils {
public:
    // Strictly converts a UTF-8 string to UTF-16 (std::wstring).
    // Rejects invalid UTF-8 byte sequences using MB_ERR_INVALID_CHARS.
    // Throws StringConversionError on invalid input.
    static std::wstring Utf8ToWide(std::string_view utf8);

    // Strictly converts a UTF-16 string (std::wstring) to UTF-8.
    // Rejects invalid/unpaired surrogate code units using WC_ERR_INVALID_CHARS.
    // Throws StringConversionError on invalid input.
    static std::string WideToUtf8(std::wstring_view wide);

    // Checks if the given string contains an embedded NUL character.
    static bool ContainsEmbeddedNull(std::string_view str) noexcept;
    static bool ContainsEmbeddedNull(std::wstring_view str) noexcept;
};

} // namespace envlauncher
