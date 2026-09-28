#include "util/StringUtils.h"
#include <windows.h>

namespace envlauncher {

std::wstring StringUtils::Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }

    int requiredSize = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0
    );

    if (requiredSize == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_UNICODE_TRANSLATION) {
            throw StringConversionError("Invalid UTF-8 byte sequence encountered.");
        }
        throw StringConversionError("MultiByteToWideChar failed with Win32 error code: " + std::to_string(err));
    }

    std::wstring result(requiredSize, L'\0');
    int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        result.data(),
        requiredSize
    );

    if (converted == 0) {
        DWORD err = GetLastError();
        throw StringConversionError("MultiByteToWideChar failed during conversion: " + std::to_string(err));
    }

    return result;
}

std::string StringUtils::WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return std::string();
    }

    // When WC_ERR_INVALID_CHARS is set with CP_UTF8, lpDefaultChar and lpUsedDefaultChar must be NULL.
    int requiredSize = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (requiredSize == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_UNICODE_TRANSLATION) {
            throw StringConversionError("Invalid UTF-16 surrogate or code unit encountered.");
        }
        throw StringConversionError("WideCharToMultiByte failed with Win32 error code: " + std::to_string(err));
    }

    std::string result(requiredSize, '\0');
    int converted = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        static_cast<int>(wide.size()),
        result.data(),
        requiredSize,
        nullptr,
        nullptr
    );

    if (converted == 0) {
        DWORD err = GetLastError();
        throw StringConversionError("WideCharToMultiByte failed during conversion: " + std::to_string(err));
    }

    return result;
}

bool StringUtils::ContainsEmbeddedNull(std::string_view str) noexcept {
    return str.find('\0') != std::string_view::npos;
}

bool StringUtils::ContainsEmbeddedNull(std::wstring_view str) noexcept {
    return str.find(L'\0') != std::wstring_view::npos;
}

} // namespace envlauncher
