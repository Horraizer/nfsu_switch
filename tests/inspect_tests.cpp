#include "nfsu/geometry.h"
#include "nfsu/inspect.h"
#include "nfsu/inspect_window.h"
#include "nfsu/render.h"
#include "nfsu/scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

// The scene below draws five quads side by side, each split into two
// single-triangle material groups with a texture of its own. No two groups
// overlap on screen and every texture is a distinct solid color, so the
// color of a drawn pixel names the group that drew it. That is what makes
// the id table testable against the renderer instead of against itself.
constexpr std::size_t kMeshCount = 5;
constexpr std::size_t kDrawCount = kMeshCount * 2;

nfsu::MeshVertex vertex(float px, float py, float pz, float u, float v) {
    nfsu::MeshVertex out;
    out.px = px;
    out.py = py;
    out.pz = pz;
    out.u = u;
    out.v = v;
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
    return out;
}

void store_u32(unsigned char* out, std::uint32_t value) {
    out[0] = static_cast<unsigned char>(value);
    out[1] = static_cast<unsigned char>(value >> 8U);
    out[2] = static_cast<unsigned char>(value >> 16U);
    out[3] = static_cast<unsigned char>(value >> 24U);
}

void store_float(unsigned char* out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    store_u32(out, bits);
}

// One material record, with a flags value whose environment branch bit is
// clear: the retail coefficient gates then leave the diffuse lane open and
// add no environment term, so a fragment keeps its texel exactly.
nfsu::MeshMaterial material(std::uint32_t first, std::uint32_t slot,
                            unsigned char tag) {
    nfsu::MeshMaterial out;
    out.first_index = first;
    out.index_count = 3;
    out.texture_slot = slot;
    out.material_key_slot = 0x200U + tag;
    out.flags = 0x4000U + tag;
    out.offset = 0x3000U + tag * 0x40U;
    for (int k = 0; k < 3; ++k) {
        out.bounds_min[k] = -1.0F - static_cast<float>(k);
        out.bounds_max[k] = 1.0F + static_cast<float>(k);
    }
    for (std::size_t k = 0; k < nfsu::kMaterialRecordSize; ++k) {
        out.raw[k] = static_cast<unsigned char>(tag * 11U + k);
    }
    for (std::size_t k = 0; k < 3; ++k) {
        store_float(out.raw + k * 4U, out.bounds_min[k]);
        store_float(out.raw + 16U + k * 4U, out.bounds_max[k]);
    }
    store_u32(out.raw + 12, out.index_count);
    store_u32(out.raw + 28, out.texture_slot);
    store_u32(out.raw + 32, out.material_key_slot);
    store_u32(out.raw + 52, out.first_index);
    store_u32(out.raw + 56, out.flags);
    return out;
}

std::uint32_t texture_hash_of(std::size_t mesh, std::size_t slot) {
    return 0x51000000U + static_cast<std::uint32_t>(mesh) * 0x100U +
           static_cast<std::uint32_t>(slot);
}

// The last mesh is named with the bytes a file can actually hold rather
// than with an identifier: a mesh name is raw bytes, so it may carry a NUL,
// a control byte, a quote, a backslash and anything above 0x7E, and none of
// those may reach the overlay unescaped. It is also long enough that the
// overlay line quoting it passes kMaxOverlayLineBytes, which is the only
// way the truncation is exercised at all. No file holds a name this long;
// the point is to test the guarantee past the bound rather than up to it.
std::string hostile_mesh_name() {
    std::string name = "RAW";
    for (int i = 0; i < 12; ++i) {
        name += static_cast<char>(0x00);
        name += static_cast<char>(0x01 + i);
        name += static_cast<char>(0x7F);
        name += static_cast<char>(0x80 + i);
        name += static_cast<char>(0xFF);
        name += '"';
        name += '\\';
        name += static_cast<char>('a' + i);
    }
    return name;
}

// Quads at x = 0, one per mesh, spanning a distinct pz band.
nfsu::GeometryReport inspect_geometry() {
    nfsu::GeometryReport report;
    for (std::size_t i = 0; i < kMeshCount; ++i) {
        const float z0 = static_cast<float>(i) * 1.2F - 3.0F;
        const float z1 = z0 + 0.8F;
        nfsu::Mesh mesh;
        mesh.index = i;
        mesh.name = i + 1 == kMeshCount ? hostile_mesh_name()
                                        : "QUAD" + std::to_string(i);
        mesh.valid = true;
        mesh.vertices = {vertex(0.0F, 0.0F, z0, 0.0F, 0.0F),
                         vertex(0.0F, 1.0F, z0, 0.0F, 1.0F),
                         vertex(0.0F, 1.0F, z1, 1.0F, 1.0F),
                         vertex(0.0F, 0.0F, z1, 1.0F, 0.0F)};
        mesh.triangles = {{0, 1, 2}, {0, 2, 3}};
        mesh.texture_hashes = {texture_hash_of(i, 0), texture_hash_of(i, 1)};
        mesh.materials = {material(0, 0, static_cast<unsigned char>(i * 2U)),
                          material(3, 1, static_cast<unsigned char>(i * 2U + 1U))};
        mesh.directory_hash = 0x77000000U + static_cast<std::uint32_t>(i);
        mesh.directory_offset = 0x400U + i * 0x100U;
        mesh.directory_size = 0xC0U;
        mesh.has_directory_entry = true;
        report.meshes.push_back(mesh);
    }
    report.mesh_directory_valid = true;
    return report;
}

// A 1x1 opaque texture. Its color is the group's identity in the frame.
nfsu::RenderTexture solid(std::uint32_t hash, unsigned char r, unsigned char g,
                          unsigned char b, std::uint8_t layer_order,
                          std::uint8_t layer_group) {
    nfsu::RenderTexture out;
    out.hash = hash;
    out.width = 1;
    out.height = 1;
    out.valid = true;
    out.rgba = {r, g, b, 255};
    out.layer_order = layer_order;
    out.layer_group = layer_group;
    return out;
}

nfsu::RenderTextureSet inspect_textures() {
    nfsu::RenderTextureSet set;
    for (std::size_t i = 0; i < kMeshCount; ++i) {
        for (std::size_t slot = 0; slot < 2; ++slot) {
            const std::size_t draw = i * 2U + slot;
            // Distinct, far apart and never equal to the background.
            const unsigned char r = static_cast<unsigned char>(20U + draw * 20U);
            const unsigned char g = static_cast<unsigned char>(200U - draw * 15U);
            const unsigned char b = static_cast<unsigned char>(90U + draw * 7U);
            // Rows descend with the prepared order, so the retail queue walks
            // the groups back to front and its order is not the prepared one.
            const std::uint8_t row =
                static_cast<std::uint8_t>(7U - static_cast<std::uint8_t>(draw) / 2U);
            set.textures.push_back(
                solid(texture_hash_of(i, slot), r, g, b, row, 0));
        }
    }
    return set;
}

