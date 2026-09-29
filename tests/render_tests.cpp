#include "nfsu/geometry.h"
#include "nfsu/image.h"
#include "nfsu/render.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

std::uint64_t fnv1a64(const std::string& bytes) {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (unsigned char ch : bytes) {
        hash ^= ch;
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

std::string tga_bytes(const nfsu::Frame& frame) {
    std::ostringstream stream(std::ios::binary);
    std::string error;
    nfsu::write_tga(stream, frame.width(), frame.height(), frame.rgba(), error);
    return stream.str();
}

std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

// File symlink at link pointing to target. MinGW's std::filesystem does not
// implement create_symlink, so Windows uses CreateSymbolicLinkW with the
// unprivileged flag (needs Developer Mode or the symlink privilege).
void make_symlink(const fs::path& target, const fs::path& link, std::error_code& error) {
    error.clear();
#if defined(_WIN32)
    constexpr DWORD kAllowUnprivileged = 0x2;
    if (!CreateSymbolicLinkW(link.c_str(), target.c_str(), kAllowUnprivileged)) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }
#else
    fs::create_symlink(target, link, error);
#endif
}

// Whether a directory entry exists at path, without following a final
// symlink. MinGW's symlink_status follows links, so Windows asks the OS.
bool entry_exists(const fs::path& path) {
#if defined(_WIN32)
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    std::error_code error;
    return fs::symlink_status(path, error).type() != fs::file_type::not_found;
#endif
}

bool is_link(const fs::path& path) {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code error;
    return fs::is_symlink(fs::symlink_status(path, error));
#endif
}

bool is_real_directory(const fs::path& path) {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
    std::error_code error;
    return fs::symlink_status(path, error).type() == fs::file_type::directory;
#endif
}

// Removes one directory entry itself; a symlink is removed, never its
// target. Returns true if the entry is gone.
bool remove_entry(const fs::path& path) {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        RemoveDirectoryW(path.c_str());  // directory or directory link
    } else {
        DeleteFileW(path.c_str());       // file or file symlink
    }
#else
    std::error_code error;
    fs::remove(path, error);
#endif
    return !entry_exists(path);
}

// Removes a directory tree created by the tests without following links.
// Names are collected before anything is removed; depth is capped.
bool remove_tree(const fs::path& path, int depth) {
    if (depth > 16) {
        return false;
    }
    bool ok = true;
    std::vector<fs::path> children;
    std::error_code error;
    for (fs::directory_iterator it(path, error), end; !error && it != end;
         it.increment(error)) {
        children.push_back(it->path());
    }
    if (error) {
        ok = false;
    }
    for (const fs::path& child : children) {
        if (is_real_directory(child)) {
            ok = remove_tree(child, depth + 1) && ok;
        } else {
            ok = remove_entry(child) && ok;
        }
    }
    return remove_entry(path) && ok;
}

// A fresh directory under the system temp directory. The name is only used
// if create_directory reports that this call created it; an existing entry
// is never reused or deleted. The destructor removes exactly that
// directory, so cleanup also runs after failed checks.
class ScratchDir {
public:
    ScratchDir() {
        std::error_code error;
        const fs::path base = fs::temp_directory_path(error);
        if (error) {
            return;
        }
        const std::uint64_t seed =
            static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()) ^
            (static_cast<std::uint64_t>(std::random_device{}()) << 32U);
        for (std::uint64_t attempt = 0; attempt < 64; ++attempt) {
            std::ostringstream name;
            name << "nfsu_render_test_" << std::hex << seed << '_' << counter_++
                 << '_' << attempt;
            const fs::path candidate = base / name.str();
            if (fs::create_directory(candidate, error) && !error) {
                path_ = candidate;
                return;
            }
            error.clear();
        }
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    // Does not use fs::remove_all: MinGW's version follows symlinks for
    // status and loops forever on a dangling link it cannot see. This
    // removal never follows links and makes a single bounded pass.
    ~ScratchDir() {
        if (!path_.empty() && !remove_tree(path_, 0)) {
            std::cerr << "warning: could not fully remove scratch directory "
                      << path_.u8string() << '\n';
        }
    }

    bool ok() const { return !path_.empty(); }
    const fs::path& path() const { return path_; }

private:
    static inline std::uint64_t counter_ = 0;
    fs::path path_;
};

// Frame, options and a rasterizer with an unlit, untextured default.
struct Canvas {
    nfsu::Frame frame;
    nfsu::RenderOptions options;
    nfsu::RenderLimits limits;
    nfsu::RenderStats stats;
    nfsu::RasterMaterial material;

    Canvas(std::uint32_t width, std::uint32_t height) {
        options.width = width;
        options.height = height;
        options.shading = nfsu::ShadingMode::none;
        options.background[0] = 0;
        options.background[1] = 0;
        options.background[2] = 0;
        options.background[3] = 0;
        std::string error;
        check(frame.reset(width, height, options.background, limits, error),
              "canvas reset: " + error);
        material.fallback_pattern = false;
        material.fallback[0] = 200;
        material.fallback[1] = 100;
        material.fallback[2] = 50;
    }

    bool draw(const nfsu::ClipVertex (&v)[3], std::uint32_t id,
              const nfsu::RasterMaterial* override_material = nullptr) {
        nfsu::Rasterizer rasterizer(frame, options, limits, stats);
        std::string error;
        return rasterizer.draw_triangle(
            v, override_material != nullptr ? *override_material : material, id,
            error);
    }

    const unsigned char* pixel(std::uint32_t x, std::uint32_t y) const {
        return &frame.rgba()[(std::size_t{y} * frame.width() + x) * 4U];
    }

    bool covered(std::uint32_t x, std::uint32_t y) const {
        return frame.ids()[std::size_t{y} * frame.width() + x] != nfsu::kNoTriangle;
    }

    std::uint64_t covered_count() const {
        std::uint64_t count = 0;
        for (std::uint32_t id : frame.ids()) {
            count += id != nfsu::kNoTriangle ? 1 : 0;
        }
        return count;
    }
};

// Clip vertex for screen position (sx, sy) in pixels at depth z and w.
nfsu::ClipVertex at(const Canvas& canvas, double sx, double sy,
                    double z = 0.5, double w = 1.0) {
    nfsu::ClipVertex v;
    v.x = (2.0 * sx / canvas.frame.width() - 1.0) * w;
    v.y = (1.0 - 2.0 * sy / canvas.frame.height()) * w;
    v.z = z * w;
    v.w = w;
    return v;
}

nfsu::RenderTexture make_texture(std::uint32_t width, std::uint32_t height) {
    nfsu::RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.valid = true;
    texture.rgba.resize(std::size_t{width} * height * 4U);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            unsigned char* p = &texture.rgba[(std::size_t{y} * width + x) * 4U];
            p[0] = static_cast<unsigned char>(x & 0xFFU);
            p[1] = static_cast<unsigned char>(y & 0xFFU);
            p[2] = 77;
            p[3] = 255;
        }
    }
    return texture;
}

void test_trig() {
    check(nfsu::det_sin_deg(0.0) == 0.0, "sin 0");
    check(nfsu::det_cos_deg(0.0) == 1.0, "cos 0");
    check(nfsu::det_sin_deg(90.0) == 1.0, "sin 90");
    check(nfsu::det_cos_deg(90.0) == 0.0, "cos 90");
    check(nfsu::det_sin_deg(-90.0) == -1.0, "sin -90");
    check(nfsu::det_cos_deg(180.0) == -1.0, "cos 180");
    check(nfsu::det_sin_deg(360.0 + 30.0) == nfsu::det_sin_deg(30.0), "sin periodic");
    check(nfsu::det_cos_deg(-330.0) == nfsu::det_cos_deg(30.0), "cos periodic");
    for (int d = -720; d <= 720; d += 7) {
        const double r = d * 3.14159265358979323846 / 180.0;
        check(std::fabs(nfsu::det_sin_deg(d) - std::sin(r)) < 1e-14,
              "sin accuracy at " + std::to_string(d));
        check(std::fabs(nfsu::det_cos_deg(d) - std::cos(r)) < 1e-14,
              "cos accuracy at " + std::to_string(d));
    }
    check(nfsu::det_sin_deg(std::numeric_limits<double>::quiet_NaN()) == 0.0,
          "sin of NaN is defined");
}

void test_camera() {
    nfsu::OrbitCamera camera;
    camera.target = {0.0, 0.0, 0.0};
    camera.distance = 5.0;
    camera.yaw_deg = 0.0;
    camera.pitch_deg = 0.0;
    nfsu::Vec3 eye = nfsu::orbit_eye(camera);
    check(eye.x == 5.0 && eye.y == 0.0 && eye.z == 0.0, "yaw 0 eye on +X");
    camera.yaw_deg = 90.0;
    eye = nfsu::orbit_eye(camera);
    check(eye.x == 0.0 && eye.y == 0.0 && eye.z == 5.0, "yaw 90 eye on +Z");
    camera.yaw_deg = 0.0;
    camera.pitch_deg = 30.0;
    eye = nfsu::orbit_eye(camera);
    check(std::fabs(eye.y - 2.5) < 1e-14 && eye.y > 0.0, "pitch 30 above");
    camera.yaw_deg = 360.0;
    const nfsu::Vec3 wrapped = nfsu::orbit_eye(camera);
    camera.yaw_deg = 0.0;
    const nfsu::Vec3 plain = nfsu::orbit_eye(camera);
    check(wrapped.x == plain.x && wrapped.y == plain.y && wrapped.z == plain.z,
          "yaw 360 equals yaw 0 exactly");

    std::string error;
    nfsu::OrbitCamera bad;
    check(nfsu::validate_camera(bad, error), "default camera is valid");
    bad.pitch_deg = 90.0;
    check(!nfsu::validate_camera(bad, error), "pitch 90 rejected");
    bad = nfsu::OrbitCamera{};
    bad.pitch_deg = -89.0;
    check(nfsu::validate_camera(bad, error), "pitch -89 accepted");
    bad.distance = std::numeric_limits<double>::quiet_NaN();
    check(!nfsu::validate_camera(bad, error), "NaN distance rejected");
    bad = nfsu::OrbitCamera{};
    bad.distance = 0.0;
    check(!nfsu::validate_camera(bad, error), "zero distance rejected");
    bad = nfsu::OrbitCamera{};
    bad.near_plane = 2.0;
    bad.far_plane = 2.0;
    check(!nfsu::validate_camera(bad, error), "near == far rejected");
    bad = nfsu::OrbitCamera{};
    bad.fov_y_deg = 171.0;
    check(!nfsu::validate_camera(bad, error), "fov 171 rejected");
    bad = nfsu::OrbitCamera{};
    bad.target.y = std::numeric_limits<double>::infinity();
    check(!nfsu::validate_camera(bad, error), "infinite target rejected");
    bad = nfsu::OrbitCamera{};
    bad.yaw_deg = 1e9;
    check(!nfsu::validate_camera(bad, error), "huge yaw rejected");

    // Basis at yaw 0: looking down -X, screen right is -Z (the car's left
    // side, as seen from in front of the car), screen up is +Y.
    camera = nfsu::OrbitCamera{};
    camera.target = {0.0, 0.0, 0.0};
    camera.distance = 5.0;
    camera.yaw_deg = 0.0;
    camera.pitch_deg = 0.0;
    nfsu::CameraFrame frame;
    check(nfsu::make_camera_frame(camera, 100, 50, frame, error), "camera frame");
    check(frame.forward.x == -1.0 && frame.forward.y == 0.0 &&
              frame.forward.z == 0.0, "forward -X");
    check(frame.right.x == 0.0 && frame.right.y == 0.0 && frame.right.z == -1.0,
          "right -Z");
    check(frame.up.x == 0.0 && frame.up.y == 1.0 && frame.up.z == 0.0, "up +Y");
    check(!nfsu::make_camera_frame(camera, 0, 50, frame, error), "zero width rejected");

    // yaw 90 looks at the right side from +Z: the car front (+X) is on the
    // right of the screen.
    camera.yaw_deg = 90.0;
    check(nfsu::make_camera_frame(camera, 100, 100, frame, error), "yaw 90 frame");
    const nfsu::ClipVertex front = nfsu::project_point(frame, {1.0, 0.0, 0.0});
    check(front.x > 0.0, "yaw 90: +X projects right");
    camera.yaw_deg = -90.0;
    check(nfsu::make_camera_frame(camera, 100, 100, frame, error), "yaw -90 frame");
    const nfsu::ClipVertex mirrored = nfsu::project_point(frame, {1.0, 0.0, 0.0});
    check(mirrored.x < 0.0 && mirrored.x == -front.x,
          "yaw -90 is the mirror view of yaw 90");

    // At pitch 6, forward dot forward is the double immediately below 1.
    // MinGW's x87 sqrt rounds it through extended precision to 1, while
    // SSE2 rounds directly to that lower double. Pin the camera basis that
    // keeps the street-view texture coordinate on the same side of a texel.
    camera.yaw_deg = 0.0;
    camera.pitch_deg = 6.0;
    check(nfsu::make_camera_frame(camera, 1280, 720, frame, error),
          "pitch 6 camera frame");
    check(frame.forward.x == -0.9945218953682734 &&
              frame.forward.y == -0.10452846326765348,
          "pitch 6 camera basis uses single-rounded sqrt");
}

void test_projection() {
    nfsu::OrbitCamera camera;
    camera.target = {0.0, 0.0, 0.0};
    camera.distance = 10.0;
    camera.yaw_deg = 0.0;
    camera.pitch_deg = 0.0;
    camera.fov_y_deg = 90.0;
    camera.near_plane = 1.0;
    camera.far_plane = 101.0;
    nfsu::CameraFrame frame;
    std::string error;
    check(nfsu::make_camera_frame(camera, 200, 100, frame, error), "projection frame");
    check(std::fabs(frame.focal_y - 1.0) < 1e-15, "fov 90 focal 1");
    check(std::fabs(frame.focal_x - 0.5) < 1e-15, "aspect 2:1 focal_x 0.5");

    const nfsu::ClipVertex center = nfsu::project_point(frame, {0.0, 0.0, 0.0});
    check(center.x == 0.0 && center.y == 0.0 && center.w == 10.0,
          "target projects to the center at w = distance");
    // 45 degrees up at distance 10: y / w = 1, the top edge of the frame.
    const nfsu::ClipVertex top = nfsu::project_point(frame, {0.0, 10.0, 0.0});
    check(std::fabs(top.y / top.w - 1.0) < 1e-14, "top edge at y/w = 1");
    const nfsu::ClipVertex near_point = nfsu::project_point(frame, {9.0, 0.0, 0.0});
    check(near_point.w == 1.0 && near_point.z == 0.0, "near plane depth 0");
    const nfsu::ClipVertex far_point = nfsu::project_point(frame, {-91.0, 0.0, 0.0});
    check(far_point.w == 101.0 && std::fabs(far_point.z / far_point.w - 1.0) < 1e-15,
          "far plane depth 1");
    const nfsu::ClipVertex mid = nfsu::project_point(frame, {0.0, 0.0, 0.0});
    check(mid.z / mid.w > 0.0 && mid.z / mid.w < 1.0, "depth inside 0..1");
}

nfsu::ClipVertex clip_vertex(double x, double y, double w, double u = 0.0) {
    nfsu::ClipVertex v;
    v.x = x;
    v.y = y;
    v.z = 0.0;
    v.w = w;
    v.u = u;
    return v;
}

