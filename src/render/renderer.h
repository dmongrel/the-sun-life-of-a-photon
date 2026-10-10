// Draws the Sun and its planets into any number of windows: one per monitor, a preview pane, or a dev window.
#pragma once

#include <windows.h>

#include <memory>
#include <string>

namespace render {

class Renderer {
public:
    // Null when Vulkan is unavailable; the caller then paints black.
    static std::unique_ptr<Renderer> Create();
    virtual ~Renderer() = default;

    virtual bool Attach(HWND hwnd)  = 0;
    virtual void Detach(HWND hwnd)  = 0;
    virtual void Resized(HWND hwnd) = 0;

    // Renders every attached window at loop time `seconds`. Presentation is vsynced, so this
    // also paces the caller's loop.
    virtual void RenderFrame(double seconds) = 0;

    // The next frame of `hwnd` is written to `path` (format from the extension).
    virtual void RequestCapture(HWND hwnd, const std::wstring& path) = 0;
    virtual bool CapturePending() const                               = 0;

    virtual void WaitIdle() = 0;
};

}  // namespace render
