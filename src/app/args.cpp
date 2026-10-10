#include "app/args.h"

#include <cwchar>
#include <cwctype>

namespace app {
namespace {

// Windows passes the handle either glued on after a colon ("/p:1234") or as the next argument.
std::uint64_t HandleAfter(const wchar_t* rest, int argc, const wchar_t* const* argv) {
    const wchar_t* text = nullptr;
    if (*rest == L':') {
        text = rest + 1;
    } else if (*rest == L'\0' && argc > 2) {
        text = argv[2];
    }
    if (!text || !*text) return 0;

    wchar_t*                 end   = nullptr;
    const unsigned long long value = std::wcstoull(text, &end, 10);
    return (end && *end == L'\0') ? value : 0;
}

}  // namespace

Args ParseArgs(int argc, const wchar_t* const* argv) {
    Args args;
    if (argc < 2 || !argv || !argv[1]) return args;  // no argument: run the saver

    const wchar_t* arg = argv[1];
    if (*arg == L'/' || *arg == L'-') ++arg;

    const wchar_t letter = static_cast<wchar_t>(std::towlower(*arg));
    const wchar_t* rest  = *arg ? arg + 1 : arg;

    switch (letter) {
        case L's':
            args.mode = Mode::FullScreen;
            break;
        case L'w':
            args.mode = Mode::Windowed;
            break;
        case L'p':
            args.handle = HandleAfter(rest, argc, argv);
            args.mode   = args.handle ? Mode::Preview : Mode::Exit;
            break;
        case L'c':
            args.handle = HandleAfter(rest, argc, argv);
            args.mode   = Mode::Settings;
            break;
        default:
            args.mode = Mode::Exit;
            break;
    }
    return args;
}

}  // namespace app