void test_near_clipping() {
    nfsu::ClipVertex out[nfsu::kMaxClipVertices];
    const double near_plane = 1.0;
    const nfsu::ClipVertex none[3] = {clip_vertex(0, 0, 0.5), clip_vertex(0.1, 0, 0.2),
                                      clip_vertex(0, 0.1, -3.0)};
    check(nfsu::clip_triangle(none, near_plane, out) == 0, "0 inside: rejected");

    const nfsu::ClipVertex one[3] = {clip_vertex(0, 0, 3.0, 1.0),
                                     clip_vertex(0.5, 0, 0.0, 0.0),
                                     clip_vertex(0, 0.5, -1.0, 0.0)};
    std::size_t count = nfsu::clip_triangle(one, near_plane, out);
    check(count == 3, "1 inside: one triangle");
    for (std::size_t i = 0; i < count; ++i) {
        check(out[i].w >= near_plane, "1 inside: all w >= near");
    }
    // Edge from w=3 (u=1) to w=0 (u=0) crosses w=1 at t = 2/3.
    check(std::fabs(out[1].w - 1.0) < 1e-15 && std::fabs(out[1].u - 1.0 / 3.0) < 1e-15,
          "1 inside: attributes interpolated in clip space");

    const nfsu::ClipVertex two[3] = {clip_vertex(0, 0, 3.0), clip_vertex(0.5, 0, 2.0),
                                     clip_vertex(0, 0.5, 0.0)};
    count = nfsu::clip_triangle(two, near_plane, out);
    check(count == 4, "2 inside: quad (two triangles)");
    for (std::size_t i = 0; i < count; ++i) {
        check(out[i].w >= near_plane, "2 inside: all w >= near");
    }

    const nfsu::ClipVertex all[3] = {clip_vertex(0, 0, 3.0), clip_vertex(0.5, 0, 2.0),
                                     clip_vertex(0, 0.5, 1.0)};
    count = nfsu::clip_triangle(all, near_plane, out);
    check(count == 3 && out[2].w == 1.0 && out[0].x == 0.0, "3 inside, w == near kept");

    // A shared edge gives bit-identical intersections in both triangles,
    // even though they traverse it in opposite directions.
    const nfsu::ClipVertex p = clip_vertex(0.3, 0.1, 5.0, 0.25);
    const nfsu::ClipVertex q = clip_vertex(-0.7, 0.2, 0.1, 0.75);
    const nfsu::ClipVertex left[3] = {p, q, clip_vertex(0, 1.0, 4.0)};
    const nfsu::ClipVertex right[3] = {q, p, clip_vertex(0, -1.0, 4.0)};
    nfsu::ClipVertex out_left[nfsu::kMaxClipVertices];
    nfsu::ClipVertex out_right[nfsu::kMaxClipVertices];
    const std::size_t nl = nfsu::clip_triangle(left, near_plane, out_left);
    const std::size_t nr = nfsu::clip_triangle(right, near_plane, out_right);
    bool found = false;
    for (std::size_t i = 0; i < nl; ++i) {
        for (std::size_t j = 0; j < nr; ++j) {
            if (out_left[i].w == near_plane && out_right[j].w == near_plane &&
                out_left[i].x == out_right[j].x && out_left[i].y == out_right[j].y &&
                out_left[i].u == out_right[j].u) {
                found = true;
            }
        }
    }
    check(found, "shared edge intersection is identical");

    // Guard band: huge x is clipped to |x| <= 8 w.
    const nfsu::ClipVertex wide[3] = {clip_vertex(-1e30, 0, 1.0), clip_vertex(1e30, 0, 1.0),
                                      clip_vertex(0, 1.0, 1.0)};
    count = nfsu::clip_triangle(wide, near_plane, out);
    check(count >= 3, "huge triangle survives the guard band");
    for (std::size_t i = 0; i < count; ++i) {
        check(std::fabs(out[i].x) <= nfsu::kGuardBand * out[i].w * (1.0 + 1e-12),
              "guard band bound");
    }
    const nfsu::ClipVertex bad[3] = {clip_vertex(std::numeric_limits<double>::quiet_NaN(), 0, 2.0),
                                     clip_vertex(0, 0, 2.0), clip_vertex(0, 1, 2.0)};
    check(nfsu::clip_triangle(bad, near_plane, out) == 0, "NaN vertex rejected");
}

void test_viewport_bounds() {
    Canvas canvas(13, 7);
    // Covers far more than the frame in every direction.
    const nfsu::ClipVertex big[3] = {at(canvas, -40, -40), at(canvas, 200, -40),
                                     at(canvas, -40, 200)};
    check(canvas.draw(big, 1), "big triangle drawn");
    check(canvas.covered_count() == 13U * 7U, "big triangle covers the frame exactly");
    check(canvas.stats.fragments == 13U * 7U, "no fragment outside the frame");

    Canvas off(8, 8);
    const nfsu::ClipVertex outside[3] = {at(off, 20, 1), at(off, 30, 1), at(off, 20, 7)};
    check(off.draw(outside, 1), "offscreen triangle");
    check(off.covered_count() == 0 && off.stats.pixel_tests == 0,
          "offscreen triangle tests no pixels");

    Canvas partial(8, 8);
    const nfsu::ClipVertex half[3] = {at(partial, 4, -10), at(partial, 30, -10),
                                      at(partial, 4, 30)};
    check(partial.draw(half, 1), "partial triangle");
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            check(partial.covered(x, y) == (x >= 4), "partial triangle column rule");
        }
    }
}

void test_fill_rule_exact() {
    // Right triangle (0,0) (8,0) (0,8): centers with i + j <= 6 are inside;
    // centers on the hypotenuse (i + j == 7) belong to the right/bottom
    // edge and are excluded.
    Canvas canvas(8, 8);
    const nfsu::ClipVertex tri[3] = {at(canvas, 0, 0), at(canvas, 0, 8), at(canvas, 8, 0)};
    check(canvas.draw(tri, 1), "exact triangle");
    const char* expected[8] = {
        "#######.", "######..", "#####...", "####....",
        "###.....", "##......", "#.......", "........"};
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            check(canvas.covered(x, y) == (expected[y][x] == '#'),
                  "fill rule pixel " + std::to_string(x) + "," + std::to_string(y));
        }
    }
    check(canvas.pixel(0, 0)[0] == 200 && canvas.pixel(0, 0)[1] == 100 &&
              canvas.pixel(0, 0)[2] == 50 && canvas.pixel(0, 0)[3] == 255,
          "fallback color written with alpha 255");
    check(canvas.pixel(7, 7)[3] == 0, "background untouched");

    // Horizontal edges on sample rows: top included, bottom excluded.
    Canvas rows(8, 8);
    const nfsu::ClipVertex r0[3] = {at(rows, 0, 2.5), at(rows, 0, 6.5), at(rows, 8, 6.5)};
    const nfsu::ClipVertex r1[3] = {at(rows, 0, 2.5), at(rows, 8, 6.5), at(rows, 8, 2.5)};
    check(rows.draw(r0, 1) && rows.draw(r1, 2), "row rectangle");
    for (std::uint32_t y = 0; y < 8; ++y) {
        check(rows.covered(3, y) == (y >= 2 && y <= 5), "row rule at y " + std::to_string(y));
    }
}

void test_shared_edges() {
    // Quad split along a diagonal through pixel centers: every pixel is
    // covered exactly once.
    for (int reverse = 0; reverse < 2; ++reverse) {
        Canvas canvas(16, 16);
        const nfsu::ClipVertex a = at(canvas, 0, 0);
        const nfsu::ClipVertex b = at(canvas, 16, 0);
        const nfsu::ClipVertex c = at(canvas, 16, 16);
        const nfsu::ClipVertex d = at(canvas, 0, 16);
        const nfsu::ClipVertex t0[3] = {a, b, c};
        const nfsu::ClipVertex t1a[3] = {a, c, d};
        const nfsu::ClipVertex t1b[3] = {a, d, c};
        check(canvas.draw(t0, 1) && canvas.draw(reverse ? t1b : t1a, 2), "quad");
        check(canvas.stats.fragments == 256, "quad: each pixel covered exactly once");
        check(canvas.covered_count() == 256, "quad: no holes");
    }

    // Eight triangles fanned around the center of pixel (8, 8).
    Canvas fan(16, 16);
    const double cx = 8.5;
    const double cy = 8.5;
    const double ring[8][2] = {{2.5, 2.5}, {8.5, 1.0}, {14.5, 2.5}, {15.0, 8.5},
                               {14.5, 14.5}, {8.5, 15.0}, {2.5, 14.5}, {1.0, 8.5}};
    for (int i = 0; i < 8; ++i) {
        const nfsu::ClipVertex t[3] = {at(fan, cx, cy), at(fan, ring[i][0], ring[i][1]),
                                       at(fan, ring[(i + 1) % 8][0], ring[(i + 1) % 8][1])};
        check(fan.draw(t, static_cast<std::uint32_t>(i)), "fan triangle");
    }
    check(fan.stats.fragments == fan.covered_count(), "fan: no pixel covered twice");
    check(fan.covered(8, 8), "fan: center pixel covered");
}

void test_winding_and_culling() {
    Canvas canvas(8, 8);
    // Counter-clockwise as seen on screen: the source front face.
    const nfsu::ClipVertex ccw[3] = {at(canvas, 1, 1), at(canvas, 1, 7), at(canvas, 7, 7)};
    const nfsu::ClipVertex cw[3] = {at(canvas, 1, 1), at(canvas, 7, 7), at(canvas, 1, 7)};
    check(canvas.draw(ccw, 1), "ccw");
    check(canvas.stats.triangles_front == 1 && canvas.stats.triangles_back == 0,
          "ccw is front");
    const std::vector<unsigned char> image = canvas.frame.rgba();

    Canvas other(8, 8);
    check(other.draw(cw, 1), "cw");
    check(other.stats.triangles_back == 1, "cw is back");
    check(other.frame.rgba() == image, "both sides render identically without culling");

    Canvas culled(8, 8);
    culled.options.cull = nfsu::CullMode::back;
    check(culled.draw(cw, 1) && culled.draw(ccw, 2), "cull back");
    check(culled.stats.triangles_culled == 1 && culled.stats.triangles_rasterized == 1,
          "cull back drops one");
    check(culled.frame.rgba() == image, "cull back keeps the front face");

    Canvas front(8, 8);
    front.options.cull = nfsu::CullMode::front;
    check(front.draw(ccw, 1), "cull front");
    check(front.covered_count() == 0 && front.stats.triangles_culled == 1,
          "cull front drops the front face");
}

void test_degenerate() {
    Canvas canvas(8, 8);
    const nfsu::ClipVertex line[3] = {at(canvas, 1, 1), at(canvas, 4, 4), at(canvas, 7, 7)};
    const nfsu::ClipVertex point[3] = {at(canvas, 3, 3), at(canvas, 3, 3), at(canvas, 3, 3)};
    const nfsu::ClipVertex tiny[3] = {at(canvas, 3.1, 3.1), at(canvas, 3.1, 3.2),
                                      at(canvas, 3.2, 3.2)};
    check(canvas.draw(line, 1) && canvas.draw(point, 2) && canvas.draw(tiny, 3),
          "degenerate draws");
    check(canvas.stats.triangles_degenerate_screen == 2, "line and point degenerate");
    check(canvas.stats.triangles_rasterized == 1, "tiny triangle rasterized");
    check(canvas.covered_count() == 0, "degenerate triangles draw nothing");
}

void test_depth_order() {
    const auto render = [](bool swap) {
        Canvas canvas(8, 8);
        nfsu::RasterMaterial near_material = canvas.material;
        near_material.fallback[0] = 10;
        nfsu::RasterMaterial far_material = canvas.material;
        far_material.fallback[0] = 250;
        const nfsu::ClipVertex near_tri[3] = {at(canvas, 0, 0, 0.2), at(canvas, 0, 8, 0.2),
                                              at(canvas, 8, 8, 0.2)};
        const nfsu::ClipVertex far_tri[3] = {at(canvas, 0, 0, 0.7), at(canvas, 0, 8, 0.7),
                                             at(canvas, 8, 0, 0.7)};
        if (swap) {
            canvas.draw(far_tri, 1, &far_material);
            canvas.draw(near_tri, 2, &near_material);
        } else {
            canvas.draw(near_tri, 2, &near_material);
            canvas.draw(far_tri, 1, &far_material);
        }
        return canvas.frame.rgba();
    };
    const std::vector<unsigned char> a = render(false);
    const std::vector<unsigned char> b = render(true);
    check(a == b, "depth result independent of draw order");
    check(a[(2 * 8 + 1) * 4] == 10, "nearer triangle wins");
    check(a[(1 * 8 + 5) * 4] == 250, "far triangle visible where uncovered");

    // Equal depth: the larger id wins in any order.
    const auto coplanar = [](bool swap) {
        Canvas canvas(8, 8);
        nfsu::RasterMaterial m5 = canvas.material;
        m5.fallback[1] = 5;
        nfsu::RasterMaterial m9 = canvas.material;
        m9.fallback[1] = 9;
        const nfsu::ClipVertex tri[3] = {at(canvas, 0, 0), at(canvas, 0, 8), at(canvas, 8, 8)};
        if (swap) {
            canvas.draw(tri, 9, &m9);
            canvas.draw(tri, 5, &m5);
        } else {
            canvas.draw(tri, 5, &m5);
            canvas.draw(tri, 9, &m9);
        }
        return canvas.frame.rgba();
    };
    const std::vector<unsigned char> c = coplanar(false);
    check(c == coplanar(true), "coplanar tie independent of draw order");
    check(c[(7 * 8 + 1) * 4 + 1] == 9, "coplanar tie: larger id wins");

    // Beyond the far plane is discarded.
    Canvas far_canvas(8, 8);
    const nfsu::ClipVertex beyond[3] = {at(far_canvas, 0, 0, 1.5), at(far_canvas, 0, 8, 1.5),
                                        at(far_canvas, 8, 8, 1.5)};
    check(far_canvas.draw(beyond, 1), "beyond far");
    check(far_canvas.covered_count() == 0 && far_canvas.stats.fragments_far_discarded > 0,
          "beyond far discarded");
}

void test_perspective_uv() {
    // Full-screen quad whose left edge is at w = 1 and right edge at w = 4,
    // u from 0 to 1. Perspective-correct u at screen fraction s is
    // (s / 4) / ((1 - s) + s / 4), not s.
    Canvas canvas(64, 4);
    const nfsu::RenderTexture texture = make_texture(256, 1);
    nfsu::RasterMaterial material;
    material.texture = &texture;
    const nfsu::ClipVertex lt = clip_vertex(-1, 1, 1, 0);
    const nfsu::ClipVertex lb = clip_vertex(-1, -1, 1, 0);
    const nfsu::ClipVertex rt = clip_vertex(4, 4, 4, 1);
    const nfsu::ClipVertex rb = clip_vertex(4, -4, 4, 1);
    const nfsu::ClipVertex t0[3] = {lt, lb, rb};
    const nfsu::ClipVertex t1[3] = {lt, rb, rt};
    check(canvas.draw(t0, 1, &material) && canvas.draw(t1, 2, &material), "perspective quad");
    for (std::uint32_t x = 0; x < 64; ++x) {
        const double s = (x + 0.5) / 64.0;
        const double u = (s / 4.0) / ((1.0 - s) + s / 4.0);
        const int expected = static_cast<int>(std::floor(u * 256.0));
        const int got = canvas.pixel(x, 1)[0];
        check(std::abs(got - expected) <= 1,
              "perspective u at column " + std::to_string(x) + ": got " +
                  std::to_string(got) + " expected " + std::to_string(expected));
    }
    check(canvas.pixel(32, 1)[0] < 60, "middle is not affine (which would be 128)");
}

void test_uv_wrap_and_flip() {
    const nfsu::RenderTexture texture = make_texture(4, 4);
    const auto sample = [&](double u, double v, bool flip) {
        Canvas canvas(4, 4);
        canvas.options.flip_v = flip;
        nfsu::RasterMaterial material;
        material.texture = &texture;
        nfsu::ClipVertex a = at(canvas, -2, -2);
        nfsu::ClipVertex b = at(canvas, -2, 20);
        nfsu::ClipVertex c = at(canvas, 20, -2);
        for (nfsu::ClipVertex* p : {&a, &b, &c}) {
            p->u = u;
            p->v = v;
        }
        const nfsu::ClipVertex tri[3] = {a, b, c};
        canvas.draw(tri, 1, &material);
        return std::pair<int, int>(canvas.pixel(0, 0)[0], canvas.pixel(0, 0)[1]);
    };
    check(sample(0.1, 0.1, false) == std::pair<int, int>(0, 0), "in-range uv");
    check(sample(0.6, 0.3, false) == std::pair<int, int>(2, 1), "in-range uv 2");
    check(sample(-0.25, -0.75, false) == std::pair<int, int>(3, 1), "negative uv wraps");
    check(sample(209.18, -39.68, false) == std::pair<int, int>(0, 1), "large uv wraps");
    check(sample(1e20, -1e20, false) == std::pair<int, int>(0, 0), "huge uv wraps to 0");
    check(sample(-1e-17, 2.1, false) == std::pair<int, int>(3, 0), "tiny negative u stays in range");
    check(sample(0.1, 0.1, true) == std::pair<int, int>(0, 3), "flip v uses 1 - v");
    check(sample(0.1, 0.9, true) == std::pair<int, int>(0, 0), "flip v 0.9");
}

void test_alpha() {
    nfsu::RenderTexture texture = make_texture(2, 1);
    texture.rgba[3] = 127;
    texture.rgba[7] = 128;
    const auto run = [&](std::uint8_t cutoff, nfsu::RenderStats& stats) {
        Canvas canvas(2, 1);
        canvas.options.alpha_cutoff = cutoff;
        nfsu::RasterMaterial material;
        material.texture = &texture;
        nfsu::ClipVertex a = at(canvas, 0, -4);
        nfsu::ClipVertex b = at(canvas, 0, 5);
        nfsu::ClipVertex c = at(canvas, 2, -4);
        nfsu::ClipVertex d = at(canvas, 2, 5);
        a.u = b.u = 0.0;
        c.u = d.u = 1.0;
        const nfsu::ClipVertex t0[3] = {a, b, d};
        const nfsu::ClipVertex t1[3] = {a, d, c};
        canvas.draw(t0, 1, &material);
        canvas.draw(t1, 2, &material);
        stats = canvas.stats;
        return std::pair<bool, bool>(canvas.covered(0, 0), canvas.covered(1, 0));
    };
    nfsu::RenderStats stats;
    check(run(128, stats) == std::pair<bool, bool>(false, true), "alpha 127 discarded, 128 kept");
    check(stats.fragments_alpha_discarded == 1, "alpha discard counted");
    check(run(0, stats) == std::pair<bool, bool>(true, true), "cutoff 0 keeps all");
    check(run(255, stats) == std::pair<bool, bool>(false, false), "cutoff 255 drops both");
}

