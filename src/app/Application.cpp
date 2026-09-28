#include "app/Application.h"
#include "platform/DpiHelper.h"

namespace envlauncher {

Application::Application() = default;

Application::~Application() {
    Shutdown();
}

bool Application::Initialize() {
    DpiHelper::InitializeDpiAwareness();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        comInitialized_ = true;
    }

    return true;
}

void Application::Shutdown() {
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
}

int Application::Run(HINSTANCE hInstance, int nCmdShow) {
    if (!Initialize()) {
        return 1;
    }

    if (!mainWindow_.Create(hInstance, nCmdShow)) {
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}

} // namespace envlauncher
