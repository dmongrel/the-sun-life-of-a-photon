#include "app/host.h"

#include "app/input.h"
#include "app/log.h"
#include "render/renderer.h"
#include "sim/schedule.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace app {
namespace {

const wchar_t* kWindowClass = L"TheSunLifeOfAPhoton";

enum class Kind { FullScreen, Windowed, Preview };

struct Host {
    Kind                              kind = Kind::Windowed;
    std::unique_ptr<render::Renderer> renderer;
    std::vector<HWND>                 windows;
    InputGate                         gate;
    double                            seconds = 0.0;  // wall clock, since the run started
    bool                              running = true;
};

Host* g_host = nullptr;

void Quit() {
    if (g_host) g_host->running = false;
}

bool HasEnv(const wchar_t* name) { return GetEnvironmentVariableW(name, nullptr, 0) > 0; }

double EnvNumber(const wchar_t* name, double fallback) {
    wchar_t     value[64];
    const DWORD n = GetEnvironmentVariableW(name, value, 64);
    return (n > 0 && n < 64) ? std::wcstod(value, nullptr) : fallback;
}

std::wstring EnvString(const wchar_t* name) {
    wchar_t     value[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(name, value, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(value) : std::wstring();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Host* host = g_host;
    const bool saver = host && host->kind == Kind::FullScreen;

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;  // the renderer, or WM_PAINT below, owns every pixel
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC         dc = BeginPaint(hwnd, &ps);
            if (!host || !host->renderer) FillRect(dc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            if (host && host->renderer) host->renderer->Resized(hwnd);
            return 0;
        case WM_SETCURSOR:
            if (saver) {
                SetCursor(nullptr);
                return TRUE;
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
            // Polling (InputGate) sees these too; the messages just react a frame sooner.
            if (saver) {
                if (host->seconds >= InputGate::kGraceSeconds) Quit();
                return 0;
            }
            if (msg == WM_KEYDOWN && wp == VK_ESCAPE) Quit();
            break;
        case WM_SYSCOMMAND:
            // A running saver must not start another one, or let the monitor power-save menu in.
            if (saver && ((wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_MONITORPOWER)) return 0;
            break;
        case WM_CLOSE:
            Quit();
            return 0;
        case WM_TIMER:
            // The preview's parent belongs to another process and can vanish at any moment.
            if (host && host->kind == Kind::Preview && !IsWindow(GetParent(hwnd))) Quit();
            return 0;
        case WM_DESTROY:
            if (host) {
                if (host->renderer) host->renderer->Detach(hwnd);
                host->windows.erase(std::remove(host->windows.begin(), host->windows.end(), hwnd), host->windows.end());
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = instance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClass;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

BOOL CALLBACK CollectMonitor(HMONITOR, HDC, LPRECT rect, LPARAM out) {
    reinterpret_cast<std::vector<RECT>*>(out)->push_back(*rect);
    return TRUE;
}

// The main loop shared by every mode.
//
// Loop time is wall time times the speed factor, plus a starting offset:
//   SUN_PHOTON_SPEED       the speed factor (default 1)
//   SUN_PHOTON_TIME        loop time to start at, so a run can begin anywhere in the loop
//   SUN_PHOTON_CAPTURE     /w only: write the frame shown at loop time SUN_PHOTON_CAPTURE_AT
//                          (default 5) to this file, then exit. Without SUN_PHOTON_TIME the run
//                          starts one second before that moment.
int Run(Host& host) {
    const double speed     = std::clamp(EnvNumber(L"SUN_PHOTON_SPEED", 1.0), 0.05, 20.0);
    const std::wstring capture = host.kind == Kind::Windowed ? EnvString(L"SUN_PHOTON_CAPTURE") : std::wstring();
    const double captureAt = EnvNumber(L"SUN_PHOTON_CAPTURE_AT", 5.0);
    const double offset    = HasEnv(L"SUN_PHOTON_TIME") ? EnvNumber(L"SUN_PHOTON_TIME", 0.0)
                             : capture.empty()         ? 0.0
                                                       : std::max(captureAt - speed, 0.0);
    Log("speed %.2f, starting at loop time %.1f of %.1f", speed, offset, double(sim::LoopLength()));
    bool requested = false;

    LARGE_INTEGER frequency{}, start{}, now{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    double        reportAt     = 5.0;
    std::uint64_t frames       = 0;
    std::uint64_t reportFrames = 0;
    double        reportStart  = 0.0;

    MSG msg{};
    while (host.running && !host.windows.empty()) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) host.running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!host.running || host.windows.empty()) break;

        QueryPerformanceCounter(&now);
        host.seconds = double(now.QuadPart - start.QuadPart) / double(frequency.QuadPart);
        const double loopTime = offset + host.seconds * speed;

        if (host.kind == Kind::FullScreen && host.gate.Update(host.seconds, ReadInput())) {
            Log("input ended the run at %.2f s", host.seconds);
            break;
        }

        if (!capture.empty()) {
            if (!requested && loopTime >= captureAt && host.renderer) {
                host.renderer->RequestCapture(host.windows.front(), capture);
                requested = true;
            } else if (requested && (!host.renderer || !host.renderer->CapturePending())) {
                break;
            } else if (!host.renderer && loopTime >= captureAt) {
                Log("capture impossible without Vulkan");
                break;
            }
        }

        if (host.renderer) {
            host.renderer->RenderFrame(loopTime);  // vsync paces the loop
        } else {
            Sleep(16);
        }
        ++frames;

        if (host.seconds >= reportAt) {
            const double span = host.seconds - reportStart;
            Log("%.1f fps over the last %.1f s (loop time %.0f)", double(frames - reportFrames) / span, span,
                std::fmod(loopTime, double(sim::LoopLength())));
            reportFrames = frames;
            reportStart  = host.seconds;
            reportAt     = host.seconds + 5.0;
        }
    }
    Log("ran %.1f s, %llu frames, %.1f fps", host.seconds, static_cast<unsigned long long>(frames),
        host.seconds > 0 ? double(frames) / host.seconds : 0.0);

    if (host.renderer) host.renderer->WaitIdle();
    for (HWND hwnd : std::vector<HWND>(host.windows)) DestroyWindow(hwnd);
    host.renderer.reset();
    return 0;
}

void AttachAll(Host& host) {
    host.renderer = render::Renderer::Create();
    Log("renderer: %s", host.renderer ? "vulkan" : "none, painting black");
    if (!host.renderer) return;
    for (HWND hwnd : host.windows) {
        if (!host.renderer->Attach(hwnd)) Log("window %p could not be attached", static_cast<void*>(hwnd));
    }
}

}  // namespace

int RunFullScreen(HINSTANCE instance) {
    if (!RegisterWindowClass(instance)) return 0;
    Host host;
    host.kind = Kind::FullScreen;
    g_host    = &host;

    std::vector<RECT> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
    for (const RECT& r : monitors) {
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kWindowClass, L"The Sun: The Life of a Photon",
                                    WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                                    instance, nullptr);
        if (!hwnd) continue;
        ShowWindow(hwnd, SW_SHOW);
        host.windows.push_back(hwnd);
    }
    if (!host.windows.empty()) {
        SetForegroundWindow(host.windows.front());
        ShowCursor(FALSE);
        AttachAll(host);
        Run(host);
        ShowCursor(TRUE);
    }
    g_host = nullptr;
    return 0;
}

int RunWindowed(HINSTANCE instance) {
    if (!RegisterWindowClass(instance)) return 0;
    Host host;
    host.kind = Kind::Windowed;
    g_host    = &host;

    RECT r{0, 0, 1600, 900};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, kWindowClass, L"The Sun: The Life of a Photon", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, nullptr);
    if (hwnd) {
        // The first ShowWindow obeys the launcher's STARTUPINFO, which a script may set to
        // minimised or hidden; a capture run needs the window at its real size, so show it twice.
        ShowWindow(hwnd, SW_SHOW);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        host.windows.push_back(hwnd);
        AttachAll(host);
        Run(host);
    }
    g_host = nullptr;
    return 0;
}

int RunPreview(HINSTANCE instance, HWND parent) {
    if (!IsWindow(parent) || !RegisterWindowClass(instance)) return 0;
    Host host;
    host.kind = Kind::Preview;
    g_host    = &host;

    RECT rc{};
    GetClientRect(parent, &rc);
    HWND hwnd = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, rc.right, rc.bottom, parent,
                                nullptr, instance, nullptr);
    if (hwnd) {
        SetTimer(hwnd, 1, 250, nullptr);
        host.windows.push_back(hwnd);
        AttachAll(host);
        Run(host);
    }
    g_host = nullptr;
    return 0;
}

int RunSettings(HWND owner) {
    MessageBoxW(owner,
                L"The Sun: The Life of a Photon has no settings.\n\n"
                L"The light reaching you now left the Sun's surface eight minutes ago, "
                L"and set out from its core about 170,000 years before that.",
                L"The Sun: The Life of a Photon", MB_OK | MB_ICONINFORMATION);
    return 0;
}

}  // namespace app
