#include "nfsu/inspect_window.h"

// The part of a window backend that is not a window: arithmetic every
// backend needs and none of them should reinvent. It calls no operating
// system, so it is compiled into nfsu_inspect_window on every platform,
// including the builds whose backend is the stub, and is tested headlessly.

namespace nfsu {

PanDelta pan_delta_for(
    int client_width,
    int client_height,
    std::uint32_t frame_width,
    std::uint32_t frame_height,
    double dx,
    double dy) noexcept {
    PanDelta out;
    if (client_width <= 0 || client_height <= 0 || frame_width == 0U ||
        frame_height == 0U) {
        return out;
    }
    // The inverse of the stretch that puts the frame on the client area,
    // per axis, because a divisor that does not divide the client size
    // evenly stretches the two axes by slightly different amounts.
    out.dx = dx * static_cast<double>(frame_width) / static_cast<double>(client_width);
    out.dy =
        dy * static_cast<double>(frame_height) / static_cast<double>(client_height);
    return out;
}

}  // namespace nfsu