void test_vertex_color_and_fallback() {
    unsigned char c1[4];
    unsigned char c2[4];
    unsigned char c3[4];
    nfsu::fallback_color(0x77A20245U, c1);
    nfsu::fallback_color(0x77A20245U, c2);
    nfsu::fallback_color(0xBD1F0E0AU, c3);
    check(std::memcmp(c1, c2, 4) == 0, "fallback color deterministic");
    check(std::memcmp(c1, c3, 3) != 0, "fallback colors differ per hash");
    for (int k = 0; k < 3; ++k) {
        check(c1[k] >= 64 && c1[k] <= 223 && c3[k] >= 64 && c3[k] <= 223,
              "fallback channel range");
    }
    check(c1[3] == 255, "fallback opaque");

    nfsu::RenderTexture white = make_texture(1, 1);
    white.rgba = {255, 255, 255, 255};
    const auto run = [&](bool vertex_color) {
        Canvas canvas(2, 2);
        canvas.options.vertex_color = vertex_color;
        nfsu::RasterMaterial material;
        material.texture = &white;
        nfsu::ClipVertex a = at(canvas, -2, -2);
        nfsu::ClipVertex b = at(canvas, -2, 9);
        nfsu::ClipVertex c = at(canvas, 9, -2);
        for (nfsu::ClipVertex* p : {&a, &b, &c}) {
            p->r = 255;
            p->g = 128;
            p->b = 0;
        }
        const nfsu::ClipVertex tri[3] = {a, b, c};
        canvas.draw(tri, 1, &material);
        const unsigned char* p = canvas.pixel(1, 1);
        return std::vector<int>{p[0], p[1], p[2]};
    };
    check(run(true) == std::vector<int>{255, 128, 0}, "vertex color modulates");
    check(run(false) == std::vector<int>{255, 255, 255}, "vertex color off by default");

    // Fallback checker: 8 cells per UV unit, darker cells at 3/4.
    Canvas canvas(16, 1);
    canvas.material.fallback_pattern = true;
    nfsu::ClipVertex a = at(canvas, 0, -4);
    nfsu::ClipVertex b = at(canvas, 0, 5);
    nfsu::ClipVertex c = at(canvas, 16, -4);
    nfsu::ClipVertex d = at(canvas, 16, 5);
    c.u = d.u = 1.0;
    const nfsu::ClipVertex t0[3] = {a, b, d};
    const nfsu::ClipVertex t1[3] = {a, d, c};
    canvas.draw(t0, 1);
    canvas.draw(t1, 2);
    check(canvas.pixel(0, 0)[0] == 200 && canvas.pixel(2, 0)[0] == 150 &&
              canvas.pixel(4, 0)[0] == 200,
          "fallback checker pattern");

    // Flat shade factor.
    Canvas shaded(2, 2);
    shaded.material.shade = 128;
    const nfsu::ClipVertex full[3] = {at(shaded, -2, -2), at(shaded, -2, 9), at(shaded, 9, -2)};
    shaded.draw(full, 1);
    check(shaded.pixel(0, 0)[0] == 100 && shaded.pixel(0, 0)[1] == 50 &&
              shaded.pixel(0, 0)[2] == 25,
          "shade 128 halves color");

    const auto lamp_pixel = [](std::uint8_t mode) {
        Canvas lamp(2, 2);
        lamp.material.lamp_mode = mode;
        const nfsu::ClipVertex tri[3] = {at(lamp, -2, -2), at(lamp, -2, 9),
                                         at(lamp, 9, -2)};
        lamp.draw(tri, 1);
        const unsigned char* p = lamp.pixel(0, 0);
        return std::array<int, 3>{p[0], p[1], p[2]};
    };
    check(lamp_pixel(0) == std::array<int, 3>{200, 100, 50} &&
              lamp_pixel(1) == std::array<int, 3>{226, 173, 138} &&
              lamp_pixel(2) == std::array<int, 3>{211, 102, 52} &&
              lamp_pixel(3) == std::array<int, 3>{241, 117, 64},
          "head, tail and braking lamp modes affect the reference pixel");
}

nfsu::MeshVertex vertex(float px, float py, float pz, float u = 0.0F, float v = 0.0F) {
    nfsu::MeshVertex out;
    out.px = px;
    out.py = py;
    out.pz = pz;
    out.u = u;
    out.v = v;
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
    return out;
}

nfsu::MeshMaterial material_range(std::uint32_t first, std::uint32_t count,
                                   std::uint32_t slot, std::uint32_t flags = 0) {
    nfsu::MeshMaterial material;
    material.first_index = first;
    material.index_count = count;
    material.texture_slot = slot;
    material.flags = flags;
    const auto store = [](unsigned char* out, std::uint32_t value) {
        out[0] = static_cast<unsigned char>(value);
        out[1] = static_cast<unsigned char>(value >> 8U);
        out[2] = static_cast<unsigned char>(value >> 16U);
        out[3] = static_cast<unsigned char>(value >> 24U);
    };
    store(material.raw + 12, material.index_count);
    store(material.raw + 28, material.texture_slot);
    store(material.raw + 52, material.first_index);
    store(material.raw + 56, material.flags);
    return material;
}

// Two quads facing +X (the front) at source pz = +0.5 (the car's left
// side) and pz = -0.5 (right side). Group 0 uses a pack texture, group 1 a
// hash that is not in the pack.
nfsu::GeometryReport synthetic_scene() {
    nfsu::GeometryReport report;
    nfsu::Mesh mesh;
    mesh.index = 0;
    mesh.name = "TEST_MESH";
    mesh.valid = true;
    mesh.vertices = {
        vertex(0, 0, 0.1F, 0, 0), vertex(0, 1, 0.1F, 0, 1), vertex(0, 1, 0.9F, 1, 1),
        vertex(0, 0, 0.9F, 1, 0),
        vertex(0, 0, -0.9F, 0, 0), vertex(0, 1, -0.9F, 0, 1), vertex(0, 1, -0.1F, 1, 1),
        vertex(0, 0, -0.1F, 1, 0)};
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}, {1, 1, 2}};
    mesh.texture_hashes = {0x11111111U, 0x22222222U};
    mesh.materials = {material_range(0, 6, 0), material_range(6, 9, 1)};
    report.meshes.push_back(mesh);

    nfsu::Mesh broken;
    broken.index = 1;
    broken.name = "BROKEN";
    broken.valid = false;
    report.meshes.push_back(broken);
    return report;
}

nfsu::RenderTextureSet synthetic_textures() {
    nfsu::RenderTextureSet set;
    nfsu::RenderTexture a = make_texture(4, 4);
    a.hash = 0x11111111U;
    set.textures.push_back(a);
    nfsu::RenderTexture dup = make_texture(2, 2);
    dup.hash = 0x33333333U;
    set.textures.push_back(dup);
    set.textures.push_back(dup);
    nfsu::RenderTexture invalid;
    invalid.hash = 0x44444444U;
    set.textures.push_back(invalid);
    return set;
}

nfsu::RenderOptions scene_options() {
    nfsu::RenderOptions options;
    options.width = 32;
    options.height = 24;
    options.camera.target = {0.0, 0.5, 0.0};
    options.camera.yaw_deg = 0.0;
    options.camera.pitch_deg = 0.0;
    options.camera.distance = 4.0;
    options.camera.fov_y_deg = 40.0;
    return options;
}

// The all-meshes profile; tests/scene_tests.cpp covers selection.
nfsu::PreparedCarScene prepare_all(const nfsu::GeometryReport& geometry,
                                   const nfsu::RenderTextureSet& textures) {
    nfsu::PreparedCarScene scene;
    std::string error;
    check(nfsu::prepare_car_scene(geometry, textures, nfsu::MeshSelection{},
                                  nfsu::SceneLimits{}, scene, error),
          "prepare all meshes: " + error);
    return scene;
}

// Retail material state. Every rule below is recovered in
// docs/MATERIALS.md, and no test here reads a retail asset.

void test_material_state_decoders() {
    // The prepared-scene layer decodes the two raw texture-info bytes on
    // its own, because it must not depend on the pack reader. Both
    // decoders therefore have to agree for every byte value.
    bool same = true;
    for (unsigned int raw = 0; raw < 256U; ++raw) {
        nfsu::TextureRecord record;
        record.alpha_test_mode = static_cast<std::uint8_t>(raw);
        record.alpha_blend_mode = static_cast<std::uint8_t>(raw);
        const std::string scene_name = nfsu::render_blend_mode_name(
            nfsu::render_blend_mode_of(record.alpha_blend_mode));
        const std::string pack_name =
            nfsu::texture_alpha_blend_name(nfsu::texture_alpha_blend(record));
        same = same &&
               nfsu::render_alpha_test_enabled(record.alpha_test_mode) ==
                   nfsu::texture_alpha_test_enabled(record) &&
               scene_name == pack_name;
    }
    check(same, "scene and pack alpha state decoders agree on all 256 values");
    check(nfsu::render_alpha_test_enabled(1) &&
              !nfsu::render_alpha_test_enabled(0) &&
              !nfsu::render_alpha_test_enabled(2) &&
              !nfsu::render_alpha_test_enabled(255),
          "only +85 == 1 enables the alpha test");
    check(nfsu::render_blend_mode_of(0) == nfsu::RenderBlendMode::disabled &&
              nfsu::render_blend_mode_of(1) == nfsu::RenderBlendMode::source_over &&
              nfsu::render_blend_mode_of(2) == nfsu::RenderBlendMode::additive &&
              nfsu::render_blend_mode_of(6) == nfsu::RenderBlendMode::source_over &&
              nfsu::render_blend_mode_of(255) == nfsu::RenderBlendMode::source_over,
          "+86 selects the blend and every other nonzero value is source-over");
    check(nfsu::kRenderAlphaRef == 0xAAU, "the alpha reference is 0xAA");
    // The draw queue row, and the same duplication rule as above.
    bool rows_agree = true;
    for (unsigned int group = 0; group < 256U && rows_agree; ++group) {
        for (unsigned int order = 0; order < 256U; ++order) {
            nfsu::TextureRecord record;
            record.layer_group = static_cast<std::uint8_t>(group);
            record.layer_order = static_cast<std::uint8_t>(order);
            if (nfsu::render_draw_layer(record.layer_group, record.layer_order) !=
                nfsu::texture_draw_layer(record)) {
                rows_agree = false;
                break;
            }
        }
    }
    check(rows_agree, "scene and pack draw layer decoders agree on all 65536 pairs");
    check(nfsu::kRenderDrawLayerCount == 16, "the retail bucket table has 16 rows");
    // The four rows the retail 240SX pack uses.
    check(nfsu::render_draw_layer(0, 0) == 0 && nfsu::render_draw_layer(0, 5) == 5 &&
              nfsu::render_draw_layer(1, 0) == 8 &&
              nfsu::render_draw_layer(1, 5) == 13,
          "the retail 240SX rows are 0, 5, 8 and 13");
    check(nfsu::render_draw_layer(0, 0xFFU) == 0, "a row below the table is clamped up");
    check(nfsu::render_draw_layer(1, 0x7FU) == nfsu::kRenderDrawLayerCount - 1,
          "a row past the table is clamped down");
    check(std::string(nfsu::material_state_mode_name(
              nfsu::MaterialStateMode::off)) == "off" &&
              std::string(nfsu::material_state_mode_name(
                  nfsu::MaterialStateMode::retail)) == "retail",
          "material state mode names");
}

void test_material_gates() {
    // The branch bits are used only as the retail upload tests them. The
    // result string is diffuse, specular, environment.
    const auto gates = [](std::uint32_t flags, bool global) {
        const nfsu::MaterialCoefficientGates g =
            nfsu::material_coefficient_gates(flags, global);
        return std::string(g.diffuse ? "d" : "-") + (g.specular ? "s" : "-") +
               (g.environment ? "e" : "-");
    };
    check(gates(0x00000000U, true) == "ds-", "no environment branch bit");
    check(gates(0x00000080U, true) == "ds-", "the other bit alone keeps no environment");
    check(gates(0x00000180U, false) == "ds-", "the global off zeros environment");
    check(gates(0x00000100U, true) == "--e", "environment kept, diffuse and specular zeroed");
    check(gates(0x00000180U, true) == "dse", "all three groups survive");
    check(gates(0xFFFFFF7FU, true) == "--e", "only the two branch bits matter");
    check(gates(0xFFFFFFFFU, true) == "dse", "every other bit is ignored");
    // 0x00004180 is 922 of the 1242 records of the 240SX geometry and has
    // both branch bits, so every coefficient group survives for it.
    check(gates(0x00004180U, true) == "dse",
          "the most common retail flag value keeps all three groups");
    check(gates(0x00018000U, true) == "ds-",
          "a retail flag value without the environment branch bit");
    // The traced branch zeros the diffuse and the specular vectors
    // together, so the two can never disagree.
    bool paired = true;
    for (int bit = 0; bit < 32 && paired; ++bit) {
        for (int extra = 0; extra < 4; ++extra) {
            const std::uint32_t flags =
                (1U << bit) | (static_cast<std::uint32_t>(extra) * 0x80U);
            for (int global = 0; global < 2; ++global) {
                const nfsu::MaterialCoefficientGates g =
                    nfsu::material_coefficient_gates(flags, global != 0);
                paired = paired && g.diffuse == g.specular;
            }
        }
    }
    check(paired, "the diffuse and specular gates are always equal");
}

void test_material_alpha_test() {
    // D3DCMP_GREATER against 0xAA, applied to the source alpha after the
    // undecoded diffuse alpha lane has scaled it.
    nfsu::RenderTexture texture = make_texture(2, 1);
    texture.rgba[3] = 0xAA;
    texture.rgba[7] = 0xAB;
    const auto run = [&](bool enabled, double scale, nfsu::RenderStats& stats) {
        Canvas canvas(2, 1);
        nfsu::RasterMaterial material;
        material.texture = &texture;
        material.alpha_test = enabled;
        material.alpha_scale = scale;
        nfsu::ClipVertex a = at(canvas, 0, -4);
        nfsu::ClipVertex b = at(canvas, 0, 5);
        nfsu::ClipVertex c = at(canvas, 2, -4);
        nfsu::ClipVertex d = at(canvas, 2, 5);
        a.u = b.u = 0.0;
        c.u = d.u = 1.0;
        const nfsu::ClipVertex t0[3] = {a, b, d};
        const nfsu::ClipVertex t1[3] = {a, d, c};
        canvas.draw(t0, 1, &material);
        canvas.draw(t1, 2, &material);
        stats = canvas.stats;
        return std::pair<bool, bool>(canvas.covered(0, 0), canvas.covered(1, 0));
    };
    nfsu::RenderStats stats;
    check(run(true, 1.0, stats) == std::pair<bool, bool>(false, true),
          "alpha 0xAA fails GREATER and 0xAB passes");
    check(stats.fragments_alpha_discarded == 1, "the retail alpha discard is counted");
    check(run(false, 1.0, stats) == std::pair<bool, bool>(true, true),
          "the test off keeps both");
    check(stats.fragments_alpha_discarded == 0, "nothing discarded with the test off");
    check(run(true, 2.0, stats) == std::pair<bool, bool>(true, true),
          "an alpha lane above 1 lifts both past the reference");
    check(run(true, 0.5, stats) == std::pair<bool, bool>(false, false),
          "an alpha lane below 1 drops both");
}

void test_car_linear_repeat_sample() {
    nfsu::RenderTexture texture = make_texture(2, 1);
    texture.rgba = {255, 0, 0, 255, 0, 0, 255, 255};
    const auto sample = [&](double u) {
        Canvas canvas(1, 1);
        nfsu::RasterMaterial material;
        material.texture = &texture;
        material.linear_filter = true;
        nfsu::ClipVertex tri[3] = {at(canvas, -2, -2), at(canvas, -2, 4),
                                    at(canvas, 4, -2)};
        for (auto& vertex : tri) {
            vertex.u = u;
            vertex.v = 0.5;
        }
        canvas.draw(tri, 1, &material);
        return std::array<unsigned, 3>{canvas.pixel(0, 0)[0],
                                       canvas.pixel(0, 0)[1],
                                       canvas.pixel(0, 0)[2]};
    };
    check(sample(0.0) == std::array<unsigned, 3>{128, 0, 128} &&
          sample(1.0) == sample(0.0),
          "car linear filter blends across the repeat seam");
    check(sample(0.25) == std::array<unsigned, 3>{255, 0, 0} &&
          sample(0.75) == std::array<unsigned, 3>{0, 0, 255},
          "car linear filter uses texel centres");
}

