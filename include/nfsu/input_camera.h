#pragma once

#include "nfsu/input.h"
#include "nfsu/inspect.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Controller input on the inspection camera. See docs/INPUT.md.
//
// The camera it moves is the OrbitCamera an Inspector holds, and it moves it
// only through Inspector::orbit, pan, zoom and command, the calls the
// desktop pointer makes. So every clamp and wrap that keeps the camera
// valid under hostile pointer input keeps it valid under a controller too,
// and whichever renderer draws Inspector::options() - the CPU reference
// renderer today, a deko3d one later - shows the result. Nothing here
// renders, and nothing here knows which renderer will.

// The camera's actions, as ids for the remapping table of include/nfsu/input.h.
// The five motions are analog; exit_session and one action per
// InspectorCommand, which follow it in InspectorCommand order, are digital.
enum class CameraAction : std::uint16_t {
    orbit_x,
    orbit_y,
    pan_x,
    pan_y,
    zoom,
    exit_session
};

constexpr std::uint16_t kFirstCameraCommand = 6;
constexpr std::size_t kInspectorCommandCount =
    static_cast<std::size_t>(InspectorCommand::toggle_flip_v) + 1U;
constexpr std::size_t kCameraActionCount = kFirstCameraCommand + kInspectorCommandCount;

std::uint16_t camera_action_id(CameraAction action) noexcept;
std::uint16_t camera_command_action(InspectorCommand command) noexcept;

// The kind of every camera action, indexed by action id.
std::vector<ActionKind> camera_action_kinds();

// The table a controller starts with: right stick orbits, left stick pans,
// the triggers zoom, the D-pad snaps to the four side views, and the rest
// is laid out in docs/INPUT.md. Every control is bound exactly once.
std::vector<InputBinding> default_camera_bindings();

// How fast full deflection moves the camera. The convention is that the
// stick moves the camera: right swings the eye along the camera's right
// vector around the target, up raises it, and a pan carries the target the
// way the stick points, in the plane facing the camera. For the orbit that
// is what the pointer already does - a drag right lowers yaw, which is the
// same swing, and a drag down raises pitch - and for the pan it is the
// opposite of the pointer's grab, which carries the target against the
// drag. docs/INPUT.md measures each direction on screen. The table's invert
// flag is how a player who wants the other convention on an axis gets it.
struct CameraRates {
    // Degrees of yaw or pitch per second, whatever the pointer's own
    // degrees per pixel are.
    double orbit_deg_per_second = 120.0;
    // Viewport heights per second, so a pan crosses the screen in the same
    // time at any resolution and at any drag divisor.
    double pan_heights_per_second = 0.75;
    // Zoom steps per second; a step is Inspector::zoom(1).
    double zoom_steps_per_second = 6.0;
};

constexpr double kMaxOrbitDegPerSecond = 3600.0;
constexpr double kMaxPanHeightsPerSecond = 100.0;
constexpr double kMaxZoomStepsPerSecond = 240.0;

// Checks that every rate is finite, above 0 and at most its maximum.
bool validate_camera_rates(const CameraRates& rates, std::string& error);

// What one frame did, in the units each Inspector call takes.
struct CameraStep {
    // Handed to Inspector::orbit, in the pointer pixels it takes.
    double orbit_dx = 0.0;
    double orbit_dy = 0.0;
    // Handed to Inspector::pan, in pixels of the current viewport.
    double pan_dx = 0.0;
    double pan_dy = 0.0;
    // Handed to Inspector::zoom.
    int zoom_steps = 0;
    // The commands pressed on this frame, in InspectorCommand order, each
    // applied once. A command the inspector cannot apply changes nothing,
    // exactly as a key the window maps to it.
    std::vector<InspectorCommand> commands;
    // A motion action was nonzero on this frame, whatever the interval. A
    // backend renders a moving camera the way the window renders a drag.
    bool moving = false;
    // exit_session was pressed on this frame.
    bool exit_requested = false;
};

class GamepadCamera {
public:
    // The default table, StickDeadZone{} and CameraRates{}.
    GamepadCamera();

    // Each setter validates, and changes nothing when it refuses. Each one
    // that succeeds also forgets every earlier frame, so a button held
    // across a remapping counts as held from before it and fires nothing.
    bool set_bindings(const std::vector<InputBinding>& bindings, std::string& error);
    bool set_dead_zone(const StickDeadZone& zone, std::string& error);
    bool set_rates(const CameraRates& rates, std::string& error);

    const std::vector<InputBinding>& bindings() const noexcept { return bindings_; }
    const StickDeadZone& dead_zone() const noexcept { return zone_; }
    const CameraRates& rates() const noexcept { return rates_; }

    // Reads one frame and applies it: orbit, then pan, then zoom, then the
    // pressed commands in InspectorCommand order. Motion is the action value
    // times its rate times the interval the frame reports, so it is the
    // same at any frame rate and zero on the first frame; zoom keeps the
    // fraction of a step that has not added up to a whole one yet, as the
    // window keeps a fraction of a wheel notch. An inspector with no scene
    // ignores every call, as it does for the pointer.
    CameraStep step(const GamepadFrame& frame, Inspector& inspector);

    // Forgets every earlier frame and the zoom fraction; keeps the
    // configuration. For a new session.
    void reset() noexcept;

private:
    bool configure(std::string& error);

    std::vector<InputBinding> bindings_;
    StickDeadZone zone_;
    CameraRates rates_;
    InputMapper mapper_;
    double zoom_residual_ = 0.0;
};

} // namespace nfsu
