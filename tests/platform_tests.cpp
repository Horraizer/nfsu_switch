#include "nfsu/arg_path.h"
#include "nfsu/geometry.h"
#include "nfsu/input_camera.h"
#include "nfsu/inspect.h"
#include "nfsu/inspect_session.h"
#include "nfsu/inspect_window.h"
#include "nfsu/platform.h"
#include "nfsu/platform_desktop.h"
#include "nfsu/platform_fakes.h"
#include "nfsu/render.h"
#include "nfsu/scene.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// The platform boundary of include/nfsu/platform.h, driven through the fakes
// of include/nfsu/platform_fakes.h: the inspection session that the Win32
// window procedure used to be, the frame loop a console backend runs, the
// neutral filesystem helpers, and the desktop clock and log.
//
// Every assertion is about the code under test - the session, the loop, the
// helpers - and never about a fake's own bookkeeping. A fake reports what it
// was asked, and the test compares that with what the code under test was
// supposed to ask: the pixels against the frame the renderer produced, a
// key's effect against an Inspector given the documented command, a pan
// against pan_delta_for. None of these could be checked before, because the
// logic was inside a window procedure that only a real window runs.

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

// ---------------------------------------------------------------------------
// The five-quad fixture of tests/inspect_tests.cpp: no two groups overlap on
// screen and each texture is a distinct solid color, so the frame shows
// which group drew each pixel.
// ---------------------------------------------------------------------------

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