// Draws an opaque destination, then one or two blended triangles over it.
struct BlendCase {
    std::vector<int> pixel;
    nfsu::RenderStats stats;
};

BlendCase blend_over(nfsu::RenderBlendMode mode, const unsigned char (&src)[4],
                     double z, bool second, const unsigned char (&src2)[4],
                     double z2 = 0.1) {
    Canvas canvas(1, 1);
    nfsu::RenderTexture back = make_texture(1, 1);
    back.rgba = {200, 100, 50, 255};
    nfsu::RenderTexture front = make_texture(1, 1);
    front.rgba = {src[0], src[1], src[2], src[3]};
    nfsu::RenderTexture front2 = make_texture(1, 1);
    front2.rgba = {src2[0], src2[1], src2[2], src2[3]};
    nfsu::RasterMaterial opaque;
    opaque.texture = &back;
    nfsu::RasterMaterial blended;
    blended.texture = &front;
    blended.blend = mode;
    nfsu::RasterMaterial blended2 = blended;
    blended2.texture = &front2;
    const auto tri = [&canvas](double depth, nfsu::ClipVertex (&out)[3]) {
        out[0] = at(canvas, -2, -2, depth);
        out[1] = at(canvas, -2, 4, depth);
        out[2] = at(canvas, 4, -2, depth);
    };
    nfsu::ClipVertex o[3];
    nfsu::ClipVertex b[3];
    nfsu::ClipVertex b2[3];
    tri(0.5, o);
    tri(z, b);
    tri(z2, b2);
    canvas.draw(o, 0, &opaque);
    canvas.draw(b, 1, &blended);
    if (second) {
        canvas.draw(b2, 2, &blended2);
    }
    BlendCase result;
    result.pixel = {canvas.pixel(0, 0)[0], canvas.pixel(0, 0)[1],
                    canvas.pixel(0, 0)[2], canvas.pixel(0, 0)[3]};
    result.stats = canvas.stats;
    return result;
}

void test_blend_modes() {
    const unsigned char none[4] = {0, 0, 0, 0};
    // source-over: (src * a + dst * (255 - a) + 127) / 255 over (200,100,50).
    const unsigned char over[4] = {0, 0, 255, 128};
    const BlendCase a =
        blend_over(nfsu::RenderBlendMode::source_over, over, 0.25, false, none);
    check(a.pixel == std::vector<int>({100, 50, 153, 255}), "source-over result");
    check(a.stats.fragments_blended == 1 && a.stats.fragments_blend_overlap == 0,
          "one blended fragment and no overlap");
    // additive: min(255, (src * a + 127) / 255 + dst).
    const unsigned char add[4] = {100, 200, 250, 128};
    const BlendCase b =
        blend_over(nfsu::RenderBlendMode::additive, add, 0.25, false, none);
    check(b.pixel == std::vector<int>({250, 200, 175, 255}), "additive result");
    const unsigned char bright[4] = {255, 255, 255, 255};
    const BlendCase c =
        blend_over(nfsu::RenderBlendMode::additive, bright, 0.25, false, none);
    check(c.pixel == std::vector<int>({255, 255, 255, 255}), "additive saturates");
    // A blended draw behind opaque geometry still fails the depth test.
    const BlendCase d =
        blend_over(nfsu::RenderBlendMode::source_over, over, 0.75, false, none);
    check(d.pixel == std::vector<int>({200, 100, 50, 255}),
          "a blended draw behind opaque geometry is rejected");
    check(d.stats.fragments_blended == 0 && d.stats.fragments_depth_failed == 1,
          "the rejected blended fragment is a depth failure");
    // A blended draw in front of another one composites over it, and the
    // overlap counter counts the second fragment.
    const unsigned char over2[4] = {200, 100, 50, 200};
    const BlendCase e =
        blend_over(nfsu::RenderBlendMode::source_over, over, 0.25, true, over2, 0.1);
    check(e.pixel == std::vector<int>({178, 89, 72, 255}),
          "a nearer blended draw composites over the blended draw before it");
    check(e.stats.fragments_blended == 2 && e.stats.fragments_blend_overlap == 1,
          "the second blended fragment is counted as an overlap");
    // The retail path leaves depth writes on for blended car draws, so a
    // blended draw behind an earlier blended one is rejected outright.
    const BlendCase f =
        blend_over(nfsu::RenderBlendMode::source_over, over, 0.25, true, over2, 0.4);
    check(f.pixel == std::vector<int>({100, 50, 153, 255}),
          "a blended draw writes depth and hides the blended draw behind it");
    check(f.stats.fragments_blended == 1 && f.stats.fragments_blend_overlap == 0,
          "the rejected second blended fragment never blends");
}

void test_environment_term() {
    // environment[k] carries every factor but the texel alpha, and the
    // fragment adds environment[k] * texel_alpha / 255.
    const auto run = [](unsigned char alpha, double e0, double e2) {
        Canvas canvas(1, 1);
        nfsu::RenderTexture texture = make_texture(1, 1);
        texture.rgba = {10, 20, 30, alpha};
        nfsu::RasterMaterial material;
        material.texture = &texture;
        material.environment[0] = e0;
        material.environment[2] = e2;
        const nfsu::ClipVertex t[3] = {at(canvas, -2, -2), at(canvas, -2, 4),
                                       at(canvas, 4, -2)};
        canvas.draw(t, 1, &material);
        return std::vector<int>({canvas.pixel(0, 0)[0], canvas.pixel(0, 0)[1],
                                 canvas.pixel(0, 0)[2]});
    };
    check(run(255, 0.0, 0.0) == std::vector<int>({10, 20, 30}), "no term, no change");
    check(run(255, 40.4, 200.0) == std::vector<int>({50, 20, 230}),
          "the term at full alpha");
    check(run(128, 40.4, 200.0) == std::vector<int>({30, 20, 130}),
          "the term scaled by the texel alpha");
    check(run(0, 40.4, 200.0) == std::vector<int>({10, 20, 30}),
          "no term at zero alpha");
    check(run(255, 0.0, 255.0) == std::vector<int>({10, 20, 255}),
          "the term saturates");
}

void test_material_coefficient_validation() {
    std::string error;
    check(nfsu::validate_material_coefficients(nfsu::MaterialCoefficients{}, error),
          "the default coefficients are valid: " + error);
    const auto refused = [&error](const nfsu::MaterialCoefficients& c,
                                  const std::string& label) {
        error.clear();
        const bool ok = nfsu::validate_material_coefficients(c, error);
        check(!ok && !error.empty(), label);
    };
    nfsu::MaterialCoefficients power;
    power.environment_power = nfsu::kMaxEnvironmentPower + 1U;
    refused(power, "an environment power above the bound is refused");
    nfsu::MaterialCoefficients negative;
    negative.alpha_min = -1.0;
    refused(negative, "a negative alpha lane is refused");
    nfsu::MaterialCoefficients huge;
    huge.environment_range[2] = 1e9;
    refused(huge, "an environment range above the bound is refused");
    nfsu::MaterialCoefficients nan;
    nan.alpha_range = std::numeric_limits<double>::quiet_NaN();
    refused(nan, "a non-finite alpha lane is refused");
    nfsu::MaterialCoefficients edge;
    edge.alpha_min = nfsu::kMaxMaterialCoefficient;
    edge.environment_min[1] = nfsu::kMaxMaterialCoefficient;
    edge.environment_power = nfsu::kMaxEnvironmentPower;
    check(nfsu::validate_material_coefficients(edge, error),
          "the bounds themselves are accepted: " + error);
}

constexpr std::uint32_t kBlendHash = 0x55555555U;
constexpr std::uint32_t kOpaqueHash = 0x66666666U;

// One mesh with two groups covering the same screen area. Group 0, first in
// record order, is the quad in front; group 1 is the quad behind it. A
// frame that blends correctly must therefore have drawn group 0 last.
nfsu::GeometryReport blend_geometry(std::uint32_t flags,
                                    std::uint32_t back_flags = 0xFFFFFFFFU) {
    if (back_flags == 0xFFFFFFFFU) {
        back_flags = flags;
    }
    nfsu::GeometryReport report;
    nfsu::Mesh mesh;
    mesh.index = 0;
    mesh.name = "BLEND_MESH";
    mesh.valid = true;
    mesh.vertices = {
        vertex(1, 0, -0.5F, 0, 0), vertex(1, 1, -0.5F, 0, 1),
        vertex(1, 1, 0.5F, 1, 1), vertex(1, 0, 0.5F, 1, 0),
        vertex(0, 0, -0.5F, 0, 0), vertex(0, 1, -0.5F, 0, 1),
        vertex(0, 1, 0.5F, 1, 1), vertex(0, 0, 0.5F, 1, 0)};
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}};
    mesh.texture_hashes = {kBlendHash, kOpaqueHash};
    mesh.materials = {material_range(0, 6, 0, flags),
                      material_range(6, 6, 1, back_flags)};
    report.meshes.push_back(mesh);
    return report;
}

// front_layer 5 and back_layer 0 are the arrangement the retail 240SX pack
// uses: its blended glass sits in row 5 and its opaque body in row 0.
nfsu::RenderTextureSet blend_textures(std::uint8_t front_blend,
                                      std::uint8_t back_blend,
                                      std::uint8_t front_layer = 5,
                                      std::uint8_t back_layer = 0) {
    nfsu::RenderTextureSet set;
    nfsu::RenderTexture front = make_texture(1, 1);
    front.hash = kBlendHash;
    front.rgba = {0, 0, 255, 128};
    front.alpha_blend_mode = front_blend;
    front.layer_order = front_layer;
    set.textures.push_back(front);
    nfsu::RenderTexture back = make_texture(1, 1);
    back.hash = kOpaqueHash;
    back.rgba = {200, 100, 50, 255};
    back.alpha_blend_mode = back_blend;
    back.layer_order = back_layer;
    set.textures.push_back(back);
    return set;
}

nfsu::RenderOptions blend_options() {
    nfsu::RenderOptions options = scene_options();
    options.shading = nfsu::ShadingMode::none;
    options.background[0] = 0;
    options.background[1] = 0;
    options.background[2] = 0;
    options.background[3] = 255;
    return options;
}

void test_material_state_scene() {
    const nfsu::RenderOptions base = blend_options();
    const nfsu::PreparedCarScene scene =
        prepare_all(blend_geometry(0), blend_textures(1, 0));
    const std::size_t centre =
        (std::size_t{base.height / 2} * base.width + base.width / 2) * 4U;
    const auto render = [&](const nfsu::PreparedCarScene& drawn,
                            const nfsu::RenderOptions& options,
                            nfsu::RenderStats& stats) {
        nfsu::Frame frame;
        std::string error;
        check(nfsu::render_scene(drawn, options, nfsu::RenderLimits{}, frame, stats,
                                 error),
              "material state render: " + error);
        if (frame.rgba().size() < centre + 4U) {
            return std::vector<int>();
        }
        return std::vector<int>({frame.rgba()[centre], frame.rgba()[centre + 1],
                                 frame.rgba()[centre + 2],
                                 frame.rgba()[centre + 3]});
    };

    // Off: the front quad is drawn first, wins the depth test and is
    // written opaquely, exactly as the renderer has always done.
    nfsu::RenderStats off_stats;
    const std::vector<int> off = render(scene, base, off_stats);
    check(off == std::vector<int>({0, 0, 255, 255}),
          "material state off draws the front quad opaquely");
    check(off_stats.groups_blended == 0 && off_stats.groups_alpha_tested == 0 &&
              off_stats.fragments_blended == 0 && off_stats.pixels_blended == 0,
          "material state off reports no blending");

    // Retail: the opaque group is drawn first although it is second in
    // record order, and the blended group is composited over it.
    nfsu::RenderOptions retail = base;
    retail.material_state = nfsu::MaterialStateMode::retail;
    nfsu::RenderStats retail_stats;
    check(render(scene, retail, retail_stats) == std::vector<int>({100, 50, 153, 255}),
          "the blended group is composited over the opaque one");
    check(retail_stats.groups_blended == 1 && retail_stats.groups_alpha_tested == 0,
          "one blended group");
    check(retail_stats.fragments_blended > 0 &&
              retail_stats.fragments_blend_overlap == 0,
          "a single blended group cannot overlap itself");
    check(retail_stats.pixels_blended > 0, "blended pixels counted");
    // The two passes must submit exactly what one pass submits.
    check(retail_stats.material_groups == off_stats.material_groups &&
              retail_stats.triangles_submitted == off_stats.triangles_submitted &&
              retail_stats.groups_resolved == off_stats.groups_resolved &&
              retail_stats.triangles_rasterized == off_stats.triangles_rasterized,
          "the two passes submit exactly the groups one pass submits");

    // A constant alpha lane has to reach the fragment even though it does
    // not depend on the view direction: 128 * 0.5 = 64 as the source alpha.
    nfsu::RenderOptions half = retail;
    half.coefficients.alpha_min = 0.5;
    nfsu::RenderStats half_stats;
    check(render(scene, half, half_stats) == std::vector<int>({150, 75, 101, 255}),
          "a constant alpha lane scales the source alpha");

    // A closed diffuse gate zeros the effect's whole output. On the
    // blended group in front that means it contributes nothing, so the
    // ungated opaque group behind it shows unchanged.
    nfsu::RenderStats gated_stats;
    const nfsu::PreparedCarScene gated_front =
        prepare_all(blend_geometry(0x00000100U, 0), blend_textures(1, 0));
    check(render(gated_front, retail, gated_stats) ==
              std::vector<int>({200, 100, 50, 255}),
          "a closed gate makes a source-over group contribute nothing");
    check(gated_stats.fragments_blended > 0,
          "the group is still blended, it just adds nothing");
    // On the opaque group it means black, not the texel colour, because
    // the effect writes zero whatever the undecoded coefficients hold.
    nfsu::RenderStats gated_both_stats;
    const nfsu::PreparedCarScene gated_both =
        prepare_all(blend_geometry(0x00000100U), blend_textures(1, 0));
    check(render(gated_both, retail, gated_both_stats) ==
              std::vector<int>({0, 0, 0, 255}),
          "a closed gate writes an opaque group black");

    // Both groups blended: the nearer one is drawn second, in the later
    // row, so it composites over the first and the overlap is counted.
    nfsu::RenderStats both_stats;
    const nfsu::PreparedCarScene both =
        prepare_all(blend_geometry(0), blend_textures(1, 1));
    render(both, retail, both_stats);
    check(both_stats.groups_blended == 2 && both_stats.fragments_blend_overlap > 0,
          "two blended groups overlap and the overlap is counted");

    // An alpha-tested front texture is removed whole, because its texel
    // alpha 128 is not above 0xAA, so the quad behind shows through.
    nfsu::RenderTextureSet tested = blend_textures(0, 0);
    tested.textures[0].alpha_test_mode = 1;
    tested.textures[0].layer_order = 0;
    nfsu::RenderStats tested_stats;
    const std::vector<int> pixel =
        render(prepare_all(blend_geometry(0), tested), retail, tested_stats);
    check(tested_stats.groups_alpha_tested == 1 &&
              tested_stats.fragments_alpha_discarded > 0,
          "the alpha-tested group discards fragments");
    check(pixel == std::vector<int>({200, 100, 50, 255}),
          "the quad behind shows through the failed alpha test");
}

// Three quads, one material group each, at px = depths[k] with texture
// slot slots[k]. Larger px is nearer the camera. This is the fixture for
// the two draw order rules a plain sort by row would not reproduce.
nfsu::GeometryReport order_geometry(const float (&depths)[3],
                                    const std::uint32_t (&slots)[3]) {
    nfsu::GeometryReport report;
    nfsu::Mesh mesh;
    mesh.index = 0;
    mesh.name = "ORDER_MESH";
    mesh.valid = true;
    for (int k = 0; k < 3; ++k) {
        const float x = depths[k];
        mesh.vertices.push_back(vertex(x, 0, -0.5F, 0, 0));
        mesh.vertices.push_back(vertex(x, 1, -0.5F, 0, 1));
        mesh.vertices.push_back(vertex(x, 1, 0.5F, 1, 1));
        mesh.vertices.push_back(vertex(x, 0, 0.5F, 1, 0));
        const std::uint16_t base = static_cast<std::uint16_t>(k * 4);
        mesh.triangles.push_back({base, static_cast<std::uint16_t>(base + 1),
                                  static_cast<std::uint16_t>(base + 2)});
        mesh.triangles.push_back({base, static_cast<std::uint16_t>(base + 2),
                                  static_cast<std::uint16_t>(base + 3)});
        mesh.materials.push_back(
            material_range(static_cast<std::uint32_t>(k * 6), 6, slots[k]));
    }
    mesh.texture_hashes = {kBlendHash, kOpaqueHash};
    report.meshes.push_back(mesh);
    return report;
}

