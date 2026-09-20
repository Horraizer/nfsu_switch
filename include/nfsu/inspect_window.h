#pragma once

#include "nfsu/inspect.h"

#include <cstdint>
#include <string>

namespace nfsu {

// The desktop inspection window: the one piece of this project that talks to
// a platform windowing API. It owns the event loop, renders each frame with
// the CPU reference renderer from Inspector::options(), presents the
// diagnostic view and draws the overlay text the Inspector builds.
//
// Everything it decides lives in include/nfsu/inspect.h, which knows nothing
// about windows. A deko3d backend replaces this file alone: it drives the
// same Inspector, submits the same PreparedCarScene with the same
// RenderOptions and numbers its triangles as prepared_triangle_ranges says,
// and every control, view and overlay line then behaves identically. See
// docs/INSPECTOR.md.

struct InspectionWindowOptions {
    // Window title. Bytes outside printable ASCII are replaced with '?'.
    std::string title = "nfsu inspector";
    // While a pointer button is held the frame is rendered at 1 / divisor of
    // the window size and stretched to fill it, which keeps a drag
    // responsive on a CPU renderer. 1 always renders at full size. Values
    // above kMaxDragDivisor are refused.
    std::uint32_t drag_divisor = 2;
};

constexpr std::uint32_t kMaxDragDivisor = 16;

// True when this build has a window backend at all. False builds still
// compile and link; run_inspection_window then fails with a clear message
// instead of pretending to open anything.
bool inspection_window_supported() noexcept;

// "win32" or "none".
const char* inspection_window_backend_name() noexcept;

// Opens the window and runs until the user closes it.
//
// The inspector must already hold a scene. Each frame is rendered with
// render_scene from inspector.options() and inspector.limits(); a render
// that fails leaves the previous frame on screen and reports the failure in
// the overlay, and only a failure with no frame yet ends the session.
//
// Returns false, with error set, when this build has no backend, when the
// options are out of range, when the inspector has no scene, and when the
// window cannot be created or the first frame cannot be rendered. It writes
// nothing to stdout, creates no file and never touches the scene.
bool run_inspection_window(
    Inspector& inspector,
    const InspectionWindowOptions& options,
    std::string& error);

} // namespace nfsu