nfsu::MeshMaterial material(std::uint32_t first, std::uint32_t slot, unsigned char tag) {
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

nfsu::GeometryReport fixture_geometry() {
    nfsu::GeometryReport report;
    for (std::size_t i = 0; i < kMeshCount; ++i) {
        const float z0 = static_cast<float>(i) * 1.2F - 3.0F;
        const float z1 = z0 + 0.8F;
        nfsu::Mesh mesh;
        mesh.index = i;
        mesh.name = "QUAD" + std::to_string(i);
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

nfsu::RenderTextureSet fixture_textures() {
    nfsu::RenderTextureSet set;
    for (std::size_t i = 0; i < kMeshCount; ++i) {
        for (std::size_t slot = 0; slot < 2; ++slot) {
            const std::size_t draw = i * 2U + slot;
            nfsu::RenderTexture texture;
            texture.hash = texture_hash_of(i, slot);
            texture.width = 1;
            texture.height = 1;
            texture.valid = true;
            texture.rgba = {static_cast<unsigned char>(20U + draw * 20U),
                            static_cast<unsigned char>(200U - draw * 15U),
                            static_cast<unsigned char>(90U + draw * 7U), 255};
            texture.layer_order = static_cast<std::uint8_t>(7U - draw / 2U);
            set.textures.push_back(texture);
        }
    }
    return set;
}

// Straight at the quads from +X with no shading and no culling, so a drawn
// pixel is exactly its group's texel.
nfsu::RenderOptions fixture_options() {
    nfsu::RenderOptions options;
    options.width = 240;
    options.height = 120;
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
    if (!nfsu::prepare_car_scene(fixture_geometry(), fixture_textures(), nfsu::MeshSelection{},
                                 nfsu::SceneLimits{}, scene, error)) {
        std::cerr << "test setup failed: " << error << '\n';
        return false;
    }
    return true;
}

bool open_inspector(nfsu::Inspector& inspector, const nfsu::PreparedCarScene& scene,
                    const nfsu::RenderLimits& limits = nfsu::RenderLimits{}) {
    std::string error;
    if (!inspector.reset(scene, fixture_options(), limits, error)) {
        std::cerr << "test inspector reset failed: " << error << '\n';
        return false;
    }
    return true;
}

std::uint64_t bits_of(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Everything a command or a key can change, as text a failure can print.
std::string state_of(const nfsu::Inspector& inspector) {
    const nfsu::RenderOptions& o = inspector.options();
    const double camera[] = {o.camera.yaw_deg,  o.camera.pitch_deg, o.camera.distance,
                             o.camera.target.x, o.camera.target.y,  o.camera.target.z};
    std::string out;
    for (const double value : camera) {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%016llX ",
                      static_cast<unsigned long long>(bits_of(value)));
        out += buffer;
    }
    out += std::to_string(o.width) + "x" + std::to_string(o.height);
    out += " cull=" + std::string(nfsu::cull_mode_name(o.cull));
    out += " shading=" + std::string(nfsu::shading_mode_name(o.shading));
    out += " state=" + std::string(nfsu::material_state_mode_name(o.material_state));
    out += " vertex_color=" + std::to_string(o.vertex_color);
    out += " flip_v=" + std::to_string(o.flip_v);
    out += " view=" + std::string(nfsu::inspector_view_name(inspector.view()));
    out += " overlay=" + std::string(nfsu::inspector_overlay_name(inspector.overlay()));
    out += " highlight=" + std::to_string(inspector.highlight());
    out += " selected=" + std::to_string(inspector.selected_draw());
    return out;
}

// A session over fakes. Members are declared in the order they must be
// built: the services before the session that holds references to them.
struct Rig {
    nfsu::FakeInput input;
    nfsu::FakeDisplay display{240, 120};
    nfsu::FakeLifecycle lifecycle{1000};
    nfsu::Inspector& inspector;
    nfsu::InspectionWindowOptions options;
    nfsu::InspectionSession session;

    explicit Rig(nfsu::Inspector& target, std::uint32_t divisor = 2)
        : inspector(target), options(with_divisor(divisor)),
          session(target, options, input, display, lifecycle) {}

    static nfsu::InspectionWindowOptions with_divisor(std::uint32_t divisor) {
        nfsu::InspectionWindowOptions out;
        out.drag_divisor = divisor;
        return out;
    }

    // What the Win32 backend does for one WM_PAINT.
    void paint() {
        session.update();
        session.present();
    }
};

// The prepared position of the group that drew pixel x, y of the frame, or
// kNoHandle over the background.
std::size_t draw_at(const nfsu::Inspector& inspector, const nfsu::Frame& frame, std::uint32_t x,
                    std::uint32_t y) {
    const std::uint32_t id = frame.ids()[static_cast<std::size_t>(y) * frame.width() + x];
    return id == nfsu::kNoTriangle ? nfsu::kNoHandle : inspector.draw_of_triangle(id);
}

// A pixel of the frame some group drew, and one nothing drew.
bool find_pixels(const nfsu::Inspector& inspector, const nfsu::Frame& frame,
                 std::uint32_t& drawn_x, std::uint32_t& drawn_y, std::uint32_t& empty_x,
                 std::uint32_t& empty_y) {
    bool drawn = false;
    bool empty = false;
    // Away from the edges, and for the drawn pixel inside a 7 by 7 block of
    // one group, so that a click released within its slack of it lands on
    // the same group.
    for (std::uint32_t y = 8; y + 8 < frame.height(); ++y) {
        for (std::uint32_t x = 8; x + 8 < frame.width(); ++x) {
            const std::size_t draw = draw_at(inspector, frame, x, y);
            bool inside = !drawn && draw != nfsu::kNoHandle;
            for (std::uint32_t dy = 0; inside && dy < 7; ++dy) {
                for (std::uint32_t dx = 0; inside && dx < 7; ++dx) {
                    inside = draw_at(inspector, frame, x + dx - 3, y + dy - 3) == draw;
                }
            }
            if (inside) {
                drawn_x = x;
                drawn_y = y;
                drawn = true;
            }
            if (!empty && draw == nfsu::kNoHandle) {
                empty_x = x;
                empty_y = y;
                empty = true;
            }
        }
    }
    return drawn && empty;
}

// ---------------------------------------------------------------------------
// The session: what the window procedure used to decide
// ---------------------------------------------------------------------------

void test_first_frame() {
    nfsu::PreparedCarScene scene;
    nfsu::Inspector inspector;
    if (!build_scene(scene) || !open_inspector(inspector, scene)) {
        ++g_failures;
        return;
    }
    Rig rig(inspector);
    rig.display.set_surface(200, 100);
    rig.session.start();
    check(inspector.options().width == 200 && inspector.options().height == 100,
          "start sizes the viewport to the surface");
    check(rig.display.redraw_requests() == 0 && !rig.session.redraw_requested(),
          "start asks for no redraw: the first paint is the platform's to schedule");
    rig.paint();
    check(rig.session.have_frame() && !rig.session.failed(), "the first paint renders a frame");
    check(rig.display.frames_begun() == 1 && rig.display.frames_ended() == 1 &&
              rig.display.presented().size() == 1 && rig.display.protocol_errors() == 0,
          "one paint is one frame begun, drawn and ended, in that order");
    if (rig.display.presented().empty()) {
        return;
    }
    const nfsu::FakeDisplay::Presented& shown = rig.display.presented().back();
    const nfsu::Frame& frame = rig.session.frame();
    check(shown.width == 200 && shown.height == 100 && shown.surface_width == 200,
          "the image is the rendered frame's size");
    // The shaded view with nothing selected is the frame itself, in the
    // byte order a display takes.
    bool same = shown.bgra.size() == frame.rgba().size();
    for (std::size_t i = 0; same && i < frame.rgba().size() / 4U; ++i) {
        same = shown.bgra[i * 4U + 0] == frame.rgba()[i * 4U + 2] &&
               shown.bgra[i * 4U + 1] == frame.rgba()[i * 4U + 1] &&
               shown.bgra[i * 4U + 2] == frame.rgba()[i * 4U + 0] &&
               shown.bgra[i * 4U + 3] == 255;
    }
    check(same, "every presented pixel is the rendered pixel, as B, G, R, 255");
    std::vector<std::string> expected;
    nfsu::RenderStats stats;
    std::string error;
    nfsu::Frame reference;
    check(nfsu::render_scene(scene, inspector.options(), inspector.limits(), reference, stats,
                             error),
          "the reference frame renders");
    inspector.build_overlay(stats, expected);
    check(!expected.empty() && shown.overlay == expected,
          "the overlay is the inspector's text for that frame");

    // A surface with no area presents nothing, and still ends the frame.
    rig.display.set_surface(0, 0);
    rig.session.present();
    check(rig.display.presented().size() == 1 &&
              rig.display.frames_ended() == 2 && rig.display.protocol_errors() == 0,
          "a surface with no area draws nothing and still ends its frame");

    // A frame the display cannot begin is not ended either, and the request
    // for it stands.
    rig.display.set_surface(200, 100);
    rig.session.handle(nfsu::key_event(nfsu::Key::v));
    rig.display.refuse_frames(1);
    rig.paint();
    check(rig.display.frames_ended() == 2 && rig.display.protocol_errors() == 0 &&
              rig.session.redraw_requested(),
          "a refused begin_frame skips end_frame and keeps the redraw pending");
    rig.paint();
    check(rig.display.presented().size() == 2 && !rig.session.redraw_requested(),
          "the next paint presents it");
}

void test_drag_and_click() {
    nfsu::PreparedCarScene scene;
    nfsu::Inspector inspector;
    if (!build_scene(scene) || !open_inspector(inspector, scene)) {
        ++g_failures;
        return;
    }
    Rig rig(inspector);
    rig.session.start();
    rig.paint();
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t ex = 0;
    std::uint32_t ey = 0;
    if (!find_pixels(inspector, rig.session.frame(), x, y, ex, ey)) {
        std::cerr << "test setup failed: no drawn and empty pixels\n";
        ++g_failures;
        return;
    }
    const std::size_t under = draw_at(inspector, rig.session.frame(), x, y);
    const auto px = [](std::uint32_t v) { return static_cast<std::int32_t>(v); };

    // A press starts an orbit drag: capture, a smaller viewport, no redraw.
    const std::size_t redraws = rig.display.redraw_requests();
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::primary, px(x), px(y)));
    check(rig.session.dragging() && rig.input.captured(), "a press captures the pointer");
    check(inspector.options().width == 120 && inspector.options().height == 60,
          "a drag renders at 1 / drag_divisor of the surface");
    check(rig.display.redraw_requests() == redraws, "a press alone asks for no redraw");

    // A release within the slack is a click, resolved at the full size.
    rig.session.handle(nfsu::pointer_up_event(px(x) + 2, px(y) - 1));
    check(!rig.session.dragging() && !rig.input.captured(), "a release gives the pointer back");
    check(inspector.options().width == 240 && inspector.options().height == 120,
          "and the viewport is the surface again");
    check(rig.display.redraw_requests() == redraws + 1, "and asks for a redraw");
    check(inspector.selected_draw() == nfsu::kNoHandle, "the click is not resolved until the paint");
    rig.paint();
    check(inspector.selected_draw() == draw_at(inspector, rig.session.frame(), x + 2, y - 1) &&
              inspector.selected_draw() == under,
          "a click selects the group under the release");

    // A click on the background clears it.
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::primary, px(ex), px(ey)));
    rig.session.handle(nfsu::pointer_up_event(px(ex), px(ey)));
    rig.paint();
    check(inspector.selected_draw() == nfsu::kNoHandle, "a click on the background clears");

    // A drag past the slack is an orbit and not a click.
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::primary, px(x), px(y)));
    rig.session.handle(nfsu::pointer_up_event(px(x) + 4, px(y)));
    rig.paint();
    check(inspector.selected_draw() == nfsu::kNoHandle,
          "a release four pixels away is not a click");

    // The orbit itself: a move turns the camera by the pointer's degrees.
    const double yaw = inspector.options().camera.yaw_deg;
    const double scale = inspector.sensitivity().orbit_deg_per_pixel;
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::primary, 50, 50));
    rig.session.handle(nfsu::pointer_move_event(60, 50));
    check(inspector.options().camera.yaw_deg == yaw - 10.0 * scale,
          "ten pixels right is ten pixels of orbit");
    const std::size_t before_still = rig.display.redraw_requests();
    rig.session.handle(nfsu::pointer_move_event(60, 50));
    check(rig.display.redraw_requests() == before_still,
          "a move that goes nowhere asks for nothing");
    rig.session.handle(nfsu::pointer_up_event(60, 50));

    // A secondary drag pans and is never a click.
    const nfsu::Vec3 target = inspector.options().camera.target;
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::secondary, px(x), px(y)));
    rig.session.handle(nfsu::pointer_move_event(px(x) + 1, px(y)));
    check(inspector.options().camera.target.x != target.x ||
              inspector.options().camera.target.z != target.z,
          "a secondary drag pans");
    check(inspector.options().camera.yaw_deg == yaw - 10.0 * scale, "and does not orbit");
    rig.session.handle(nfsu::pointer_up_event(px(x), px(y)));
    rig.paint();
    check(inspector.selected_draw() == nfsu::kNoHandle,
          "a pan released where it started is not a click");

    // A second button switches the drag, and any release ends it.
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::primary, 10, 10));
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::middle, 10, 10));
    const nfsu::Vec3 switched = inspector.options().camera.target;
    rig.session.handle(nfsu::pointer_move_event(20, 10));
    check(inspector.options().camera.target.x != switched.x ||
              inspector.options().camera.target.z != switched.z,
          "a second button turns the drag into a pan");
    rig.session.handle(nfsu::pointer_up_event(20, 10));
    check(!rig.session.dragging(), "the first release ends it, whichever button started it");
    const std::size_t calls = rig.input.capture_calls().size();
    rig.session.handle(nfsu::pointer_up_event(20, 10));
    check(rig.input.capture_calls().size() == calls, "a release with no drag releases nothing");
    check(rig.display.protocol_errors() == 0, "the display was used by its contract");
}