// Two source-over textures in the same row, so nothing but the two order
// rules inside a row can decide the composite.
nfsu::RenderTextureSet order_textures() {
    nfsu::RenderTextureSet set;
    nfsu::RenderTexture a = make_texture(1, 1);
    a.hash = kBlendHash;
    a.rgba = {0, 0, 255, 128};
    a.alpha_blend_mode = 1;
    a.layer_order = 5;
    set.textures.push_back(a);
    nfsu::RenderTexture b = make_texture(1, 1);
    b.hash = kOpaqueHash;
    b.rgba = {255, 0, 0, 200};
    b.alpha_blend_mode = 1;
    b.layer_order = 5;
    set.textures.push_back(b);
    return set;
}

void test_draw_order_rules() {
    nfsu::RenderOptions retail = blend_options();
    retail.material_state = nfsu::MaterialStateMode::retail;
    const std::size_t centre =
        (std::size_t{retail.height / 2} * retail.width + retail.width / 2) * 4U;
    const auto composite = [&](const float (&depths)[3],
                               const std::uint32_t (&slots)[3]) {
        nfsu::Frame frame;
        nfsu::RenderStats stats;
        std::string error;
        check(nfsu::render_scene(
                  prepare_all(order_geometry(depths, slots), order_textures()),
                  retail, nfsu::RenderLimits{}, frame, stats, error),
              "draw order render: " + error);
        if (frame.rgba().size() < centre + 4U) {
            return std::vector<int>();
        }
        return std::vector<int>({frame.rgba()[centre], frame.rgba()[centre + 1],
                                 frame.rgba()[centre + 2]});
    };

    // Rule 2, textures batch in first-use order inside a row. Groups 0 and
    // 2 share texture A, so the queue draws them together where A was
    // first used and texture B last: A, A, B. All three quads are
    // coplanar, so nothing but the order can decide the composite. Over a
    // black frame that is (0,0,128), then (0,0,192), then (200,0,41).
    // Submitting in plain record order, A, B, A, would give (100,0,142).
    const float coplanar[3] = {0.0F, 0.0F, 0.0F};
    const std::uint32_t a_b_a[3] = {0, 1, 0};
    check(composite(coplanar, a_b_a) == std::vector<int>({200, 0, 41}),
          "textures batch in first-use order inside a row");

    // Rule 3, inside one texture the material records keep their order.
    // Groups 0 and 1 are both texture A, the first farther than the
    // second, and group 2 is texture B behind both. In record order the
    // far quad blends first and the near one over it, giving (0,0,192);
    // the reverse order would let the near quad write depth and reject the
    // far one, leaving (0,0,128). B is rejected by depth in either case.
    const float far_near_behind[3] = {0.0F, 1.0F, -1.0F};
    const std::uint32_t a_a_b[3] = {0, 0, 1};
    check(composite(far_near_behind, a_a_b) == std::vector<int>({0, 0, 192}),
          "material records keep their order inside one texture");

    // The row histogram reports what was drawn: three groups, all in row 5.
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    std::string error;
    check(nfsu::render_scene(
              prepare_all(order_geometry(coplanar, a_b_a), order_textures()),
              retail, nfsu::RenderLimits{}, frame, stats, error),
          "row histogram render: " + error);
    check(stats.draw_rows[5] == 3, "three drawable groups in row 5");
    std::uint64_t other_rows = 0;
    for (int row = 0; row < nfsu::kRenderDrawLayerCount; ++row) {
        other_rows += row == 5 ? 0 : stats.draw_rows[row];
    }
    check(other_rows == 0, "no group in any other row");
    nfsu::RenderOptions off = blend_options();
    nfsu::RenderStats off_stats;
    check(nfsu::render_scene(
              prepare_all(order_geometry(coplanar, a_b_a), order_textures()),
              off, nfsu::RenderLimits{}, frame, off_stats, error),
          "row histogram off render: " + error);
    std::uint64_t off_rows = 0;
    for (int row = 0; row < nfsu::kRenderDrawLayerCount; ++row) {
        off_rows += off_stats.draw_rows[row];
    }
    check(off_rows == 0, "material state off assigns no rows");
}

void test_draw_layer_order() {
    // The row comes from the bound texture, not from whether the group
    // blends: two blended groups swap their composite when their rows swap.
    nfsu::RenderOptions retail = blend_options();
    retail.material_state = nfsu::MaterialStateMode::retail;
    const std::size_t centre =
        (std::size_t{retail.height / 2} * retail.width + retail.width / 2) * 4U;
    const auto composite = [&](std::uint8_t front_layer, std::uint8_t back_layer) {
        nfsu::Frame frame;
        nfsu::RenderStats stats;
        std::string error;
        check(nfsu::render_scene(
                  prepare_all(blend_geometry(0),
                              blend_textures(1, 1, front_layer, back_layer)),
                  retail, nfsu::RenderLimits{}, frame, stats, error),
              "layer order render: " + error);
        if (frame.rgba().size() < centre + 4U) {
            return std::vector<int>();
        }
        return std::vector<int>({frame.rgba()[centre], frame.rgba()[centre + 1],
                                 frame.rgba()[centre + 2]});
    };
    // Row 0 before row 5: the back group composites first and the front
    // group over it, whatever the depth policy, because the front quad is
    // nearer than the back one.
    check(composite(5, 0) == std::vector<int>({100, 50, 153}),
          "the row 0 group composites before the row 5 group");
    check(composite(0, 5) != composite(5, 0),
          "swapping the rows swaps which group composites last");
}

void test_environment_scene() {
    nfsu::RenderOptions options = blend_options();
    options.material_state = nfsu::MaterialStateMode::retail;
    options.coefficients.environment_range[0] = 1.0;
    options.coefficients.environment_color[0] = 255;
    const std::size_t centre =
        (std::size_t{options.height / 2} * options.width + options.width / 2) * 4U;
    // The front quad's texel is (0, 0, 255, 128), so its red channel is 0
    // and only the environment term can raise it.
    const auto red = [&](std::uint32_t flags, bool global) {
        nfsu::RenderOptions local = options;
        local.coefficients.environment_enabled = global;
        nfsu::Frame frame;
        nfsu::RenderStats stats;
        std::string error;
        check(nfsu::render_scene(prepare_all(blend_geometry(flags),
                                             blend_textures(0, 0)),
                                 local, nfsu::RenderLimits{}, frame, stats, error),
              "environment render: " + error);
        return frame.rgba().size() >= centre + 4U
                   ? static_cast<int>(frame.rgba()[centre])
                   : -1;
    };
    check(red(0x00000000U, true) == 0, "no environment branch bit means no term");
    check(red(0x00000180U, false) == 0, "the global off means no term");
    check(red(0x00000180U, true) > 0, "an open gate adds the environment term");
    // The upload zeros the W lane of DiffuseMin and DiffuseRange with the
    // rest of the vector, so the closed diffuse gate takes the environment
    // term with it even though the environment vectors survive.
    check(red(0x00000100U, true) == 0,
          "a closed diffuse gate zeros the alpha lane and the term with it");
}

void test_bindings() {
    const nfsu::RenderTextureSet set = synthetic_textures();
    nfsu::Mesh mesh;
    mesh.texture_hashes = {0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U};
    const auto kind = [&](std::uint32_t slot) {
        return nfsu::resolve_material(mesh, material_range(0, 3, slot), set).kind;
    };
    check(kind(0) == nfsu::BindingKind::resolved, "hash in pack resolves");
    check(nfsu::resolve_material(mesh, material_range(0, 3, 0), set).texture_index == 0,
          "resolved index");
    check(kind(1) == nfsu::BindingKind::unresolved, "hash not in pack unresolved");
    check(kind(2) == nfsu::BindingKind::unusable, "duplicate hash unusable");
    check(kind(3) == nfsu::BindingKind::unusable, "invalid texture unusable");
    check(kind(4) == nfsu::BindingKind::invalid_slot, "slot out of range");
}

void test_scene() {
    const nfsu::PreparedCarScene scene =
        prepare_all(synthetic_scene(), synthetic_textures());
    nfsu::RenderOptions options = scene_options();
    const nfsu::RenderLimits limits;
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    std::string error;
    check(nfsu::render_scene(scene, options, limits, frame, stats, error),
          "scene render: " + error);
    check(stats.meshes == 2 && stats.meshes_rendered == 1 &&
              stats.meshes_skipped_invalid == 1, "scene meshes");
    check(stats.material_groups == 2 && stats.groups_resolved == 1 &&
              stats.groups_unresolved == 1 && stats.unresolved_hashes == 1,
          "scene groups");
    check(stats.triangles_submitted == 5 && stats.triangles_resolved == 2 &&
              stats.triangles_fallback == 3, "scene triangles");
    check(stats.triangles_degenerate_source == 1 &&
              stats.triangles_degenerate_screen == 1, "degenerate source triangle preserved");
    check(stats.triangles_rasterized == 4, "scene rasterized");
    check(stats.pixels_drawn > 0 && stats.pixels_fallback > 0 &&
              stats.pixels_fallback < stats.pixels_drawn, "scene pixel split");

    // Mapping world = (px, py, -pz): source +pz (textured, the car's left)
    // is on the right of the screen when viewed from the front.
    const auto owner = [&](std::uint32_t x, std::uint32_t y) {
        return frame.ids()[std::size_t{y} * frame.width() + x];
    };
    bool left_fallback = true;
    bool right_textured = true;
    unsigned char fb[4];
    nfsu::fallback_color(0x22222222U, fb);
    for (std::uint32_t y = 0; y < frame.height(); ++y) {
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::uint32_t id = owner(x, y);
            if (id == nfsu::kNoTriangle) {
                continue;
            }
            if (x < 16) {
                left_fallback = left_fallback && id >= 2;
            } else {
                right_textured = right_textured && id < 2;
            }
        }
    }
    check(left_fallback, "unresolved group on the left uses the fallback");
    check(right_textured, "source +pz appears on the right from the front");

    // Unlit so that the exact fallback colors are visible.
    options.shading = nfsu::ShadingMode::none;
    check(nfsu::render_scene(scene, options, limits, frame, stats, error),
          "unlit scene");
    bool saw_fallback = false;
    for (std::size_t i = 0; i < frame.ids().size(); ++i) {
        if (frame.ids()[i] >= 2 && frame.ids()[i] != nfsu::kNoTriangle) {
            const unsigned char* p = &frame.rgba()[i * 4U];
            const bool base = p[0] == fb[0] && p[1] == fb[1] && p[2] == fb[2];
            const bool dark = p[0] == fb[0] * 3 / 4 && p[1] == fb[1] * 3 / 4 &&
                              p[2] == fb[2] * 3 / 4;
            check(base || dark, "fallback pixel color");
            saw_fallback = true;
        }
    }
    check(saw_fallback, "fallback pixels present");
}

void test_golden_and_rerender() {
    const nfsu::PreparedCarScene scene =
        prepare_all(synthetic_scene(), synthetic_textures());
    const nfsu::RenderOptions options = scene_options();
    const nfsu::RenderLimits limits;
    nfsu::Frame first;
    nfsu::Frame second;
    nfsu::RenderStats s1;
    nfsu::RenderStats s2;
    std::string error;
    check(nfsu::render_scene(scene, options, limits, first, s1, error) &&
              nfsu::render_scene(scene, options, limits, second, s2, error),
          "rerender");
    const std::string a = tga_bytes(first);
    const std::string b = tga_bytes(second);
    check(a == b, "rerender is byte-identical");
    check(a.size() == 18U + 32U * 24U * 4U, "golden size");
    const std::uint64_t hash = fnv1a64(a);
    // Locks the synthetic scene; the pixel rules themselves are checked
    // exactly by the tests above.
    constexpr std::uint64_t kGolden = 0xB62BB1032418E8F3ULL;
    if (hash != kGolden) {
        std::cerr << "golden FNV-1a 64: 0x" << std::hex << hash << std::dec << '\n';
    }
    check(hash == kGolden, "golden synthetic screenshot hash");
    check(s1.pixels_drawn == s2.pixels_drawn && s1.fragments == s2.fragments,
          "rerender stats identical");
}

void test_limits() {
    nfsu::Frame frame;
    nfsu::RenderLimits limits;
    const unsigned char bg[4] = {0, 0, 0, 0};
    std::string error;
    check(!frame.reset(0, 0, bg, limits, error), "0x0 rejected");
    check(!frame.reset(8193, 1, bg, limits, error), "8193 wide rejected");
    check(!frame.reset(1, 8193, bg, limits, error), "8193 high rejected");
    check(!frame.reset(8192, 8192, bg, limits, error), "over the pixel cap rejected");
    check(frame.width() == 0 && frame.rgba().empty(), "nothing allocated on rejection");
    limits.max_pixels = 100;
    check(!frame.reset(11, 10, bg, limits, error), "custom pixel cap");
    check(frame.reset(10, 10, bg, limits, error), "at the pixel cap accepted");

    const nfsu::PreparedCarScene scene =
        prepare_all(synthetic_scene(), synthetic_textures());
    nfsu::RenderOptions options = scene_options();
    nfsu::RenderStats stats;
    nfsu::Frame fresh;
    nfsu::RenderLimits tight;
    tight.max_source_triangles = 4;
    check(!nfsu::render_scene(scene, options, tight, fresh, stats, error),
          "triangle cap");
    check(fresh.width() == 0, "triangle cap fails before allocating");

    tight = nfsu::RenderLimits{};
    tight.max_pixel_tests = 10;
    check(!nfsu::render_scene(scene, options, tight, fresh, stats, error) &&
              error.find("pixel test") != std::string::npos,
          "pixel test cap");

    tight = nfsu::RenderLimits{};
    tight.max_raster_triangles = 1;
    check(!nfsu::render_scene(scene, options, tight, fresh, stats, error),
          "raster triangle cap");

    options.camera.pitch_deg = 95.0;
    check(!nfsu::render_scene(scene, options, nfsu::RenderLimits{}, fresh,
                              stats, error), "invalid camera rejected");
}

// Literal-only JDLZ blob, as in texture_tests: a flags1 byte of 0 before
// every 8 literals and one flags2 byte at the start.
std::vector<unsigned char> jdlz_literals(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> body;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i % 8 == 0) {
            body.push_back(0x00);
            if (i == 0) {
                body.push_back(0x00);
            }
        }
        body.push_back(data[i]);
    }
    std::vector<unsigned char> blob = {'J', 'D', 'L', 'Z', 0x02, 0x10, 0x00, 0x00};
    const std::uint32_t sizes[2] = {static_cast<std::uint32_t>(data.size()),
                                    static_cast<std::uint32_t>(body.size() + 16)};
    for (std::uint32_t value : sizes) {
        for (unsigned int k = 0; k < 4; ++k) {
            blob.push_back(static_cast<unsigned char>((value >> (8U * k)) & 0xFFU));
        }
    }
    blob.insert(blob.end(), body.begin(), body.end());
    return blob;
}

// A pack of A8R8G8B8 4x4 textures (64 retained bytes each) backed by
// literal JDLZ blobs in bytes. Record 2 is an invalid record and record 3
// has a corrupt blob; both must fall back without failing the call.
nfsu::TexturePack decode_pack(std::vector<unsigned char>& bytes) {
    nfsu::TexturePack pack;
    bytes.clear();
    for (std::size_t i = 0; i < 4; ++i) {
        std::vector<unsigned char> pixels(64);
        for (std::size_t p = 0; p < pixels.size(); ++p) {
            pixels[p] = static_cast<unsigned char>(i * 16 + p);
        }
        std::vector<unsigned char> blob = jdlz_literals(pixels);
        if (i == 3) {
            // First flags1 byte: the first token becomes a match that
            // reaches before the output start.
            blob[16] = 0xFF;
        }
        nfsu::TextureRecord record;
        record.index = i;
        record.hash = 0x100U + static_cast<std::uint32_t>(i);
        record.valid = i != 2;
        record.problem = i == 2 ? "synthetic invalid record" : "";
        record.format = nfsu::PixelFormat::a8r8g8b8;
        record.width = 4;
        record.height = 4;
        record.mip_count = 1;
        record.data_size = 64;
        record.top_level_size = 64;
        record.blob_offset = bytes.size();
        record.blob_size = static_cast<std::uint32_t>(blob.size());
        bytes.insert(bytes.end(), blob.begin(), blob.end());
        pack.textures.push_back(record);
    }
    return pack;
}

