#include "util/Win32Error.h"

namespace envlauncher {

std::wstring Win32Error::Format(DWORD errorCode) {
    if (errorCode == 0) {
        return L"Success";
    }

    LPWSTR buffer = nullptr;
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    std::wstring message;
    if (size > 0 && buffer != nullptr) {
        message.assign(buffer, size);
        LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
            message.pop_back();
        }
    } else {
        message = L"Unknown error (code " + std::to_wstring(errorCode) + L")";
    }

    return L"Error " + std::to_wstring(errorCode) + L": " + message;
}

} // namespace envlauncher
