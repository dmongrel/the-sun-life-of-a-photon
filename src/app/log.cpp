#include "app/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace app {
namespace {

FILE*         g_file = nullptr;
LARGE_INTEGER g_start{};
LARGE_INTEGER g_frequency{};

}  // namespace

void LogOpen() {
    wchar_t     value[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"SUN_PHOTON_LOG", value, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;

    std::wstring path;
    if (std::wstring(value) == L"1") {
        wchar_t     temp[MAX_PATH];
        const DWORD t = GetTempPathW(MAX_PATH, temp);
        if (t == 0 || t >= MAX_PATH) return;
        path = std::wstring(temp) + L"the-sun.log";
    } else {
        path = value;
    }

    g_file = _wfopen(path.c_str(), L"a");
    QueryPerformanceFrequency(&g_frequency);
    QueryPerformanceCounter(&g_start);
    if (g_file) Log("---- log opened, pid %lu", GetCurrentProcessId());
}

bool LogEnabled() { return g_file != nullptr; }

void Log(const char* format, ...) {
    if (!g_file) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double seconds = static_cast<double>(now.QuadPart - g_start.QuadPart) / g_frequency.QuadPart;

    std::fprintf(g_file, "[%9.3f] ", seconds);
    va_list args;
    va_start(args, format);
    std::vfprintf(g_file, format, args);
    va_end(args);
    std::fputc('\n', g_file);
    std::fflush(g_file);
}

}  // namespace app