void test_pan_in_surface_pixels() {
    nfsu::PreparedCarScene scene;
    nfsu::Inspector inspector;
    nfsu::Inspector reference;
    if (!build_scene(scene) || !open_inspector(inspector, scene) ||
        !open_inspector(reference, scene)) {
        ++g_failures;
        return;
    }
    Rig rig(inspector, 4);
    rig.session.start();
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::secondary, 100, 40));
    rig.session.handle(nfsu::pointer_move_event(130, 52));
    std::string error;
    check(reference.set_viewport(60, 30, error), "the reference renders at the drag size");
    const nfsu::PanDelta delta = nfsu::pan_delta_for(240, 120, 60, 30, 30.0, 12.0);
    reference.pan(delta.dx, delta.dy);
    check(state_of(inspector) == state_of(reference),
          "a pan converts surface pixels with pan_delta_for, to the bit; the session reads " +
              state_of(inspector));
}

void test_wheel() {
    nfsu::PreparedCarScene scene;
    nfsu::Inspector inspector;
    if (!build_scene(scene) || !open_inspector(inspector, scene)) {
        ++g_failures;
        return;
    }
    Rig rig(inspector);
    rig.session.start();
    const double distance = inspector.options().camera.distance;
    const std::size_t redraws = rig.display.redraw_requests();
    rig.session.handle(nfsu::wheel_event(40));
    rig.session.handle(nfsu::wheel_event(40));
    check(inspector.options().camera.distance == distance &&
              rig.display.redraw_requests() == redraws,
          "two thirds of a notch zoom nothing and ask for nothing");
    rig.session.handle(nfsu::wheel_event(40));
    check(inspector.options().camera.distance < distance &&
              rig.display.redraw_requests() == redraws + 1,
          "the third third is one notch in");
    rig.session.handle(nfsu::wheel_event(-nfsu::kWheelNotch));
    check(inspector.options().camera.distance == distance, "one notch out undoes it");

    // Deltas no window reports are clamped to the 16-bit range, so the sum
    // cannot overflow: 32767 is 273 notches and 107 left over.
    nfsu::Inspector wide;
    nfsu::Inspector steps;
    if (!open_inspector(wide, scene) || !open_inspector(steps, scene)) {
        ++g_failures;
        return;
    }
    Rig wild(wide);
    wild.session.start();
    wild.session.handle(nfsu::wheel_event(std::numeric_limits<std::int32_t>::max()));
    steps.zoom(273);
    check(state_of(wide) == state_of(steps), "a delta past 32767 zooms 273 notches");
    wild.session.handle(nfsu::wheel_event(13));
    steps.zoom(1);
    check(state_of(wide) == state_of(steps), "and keeps the 107 left over");
    wild.session.handle(nfsu::wheel_event(std::numeric_limits<std::int32_t>::min()));
    std::string error;
    check(nfsu::validate_camera(wide.options().camera, error),
          "the most negative delta leaves a valid camera");
}

