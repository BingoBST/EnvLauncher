#include <windows.h>
#include <cstdio>
#include <string>

int wmain(int argc, wchar_t* argv[]) {
    // Usage: env_probe.exe <VARIABLE_NAME> <OUTPUT_FILE_PATH>
    if (argc != 3) {
        return 1;
    }

    const wchar_t* varName = argv[1];
    const wchar_t* outputPath = argv[2];

    SetLastError(ERROR_SUCCESS);
    DWORD len = GetEnvironmentVariableW(varName, nullptr, 0);
    DWORD err = GetLastError();
    std::wstring value;
    bool isSet = false;

    if (len > 0) {
        value.resize(len);
        DWORD readLen = GetEnvironmentVariableW(varName, value.data(), len);
        if (readLen > 0) {
            value.resize(readLen);
            isSet = true;
        }
    } else if (err == ERROR_SUCCESS) {
        isSet = true;
        value = L"";
    } else {
        // Check raw environment strings in case OS doesn't expose empty vars via GetEnvironmentVariableW
        LPWCH rawEnv = GetEnvironmentStringsW();
        if (rawEnv) {
            LPWCH p = rawEnv;
            std::wstring targetPrefix = std::wstring(varName) + L"=";
            while (*p != L'\0') {
                std::wstring entry(p);
                if (entry.rfind(targetPrefix, 0) == 0) {
                    isSet = true;
                    value = entry.substr(targetPrefix.length());
                    break;
                }
                p += entry.length() + 1;
            }
            FreeEnvironmentStringsW(rawEnv);
        }
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, outputPath, L"wb") != 0 || !f) {
        return 2;
    }

    if (isSet) {
        if (!value.empty()) {
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                std::string utf8(utf8Len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), utf8.data(), utf8Len, nullptr, nullptr);
                fwrite(utf8.data(), 1, utf8.size(), f);
            }
        } else {
            const char emptyMsg[] = "__VARIABLE_EMPTY__";
            fwrite(emptyMsg, 1, sizeof(emptyMsg) - 1, f);
        }
    } else {
        const char notSetMsg[] = "__VARIABLE_NOT_SET__";
        fwrite(notSetMsg, 1, sizeof(notSetMsg) - 1, f);
    }

    fclose(f);
    return 0;
}