nfsu::RenderOptions inspect_options() {
    nfsu::RenderOptions options;
    options.width = 240;
    options.height = 120;
    // Straight at the quads from +X, with no shading and no culling, so a
    // drawn pixel is exactly the texel of its group.
    options.camera.yaw_deg = 0.0;
    options.camera.pitch_deg = 0.0;
    options.camera.distance = 8.0;
    options.camera.target = nfsu::Vec3{0.0, 0.5, 0.0};
    options.camera.fov_y_deg = 45.0;
    options.shading = nfsu::ShadingMode::none;
    options.cull = nfsu::CullMode::none;
    return options;
}

bool build_scene(nfsu::PreparedCarScene& scene) {
    std::string error;
    const bool ok = nfsu::prepare_car_scene(
        inspect_geometry(), inspect_textures(), nfsu::MeshSelection{},
        nfsu::SceneLimits{}, scene, error);
    if (!ok) {
        std::cerr << "test setup failed: " << error << '\n';
    }
    return ok;
}

// The prepared position of the group whose texture holds this color, or
// kNoHandle.
std::size_t draw_of_color(const nfsu::PreparedCarScene& scene,
                          const unsigned char* pixel) {
    for (std::size_t i = 0; i < scene.draws.size(); ++i) {
        const nfsu::PreparedDraw& draw = scene.draws[i];
        if (draw.binding != nfsu::BindingKind::resolved) {
            continue;
        }
        const nfsu::RenderTexture& texture = scene.textures[draw.texture];
        if (texture.rgba.size() >= 3 && texture.rgba[0] == pixel[0] &&
            texture.rgba[1] == pixel[1] && texture.rgba[2] == pixel[2]) {
            return i;
        }
    }
    return nfsu::kNoHandle;
}

bool render(const nfsu::PreparedCarScene& scene, const nfsu::RenderOptions& options,
            nfsu::Frame& frame, nfsu::RenderStats& stats) {
    std::string error;
    if (!nfsu::render_scene(scene, options, nfsu::RenderLimits{}, frame, stats,
                            error)) {
        std::cerr << "test render failed: " << error << '\n';
        return false;
    }
    return true;
}

bool open_inspector(nfsu::Inspector& inspector, const nfsu::PreparedCarScene& scene,
                    const nfsu::RenderOptions& options) {
    std::string error;
    if (!inspector.reset(scene, options, nfsu::RenderLimits{}, error)) {
        std::cerr << "test inspector reset failed: " << error << '\n';
        return false;
    }
    return true;
}

void test_order_and_ranges() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    check(scene.draws.size() == kDrawCount, "the fixture prepares ten groups");
    std::string error;
    std::vector<std::uint32_t> plain;
    std::vector<unsigned char> layers;
    check(nfsu::prepared_draw_order(scene, false, plain, &layers, error),
          "the prepared order is produced");
    bool identity = plain.size() == scene.draws.size();
    for (std::size_t i = 0; identity && i < plain.size(); ++i) {
        identity = plain[i] == i;
    }
    check(identity, "without the retail state the order is the prepared one");
    bool rows_zero = layers.size() == scene.draws.size();
    for (const unsigned char row : layers) {
        rows_zero = rows_zero && row == 0;
    }
    check(rows_zero, "without the retail state no draw queue row is assigned");

    std::vector<std::uint32_t> retail;
    check(nfsu::prepared_draw_order(scene, true, retail, &layers, error),
          "the retail order is produced");
    check(retail.size() == scene.draws.size(), "the retail order covers every group");
    check(retail != plain, "the fixture's rows make the retail order differ");
    std::set<std::uint32_t> seen(retail.begin(), retail.end());
    check(seen.size() == retail.size(), "the retail order is a permutation");
    // Rows never descend along the retail order, which is the whole point of
    // the queue: it drains the table by ascending row.
    bool ascending = true;
    for (std::size_t i = 1; i < retail.size(); ++i) {
        ascending = ascending && layers[retail[i - 1]] <= layers[retail[i]];
    }
    check(ascending, "the retail order walks the draw queue rows in ascending order");

    std::vector<nfsu::PreparedTriangleRange> ranges;
    check(nfsu::prepared_triangle_ranges(scene, retail, ranges, error),
          "the triangle ranges are produced");
    check(ranges.size() == kDrawCount, "every drawable group gets a range");
    std::uint32_t next = 0;
    bool contiguous = true;
    for (const nfsu::PreparedTriangleRange& range : ranges) {
        contiguous = contiguous && range.first_id == next && range.count == 1;
        next += range.count;
    }
    check(contiguous, "the ranges are contiguous from zero");
    check(next == nfsu::prepared_draw_triangles(scene),
          "the ranges cover every submitted triangle");
    for (const nfsu::PreparedTriangleRange& range : ranges) {
        check(nfsu::find_prepared_triangle(ranges, range.first_id) == range.draw,
              "a range's first id finds its group");
        check(nfsu::find_prepared_triangle(ranges,
                                           range.first_id + range.count - 1) ==
                  range.draw,
              "a range's last id finds its group");
    }
    check(nfsu::find_prepared_triangle(ranges, next) == nfsu::kNoHandle,
          "an id past the last range finds nothing");
    check(nfsu::find_prepared_triangle(ranges, nfsu::kNoTriangle) == nfsu::kNoHandle,
          "the no-triangle id finds nothing");

    std::vector<nfsu::PreparedTriangleRange> rejected;
    std::vector<std::uint32_t> broken = retail;
    broken.pop_back();
    check(!nfsu::prepared_triangle_ranges(scene, broken, rejected, error) &&
              rejected.empty(),
          "an order of the wrong size is refused");
    broken = retail;
    broken[0] = broken[1];
    check(!nfsu::prepared_triangle_ranges(scene, broken, rejected, error) &&
              rejected.empty(),
          "an order that repeats a group is refused");
    broken = retail;
    broken[0] = static_cast<std::uint32_t>(scene.draws.size());
    check(!nfsu::prepared_triangle_ranges(scene, broken, rejected, error) &&
              rejected.empty(),
          "an order naming a group that does not exist is refused");
}

// One hand-built group bound to one of the two textures below.
nfsu::PreparedDraw ordered_draw(const nfsu::PreparedCarScene& scene,
                                std::size_t material_index, std::size_t texture) {
    nfsu::PreparedDraw draw;
    draw.mesh = 0;
    draw.material_index = material_index;
    draw.first_index = static_cast<std::uint32_t>(material_index) * 3U;
    draw.index_count = 3;
    draw.binding = nfsu::BindingKind::resolved;
    draw.texture = texture;
    draw.texture_hash = scene.textures[texture].hash;
    draw.drawable = true;
    return draw;
}