void test_keys() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    // Every key the documented table names, against an inspector given the
    // command itself, from a state in which every one of those commands
    // changes something: the camera turned off its home, group 0 selected.
    struct Binding {
        nfsu::Key key;
        nfsu::InspectorCommand command;
    };
    const Binding table[] = {
        {nfsu::Key::digit_1, nfsu::InspectorCommand::view_front},
        {nfsu::Key::digit_2, nfsu::InspectorCommand::view_right},
        {nfsu::Key::digit_3, nfsu::InspectorCommand::view_rear},
        {nfsu::Key::digit_4, nfsu::InspectorCommand::view_left},
        {nfsu::Key::digit_5, nfsu::InspectorCommand::view_top},
        {nfsu::Key::r, nfsu::InspectorCommand::reset_camera},
        {nfsu::Key::f, nfsu::InspectorCommand::frame_selection},
        {nfsu::Key::g, nfsu::InspectorCommand::frame_scene},
        {nfsu::Key::n, nfsu::InspectorCommand::select_next_draw},
        {nfsu::Key::p, nfsu::InspectorCommand::select_previous_draw},
        {nfsu::Key::m, nfsu::InspectorCommand::select_next_mesh},
        {nfsu::Key::b, nfsu::InspectorCommand::select_previous_mesh},
        {nfsu::Key::escape, nfsu::InspectorCommand::clear_selection},
        {nfsu::Key::v, nfsu::InspectorCommand::cycle_view},
        {nfsu::Key::o, nfsu::InspectorCommand::cycle_overlay},
        {nfsu::Key::h, nfsu::InspectorCommand::toggle_highlight},
        {nfsu::Key::c, nfsu::InspectorCommand::cycle_cull},
        {nfsu::Key::s, nfsu::InspectorCommand::toggle_shading},
        {nfsu::Key::t, nfsu::InspectorCommand::toggle_material_state},
        {nfsu::Key::x, nfsu::InspectorCommand::toggle_vertex_color},
        {nfsu::Key::u, nfsu::InspectorCommand::toggle_flip_v},
    };
    const auto prepare = [&scene](nfsu::Inspector& inspector) {
        if (!open_inspector(inspector, scene)) {
            return false;
        }
        std::string error;
        inspector.orbit(40.0, 25.0);
        return inspector.command(nfsu::InspectorCommand::select_next_draw, error);
    };
    for (const Binding& binding : table) {
        nfsu::Inspector pressed;
        nfsu::Inspector commanded;
        nfsu::Inspector untouched;
        if (!prepare(pressed) || !prepare(commanded) || !prepare(untouched)) {
            ++g_failures;
            return;
        }
        Rig rig(pressed);
        const std::size_t redraws = rig.display.redraw_requests();
        rig.session.handle(nfsu::key_event(binding.key));
        std::string error;
        commanded.command(binding.command, error);
        const std::string name = nfsu::inspector_command_name(binding.command);
        check(state_of(pressed) == state_of(commanded), "the key for " + name + " applies it");
        check(state_of(pressed) != state_of(untouched),
              "and " + name + " changes something here, so the comparison means something");
        check(rig.display.redraw_requests() == redraws + 1, "and asks for a redraw");
    }

    nfsu::Inspector inspector;
    nfsu::Inspector untouched;
    if (!prepare(inspector) || !prepare(untouched)) {
        ++g_failures;
        return;
    }
    Rig rig(inspector);
    rig.session.handle(nfsu::key_event(nfsu::Key::other));
    check(state_of(inspector) == state_of(untouched) && rig.display.redraw_requests() == 0,
          "another key does nothing and asks for nothing");
    rig.session.handle(nfsu::key_event(nfsu::Key::q));
    check(rig.lifecycle.exit_requests() == 1 && state_of(inspector) == state_of(untouched) &&
              rig.display.redraw_requests() == 0,
          "Q asks the platform to end the session and changes nothing else");
    check(!rig.lifecycle.pump(), "and the session then ends");
}

