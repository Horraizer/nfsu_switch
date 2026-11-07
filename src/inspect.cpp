#include "nfsu/inspect.h"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <cstdio>
#include <new>
#include <utility>

namespace nfsu {

namespace {

// Shortest round-tripping decimal, as the viewer report prints doubles.
std::string number(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

std::string hex32_text(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X",
                  static_cast<unsigned int>(value));
    return buffer;
}

std::string hex8_text(std::uint8_t value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02X",
                  static_cast<unsigned int>(value));
    return buffer;
}

// Printable ASCII, with quote, backslash and every other byte escaped. A
// mesh name is 28 raw bytes from the file and reaches the overlay unchanged
// otherwise.
std::string quoted(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte >= 0x20U && byte <= 0x7EU && ch != '"' && ch != '\\') {
            out += ch;
        } else {
            out += "\\x";
            out += kHex[byte >> 4U];
            out += kHex[byte & 0x0FU];
        }
    }
    return out + "\"";
}

double clamp_double(double value, double low, double high) noexcept {
    if (!(value == value)) {  // NaN never reaches the camera
        return low;
    }
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

double clamp_delta(double value) noexcept {
    return clamp_double(value, -kMaxPointerDelta, kMaxPointerDelta);
}

// Yaw into [-180, 180). The input is bounded by the delta clamp and the
// previous wrap, so the subtraction is exact enough to stay in range.
double wrap_yaw(double yaw) noexcept {
    if (!(yaw == yaw)) {
        return 0.0;
    }
    const double turns = std::floor((yaw + 180.0) / 360.0);
    const double wrapped = yaw - 360.0 * turns;
    return clamp_double(wrapped, -180.0, 180.0);
}

// Distinct, bounded colors for the draws and meshes views. The same
// generator as fallback_color, keyed on an index instead of a hash, so
// neighbouring indices are far apart and every channel stays in 64..223.
void index_color(std::size_t index, unsigned char (&rgba)[4]) noexcept {
    const std::uint32_t key = static_cast<std::uint32_t>(index) * 2654435761U;
    fallback_color(key, rgba);
}

}  // namespace

InspectionBuffers inspection_buffers_of(const Frame& frame) noexcept {
    InspectionBuffers buffers;
    buffers.width = frame.width();
    buffers.height = frame.height();
    buffers.ids = frame.ids().empty() ? nullptr : frame.ids().data();
    buffers.depth = frame.depth().empty() ? nullptr : frame.depth().data();
    buffers.rgba = frame.rgba().empty() ? nullptr : frame.rgba().data();
    return buffers;
}

bool validate_inspector_sensitivity(
    const InspectorSensitivity& sensitivity, std::string& error) {
    if (!std::isfinite(sensitivity.orbit_deg_per_pixel) ||
        sensitivity.orbit_deg_per_pixel <= 0.0 ||
        sensitivity.orbit_deg_per_pixel > kMaxOrbitSensitivity) {
        error = "orbit sensitivity must be above 0 and at most " +
                number(kMaxOrbitSensitivity) + " degrees per pixel";
        return false;
    }
    if (!std::isfinite(sensitivity.zoom_step) ||
        sensitivity.zoom_step < kMinZoomStep ||
        sensitivity.zoom_step > kMaxZoomStep) {
        error = "zoom step must be between " + number(kMinZoomStep) + " and " +
                number(kMaxZoomStep);
        return false;
    }
    return true;
}

const char* inspector_view_name(InspectorView view) noexcept {
    switch (view) {
    case InspectorView::shaded:
        return "shaded";
    case InspectorView::draws:
        return "draws";
    case InspectorView::meshes:
        return "meshes";
    case InspectorView::depth:
        return "depth";
    }
    return "shaded";
}

const char* inspector_overlay_name(InspectorOverlay overlay) noexcept {
    switch (overlay) {
    case InspectorOverlay::off:
        return "off";
    case InspectorOverlay::summary:
        return "summary";
    case InspectorOverlay::detail:
        return "detail";
    case InspectorOverlay::help:
        return "help";
    }
    return "off";
}