void test_texture_decode() {
    std::vector<unsigned char> bytes;
    const nfsu::TexturePack pack = decode_pack(bytes);
    nfsu::RenderTextureSet set;
    std::string error;

    // The budget is checked with each valid record's full top-level size
    // before it is decoded, since a decode failure cannot be known in
    // advance: records 0, 1 and 3 need 192 bytes. Record 3 then fails to
    // decode and retains nothing, so 128 bytes stay retained.
    nfsu::RenderTextureLimits limits;
    limits.max_total_bytes = 191;
    check(!nfsu::decode_render_textures(bytes, pack, limits, set, error) &&
              error.find("at texture 3") != std::string::npos,
          "one byte below the budget of all valid records is fatal");
    limits.max_total_bytes = 192;
    const bool decoded = nfsu::decode_render_textures(bytes, pack, limits, set, error);
    check(decoded, "decode at the exact aggregate limit: " + error);
    check(set.decoded_bytes == 128 && set.textures.size() == 4, "decoded bytes counted");
    if (set.textures.size() != 4 || set.textures[0].rgba.size() != 64) {
        return;
    }
    check(set.textures[0].valid && set.textures[1].valid, "valid textures decoded");
    check(set.textures[0].rgba[0] == 2 && set.textures[0].rgba[2] == 0 &&
              set.textures[0].rgba[3] == 3,
          "A8R8G8B8 reordered to RGBA");
    check(!set.textures[2].valid && set.textures[2].problem == "synthetic invalid record",
          "invalid record falls back");
    check(!set.textures[3].valid && !set.textures[3].problem.empty() &&
              set.textures[3].rgba.empty(),
          "corrupt blob falls back without retaining pixels");

    for (const auto format : {nfsu::PixelFormat::dxt1, nfsu::PixelFormat::dxt3}) {
        auto native_pack = pack;
        native_pack.textures.resize(1);
        auto& record = native_pack.textures[0];
        record.format = format;
        const std::uint64_t top = format == nfsu::PixelFormat::dxt1 ? 8 : 16;
        record.top_level_size = top;
        nfsu::RenderTextureSet desktop;
        check(nfsu::decode_render_textures(bytes, native_pack, {}, desktop, error) &&
              desktop.textures.size() == 1 && desktop.textures[0].usable() &&
              desktop.textures[0].storage == nfsu::RenderTextureStorage::rgba8 &&
              desktop.textures[0].rgba.size() == 64 && desktop.textures[0].blocks.empty() &&
              desktop.decoded_bytes == 64, "DXT defaults to desktop RGBA without opt-in: " + error);
        nfsu::RenderTextureLimits native_limits;
        native_limits.gpu_blocks = true;
        native_limits.max_total_bytes = top;
        nfsu::RenderTextureSet native;
        check(nfsu::decode_render_textures(bytes, native_pack, native_limits, native, error) &&
              native.textures.size() == 1 && native.textures[0].usable() &&
              native.textures[0].rgba.empty() && native.textures[0].blocks.size() == top &&
              native.decoded_bytes == top, "native DXT top level only: " + error);
        native_limits.max_total_bytes = top - 1;
        check(!nfsu::decode_render_textures(bytes, native_pack, native_limits, native, error) &&
              native.textures.empty(), "native DXT checks the budget before retaining blocks");
    }

    // One byte less: fatal, before the allocation for record 1.
    limits.max_total_bytes = 127;
    std::vector<std::uint64_t> allocations;
    limits.before_allocate = [&](std::uint64_t size) { allocations.push_back(size); };
    set.decoded_bytes = 99;
    check(!nfsu::decode_render_textures(bytes, pack, limits, set, error) &&
              error.find("exceed 127") != std::string::npos,
          "aggregate limit exceeded: " + error);
    check(allocations.size() == 1 && allocations[0] == 64,
          "limit checked before the second allocation");
    check(set.textures.empty() && set.decoded_bytes == 0, "set empty after a fatal failure");

    // A single texture larger than the limit is fatal too, not a fallback.
    limits.max_total_bytes = 63;
    limits.before_allocate = nullptr;
    check(!nfsu::decode_render_textures(bytes, pack, limits, set, error),
          "single texture over the limit");

    // Allocation failure is caught and reported.
    limits = nfsu::RenderTextureLimits{};
    int calls = 0;
    limits.before_allocate = [&](std::uint64_t) {
        if (++calls == 2) {
            throw std::bad_alloc();
        }
    };
    check(!nfsu::decode_render_textures(bytes, pack, limits, set, error) &&
              error.find("out of memory") != std::string::npos,
          "bad_alloc caught: " + error);
    check(set.textures.empty(), "set empty after allocation failure");

    // Default limits accept the pack; the default is far above the retail
    // 240SX total reported by nfsu_viewer.
    check(nfsu::decode_render_textures(bytes, pack, nfsu::RenderTextureLimits{}, set, error) &&
              set.decoded_bytes == 128,
          "default limit");
    check(nfsu::RenderTextureLimits{}.max_total_bytes == 128ULL * 1024ULL * 1024ULL,
          "default aggregate limit 128 MiB");
}

// One texture source built by hand: raw 4x4 A8R8G8B8 records whose 64
// bytes all equal a fill value, so the decoded RGBA equals the fill.
struct HandSource {
    std::vector<unsigned char> bytes;
    std::vector<nfsu::TexturePack> packs;
    bool complete = true;
    const std::vector<std::uint32_t>* wanted = nullptr;

    nfsu::TextureRecord& add(std::size_t pack, std::uint32_t hash, unsigned char fill,
                             bool valid = true) {
        if (packs.size() <= pack) {
            packs.resize(pack + 1);
        }
        nfsu::TextureRecord record;
        record.index = packs[pack].textures.size();
        record.storage = nfsu::TextureStorage::raw;
        record.hash = hash;
        record.table_hash = hash;
        record.valid = valid;
        record.problem = valid ? "" : "synthetic invalid record";
        record.format = nfsu::PixelFormat::a8r8g8b8;
        record.width = 4;
        record.height = 4;
        record.mip_count = 1;
        record.data_size = 64;
        record.top_level_size = 64;
        record.blob_offset = bytes.size();
        record.blob_size = 64;
        bytes.insert(bytes.end(), 64, fill);
        packs[pack].textures.push_back(record);
        return packs[pack].textures.back();
    }

    // Call after the last add, once bytes has its final size and address:
    // fixes the parse source and file size the pairing check uses.
    nfsu::RenderTextureSource view() {
        nfsu::RenderTextureSource source;
        source.bytes = &bytes;
        for (nfsu::TexturePack& pack : packs) {
            pack.parse_source = bytes.data();
            pack.file_size = bytes.size();
            pack.tables_parsed = true;
            source.packs.push_back(&pack);
        }
        source.complete = complete;
        source.wanted = wanted;
        return source;
    }
};

bool decode_sources(std::vector<HandSource>& hand, nfsu::RenderTextureSet& set,
                    std::string& error,
                    const nfsu::RenderTextureLimits& limits = nfsu::RenderTextureLimits{}) {
    std::vector<nfsu::RenderTextureSource> sources;
    for (HandSource& source : hand) {
        sources.push_back(source.view());
    }
    return nfsu::decode_render_texture_sources(sources, limits, set, error);
}

nfsu::MaterialBinding bind(const nfsu::RenderTextureSet& set, std::uint32_t hash) {
    nfsu::Mesh mesh;
    mesh.texture_hashes = {hash};
    return nfsu::resolve_material(mesh, material_range(0, 3, 0), set);
}