void test_capture_lost_and_resize() {
    nfsu::PreparedCarScene scene;
    nfsu::Inspector inspector;
    if (!build_scene(scene) || !open_inspector(inspector, scene)) {
        ++g_failures;
        return;
    }
    Rig rig(inspector);
    rig.session.start();
    rig.paint();

    // The platform takes the pointer mid-drag: the drag ends, the viewport
    // returns, and nothing is picked or released.
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::primary, 30, 30));
    rig.session.handle(nfsu::pointer_cancel_event());
    check(!rig.session.dragging() && inspector.options().width == 240,
          "a lost capture ends the drag and restores the viewport");
    const std::size_t calls = rig.input.capture_calls().size();
    rig.session.handle(nfsu::pointer_up_event(30, 30));
    rig.paint();
    check(rig.input.capture_calls().size() == calls &&
              inspector.selected_draw() == nfsu::kNoHandle,
          "the release that follows is not a click and releases nothing");

    // A resize follows the surface and asks for a redraw.
    const std::size_t redraws = rig.display.redraw_requests();
    rig.session.handle(nfsu::resize_event(480, 200));
    check(inspector.options().width == 480 && inspector.options().height == 200 &&
              rig.display.redraw_requests() == redraws + 1,
          "a resize sizes the viewport to the new surface");
    rig.session.handle(nfsu::resize_event(0, 0));
    check(inspector.options().width == 480, "a surface with no area keeps the viewport");

    // A surface the limits refuse keeps the old viewport, and a click is
    // then dropped rather than resolved against the wrong pixel.
    rig.session.handle(nfsu::resize_event(9000, 120));
    check(inspector.options().width == 480, "a surface past the limits keeps the viewport");
    rig.display.set_surface(9000, 120);
    rig.paint();
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t ex = 0;
    std::uint32_t ey = 0;
    if (!find_pixels(inspector, rig.session.frame(), x, y, ex, ey)) {
        std::cerr << "test setup failed: no drawn pixel\n";
        ++g_failures;
        return;
    }
    rig.session.handle(nfsu::pointer_down_event(nfsu::PointerButton::primary,
                                                static_cast<std::int32_t>(x),
                                                static_cast<std::int32_t>(y)));
    rig.session.handle(nfsu::pointer_up_event(static_cast<std::int32_t>(x),
                                              static_cast<std::int32_t>(y)));
    rig.paint();
    check(inspector.selected_draw() == nfsu::kNoHandle,
          "a click on a surface the frame does not cover one to one is dropped");
    check(rig.display.protocol_errors() == 0, "the display was used by its contract");
}