const char* inspector_command_name(InspectorCommand command) noexcept {
    switch (command) {
    case InspectorCommand::reset_camera:
        return "reset-camera";
    case InspectorCommand::view_front:
        return "view-front";
    case InspectorCommand::view_rear:
        return "view-rear";
    case InspectorCommand::view_left:
        return "view-left";
    case InspectorCommand::view_right:
        return "view-right";
    case InspectorCommand::view_top:
        return "view-top";
    case InspectorCommand::frame_scene:
        return "frame-scene";
    case InspectorCommand::frame_selection:
        return "frame-selection";
    case InspectorCommand::select_next_draw:
        return "select-next-draw";
    case InspectorCommand::select_previous_draw:
        return "select-previous-draw";
    case InspectorCommand::select_next_mesh:
        return "select-next-mesh";
    case InspectorCommand::select_previous_mesh:
        return "select-previous-mesh";
    case InspectorCommand::clear_selection:
        return "clear-selection";
    case InspectorCommand::cycle_view:
        return "cycle-view";
    case InspectorCommand::cycle_overlay:
        return "cycle-overlay";
    case InspectorCommand::toggle_highlight:
        return "toggle-highlight";
    case InspectorCommand::cycle_cull:
        return "cycle-cull";
    case InspectorCommand::toggle_shading:
        return "toggle-shading";
    case InspectorCommand::toggle_material_state:
        return "toggle-material-state";
    case InspectorCommand::toggle_vertex_color:
        return "toggle-vertex-color";
    case InspectorCommand::toggle_flip_v:
        return "toggle-flip-v";
    }
    return "unknown";
}

bool Inspector::reset(
    const PreparedCarScene& scene,
    const RenderOptions& options,
    const RenderLimits& limits,
    std::string& error) {
    scene_ = nullptr;
    order_.clear();
    ranges_.clear();
    selected_draw_ = kNoHandle;
    if (!validate_prepared_scene(scene, error)) {
        return false;
    }
    if (!validate_camera(options.camera, error)) {
        return false;
    }
    if (options.width == 0 || options.height == 0 ||
        options.width > limits.max_width || options.height > limits.max_height ||
        std::uint64_t{options.width} * options.height > limits.max_pixels) {
        error = "the inspector viewport is outside the render limits";
        return false;
    }
    if (options.material_state == MaterialStateMode::retail &&
        !validate_material_coefficients(options.coefficients, error)) {
        return false;
    }
    scene_ = &scene;
    options_ = options;
    limits_ = limits;
    home_ = options.camera;
    view_ = InspectorView::shaded;
    overlay_ = InspectorOverlay::summary;
    highlight_ = true;
    frame_stale_ = true;
    dirty_ = true;
    if (!rebuild_order(error)) {
        scene_ = nullptr;
        order_.clear();
        ranges_.clear();
        return false;
    }
    return true;
}

bool Inspector::rebuild_order(std::string& error) {
    order_.clear();
    ranges_.clear();
    instances_ = PreparedMeshInstances{};
    if (scene_ == nullptr) {
        return true;
    }
    // Built here, beside the order and the id table, so that world_bounds
    // can measure every drawn copy without allocating: the overlay asks it
    // for the bounds once a frame.
    if (!prepared_mesh_instances(*scene_, instances_, error)) {
        instances_ = PreparedMeshInstances{};
        return false;
    }
    const bool retail = options_.material_state == MaterialStateMode::retail;
    if (!prepared_draw_order(*scene_, retail, order_, nullptr, error)) {
        order_.clear();
        instances_ = PreparedMeshInstances{};
        return false;
    }
    if (!prepared_triangle_ranges(*scene_, order_, ranges_, error)) {
        order_.clear();
        ranges_.clear();
        instances_ = PreparedMeshInstances{};
        return false;
    }
    return true;
}

bool Inspector::set_sensitivity(
    const InspectorSensitivity& sensitivity, std::string& error) {
    if (!validate_inspector_sensitivity(sensitivity, error)) {
        return false;
    }
    sensitivity_ = sensitivity;
    return true;
}

bool Inspector::set_viewport(
    std::uint32_t width, std::uint32_t height, std::string& error) {
    if (width == 0 || height == 0) {
        error = "the viewport must have a positive width and height";
        return false;
    }
    if (width > limits_.max_width || height > limits_.max_height ||
        std::uint64_t{width} * height > limits_.max_pixels) {
        error = "the viewport is outside the render limits";
        return false;
    }
    if (options_.width != width || options_.height != height) {
        options_.width = width;
        options_.height = height;
        touch_frame();
    }
    return true;
}

void Inspector::orbit(double dx, double dy) {
    if (scene_ == nullptr) {
        return;
    }
    const double scale = sensitivity_.orbit_deg_per_pixel;
    // The grab convention: the car follows the pointer.
    options_.camera.yaw_deg =
        wrap_yaw(options_.camera.yaw_deg - clamp_delta(dx) * scale);
    options_.camera.pitch_deg = clamp_double(
        options_.camera.pitch_deg + clamp_delta(dy) * scale, kMinPitch, kMaxPitch);
    touch_frame();
}

