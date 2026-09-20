#pragma once

#include "nfsu/scene.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace nfsu {

// Deterministic CPU reference renderer. See docs/RENDERER.md for the exact
// camera, projection, clipping, coverage, depth, sampling and alpha rules.

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Sine and cosine of an angle in degrees, computed with fixed polynomials
// and IEEE basic operations only, so results do not depend on the C library.
double det_sin_deg(double degrees) noexcept;
double det_cos_deg(double degrees) noexcept;

// Orbit camera in the Y-up world frame world = (px, py, -pz):
//   eye = target + distance * (cos(pitch) cos(yaw), sin(pitch),
//                              cos(pitch) sin(yaw))
// yaw 0 looks at the car front from +X, yaw 90 looks at the right side from
// +Z, pitch > 0 looks from above. up = +Y. fov_y is the full vertical field
// of view. Points closer than near_plane are clipped, points with a depth
// beyond far_plane are discarded per pixel.
struct OrbitCamera {
    double yaw_deg = 35.0;
    double pitch_deg = 15.0;
    double distance = 7.0;
    Vec3 target{0.2, 0.35, 0.0};
    double fov_y_deg = 35.0;
    double near_plane = 0.05;
    double far_plane = 100.0;
};

constexpr double kMinPitch = -89.0;
constexpr double kMaxPitch = 89.0;
constexpr double kMaxYawMagnitude = 36000.0;
constexpr double kMinDistance = 0.01;
constexpr double kMaxDistance = 10000.0;
constexpr double kMaxTargetMagnitude = 10000.0;
constexpr double kMinFov = 1.0;
constexpr double kMaxFov = 170.0;
constexpr double kMinNear = 1e-4;
constexpr double kMaxNear = 10.0;
constexpr double kMaxFar = 100000.0;

// Checks that every camera value is finite and inside the ranges above and
// that near_plane < far_plane.
bool validate_camera(const OrbitCamera& camera, std::string& error);

Vec3 orbit_eye(const OrbitCamera& camera) noexcept;

// Right-handed look-at basis plus a D3D-style projection:
//   view = (dot(right, p - eye), dot(up, p - eye), dot(forward, p - eye))
//   clip = (view.x * focal_x, view.y * focal_y,
//           (view.z - near) * far / (far - near), view.z)
// so after division x, y are in [-1, 1] inside the frame, z is 0 at the near
// plane and 1 at the far plane, and w is the distance along forward.
struct CameraFrame {
    Vec3 eye;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
    double focal_x = 1.0;
    double focal_y = 1.0;
    double near_plane = 0.05;
    double far_plane = 100.0;
};

bool make_camera_frame(
    const OrbitCamera& camera,
    std::uint32_t width,
    std::uint32_t height,
    CameraFrame& frame,
    std::string& error);

// Clip-space vertex with its attributes. r, g, b, a are 0..255.
struct ClipVertex {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
    double u = 0.0;
    double v = 0.0;
    double r = 255.0;
    double g = 255.0;
    double b = 255.0;
    double a = 255.0;
};

ClipVertex project_point(const CameraFrame& frame, const Vec3& world) noexcept;

// Guard band: clip-space x and y are clipped to [-g w, g w].
constexpr double kGuardBand = 8.0;
constexpr std::size_t kMaxClipVertices = 9;

// Sutherland-Hodgman clipping of one triangle against w >= near and the
// guard band. Intersections are always computed from the inside vertex to
// the outside one, so shared edges give identical vertices. Returns the
// number of polygon vertices written (0 or 3..kMaxClipVertices); the polygon
// keeps the input orientation and is drawn as a fan from out[0].
std::size_t clip_triangle(
    const ClipVertex (&input)[3],
    double near_plane,
    ClipVertex (&out)[kMaxClipVertices]);

// Deterministic fallback color for a hash that is not drawn with a texture.
// Channels are in 64..223 and alpha is 255.
void fallback_color(std::uint32_t hash, unsigned char (&rgba)[4]) noexcept;

enum class CullMode { none, back, front };