// The retail order keeps the record order of the groups that a comparison
// cannot separate, which is what "inside one texture the material records
// keep their order" means. The fixtures that cover the rule through the
// renderer hold three groups, and libstdc++ sorts a range that short with
// insertion sort whether the call asked for a stable sort or not, so an
// unstable sort passes them unchanged. This scene is past that threshold,
// where the two sorts genuinely differ, and it calls prepared_draw_order
// directly because the inspector and a future deko3d backend take their
// submission order from it and number every triangle by it.
void test_draw_order_stability() {
    nfsu::PreparedCarScene scene;
    scene.source_mesh_count = 1;
    scene.meshes.emplace_back();
    scene.meshes.back().name = "ORDER";
    // Two textures whose raw bytes put them in different draw queue rows:
    // layer_order counts as a signed byte and layer_group adds eight when
    // it is not zero, so these are rows 3 and 1.
    for (const std::uint8_t row : {std::uint8_t{3}, std::uint8_t{1}}) {
        nfsu::RenderTexture texture;
        texture.hash = 0x0A0000FFU - row;
        texture.width = 1;
        texture.height = 1;
        texture.valid = true;
        texture.rgba = {row, row, row, 255};
        texture.layer_order = row;
        texture.layer_group = 0;
        scene.textures.push_back(texture);
    }
    // Comfortably past the sixteen elements below which introsort never
    // reaches its unstable path.
    constexpr std::size_t kGroups = 48;
    for (std::size_t i = 0; i < kGroups; ++i) {
        scene.draws.push_back(ordered_draw(scene, i, i < kGroups / 2 ? 0 : 1));
    }
    std::string error;
    std::vector<std::uint32_t> order;
    std::vector<unsigned char> layers;
    check(nfsu::prepared_draw_order(scene, true, order, &layers, error),
          "the retail order of a large scene is produced: " + error);
    // Row 1 drains before row 3, and inside each row the records keep the
    // order they were prepared in.
    std::vector<std::uint32_t> expected;
    for (std::size_t i = kGroups / 2; i < kGroups; ++i) {
        expected.push_back(static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0; i < kGroups / 2; ++i) {
        expected.push_back(static_cast<std::uint32_t>(i));
    }
    check(order == expected,
          "the rows drain in order and the records inside a row keep theirs");

    // Every group on one texture in one row, so no comparison can decide
    // anything at all and the order must come back untouched.
    scene.draws.clear();
    for (std::size_t i = 0; i < kGroups; ++i) {
        scene.draws.push_back(ordered_draw(scene, i, 0));
    }
    check(nfsu::prepared_draw_order(scene, true, order, &layers, error),
          "the retail order of a single-row scene is produced: " + error);
    bool identity = order.size() == kGroups;
    for (std::size_t i = 0; identity && i < order.size(); ++i) {
        identity = order[i] == static_cast<std::uint32_t>(i);
    }
    check(identity, "groups a comparison cannot separate keep the prepared order");
}

// The heart of the contract: the id the renderer wrote at a pixel names the
// group that drew that pixel, in both material states.
void test_ids_match_the_renderer() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    const nfsu::MaterialStateMode modes[2] = {nfsu::MaterialStateMode::off,
                                              nfsu::MaterialStateMode::retail};
    for (const nfsu::MaterialStateMode mode : modes) {
        const std::string label =
            std::string(nfsu::material_state_mode_name(mode)) + ": ";
        nfsu::RenderOptions options = inspect_options();
        options.material_state = mode;
        nfsu::Frame frame;
        nfsu::RenderStats stats;
        if (!render(scene, options, frame, stats)) {
            ++g_failures;
            continue;
        }
        nfsu::Inspector inspector;
        if (!open_inspector(inspector, scene, options)) {
            ++g_failures;
            continue;
        }
        check(stats.pixels_drawn > 1000, label + "the fixture draws a real frame");
        std::set<std::size_t> found;
        std::size_t drawn = 0;
        std::size_t agreed = 0;
        for (std::size_t i = 0; i < frame.ids().size(); ++i) {
            const std::uint32_t id = frame.ids()[i];
            if (id == nfsu::kNoTriangle) {
                continue;
            }
            ++drawn;
            const std::size_t by_color = draw_of_color(scene, &frame.rgba()[i * 4U]);
            const std::size_t by_id = inspector.draw_of_triangle(id);
            if (by_color == by_id && by_id != nfsu::kNoHandle) {
                ++agreed;
                found.insert(by_id);
            }
        }
        check(drawn > 0 && agreed == drawn,
              label + "every drawn pixel's id names the group whose texture is there");
        check(found.size() == kDrawCount,
              label + "every group contributes pixels the id table resolves");
    }
}