void Inspector::pan(double dx, double dy) {
    if (scene_ == nullptr) {
        return;
    }
    CameraFrame frame;
    std::string ignored;
    if (!make_camera_frame(options_.camera, options_.width, options_.height, frame,
                           ignored)) {
        return;
    }
    if (!(frame.focal_y > 0.0) || options_.height == 0) {
        return;
    }
    // Half the frame is distance / focal_y world units tall at the target,
    // so one pixel is that over half the height.
    const double per_pixel = 2.0 * options_.camera.distance /
                             (frame.focal_y * static_cast<double>(options_.height));
    const double right = clamp_delta(dx) * per_pixel;
    const double up = clamp_delta(dy) * per_pixel;
    Vec3 target = options_.camera.target;
    target.x += -frame.right.x * right + frame.up.x * up;
    target.y += -frame.right.y * right + frame.up.y * up;
    target.z += -frame.right.z * right + frame.up.z * up;
    options_.camera.target.x =
        clamp_double(target.x, -kMaxTargetMagnitude, kMaxTargetMagnitude);
    options_.camera.target.y =
        clamp_double(target.y, -kMaxTargetMagnitude, kMaxTargetMagnitude);
    options_.camera.target.z =
        clamp_double(target.z, -kMaxTargetMagnitude, kMaxTargetMagnitude);
    touch_frame();
}

void Inspector::zoom(int steps) {
    if (scene_ == nullptr || steps == 0) {
        return;
    }
    if (steps > kMaxZoomSteps) {
        steps = kMaxZoomSteps;
    }
    if (steps < -kMaxZoomSteps) {
        steps = -kMaxZoomSteps;
    }
    // Repeated multiplication, so no library power function decides the
    // camera. The clamp inside the loop keeps the product finite.
    double distance = options_.camera.distance;
    const int count = steps > 0 ? steps : -steps;
    for (int i = 0; i < count; ++i) {
        distance = steps > 0 ? distance / sensitivity_.zoom_step
                             : distance * sensitivity_.zoom_step;
        distance = clamp_double(distance, kMinDistance, kMaxDistance);
    }
    options_.camera.distance = distance;
    touch_frame();
}

void Inspector::select_draw(std::size_t draw) noexcept {
    if (selected_draw_ != draw) {
        selected_draw_ = draw;
        dirty_ = true;
    }
}

void Inspector::clear_selection() noexcept { select_draw(kNoHandle); }

std::size_t Inspector::selected_mesh() const noexcept {
    if (scene_ == nullptr || selected_draw_ >= scene_->draws.size()) {
        return kNoHandle;
    }
    return scene_->draws[selected_draw_].mesh;
}

std::size_t Inspector::draw_of_triangle(std::uint32_t id) const noexcept {
    if (id == kNoTriangle) {
        return kNoHandle;
    }
    return find_prepared_triangle(ranges_, id);
}

bool Inspector::pick(
    const InspectionBuffers& buffers,
    std::uint32_t x,
    std::uint32_t y,
    std::string& error) {
    if (scene_ == nullptr) {
        error = "the inspector has no scene";
        return false;
    }
    if (buffers.ids == nullptr) {
        error = "the frame carries no triangle ids";
        return false;
    }
    if (x >= buffers.width || y >= buffers.height) {
        error = "the picked pixel is outside the frame";
        return false;
    }
    const std::uint32_t id =
        buffers.ids[static_cast<std::size_t>(y) * buffers.width + x];
    select_draw(draw_of_triangle(id));
    return true;
}

