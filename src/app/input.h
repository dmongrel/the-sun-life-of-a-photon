// When does input end a full-screen run?
//
// The decision is kept apart from reading the hardware so it can be tested: InputGate::Update is
// pure, and ReadInput is the only part that talks to Win32. Getting this wrong is the worst bug a
// screen saver can have, because then nobody can get their desktop back.
#pragma once

#include <array>

namespace app {

struct InputSample {
    bool cursorValid = false;  // GetCursorPos fails off the input desktop; that is not movement
    int  cursorX     = 0;
    int  cursorY     = 0;

    // Indexed by virtual-key code, mouse buttons included: down now, or pressed since last read.
    std::array<bool, 256> down{};
};

class InputGate {
public:
    static constexpr double kGraceSeconds   = 0.5;  // the key that started the saver may still be held
    static constexpr int    kDeadZonePixels = 8;

    // True once the run should end. `seconds` is the time since the saver started.
    bool Update(double seconds, const InputSample& sample);

private:
    bool                  armed_     = false;
    bool                  hasCursor_ = false;
    int                   originX_   = 0;
    int                   originY_   = 0;
    std::array<bool, 256> held_{};  // keys already down when the grace period ended
};

InputSample ReadInput();

}  // namespace app
