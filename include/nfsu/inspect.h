#pragma once

#include "nfsu/render.h"
#include "nfsu/scene.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Interactive inspection state over a PreparedCarScene: the camera the
// pointer drives, which material group is selected, which diagnostic view
// and overlay are shown, and the text of that overlay. See
// docs/INSPECTOR.md.
//
// Nothing here opens a window, reads a file, calls an operating system or
// keeps a clock. The state is pure data and every transition is arithmetic
// on it, so the whole layer is exercised headlessly by
// tests/inspect_tests.cpp and a second backend reuses it unchanged.
// include/nfsu/inspect_window.h holds the desktop window that drives it.
//
// A backend frame reaches it through InspectionBuffers below and never
// through a backend type. The one place the CPU renderer appears is
// inspection_buffers_of(const Frame&), a convenience for the desktop
// backend; render.h is included for RenderOptions, RenderStats, RenderLimits
// and OrbitCamera, which are the camera and option contract every backend
// shares, not for the rasterizer.

// The per-pixel buffers an inspector reads back after a frame.
//
// ids holds, per pixel, the id of the triangle that owns it, kNoTriangle
// where nothing was drawn, exactly as the CPU renderer numbers triangles
// (see prepared_triangle_ranges). depth and rgba are optional; a view that
// needs a buffer that is null is refused. Nothing is copied: the buffers
// must outlive the call they are passed to.
struct InspectionBuffers {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const std::uint32_t* ids = nullptr;       // width * height entries
    const double* depth = nullptr;            // width * height entries
    const unsigned char* rgba = nullptr;      // width * height * 4 bytes
};

// The buffers of a CPU frame. The frame must outlive the result.
InspectionBuffers inspection_buffers_of(const Frame& frame) noexcept;

// How far one unit of pointer or wheel motion moves the camera.
struct InspectorSensitivity {
    // Degrees of yaw and pitch per pixel of drag.
    double orbit_deg_per_pixel = 0.35;
    // Distance is divided by this per wheel step towards the car.
    double zoom_step = 1.125;
};

constexpr double kMaxOrbitSensitivity = 45.0;
constexpr double kMinZoomStep = 1.0001;
constexpr double kMaxZoomStep = 4.0;
// Pointer deltas and wheel steps are clamped to these before they are
// applied, so no single event can move the camera unboundedly far. The clamp
// also turns a delta that is not a number into a bounded one, which is what
// keeps a stray event out of the camera rather than into an invalid one; the
// yaw wrap is a floor and a subtraction, with no loop that could fail to
// terminate.
constexpr double kMaxPointerDelta = 100000.0;
constexpr int kMaxZoomSteps = 1024;

// Checks that both values are finite, that orbit_deg_per_pixel is above 0
// and at most kMaxOrbitSensitivity and that zoom_step is between
// kMinZoomStep and kMaxZoomStep.
bool validate_inspector_sensitivity(
    const InspectorSensitivity& sensitivity, std::string& error);

// What the window shows in place of the shaded frame. Every view is derived
// from the buffers above, so each one is a diagnostic of the frame that was
// actually drawn and never a second render with other rules.
enum class InspectorView {
    // The frame as the renderer produced it.
    shaded,
    // Each drawn pixel takes a deterministic color from the prepared
    // position of the material group that drew it.
    draws,
    // The same, keyed on the prepared mesh instead of the group.
    meshes,
    // Drawn pixels as a gray ramp between the nearest and the furthest
    // depth in the frame, the nearest white. A frame whose drawn pixels all
    // share one depth is drawn white.
    depth
};

const char* inspector_view_name(InspectorView view) noexcept;

// Which text the overlay carries. The lines are built by
// Inspector::build_overlay and are printable ASCII.
enum class InspectorOverlay {
    off,
    // Camera, view, render options, frame statistics and one selection line.
    summary,
    // summary, then every recovered field of the selected group, of its
    // texture and of its retail render state.
    detail,
    // The key and pointer bindings the desktop window implements.
    help
};