const char* cull_mode_name(CullMode mode) noexcept;

enum class ShadingMode { none, flat };

const char* shading_mode_name(ShadingMode mode) noexcept;

// Fixed directional light for flat shading, normalized at use.
constexpr double kLightX = 0.35;
constexpr double kLightY = 0.85;
constexpr double kLightZ = 0.40;

struct RenderOptions {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    OrbitCamera camera;
    // Sample (u, 1 - v) instead of (u, v).
    bool flip_v = false;
    // Texels with alpha below the cutoff are discarded. The default 0 draws
    // every texel's decoded RGB opaquely; material flags and blending are
    // not decoded yet, so a nonzero cutoff is a diagnostic.
    std::uint8_t alpha_cutoff = 0;
    // Multiply by the vertex color bytes 0..2 (documented as RGB).
    bool vertex_color = false;
    CullMode cull = CullMode::none;
    ShadingMode shading = ShadingMode::flat;
    unsigned char background[4] = {40, 44, 52, 255};
};

struct RenderLimits {
    std::uint32_t max_width = 8192;
    std::uint32_t max_height = 8192;
    std::uint64_t max_pixels = 16777216;
    std::uint64_t max_source_triangles = 1U << 22;
    std::uint64_t max_raster_triangles = 1U << 24;
    // Triangles over all drawable ranges (ranges may overlap).
    std::uint64_t max_submitted_triangles = 1U << 24;
    std::uint64_t max_pixel_tests = 1ULL << 32;
};

// Bytes allocated per frame pixel: RGBA8, f64 depth, u32 triangle id.
constexpr std::uint64_t kFrameBytesPerPixel = 16;

struct RenderStats {
    std::uint64_t meshes = 0;
    std::uint64_t meshes_rendered = 0;
    std::uint64_t meshes_skipped_invalid = 0;
    std::uint64_t material_groups = 0;
    std::uint64_t groups_resolved = 0;
    std::uint64_t groups_unresolved = 0;
    std::uint64_t groups_unusable = 0;
    std::uint64_t groups_invalid = 0;
    std::uint64_t unresolved_hashes = 0;
    std::uint64_t triangles_submitted = 0;
    std::uint64_t triangles_resolved = 0;
    std::uint64_t triangles_fallback = 0;
    std::uint64_t triangles_bad_index = 0;
    std::uint64_t triangles_degenerate_source = 0;
    std::uint64_t triangles_outside = 0;
    std::uint64_t triangles_clipped = 0;
    std::uint64_t triangles_degenerate_screen = 0;
    std::uint64_t triangles_culled = 0;
    std::uint64_t triangles_front = 0;
    std::uint64_t triangles_back = 0;
    std::uint64_t triangles_rasterized = 0;
    std::uint64_t raster_triangles = 0;
    std::uint64_t pixel_tests = 0;
    std::uint64_t fragments = 0;
    std::uint64_t fragments_alpha_discarded = 0;
    std::uint64_t fragments_far_discarded = 0;
    std::uint64_t fragments_depth_failed = 0;
    std::uint64_t fragments_written = 0;
    std::uint64_t pixels_drawn = 0;
    std::uint64_t pixels_fallback = 0;
    // Bounds of drawn pixels, inclusive; valid when pixels_drawn > 0.
    std::uint32_t min_x = 0;
    std::uint32_t min_y = 0;
    std::uint32_t max_x = 0;
    std::uint32_t max_y = 0;
};

constexpr std::uint32_t kNoTriangle = 0xFFFFFFFFU;

class Frame {
public:
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    const std::vector<unsigned char>& rgba() const noexcept { return rgba_; }
    const std::vector<double>& depth() const noexcept { return depth_; }
    // Id of the triangle that owns each pixel, kNoTriangle if none.
    const std::vector<std::uint32_t>& ids() const noexcept { return ids_; }

