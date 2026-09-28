#include "ui/FileDialog.h"
#include <shobjidl.h>
#include <wrl/client.h>

namespace envlauncher {

bool FileDialog::ChooseExecutableFile(HWND parent, std::wstring& outPath) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> fileDialog;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&fileDialog)
    );
    if (FAILED(hr)) {
        return false;
    }

    COMDLG_FILTERSPEC fileTypes[] = {
        { L"Executable Files (*.exe)", L"*.exe" },
        { L"Batch & Command Scripts (*.bat;*.cmd)", L"*.bat;*.cmd" },
        { L"All Files (*.*)", L"*.*" }
    };
    fileDialog->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes);
    fileDialog->SetFileTypeIndex(1);
    fileDialog->SetTitle(L"Select Executable");

    DWORD options = 0;
    if (SUCCEEDED(fileDialog->GetOptions(&options))) {
        fileDialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    }

    hr = fileDialog->Show(parent);
    if (FAILED(hr)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IShellItem> item;
    hr = fileDialog->GetResult(&item);
    if (FAILED(hr)) {
        return false;
    }

    PWSTR filePath = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
    if (SUCCEEDED(hr) && filePath) {
        outPath = filePath;
        CoTaskMemFree(filePath);
        return true;
    }

    return false;
}

bool FileDialog::ChooseDirectory(HWND parent, std::wstring& outPath) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> folderDialog;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&folderDialog)
    );
    if (FAILED(hr)) {
        return false;
    }

    folderDialog->SetTitle(L"Select Working Directory");

    DWORD options = 0;
    if (SUCCEEDED(folderDialog->GetOptions(&options))) {
        folderDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    }

    hr = folderDialog->Show(parent);
    if (FAILED(hr)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IShellItem> item;
    hr = folderDialog->GetResult(&item);
    if (FAILED(hr)) {
        return false;
    }

    PWSTR folderPath = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &folderPath);
    if (SUCCEEDED(hr) && folderPath) {
        outPath = folderPath;
        CoTaskMemFree(folderPath);
        return true;
    }

    return false;
}

} // namespace envlauncher
