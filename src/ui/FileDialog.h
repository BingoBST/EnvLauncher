#pragma once

#include <windows.h>
#include <string>

namespace envlauncher {

class FileDialog {
public:
    // Opens a native Windows file open dialog to select an executable file.
    // Filter: "Executable Files (*.exe)", "*.exe", "All Files (*.*)", "*.*"
    // Returns true if a file was selected, false if canceled.
    static bool ChooseExecutableFile(HWND parent, std::wstring& outPath);

    // Opens a native Windows folder open dialog to select a folder directory.
    // Returns true if a folder was selected, false if canceled.
    static bool ChooseDirectory(HWND parent, std::wstring& outPath);
};

} // namespace envlauncher
