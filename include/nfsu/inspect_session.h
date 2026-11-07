#pragma once

#include "nfsu/inspect.h"
#include "nfsu/inspect_window.h"
#include "nfsu/platform.h"
#include "nfsu/render.h"

#include <string>
#include <vector>

namespace nfsu {

class GamepadCamera;

// Everything the inspection window decides that is not a window: what a
// drag, a click, a wheel notch and a key do, at what size the next frame is
// rendered, when a render happens, how the view and the highlight become
// the pixels that are shown, and what ends the session. See
// docs/INSPECTOR.md and docs/PLATFORM_SERVICES.md.
//
// This is the logic src/inspect_window_win32.cpp used to hold in its window
// procedure, moved here unchanged so that it runs on every platform and is
// tested headlessly through the fakes of include/nfsu/platform_fakes.h. It
// reaches the platform only through the services it is handed: input for
// pointer capture, display for presenting, lifecycle for ending the
// session. It renders with the CPU reference renderer, as the window
// always has.
//
// The backend owns the event loop and decides when each call is made. The
// Win32 backend calls handle() for every input message as the window
// procedure receives it, and update() then present() for every paint; a
// frame-driven backend runs run_inspection_loop below. The session and the
// inspector must both outlive every call, and the services must outlive
// the session.
class InspectionSession {
public:
    InspectionSession(
        Inspector& inspector,
        const InspectionWindowOptions& options,
        InputService& input,
        DisplayService& display,
        LifecycleService& lifecycle);

    InspectionSession(const InspectionSession&) = delete;
    InspectionSession& operator=(const InspectionSession&) = delete;

    // Takes the surface size from the display and sizes the viewport to it.
    // Asks for no redraw: the first paint of a new surface is the
    // platform's to schedule.
    void start();

    // One input event, applied at once.
    void handle(const InputEvent& event);

    // handle() for every event the input service has queued.
    void pump_events();

    // Renders when the inspector says the frame is stale or there is no
    // frame yet, then resolves a pending click against the frame now on
    // hand. A render that fails keeps the previous frame and reports the
    // failure in the overlay; one that fails with no frame yet fails the
    // session.
    void update();

    // Presents the current frame through the display: the view the
    // inspector selects, the selected group tinted, the overlay on top.
    // Draws nothing, and still begins and ends the frame, when the surface
    // has no area or there is no frame yet.
    void present();

    // Whether a controller is moving the camera. While it is, the frame is
    // rendered at 1 / drag_divisor of the surface, as it is during a
    // pointer drag, and the surface size returns the moment it stops,
    // including when its device disappears mid-motion.
    void set_gamepad_motion(bool moving);

    // Set when a render failed with no frame to fall back to, or when a
    // frame could not be composed; the session cannot go on.
    bool failed() const noexcept { return !failure_.empty(); }
    const std::string& failure() const noexcept { return failure_; }

    // Whether anything asked for a redraw since the last present(). The
    // Win32 backend leaves this to the window's own paint scheduling; a
    // frame loop presents when it is set.
    bool redraw_requested() const noexcept { return redraw_requested_; }

    // Read-only views of the state, for tests and for a backend's overlay.
    bool dragging() const noexcept { return orbiting_ || panning_; }
    bool have_frame() const noexcept { return have_frame_; }
    const Frame& frame() const noexcept { return frame_; }
    const std::string& render_error() const noexcept { return render_error_; }

private:
    bool apply_viewport();
    void begin_drag(bool orbit, std::int32_t x, std::int32_t y);
    void end_drag(std::int32_t x, std::int32_t y, bool was_orbit);
    void pointer_move(std::int32_t x, std::int32_t y);
    void wheel(std::int32_t delta);
    void key(Key key);
    bool compose();
    void request_redraw();

    Inspector& inspector_;
    InspectionWindowOptions options_;
    InputService& input_;
    DisplayService& display_;
    LifecycleService& lifecycle_;

    Frame frame_;
    RenderStats stats_;
    // Set when frame_ holds a frame that can be presented.
    bool have_frame_ = false;
    // The last render failure, shown in the overlay until a render succeeds.
    std::string render_error_;
    // Pointer state.
    bool orbiting_ = false;
    bool panning_ = false;
    std::int32_t last_x_ = 0;
    std::int32_t last_y_ = 0;
    std::int32_t press_x_ = 0;
    std::int32_t press_y_ = 0;
    // Wheel movement that has not yet added up to a whole notch. A
    // precision trackpad reports less than a notch at a time, and dropping
    // the remainder would leave it unable to zoom at all.
    std::int32_t wheel_residual_ = 0;
    // A click that has not been resolved into a selection yet, in surface
    // pixels; resolved against the frame the next update renders.
    bool pending_pick_ = false;
    std::int32_t pick_x_ = 0;
    std::int32_t pick_y_ = 0;
    // The surface size the viewport follows.
    std::int32_t client_width_ = 0;
    std::int32_t client_height_ = 0;
    bool gamepad_moving_ = false;
    bool redraw_requested_ = false;
    // Scratch buffers, kept between presents so that a drag allocates
    // nothing.
    std::vector<unsigned char> view_rgba_;
    std::vector<unsigned char> mask_;
    std::vector<unsigned char> bgra_;
    std::vector<std::string> overlay_;
    // Why the session cannot go on; empty while it can.
    std::string failure_;
};

// The session loop of a platform that draws frames on its own clock rather
// than on window messages: a console, or a test. Each iteration pumps the
// lifecycle, applies every queued input event, samples the controllers
// through `input` and applies them through `gamepads` when it is not null,
// renders when the frame is stale, and presents when anything changed. A
// controller's exit_session asks the lifecycle to end the session.
//
// The Win32 backend does not use it: its loop is the window's message loop,
// and it drives the same session from the window procedure instead.
//
// Refused before anything runs, as run_inspection_window refuses: an
// inspector with no scene and a drag divisor outside 1..kMaxDragDivisor.
// Returns false, with error set, when the session failed; true when the
// lifecycle ended it.
bool run_inspection_loop(
    Inspector& inspector,
    const InspectionWindowOptions& options,
    GamepadCamera* gamepads,
    InputService& input,
    DisplayService& display,
    LifecycleService& lifecycle,
    TimingService& timing,
    std::string& error);

} // namespace nfsu
