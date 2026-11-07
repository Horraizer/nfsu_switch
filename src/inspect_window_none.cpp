#include "nfsu/inspect_window.h"

// The window backend for platforms this project has no window code for. It
// fails closed: nothing here pretends to open a window, and a caller that
// asked for one is told plainly that this build cannot.

namespace nfsu {

bool inspection_window_supported() noexcept { return false; }

const char* inspection_window_backend_name() noexcept { return "none"; }

bool run_inspection_window(
    Inspector& inspector,
    const InspectionWindowOptions& options,
    std::string& error) {
    (void)inspector;
    (void)options;
    error = "this build has no desktop inspection window backend";
    return false;
}

} // namespace nfsu
