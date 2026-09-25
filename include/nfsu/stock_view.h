#pragma once

#include "nfsu/input.h"
#include "nfsu/input_camera.h"
#include "nfsu/inspect.h"
#include "nfsu/render.h"
#include "nfsu/scene.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// The stock car on the console, orbited with a controller (issue #148). The
// Switch application (switch/car_main.cpp) samples its controller into a
// GamepadFrame once a frame and hands it here; everything that decides what
// the frame does - which control does what, the camera it moves, when the
// frame has to be rendered again and whether the camera is the one the
// pinned frame was rendered from - is decided here, from the frames alone,
// so a recorded sequence of frames states a console session on the desktop
// (tests/stock_frame_tests.cpp). docs/SWITCH_BUILD.md.
//
// The camera is the controller model of docs/INPUT.md unchanged: an
// Inspector holds it and a GamepadCamera moves it, through
// Inspector::orbit and Inspector::command only. This file adds the control
// table and nothing else to that model. Nothing here reads a clock or calls
// an operating system; the frame carries its own time.

// libnx's HidNpadButton bits for the sixteen buttons docs/INPUT.md maps, by
// value, so that the mapping below is neutral code a desktop test runs.
// switch/car_main.cpp checks each against libnx's own enumerator at compile
// time.
constexpr std::uint64_t kNpadButtonA = 1ULL << 0U;
constexpr std::uint64_t kNpadButtonB = 1ULL << 1U;
constexpr std::uint64_t kNpadButtonX = 1ULL << 2U;
constexpr std::uint64_t kNpadButtonY = 1ULL << 3U;
constexpr std::uint64_t kNpadButtonStickL = 1ULL << 4U;
constexpr std::uint64_t kNpadButtonStickR = 1ULL << 5U;
constexpr std::uint64_t kNpadButtonL = 1ULL << 6U;
constexpr std::uint64_t kNpadButtonR = 1ULL << 7U;
constexpr std::uint64_t kNpadButtonZL = 1ULL << 8U;
constexpr std::uint64_t kNpadButtonZR = 1ULL << 9U;
constexpr std::uint64_t kNpadButtonPlus = 1ULL << 10U;
constexpr std::uint64_t kNpadButtonMinus = 1ULL << 11U;
constexpr std::uint64_t kNpadButtonLeft = 1ULL << 12U;
constexpr std::uint64_t kNpadButtonUp = 1ULL << 13U;
constexpr std::uint64_t kNpadButtonRight = 1ULL << 14U;
constexpr std::uint64_t kNpadButtonDown = 1ULL << 15U;

// The GamepadButton bits of a held HidNpadButton set (padGetButtons), by
// docs/INPUT.md's table: A face_east, B face_south, X face_north, Y
// face_west, the stick clicks, L and R the shoulders, ZL and ZR the
// triggers, + start, - select and the D-pad. Every other bit is dropped:
// the stick pseudo-buttons 16..23, which would make a stick press the
// D-pad, the single Joy-Con's SL and SR, and everything above them.
std::uint32_t gamepad_buttons_from_npad(std::uint64_t buttons) noexcept;

// The application's control table, over the camera actions of
// include/nfsu/input_camera.h:
//
//   left stick  orbit: right swings the eye to the camera's right around
//               the car, up raises it (docs/INPUT.md's convention)
//   -           reset_camera: back to the default camera, and so to the
//               frame the pinned hash is compared with
//   +           exit_session: leave the application
//
// Nothing else is bound. The pan, the zoom, the side views and every
// selection and view command stay unbound, so no control can change a
// render option or the viewport.
std::vector<InputBinding> stock_view_bindings();

// True when every field of options is RenderOptions{}'s: the camera, the
// viewport, every render option, the coefficients and the background. The
// pinned frames are rendered with RenderOptions{}, so this is when a
// frame's hash can be compared with a pin at all. Exact comparison: a
// camera that orbited away and back by hand is not the default unless its
// every value came back to the bit, which is what reset_camera does.
bool stock_view_is_default(const RenderOptions& options) noexcept;

// What one controller frame did.
struct StockViewStep {
    CameraStep camera;
    // + was pressed on this frame.
    bool exit = false;
};

// The session: a prepared scene, the camera over it and the controller
// that moves the camera. It renders with the CPU reference renderer, as
// render_stock_frame does, with the options the camera is at.
class StockView {
public:
    // The control table above, StickDeadZone{} and CameraRates{}.
    StockView();

    StockView(const StockView&) = delete;
    StockView& operator=(const StockView&) = delete;

    // Binds a prepared scene, which must outlive the view, at
    // RenderOptions{} and RenderLimits{}, and forgets every earlier
    // controller frame. The first frame is then due: needs_render() is
    // true. False, with error set and no scene bound, when the Inspector
    // refuses the scene.
    bool start(const PreparedCarScene& scene, std::string& error);

    // Reads one controller frame and applies it to the camera. Before
    // start() has succeeded it moves nothing and reports nothing.
    StockViewStep step(const GamepadFrame& frame);

    // Renders the scene at the current camera into frame and stats with
    // render_scene and RenderLimits{}, then marks the frame current. False,
    // with error set and the frame still due, without a scene or when the
    // render fails; an allocation failure is caught and reported.
    bool render(Frame& frame, RenderStats& stats, std::string& error);

    // The camera has moved, or a scene was bound, since the last render.
    bool needs_render() const noexcept;
    // stock_view_is_default(options()).
    bool at_default() const noexcept;
    // The options the next render uses.
    const RenderOptions& options() const noexcept { return inspector_.options(); }

private:
    Inspector inspector_;
    GamepadCamera camera_;
    // Why the control table was refused, which cannot happen for the
    // table above; start() reports it instead of binding a scene.
    std::string bindings_error_;
};

} // namespace nfsu
