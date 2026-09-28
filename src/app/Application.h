#pragma once

#include "ui/MainWindow.h"
#include <windows.h>

namespace envlauncher {

class Application {
public:
    Application();
    ~Application();

    // Runs application lifecycle and message pump
    int Run(HINSTANCE hInstance, int nCmdShow);

private:
    bool Initialize();
    void Shutdown();

    MainWindow mainWindow_;
    bool comInitialized_{false};
};

} // namespace envlauncher
