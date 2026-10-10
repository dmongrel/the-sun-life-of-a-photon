// The screen saver's windows and main loop, one entry point per mode.
#pragma once

#include <windows.h>

namespace app {

int RunFullScreen(HINSTANCE instance);           // one top-most window per monitor
int RunWindowed(HINSTANCE instance);             // a resizable development window
int RunPreview(HINSTANCE instance, HWND parent);  // inside the Screen Saver Settings pane
int RunSettings(HWND owner);                      // there are no settings; says so

}  // namespace app
