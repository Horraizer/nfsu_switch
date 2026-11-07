#include "nfsu/input_camera.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

// Controller input on the inspection camera: include/nfsu/input_camera.h
// states the contract and docs/INPUT.md the layout and the reasons. As in
// src/input.cpp, contraction is disabled for this file by CMakeLists.txt,
// so a recorded session produces the same Inspector calls, to the bit, on
// every build.

namespace nfsu {

namespace {

InputBinding bind(GamepadButton button, CameraAction action, bool invert = false) {
    InputBinding binding;
    binding.control = gamepad_control(button);
    binding.action = camera_action_id(action);
    binding.invert = invert;
    return binding;
}

InputBinding bind(GamepadButton button, InspectorCommand command) {
    InputBinding binding;
    binding.control = gamepad_control(button);
    binding.action = camera_command_action(command);
    return binding;
}

InputBinding bind(GamepadAxis axis, CameraAction action) {
    InputBinding binding;
    binding.control = gamepad_control(axis);
    binding.action = camera_action_id(action);
    return binding;
}

bool rate_ok(double value, double maximum) noexcept {
    return std::isfinite(value) && value > 0.0 && value <= maximum;
}

} // namespace

std::uint16_t camera_action_id(CameraAction action) noexcept {
    return static_cast<std::uint16_t>(action);
}

std::uint16_t camera_command_action(InspectorCommand command) noexcept {
    return static_cast<std::uint16_t>(kFirstCameraCommand +
                                      static_cast<std::uint16_t>(command));
}

std::vector<ActionKind> camera_action_kinds() {
    std::vector<ActionKind> kinds(kCameraActionCount, ActionKind::digital);
    for (const CameraAction action : {CameraAction::orbit_x, CameraAction::orbit_y,
                                      CameraAction::pan_x, CameraAction::pan_y,
                                      CameraAction::zoom}) {
        kinds[camera_action_id(action)] = ActionKind::analog;
    }
    return kinds;
}

std::vector<InputBinding> default_camera_bindings() {
    return {
        bind(GamepadAxis::right_x, CameraAction::orbit_x),
        bind(GamepadAxis::right_y, CameraAction::orbit_y),
        bind(GamepadAxis::left_x, CameraAction::pan_x),
        bind(GamepadAxis::left_y, CameraAction::pan_y),
        bind(GamepadButton::trigger_right, CameraAction::zoom),
        bind(GamepadButton::trigger_left, CameraAction::zoom, true),
        bind(GamepadButton::start, CameraAction::exit_session),
        bind(GamepadButton::select, InspectorCommand::reset_camera),
        bind(GamepadButton::dpad_up, InspectorCommand::view_front),
        bind(GamepadButton::dpad_right, InspectorCommand::view_right),
        bind(GamepadButton::dpad_down, InspectorCommand::view_rear),
        bind(GamepadButton::dpad_left, InspectorCommand::view_left),
        bind(GamepadButton::stick_right, InspectorCommand::view_top),
        bind(GamepadButton::stick_left, InspectorCommand::frame_scene),
        bind(GamepadButton::face_east, InspectorCommand::frame_selection),
        bind(GamepadButton::face_south, InspectorCommand::clear_selection),
        bind(GamepadButton::shoulder_right, InspectorCommand::select_next_draw),
        bind(GamepadButton::shoulder_left, InspectorCommand::select_previous_draw),
        bind(GamepadButton::face_north, InspectorCommand::cycle_view),
        bind(GamepadButton::face_west, InspectorCommand::cycle_overlay),
    };
}

bool validate_camera_rates(const CameraRates& rates, std::string& error) {
    if (!rate_ok(rates.orbit_deg_per_second, kMaxOrbitDegPerSecond)) {
        error = "the orbit rate must be above 0 and at most " +
                std::to_string(kMaxOrbitDegPerSecond) + " degrees per second";
        return false;
    }
    if (!rate_ok(rates.pan_heights_per_second, kMaxPanHeightsPerSecond)) {
        error = "the pan rate must be above 0 and at most " +
                std::to_string(kMaxPanHeightsPerSecond) + " viewport heights per second";
        return false;
    }
    if (!rate_ok(rates.zoom_steps_per_second, kMaxZoomStepsPerSecond)) {
        error = "the zoom rate must be above 0 and at most " +
                std::to_string(kMaxZoomStepsPerSecond) + " steps per second";
        return false;
    }
    return true;
}

GamepadCamera::GamepadCamera() : bindings_(default_camera_bindings()) {
    std::string ignored;
    // The defaults validate by construction; tests/input_tests.cpp checks
    // that they do.
    static_cast<void>(configure(ignored));
}

bool GamepadCamera::configure(std::string& error) {
    if (!mapper_.configure(camera_action_kinds(), bindings_, zone_, error)) {
        return false;
    }
    zoom_residual_ = 0.0;
    return true;
}

bool GamepadCamera::set_bindings(const std::vector<InputBinding>& bindings, std::string& error) {
    if (!validate_input_bindings(camera_action_kinds(), bindings, error)) {
        return false;
    }
    bindings_ = bindings;
    return configure(error);
}

bool GamepadCamera::set_dead_zone(const StickDeadZone& zone, std::string& error) {
    if (!validate_stick_dead_zone(zone, error)) {
        return false;
    }
    zone_ = zone;
    return configure(error);
}

bool GamepadCamera::set_rates(const CameraRates& rates, std::string& error) {
    if (!validate_camera_rates(rates, error)) {
        return false;
    }
    rates_ = rates;
    mapper_.reset();
    zoom_residual_ = 0.0;
    return true;
}

void GamepadCamera::reset() noexcept {
    mapper_.reset();
    zoom_residual_ = 0.0;
}

CameraStep GamepadCamera::step(const GamepadFrame& frame, Inspector& inspector) {
    CameraStep out;
    mapper_.update(frame);
    const std::vector<ActionState>& actions = mapper_.actions();
    if (actions.size() != kCameraActionCount) {
        return out;
    }
    const auto value = [&actions](CameraAction action) {
        return actions[camera_action_id(action)].value;
    };
    const double orbit_x = value(CameraAction::orbit_x);
    const double orbit_y = value(CameraAction::orbit_y);
    const double pan_x = value(CameraAction::pan_x);
    const double pan_y = value(CameraAction::pan_y);
    const double zoom = value(CameraAction::zoom);
    out.moving = orbit_x != 0.0 || orbit_y != 0.0 || pan_x != 0.0 || pan_y != 0.0 ||
                 zoom != 0.0;
    const double seconds = static_cast<double>(mapper_.interval_us()) / 1000000.0;

    // Orbit. The inspector turns pointer pixels into degrees with its own
    // sensitivity, so the degrees are turned back into pixels first. Its
    // signs are already the stick's: a positive dx lowers yaw, which swings
    // the eye along the camera's right vector (render.h builds right as
    // forward x up, and yaw raises the eye towards +Z), and a positive dy
    // raises pitch, which lifts the eye. tests/input_tests.cpp measures both
    // against make_camera_frame rather than trusting this paragraph.
    const double turn = rates_.orbit_deg_per_second * seconds;
    const double degrees_per_pixel = inspector.sensitivity().orbit_deg_per_pixel;
    out.orbit_dx = (orbit_x * turn) / degrees_per_pixel;
    out.orbit_dy = (orbit_y * turn) / degrees_per_pixel;
    if (out.orbit_dx != 0.0 || out.orbit_dy != 0.0) {
        inspector.orbit(out.orbit_dx, out.orbit_dy);
    }

    // Pan, in pixels of the viewport the inspector will render at. A pointer
    // moving right carries the target left, the grab convention; a stick
    // pushed right carries it right.
    const double slide = rates_.pan_heights_per_second * seconds *
                         static_cast<double>(inspector.options().height);
    out.pan_dx = -(pan_x * slide);
    out.pan_dy = pan_y * slide;
    if (out.pan_dx != 0.0 || out.pan_dy != 0.0) {
        inspector.pan(out.pan_dx, out.pan_dy);
    }

    // Zoom, in whole steps. The fraction carried over is below one step in
    // magnitude after every frame, and one frame adds at most
    // kMaxZoomStepsPerSecond * kMaxFrameIntervalUs of a second, so the
    // conversion to int cannot overflow.
    zoom_residual_ += zoom * rates_.zoom_steps_per_second * seconds;
    out.zoom_steps = static_cast<int>(zoom_residual_);
    zoom_residual_ -= static_cast<double>(out.zoom_steps);
    if (out.zoom_steps != 0) {
        inspector.zoom(out.zoom_steps);
    }

    for (std::size_t i = 0; i < kInspectorCommandCount; ++i) {
        if (!actions[kFirstCameraCommand + i].pressed) {
            continue;
        }
        const InspectorCommand command = static_cast<InspectorCommand>(i);
        std::string ignored;
        static_cast<void>(inspector.command(command, ignored));
        out.commands.push_back(command);
    }
    out.exit_requested = actions[camera_action_id(CameraAction::exit_session)].pressed;
    return out;
}

} // namespace nfsu