bool Inspector::world_bounds(
    std::size_t draw, Vec3& minimum, Vec3& maximum) const noexcept {
    if (scene_ == nullptr) {
        return false;
    }
    bool any = false;
    const auto take = [&](const MeshVertex& vertex, const float* transform) {
        // placed_vertex is the renderer's own placement rule, so the box
        // can never describe a position the frame does not show.
        const Vec3 world = placed_vertex(vertex, transform);
        if (!any) {
            minimum = world;
            maximum = world;
            any = true;
            return;
        }
        minimum.x = std::min(minimum.x, world.x);
        minimum.y = std::min(minimum.y, world.y);
        minimum.z = std::min(minimum.z, world.z);
        maximum.x = std::max(maximum.x, world.x);
        maximum.y = std::max(maximum.y, world.y);
        maximum.z = std::max(maximum.z, world.z);
    };
    // Every drawn copy of one prepared mesh, each with the transform that
    // places it. A scene with no instances has one copy per mesh, drawn in
    // its own space, which is the null transform.
    const bool instanced = !scene_->instances.empty();
    const auto for_each_copy = [&](std::size_t handle, auto&& visit) {
        if (!instanced) {
            visit(static_cast<const float*>(nullptr));
            return;
        }
        if (handle + 1 >= instances_.first.size()) {
            return;
        }
        for (std::uint32_t k = instances_.first[handle];
             k < instances_.first[handle + 1] && k < instances_.order.size(); ++k) {
            const std::uint32_t which = instances_.order[k];
            if (which < scene_->instances.size()) {
                visit(scene_->instances[which].transform);
            }
        }
    };
    if (draw == kNoHandle) {
        for (std::size_t m = 0; m < scene_->meshes.size(); ++m) {
            const PreparedMesh& mesh = scene_->meshes[m];
            for_each_copy(m, [&](const float* transform) {
                for (const MeshVertex& vertex : mesh.vertices) {
                    take(vertex, transform);
                }
            });
        }
        return any;
    }
    if (draw >= scene_->draws.size()) {
        return false;
    }
    const PreparedDraw& prepared = scene_->draws[draw];
    if (!prepared.drawable || prepared.mesh >= scene_->meshes.size()) {
        return false;
    }
    const PreparedMesh& mesh = scene_->meshes[prepared.mesh];
    // Only the vertices the group actually draws, and only indices inside
    // the mesh: a bad index is skipped exactly as the renderer skips it.
    const std::uint64_t first = prepared.first_index / 3U;
    const std::uint64_t last = (std::uint64_t{prepared.first_index} +
                                prepared.index_count) / 3U;
    for_each_copy(prepared.mesh, [&](const float* transform) {
        for (std::uint64_t t = first; t < last && t < mesh.triangles.size(); ++t) {
            const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(t)];
            const std::uint16_t indices[3] = {triangle.a, triangle.b, triangle.c};
            for (const std::uint16_t index : indices) {
                if (index < mesh.vertices.size()) {
                    take(mesh.vertices[index], transform);
                }
            }
        }
    });
    return any;
}

bool Inspector::frame_bounds(std::size_t draw, std::string& error) {
    Vec3 minimum;
    Vec3 maximum;
    if (!world_bounds(draw, minimum, maximum)) {
        error = draw == kNoHandle ? "the scene has no vertices to frame"
                                  : "the selected group has no vertices to frame";
        return false;
    }
    const Vec3 centre{(minimum.x + maximum.x) * 0.5, (minimum.y + maximum.y) * 0.5,
                      (minimum.z + maximum.z) * 0.5};
    const double extent =
        std::max(maximum.x - minimum.x,
                 std::max(maximum.y - minimum.y, maximum.z - minimum.z));
    options_.camera.target.x =
        clamp_double(centre.x, -kMaxTargetMagnitude, kMaxTargetMagnitude);
    options_.camera.target.y =
        clamp_double(centre.y, -kMaxTargetMagnitude, kMaxTargetMagnitude);
    options_.camera.target.z =
        clamp_double(centre.z, -kMaxTargetMagnitude, kMaxTargetMagnitude);
    // A fixed multiple of the largest side, which keeps the whole box in
    // frame for every field of view this camera accepts without solving for
    // the projection. A degenerate box falls back to the smallest distance
    // that is not the near plane.
    options_.camera.distance =
        clamp_double(extent * 2.5, std::max(kMinDistance, options_.camera.near_plane * 4.0),
                     kMaxDistance);
    touch_frame();
    return true;
}