void test_camera_input() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    nfsu::Inspector inspector;
    if (!open_inspector(inspector, scene, inspect_options())) {
        ++g_failures;
        return;
    }
    const double scale = inspector.sensitivity().orbit_deg_per_pixel;
    const double yaw0 = inspector.options().camera.yaw_deg;
    const double pitch0 = inspector.options().camera.pitch_deg;
    inspector.orbit(10.0, 0.0);
    check(inspector.options().camera.yaw_deg == yaw0 - 10.0 * scale,
          "dragging right lowers yaw, so the car follows the pointer");
    inspector.orbit(-10.0, 0.0);
    check(inspector.options().camera.yaw_deg == yaw0, "the orbit is reversible");
    inspector.orbit(0.0, 10.0);
    check(inspector.options().camera.pitch_deg == pitch0 + 10.0 * scale,
          "dragging down raises pitch, so the roof comes into view");

    // Clamps and the wrap, from a hostile sequence.
    std::string error;
    for (int i = 0; i < 200; ++i) {
        inspector.orbit(1000.0, 1000.0);
        inspector.orbit(-3000.0, -50.0);
        inspector.pan(500.0, -700.0);
        inspector.zoom(7);
        inspector.zoom(-9);
        check(nfsu::validate_camera(inspector.options().camera, error),
              "the camera stays valid under hostile input");
        // Half open, as include/nfsu/inspect.h documents it: 180 is the
        // same heading as -180 and only one of them may be produced. A wrap
        // that saturated there instead of wrapping would sit on the closed
        // end and stop the camera turning, which is why the upper bound is
        // strict.
        check(inspector.options().camera.yaw_deg >= -180.0 &&
                  inspector.options().camera.yaw_deg < 180.0,
              "yaw stays wrapped");
    }
    // The wrap is a wrap, not a saturation: turning one way for a whole
    // revolution passes 180, comes back at -180 and arrives where it
    // started. At one degree per pixel every step below is exact.
    nfsu::Inspector turner;
    if (!open_inspector(turner, scene, inspect_options())) {
        ++g_failures;
        return;
    }
    nfsu::InspectorSensitivity one_degree = turner.sensitivity();
    one_degree.orbit_deg_per_pixel = 1.0;
    check(turner.set_sensitivity(one_degree, error), "one degree per pixel is set");
    check(turner.command(nfsu::InspectorCommand::view_front, error) &&
              turner.options().camera.yaw_deg == 0.0,
          "the turn starts at the front view");
    for (int i = 0; i < 179; ++i) {
        turner.orbit(-1.0, 0.0);
    }
    check(turner.options().camera.yaw_deg == 179.0,
          "179 degrees of turning is 179 degrees of yaw");
    turner.orbit(-1.0, 0.0);
    check(turner.options().camera.yaw_deg == -180.0,
          "the 180th degree wraps to the other end rather than stopping there");
    for (int i = 0; i < 180; ++i) {
        turner.orbit(-1.0, 0.0);
    }
    check(turner.options().camera.yaw_deg == 0.0,
          "a whole revolution of turning arrives back at the front view");

    inspector.orbit(1e18, 1e18);
    check(nfsu::validate_camera(inspector.options().camera, error),
          "an enormous delta is clamped, not applied");
    const double not_a_number = std::numeric_limits<double>::quiet_NaN();
    inspector.orbit(not_a_number, not_a_number);
    check(nfsu::validate_camera(inspector.options().camera, error),
          "a not-a-number delta leaves a valid camera");

    nfsu::Inspector fresh;
    if (!open_inspector(fresh, scene, inspect_options())) {
        ++g_failures;
        return;
    }
    const double distance = fresh.options().camera.distance;
    fresh.zoom(1);
    check(fresh.options().camera.distance < distance, "a positive step zooms in");
    fresh.zoom(-1);
    check(fresh.options().camera.distance == distance,
          "one step out undoes one step in exactly");
    fresh.zoom(100000);
    check(fresh.options().camera.distance == nfsu::kMinDistance,
          "zooming in stops at the smallest distance");
    fresh.zoom(-100000);
    check(fresh.options().camera.distance == nfsu::kMaxDistance,
          "zooming out stops at the largest distance");

    // A pan moves the target across the view, never along it, and by the
    // pixels the pointer crossed the window, not by the pixels of the frame
    // that was rendered for it. The window renders a drag at a fraction of
    // the client area, so the two are only the same outside a drag: the
    // case below is the one a default configuration is always in while a
    // pan is happening.
    nfsu::Inspector panner;
    if (!open_inspector(panner, scene, inspect_options())) {
        ++g_failures;
        return;
    }
    const int client_width = static_cast<int>(inspect_options().width);
    const int client_height = static_cast<int>(inspect_options().height);
    check(panner.set_viewport(static_cast<std::uint32_t>(client_width) / 2U,
                              static_cast<std::uint32_t>(client_height) / 2U, error),
          "a drag renders at half the client area");
    const nfsu::Vec3 before = panner.options().camera.target;
    nfsu::CameraFrame camera_frame;
    check(nfsu::make_camera_frame(panner.options().camera,
                                  static_cast<std::uint32_t>(client_width),
                                  static_cast<std::uint32_t>(client_height),
                                  camera_frame, error),
          "the fixture camera projects");
    const nfsu::PanDelta delta =
        nfsu::pan_delta_for(client_width, client_height, panner.options().width,
                            panner.options().height, 30.0, 12.0);
    panner.pan(delta.dx, delta.dy);
    const nfsu::Vec3 after = panner.options().camera.target;
    const nfsu::Vec3 moved{after.x - before.x, after.y - before.y, after.z - before.z};
    const double along = moved.x * camera_frame.forward.x +
                         moved.y * camera_frame.forward.y +
                         moved.z * camera_frame.forward.z;
    const double sideways = moved.x * camera_frame.right.x +
                            moved.y * camera_frame.right.y +
                            moved.z * camera_frame.right.z;
    const double upward = moved.x * camera_frame.up.x + moved.y * camera_frame.up.y +
                          moved.z * camera_frame.up.z;
    check(along > -1e-9 && along < 1e-9, "a pan does not move along the view");
    check(sideways < 0.0, "dragging right moves the target left, so the car follows");
    check(upward > 0.0, "dragging down moves the target up, so the car follows");
    // One pixel of drag is one pixel of world at the target's depth, in the
    // pixels of the window. Measuring that against the rendered height
    // instead would be measuring pan against itself and would hold whatever
    // the frame size was.
    const double expected = 30.0 * 2.0 * panner.options().camera.distance /
                            (camera_frame.focal_y *
                             static_cast<double>(client_height));
    const double difference = -sideways - expected;
    check(difference > -1e-9 && difference < 1e-9,
          "a pan moves the target by exactly the pixels dragged");
}

// The world distance one drag moves the target, with the frame rendered at
// 1 / divisor of a window of this size, which is what the window does for
// the length of a drag.
double pan_distance_at_divisor(const nfsu::PreparedCarScene& scene, int client_width,
                               int client_height, std::uint32_t divisor, double dx,
                               double dy) {
    nfsu::Inspector inspector;
    if (!open_inspector(inspector, scene, inspect_options())) {
        return -1.0;
    }
    std::string error;
    if (!inspector.set_viewport(static_cast<std::uint32_t>(client_width) / divisor,
                                static_cast<std::uint32_t>(client_height) / divisor,
                                error)) {
        std::cerr << "test setup failed: " << error << '\n';
        return -1.0;
    }
    const nfsu::Vec3 before = inspector.options().camera.target;
    const nfsu::PanDelta delta =
        nfsu::pan_delta_for(client_width, client_height, inspector.options().width,
                            inspector.options().height, dx, dy);
    inspector.pan(delta.dx, delta.dy);
    const nfsu::Vec3 after = inspector.options().camera.target;
    const double mx = after.x - before.x;
    const double my = after.y - before.y;
    const double mz = after.z - before.z;
    return std::sqrt(mx * mx + my * my + mz * mz);
}

// The drag divisor is a rendering decision and must not be a control
// decision: the same drag over the same window has to move the target the
// same world distance whatever fraction of the window the frame is drawn
// at. Nothing else pins this, and the default configuration never renders a
// pan at full size, so without it the shipped default is the broken one.
void test_pan_is_divisor_invariant() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    // A window every divisor below divides exactly, so the comparison is
    // not softened by the integer division the window does.
    const int client_width = 1000;
    const int client_height = 600;
    const double one = pan_distance_at_divisor(scene, client_width, client_height, 1,
                                               100.0, 0.0);
    const double two = pan_distance_at_divisor(scene, client_width, client_height, 2,
                                               100.0, 0.0);
    const double four = pan_distance_at_divisor(scene, client_width, client_height, 4,
                                                100.0, 0.0);
    check(one > 0.0 && two > 0.0 && four > 0.0, "every drag moves the target");
    if (!(one > 0.0)) {
        return;
    }
    check(two / one > 1.0 - 1e-9 && two / one < 1.0 + 1e-9,
          "a drag at divisor 2 moves the target as far as at divisor 1");
    check(four / one > 1.0 - 1e-9 && four / one < 1.0 + 1e-9,
          "a drag at divisor 4 moves the target as far as at divisor 1");

    // And the distance they all agree on is the documented one, so three
    // wrong answers that agree would not pass either.
    nfsu::Inspector reference;
    if (!open_inspector(reference, scene, inspect_options())) {
        ++g_failures;
        return;
    }
    std::string error;
    nfsu::CameraFrame camera_frame;
    check(nfsu::make_camera_frame(reference.options().camera,
                                  static_cast<std::uint32_t>(client_width),
                                  static_cast<std::uint32_t>(client_height),
                                  camera_frame, error),
          "the reference camera projects");
    const double expected = 100.0 * 2.0 * reference.options().camera.distance /
                            (camera_frame.focal_y * static_cast<double>(client_height));
    check(one - expected > -1e-9 && one - expected < 1e-9,
          "one client pixel of drag is one client pixel of world at the target");

    // An orbit is measured in degrees per pixel, which no viewport enters,
    // so it must not be scaled and must not change with the divisor.
    const auto yaw_after_orbit = [&](std::uint32_t divisor) {
        nfsu::Inspector inspector;
        if (!open_inspector(inspector, scene, inspect_options())) {
            return 1e18;
        }
        std::string ignored;
        if (!inspector.set_viewport(static_cast<std::uint32_t>(client_width) / divisor,
                                    static_cast<std::uint32_t>(client_height) / divisor,
                                    ignored)) {
            return 1e18;
        }
        inspector.orbit(100.0, 0.0);
        return inspector.options().camera.yaw_deg;
    };
    check(yaw_after_orbit(1) == yaw_after_orbit(2) &&
              yaw_after_orbit(1) == yaw_after_orbit(4),
          "an orbit turns the same amount whatever the frame is rendered at");
}