    // Validates the size against the limits before allocating.
    bool reset(
        std::uint32_t width,
        std::uint32_t height,
        const unsigned char (&background)[4],
        const RenderLimits& limits,
        std::string& error);

private:
    friend class Rasterizer;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<unsigned char> rgba_;
    std::vector<double> depth_;
    std::vector<std::uint32_t> ids_;
};

// Per-triangle material state for the rasterizer.
struct RasterMaterial {
    const RenderTexture* texture = nullptr;  // null draws the fallback
    unsigned char fallback[4] = {255, 255, 255, 255};
    // Flat shade factor 0..256 applied to RGB.
    std::uint32_t shade = 256;
    bool fallback_pattern = true;
};

class Rasterizer {
public:
    Rasterizer(Frame& frame, const RenderOptions& options,
               const RenderLimits& limits, RenderStats& stats);

    // Clips, projects and fills one triangle. id is the draw-order
    // independent key used for depth ties (the larger id wins, which equals
    // a less-equal test in increasing id order). Returns false only when a
    // work limit is exceeded; error is set then.
    bool draw_triangle(
        const ClipVertex (&vertices)[3],
        const RasterMaterial& material,
        std::uint32_t id,
        std::string& error);

private:
    struct ScreenVertex;
    bool fill(const ScreenVertex* a, const ScreenVertex* b,
              const ScreenVertex* c, std::int64_t area,
              const RasterMaterial& material, std::uint32_t id,
              std::string& error);

    Frame& frame_;
    const RenderOptions& options_;
    const RenderLimits& limits_;
    RenderStats& stats_;
};

// Renders the drawable draws of a prepared scene in draw order (selected
// meshes in source order, material records in record order) with the
// binding fixed at preparation; it never looks up a texture hash. Triangle
// ids are the running number of submitted triangles. The scene is checked
// with validate_prepared_scene, and the options and limits are checked,
// before the frame is allocated, so a rejected scene leaves frame
// untouched. Vertex indices are checked per triangle (bad_index).
bool render_scene(
    const PreparedCarScene& scene,
    const RenderOptions& options,
    const RenderLimits& limits,
    Frame& frame,
    RenderStats& stats,
    std::string& error);

struct FrameWriteResult {
    std::string error;
    // Files created by the failed call that are still present after the
    // rollback attempt.
    std::vector<std::filesystem::path> not_removed;
    // Test hook, called with the complete partial file before publication;
    // returning false makes the write fail and roll back.
    std::function<bool(const std::filesystem::path&)> before_rename;
};

// True when write_frame_tga has exclusive-create and no-replace primitives
// on this platform (Windows and POSIX). Elsewhere it fails closed.
bool frame_writer_supported() noexcept;

// Writes the frame as a TGA through write_tga, never replacing any
// existing file:
// - A target equal to any source path is refused, and an existing target
//   is refused up front.
// - "<target>.partial" is created exclusively and without following a
//   symlink or reparse point at that name (Windows CreateFileW CREATE_NEW
//   with FILE_FLAG_OPEN_REPARSE_POINT; POSIX open O_CREAT | O_EXCL |
//   O_NOFOLLOW). If anything already exists there, the call fails and
//   leaves it alone.
// - The complete partial is published under the target name with a
//   no-replace operation (Windows SetFileInformationByHandle rename with
//   ReplaceIfExists = FALSE on the still-open handle; POSIX link() then
//   unlink() of the partial). A target that appears at any point before
//   publication makes the call fail and is left untouched.
// On failure after the partial was created, it is passed to
// rollback_created_files and error states the result; not_removed lists
// what could not be removed.
bool write_frame_tga(
    const Frame& frame,
    const std::filesystem::path& target,
    const std::vector<std::filesystem::path>& sources,
    FrameWriteResult& result);

// The same write, for an RGBA8 image that is not a rendered frame, for
// example a generated texture. rgba must hold exactly width * height * 4
// bytes. Every rule above applies unchanged, including the refusal to
// replace an existing target.
bool write_rgba_tga(
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgba,
    const std::filesystem::path& target,
    const std::vector<std::filesystem::path>& sources,
    FrameWriteResult& result);

} // namespace nfsu