void test_render_failures() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    // A first frame that cannot render fails the session.
    {
        nfsu::RenderLimits tight;
        tight.max_pixel_tests = 1;
        nfsu::Inspector inspector;
        if (!open_inspector(inspector, scene, tight)) {
            ++g_failures;
            return;
        }
        Rig rig(inspector);
        rig.session.start();
        rig.paint();
        check(rig.session.failed() && !rig.session.failure().empty() &&
                  !rig.session.have_frame(),
              "a first frame that fails to render fails the session: " + rig.session.failure());
        check(rig.display.presented().empty() && rig.display.frames_ended() == 1,
              "and presents nothing");
    }
    // A later one keeps the frame there is, and says so in the overlay
    // until a render succeeds again.
    {
        nfsu::Inspector probe;
        if (!open_inspector(probe, scene)) {
            ++g_failures;
            return;
        }
        nfsu::Frame frame;
        nfsu::RenderStats stats;
        std::string error;
        nfsu::render_scene(scene, probe.options(), probe.limits(), frame, stats, error);
        nfsu::RenderLimits limits;
        limits.max_pixel_tests = stats.pixel_tests;
        nfsu::Inspector inspector;
        if (!open_inspector(inspector, scene, limits)) {
            ++g_failures;
            return;
        }
        Rig rig(inspector);
        rig.session.start();
        rig.paint();
        check(rig.session.have_frame() && !rig.session.failed(),
              "setup: the first frame fits the limit exactly");
        rig.session.handle(nfsu::wheel_event(4 * nfsu::kWheelNotch));
        rig.paint();
        check(!rig.session.failed() && !rig.session.render_error().empty() &&
                  rig.session.have_frame(),
              "a closer camera needs more pixel tests, and that render fails without "
              "failing the session");
        // Deliberately not asserted: that the previous frame is shown again.
        // It is not, and it never was. render_scene resets and draws into the
        // frame it is given before a work limit stops it, and the window has
        // always rendered into the frame it shows, so what is presented after
        // a failure like this one is the part of the new frame drawn before
        // the limit - not the previous frame include/nfsu/inspect_window.h
        // promises. Moved here unchanged; the handoff reports it.
        const nfsu::FakeDisplay::Presented& kept = rig.display.presented().back();
        check(!kept.overlay.empty() &&
                  kept.overlay.back() == "render failed: " + rig.session.render_error(),
              "with the failure as the overlay's last line");
        rig.session.handle(nfsu::wheel_event(-4 * nfsu::kWheelNotch));
        rig.paint();
        check(rig.session.render_error().empty() &&
                  rig.display.presented().back().overlay.back().find("render failed") ==
                      std::string::npos,
              "and the line goes once a render succeeds");
    }
}

void test_highlight() {
    nfsu::PreparedCarScene scene;
    nfsu::Inspector inspector;
    if (!build_scene(scene) || !open_inspector(inspector, scene)) {
        ++g_failures;
        return;
    }
    Rig rig(inspector);
    rig.session.start();
    rig.session.handle(nfsu::key_event(nfsu::Key::n));
    rig.paint();
    const std::size_t selected = inspector.selected_draw();
    const nfsu::Frame& frame = rig.session.frame();
    const std::vector<unsigned char>& shown = rig.display.presented().back().bgra;
    std::size_t tinted = 0;
    bool exact = shown.size() == frame.rgba().size();
    for (std::size_t i = 0; exact && i < frame.ids().size(); ++i) {
        const std::uint32_t id = frame.ids()[i];
        const bool mine = id != nfsu::kNoTriangle && inspector.draw_of_triangle(id) == selected;
        unsigned r = frame.rgba()[i * 4U + 0];
        unsigned g = frame.rgba()[i * 4U + 1];
        unsigned b = frame.rgba()[i * 4U + 2];
        if (mine) {
            ++tinted;
            r = (r + 255U) / 2U;
            g = (g + 190U) / 2U;
            b = b / 2U;
        }
        exact = shown[i * 4U + 0] == b && shown[i * 4U + 1] == g && shown[i * 4U + 2] == r &&
                shown[i * 4U + 3] == 255;
    }
    check(selected != nfsu::kNoHandle && tinted > 0,
          "setup: a selected group covers some pixels");
    check(exact, "the selected group's pixels are half way to amber and no others are");
}

// ---------------------------------------------------------------------------
// The frame loop a console backend runs
// ---------------------------------------------------------------------------

nfsu::GamepadFrame pad(std::uint32_t buttons, std::int32_t rx) {
    nfsu::GamepadFrame frame;
    frame.pads[0].connected = true;
    frame.pads[0].buttons = buttons;
    frame.pads[0].axes[2] = rx;
    return frame;
}

