#pragma once
#include "nfsu/input.h"

namespace nfsu {
// Temporary race diagnostic chord. Process once per displayed frame, not
// once per simulation catch-up tick. Rear view on R3 alone is unchanged.
struct RaceDebugInput {
    bool consuming_sticks = false;
    bool update(bool connected, bool left, bool right) noexcept {
        if (!connected || (!left && !right)) {
            consuming_sticks = false;
            return false;
        }
        if (left && right && !consuming_sticks) {
            consuming_sticks = true;
            return true;
        }
        return false;
    }
    void filter(GamepadFrame& frame) const noexcept {
        if (consuming_sticks)
            frame.pads[0].buttons &= ~(gamepad_button_bit(GamepadButton::stick_left) |
                                      gamepad_button_bit(GamepadButton::stick_right));
    }
};
} // namespace nfsu
