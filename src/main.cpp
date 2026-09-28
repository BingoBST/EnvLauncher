#include "app/Application.h"

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    PWSTR pCmdLine,
    int nCmdShow
) {
    (void)hPrevInstance;
    (void)pCmdLine;

    envlauncher::Application app;
    return app.Run(hInstance, nCmdShow);
}
