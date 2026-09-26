#include "nfsu/geometry.h"
#include "nfsu/input.h"
#include "nfsu/input_camera.h"
#include "nfsu/inspect.h"
#include "nfsu/render.h"
#include "nfsu/scene.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

// The controller model of include/nfsu/input.h and its camera in
// include/nfsu/input_camera.h, driven by frames and recordings only: no
// test here reads a clock or opens a device, which is the property that
// makes every one of them replay identically.
//
// Most camera expectations are exact rather than within a tolerance. The
// rates and the 15625-microsecond frame interval (1/64 s) are chosen so
// that every per-frame delta is a short binary fraction, and the
// inspector's sensitivity is set to one degree per pixel, so a stick held
// for n frames moves the camera by exactly n deltas whatever the platform
// and whether or not it fuses a multiply-add. The replay test at the end is
// the one that pins a camera it did not derive, and it says so.

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

// ---------------------------------------------------------------------------
// The scene the inspector holds: the five-quad fixture of
// tests/inspect_tests.cpp, whose name, textures and material records are
// known to prepare. Only the camera matters here, but an Inspector ignores
// every input until it holds a scene.
// ---------------------------------------------------------------------------

constexpr std::size_t kMeshCount = 5;

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

nfsu::RenderOptions fixture_options() {
    nfsu::RenderOptions options;
    options.width = 240;
    options.height = 120;
    options.camera.yaw_deg = 30.0;
    options.camera.pitch_deg = 10.0;
    options.camera.distance = 8.0;
    options.camera.target = nfsu::Vec3{0.0, 0.5, 0.0};
    options.camera.fov_y_deg = 45.0;
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
                    double degrees_per_pixel = 1.0) {
    std::string error;
    if (!inspector.reset(scene, fixture_options(), nfsu::RenderLimits{}, error)) {
        std::cerr << "test inspector reset failed: " << error << '\n';
        return false;
    }
    nfsu::InspectorSensitivity sensitivity;
    sensitivity.orbit_deg_per_pixel = degrees_per_pixel;
    if (!inspector.set_sensitivity(sensitivity, error)) {
        std::cerr << "test sensitivity refused: " << error << '\n';
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

constexpr std::uint64_t kStepUs = 15625;  // 1/64 s, exactly

std::uint32_t bit(nfsu::GamepadButton button) { return nfsu::gamepad_button_bit(button); }

nfsu::GamepadFrame frame_at(std::uint64_t time_us) {
    nfsu::GamepadFrame frame;
    frame.time_us = time_us;
    return frame;
}

nfsu::GamepadFrame pad_frame(std::uint64_t time_us, std::uint32_t buttons, std::int32_t lx,
                             std::int32_t ly, std::int32_t rx, std::int32_t ry,
                             std::size_t slot = 0) {
    nfsu::GamepadFrame frame = frame_at(time_us);
    nfsu::GamepadState& pad = frame.pads[slot];
    pad.connected = true;
    pad.buttons = buttons;
    pad.axes[0] = lx;
    pad.axes[1] = ly;
    pad.axes[2] = rx;
    pad.axes[3] = ry;
    return frame;
}

std::uint64_t bits_of(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Every value of the camera, bit for bit, as text a failure can print.
std::string camera_bits(const nfsu::OrbitCamera& camera) {
    const double values[] = {camera.yaw_deg,  camera.pitch_deg, camera.distance,
                             camera.target.x, camera.target.y,  camera.target.z,
                             camera.fov_y_deg, camera.near_plane, camera.far_plane};
    std::string out;
    for (const double value : values) {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%016llX",
                      static_cast<unsigned long long>(bits_of(value)));
        if (!out.empty()) {
            out += ' ';
        }
        out += buffer;
    }
    return out;
}

// FNV-1a over the camera's bytes, folded in after every frame, so that a
// replay that diverged on any frame and converged again is still caught.
void fold(std::uint64_t& hash, const nfsu::OrbitCamera& camera) {
    const double values[] = {camera.yaw_deg,  camera.pitch_deg, camera.distance,
                             camera.target.x, camera.target.y,  camera.target.z};
    for (const double value : values) {
        const std::uint64_t word = bits_of(value);
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (word >> (8 * byte)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    }
}

// ---------------------------------------------------------------------------
// The recorded form
// ---------------------------------------------------------------------------

void test_recording_round_trip() {
    const std::string canonical =
        "nfsu-input 1\n"
        "0\n"
        "16000 0 - 0 0 0 0\n"
        "32000 0 face_east+trigger_right 0 0 32767 -120\n"
        "32000 1 face_south+dpad_right 17 -32768 0 5\n"
        "48000 3 start+select+stick_left+stick_right 1 2 3 4\n";
    std::vector<nfsu::GamepadFrame> frames;
    std::string error;
    check(nfsu::parse_input_recording(canonical, frames, error),
          "a canonical recording parses: " + error);
    check(frames.size() == 4, "four times are four frames");
    if (frames.size() == 4) {
        check(frames[0].time_us == 0 && !frames[0].pads[0].connected,
              "a time alone is a frame with nothing connected");
        check(frames[2].pads[0].connected && frames[2].pads[1].connected &&
                  !frames[2].pads[2].connected,
              "two lines with one time are one frame with two slots");
        check(frames[2].pads[0].buttons ==
                  (bit(nfsu::GamepadButton::face_east) | bit(nfsu::GamepadButton::trigger_right)),
              "buttons are read by name");
        check(frames[2].pads[1].axes[1] == nfsu::kAxisMin && frames[2].pads[0].axes[3] == -120,
              "axes are read signed, kAxisMin included");
        check(frames[3].pads[3].connected && frames[3].pads[3].axes[3] == 4,
              "a slot past the first is read into its own place");
    }
    check(nfsu::format_input_recording(frames) == canonical,
          "formatting a canonical recording gives it back byte for byte");

    // Accepted but not canonical: comments, blank lines, CR LF, runs of
    // spaces, buttons out of order and slots out of order. The canonical
    // text is what comes back.
    const std::string loose =
        "nfsu-input 1\r\n"
        "# a comment\r\n"
        "\r\n"
        "10   2  trigger_right+face_east   0 0 0 0\r\n"
        "10 0 - 1 1 1 1\r\n"
        "   # an indented comment\n"
        "20";
    std::vector<nfsu::GamepadFrame> loose_frames;
    check(nfsu::parse_input_recording(loose, loose_frames, error),
          "comments, blank lines, CR LF and extra spaces are accepted: " + error);
    check(nfsu::format_input_recording(loose_frames) ==
              "nfsu-input 1\n"
              "10 0 - 1 1 1 1\n"
              "10 2 face_east+trigger_right 0 0 0 0\n"
              "20\n",
          "the canonical form orders slots and buttons and drops the rest");

    // What a backend fills in beyond what is read: a disconnected slot with
    // fields set, bits that are not buttons, and axes outside the range. The
    // text records what the mapper reads, and it parses back.
    nfsu::GamepadFrame odd = pad_frame(7, 0xFFFF0000U | bit(nfsu::GamepadButton::start),
                                       100000, -100000, 5, 6);
    odd.pads[4].buttons = bit(nfsu::GamepadButton::face_south);
    odd.pads[4].axes[0] = 999;
    const std::string odd_text = nfsu::format_input_recording({odd});
    check(odd_text == "nfsu-input 1\n7 0 start 32767 -32768 5 6\n",
          "undefined bits are dropped, axes clamped and a disconnected slot omitted");
    std::vector<nfsu::GamepadFrame> odd_back;
    check(nfsu::parse_input_recording(odd_text, odd_back, error) && odd_back.size() == 1,
          "the recorded form of a live frame parses");

    // Appended, not replaced.
    std::vector<nfsu::GamepadFrame> appended(2);
    check(nfsu::parse_input_recording("nfsu-input 1\n5\n", appended, error) &&
              appended.size() == 3 && appended[2].time_us == 5,
          "parsing appends to the frames already there");
    std::vector<nfsu::GamepadFrame> none;
    check(nfsu::parse_input_recording("nfsu-input 1\n", none, error) && none.empty(),
          "a header alone is a recording of no frames");
}

void test_recording_refusals() {
    struct Case {
        const char* label;
        std::string text;
        const char* line;
    };
    const std::string h = "nfsu-input 1\n";
    const Case cases[] = {
        {"an empty text", "", "line 1"},
        {"another version", "nfsu-input 2\n", "line 1"},
        {"a comment before the header", "# first\nnfsu-input 1\n", "line 1"},
        // In comments, which nothing else reads: only the byte rule can
        // refuse these.
        {"a byte above 0x7E in a comment", h + "10\n# caf\xC3\xA9\n", "line 3"},
        {"a tab in a comment", h + "# a\tcomment\n", "line 2"},
        {"a byte above 0x7E in a field", h + "10 0 - 0 0 0 \x80\n", "line 2"},
        {"two fields", h + "10 0\n", "line 2"},
        {"eight fields", h + "10 0 - 0 0 0 0 0\n", "line 2"},
        {"a leading zero", h + "010\n", "line 2"},
        {"a signed time", h + "+10\n", "line 2"},
        {"a negative time", h + "-10\n", "line 2"},
        {"a time past 64 bits", h + "18446744073709551616\n", "line 2"},
        {"slot 8", h + "10 8 - 0 0 0 0\n", "line 2"},
        {"slot 08", h + "10 08 - 0 0 0 0\n", "line 2"},
        {"an unknown button", h + "10 0 face_up 0 0 0 0\n", "line 2"},
        {"a button named twice", h + "10 0 start+start 0 0 0 0\n", "line 2"},
        {"an empty button name", h + "10 0 start+ 0 0 0 0\n", "line 2"},
        {"an axis past the top", h + "10 0 - 32768 0 0 0\n", "line 2"},
        {"an axis past the bottom", h + "10 0 - 0 -32769 0 0\n", "line 2"},
        {"minus zero", h + "10 0 - 0 0 -0 0\n", "line 2"},
        {"time going backwards", h + "20\n10\n", "line 3"},
        {"a slot named twice in a frame", h + "10 1 - 0 0 0 0\n10 1 - 0 0 0 0\n", "line 3"},
        {"an empty frame after a slot", h + "10 1 - 0 0 0 0\n10\n", "line 3"},
        {"a slot after an empty frame", h + "10\n10 1 - 0 0 0 0\n", "line 3"},
        {"an empty frame twice", h + "10\n10\n", "line 3"},
    };
    for (const Case& refused : cases) {
        std::vector<nfsu::GamepadFrame> frames(1);
        std::string error;
        const bool ok = nfsu::parse_input_recording(refused.text, frames, error);
        check(!ok, std::string("refused: ") + refused.label);
        check(error.find(refused.line) != std::string::npos,
              std::string("the refusal of ") + refused.label + " names " + refused.line +
                  ", and reads: " + error);
        check(frames.size() == 1, std::string("nothing is appended for ") + refused.label);
        if (std::string(refused.label).find("in a comment") != std::string::npos) {
            check(error.find("printable ASCII") != std::string::npos,
                  std::string("the refusal of ") + refused.label + " is the byte rule's");
        }
    }
    std::string oversized(nfsu::kMaxRecordingBytes + 1U, '#');
    std::vector<nfsu::GamepadFrame> frames;
    std::string error;
    check(!nfsu::parse_input_recording(oversized, frames, error) && frames.empty(),
          "a text longer than kMaxRecordingBytes is refused before it is read");
}

// ---------------------------------------------------------------------------
// Dead zones
// ---------------------------------------------------------------------------

void test_dead_zone() {
    std::string error;
    const nfsu::StickDeadZone zone;
    check(nfsu::validate_stick_dead_zone(zone, error), "the default zone validates");
    check(!nfsu::validate_stick_dead_zone({-1, 100}, error), "a negative inner is refused");
    check(!nfsu::validate_stick_dead_zone({100, 100}, error), "inner equal to outer is refused");
    check(!nfsu::validate_stick_dead_zone({10, nfsu::kAxisMax + 1}, error),
          "an outer past full deflection is refused");
    check(nfsu::validate_stick_dead_zone({0, nfsu::kAxisMax}, error),
          "the widest zone validates");

    // The inner edge, decided by an exact integer comparison.
    nfsu::StickVector v = nfsu::apply_stick_dead_zone(zone, 3000, 0);
    check(v.x == 0.0 && v.y == 0.0, "a stick exactly on the inner edge reads centred");
    v = nfsu::apply_stick_dead_zone(zone, 3001, 0);
    check(v.x == 1.0 / 27000.0 && v.y == 0.0,
          "one unit past the inner edge reads 1 / (outer - inner)");
    // 1800^2 + 2400^2 is 3000^2 exactly: on the edge along a diagonal.
    v = nfsu::apply_stick_dead_zone(zone, 1800, -2400);
    check(v.x == 0.0 && v.y == 0.0, "a diagonal exactly on the inner edge reads centred");
    v = nfsu::apply_stick_dead_zone(zone, 1800, -2401);
    check(v.x > 0.0 && v.y < 0.0, "a diagonal one unit past it does not");

    // The widest zone (inner 0) at a perfectly centred stick: every other
    // valid (inner, outer) pair's inner edge is equivalent under <= and <,
    // because the fallback branch's scale converges to the same value
    // either way, but inner 0 is not - the fallback there divides x and y,
    // both 0, by length, also 0, and only the early return above (taken
    // when the comparison is <=, skipped when it is <) avoids that 0 / 0.
    // Finding 3, review comment 2933 on PR #136.
    const nfsu::StickDeadZone widest{0, nfsu::kAxisMax};
    v = nfsu::apply_stick_dead_zone(widest, 0, 0);
    check(v.x == 0.0 && v.y == 0.0, "the widest zone at a centred stick reads exactly centred");
    check(!std::isnan(v.x) && !std::isnan(v.y),
          "and never NaN, even though the fallback branch it must not take would divide 0 by 0");

    // The outer edge and past it.
    v = nfsu::apply_stick_dead_zone(zone, 30000, 0);
    check(v.x == 1.0 && v.y == 0.0, "a stick on the outer edge reads fully deflected");
    v = nfsu::apply_stick_dead_zone(zone, 29999, 0);
    check(v.x == 26999.0 / 27000.0, "just inside the outer edge the scale is still linear");
    v = nfsu::apply_stick_dead_zone(zone, nfsu::kAxisMax, 0);
    check(v.x == 1.0 && v.y == 0.0, "an axis pinned at its extreme reads 1");
    v = nfsu::apply_stick_dead_zone(zone, 0, nfsu::kAxisMin);
    check(v.x == 0.0 && v.y == -1.0, "kAxisMin reads -1, the length of kAxisMax");
    const nfsu::StickVector corner =
        nfsu::apply_stick_dead_zone(zone, nfsu::kAxisMax, nfsu::kAxisMax);
    check(corner.x == corner.y && corner.x > 0.707 && corner.x < 0.7072,
          "a stick in the corner of its square keeps its diagonal");
    check(corner.x * corner.x + corner.y * corner.y <= 1.0 + 1e-15,
          "and is no longer than full deflection");
    // Asymmetric on purpose: a clamp only shows in the direction, and two
    // equal components point the same way clamped or not.
    const nfsu::StickVector wild =
        nfsu::apply_stick_dead_zone(zone, std::numeric_limits<std::int32_t>::max(), 20000);
    const nfsu::StickVector tame = nfsu::apply_stick_dead_zone(zone, nfsu::kAxisMax, 20000);
    check(wild.x == tame.x && wild.y == tame.y && wild.x < 0.9,
          "a live value outside the range reads as the extreme it is clamped to");
    const nfsu::StickVector low =
        nfsu::apply_stick_dead_zone(zone, std::numeric_limits<std::int32_t>::min(), 20000);
    const nfsu::StickVector floor = nfsu::apply_stick_dead_zone(zone, -nfsu::kAxisMax, 20000);
    check(low.x == floor.x && low.y == floor.y,
          "and one below it as -kAxisMax, not kAxisMin");
    const nfsu::StickVector minimum = nfsu::apply_stick_dead_zone(zone, nfsu::kAxisMin, 20000);
    check(minimum.x == floor.x && minimum.y == floor.y,
          "kAxisMin itself points exactly where -kAxisMax does");

    // Symmetric to the bit, and monotonic across the band.
    const std::int32_t samples[] = {3001, 4000, 12345, 17000, 29999, 32000};
    for (const std::int32_t x : samples) {
        const nfsu::StickVector plus = nfsu::apply_stick_dead_zone(zone, x, x / 3);
        const nfsu::StickVector minus = nfsu::apply_stick_dead_zone(zone, -x, -(x / 3));
        check(minus.x == -plus.x && minus.y == -plus.y,
              "the zone is symmetric through the centre at " + std::to_string(x));
    }
    double previous = 0.0;
    bool monotonic = true;
    for (std::int32_t x = 2990; x <= 30010; x += 10) {
        const double value = nfsu::apply_stick_dead_zone(zone, x, 0).x;
        monotonic = monotonic && value >= previous && value <= 1.0;
        previous = value;
    }
    check(monotonic, "deflection grows with the stick and never passes 1");
}

// ---------------------------------------------------------------------------
// Issue #165: the dead zone's square root is det_sqrt, not the library
// std::sqrt, which MinGW's out-of-line implementation does not round
// correctly at -O0. The default build (no CMAKE_BUILD_TYPE, so no
// optimization flags) took that out-of-line path and, for this case, gave
// a different answer than every optimized build; this pins the optimized
// answer in every build.
// ---------------------------------------------------------------------------

void test_dead_zone_determinism() {
    // The reviewer's case on PR #162 (issue #165's origin): the default
    // zone (inner 3000, outer 30000) at x=3184, y=61. squared =
    // 3184^2 + 61^2 = 10141577, strictly between 3000^2 and 30000^2, so the
    // fallback branch runs and (length - inner) cancels most of length's
    // significant bits, which is what turns an 18-ulp difference in the
    // square root into an 18-ulp difference in x. Linked against the
    // library sqrt, a default (-O0) build gave x=0x1.bff3a1d7de2afp-8,
    // y=0x1.129fa766c0ce6p-13; every build whose compiler inlined a
    // hardware sqrtsd (-O2, -O3, and MSVC's /fp:precise) gave
    // x=0x1.bff3a1d7de2c1p-8, y=0x1.129fa766c0cf2p-13. det_sqrt makes every
    // build give the second pair.
    const nfsu::StickDeadZone zone;
    const nfsu::StickVector v = nfsu::apply_stick_dead_zone(zone, 3184, 61);
    check(v.x == 0x1.bff3a1d7de2c1p-8,
          "the reviewer's case (x=3184, y=61) gives the correctly rounded x "
          "in every build");
    check(v.y == 0x1.129fa766c0cf2p-13,
          "and the correctly rounded y");
}

// ---------------------------------------------------------------------------
// The remapping table
// ---------------------------------------------------------------------------

nfsu::InputBinding binding(nfsu::GamepadButton button, std::uint16_t action,
                           bool invert = false) {
    nfsu::InputBinding out;
    out.control = nfsu::gamepad_control(button);
    out.action = action;
    out.invert = invert;
    return out;
}

nfsu::InputBinding binding(nfsu::GamepadAxis axis, std::uint16_t action, bool invert = false) {
    nfsu::InputBinding out;
    out.control = nfsu::gamepad_control(axis);
    out.action = action;
    out.invert = invert;
    return out;
}

void test_binding_refusals() {
    std::string error;
    const std::vector<nfsu::ActionKind> kinds = nfsu::camera_action_kinds();
    const std::vector<nfsu::InputBinding> defaults = nfsu::default_camera_bindings();
    check(nfsu::validate_input_bindings(kinds, defaults, error),
          "the default camera table validates: " + error);
    check(defaults.size() == nfsu::kGamepadButtonCount + nfsu::kGamepadAxisCount,
          "the default table binds every button and every axis");
    check(kinds.size() == nfsu::kCameraActionCount &&
              kinds[nfsu::camera_action_id(nfsu::CameraAction::zoom)] ==
                  nfsu::ActionKind::analog &&
              kinds[nfsu::camera_action_id(nfsu::CameraAction::exit_session)] ==
                  nfsu::ActionKind::digital &&
              kinds.back() == nfsu::ActionKind::digital,
          "motions are analog and every command is digital");

    // The layout docs/INPUT.md tabulates, binding by binding, so that a
    // control quietly moved to another action is a failure here.
    using C = nfsu::CameraAction;
    using I = nfsu::InspectorCommand;
    using B = nfsu::GamepadButton;
    const auto id = [](C action) { return nfsu::camera_action_id(action); };
    const auto command = [](I which) { return nfsu::camera_command_action(which); };
    const std::vector<nfsu::InputBinding> documented = {
        binding(nfsu::GamepadAxis::right_x, id(C::orbit_x)),
        binding(nfsu::GamepadAxis::right_y, id(C::orbit_y)),
        binding(nfsu::GamepadAxis::left_x, id(C::pan_x)),
        binding(nfsu::GamepadAxis::left_y, id(C::pan_y)),
        binding(B::trigger_right, id(C::zoom)),
        binding(B::trigger_left, id(C::zoom), true),
        binding(B::start, id(C::exit_session)),
        binding(B::select, command(I::reset_camera)),
        binding(B::dpad_up, command(I::view_front)),
        binding(B::dpad_right, command(I::view_right)),
        binding(B::dpad_down, command(I::view_rear)),
        binding(B::dpad_left, command(I::view_left)),
        binding(B::stick_right, command(I::view_top)),
        binding(B::stick_left, command(I::frame_scene)),
        binding(B::face_east, command(I::frame_selection)),
        binding(B::face_south, command(I::clear_selection)),
        binding(B::shoulder_right, command(I::select_next_draw)),
        binding(B::shoulder_left, command(I::select_previous_draw)),
        binding(B::face_north, command(I::cycle_view)),
        binding(B::face_west, command(I::cycle_overlay)),
    };
    bool same = documented.size() == defaults.size();
    for (std::size_t i = 0; same && i < documented.size(); ++i) {
        same = documented[i].control.kind == defaults[i].control.kind &&
               documented[i].control.index == defaults[i].control.index &&
               documented[i].action == defaults[i].action &&
               documented[i].invert == defaults[i].invert;
    }
    check(same, "the default table is the documented layout, binding for binding");

    const std::uint16_t orbit_x = nfsu::camera_action_id(nfsu::CameraAction::orbit_x);
    const std::uint16_t reset = nfsu::camera_command_action(nfsu::InspectorCommand::reset_camera);
    struct Case {
        const char* label;
        std::vector<nfsu::InputBinding> table;
        const char* names;
    };
    nfsu::InputBinding bad_button = binding(nfsu::GamepadButton::start, reset);
    bad_button.control.index = static_cast<std::uint8_t>(nfsu::kGamepadButtonCount);
    nfsu::InputBinding bad_axis = binding(nfsu::GamepadAxis::left_x, orbit_x);
    bad_axis.control.index = static_cast<std::uint8_t>(nfsu::kGamepadAxisCount);
    const Case cases[] = {
        {"a control bound twice",
         {binding(nfsu::GamepadButton::start, reset),
          binding(nfsu::GamepadButton::start, orbit_x)},
         "binding 1"},
        {"an axis on a digital action", {binding(nfsu::GamepadAxis::left_x, reset)}, "binding 0"},
        {"an inverted digital action",
         {binding(nfsu::GamepadButton::start, reset, true)},
         "binding 0"},
        {"an action past the set",
         {binding(nfsu::GamepadButton::start,
                  static_cast<std::uint16_t>(nfsu::kCameraActionCount))},
         "binding 0"},
        {"a button past the last", {binding(nfsu::GamepadButton::start, reset), bad_button},
         "binding 1"},
        {"an axis past the last", {bad_axis}, "binding 0"},
    };
    for (const Case& refused : cases) {
        const bool ok = nfsu::validate_input_bindings(kinds, refused.table, error);
        check(!ok, std::string("refused: ") + refused.label);
        check(error.find(refused.names) != std::string::npos,
              std::string("the refusal of ") + refused.label + " names " + refused.names +
                  ": " + error);
    }
    check(!nfsu::validate_input_bindings({}, {}, error), "an empty action set is refused");
    check(!nfsu::validate_input_bindings(
              std::vector<nfsu::ActionKind>(nfsu::kMaxInputActions + 1U, nfsu::ActionKind::analog),
              {}, error),
          "an action set past kMaxInputActions is refused");
    std::vector<nfsu::InputBinding> many;
    for (std::size_t i = 0; i <= nfsu::kMaxInputBindings; ++i) {
        many.push_back(binding(nfsu::GamepadButton::start, 0));
    }
    check(!nfsu::validate_input_bindings(kinds, many, error),
          "a table past kMaxInputBindings is refused");

    // A refused table changes nothing.
    nfsu::GamepadCamera camera;
    check(!camera.set_bindings({binding(nfsu::GamepadAxis::left_x, reset)}, error),
          "the camera refuses a bad table");
    check(camera.bindings().size() == defaults.size(), "and keeps the one it had");
}

// ---------------------------------------------------------------------------
// The mapper's rules, on a two-action set
// ---------------------------------------------------------------------------

constexpr std::uint16_t kMove = 0;
constexpr std::uint16_t kFire = 1;

bool mapper_under_test(nfsu::InputMapper& mapper) {
    std::string error;
    const bool ok = mapper.configure(
        {nfsu::ActionKind::analog, nfsu::ActionKind::digital},
        {binding(nfsu::GamepadAxis::left_x, kMove),
         binding(nfsu::GamepadButton::dpad_right, kMove),
         binding(nfsu::GamepadButton::dpad_left, kMove, true),
         binding(nfsu::GamepadButton::face_south, kFire)},
        nfsu::StickDeadZone{}, error);
    if (!ok) {
        std::cerr << "test mapper refused: " << error << '\n';
    }
    return ok;
}

void test_mapper_rules() {
    nfsu::InputMapper mapper;
    if (!mapper_under_test(mapper)) {
        ++g_failures;
        return;
    }
    const std::uint32_t right = bit(nfsu::GamepadButton::dpad_right);
    const std::uint32_t left = bit(nfsu::GamepadButton::dpad_left);
    const std::uint32_t fire = bit(nfsu::GamepadButton::face_south);
    const auto move = [&mapper]() { return mapper.actions()[kMove].value; };
    const auto held = [&mapper]() { return mapper.actions()[kFire].held; };
    const auto pressed = [&mapper]() { return mapper.actions()[kFire].pressed; };

    // Intervals.
    mapper.update(pad_frame(1000, 0, 0, 0, 0, 0));
    check(mapper.interval_us() == 0, "the first frame reports no interval");
    mapper.update(pad_frame(1000 + kStepUs, 0, 0, 0, 0, 0));
    check(mapper.interval_us() == kStepUs, "the next reports the time between them");
    mapper.update(pad_frame(9000000, 0, 0, 0, 0, 0));
    check(mapper.interval_us() == nfsu::kMaxFrameIntervalUs,
          "a stall is clamped to kMaxFrameIntervalUs");
    mapper.update(pad_frame(5000, 0, 0, 0, 0, 0));
    check(mapper.interval_us() == 0, "a time that goes backwards reports no interval");
    mapper.update(pad_frame(5000 + kStepUs, 0, 0, 0, 0, 0));
    check(mapper.interval_us() == kStepUs, "and the next is measured from it");

    // Conflicts, exactly.
    std::uint64_t t = 100000;
    mapper.update(pad_frame(t += kStepUs, right | left, 0, 0, 0, 0));
    check(move() == 0.0, "opposite buttons on one controller cancel exactly");
    mapper.update(pad_frame(t += kStepUs, right, nfsu::kAxisMax, 0, 0, 0));
    check(move() == 1.0, "two sources pushing one way are no faster than one");
    mapper.update(pad_frame(t += kStepUs, right, -nfsu::kAxisMax, 0, 0, 0));
    check(move() == 0.0, "a stick against a button cancels exactly");
    nfsu::GamepadFrame two = pad_frame(t += kStepUs, 0, nfsu::kAxisMax, 0, 0, 0, 0);
    two.pads[5] = pad_frame(0, 0, nfsu::kAxisMin, 0, 0, 0, 0).pads[0];
    mapper.update(two);
    check(move() == 0.0, "two controllers pushing opposite ways cancel exactly");

    // A button held when its controller connects fires nothing until it
    // has been let go.
    mapper.reset();
    mapper.update(pad_frame(t += kStepUs, fire | right, 0, 0, 0, 0));
    check(!held() && !pressed() && move() == 0.0,
          "buttons down on the connecting frame are latched, analog ones too");
    mapper.update(pad_frame(t += kStepUs, fire | right, 0, 0, 0, 0));
    check(!held() && !pressed(), "and stay latched while held");
    mapper.update(pad_frame(t += kStepUs, 0, 0, 0, 0, 0));
    check(!held() && !pressed(), "letting go fires nothing");
    mapper.update(pad_frame(t += kStepUs, fire, 0, 0, 0, 0));
    check(held() && pressed(), "the next press fires, on its own frame");
    mapper.update(pad_frame(t += kStepUs, fire, 0, 0, 0, 0));
    check(held() && !pressed(), "and holding it does not fire again");

    // The controller disappears mid-press and mid-deflection.
    mapper.update(pad_frame(t += kStepUs, fire, nfsu::kAxisMax, 0, 0, 0));
    check(held() && move() == 1.0, "setup: held and deflected");
    mapper.update(frame_at(t += kStepUs));
    check(!held() && !pressed() && move() == 0.0,
          "a disconnect ends everything on its own frame and fires nothing");
    mapper.update(pad_frame(t += kStepUs, fire, 0, 0, 0, 0));
    check(!held() && !pressed(), "reconnecting with the button still down fires nothing");
    mapper.update(pad_frame(t += kStepUs, 0, 0, 0, 0, 0));
    mapper.update(pad_frame(t += kStepUs, fire, 0, 0, 0, 0));
    check(pressed(), "until it is pressed again");

    // A second controller pressing what the first already holds is not a
    // new press.
    nfsu::GamepadFrame both = pad_frame(t += kStepUs, fire, 0, 0, 0, 0, 0);
    both.pads[1] = pad_frame(0, 0, 0, 0, 0, 0, 0).pads[0];
    mapper.update(both);
    both.time_us = t += kStepUs;
    both.pads[1].buttons = fire;
    mapper.update(both);
    check(held() && !pressed(), "a second source of a held action is not a press");

    // A refused configuration keeps the one that worked.
    std::string error;
    check(!mapper.configure({nfsu::ActionKind::digital},
                            {binding(nfsu::GamepadAxis::left_x, 0)}, nfsu::StickDeadZone{},
                            error),
          "a bad configuration is refused");
    mapper.update(pad_frame(t += kStepUs, right, 0, 0, 0, 0));
    check(mapper.actions().size() == 2 && move() == 1.0,
          "and the mapper still runs the one it had");
    check(!mapper.configure({nfsu::ActionKind::analog}, {}, nfsu::StickDeadZone{5, 5}, error),
          "a bad dead zone is refused");
}

// ---------------------------------------------------------------------------
// The camera
// ---------------------------------------------------------------------------

nfsu::CameraRates exact_rates() {
    nfsu::CameraRates rates;
    rates.orbit_deg_per_second = 128.0;   // 2 degrees per 1/64 s
    rates.pan_heights_per_second = 0.75;
    rates.zoom_steps_per_second = 6.0;    // 0.09375 steps per 1/64 s
    return rates;
}

bool camera_under_test(nfsu::GamepadCamera& camera) {
    std::string error;
    if (!camera.set_rates(exact_rates(), error)) {
        std::cerr << "test rates refused: " << error << '\n';
        return false;
    }
    return true;
}

nfsu::Vec3 difference(const nfsu::Vec3& a, const nfsu::Vec3& b) {
    return nfsu::Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

double dot(const nfsu::Vec3& a, const nfsu::Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

void test_camera_motion() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    const nfsu::OrbitCamera start = fixture_options().camera;

    // A stick held for n frames moves the camera by exactly n deltas.
    {
        nfsu::Inspector inspector;
        nfsu::GamepadCamera camera;
        if (!open_inspector(inspector, scene) || !camera_under_test(camera)) {
            ++g_failures;
            return;
        }
        std::uint64_t t = 0;
        nfsu::CameraStep step = camera.step(pad_frame(t, 0, 0, 0, nfsu::kAxisMax, 0), inspector);
        check(step.moving && step.orbit_dx == 0.0 &&
                  inspector.options().camera.yaw_deg == start.yaw_deg,
              "the first frame moves nothing: it has no interval");
        for (int i = 0; i < 9; ++i) {
            step = camera.step(pad_frame(t += kStepUs, 0, 0, 0, nfsu::kAxisMax, 0), inspector);
        }
        check(inspector.options().camera.yaw_deg == start.yaw_deg - 18.0,
              "nine frames at full deflection and 128 degrees per second turn 18 degrees");
        check(step.orbit_dx == 2.0 && step.orbit_dy == 0.0,
              "each hands the inspector the pixels of 2 degrees at one degree per pixel");
        for (int i = 0; i < 4; ++i) {
            camera.step(pad_frame(t += kStepUs, 0, 0, 0, 0, nfsu::kAxisMax), inspector);
        }
        check(inspector.options().camera.pitch_deg == start.pitch_deg + 8.0,
              "stick up raises pitch by the same rate");
    }

    // The convention, against the renderer's own basis rather than a sign
    // this test chose: stick right orbits the eye to the camera's right,
    // stick up raises it, and a pan carries the target the way it points.
    {
        nfsu::Inspector inspector;
        nfsu::GamepadCamera camera;
        if (!open_inspector(inspector, scene) || !camera_under_test(camera)) {
            ++g_failures;
            return;
        }
        std::string error;
        nfsu::CameraFrame basis;
        check(nfsu::make_camera_frame(inspector.options().camera, 240, 120, basis, error),
              "the fixture camera projects");
        const nfsu::Vec3 eye0 = nfsu::orbit_eye(inspector.options().camera);
        const nfsu::Vec3 target0 = inspector.options().camera.target;
        std::uint64_t t = 0;
        camera.step(pad_frame(t, 0, 0, 0, 12000, 0), inspector);
        camera.step(pad_frame(t += kStepUs, 0, 0, 0, 12000, 0), inspector);
        const nfsu::Vec3 eye1 = nfsu::orbit_eye(inspector.options().camera);
        check(dot(difference(eye1, eye0), basis.right) > 0.0,
              "stick right moves the eye along the camera's right");
        camera.step(pad_frame(t += kStepUs, 0, 0, 0, 0, 12000), inspector);
        const nfsu::Vec3 eye2 = nfsu::orbit_eye(inspector.options().camera);
        check(eye2.y > eye1.y, "stick up raises the eye");
        check(nfsu::make_camera_frame(inspector.options().camera, 240, 120, basis, error),
              "the moved camera projects");
        const nfsu::Vec3 before = inspector.options().camera.target;
        camera.step(pad_frame(t += kStepUs, 0, 12000, 0, 0, 0), inspector);
        const nfsu::Vec3 after_right = inspector.options().camera.target;
        check(dot(difference(after_right, before), basis.right) > 0.0,
              "left stick right carries the target to the camera's right");
        camera.step(pad_frame(t += kStepUs, 0, 0, 12000, 0, 0), inspector);
        check(dot(difference(inspector.options().camera.target, after_right), basis.up) > 0.0,
              "left stick up carries the target up");
        check(dot(difference(inspector.options().camera.target, target0), basis.forward) <
                      1e-9 &&
                  dot(difference(inspector.options().camera.target, target0), basis.forward) >
                      -1e-9,
              "a pan never moves the target along the view");
    }

    // Zoom in whole steps, with the fraction carried: 0.09375 of a step per
    // frame reaches the first whole step on the eleventh interval.
    {
        nfsu::Inspector inspector;
        nfsu::GamepadCamera camera;
        if (!open_inspector(inspector, scene) || !camera_under_test(camera)) {
            ++g_failures;
            return;
        }
        const std::uint32_t in = bit(nfsu::GamepadButton::trigger_right);
        const std::uint32_t out = bit(nfsu::GamepadButton::trigger_left);
        std::uint64_t t = 0;
        camera.step(pad_frame(t, 0, 0, 0, 0, 0), inspector);
        int first_step = 0;
        for (int interval = 1; interval <= 20 && first_step == 0; ++interval) {
            const nfsu::CameraStep step =
                camera.step(pad_frame(t += kStepUs, in, 0, 0, 0, 0), inspector);
            if (step.zoom_steps != 0) {
                first_step = interval;
                check(step.zoom_steps == 1, "a whole step at a time at this rate");
            }
        }
        check(first_step == 11, "the first whole step comes on the eleventh interval, not " +
                                    std::to_string(first_step));
        check(inspector.options().camera.distance < start.distance, "and it zooms in");
        const double distance = inspector.options().camera.distance;
        for (int i = 0; i < 40; ++i) {
            const nfsu::CameraStep step =
                camera.step(pad_frame(t += kStepUs, in | out, 0, 0, 0, 0), inspector);
            check(step.zoom_steps == 0 && !step.moving,
                  "both triggers at once cancel exactly, frame after frame");
        }
        check(inspector.options().camera.distance == distance, "and the distance never moves");
    }
}

void test_camera_hostile() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    std::string error;

    // An axis pinned at its extreme, for long enough to wrap yaw many times
    // over, with every frame a stall that is clamped to the maximum.
    {
        nfsu::Inspector inspector;
        nfsu::GamepadCamera camera;
        if (!open_inspector(inspector, scene, 0.35) || !camera_under_test(camera)) {
            ++g_failures;
            return;
        }
        std::uint64_t t = 0;
        bool valid = true;
        bool wrapped = true;
        for (int i = 0; i < 3000; ++i) {
            camera.step(pad_frame(t, 0, nfsu::kAxisMin, nfsu::kAxisMax, nfsu::kAxisMax,
                                  nfsu::kAxisMax),
                        inspector);
            t += 5000000;
            const nfsu::OrbitCamera& now = inspector.options().camera;
            valid = valid && nfsu::validate_camera(now, error);
            wrapped = wrapped && now.yaw_deg >= -180.0 && now.yaw_deg < 180.0;
        }
        check(valid, "the camera stays valid under a pinned stick and endless stalls");
        check(wrapped, "yaw stays wrapped");
        check(inspector.options().camera.pitch_deg == nfsu::kMaxPitch,
              "pitch rests on its limit rather than past it");
    }

    // The controller disappears mid-gesture: the camera stops on that very
    // frame and stays stopped, and a reconnect holding a button fires
    // nothing.
    {
        nfsu::Inspector inspector;
        nfsu::GamepadCamera camera;
        if (!open_inspector(inspector, scene) || !camera_under_test(camera)) {
            ++g_failures;
            return;
        }
        std::uint64_t t = 0;
        const std::uint32_t reset = bit(nfsu::GamepadButton::select);
        camera.step(pad_frame(t, 0, 20000, 0, nfsu::kAxisMax, 0), inspector);
        camera.step(pad_frame(t += kStepUs, 0, 20000, 0, nfsu::kAxisMax, 0), inspector);
        const std::string moved = camera_bits(inspector.options().camera);
        check(moved != camera_bits(fixture_options().camera), "setup: the stick moved the camera");
        nfsu::CameraStep step = camera.step(frame_at(t += kStepUs), inspector);
        check(!step.moving && step.commands.empty() && step.orbit_dx == 0.0 &&
                  step.pan_dx == 0.0,
              "the frame the controller disappears on moves nothing and fires nothing");
        for (int i = 0; i < 10; ++i) {
            step = camera.step(frame_at(t += kStepUs), inspector);
        }
        check(camera_bits(inspector.options().camera) == moved,
              "and the camera stays where the gesture left it");
        step = camera.step(pad_frame(t += kStepUs, reset, 0, 0, 0, 0), inspector);
        check(step.commands.empty() && camera_bits(inspector.options().camera) == moved,
              "reconnecting with reset held does not reset");
        camera.step(pad_frame(t += kStepUs, 0, 0, 0, 0, 0), inspector);
        step = camera.step(pad_frame(t += kStepUs, reset, 0, 0, 0, 0), inspector);
        check(step.commands.size() == 1 &&
                  step.commands[0] == nfsu::InspectorCommand::reset_camera &&
                  camera_bits(inspector.options().camera) ==
                      camera_bits(fixture_options().camera),
              "a fresh press does");
    }

    // Conflicting inputs: one stick against another controller's, and two
    // commands on one frame.
    {
        nfsu::Inspector inspector;
        nfsu::GamepadCamera camera;
        if (!open_inspector(inspector, scene) || !camera_under_test(camera)) {
            ++g_failures;
            return;
        }
        std::uint64_t t = 0;
        nfsu::GamepadFrame frame = pad_frame(t, 0, 0, 0, nfsu::kAxisMax, 0, 2);
        frame.pads[6] = pad_frame(0, 0, 0, 0, nfsu::kAxisMin, 0, 0).pads[0];
        const std::string still = camera_bits(inspector.options().camera);
        for (int i = 0; i < 20; ++i) {
            frame.time_us = t;
            t += kStepUs;
            const nfsu::CameraStep step = camera.step(frame, inspector);
            check(!step.moving, "opposite sticks on two controllers cancel exactly");
        }
        check(camera_bits(inspector.options().camera) == still, "and the camera never moves");

        const std::uint32_t front = bit(nfsu::GamepadButton::dpad_up);
        const std::uint32_t rear = bit(nfsu::GamepadButton::dpad_down);
        camera.step(pad_frame(t += kStepUs, 0, 0, 0, 0, 0), inspector);
        const nfsu::CameraStep step =
            camera.step(pad_frame(t += kStepUs, front | rear, 0, 0, 0, 0), inspector);
        check(step.commands.size() == 2 &&
                  step.commands[0] == nfsu::InspectorCommand::view_front &&
                  step.commands[1] == nfsu::InspectorCommand::view_rear,
              "two commands on one frame both apply, in InspectorCommand order");
        nfsu::Inspector reference;
        if (!open_inspector(reference, scene)) {
            ++g_failures;
            return;
        }
        check(reference.command(nfsu::InspectorCommand::view_rear, error) &&
                  camera_bits(reference.options().camera) ==
                      camera_bits(inspector.options().camera),
              "so the later one decides the camera");
    }

    // The dead zone edge, through the whole path.
    {
        nfsu::Inspector inspector;
        nfsu::GamepadCamera camera;
        if (!open_inspector(inspector, scene) || !camera_under_test(camera)) {
            ++g_failures;
            return;
        }
        const nfsu::StickDeadZone zone = camera.dead_zone();
        std::uint64_t t = 0;
        camera.step(pad_frame(t, 0, 0, 0, 0, 0), inspector);
        const std::string still = camera_bits(inspector.options().camera);
        nfsu::CameraStep step =
            camera.step(pad_frame(t += kStepUs, 0, 0, 0, zone.inner, 0), inspector);
        check(!step.moving && camera_bits(inspector.options().camera) == still,
              "a stick on the inner edge moves nothing");
        step = camera.step(pad_frame(t += kStepUs, 0, 0, 0, zone.inner + 1, 0), inspector);
        check(step.moving && camera_bits(inspector.options().camera) != still,
              "one unit past it moves the camera");
    }

    // exit_session fires once per press.
    {
        nfsu::Inspector inspector;
        nfsu::GamepadCamera camera;
        if (!open_inspector(inspector, scene)) {
            ++g_failures;
            return;
        }
        const std::uint32_t start = bit(nfsu::GamepadButton::start);
        camera.step(pad_frame(0, 0, 0, 0, 0, 0), inspector);
        check(camera.step(pad_frame(kStepUs, start, 0, 0, 0, 0), inspector).exit_requested,
              "start asks to end the session");
        check(!camera.step(pad_frame(2 * kStepUs, start, 0, 0, 0, 0), inspector).exit_requested,
              "once per press");
    }

    // Remapping inverts an axis: the orbit then swings the other way.
    {
        nfsu::Inspector inspector;
        nfsu::GamepadCamera camera;
        if (!open_inspector(inspector, scene) || !camera_under_test(camera)) {
            ++g_failures;
            return;
        }
        std::vector<nfsu::InputBinding> table = nfsu::default_camera_bindings();
        for (nfsu::InputBinding& entry : table) {
            if (entry.action == nfsu::camera_action_id(nfsu::CameraAction::orbit_x)) {
                entry.invert = true;
            }
        }
        check(camera.set_bindings(table, error), "an inverted orbit is a valid table");
        camera.step(pad_frame(0, 0, 0, 0, nfsu::kAxisMax, 0), inspector);
        camera.step(pad_frame(kStepUs, 0, 0, 0, nfsu::kAxisMax, 0), inspector);
        check(inspector.options().camera.yaw_deg == fixture_options().camera.yaw_deg + 2.0,
              "inverted, stick right raises yaw and swings the eye to the camera's left");
    }

    // A setter that succeeds forgets every earlier frame: the next one has
    // no interval, and a button held across the change is latched, so it
    // fires nothing until it is pressed again.
    {
        const std::uint32_t reset = bit(nfsu::GamepadButton::select);
        const auto change = [&](nfsu::GamepadCamera& camera, int which) {
            std::string refused;
            if (which == 0) {
                return camera.set_rates(exact_rates(), refused);
            }
            if (which == 1) {
                return camera.set_bindings(nfsu::default_camera_bindings(), refused);
            }
            return camera.set_dead_zone(nfsu::StickDeadZone{}, refused);
        };
        const char* const names[] = {"set_rates", "set_bindings", "set_dead_zone"};
        for (int which = 0; which < 3; ++which) {
            nfsu::Inspector inspector;
            nfsu::GamepadCamera camera;
            if (!open_inspector(inspector, scene) || !camera_under_test(camera)) {
                ++g_failures;
                return;
            }
            std::uint64_t t = 0;
            camera.step(pad_frame(t, 0, 0, 0, 0, 0), inspector);
            camera.step(pad_frame(t += kStepUs, reset, 0, 0, 0, 0), inspector);
            inspector.orbit(30.0, 0.0);
            check(change(camera, which), std::string(names[which]) + " succeeds");
            const std::string moved = camera_bits(inspector.options().camera);
            nfsu::CameraStep step =
                camera.step(pad_frame(t += kStepUs, reset, 0, 0, nfsu::kAxisMax, 0), inspector);
            check(step.commands.empty() && step.orbit_dx == 0.0 &&
                      camera_bits(inspector.options().camera) == moved,
                  std::string(names[which]) +
                      " forgets the frames before it: no interval, and the held button fires nothing");
        }
    }

    // Rates are refused, not clamped.
    nfsu::GamepadCamera camera;
    const double bad[] = {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity(), 1e9};
    for (const double value : bad) {
        nfsu::CameraRates rates;
        rates.orbit_deg_per_second = value;
        check(!camera.set_rates(rates, error), "an orbit rate of " + std::to_string(value) +
                                                   " is refused");
        rates = nfsu::CameraRates{};
        rates.pan_heights_per_second = value;
        check(!camera.set_rates(rates, error), "a pan rate of " + std::to_string(value) +
                                                   " is refused");
        rates = nfsu::CameraRates{};
        rates.zoom_steps_per_second = value;
        check(!camera.set_rates(rates, error), "a zoom rate of " + std::to_string(value) +
                                                   " is refused");
    }
    check(camera.rates().orbit_deg_per_second == nfsu::CameraRates{}.orbit_deg_per_second,
          "a refused rate changes nothing");

    // An inspector with no scene ignores the controller, as it ignores the
    // pointer.
    nfsu::Inspector empty;
    nfsu::GamepadCamera idle;
    idle.step(pad_frame(0, 0, 0, 0, nfsu::kAxisMax, 0), empty);
    idle.step(pad_frame(kStepUs, 0, 0, 0, nfsu::kAxisMax, 0), empty);
    check(!empty.has_scene(), "a camera over no scene stays that way");
}

// ---------------------------------------------------------------------------
// A recorded session, replayed
// ---------------------------------------------------------------------------

// Every hostile case above in one scripted session, for the property all of
// them rest on: the same frames give the same camera, to the bit, every
// time they are replayed and after a trip through the recorded text.
const char* const kSession =
    "nfsu-input 1\n"
    "# connects holding exit and reset: both latched, neither fires\n"
    "0 0 start+select 0 0 0 0\n"
    "# the right stick exactly on the inner edge: centred\n"
    "16000 0 start+select 0 0 3000 0\n"
    "# one unit past it\n"
    "32000 0 start+select 0 0 3001 0\n"
    "# let go: releasing a latch fires nothing\n"
    "48000 0 - 0 0 3001 0\n"
    "# pinned in the corner, beyond the circle\n"
    "64000 0 - 0 0 32767 32767\n"
    "80000 0 - 0 0 32767 32767\n"
    "# kAxisMin, which reads as -32767\n"
    "96000 0 - 0 0 -32768 0\n"
    "# a second controller pushes the other way: they cancel\n"
    "112000 0 - 0 0 32767 0\n"
    "112000 1 - 0 0 -32767 0\n"
    "# both zoom triggers on one controller\n"
    "128000 0 trigger_left+trigger_right 0 0 0 0\n"
    "128000 1 - 0 0 0 0\n"
    "# zoom in on both controllers: clamped to one\n"
    "144000 0 trigger_right 0 0 0 0\n"
    "144000 1 trigger_right 0 0 0 0\n"
    "# the first disappears mid-gesture; the second pans and zooms\n"
    "160000 1 trigger_right 32767 -20000 0 0\n"
    "# two five-second stalls, each clamped to 100 ms: the zoom adds up to\n"
    "# its first whole step on the second\n"
    "5160000 1 trigger_right 32767 -20000 0 0\n"
    "10160000 1 trigger_right 32767 -20000 0 0\n"
    "# the first comes back holding reset: latched\n"
    "10176000 0 select 0 0 0 0\n"
    "10176000 1 - 0 0 0 0\n"
    "# two views on one frame\n"
    "10192000 0 dpad_up+dpad_down 0 0 0 0\n"
    "# nothing connected\n"
    "10208000\n"
    "# back, orbiting and panning together\n"
    "10224000 0 face_south 20000 7000 -25000 31000\n"
    "10240000 0 face_south 20000 7000 -25000 31000\n"
    "10256000 0 shoulder_right 20000 7000 -25000 31000\n";

struct Replay {
    std::string camera;
    std::uint64_t digest = 14695981039346656037ULL;
    std::size_t commands = 0;
};

bool replay(const nfsu::PreparedCarScene& scene, const std::vector<nfsu::GamepadFrame>& frames,
            Replay& out) {
    nfsu::Inspector inspector;
    if (!open_inspector(inspector, scene, 0.35)) {
        return false;
    }
    nfsu::GamepadCamera camera;
    for (const nfsu::GamepadFrame& frame : frames) {
        const nfsu::CameraStep step = camera.step(frame, inspector);
        out.commands += step.commands.size();
        fold(out.digest, inspector.options().camera);
    }
    out.camera = camera_bits(inspector.options().camera);
    return true;
}

void test_recorded_replay() {
    nfsu::PreparedCarScene scene;
    if (!build_scene(scene)) {
        ++g_failures;
        return;
    }
    std::vector<nfsu::GamepadFrame> frames;
    std::string error;
    if (!nfsu::parse_input_recording(kSession, frames, error)) {
        std::cerr << "test session refused: " << error << '\n';
        ++g_failures;
        return;
    }
    check(frames.size() == 19, "the session is 19 frames, not " + std::to_string(frames.size()));

    Replay first;
    Replay second;
    if (!replay(scene, frames, first) || !replay(scene, frames, second)) {
        ++g_failures;
        return;
    }
    check(first.camera == second.camera && first.digest == second.digest,
          "two replays of one recording give one camera, to the bit, on every frame");

    std::vector<nfsu::GamepadFrame> again;
    check(nfsu::parse_input_recording(nfsu::format_input_recording(frames), again, error),
          "the session's canonical text parses");
    Replay third;
    if (!replay(scene, again, third)) {
        ++g_failures;
        return;
    }
    check(third.camera == first.camera && third.digest == first.digest,
          "and replays to the same camera");

    // The golden master. These values are what this implementation produced
    // when the test was written, not values derived by hand; the exact tests
    // above are what argue they are right. What pinning them adds is that a
    // replay on another day, another build or another machine is compared
    // with this one and not only with itself: any change to the dead zone,
    // the mapping, the rates, the order of the Inspector calls or the
    // inspector's own arithmetic moves them. They hold on any platform that
    // evaluates doubles as IEEE binary64 without contraction in these
    // libraries; docs/INPUT.md names the libraries and how it was measured.
    const std::string golden_camera =
        "C0660C4AEC4D8D68 4011EF4455426709 401C71C71C71C71C 3FE1A51699E8084F "
        "3F585684551A16D0 BFE35C54C352170B 4046800000000000 3FA999999999999A "
        "4059000000000000";
    const std::uint64_t golden_digest = 0x7BE849D74BE1CBE8ULL;
    check(first.commands == 3, "the session issues three commands, not " +
                                   std::to_string(first.commands));
    check(first.camera == golden_camera,
          "the replayed camera is the pinned one; it reads " + first.camera);
    char digest[24];
    std::snprintf(digest, sizeof(digest), "%016llX",
                  static_cast<unsigned long long>(first.digest));
    check(first.digest == golden_digest,
          std::string("the per-frame digest is the pinned one; it reads 0x") + digest);
}

} // namespace

int main() {
    test_recording_round_trip();
    test_recording_refusals();
    test_dead_zone();
    test_dead_zone_determinism();
    test_binding_refusals();
    test_mapper_rules();
    test_camera_motion();
    test_camera_hostile();
    test_recorded_replay();

    if (g_failures != 0) {
        std::cerr << g_failures << " input test(s) failed\n";
        return 1;
    }
    std::cout << "input tests passed\n";
    return 0;
}