void test_frame_loop() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    constexpr std::uint64_t kStep = 15625;
    // Nothing for three frames, the right stick pinned for four, the
    // controller gone mid-gesture, then nothing for four more.
    std::vector<nfsu::GamepadFrame> frames;
    for (int k = 0; k < 12; ++k) {
        nfsu::GamepadFrame frame;
        if (k >= 3 && k < 7) {
            frame = pad(0, nfsu::kAxisMax);
        }
        frame.time_us = static_cast<std::uint64_t>(k) * kStep;
        frames.push_back(frame);
    }
    {
        nfsu::Inspector inspector;
        if (!open_inspector(inspector, scene)) {
            ++g_failures;
            return;
        }
        nfsu::FakeInput input;
        nfsu::FakeDisplay display(240, 120);
        nfsu::FakeLifecycle lifecycle(12);
        nfsu::FakeTiming timing(0, kStep);
        for (const nfsu::GamepadFrame& frame : frames) {
            input.push_gamepads(frame);
        }
        nfsu::GamepadCamera gamepads;
        nfsu::InspectionWindowOptions options;
        std::string error;
        check(nfsu::run_inspection_loop(inspector, options, &gamepads, input, display, lifecycle,
                                        timing, error),
              "the loop runs until the lifecycle ends it: " + error);
        check(lifecycle.pumps() == 12 && input.samples() == 12 && timing.reads() == 12,
              "one pump, one sample and one clock read per frame");
        std::string sizes;
        for (const nfsu::FakeDisplay::Presented& shown : display.presented()) {
            sizes += std::to_string(shown.width) + "x" + std::to_string(shown.height) + " ";
        }
        check(sizes == "240x120 120x60 120x60 120x60 120x60 240x120 ",
              "a frame is presented first, while the stick moves, at the drag size, and once "
              "when it stops, at full size again; it reads " + sizes);
        check(display.protocol_errors() == 0, "the display was used by its contract");

        // What the loop fed the camera is exactly the frames with the
        // clock's times: the same frames stepped directly give the same
        // orbit, to the bit.
        nfsu::Inspector direct;
        if (!open_inspector(direct, scene)) {
            ++g_failures;
            return;
        }
        nfsu::GamepadCamera replay;
        for (const nfsu::GamepadFrame& frame : frames) {
            replay.step(frame, direct);
        }
        check(bits_of(inspector.options().camera.yaw_deg) ==
                      bits_of(direct.options().camera.yaw_deg) &&
                  bits_of(inspector.options().camera.pitch_deg) ==
                      bits_of(direct.options().camera.pitch_deg),
              "the loop moves the camera exactly as the frames do on their own");
        check(inspector.options().camera.yaw_deg != fixture_options().camera.yaw_deg,
              "and they did move it");
    }

    // A controller's exit ends the loop through the lifecycle.
    {
        nfsu::Inspector inspector;
        if (!open_inspector(inspector, scene)) {
            ++g_failures;
            return;
        }
        nfsu::FakeInput input;
        nfsu::FakeDisplay display(240, 120);
        nfsu::FakeLifecycle lifecycle(50);
        nfsu::FakeTiming timing(0, kStep);
        const std::uint32_t start = nfsu::gamepad_button_bit(nfsu::GamepadButton::start);
        input.push_gamepads(pad(0, 0));
        input.push_gamepads(pad(0, 0));
        input.push_gamepads(pad(start, 0));
        nfsu::GamepadCamera gamepads;
        std::string error;
        check(nfsu::run_inspection_loop(inspector, nfsu::InspectionWindowOptions{}, &gamepads,
                                        input, display, lifecycle, timing, error),
              "an exit is a clean end");
        check(lifecycle.exit_requests() == 1 && lifecycle.pumps() == 3,
              "start on the third frame ends the loop after it");
    }

    // Input events reach the session on the frame they are queued for, and
    // Q ends the loop the same way.
    {
        nfsu::Inspector inspector;
        if (!open_inspector(inspector, scene)) {
            ++g_failures;
            return;
        }
        nfsu::FakeInput input;
        nfsu::FakeDisplay display(240, 120);
        nfsu::FakeLifecycle lifecycle(50);
        nfsu::FakeTiming timing;
        lifecycle.on_pump([&input](std::size_t frame) {
            if (frame == 1) {
                input.push_event(nfsu::key_event(nfsu::Key::v));
            } else if (frame == 4) {
                input.push_event(nfsu::key_event(nfsu::Key::q));
            }
        });
        std::string error;
        check(nfsu::run_inspection_loop(inspector, nfsu::InspectionWindowOptions{}, nullptr,
                                        input, display, lifecycle, timing, error),
              "a loop with no controller runs on events alone");
        check(inspector.view() == nfsu::InspectorView::draws && lifecycle.pumps() == 5 &&
                  timing.reads() == 0,
              "the key applies on its frame, Q ends the loop after its own, and no clock is "
              "read without a controller");
        check(display.presented().size() == 2,
              "only the first frame and the one the key changed are presented");
    }

    // Refusals, and a session that fails.
    {
        nfsu::Inspector empty;
        nfsu::FakeInput input;
        nfsu::FakeDisplay display(240, 120);
        nfsu::FakeLifecycle lifecycle(5);
        nfsu::FakeTiming timing;
        std::string error;
        check(!nfsu::run_inspection_loop(empty, nfsu::InspectionWindowOptions{}, nullptr, input,
                                         display, lifecycle, timing, error) &&
                  !error.empty() && lifecycle.pumps() == 0,
              "an inspector with no scene is refused before anything runs");
        nfsu::Inspector inspector;
        if (!open_inspector(inspector, scene)) {
            ++g_failures;
            return;
        }
        for (const std::uint32_t divisor : {0U, nfsu::kMaxDragDivisor + 1U}) {
            nfsu::InspectionWindowOptions options;
            options.drag_divisor = divisor;
            error.clear();
            check(!nfsu::run_inspection_loop(inspector, options, nullptr, input, display,
                                             lifecycle, timing, error) &&
                      !error.empty() && lifecycle.pumps() == 0,
                  "drag divisor " + std::to_string(divisor) + " is refused before anything runs");
        }
        nfsu::RenderLimits tight;
        tight.max_pixel_tests = 1;
        nfsu::Inspector failing;
        if (!open_inspector(failing, scene, tight)) {
            ++g_failures;
            return;
        }
        error.clear();
        check(!nfsu::run_inspection_loop(failing, nfsu::InspectionWindowOptions{}, nullptr,
                                         input, display, lifecycle, timing, error) &&
                  !error.empty() && lifecycle.pumps() == 1 && display.presented().empty(),
              "a first frame that cannot render ends the loop with its error: " + error);
    }
}