void test_texture_sources() {
    constexpr std::uint32_t kA = 0xA0000001U;
    constexpr std::uint32_t kB = 0xB0000002U;
    constexpr std::uint32_t kC = 0xC0000003U;
    constexpr std::uint32_t kD = 0xD0000004U;
    std::string error;
    {
        // The earlier source wins; provenance and the shadow list are exact.
        std::vector<HandSource> hand(2);
        hand[0].add(0, kA, 1);
        hand[1].add(0, kB, 3);
        hand[1].add(0, kA, 2);
        nfsu::RenderTextureSet set;
        check(decode_sources(hand, set, error), "two sources decode: " + error);
        check(set.textures.size() == 2U && set.decoded_bytes == 128U,
              "shadowed record is not decoded");
        if (set.textures.size() == 2U) {
            check(set.textures[0].source == 0 && set.textures[0].hash == kA &&
                      set.textures[1].source == 1 && set.textures[1].pack == 0 &&
                      set.textures[1].record == 0 && set.textures[1].hash == kB,
                  "provenance in source, pack, record order");
        }
        check(set.shadowed.size() == 1U && set.shadowed[0].hash == kA &&
                  set.shadowed[0].source == 1 && set.shadowed[0].pack == 0 &&
                  set.shadowed[0].record == 1 && set.shadowed[0].by_source == 0,
              "shadow entry");
        const nfsu::MaterialBinding a = bind(set, kA);
        const nfsu::MaterialBinding b = bind(set, kB);
        check(a.kind == nfsu::BindingKind::resolved &&
                  set.textures[a.texture_index].rgba[0] == 1 &&
                  b.kind == nfsu::BindingKind::resolved &&
                  set.textures[b.texture_index].source == 1,
              "hash resolves from the highest-priority source that has it");
        nfsu::RenderTextureSet again;
        decode_sources(hand, again, error);
        check(again.textures.size() == set.textures.size() &&
                  again.shadowed.size() == set.shadowed.size() &&
                  again.textures[1].rgba == set.textures[1].rgba,
              "multi-source decode is deterministic");

        // Reversing the order reverses the choice.
        std::swap(hand[0], hand[1]);
        check(decode_sources(hand, set, error) &&
                  set.textures[bind(set, kA).texture_index].rgba[0] == 2 &&
                  set.shadowed.size() == 1U && set.shadowed[0].source == 1,
              "caller order defines priority");
    }
    {
        // An invalid higher-priority record makes the hash unusable; the
        // lower-priority valid record is not used.
        std::vector<HandSource> hand(2);
        hand[0].add(0, kA, 1, false);
        hand[1].add(0, kA, 2);
        nfsu::RenderTextureSet set;
        check(decode_sources(hand, set, error) && set.textures.size() == 1U &&
                  bind(set, kA).kind == nfsu::BindingKind::unusable &&
                  set.shadowed.size() == 1U,
              "no silent fallback from an invalid record");
    }
    {
        // A record whose info hash is wrong still claims its table hash.
        std::vector<HandSource> hand(2);
        nfsu::TextureRecord& broken = hand[0].add(0, 0x99999999U, 1, false);
        broken.table_hash = kA;
        hand[1].add(0, kA, 2);
        nfsu::RenderTextureSet set;
        check(decode_sources(hand, set, error) &&
                  bind(set, kA).kind == nfsu::BindingKind::unusable &&
                  bind(set, 0x99999999U).kind == nfsu::BindingKind::unusable &&
                  set.shadowed.size() == 1U && set.shadowed[0].hash == kA,
              "table hash claim blocks the lower source");
        check(set.textures.size() == 1U && set.textures[0].hash == 0x99999999U &&
                  set.textures[0].claim_count == 2U && set.textures[0].claims[0] == 0x99999999U &&
                  set.textures[0].claims[1] == kA,
              "retained record keeps every distinct claim");
    }
    {
        // A valid kA in source 0, an invalid record claiming {kB, kA} in
        // source 1 and a valid kB in source 2. The source 1 record is
        // shadowed by kA, but its kB claim stays unusable and still
        // shadows source 2.
        std::vector<HandSource> hand(3);
        hand[0].add(0, kA, 1);
        nfsu::TextureRecord& broken = hand[1].add(0, kB, 2, false);
        broken.table_hash = kA;
        hand[2].add(0, kB, 3);
        nfsu::RenderTextureSet set;
        check(decode_sources(hand, set, error) && set.textures.size() == 1U &&
                  set.textures[0].source == 0 && bind(set, kA).kind == nfsu::BindingKind::resolved &&
                  bind(set, kB).kind == nfsu::BindingKind::unusable,
              "partially shadowed claim stays unusable");
        check(set.shadowed.size() == 2U && set.shadowed[0].hash == kA &&
                  set.shadowed[0].source == 1 && set.shadowed[0].by_source == 0 &&
                  set.shadowed[1].hash == kB && set.shadowed[1].source == 2 &&
                  set.shadowed[1].by_source == 1,
              "lower kB is shadowed by the partially shadowed record");
        check(set.unusable_claims.size() == 1U && set.unusable_claims[0].hash == kB &&
                  set.unusable_claims[0].source == 1 && set.unusable_claims[0].pack == 0 &&
                  set.unusable_claims[0].record == 0,
              "unusable claim provenance");

        // Without the lower source kB is still unusable.
        hand.pop_back();
        check(decode_sources(hand, set, error) && set.textures.size() == 1U &&
                  bind(set, kA).kind == nfsu::BindingKind::resolved &&
                  bind(set, kB).kind == nfsu::BindingKind::unusable &&
                  set.unusable_claims.size() == 1U && set.shadowed.size() == 1U,
              "unusable claim without a lower source");

        // A fatal failure after the claim was recorded leaves it out too.
        std::vector<HandSource> fatal = hand;
        fatal[1].add(0, kC, 5);
        nfsu::RenderTextureLimits limits;
        limits.max_total_bytes = 64;
        check(!decode_sources(fatal, set, error, limits) && set.textures.empty() &&
                  set.shadowed.empty() && set.unusable_claims.empty(),
              "fatal failure leaves unusable claims empty");

        // If source 0 already owns kA and kB, the record is fully shadowed
        // and both hashes resolve from source 0.
        std::vector<HandSource> owned(3);
        owned[0].add(0, kA, 1);
        owned[0].add(0, kB, 4);
        nfsu::TextureRecord& both = owned[1].add(0, kB, 2, false);
        both.table_hash = kA;
        owned[2].add(0, kB, 3);
        check(decode_sources(owned, set, error) && set.textures.size() == 2U &&
                  set.unusable_claims.empty() && set.shadowed.size() == 2U &&
                  set.shadowed[0].hash == kB && set.shadowed[0].by_source == 0 &&
                  set.shadowed[1].by_source == 0 &&
                  bind(set, kA).kind == nfsu::BindingKind::resolved &&
                  bind(set, kB).kind == nfsu::BindingKind::resolved &&
                  set.textures[bind(set, kB).texture_index].rgba[0] == 4,
              "fully shadowed record adds no unusable claim");
    }
    {
        // Several unusable claims: source, pack, record, then claim order,
        // and the same on every run. A same-source valid kD collides with
        // the claim.
        std::vector<HandSource> hand(2);
        hand[0].add(0, kA, 1);
        nfsu::TextureRecord& three = hand[1].add(0, kC, 2, false);
        three.storage = nfsu::TextureStorage::jdlz;
        three.table_hash = kD;
        three.blob_entry[0] = 0x01;
        three.blob_entry[3] = 0xA0;
        nfsu::TextureRecord& two = hand[1].add(1, 0x99999999U, 3, false);
        two.table_hash = kA;
        hand[1].add(1, kD, 4);
        nfsu::RenderTextureSet set;
        nfsu::RenderTextureSet again;
        check(decode_sources(hand, set, error) && decode_sources(hand, again, error),
              "unusable claim order decodes: " + error);
        const std::vector<std::uint32_t> hashes = {kC, kD, 0x99999999U};
        const std::vector<std::size_t> packs = {0, 0, 1};
        const std::vector<std::size_t> records = {0, 0, 0};
        bool ordered = set.unusable_claims.size() == 3U &&
                       again.unusable_claims.size() == 3U;
        for (std::size_t i = 0; ordered && i < 3U; ++i) {
            const nfsu::UnusableClaim& x = set.unusable_claims[i];
            const nfsu::UnusableClaim& y = again.unusable_claims[i];
            ordered = x.hash == hashes[i] && x.source == 1 && x.pack == packs[i] &&
                      x.record == records[i] && y.hash == x.hash && y.source == x.source &&
                      y.pack == x.pack && y.record == x.record;
        }
        check(ordered, "unusable claims are ordered and deterministic");
        check(set.textures.size() == 2U && bind(set, kA).kind == nfsu::BindingKind::resolved &&
                  bind(set, kC).kind == nfsu::BindingKind::unusable &&
                  bind(set, kD).kind == nfsu::BindingKind::unusable &&
                  bind(set, 0x99999999U).kind == nfsu::BindingKind::unusable,
              "unusable claim collides with a valid record of its source");

        // A wanted list that names none of the record's claims skips them.
        const std::vector<std::uint32_t> wanted = {kD};
        hand[1].wanted = &wanted;
        check(decode_sources(hand, set, error) && set.unusable_claims.size() == 2U &&
                  set.unusable_claims[0].hash == kC && set.unusable_claims[1].hash == kD &&
                  bind(set, 0x99999999U).kind == nfsu::BindingKind::unresolved,
              "unwanted shadowed record adds no unusable claim");
    }
    {
        // A mismatched record and a valid record for kA in one source: kA
        // is ambiguous there, never resolved from the valid record.
        std::vector<HandSource> hand(1);
        nfsu::TextureRecord& broken = hand[0].add(0, kB, 1, false);
        broken.table_hash = kA;
        hand[0].add(0, kA, 2);
        nfsu::RenderTextureSet set;
        check(decode_sources(hand, set, error) && set.textures.size() == 2U &&
                  bind(set, kA).kind == nfsu::BindingKind::unusable &&
                  bind(set, kB).kind == nfsu::BindingKind::unusable,
              "mismatched claim collides with a valid record in one source");

        // With a wanted list naming only kA the mismatched record is kept,
        // because one of its claims is wanted.
        const std::vector<std::uint32_t> wanted = {kA};
        hand[0].wanted = &wanted;
        check(decode_sources(hand, set, error) && set.textures.size() == 2U &&
                  set.not_wanted == 0U &&
                  bind(set, kA).kind == nfsu::BindingKind::unusable,
              "wanted filter keeps a record for any claimed hash");

        // The same with the valid record in a later pack and the claim in
        // the JDLZ blob table instead of the hash table.
        std::vector<HandSource> jdlz(1);
        nfsu::TextureRecord& blob_claim = jdlz[0].add(0, kB, 1, false);
        blob_claim.storage = nfsu::TextureStorage::jdlz;
        blob_claim.table_hash = kB;
        blob_claim.blob_entry[0] = 0x01;
        blob_claim.blob_entry[3] = 0xA0;
        jdlz[0].add(1, kA, 2);
        jdlz[0].wanted = &wanted;
        check(decode_sources(jdlz, set, error) && set.textures.size() == 2U &&
                  bind(set, kA).kind == nfsu::BindingKind::unusable,
              "blob table claim collides across packs of one source");
    }
    {
        // Duplicates inside one source, also across its packs, stay
        // ambiguous.
        std::vector<HandSource> hand(2);
        hand[0].add(0, kA, 1);
        hand[1].add(0, kC, 2);
        hand[1].add(1, kC, 3);
        nfsu::RenderTextureSet set;
        check(decode_sources(hand, set, error) && set.textures.size() == 3U &&
                  bind(set, kC).kind == nfsu::BindingKind::unusable && set.shadowed.empty(),
              "duplicate across packs of one source is unusable");
    }
    {
        // After an incomplete source nothing later is used.
        std::vector<HandSource> hand(3);
        hand[0].add(0, kA, 1);
        hand[0].complete = false;
        hand[1].add(0, kB, 2);
        hand[2].add(0, kC, 3);
        nfsu::RenderTextureSet set;
        check(decode_sources(hand, set, error) && set.textures.size() == 1U &&
                  set.blocked_sources == std::vector<std::size_t>{1, 2} &&
                  bind(set, kB).kind == nfsu::BindingKind::unresolved,
              "incomplete source blocks later sources");

        std::vector<nfsu::RenderTextureSource> sources;
        hand[0].complete = true;
        for (HandSource& source : hand) {
            sources.push_back(source.view());
        }
        hand[0].packs[0].tables_parsed = false;
        check(nfsu::decode_render_texture_sources(sources, nfsu::RenderTextureLimits{}, set,
                                                  error) &&
                  set.blocked_sources == std::vector<std::size_t>{1, 2},
              "a pack without tables makes its source incomplete");
    }
    {
        // One aggregate budget over all sources, exact to the byte.
        std::vector<HandSource> hand(2);
        hand[0].add(0, kA, 1);
        hand[1].add(0, kB, 2);
        nfsu::RenderTextureLimits limits;
        limits.max_total_bytes = 127;
        nfsu::RenderTextureSet set;
        set.decoded_bytes = 5;
        check(!decode_sources(hand, set, error, limits) &&
                  error.find("exceed 127 bytes at texture 0 of source 1 pack 0 (64 retained, "
                             "64 more)") != std::string::npos &&
                  set.textures.empty() && set.decoded_bytes == 0,
              "aggregate budget across sources: " + error);
        limits.max_total_bytes = 128;
        check(decode_sources(hand, set, error, limits) && set.decoded_bytes == 128,
              "budget met exactly");

        limits = nfsu::RenderTextureLimits{};
        int calls = 0;
        limits.before_allocate = [&](std::uint64_t) {
            if (++calls == 2) {
                throw std::bad_alloc();
            }
        };
        check(!decode_sources(hand, set, error, limits) &&
                  error.find("out of memory") != std::string::npos && set.textures.empty(),
              "allocation failure in the second source leaves the set empty");
    }
    {
        // Unwanted records are not decoded but still claim their hashes.
        const std::vector<std::uint32_t> wanted = {kB, kD};
        std::vector<HandSource> hand(3);
        hand[0].add(0, kA, 1);
        hand[1].add(0, kB, 2);
        hand[1].add(0, kC, 3);
        hand[1].wanted = &wanted;
        hand[2].add(0, kC, 4);
        hand[2].add(0, kD, 5);
        hand[2].wanted = &wanted;
        nfsu::RenderTextureSet set;
        check(decode_sources(hand, set, error) && set.textures.size() == 3U &&
                  set.not_wanted == 1U && set.shadowed.size() == 1U &&
                  set.shadowed[0].hash == kC && set.shadowed[0].by_source == 1 &&
                  bind(set, kC).kind == nfsu::BindingKind::unresolved &&
                  bind(set, kD).kind == nfsu::BindingKind::resolved,
              "wanted filter keeps precedence claims");
    }
    {
        // Packs must come from the bytes they are paired with.
        std::vector<HandSource> hand(1);
        hand[0].add(0, kA, 1);
        std::vector<nfsu::RenderTextureSource> sources = {hand[0].view()};
        hand[0].packs[0].file_size = 1;
        nfsu::RenderTextureSet set;
        check(!nfsu::decode_render_texture_sources(sources, nfsu::RenderTextureLimits{}, set,
                                                   error) &&
                  error == "texture source 0 pack 0 was not parsed from the bytes of that source",
              "mispaired pack refused");

        // A parsed pack paired with an unrelated buffer of the same size
        // and content is refused; its own buffer is accepted.
        const std::vector<unsigned char> parsed_bytes(64, 0);
        const std::vector<unsigned char> unrelated(parsed_bytes);
        const nfsu::TexturePack parsed =
            nfsu::parse_texture_pack(parsed_bytes.data(), parsed_bytes.size());
        check(parsed.parse_source == parsed_bytes.data() &&
                  parsed.file_size == unrelated.size(),
              "parser records its source buffer");
        nfsu::RenderTextureSource same_size;
        same_size.bytes = &unrelated;
        same_size.packs.push_back(&parsed);
        check(!nfsu::decode_render_texture_sources({same_size}, nfsu::RenderTextureLimits{},
                                                   set, error) &&
                  error == "texture source 0 pack 0 was not parsed from the bytes of that source",
              "same-size unrelated buffer refused");
        same_size.bytes = &parsed_bytes;
        check(nfsu::decode_render_texture_sources({same_size}, nfsu::RenderTextureLimits{},
                                                  set, error) &&
                  set.textures.empty() && set.blocked_sources.empty(),
              "the parsed buffer itself is accepted: " + error);
        sources[0].bytes = nullptr;
        check(!nfsu::decode_render_texture_sources(sources, nfsu::RenderTextureLimits{}, set,
                                                   error) &&
                  error == "texture source 0 has no bytes",
              "null bytes refused");
        sources.clear();
        check(nfsu::decode_render_texture_sources(sources, nfsu::RenderTextureLimits{}, set,
                                                  error) &&
                  set.textures.empty(),
              "no sources is an empty set");
    }
    {
        // The source lifecycle can destroy a consumed pack. Its priority
        // claims and retained pixels must survive before the next load.
        std::vector<nfsu::RenderTextureSource> deferred(3);
        HandSource current;
        std::vector<std::size_t> loaded, released;
        nfsu::RenderTextureLimits limits;
        limits.load_source = [&](std::size_t i, nfsu::RenderTextureSource& source,
                                  std::string&) {
            check(loaded.size() == released.size(), "previous source released before next read");
            current = HandSource{};
            current.add(0, kA, 1, i != 0);
            if (i == 1) current.add(0, kB, 2);
            current.complete = i != 1;
            source = current.view(); loaded.push_back(i); return true;
        };
        limits.after_source = [&](std::size_t i) { current = HandSource{}; released.push_back(i); };
        nfsu::RenderTextureSet set;
        check(nfsu::decode_render_texture_sources(deferred, limits, set, error) &&
              loaded == std::vector<std::size_t>{0,1} && released == loaded &&
              set.blocked_sources == std::vector<std::size_t>{2} &&
              bind(set,kA).kind == nfsu::BindingKind::unusable &&
              bind(set,kB).kind == nfsu::BindingKind::resolved && set.shadowed.size() == 1,
              "lazy release preserves invalid claims, pixels and incomplete blocking");
        limits.load_source = [](std::size_t, nfsu::RenderTextureSource&, std::string& problem) {
            problem = "synthetic source read refusal"; return false;
        };
        check(!nfsu::decode_render_texture_sources(deferred, limits, set, error) &&
              set.textures.empty() && error == "synthetic source read refusal",
              "lazy read failure clears result");
        struct Cancel {};
        limits.load_source = [](std::size_t, nfsu::RenderTextureSource&, std::string&) -> bool {
            throw Cancel{};
        };
        bool cancelled = false;
        try { nfsu::decode_render_texture_sources(deferred, limits, set, error); }
        catch (const Cancel&) { cancelled = true; }
        check(cancelled && set.textures.empty(), "lazy loader cancellation propagates");
    }
    {
        // One source with one pack equals decode_render_textures.
        std::vector<unsigned char> bytes;
        nfsu::TexturePack pack = decode_pack(bytes);
        nfsu::RenderTextureSet old_set;
        nfsu::RenderTextureSet new_set;
        check(nfsu::decode_render_textures(bytes, pack, nfsu::RenderTextureLimits{}, old_set,
                                           error),
              "single pack decode");
        pack.parse_source = bytes.data();
        pack.file_size = bytes.size();
        pack.tables_parsed = true;
        nfsu::RenderTextureSource source;
        source.bytes = &bytes;
        source.packs.push_back(&pack);
        check(nfsu::decode_render_texture_sources({source}, nfsu::RenderTextureLimits{},
                                                  new_set, error),
              "single source decode");
        bool same = old_set.textures.size() == new_set.textures.size() &&
                    old_set.decoded_bytes == new_set.decoded_bytes;
        for (std::size_t i = 0; same && i < old_set.textures.size(); ++i) {
            const nfsu::RenderTexture& x = old_set.textures[i];
            const nfsu::RenderTexture& y = new_set.textures[i];
            same = x.hash == y.hash && x.valid == y.valid && x.rgba == y.rgba &&
                   x.problem == y.problem && x.width == y.width &&
                   x.alpha_test_mode == y.alpha_test_mode &&
                   x.alpha_blend_mode == y.alpha_blend_mode &&
                   x.layer_order == y.layer_order &&
                   x.layer_group == y.layer_group && y.record == i;
        }
        check(same, "one source equals decode_render_textures");
    }
}

void test_alpha_default() {
    check(nfsu::RenderOptions{}.alpha_cutoff == 0, "default alpha cutoff is 0");
    check(nfsu::RenderOptions{}.material_state == nfsu::MaterialStateMode::off,
          "the retail material state is off by default");
    const nfsu::RasterMaterial defaults;
    check(!defaults.alpha_test && defaults.blend == nfsu::RenderBlendMode::disabled &&
              defaults.alpha_scale == 1.0 &&
              defaults.environment[0] == 0.0 && defaults.environment[1] == 0.0 &&
              defaults.environment[2] == 0.0,
          "a default raster material is the opaque state");
    check(nfsu::RenderTexture{}.alpha_test_mode == 0 &&
              nfsu::RenderTexture{}.alpha_blend_mode == 0 &&
              nfsu::RenderTexture{}.layer_order == 0 &&
              nfsu::RenderTexture{}.layer_group == 0,
          "a hand-built texture is opaque and in row 0");
    nfsu::RenderTexture texture = make_texture(1, 1);
    texture.rgba = {90, 80, 70, 0};
    Canvas canvas(2, 2);
    nfsu::RasterMaterial material;
    material.texture = &texture;
    const nfsu::ClipVertex tri[3] = {at(canvas, -2, -2), at(canvas, -2, 9), at(canvas, 9, -2)};
    canvas.draw(tri, 1, &material);
    const unsigned char* p = canvas.pixel(1, 1);
    check(p[0] == 90 && p[1] == 80 && p[2] == 70 && p[3] == 255,
          "alpha 0 texel drawn opaquely with its decoded RGB by default");
    check(canvas.stats.fragments_alpha_discarded == 0, "nothing discarded by default");
}