void test_commands() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    nfsu::Inspector inspector;
    if (!open_inspector(inspector, scene, inspect_options())) {
        ++g_failures;
        return;
    }
    std::string error;
    check(inspector.command(nfsu::InspectorCommand::view_right, error) &&
              inspector.options().camera.yaw_deg == 90.0 &&
              inspector.options().camera.pitch_deg == 0.0,
          "the right view snaps the camera");
    check(inspector.command(nfsu::InspectorCommand::reset_camera, error) &&
              inspector.options().camera.yaw_deg == inspect_options().camera.yaw_deg,
          "reset returns to the camera the session started from");

    // Group selection walks every drawable group once and wraps.
    check(inspector.selected_draw() == nfsu::kNoHandle, "nothing starts selected");
    check(!inspector.command(nfsu::InspectorCommand::frame_selection, error) &&
              !error.empty(),
          "framing a selection that does not exist is refused");
    std::vector<std::size_t> walk;
    for (std::size_t i = 0; i < kDrawCount; ++i) {
        check(inspector.command(nfsu::InspectorCommand::select_next_draw, error),
              "the next group is selected");
        walk.push_back(inspector.selected_draw());
    }
    std::set<std::size_t> distinct(walk.begin(), walk.end());
    check(distinct.size() == kDrawCount && walk.front() == 0,
          "the walk visits every drawable group once, starting at the first");
    check(inspector.command(nfsu::InspectorCommand::select_next_draw, error) &&
              inspector.selected_draw() == 0,
          "the walk wraps");
    check(inspector.command(nfsu::InspectorCommand::select_previous_draw, error) &&
              inspector.selected_draw() == kDrawCount - 1,
          "the walk runs backwards too");
    check(inspector.command(nfsu::InspectorCommand::clear_selection, error) &&
              inspector.selected_draw() == nfsu::kNoHandle,
          "the selection clears");

    // Mesh selection lands on the first group of each mesh in turn.
    check(inspector.command(nfsu::InspectorCommand::select_next_mesh, error) &&
              inspector.selected_mesh() == 0,
          "the first mesh is selected");
    check(inspector.command(nfsu::InspectorCommand::select_next_mesh, error) &&
              inspector.selected_mesh() == 1,
          "the next mesh is selected");
    check(inspector.command(nfsu::InspectorCommand::select_previous_mesh, error) &&
              inspector.selected_mesh() == 0,
          "the previous mesh is selected");
    check(inspector.command(nfsu::InspectorCommand::select_previous_mesh, error) &&
              inspector.selected_mesh() == kMeshCount - 1,
          "the mesh walk wraps backwards");

    // Framing puts the target on the selection and keeps the camera valid.
    check(inspector.command(nfsu::InspectorCommand::frame_selection, error),
          "the selection can be framed");
    check(nfsu::validate_camera(inspector.options().camera, error),
          "framing leaves a valid camera");
    const nfsu::Vec3 framed = inspector.options().camera.target;
    check(inspector.command(nfsu::InspectorCommand::frame_scene, error),
          "the scene can be framed");
    check(nfsu::validate_camera(inspector.options().camera, error),
          "framing the scene leaves a valid camera");
    check(inspector.options().camera.target.z != framed.z,
          "framing the scene is not framing one group");

    // Option toggles, each of which the window shows and the renderer reads.
    check(inspector.command(nfsu::InspectorCommand::cycle_cull, error) &&
              inspector.options().cull == nfsu::CullMode::back,
          "cull cycles to back");
    check(inspector.command(nfsu::InspectorCommand::cycle_cull, error) &&
              inspector.options().cull == nfsu::CullMode::front,
          "cull cycles to front");
    check(inspector.command(nfsu::InspectorCommand::cycle_cull, error) &&
              inspector.options().cull == nfsu::CullMode::none,
          "cull cycles back to none");
    check(inspector.command(nfsu::InspectorCommand::toggle_vertex_color, error) &&
              inspector.options().vertex_color,
          "vertex color toggles on");
    check(inspector.command(nfsu::InspectorCommand::toggle_flip_v, error) &&
              inspector.options().flip_v,
          "flip-v toggles on");
    check(inspector.command(nfsu::InspectorCommand::toggle_shading, error) &&
              inspector.options().shading == nfsu::ShadingMode::flat,
          "shading toggles to flat");
    check(inspector.command(nfsu::InspectorCommand::cycle_view, error) &&
              inspector.view() == nfsu::InspectorView::draws,
          "the view cycles");
    check(inspector.command(nfsu::InspectorCommand::cycle_overlay, error) &&
              inspector.overlay() == nfsu::InspectorOverlay::detail,
          "the overlay cycles");
    const bool highlight = inspector.highlight();
    check(inspector.command(nfsu::InspectorCommand::toggle_highlight, error) &&
              inspector.highlight() != highlight,
          "the highlight toggles");

    // The material state changes the submission order, so the id table has
    // to be rebuilt and the selection made against the old ids dropped.
    check(inspector.command(nfsu::InspectorCommand::select_next_draw, error),
          "a group is selected before the material state changes");
    const std::vector<std::uint32_t> before = inspector.draw_order();
    check(inspector.command(nfsu::InspectorCommand::toggle_material_state, error) &&
              inspector.options().material_state == nfsu::MaterialStateMode::retail,
          "the retail material state is selected");
    check(inspector.draw_order() != before,
          "the retail state rebuilds the submission order");
    check(inspector.selected_draw() == nfsu::kNoHandle,
          "the selection is dropped with the ids it was made against");
    check(inspector.triangle_ranges().size() == kDrawCount,
          "the triangle ranges follow the new order");
}