bool Inspector::command(InspectorCommand command, std::string& error) {
    if (scene_ == nullptr) {
        error = "the inspector has no scene";
        return false;
    }
    const auto snap = [&](double yaw, double pitch) {
        options_.camera.yaw_deg = yaw;
        options_.camera.pitch_deg = pitch;
        touch_frame();
    };
    // Prepared positions of the drawable groups, in prepared order, which
    // is the order the two selection walks step through.
    const auto drawable_positions = [&](std::vector<std::size_t>& out) {
        out.clear();
        for (std::size_t i = 0; i < scene_->draws.size(); ++i) {
            if (scene_->draws[i].drawable) {
                out.push_back(i);
            }
        }
    };
    switch (command) {
    case InspectorCommand::reset_camera:
        options_.camera = home_;
        touch_frame();
        return true;
    case InspectorCommand::view_front:
        snap(0.0, 0.0);
        return true;
    case InspectorCommand::view_rear:
        snap(180.0, 0.0);
        return true;
    case InspectorCommand::view_right:
        snap(90.0, 0.0);
        return true;
    case InspectorCommand::view_left:
        snap(-90.0, 0.0);
        return true;
    case InspectorCommand::view_top:
        snap(options_.camera.yaw_deg, kMaxPitch);
        return true;
    case InspectorCommand::frame_scene:
        return frame_bounds(kNoHandle, error);
    case InspectorCommand::frame_selection:
        if (selected_draw_ == kNoHandle) {
            error = "no material group is selected";
            return false;
        }
        return frame_bounds(selected_draw_, error);
    case InspectorCommand::clear_selection:
        clear_selection();
        return true;
    case InspectorCommand::select_next_draw:
    case InspectorCommand::select_previous_draw: {
        std::vector<std::size_t> positions;
        try {
            drawable_positions(positions);
        } catch (const std::bad_alloc&) {
            error = "not enough memory to walk the material groups";
            return false;
        }
        if (positions.empty()) {
            error = "the scene has no drawable material group";
            return false;
        }
        const bool forward = command == InspectorCommand::select_next_draw;
        const auto found =
            std::lower_bound(positions.begin(), positions.end(), selected_draw_);
        std::size_t at = static_cast<std::size_t>(found - positions.begin());
        if (selected_draw_ == kNoHandle || found == positions.end() ||
            *found != selected_draw_) {
            // Nothing selected, or a group that is not drawable: start at
            // the first drawable group in the chosen direction.
            at = forward ? 0 : positions.size() - 1;
        } else {
            at = forward ? (at + 1) % positions.size()
                         : (at + positions.size() - 1) % positions.size();
        }
        select_draw(positions[at]);
        return true;
    }
    case InspectorCommand::select_next_mesh:
    case InspectorCommand::select_previous_mesh: {
        if (scene_->meshes.empty()) {
            error = "the scene has no mesh";
            return false;
        }
        std::vector<std::size_t> positions;
        try {
            drawable_positions(positions);
        } catch (const std::bad_alloc&) {
            error = "not enough memory to walk the material groups";
            return false;
        }
        if (positions.empty()) {
            error = "the scene has no drawable material group";
            return false;
        }
        const bool forward = command == InspectorCommand::select_next_mesh;
        const std::size_t current = selected_mesh();
        // The first drawable group of the next mesh that has one, wrapping.
        const std::size_t count = scene_->meshes.size();
        std::size_t start = forward ? 0 : count - 1;
        if (current != kNoHandle) {
            start = forward ? (current + 1) % count : (current + count - 1) % count;
        }
        for (std::size_t step = 0; step < count; ++step) {
            const std::size_t mesh =
                forward ? (start + step) % count : (start + count - step) % count;
            for (const std::size_t position : positions) {
                if (scene_->draws[position].mesh == mesh) {
                    select_draw(position);
                    return true;
                }
            }
        }
        error = "no mesh has a drawable material group";
        return false;
    }
    case InspectorCommand::cycle_view:
        switch (view_) {
        case InspectorView::shaded:
            view_ = InspectorView::draws;
            break;
        case InspectorView::draws:
            view_ = InspectorView::meshes;
            break;
        case InspectorView::meshes:
            view_ = InspectorView::depth;
            break;
        case InspectorView::depth:
            view_ = InspectorView::shaded;
            break;
        }
        dirty_ = true;
        return true;
    case InspectorCommand::cycle_overlay:
        switch (overlay_) {
        case InspectorOverlay::off:
            overlay_ = InspectorOverlay::summary;
            break;
        case InspectorOverlay::summary:
            overlay_ = InspectorOverlay::detail;
            break;
        case InspectorOverlay::detail:
            overlay_ = InspectorOverlay::help;
            break;
        case InspectorOverlay::help:
            overlay_ = InspectorOverlay::off;
            break;
        }
        dirty_ = true;
        return true;
    case InspectorCommand::toggle_highlight:
        highlight_ = !highlight_;
        dirty_ = true;
        return true;
    case InspectorCommand::cycle_cull:
        options_.cull = options_.cull == CullMode::none
                            ? CullMode::back
                            : (options_.cull == CullMode::back ? CullMode::front
                                                               : CullMode::none);
        touch_frame();
        return true;
    case InspectorCommand::toggle_shading:
        options_.shading = options_.shading == ShadingMode::flat ? ShadingMode::none
                                                                 : ShadingMode::flat;
        touch_frame();
        return true;
    case InspectorCommand::toggle_material_state: {
        const MaterialStateMode wanted =
            options_.material_state == MaterialStateMode::off
                ? MaterialStateMode::retail
                : MaterialStateMode::off;
        if (wanted == MaterialStateMode::retail &&
            !validate_material_coefficients(options_.coefficients, error)) {
            return false;
        }
        const MaterialStateMode previous = options_.material_state;
        options_.material_state = wanted;
        // The submission order, and therefore every triangle id, depends on
        // the material state, so the id table is rebuilt before the next
        // frame is picked in. A failure leaves the previous state in place.
        if (!rebuild_order(error)) {
            options_.material_state = previous;
            std::string ignored;
            rebuild_order(ignored);
            return false;
        }
        // The ids the selection was made against are gone.
        selected_draw_ = kNoHandle;
        touch_frame();
        return true;
    }
    case InspectorCommand::toggle_vertex_color:
        options_.vertex_color = !options_.vertex_color;
        touch_frame();
        return true;
    case InspectorCommand::toggle_flip_v:
        options_.flip_v = !options_.flip_v;
        touch_frame();
        return true;
    }
    error = "unknown inspector command";
    return false;
}

