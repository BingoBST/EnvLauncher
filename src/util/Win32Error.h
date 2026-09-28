#pragma once

#include <windows.h>
#include <string>

namespace envlauncher {

class Win32Error {
public:
    // Formats a Win32 DWORD error code into a descriptive string,
    // e.g.: "Error 2: The system cannot find the file specified."
    static std::wstring Format(DWORD errorCode);
};

} // namespace envlauncher