void test_pick_and_highlight() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    const nfsu::RenderOptions options = inspect_options();
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    if (!render(scene, options, frame, stats)) {
        ++g_failures;
        return;
    }
    nfsu::Inspector inspector;
    if (!open_inspector(inspector, scene, options)) {
        ++g_failures;
        return;
    }
    const nfsu::InspectionBuffers buffers = nfsu::inspection_buffers_of(frame);
    std::string error;
    check(!inspector.pick(buffers, frame.width(), 0, error) && !error.empty(),
          "a pixel outside the frame is refused");

    // Every drawn pixel selects the group whose texture is there, and a
    // background pixel clears the selection.
    std::size_t checked = 0;
    std::size_t background = 0;
    for (std::uint32_t y = 0; y < frame.height(); y += 7) {
        for (std::uint32_t x = 0; x < frame.width(); x += 5) {
            const std::size_t index = std::size_t{y} * frame.width() + x;
            const bool drawn = frame.ids()[index] != nfsu::kNoTriangle;
            if (!inspector.pick(buffers, x, y, error)) {
                check(false, "picking inside the frame succeeds");
                return;
            }
            if (drawn) {
                const std::size_t expected =
                    draw_of_color(scene, &frame.rgba()[index * 4U]);
                if (inspector.selected_draw() != expected) {
                    check(false, "a click selects the group under the pointer");
                    return;
                }
                ++checked;
            } else {
                if (inspector.selected_draw() != nfsu::kNoHandle) {
                    check(false, "a click on the background clears the selection");
                    return;
                }
                ++background;
            }
        }
    }
    check(checked > 20 && background > 20,
          "the sweep covered both drawn and background pixels");

    // The highlight mask is exactly the selected group's pixels.
    std::vector<unsigned char> mask;
    check(inspector.highlight_mask(buffers, mask, error) && mask.size() == frame.ids().size(),
          "an empty selection still produces a mask");
    for (const unsigned char value : mask) {
        if (value != 0) {
            check(false, "nothing is highlighted while nothing is selected");
            break;
        }
    }
    check(inspector.command(nfsu::InspectorCommand::select_next_draw, error),
          "a group is selected for the highlight");
    const std::size_t selected = inspector.selected_draw();
    check(inspector.highlight_mask(buffers, mask, error), "the mask is produced");
    std::size_t marked = 0;
    bool exact = true;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const bool owns = frame.ids()[i] != nfsu::kNoTriangle &&
                          inspector.draw_of_triangle(frame.ids()[i]) == selected;
        exact = exact && ((mask[i] != 0) == owns);
        marked += mask[i] != 0 ? 1U : 0U;
    }
    check(exact && marked > 0,
          "the mask marks exactly the pixels the selected group drew");
    check(inspector.command(nfsu::InspectorCommand::toggle_highlight, error) &&
              inspector.highlight_mask(buffers, mask, error),
          "the mask is produced with the highlight off");
    marked = 0;
    for (const unsigned char value : mask) {
        marked += value != 0 ? 1U : 0U;
    }
    check(marked == 0, "the highlight off marks nothing");
}

void test_views() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    const nfsu::RenderOptions options = inspect_options();
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    if (!render(scene, options, frame, stats)) {
        ++g_failures;
        return;
    }
    nfsu::Inspector inspector;
    if (!open_inspector(inspector, scene, options)) {
        ++g_failures;
        return;
    }
    const nfsu::InspectionBuffers buffers = nfsu::inspection_buffers_of(frame);
    std::string error;
    std::vector<unsigned char> pixels;
    check(inspector.view() == nfsu::InspectorView::shaded, "the session starts shaded");
    check(inspector.view_pixels(buffers, pixels, error) &&
              pixels.size() == frame.rgba().size() && pixels == frame.rgba(),
          "the shaded view is the frame itself");

    check(inspector.command(nfsu::InspectorCommand::cycle_view, error) &&
              inspector.view() == nfsu::InspectorView::draws,
          "the draws view is selected");
    check(inspector.view_pixels(buffers, pixels, error), "the draws view is produced");
    std::map<std::size_t, std::vector<unsigned char>> colors;
    bool one_color_per_draw = true;
    bool background_kept = true;
    for (std::size_t i = 0; i < frame.ids().size(); ++i) {
        const std::vector<unsigned char> pixel(pixels.begin() + i * 4,
                                               pixels.begin() + i * 4 + 4);
        if (frame.ids()[i] == nfsu::kNoTriangle) {
            for (int k = 0; k < 4; ++k) {
                background_kept = background_kept && pixel[k] == options.background[k];
            }
            continue;
        }
        const std::size_t draw = inspector.draw_of_triangle(frame.ids()[i]);
        const auto found = colors.find(draw);
        if (found == colors.end()) {
            colors.emplace(draw, pixel);
        } else {
            one_color_per_draw = one_color_per_draw && found->second == pixel;
        }
    }
    check(background_kept, "the draws view keeps the background where nothing was drawn");
    check(one_color_per_draw, "every group has one color in the draws view");
    check(colors.size() == kDrawCount, "every group appears in the draws view");
    std::set<std::vector<unsigned char>> distinct;
    for (const auto& entry : colors) {
        distinct.insert(entry.second);
    }
    check(distinct.size() == kDrawCount, "no two groups share a color");

    check(inspector.command(nfsu::InspectorCommand::cycle_view, error) &&
              inspector.view() == nfsu::InspectorView::meshes,
          "the meshes view is selected");
    check(inspector.view_pixels(buffers, pixels, error), "the meshes view is produced");
    std::map<std::size_t, std::vector<unsigned char>> mesh_colors;
    bool one_color_per_mesh = true;
    for (std::size_t i = 0; i < frame.ids().size(); ++i) {
        if (frame.ids()[i] == nfsu::kNoTriangle) {
            continue;
        }
        const std::size_t draw = inspector.draw_of_triangle(frame.ids()[i]);
        const std::size_t mesh = scene.draws[draw].mesh;
        const std::vector<unsigned char> pixel(pixels.begin() + i * 4,
                                               pixels.begin() + i * 4 + 4);
        const auto found = mesh_colors.find(mesh);
        if (found == mesh_colors.end()) {
            mesh_colors.emplace(mesh, pixel);
        } else {
            one_color_per_mesh = one_color_per_mesh && found->second == pixel;
        }
    }
    check(one_color_per_mesh && mesh_colors.size() == kMeshCount,
          "the meshes view gives each mesh one color");

    check(inspector.command(nfsu::InspectorCommand::cycle_view, error) &&
              inspector.view() == nfsu::InspectorView::depth,
          "the depth view is selected");
    check(inspector.view_pixels(buffers, pixels, error), "the depth view is produced");
    bool white_at_nearest = false;
    double nearest = 0.0;
    bool any = false;
    std::size_t nearest_index = 0;
    for (std::size_t i = 0; i < frame.ids().size(); ++i) {
        if (frame.ids()[i] == nfsu::kNoTriangle) {
            continue;
        }
        if (!any || frame.depth()[i] < nearest) {
            nearest = frame.depth()[i];
            nearest_index = i;
            any = true;
        }
    }
    if (any) {
        white_at_nearest = pixels[nearest_index * 4U] == 255 &&
                           pixels[nearest_index * 4U + 1] == 255 &&
                           pixels[nearest_index * 4U + 2] == 255;
    }
    check(white_at_nearest, "the nearest drawn pixel is white in the depth view");

    // A view that needs a buffer the backend did not give it is refused
    // rather than invented.
    nfsu::InspectionBuffers partial = buffers;
    partial.depth = nullptr;
    check(!inspector.view_pixels(partial, pixels, error) && pixels.empty(),
          "the depth view without a depth buffer is refused");
    partial = buffers;
    partial.ids = nullptr;
    check(!inspector.view_pixels(partial, pixels, error) && pixels.empty(),
          "a view without triangle ids is refused");
}