bool Inspector::view_pixels(
    const InspectionBuffers& buffers,
    std::vector<unsigned char>& rgba,
    std::string& error) const {
    rgba.clear();
    if (buffers.ids == nullptr) {
        error = "the frame carries no triangle ids";
        return false;
    }
    if (buffers.width == 0 || buffers.height == 0) {
        error = "the frame has a zero side";
        return false;
    }
    const std::uint64_t pixels =
        std::uint64_t{buffers.width} * std::uint64_t{buffers.height};
    if (buffers.width > limits_.max_width || buffers.height > limits_.max_height ||
        pixels > limits_.max_pixels) {
        error = "the frame is outside the render limits";
        return false;
    }
    if (view_ == InspectorView::shaded && buffers.rgba == nullptr) {
        error = "the frame carries no color buffer";
        return false;
    }
    if (view_ == InspectorView::depth && buffers.depth == nullptr) {
        error = "the frame carries no depth buffer";
        return false;
    }
    try {
        rgba.assign(static_cast<std::size_t>(pixels) * 4U, 0);
    } catch (const std::bad_alloc&) {
        error = "not enough memory for the inspector view";
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(pixels);
    if (view_ == InspectorView::shaded) {
        std::copy(buffers.rgba, buffers.rgba + count * 4U, rgba.begin());
        return true;
    }
    // Everything else keeps the background where nothing was drawn, so the
    // silhouette of the car stays readable in every view.
    double near_depth = 0.0;
    double far_depth = 0.0;
    bool any_depth = false;
    if (view_ == InspectorView::depth) {
        for (std::size_t i = 0; i < count; ++i) {
            if (buffers.ids[i] == kNoTriangle) {
                continue;
            }
            const double value = buffers.depth[i];
            if (!std::isfinite(value)) {
                continue;
            }
            if (!any_depth) {
                near_depth = value;
                far_depth = value;
                any_depth = true;
            } else {
                near_depth = std::min(near_depth, value);
                far_depth = std::max(far_depth, value);
            }
        }
    }
    const double span = far_depth - near_depth;
    for (std::size_t i = 0; i < count; ++i) {
        unsigned char color[4] = {options_.background[0], options_.background[1],
                                  options_.background[2], options_.background[3]};
        const std::uint32_t id = buffers.ids[i];
        if (id != kNoTriangle) {
            const std::size_t draw = draw_of_triangle(id);
            switch (view_) {
            case InspectorView::draws:
                if (draw != kNoHandle) {
                    index_color(draw, color);
                }
                break;
            case InspectorView::meshes:
                if (draw != kNoHandle && draw < scene_->draws.size()) {
                    index_color(scene_->draws[draw].mesh, color);
                }
                break;
            case InspectorView::depth: {
                double shade = 1.0;
                const double value = buffers.depth[i];
                if (any_depth && span > 0.0 && std::isfinite(value)) {
                    shade = 1.0 - (value - near_depth) / span;
                }
                shade = clamp_double(shade, 0.0, 1.0);
                const unsigned char gray =
                    static_cast<unsigned char>(shade * 255.0 + 0.5);
                color[0] = gray;
                color[1] = gray;
                color[2] = gray;
                color[3] = 255;
                break;
            }
            case InspectorView::shaded:
                break;
            }
        }
        rgba[i * 4U + 0] = color[0];
        rgba[i * 4U + 1] = color[1];
        rgba[i * 4U + 2] = color[2];
        rgba[i * 4U + 3] = color[3];
    }
    return true;
}

bool Inspector::highlight_mask(
    const InspectionBuffers& buffers,
    std::vector<unsigned char>& mask,
    std::string& error) const {
    mask.clear();
    if (buffers.ids == nullptr) {
        error = "the frame carries no triangle ids";
        return false;
    }
    if (buffers.width == 0 || buffers.height == 0) {
        error = "the frame has a zero side";
        return false;
    }
    const std::uint64_t pixels =
        std::uint64_t{buffers.width} * std::uint64_t{buffers.height};
    if (buffers.width > limits_.max_width || buffers.height > limits_.max_height ||
        pixels > limits_.max_pixels) {
        error = "the frame is outside the render limits";
        return false;
    }
    try {
        mask.assign(static_cast<std::size_t>(pixels), 0);
    } catch (const std::bad_alloc&) {
        error = "not enough memory for the highlight mask";
        return false;
    }
    if (!highlight_ || selected_draw_ == kNoHandle) {
        return true;
    }
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const std::uint32_t id = buffers.ids[i];
        if (id != kNoTriangle && draw_of_triangle(id) == selected_draw_) {
            mask[i] = 1;
        }
    }
    return true;
}