const char* inspector_overlay_name(InspectorOverlay overlay) noexcept;

// One discrete action. The desktop window maps keys to these and to nothing
// else; a second backend may map whatever it likes to the same set.
enum class InspectorCommand {
    reset_camera,
    view_front,
    view_rear,
    view_left,
    view_right,
    view_top,
    frame_scene,
    frame_selection,
    select_next_draw,
    select_previous_draw,
    select_next_mesh,
    select_previous_mesh,
    clear_selection,
    cycle_view,
    cycle_overlay,
    toggle_highlight,
    cycle_cull,
    toggle_shading,
    toggle_material_state,
    toggle_vertex_color,
    toggle_flip_v
};

// A stable lowercase ASCII name, for the overlay and for tests.
const char* inspector_command_name(InspectorCommand command) noexcept;

// Overlay lines are bounded so that a window can size a text box from the
// count alone.
constexpr std::size_t kMaxOverlayLineBytes = 200;

class Inspector {
public:
    // An inspector with no scene. Every query answers the empty state and
    // every input is ignored until reset succeeds.
    Inspector() = default;

    // Binds a scene and the options its first frame is rendered with.
    //
    // The scene is checked with validate_prepared_scene and must outlive
    // the inspector; nothing is copied out of it. options supplies the
    // camera the session starts from, which is also the camera
    // reset_camera returns to, and every render option the overlay reports.
    // It is refused unless validate_camera accepts its camera, its size is
    // inside limits and, with the retail material state, its coefficients
    // validate: an inspector never holds options a backend would refuse to
    // render.
    //
    // On failure the inspector is left with no scene and error is set.
    bool reset(
        const PreparedCarScene& scene,
        const RenderOptions& options,
        const RenderLimits& limits,
        std::string& error);

    bool has_scene() const noexcept { return scene_ != nullptr; }
    const PreparedCarScene* scene() const noexcept { return scene_; }

    // The options to render the next frame with. Always renderable.
    const RenderOptions& options() const noexcept { return options_; }
    const RenderLimits& limits() const noexcept { return limits_; }

    const InspectorSensitivity& sensitivity() const noexcept {
        return sensitivity_;
    }
    bool set_sensitivity(
        const InspectorSensitivity& sensitivity, std::string& error);

    // Resizes the frame the backend is asked to render. Refused, with the
    // old size kept, for a zero side or a size outside limits.
    bool set_viewport(
        std::uint32_t width, std::uint32_t height, std::string& error);

    // Pointer drag in pixels, y downwards, with the grab convention: the
    // car follows the pointer. Dragging right turns the side that faces the
    // camera towards the right of the screen, which lowers yaw; dragging
    // down brings the roof towards the camera, which raises pitch. Deltas
    // are clamped to kMaxPointerDelta, yaw is wrapped into [-180, 180) and
    // pitch is clamped to kMinPitch..kMaxPitch.
    void orbit(double dx, double dy);

    // Pointer drag in pixels that moves the camera target in the plane
    // through it that faces the camera, so that a point at the target's
    // depth stays under the pointer. Each target component is clamped to
    // kMaxTargetMagnitude. A viewport or camera the projection cannot be
    // built for moves nothing.
    void pan(double dx, double dy);

    // Wheel steps: positive zooms in, dividing distance by zoom_step per
    // step, negative zooms out. Clamped to kMaxZoomSteps and then to
    // kMinDistance..kMaxDistance.
    void zoom(int steps);

    // Applies one command. Returns false, with error set and nothing
    // changed, only when the command cannot be applied: selecting or
    // framing with no scene, with no drawable group, or with nothing
    // selected.
    bool command(InspectorCommand command, std::string& error);