// ---------------------------------------------------------------------------
// Filesystem, timing and logging
// ---------------------------------------------------------------------------

void test_filesystem() {
    nfsu::FakeFileSystem files;
    files.refuse("b", "is not allowed here");
    char program[] = "program";
    char a[] = "a";
    char b[] = "b";
    char c[] = "c";
    char* argv[] = {program, a, b, c, nullptr};
    std::string reason;
    check(!nfsu::check_argument_paths(files, 4, argv, 1, reason) &&
              reason == "argument 2 is not allowed here",
          "the first refused argument is named by position: " + reason);
    check(files.asked() == std::vector<std::string>{"a", "b"},
          "and nothing after it is asked about");
    check(nfsu::check_argument_paths(files, 4, argv, 3, reason),
          "arguments before `first` are not checked");
    check(nfsu::check_argument_paths(files, 4, nullptr, 1, reason),
          "no argument vector is nothing to refuse");
    check(nfsu::argument_path_or_empty(files, "a") == std::filesystem::path("a") &&
              nfsu::argument_path_or_empty(files, "b").empty(),
          "an accepted argument is its path and a refused one the empty path");

    // The desktop service is the conversion the programs have always used:
    // the three free functions give what it gives, and a null argument is
    // refused through the moved loop with the words it always had.
    nfsu::FileSystemService& desktop = nfsu::desktop_file_system();
    std::filesystem::path through_service;
    std::filesystem::path through_function;
    std::string service_reason;
    std::string function_reason;
    check(desktop.argument_to_path("plain/ascii.bin", through_service, service_reason) ==
                  nfsu::argument_to_path("plain/ascii.bin", through_function, function_reason) &&
              through_service == through_function && through_service == "plain/ascii.bin",
          "the desktop service converts as argument_to_path does");
    char* with_null[] = {program, nullptr, nullptr};
    check(!nfsu::check_arguments(2, with_null, 1, reason) &&
              reason == "argument 1 is missing",
          "a null argument is refused by position, as before: " + reason);
    check(nfsu::argument_path(nullptr).empty(), "and converts to the empty path");
}

void test_desktop_timing_and_logging() {
    nfsu::DesktopTiming timing;
    std::uint64_t previous = timing.now_us();
    bool monotonic = true;
    bool advanced = false;
    // Up to about two seconds of reads: a clock that never moves fails here
    // rather than passing a check that only asks for no step backwards.
    for (long i = 0; i < 200000000L && !advanced; ++i) {
        const std::uint64_t now = timing.now_us();
        monotonic = monotonic && now >= previous;
        advanced = now > previous;
        previous = now;
    }
    check(monotonic, "the desktop clock never steps back");
    check(advanced, "and it moves");

    std::ostringstream stream;
    nfsu::DesktopLogging log(stream);
    log.write(nfsu::LogLevel::error, "bad\x01" "byte\xFF\nnext\\");
    log.write(nfsu::LogLevel::info, "");
    log.write(nfsu::LogLevel::warning, "plain text");
    check(stream.str() ==
              "error: bad\\x01byte\\xFF\\x0Anext\\\n"
              "info: \n"
              "warning: plain text\n",
          "each call is one line of ASCII, every other byte written as \\xHH; it reads " +
              stream.str());
}

// The stub stays what it was: a build with no window backend refuses to open
// a window whatever it is given, with the same words as before.
void test_backend_fails_closed() {
    if (nfsu::inspection_window_supported()) {
        check(std::string(nfsu::inspection_window_backend_name()) != "none",
              "a supported backend has a name");
        return;
    }
    nfsu::PreparedCarScene scene;
    nfsu::Inspector inspector;
    if (!build_scene(scene) || !open_inspector(inspector, scene)) {
        ++g_failures;
        return;
    }
    std::string error;
    check(!nfsu::run_inspection_window(inspector, nfsu::InspectionWindowOptions{}, error) &&
              error == "this build has no desktop inspection window backend" &&
              std::string(nfsu::inspection_window_backend_name()) == "none",
          "the stub refuses a valid inspector and valid options: " + error);
}

} // namespace

int main() {
    test_first_frame();
    test_drag_and_click();
    test_pan_in_surface_pixels();
    test_wheel();
    test_keys();
    test_capture_lost_and_resize();
    test_render_failures();
    test_highlight();
    test_frame_loop();
    test_filesystem();
    test_desktop_timing_and_logging();
    test_backend_fails_closed();

    if (g_failures != 0) {
        std::cerr << g_failures << " platform test(s) failed\n";
        return 1;
    }
    std::cout << "platform tests passed\n";
    return 0;
}