void Inspector::build_overlay(
    const RenderStats& stats, std::vector<std::string>& lines) const {
    lines.clear();
    if (overlay_ == InspectorOverlay::off) {
        return;
    }
    const auto push = [&lines](std::string text) {
        if (text.size() > kMaxOverlayLineBytes) {
            text.resize(kMaxOverlayLineBytes);
        }
        lines.push_back(std::move(text));
    };
    if (overlay_ == InspectorOverlay::help) {
        push("drag left: orbit   drag right or middle: pan   wheel: zoom");
        push("click: select the material group under the pointer");
        push("1 front  2 right  3 rear  4 left  5 top  R reset camera");
        push("F frame selection   G frame scene   Esc clear selection");
        push("N / P next or previous group   M / B next or previous mesh");
        push("V cycle view   O cycle overlay   H toggle highlight");
        push("C cycle cull   S shading   T retail material state");
        push("X vertex color   U flip-v   Q or Alt+F4 close");
        return;
    }
    if (scene_ == nullptr) {
        push("no scene");
        return;
    }
    const OrbitCamera& camera = options_.camera;
    const Vec3 eye = orbit_eye(camera);
    push("view " + std::string(inspector_view_name(view_)) + " overlay " +
         inspector_overlay_name(overlay_) + " highlight " +
         (highlight_ ? "on" : "off") + " frame " + std::to_string(options_.width) +
         "x" + std::to_string(options_.height));
    push("camera yaw " + number(camera.yaw_deg) + " pitch " +
         number(camera.pitch_deg) + " distance " + number(camera.distance) +
         " target " + number(camera.target.x) + "," + number(camera.target.y) + "," +
         number(camera.target.z));
    push("camera fov_y " + number(camera.fov_y_deg) + " near " +
         number(camera.near_plane) + " far " + number(camera.far_plane) + " eye " +
         number(eye.x) + "," + number(eye.y) + "," + number(eye.z));
    push("options cull " + std::string(cull_mode_name(options_.cull)) + " shading " +
         shading_mode_name(options_.shading) + " material " +
         material_state_mode_name(options_.material_state) + " vertex_color " +
         (options_.vertex_color ? "1" : "0") + " flip_v " +
         (options_.flip_v ? "1" : "0") + " alpha_cutoff " +
         std::to_string(static_cast<unsigned int>(options_.alpha_cutoff)));
    push("scene meshes " + std::to_string(scene_->meshes.size()) + " groups " +
         std::to_string(scene_->draws.size()) + " textures " +
         std::to_string(scene_->textures.size()) + " unresolved_hashes " +
         std::to_string(scene_->unresolved_hashes.size()));
    push("groups resolved " + std::to_string(stats.groups_resolved) + " unresolved " +
         std::to_string(stats.groups_unresolved) + " unusable " +
         std::to_string(stats.groups_unusable) + " invalid " +
         std::to_string(stats.groups_invalid));
    push("triangles submitted " + std::to_string(stats.triangles_submitted) +
         " rasterized " + std::to_string(stats.triangles_rasterized) + " culled " +
         std::to_string(stats.triangles_culled) + " clipped " +
         std::to_string(stats.triangles_clipped));
    push("pixels drawn " + std::to_string(stats.pixels_drawn) + " fallback " +
         std::to_string(stats.pixels_fallback) + " blended " +
         std::to_string(stats.pixels_blended) + " fragments " +
         std::to_string(stats.fragments_written));
    if (selected_draw_ == kNoHandle || selected_draw_ >= scene_->draws.size()) {
        push("selection none");
        return;
    }
    const PreparedDraw& draw = scene_->draws[selected_draw_];
    const PreparedMesh& mesh = scene_->meshes[draw.mesh];
    push("selection group " + std::to_string(selected_draw_) + " of " +
         std::to_string(scene_->draws.size()) + " mesh " + std::to_string(draw.mesh) +
         " " + quoted(mesh.name) + " material " + std::to_string(draw.material_index) +
         " of " + std::to_string(mesh.material_count));
    push("selection texture " + hex32_text(draw.texture_hash) + " slot " +
         std::to_string(draw.texture_slot) + " " + binding_kind_name(draw.binding) +
         " triangles " + std::to_string(draw.index_count / 3U) + " drawable " +
         (draw.drawable ? "1" : "0"));
    if (overlay_ != InspectorOverlay::detail) {
        return;
    }
    push("mesh source_index " + std::to_string(mesh.source_index) + " directory " +
         hex32_text(mesh.directory_hash) + " offset " +
         std::to_string(mesh.directory_offset) + " size " +
         std::to_string(mesh.directory_size) + " entry " +
         (mesh.has_directory_entry ? "1" : "0") + " vertices " +
         std::to_string(mesh.vertices.size()) + " triangles " +
         std::to_string(mesh.triangles.size()));
    push("group first_index " + std::to_string(draw.first_index) + " index_count " +
         std::to_string(draw.index_count) + " key_slot " +
         hex32_text(draw.material_key_slot) + " flags " + hex32_text(draw.flags) +
         " record_offset " + std::to_string(draw.record_offset));
    push("group bounds min " + number(draw.bounds_min[0]) + "," +
         number(draw.bounds_min[1]) + "," + number(draw.bounds_min[2]) + " max " +
         number(draw.bounds_max[0]) + "," + number(draw.bounds_max[1]) + "," +
         number(draw.bounds_max[2]));
    if (draw.binding == BindingKind::resolved && draw.texture < scene_->textures.size()) {
        const RenderTexture& texture = scene_->textures[draw.texture];
        push("texture index " + std::to_string(draw.texture) + " " +
             std::to_string(texture.width) + "x" + std::to_string(texture.height) +
             " bytes " + std::to_string(texture.rgba.size()) + " source " +
             std::to_string(draw.texture_source) + " pack " +
             std::to_string(draw.texture_pack) + " record " +
             std::to_string(draw.texture_record));
        push("texture state alpha_test " + hex8_text(texture.alpha_test_mode) + " (" +
             (render_alpha_test_enabled(texture.alpha_test_mode) ? "on" : "off") +
             ") blend " + hex8_text(texture.alpha_blend_mode) + " (" +
             render_blend_mode_name(render_blend_mode_of(texture.alpha_blend_mode)) +
             ") layer_group " + hex8_text(texture.layer_group) + " layer_order " +
             hex8_text(texture.layer_order) + " row " +
             std::to_string(render_draw_layer(texture.layer_group, texture.layer_order)));
        const MaterialCoefficientGates gates = material_coefficient_gates(
            draw.flags, options_.coefficients.environment_enabled);
        push("material gates diffuse " + std::string(gates.diffuse ? "1" : "0") +
             " specular " + (gates.specular ? "1" : "0") + " environment " +
             (gates.environment ? "1" : "0") + " (applied only with the retail state)");
    } else {
        push("texture none: the group draws the fallback pattern for " +
             hex32_text(draw.texture_hash));
    }
    Vec3 minimum;
    Vec3 maximum;
    if (world_bounds(selected_draw_, minimum, maximum)) {
        push("drawn world bounds min " + number(minimum.x) + "," + number(minimum.y) +
             "," + number(minimum.z) + " max " + number(maximum.x) + "," +
             number(maximum.y) + "," + number(maximum.z));
    } else {
        push("drawn world bounds: the group draws no vertex of its mesh");
    }
}

}  // namespace nfsu
