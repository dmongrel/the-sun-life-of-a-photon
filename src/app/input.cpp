#include "app/input.h"

#include <windows.h>

namespace app {

bool InputGate::Update(double seconds, const InputSample& sample) {
    if (seconds < kGraceSeconds) return false;

    if (!armed_) {
        // The first look after the grace period sets the baseline: nothing already true is input.
        held_  = sample.down;
        armed_ = true;
        if (sample.cursorValid) {
            hasCursor_ = true;
            originX_   = sample.cursorX;
            originY_   = sample.cursorY;
        }
        return false;
    }

    if (sample.cursorValid) {
        if (!hasCursor_) {
            // The cursor only just became readable, e.g. the saver got the input desktop.
            hasCursor_ = true;
            originX_   = sample.cursorX;
            originY_   = sample.cursorY;
        } else {
            const int dx = sample.cursorX - originX_;
            const int dy = sample.cursorY - originY_;
            if (dx * dx + dy * dy > kDeadZonePixels * kDeadZonePixels) return true;
        }
    }

    for (int vk = 0; vk < 256; ++vk) {
        if (!sample.down[vk]) {
            held_[vk] = false;  // released: its next press counts
        } else if (!held_[vk]) {
            return true;
        }
    }
    return false;
}

InputSample ReadInput() {
    InputSample s;
    POINT       p{};
    if (GetCursorPos(&p)) {
        s.cursorValid = true;
        s.cursorX     = p.x;
        s.cursorY     = p.y;
    }
    for (int vk = 1; vk < 256; ++vk) {
        // Bit 15: down now. Bit 0: pressed since the last call, which catches a tap between frames.
        const SHORT state = GetAsyncKeyState(vk);
        s.down[vk]        = (state & 0x8001) != 0;
    }
    return s;
}

}  // namespace app