// nfsu::same_file_path, the one comparison every "this output must differ
// from that file" refusal rests on. It is asked mostly about paths that do
// not exist yet, where fs::equivalent answers nothing at all, so what it
// does with the names is the whole guard.
//
// Whether two names differing only in case are one file is a property of
// the filesystem, so this test measures it here rather than asking the code
// under test: it creates one file and looks for it under the other case.
void test_same_file_path() {
    const ScratchDir scratch;
    check(scratch.ok(), "scratch directory created for the path comparison");
    if (!scratch.ok()) {
        return;
    }
    const fs::path root = scratch.path();
    std::error_code error;

    // Ground truth, measured rather than asked of the code under test: does
    // this filesystem fold case?
    //
    // What that can prove is one-sided. Where the answer is yes, every
    // "== folds_case" below reads as "== true", and a comparison that
    // always answered yes for a case alias would pass just as well; the
    // half that separates the two runs on a case-sensitive filesystem. The
    // assertions are written against the measurement, not against this
    // machine, so that they do the separating wherever they can.
    {
        std::ofstream stream(root / "CaseProbe.bin", std::ios::binary);
        stream.write("probe", 5);
    }
    // Asserted under its own spelling first: a probe file that was never
    // created would otherwise read as "this filesystem is case-sensitive"
    // and quietly invert every expectation below.
    check(fs::exists(root / "CaseProbe.bin", error), "the case probe file was created");
    const bool folds_case = fs::exists(root / "caseprobe.bin", error) && !error;

    // The defect this test exists for. Two spellings of one path that does
    // not exist: a bare relative name, and the same name written "./".
    // fs::weakly_canonical on its own returns the relative name for the
    // first - nothing in it exists, so it degrades to lexically_normal -
    // and an absolute path for the second, because "." does exist. Compared
    // without making both absolute first they are unequal, and the guard
    // that refuses a generated-texture dump named like the screenshot does
    // not fire: the dump is written first, so the caller gets the dump at
    // the screenshot's path and a failed run.
    //
    // These name nothing that is ever created. They are deliberately
    // relative to the working directory, because that is the only shape in
    // which the two spellings normalize differently.
    const fs::path bare("nfsu_same_path_probe.tga");
    const fs::path dotted("./nfsu_same_path_probe.tga");
    if (fs::exists(bare, error) || fs::exists(fs::path("nfsu_same_path_probe2.tga"),
                                              error)) {
        // Nothing here ever creates these, so something else did. They
        // would be compared by fs::equivalent instead of by name, which is
        // not what this case is about.
        std::cout << "note: relative path cases skipped (a probe name is taken)\n";
    } else {
        check(nfsu::same_file_path(bare, dotted),
              "a relative name and the same name written \"./\" are one file");
        check(nfsu::same_file_path(dotted, bare), "and the comparison is symmetric");
        check(nfsu::same_file_path(bare, fs::absolute(bare, error)),
              "a relative name and its absolute spelling are one file");
        check(!nfsu::same_file_path(bare, fs::path("nfsu_same_path_probe2.tga")),
              "two different relative names are not");
    }

    // The same, inside a directory that does exist, plus "." and ".."
    // components around names that do not.
    const fs::path absent = root / "absent.tga";
    check(!fs::exists(absent, error), "the absolute probe path does not exist");
    check(nfsu::same_file_path(absent, absent), "one spelling is itself");
    check(nfsu::same_file_path(absent, root / "." / "absent.tga"),
          "a \".\" component names the same file");
    check(nfsu::same_file_path(absent, root / "nothing" / ".." / "absent.tga"),
          "a \"..\" through a directory that does not exist names the same file");
    check(!nfsu::same_file_path(absent, root / "absent2.tga"),
          "two different names in one directory are not one file");
    check(!nfsu::same_file_path(absent, root / "nothing" / "absent.tga"),
          "the same name in another directory is not one file");
    // Same length, one character apart, so the case fold cannot be what
    // separates them.
    check(!nfsu::same_file_path(absent, root / "absentXtga"),
          "a name differing only outside the letters is not one file");

    // Case aliases, against the measured answer. On a filesystem that folds
    // case these are one file although neither exists, which is the second
    // half of the same defect: "--skin-dump OUT.TGA" beside an "out.tga"
    // screenshot.
    check(nfsu::same_file_path(absent, root / "ABSENT.TGA") == folds_case,
          "an upper-case spelling is one file exactly where case is folded");
    check(nfsu::same_file_path(absent, root / "Absent.Tga") == folds_case,
          "and so is a mixed-case spelling");
    // A relative pair is decided by the working directory, which need not be
    // on the same volume as the scratch directory, so its answer is
    // measured where it applies. Nothing that already exists is touched:
    // if the name is taken, or the directory cannot be written to, the case
    // is skipped rather than guessed at or forced.
    const fs::path cwd_probe("NfsuCasePathProbe.bin");
    if (fs::exists(cwd_probe, error)) {
        std::cout << "note: relative case alias skipped (the probe name is taken)\n";
    } else {
        {
            std::ofstream stream(cwd_probe, std::ios::binary);
            stream.write("probe", 5);
        }
        if (!fs::exists(cwd_probe, error)) {
            std::cout << "note: relative case alias skipped (the working directory is "
                         "not writable)\n";
        } else {
            const bool cwd_folds_case =
                fs::exists(fs::path("nfsucasepathprobe.bin"), error) && !error;
            check(fs::remove(cwd_probe, error) && !error,
                  "the working-directory probe file was removed");
            check(nfsu::same_file_path(fs::path("NFSU_SAME_PATH_PROBE.TGA"), bare) ==
                      cwd_folds_case,
                  "a relative case alias follows the working directory's answer");
        }
    }
    // The directory that would hold the file does not exist either, so the
    // answer comes from the nearest directory that does.
    check(nfsu::same_file_path(root / "nothing" / "absent.tga",
                               root / "NOTHING" / "ABSENT.TGA") == folds_case,
          "a case alias under a directory that does not exist is decided too");

    // Directories the entry probe cannot answer for: an empty one, and an
    // empty one whose own name has no letter to flip either, so the walk up
    // to the scratch directory's name is what answers.
    const fs::path empty_dir = root / "emptydir";
    const fs::path digits_dir = root / "123";
    check(fs::create_directory(empty_dir, error) && !error, "empty directory created");
    check(fs::create_directory(digits_dir, error) && !error, "digit directory created");
    check(nfsu::same_file_path(empty_dir / "x.tga", empty_dir / "X.TGA") == folds_case,
          "an empty directory still decides a case alias");
    check(nfsu::same_file_path(digits_dir / "x.tga", digits_dir / "X.TGA") == folds_case,
          "a directory with no letter in its name decides it from its parent");

    // Paths that do exist: fs::equivalent answers, and the spellings above
    // must not change that answer.
    const fs::path present = root / "present.bin";
    {
        std::ofstream stream(present, std::ios::binary);
        stream.write("present", 7);
    }
    const fs::path other = root / "other.bin";
    {
        std::ofstream stream(other, std::ios::binary);
        stream.write("other", 5);
    }
    check(nfsu::same_file_path(present, root / "." / "present.bin"),
          "an existing file is one file under a \".\" spelling");
    check(nfsu::same_file_path(present, root / "emptydir" / ".." / "present.bin"),
          "and under a \"..\" through a directory that does exist");
    check(!nfsu::same_file_path(present, other), "two existing files are not one file");
    check(nfsu::same_file_path(present, root / "PRESENT.BIN") == folds_case,
          "an existing file under a case alias follows the filesystem");


    // An entry the filesystem will not resolve says nothing about case, and
    // taking it for an answer reports a case-folding volume as
    // case-sensitive - which is the whole guard, gone, for one dangling
    // junction sorting first in the output directory. The probe directory
    // here holds exactly one entry and it dangles.
    const fs::path dangling_dir = root / "adangling";
    check(fs::create_directory(dangling_dir, error) && !error,
          "dangling-probe directory created");
    std::error_code dangling_error;
    make_symlink(dangling_dir / "gone.bin", dangling_dir / "aaa_link", dangling_error);
    if (dangling_error) {
        std::cout << "note: dangling-entry case skipped (" << dangling_error.message()
                  << ")\n";
    } else {
        check(entry_exists(dangling_dir / "aaa_link"), "the dangling link is listed");
        check(!fs::exists(dangling_dir / "aaa_link", error),
              "and does not resolve to anything");
        check(nfsu::same_file_path(dangling_dir / "x.tga", dangling_dir / "X.TGA") ==
                  folds_case,
              "a directory whose only entry dangles still decides a case alias");
        check(!nfsu::same_file_path(dangling_dir / "x.tga", dangling_dir / "y.tga"),
              "and still separates two different names");
    }

#if defined(_WIN32)
    // The extended-length prefix. This standard library does not parse it,
    // so fs::absolute turns "\\?\C:\dir\out.tga" into "C:\?\C:\dir\out.tga"
    // and the plain spelling of the same file compares different - a dump
    // published at the screenshot's path again.
    {
        const fs::path plain = fs::absolute(absent, error);
        fs::path extended(L"\\\\?\\");
        extended += plain.native();
        check(nfsu::same_file_path(extended, plain),
              "an extended-length prefix names the same file as the plain spelling");
        check(nfsu::same_file_path(plain, extended), "in both directions");
        check(!nfsu::same_file_path(extended, fs::absolute(root / "absent2.tga", error)),
              "and still separates two different names");
    }

    // A network path is absolute, but not to this standard library, which
    // parses no root name for it. Left to fs::absolute it is rebased onto
    // the current drive, which invents an alias with an unrelated local
    // path and hides the one between two spellings of itself.
    {
        const fs::path unc(L"\\\\aserver\\ashare\\out.tga");
        const fs::path dotted_unc(L"\\\\aserver\\ashare\\.\\out.tga");
        const fs::path local(L"C:\\aserver\\ashare\\out.tga");
        check(nfsu::same_file_path(unc, dotted_unc),
              "two spellings of one network path are one file");
        check(!nfsu::same_file_path(unc, local),
              "a network path is not an unrelated local path");
        fs::path extended_unc(L"\\\\?\\UNC\\aserver\\ashare\\out.tga");
        check(nfsu::same_file_path(unc, extended_unc),
              "and the extended spelling of it is the same file");
    }

    // Case folding beyond ASCII. NTFS folds it; the comparison asks the OS
    // rather than its own A-Z table. The literals are escapes so that this
    // file stays ASCII: U+0426 U+0415 U+041B U+042C and their lower case.
    {
        const fs::path upper_dir = root / fs::path(L"\x0426\x0415\x041B\x042C");
        check(fs::create_directory(upper_dir, error) && !error,
              "a non-ASCII directory was created");
        const fs::path lower_dir = root / fs::path(L"\x0446\x0435\x043B\x044C");
        const bool folds_beyond_ascii = fs::exists(lower_dir, error) && !error;
        check(nfsu::same_file_path(upper_dir / "out.tga", lower_dir / "out.tga") ==
                  folds_beyond_ascii,
              "a non-ASCII case alias follows the filesystem");
    }
#endif

    // The writers use the same comparison, so a target that names a source
    // under any of those spellings is refused and nothing is written.
    Canvas canvas(2, 2);
    const nfsu::ClipVertex tri[3] = {at(canvas, 0, 0), at(canvas, 0, 2), at(canvas, 2, 2)};
    canvas.draw(tri, 1);
    nfsu::FrameWriteResult result;
    const fs::path future = root / "future.tga";
    check(!nfsu::write_frame_tga(canvas.frame, root / "." / "future.tga", {future},
                                 result) &&
              result.error.find("must differ") != std::string::npos,
          "a target naming a source that does not exist yet is refused: " + result.error);
    check(!fs::exists(future, error) && !fs::exists(root / "future.tga.partial", error),
          "nothing was created by the refused write");
    if (folds_case) {
        check(!nfsu::write_frame_tga(canvas.frame, root / "FUTURE.TGA", {future},
                                     result) &&
                  result.error.find("must differ") != std::string::npos,
              "a target naming a source under a case alias is refused");
        check(!fs::exists(root / "FUTURE.TGA", error),
              "and nothing was created by that refusal either");
    }
}

void test_write() {
    const ScratchDir scratch;
    check(scratch.ok(), "scratch directory created");
    if (!scratch.ok()) {
        return;
    }
    const fs::path root = scratch.path();
    std::error_code error;
    check(nfsu::frame_writer_supported(), "writer supported on this platform");

    Canvas canvas(4, 3);
    const nfsu::ClipVertex tri[3] = {at(canvas, 0, 0), at(canvas, 0, 3), at(canvas, 4, 3)};
    canvas.draw(tri, 1);

    const fs::path source = root / "GEOMETRY.BIN";
    {
        std::ofstream stream(source, std::ios::binary);
        stream.write("source", 6);
    }
    const fs::path target = root / "frame.tga";
    nfsu::FrameWriteResult result;
    check(nfsu::write_frame_tga(canvas.frame, target, {source}, result),
          "write frame: " + result.error);
    check(read_file(target) == tga_bytes(canvas.frame), "written bytes match write_tga");
    check(!fs::exists(root / "frame.tga.partial"), "no partial left");

    check(!nfsu::write_frame_tga(canvas.frame, target, {source}, result) &&
              result.error.find("refusing to overwrite") != std::string::npos,
          "existing target refused");
    check(read_file(target) == tga_bytes(canvas.frame), "existing target untouched");

    check(!nfsu::write_frame_tga(canvas.frame, source, {source}, result),
          "source overwrite refused");
    check(read_file(source) == "source", "source untouched");
    check(!nfsu::write_frame_tga(canvas.frame, root / "sub" / ".." / "GEOMETRY.BIN",
                                 {source}, result),
          "source overwrite via another spelling refused");

    const fs::path second = root / "second.tga";
    const fs::path stale = root / "second.tga.partial";
    {
        std::ofstream stream(stale, std::ios::binary);
        stream.write("stale", 5);
    }
    check(!nfsu::write_frame_tga(canvas.frame, second, {source}, result) &&
              result.error.find("temporary file already exists") != std::string::npos,
          "existing partial refused");
    check(read_file(stale) == "stale" && !fs::exists(second), "existing partial untouched");
    fs::remove(stale, error);

    // Failure after the partial file is complete: it is rolled back.
    nfsu::FrameWriteResult aborted;
    bool hook_saw_partial = false;
    aborted.before_rename = [&](const fs::path& partial) {
        // The writer holds the partial open without delete sharing, so only
        // metadata is checked here.
        std::error_code size_error;
        hook_saw_partial = fs::file_size(partial, size_error) ==
                               tga_bytes(canvas.frame).size() &&
                           !size_error;
        return false;
    };
    check(!nfsu::write_frame_tga(canvas.frame, second, {source}, aborted), "aborted write");
    check(hook_saw_partial, "partial file complete before rename");
    check(aborted.error.find("removed all 1") != std::string::npos,
          "rollback reported: " + aborted.error);
    check(aborted.not_removed.empty(), "nothing left behind");
    check(!fs::exists(second) && !fs::exists(stale), "no target and no partial after rollback");

    // A target that appears during the write is not replaced.
    nfsu::FrameWriteResult raced;
    raced.before_rename = [&](const fs::path&) {
        std::ofstream stream(second, std::ios::binary);
        stream.write("other", 5);
        return true;
    };
    check(!nfsu::write_frame_tga(canvas.frame, second, {source}, raced) &&
              raced.error.find("not replaced") != std::string::npos,
          "late target not replaced");
    check(read_file(second) == "other" && !fs::exists(stale), "late target untouched");

    check(!nfsu::write_frame_tga(canvas.frame, root / "missing" / "x.tga", {source}, result),
          "missing directory fails");
    check(!fs::exists(root / "missing"), "missing directory not created");

    nfsu::Frame empty;
    check(!nfsu::write_frame_tga(empty, root / "empty.tga", {source}, result),
          "empty frame refused");

    // A symlink at the partial name, live or dangling, is never followed
    // or replaced. Creating symlinks needs a privilege on some Windows
    // setups; the case is skipped (and says so) when the OS refuses.
    const fs::path victim = root / "victim.txt";
    {
        std::ofstream stream(victim, std::ios::binary);
        stream.write("victim", 6);
    }
    const fs::path linked = root / "linked.tga";
    const fs::path linked_partial = root / "linked.tga.partial";
    std::error_code link_error;
    make_symlink(victim, linked_partial, link_error);
    if (link_error) {
        std::cout << "note: symlink cases skipped (" << link_error.message() << ")\n";
    } else {
        check(!nfsu::write_frame_tga(canvas.frame, linked, {source}, result) &&
                  result.error.find("temporary file already exists") != std::string::npos,
              "symlink partial refused: " + result.error);
        check(read_file(victim) == "victim", "symlink target untouched");
        check(is_link(linked_partial) && !entry_exists(linked),
              "symlink left in place, no target");
        check(remove_entry(linked_partial) && read_file(victim) == "victim",
              "symlink removed without touching its target");

        const fs::path dangling = root / "dangling.tga";
        const fs::path dangling_partial = root / "dangling.tga.partial";
        const fs::path nowhere = root / "nowhere.bin";
        make_symlink(nowhere, dangling_partial, link_error);
        check(!link_error, "dangling symlink created");
        check(!nfsu::write_frame_tga(canvas.frame, dangling, {source}, result) &&
                  result.error.find("temporary file already exists") != std::string::npos,
              "dangling symlink partial refused: " + result.error);
        check(!entry_exists(nowhere) && !entry_exists(dangling) &&
                  is_link(dangling_partial),
              "dangling symlink not followed");
        check(remove_entry(dangling_partial), "dangling symlink removed");

        // A dangling symlink as the target is an existing entry: it is
        // neither followed nor replaced.
        const fs::path dangling_target = root / "dangling_target.tga";
        const fs::path elsewhere = root / "elsewhere.bin";
        make_symlink(elsewhere, dangling_target, link_error);
        check(!link_error, "dangling target symlink created");
        check(!nfsu::write_frame_tga(canvas.frame, dangling_target, {source}, result) &&
                  result.error.find("refusing to overwrite") != std::string::npos,
              "dangling symlink target refused: " + result.error);
        check(is_link(dangling_target) && !entry_exists(elsewhere) &&
                  !entry_exists(root / "dangling_target.tga.partial"),
              "dangling symlink target untouched");

        // Publication never replaces a dangling link that appears late.
        const fs::path late = root / "late.tga";
        nfsu::FrameWriteResult late_result;
        late_result.before_rename = [&](const fs::path&) {
            std::error_code late_error;
            make_symlink(elsewhere, late, late_error);
            return !late_error;
        };
        check(!nfsu::write_frame_tga(canvas.frame, late, {source}, late_result) &&
                  late_result.error.find("not replaced") != std::string::npos,
              "late dangling symlink target not replaced: " + late_result.error);
        check(is_link(late) && !entry_exists(elsewhere) &&
                  !entry_exists(root / "late.tga.partial") && late_result.not_removed.empty(),
              "late dangling link untouched, partial rolled back");

        // A symlink as the target is an existing entry and is refused.
        const fs::path link_target = root / "link_target.tga";
        make_symlink(victim, link_target, link_error);
        check(!link_error, "target symlink created");
        check(!nfsu::write_frame_tga(canvas.frame, link_target, {source}, result),
              "symlink target refused");
        check(read_file(victim) == "victim", "symlink target file untouched");
    }
}

void test_scratch_dir() {
    fs::path first_path;
    {
        const ScratchDir first;
        const ScratchDir second;
        check(first.ok() && second.ok(), "two scratch directories");
        check(first.path() != second.path(), "scratch directories are distinct");
        const std::string name = first.path().filename().u8string();
        bool ascii = !name.empty();
        for (char ch : name) {
            ascii = ascii && static_cast<unsigned char>(ch) < 0x80U;
        }
        check(ascii, "scratch name is ASCII");
        first_path = first.path();
        std::ofstream stream(first.path() / "inner.txt", std::ios::binary);
        stream.write("x", 1);
        std::error_code error;
        fs::create_directory(first.path() / "sub", error);
        // Regression: a dangling link used to make cleanup spin forever.
        make_symlink(first.path() / "missing.bin", first.path() / "sub" / "dangling", error);
        make_symlink(first.path() / "inner.txt", first.path() / "live", error);
    }
    check(!entry_exists(first_path), "scratch directory removed on scope exit");
}

} // namespace

int main() {
    test_trig();
    test_camera();
    test_projection();
    test_near_clipping();
    test_viewport_bounds();
    test_fill_rule_exact();
    test_shared_edges();
    test_winding_and_culling();
    test_degenerate();
    test_depth_order();
    test_perspective_uv();
    test_uv_wrap_and_flip();
    test_alpha();
    test_material_state_decoders();
    test_material_gates();
    test_material_alpha_test();
    test_car_linear_repeat_sample();
    test_blend_modes();
    test_environment_term();
    test_material_coefficient_validation();
    test_material_state_scene();
    test_draw_layer_order();
    test_draw_order_rules();
    test_environment_scene();
    test_vertex_color_and_fallback();
    test_bindings();
    test_scene();
    test_golden_and_rerender();
    test_limits();
    test_texture_decode();
    test_texture_sources();
    test_alpha_default();
    test_scratch_dir();
    test_same_file_path();
    test_write();

    if (g_failures != 0) {
        std::cerr << g_failures << " render test(s) failed\n";
        return 1;
    }
    std::cout << "render tests passed\n";
    return 0;
}
