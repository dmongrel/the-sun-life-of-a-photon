// the-sun entry point: read the screen-saver arguments and run the mode they name.

#include <windows.h>
#include <shellapi.h>

#include "app/args.h"
#include "app/host.h"
#include "app/log.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    app::LogOpen();

    int       argc = 0;
    LPWSTR*   argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const app::Args args = app::ParseArgs(argc, argv);
    if (argv) LocalFree(argv);
    app::Log("mode %d, handle %llu", static_cast<int>(args.mode), static_cast<unsigned long long>(args.handle));

    const HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(args.handle));
    switch (args.mode) {
        case app::Mode::FullScreen:
            return app::RunFullScreen(instance);
        case app::Mode::Windowed:
            return app::RunWindowed(instance);
        case app::Mode::Preview:
            return app::RunPreview(instance, window);
        case app::Mode::Settings:
            return app::RunSettings(args.handle && IsWindow(window) ? window : nullptr);
        case app::Mode::Exit:
        default:
            return 0;
    }
}
