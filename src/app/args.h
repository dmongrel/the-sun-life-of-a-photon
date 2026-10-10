// Screen-saver command line: which mode Windows asked for, and the window handle that came with it.
#pragma once

#include <cstdint>

namespace app {

enum class Mode {
    FullScreen,  // no argument, or /s
    Preview,     // /p <hwnd> or /p:<hwnd>
    Settings,    // /c or /c:<hwnd>
    Windowed,    // /w, for development
    Exit,        // anything else, or a preview without a usable handle
};

struct Args {
    Mode          mode   = Mode::FullScreen;
    std::uint64_t handle = 0;  // the parent (preview) or owner (settings) window; 0 when absent
};

// argv[0] is the program. No Win32 calls: whether the handle names a live window is checked later.
Args ParseArgs(int argc, const wchar_t* const* argv);

}  // namespace app
