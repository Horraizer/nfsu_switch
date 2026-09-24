#include "nfsu/input.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The controller model: dead zones, the remapping table and the rules that
// turn frames into actions. include/nfsu/input.h states each rule and
// docs/INPUT.md says why it is that rule.
//
// Floating point here is plain IEEE arithmetic with contraction disabled by
// CMakeLists.txt, and the one library call is std::sqrt, which IEEE 754
// requires to be correctly rounded. So the actions one frame produces do
// not depend on the build or on whether the machine fuses a multiply-add.

namespace nfsu {

namespace {

// A live value read as the recorded form would read it: kAxisMin is
// -kAxisMax, so that full deflection is one length in every direction, and
// anything outside the range is clamped into it.
std::int64_t clamp_axis(std::int32_t value) noexcept {
    if (value < -kAxisMax) {
        return -std::int64_t{kAxisMax};
    }
    if (value > kAxisMax) {
        return kAxisMax;
    }
    return value;
}

double clamp_unit(double value) noexcept {
    if (value < -1.0) {
        return -1.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

} // namespace

const char* gamepad_button_name(GamepadButton button) noexcept {
    switch (button) {
    case GamepadButton::face_south:
        return "face_south";
    case GamepadButton::face_east:
        return "face_east";
    case GamepadButton::face_west:
        return "face_west";
    case GamepadButton::face_north:
        return "face_north";
    case GamepadButton::shoulder_left:
        return "shoulder_left";
    case GamepadButton::shoulder_right:
        return "shoulder_right";
    case GamepadButton::trigger_left:
        return "trigger_left";
    case GamepadButton::trigger_right:
        return "trigger_right";
    case GamepadButton::start:
        return "start";
    case GamepadButton::select:
        return "select";
    case GamepadButton::stick_left:
        return "stick_left";
    case GamepadButton::stick_right:
        return "stick_right";
    case GamepadButton::dpad_up:
        return "dpad_up";
    case GamepadButton::dpad_down:
        return "dpad_down";
    case GamepadButton::dpad_left:
        return "dpad_left";
    case GamepadButton::dpad_right:
        return "dpad_right";
    }
    return "unknown";
}

const char* gamepad_axis_name(GamepadAxis axis) noexcept {
    switch (axis) {
    case GamepadAxis::left_x:
        return "left_x";
    case GamepadAxis::left_y:
        return "left_y";
    case GamepadAxis::right_x:
        return "right_x";
    case GamepadAxis::right_y:
        return "right_y";
    }
    return "unknown";
}

bool validate_stick_dead_zone(const StickDeadZone& zone, std::string& error) {
    if (zone.inner < 0 || zone.outer > kAxisMax || zone.inner >= zone.outer) {
        error = "a stick dead zone needs 0 <= inner < outer <= " + std::to_string(kAxisMax);
        return false;
    }
    return true;
}

StickVector apply_stick_dead_zone(
    const StickDeadZone& zone, std::int32_t raw_x, std::int32_t raw_y) noexcept {
    const std::int64_t x = clamp_axis(raw_x);
    const std::int64_t y = clamp_axis(raw_y);
    const std::int64_t inner = zone.inner;
    const std::int64_t outer = zone.outer;
    // Squared lengths: at most 2 * 32767^2, far inside 64 bits, and exact,
    // which is what makes each edge the same decision everywhere.
    const std::int64_t squared = x * x + y * y;
    StickVector out;
    if (squared <= inner * inner) {
        return out;
    }
    const double length = std::sqrt(static_cast<double>(squared));
    double scale = 1.0;
    if (squared < outer * outer) {
        // In inner..outer, so the fraction is in 0..1. The square root of an
        // integer strictly between inner^2 and outer^2 is strictly between
        // inner and outer, since both are integers and the root is
        // correctly rounded, so the fraction is never 0 here and never 1.
        scale = (length - static_cast<double>(inner)) / static_cast<double>(outer - inner);
    }
    // Direction times rescaled length. Clamped only as a guarantee of the
    // documented bound; with |x| <= length it changes nothing.
    out.x = clamp_unit(static_cast<double>(x) / length * scale);
    out.y = clamp_unit(static_cast<double>(y) / length * scale);
    return out;
}

GamepadControl gamepad_control(GamepadButton button) noexcept {
    GamepadControl control;
    control.kind = GamepadControl::Kind::button;
    control.index = static_cast<std::uint8_t>(button);
    return control;
}

GamepadControl gamepad_control(GamepadAxis axis) noexcept {
    GamepadControl control;
    control.kind = GamepadControl::Kind::axis;
    control.index = static_cast<std::uint8_t>(axis);
    return control;
}

bool validate_input_bindings(
    const std::vector<ActionKind>& kinds,
    const std::vector<InputBinding>& bindings,
    std::string& error) {
    if (kinds.empty() || kinds.size() > kMaxInputActions) {
        error = "an action set needs 1 to " + std::to_string(kMaxInputActions) + " actions";
        return false;
    }
    if (bindings.size() > kMaxInputBindings) {
        error = "a remapping table holds at most " + std::to_string(kMaxInputBindings) +
                " bindings";
        return false;
    }
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const InputBinding& binding = bindings[i];
        const std::string where = "binding " + std::to_string(i) + ": ";
        const bool button = binding.control.kind == GamepadControl::Kind::button;
        const bool axis = binding.control.kind == GamepadControl::Kind::axis;
        if ((!button && !axis) || (button && binding.control.index >= kGamepadButtonCount) ||
            (axis && binding.control.index >= kGamepadAxisCount)) {
            error = where + "names no button and no axis";
            return false;
        }
        if (binding.action >= kinds.size()) {
            error = where + "names action " + std::to_string(binding.action) +
                    " and the set has " + std::to_string(kinds.size());
            return false;
        }
        const ActionKind kind = kinds[binding.action];
        if (kind != ActionKind::analog && kind != ActionKind::digital) {
            error = where + "drives an action of no known kind";
            return false;
        }
        const std::string name =
            button ? gamepad_button_name(static_cast<GamepadButton>(binding.control.index))
                   : gamepad_axis_name(static_cast<GamepadAxis>(binding.control.index));
        if (axis && kind == ActionKind::digital) {
            error = where + name + " is a stick axis and cannot drive a digital action";
            return false;
        }
        if (kind == ActionKind::digital && binding.invert) {
            error = where + "a digital action cannot be inverted";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (bindings[j].control.kind == binding.control.kind &&
                bindings[j].control.index == binding.control.index) {
                error = where + name + " is already bound by binding " + std::to_string(j);
                return false;
            }
        }
    }
    return true;
}

bool InputMapper::configure(
    const std::vector<ActionKind>& kinds,
    const std::vector<InputBinding>& bindings,
    const StickDeadZone& zone,
    std::string& error) {
    if (!validate_input_bindings(kinds, bindings, error) ||
        !validate_stick_dead_zone(zone, error)) {
        return false;
    }
    kinds_ = kinds;
    bindings_ = bindings;
    zone_ = zone;
    actions_.assign(kinds_.size(), ActionState{});
    sums_.assign(kinds_.size(), 0.0);
    reset();
    return true;
}

void InputMapper::reset() noexcept {
    have_previous_ = false;
    previous_time_us_ = 0;
    interval_us_ = 0;
    for (std::size_t slot = 0; slot < kMaxGamepads; ++slot) {
        connected_[slot] = false;
        latched_[slot] = 0;
    }
    for (ActionState& action : actions_) {
        action = ActionState{};
    }
}

void InputMapper::update(const GamepadFrame& frame) {
    if (kinds_.empty()) {
        return;
    }
    if (!have_previous_ || frame.time_us < previous_time_us_) {
        interval_us_ = 0;
    } else {
        const std::uint64_t elapsed = frame.time_us - previous_time_us_;
        interval_us_ = elapsed > kMaxFrameIntervalUs ? kMaxFrameIntervalUs : elapsed;
    }
    have_previous_ = true;
    previous_time_us_ = frame.time_us;

    std::vector<bool> was_held(actions_.size(), false);
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        was_held[i] = actions_[i].held;
        actions_[i] = ActionState{};
        sums_[i] = 0.0;
    }
    for (std::size_t slot = 0; slot < kMaxGamepads; ++slot) {
        const GamepadState& pad = frame.pads[slot];
        if (!pad.connected) {
            // Whatever the slot was doing ends on this frame: nothing it
            // held is carried into the sums below, and it is not a release
            // that fires anything, because a digital action fires on a
            // press.
            connected_[slot] = false;
            latched_[slot] = 0;
            continue;
        }
        const std::uint32_t buttons = pad.buttons & kGamepadButtonMask;
        if (!connected_[slot]) {
            connected_[slot] = true;
            latched_[slot] = buttons;
        }
        // A latched button is freed by being seen up once.
        latched_[slot] &= buttons;
        const std::uint32_t live = buttons & ~latched_[slot];
        const StickVector left =
            apply_stick_dead_zone(zone_, pad.axes[static_cast<std::size_t>(GamepadAxis::left_x)],
                                  pad.axes[static_cast<std::size_t>(GamepadAxis::left_y)]);
        const StickVector right =
            apply_stick_dead_zone(zone_, pad.axes[static_cast<std::size_t>(GamepadAxis::right_x)],
                                  pad.axes[static_cast<std::size_t>(GamepadAxis::right_y)]);
        for (const InputBinding& binding : bindings_) {
            const std::size_t action = binding.action;
            if (binding.control.kind == GamepadControl::Kind::button) {
                const GamepadButton button = static_cast<GamepadButton>(binding.control.index);
                if ((live & gamepad_button_bit(button)) == 0U) {
                    continue;
                }
                if (kinds_[action] == ActionKind::digital) {
                    actions_[action].held = true;
                } else {
                    sums_[action] += binding.invert ? -1.0 : 1.0;
                }
                continue;
            }
            double value = 0.0;
            switch (static_cast<GamepadAxis>(binding.control.index)) {
            case GamepadAxis::left_x:
                value = left.x;
                break;
            case GamepadAxis::left_y:
                value = left.y;
                break;
            case GamepadAxis::right_x:
                value = right.x;
                break;
            case GamepadAxis::right_y:
                value = right.y;
                break;
            }
            sums_[action] += binding.invert ? -value : value;
        }
    }
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        ActionState& action = actions_[i];
        if (kinds_[i] == ActionKind::digital) {
            action.pressed = action.held && !was_held[i];
            action.value = action.held ? 1.0 : 0.0;
        } else {
            action.value = clamp_unit(sums_[i]);
        }
    }
}

} // namespace nfsu