void test_overlay() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    const nfsu::RenderOptions options = inspect_options();
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    if (!render(scene, options, frame, stats)) {
        ++g_failures;
        return;
    }
    nfsu::Inspector inspector;
    if (!open_inspector(inspector, scene, options)) {
        ++g_failures;
        return;
    }
    std::string error;
    std::vector<std::string> lines;
    const auto ascii_and_bounded = [](const std::vector<std::string>& text) {
        for (const std::string& line : text) {
            if (line.size() > nfsu::kMaxOverlayLineBytes) {
                return false;
            }
            for (const char ch : line) {
                const unsigned char byte = static_cast<unsigned char>(ch);
                if (byte < 0x20U || byte > 0x7EU) {
                    return false;
                }
            }
        }
        return true;
    };
    const auto joined = [](const std::vector<std::string>& text) {
        std::string all;
        for (const std::string& line : text) {
            all += line;
            all += '\n';
        }
        return all;
    };

    inspector.build_overlay(stats, lines);
    check(!lines.empty() && ascii_and_bounded(lines),
          "the summary overlay is printable ASCII");
    std::string all = joined(lines);
    check(all.find("camera yaw") != std::string::npos &&
              all.find("selection none") != std::string::npos &&
              all.find("options cull none") != std::string::npos,
          "the summary reports the camera, the options and an empty selection");

    check(inspector.command(nfsu::InspectorCommand::select_next_draw, error),
          "a group is selected for the overlay");
    inspector.build_overlay(stats, lines);
    all = joined(lines);
    check(ascii_and_bounded(lines) && all.find("selection group 0 of 10") != std::string::npos &&
              all.find("QUAD0") != std::string::npos,
          "the summary names the selected group and its mesh");
    check(all.find("drawn world bounds") == std::string::npos,
          "the summary stops short of the detail fields");

    check(inspector.command(nfsu::InspectorCommand::cycle_overlay, error) &&
              inspector.overlay() == nfsu::InspectorOverlay::detail,
          "the detail overlay is selected");
    inspector.build_overlay(stats, lines);
    all = joined(lines);
    check(ascii_and_bounded(lines) &&
              all.find("texture state alpha_test") != std::string::npos &&
              all.find("group first_index") != std::string::npos &&
              all.find("drawn world bounds") != std::string::npos,
          "the detail overlay reports the record, the texture and its render state");
    char expected[16];
    std::snprintf(expected, sizeof(expected), "0x%08X",
                  static_cast<unsigned int>(scene.draws[0].texture_hash));
    check(all.find(expected) != std::string::npos,
          "the detail overlay names the texture hash of the selected group");

    // The mesh whose name is raw bytes rather than an identifier. Both
    // guarantees the window relies on are only exercised here: nothing else
    // in the fixture carries a byte outside printable ASCII, and no other
    // line comes near kMaxOverlayLineBytes.
    for (std::size_t i = 0; i + 1 < kMeshCount; ++i) {
        check(inspector.command(nfsu::InspectorCommand::select_next_mesh, error),
              "the mesh walk reaches the mesh named with raw bytes");
    }
    check(inspector.selected_mesh() + 1 == kMeshCount,
          "the mesh named with raw bytes is selected");
    inspector.build_overlay(stats, lines);
    all = joined(lines);
    check(ascii_and_bounded(lines),
          "an overlay quoting a raw mesh name stays printable ASCII within a line");
    std::size_t longest = 0;
    for (const std::string& line : lines) {
        longest = std::max(longest, line.size());
    }
    check(longest == nfsu::kMaxOverlayLineBytes,
          "the name overruns a line, so the truncation is the reason it fits");
    check(all.find("\\x00") != std::string::npos &&
              all.find("\\xFF") != std::string::npos &&
              all.find("\\x7F") != std::string::npos,
          "a NUL, a high byte and a control byte reach the overlay escaped");
    check(all.find("\\x22") != std::string::npos &&
              all.find("\\x5C") != std::string::npos,
          "a quote and a backslash in a name cannot close or escape the quoting");

    check(inspector.command(nfsu::InspectorCommand::cycle_overlay, error) &&
              inspector.overlay() == nfsu::InspectorOverlay::help,
          "the help overlay is selected");
    inspector.build_overlay(stats, lines);
    check(!lines.empty() && ascii_and_bounded(lines), "the help overlay is ASCII");

    check(inspector.command(nfsu::InspectorCommand::cycle_overlay, error) &&
              inspector.overlay() == nfsu::InspectorOverlay::off,
          "the overlay cycles off");
    inspector.build_overlay(stats, lines);
    check(lines.empty(), "the overlay off says nothing");
}

