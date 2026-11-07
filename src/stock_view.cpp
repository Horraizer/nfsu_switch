#include "nfsu/stock_view.h"

#include <cstddef>
#include <exception>
#include <new>
#include <string>
#include <vector>

// The stock car on the console, orbited with a controller: see
// include/nfsu/stock_view.h. This file does no floating-point arithmetic of
// its own. The camera arithmetic is nfsu_inspect's and nfsu_input_camera's,
// and the render nfsu_render's, each compiled with its own flags.

namespace nfsu {

namespace {

InputBinding bind(GamepadAxis axis, CameraAction action) {
    InputBinding binding;
    binding.control = gamepad_control(axis);
    binding.action = camera_action_id(action);
    return binding;
}

InputBinding bind(GamepadButton button, CameraAction action) {
    InputBinding binding;
    binding.control = gamepad_control(button);
    binding.action = camera_action_id(action);
    return binding;
}

InputBinding bind(GamepadButton button, InspectorCommand command) {
    InputBinding binding;
    binding.control = gamepad_control(button);
    binding.action = camera_command_action(command);
    return binding;
}

// docs/INPUT.md, "The Joy-Con and Pro Controller mapping a libnx backend
// would implement", row for row.
struct NpadMapping {
    std::uint64_t npad;
    GamepadButton button;
};

constexpr NpadMapping kNpadMapping[] = {
    {kNpadButtonA, GamepadButton::face_east},
    {kNpadButtonB, GamepadButton::face_south},
    {kNpadButtonX, GamepadButton::face_north},
    {kNpadButtonY, GamepadButton::face_west},
    {kNpadButtonStickL, GamepadButton::stick_left},
    {kNpadButtonStickR, GamepadButton::stick_right},
    {kNpadButtonL, GamepadButton::shoulder_left},
    {kNpadButtonR, GamepadButton::shoulder_right},
    {kNpadButtonZL, GamepadButton::trigger_left},
    {kNpadButtonZR, GamepadButton::trigger_right},
    {kNpadButtonPlus, GamepadButton::start},
    {kNpadButtonMinus, GamepadButton::select},
    {kNpadButtonLeft, GamepadButton::dpad_left},
    {kNpadButtonUp, GamepadButton::dpad_up},
    {kNpadButtonRight, GamepadButton::dpad_right},
    {kNpadButtonDown, GamepadButton::dpad_down},
};

bool same_camera(const OrbitCamera& a, const OrbitCamera& b) noexcept {
    return a.yaw_deg == b.yaw_deg && a.pitch_deg == b.pitch_deg &&
           a.distance == b.distance && a.target.x == b.target.x &&
           a.target.y == b.target.y && a.target.z == b.target.z &&
           a.fov_y_deg == b.fov_y_deg && a.near_plane == b.near_plane &&
           a.far_plane == b.far_plane;
}

bool same_coefficients(const MaterialCoefficients& a, const MaterialCoefficients& b) noexcept {
    for (std::size_t k = 0; k < 3; ++k) {
        if (a.environment_min[k] != b.environment_min[k] ||
            a.environment_range[k] != b.environment_range[k] ||
            a.environment_color[k] != b.environment_color[k]) {
            return false;
        }
    }
    return a.alpha_min == b.alpha_min && a.alpha_range == b.alpha_range &&
           a.environment_power == b.environment_power &&
           a.environment_enabled == b.environment_enabled;
}

} // namespace

std::uint32_t gamepad_buttons_from_npad(std::uint64_t buttons) noexcept {
    std::uint32_t out = 0;
    for (const NpadMapping& mapping : kNpadMapping) {
        if ((buttons & mapping.npad) != 0) {
            out |= gamepad_button_bit(mapping.button);
        }
    }
    return out;
}

std::vector<InputBinding> stock_view_bindings() {
    return {
        bind(GamepadAxis::left_x, CameraAction::orbit_x),
        bind(GamepadAxis::left_y, CameraAction::orbit_y),
        bind(GamepadButton::select, InspectorCommand::reset_camera),
        bind(GamepadButton::start, CameraAction::exit_session),
    };
}

RenderOptions stock_view_default_options() noexcept {
    RenderOptions options;
    options.material_state = MaterialStateMode::retail;
    return options;
}

bool stock_view_is_default(const RenderOptions& options) noexcept {
    const RenderOptions pinned = stock_view_default_options();
    for (std::size_t k = 0; k < 4; ++k) {
        if (options.background[k] != pinned.background[k]) {
            return false;
        }
    }
    return options.width == pinned.width && options.height == pinned.height &&
           same_camera(options.camera, pinned.camera) && options.flip_v == pinned.flip_v &&
           options.alpha_cutoff == pinned.alpha_cutoff &&
           options.vertex_color == pinned.vertex_color && options.cull == pinned.cull &&
           options.shading == pinned.shading &&
           options.material_state == pinned.material_state &&
           options.wet_road_reflections == pinned.wet_road_reflections &&
           options.reflection_pass == pinned.reflection_pass &&
           same_coefficients(options.coefficients, pinned.coefficients);
}

StockView::StockView() {
    if (!camera_.set_bindings(stock_view_bindings(), bindings_error_) &&
        bindings_error_.empty()) {
        bindings_error_ = "the control table was refused";
    }
}

bool StockView::start(const PreparedCarScene& scene, std::string& error) {
    error.clear();
    if (!bindings_error_.empty()) {
        error = "controls: " + bindings_error_;
        return false;
    }
    camera_.reset();
    return inspector_.reset(scene, stock_view_default_options(), RenderLimits{}, error);
}

StockViewStep StockView::step(const GamepadFrame& frame) {
    StockViewStep out;
    if (!inspector_.has_scene()) {
        return out;
    }
    out.camera = camera_.step(frame, inspector_);
    out.exit = out.camera.exit_requested;
    return out;
}

bool StockView::render(Frame& frame, RenderStats& stats, std::string& error) {
    error.clear();
    if (!inspector_.has_scene()) {
        error = "no scene to render";
        return false;
    }
    try {
        if (!render_scene(*inspector_.scene(), inspector_.options(), inspector_.limits(),
                          frame, stats, error)) {
            return false;
        }
    } catch (const std::bad_alloc&) {
        error = "out of memory while rendering";
        return false;
    } catch (const std::exception& caught) {
        error = std::string("failed while rendering: ") + caught.what();
        return false;
    }
    inspector_.clear_frame_stale();
    inspector_.clear_dirty();
    return true;
}

bool StockView::needs_render() const noexcept {
    return inspector_.has_scene() && inspector_.frame_stale();
}

bool StockView::at_default() const noexcept {
    return stock_view_is_default(inspector_.options());
}

} // namespace nfsu