    // Selects the group that drew the pixel, or clears the selection when
    // nothing was drawn there. buffers must come from a frame rendered from
    // this scene with these options; that cannot be checked, and a frame
    // from anything else selects whatever its ids happen to name. Fails on
    // a pixel outside the buffers and on buffers with no ids.
    bool pick(
        const InspectionBuffers& buffers,
        std::uint32_t x,
        std::uint32_t y,
        std::string& error);

    // Prepared position of the selected group, or kNoHandle.
    std::size_t selected_draw() const noexcept { return selected_draw_; }
    // Prepared position of its mesh, or kNoHandle.
    std::size_t selected_mesh() const noexcept;
    void clear_selection() noexcept;

    InspectorView view() const noexcept { return view_; }
    InspectorOverlay overlay() const noexcept { return overlay_; }
    bool highlight() const noexcept { return highlight_; }

    // The submission order and the triangle id table of the current
    // material state, rebuilt whenever that state changes. Both are empty
    // without a scene.
    const std::vector<std::uint32_t>& draw_order() const noexcept {
        return order_;
    }
    const std::vector<PreparedTriangleRange>& triangle_ranges() const noexcept {
        return ranges_;
    }

    // The prepared position of the group that drew triangle id, or
    // kNoHandle.
    std::size_t draw_of_triangle(std::uint32_t id) const noexcept;

    // Fills rgba with width * height * 4 bytes for the current view. Fails,
    // leaving rgba empty, on buffers with no ids, on a zero side, on a size
    // outside the render limits, and when the view needs a buffer that is
    // null.
    bool view_pixels(
        const InspectionBuffers& buffers,
        std::vector<unsigned char>& rgba,
        std::string& error) const;

    // Fills mask with one byte per pixel: 1 where the pixel was drawn by
    // the selected group, 0 elsewhere. All zero when nothing is selected or
    // highlighting is off. Fails as view_pixels does.
    bool highlight_mask(
        const InspectionBuffers& buffers,
        std::vector<unsigned char>& mask,
        std::string& error) const;

    // The overlay text for the frame those statistics describe, replacing
    // lines. Every line is printable ASCII and at most kMaxOverlayLineBytes
    // long. Empty for InspectorOverlay::off.
    void build_overlay(
        const RenderStats& stats, std::vector<std::string>& lines) const;

    // Set by everything that changes what the window would show, the
    // selection, the view and the overlay included, because the window has
    // to repaint for those too.
    bool dirty() const noexcept { return dirty_; }
    void clear_dirty() noexcept { dirty_ = false; }

    // Set by the subset of that which changes the frame itself: the camera,
    // the viewport and the render options. Selecting a group, switching the
    // view and moving the overlay all leave the frame alone, and a backend
    // that renders only when this is set answers those from the frame it
    // already has instead of rasterizing a car again.
    bool frame_stale() const noexcept { return frame_stale_; }
    void clear_frame_stale() noexcept { frame_stale_ = false; }

private:
    // The frame the window is showing no longer matches the state.
    void touch_frame() noexcept {
        dirty_ = true;
        frame_stale_ = true;
    }
    bool rebuild_order(std::string& error);
    // Bounds of the drawn vertices of one group, or of every prepared mesh
    // when draw is kNoHandle, in world coordinates. False when the range
    // holds no usable vertex.
    bool world_bounds(
        std::size_t draw, Vec3& minimum, Vec3& maximum) const noexcept;
    bool frame_bounds(std::size_t draw, std::string& error);
    void select_draw(std::size_t draw) noexcept;

    const PreparedCarScene* scene_ = nullptr;
    RenderOptions options_;
    RenderLimits limits_;
    OrbitCamera home_;
    InspectorSensitivity sensitivity_;
    InspectorView view_ = InspectorView::shaded;
    InspectorOverlay overlay_ = InspectorOverlay::summary;
    bool highlight_ = true;
    bool dirty_ = true;
    bool frame_stale_ = true;
    std::size_t selected_draw_ = kNoHandle;
    std::vector<std::uint32_t> order_;
    std::vector<PreparedTriangleRange> ranges_;
};

} // namespace nfsu