void test_reset_refusals() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    nfsu::Inspector inspector;
    std::string error;
    nfsu::RenderOptions bad_camera = inspect_options();
    bad_camera.camera.pitch_deg = 120.0;
    check(!inspector.reset(scene, bad_camera, nfsu::RenderLimits{}, error) &&
              !inspector.has_scene() && !error.empty(),
          "a camera the renderer would refuse is refused here too");

    nfsu::RenderOptions bad_size = inspect_options();
    bad_size.width = 0;
    check(!inspector.reset(scene, bad_size, nfsu::RenderLimits{}, error) &&
              !inspector.has_scene(),
          "a zero viewport is refused");
    bad_size = inspect_options();
    bad_size.width = nfsu::RenderLimits{}.max_width + 1;
    check(!inspector.reset(scene, bad_size, nfsu::RenderLimits{}, error) &&
              !inspector.has_scene(),
          "a viewport past the render limits is refused");

    nfsu::RenderOptions bad_coefficients = inspect_options();
    bad_coefficients.material_state = nfsu::MaterialStateMode::retail;
    bad_coefficients.coefficients.alpha_min = 1e9;
    check(!inspector.reset(scene, bad_coefficients, nfsu::RenderLimits{}, error) &&
              !inspector.has_scene(),
          "coefficients the renderer would refuse are refused here too");

    nfsu::PreparedCarScene broken;
    broken.meshes.resize(1);  // a mesh with no draw fails the scene self-check
    check(!inspector.reset(broken, inspect_options(), nfsu::RenderLimits{}, error) &&
              !inspector.has_scene(),
          "a scene that does not validate is refused");

    // An inspector with no scene answers the empty state and ignores input.
    check(inspector.selected_draw() == nfsu::kNoHandle &&
              inspector.draw_order().empty() && inspector.triangle_ranges().empty(),
          "an inspector with no scene is empty");
    const double yaw = inspector.options().camera.yaw_deg;
    inspector.orbit(100.0, 100.0);
    inspector.zoom(5);
    check(inspector.options().camera.yaw_deg == yaw,
          "input without a scene changes nothing");
    check(!inspector.command(nfsu::InspectorCommand::reset_camera, error) &&
              !error.empty(),
          "a command without a scene is refused");

    check(inspector.reset(scene, inspect_options(), nfsu::RenderLimits{}, error) &&
              inspector.has_scene(),
          "a good scene opens after the refusals");

    nfsu::InspectorSensitivity sensitivity;
    sensitivity.orbit_deg_per_pixel = 0.0;
    check(!inspector.set_sensitivity(sensitivity, error) && !error.empty(),
          "a zero orbit sensitivity is refused");
    sensitivity = nfsu::InspectorSensitivity{};
    sensitivity.zoom_step = 1.0;
    check(!inspector.set_sensitivity(sensitivity, error),
          "a zoom step that would not zoom is refused");
    sensitivity = nfsu::InspectorSensitivity{};
    sensitivity.orbit_deg_per_pixel = 1.0;
    check(inspector.set_sensitivity(sensitivity, error) &&
              inspector.sensitivity().orbit_deg_per_pixel == 1.0,
          "a sensible sensitivity is kept");

    check(!inspector.set_viewport(0, 100, error) &&
              inspector.options().width == inspect_options().width,
          "a zero viewport keeps the old one");
    check(inspector.set_viewport(64, 48, error) && inspector.options().width == 64 &&
              inspector.options().height == 48,
          "a viewport inside the limits is taken");
}

// A backend renders when the frame is stale and repaints when it is merely
// dirty. Getting this wrong costs a whole rasterized car every time the
// overlay moves, so it is pinned per transition.
void test_dirty_and_stale() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    nfsu::Inspector inspector;
    if (!open_inspector(inspector, scene, inspect_options())) {
        ++g_failures;
        return;
    }
    check(inspector.dirty() && inspector.frame_stale(),
          "a fresh session needs a first frame");
    inspector.clear_dirty();
    inspector.clear_frame_stale();
    check(!inspector.dirty() && !inspector.frame_stale(), "both flags clear");

    const auto settle = [&inspector]() {
        inspector.clear_dirty();
        inspector.clear_frame_stale();
    };
    std::string error;
    // Changes the frame depends on.
    inspector.orbit(5.0, 5.0);
    check(inspector.dirty() && inspector.frame_stale(), "an orbit stales the frame");
    settle();
    inspector.pan(5.0, 5.0);
    check(inspector.frame_stale(), "a pan stales the frame");
    settle();
    inspector.zoom(1);
    check(inspector.frame_stale(), "a zoom stales the frame");
    settle();
    check(inspector.set_viewport(200, 100, error) && inspector.frame_stale(),
          "a resize stales the frame");
    settle();
    check(inspector.command(nfsu::InspectorCommand::cycle_cull, error) &&
              inspector.frame_stale(),
          "a render option stales the frame");
    settle();
    check(inspector.command(nfsu::InspectorCommand::view_top, error) &&
              inspector.frame_stale(),
          "a view snap stales the frame");
    settle();

    // Changes that only move pixels the backend already has.
    check(inspector.command(nfsu::InspectorCommand::select_next_draw, error) &&
              inspector.dirty() && !inspector.frame_stale(),
          "selecting a group repaints without a render");
    settle();
    check(inspector.command(nfsu::InspectorCommand::cycle_view, error) &&
              inspector.dirty() && !inspector.frame_stale(),
          "switching the view repaints without a render");
    settle();
    check(inspector.command(nfsu::InspectorCommand::cycle_overlay, error) &&
              inspector.dirty() && !inspector.frame_stale(),
          "moving the overlay repaints without a render");
    settle();
    check(inspector.command(nfsu::InspectorCommand::toggle_highlight, error) &&
              inspector.dirty() && !inspector.frame_stale(),
          "the highlight repaints without a render");
    settle();
    check(inspector.command(nfsu::InspectorCommand::clear_selection, error) &&
              inspector.dirty() && !inspector.frame_stale(),
          "clearing the selection repaints without a render");
    settle();
    check(inspector.set_viewport(inspector.options().width,
                                 inspector.options().height, error) &&
              !inspector.dirty() && !inspector.frame_stale(),
          "a resize to the size already in use changes nothing");
}

void test_window_backend() {
    // The window itself is not opened here: a test that opened one would
    // wait for a person. What is checked is that the backend agrees with
    // itself and that every refusal happens before anything is created.
    const bool supported = nfsu::inspection_window_supported();
    const std::string backend = nfsu::inspection_window_backend_name();
    check(supported == (backend != "none"),
          "the backend name agrees with whether one is supported");
#ifdef _WIN32
    check(supported && backend == "win32", "Windows builds have the win32 backend");
#endif
    nfsu::Inspector empty;
    nfsu::InspectionWindowOptions options;
    std::string error;
    check(!nfsu::run_inspection_window(empty, options, error) && !error.empty(),
          "a window over an inspector with no scene is refused");

    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    nfsu::Inspector inspector;
    if (!open_inspector(inspector, scene, inspect_options())) {
        ++g_failures;
        return;
    }
    if (supported) {
        options.drag_divisor = 0;
        check(!nfsu::run_inspection_window(inspector, options, error) && !error.empty(),
              "a zero drag divisor is refused before a window is created");
        options.drag_divisor = nfsu::kMaxDragDivisor + 1;
        check(!nfsu::run_inspection_window(inspector, options, error) && !error.empty(),
              "an excessive drag divisor is refused before a window is created");
    } else {
        check(!nfsu::run_inspection_window(inspector, options, error) && !error.empty(),
              "a build with no backend refuses to open a window");
    }
}

} // namespace

int main() {
    test_order_and_ranges();
    test_draw_order_stability();
    test_ids_match_the_renderer();
    test_camera_input();
    test_pan_is_divisor_invariant();
    test_commands();
    test_pick_and_highlight();
    test_views();
    test_overlay();
    test_reset_refusals();
    test_dirty_and_stale();
    test_window_backend();

    if (g_failures != 0) {
        std::cerr << g_failures << " inspect test(s) failed\n";
        return 1;
    }
    std::cout << "inspect tests passed\n";
    return 0;
}
