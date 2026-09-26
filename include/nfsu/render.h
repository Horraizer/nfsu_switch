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
// One rounding to binary64 on the supported x86 and AArch64 render targets.
// MinGW's out-of-line sqrt can round twice through x87 extended precision.
double det_sqrt(double value) noexcept;

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

// Where one source vertex lands in the Y-up world frame above, for one
// drawn copy. `transform` is a PreparedInstance::transform, or null for a
// mesh drawn once in its own space, which is what a profile with no
// instances asks for.
//
// With no transform the mapping is `world = (px, py, -pz)`, the one this
// renderer has always used, so a profile with no instances produces the
// frames of every earlier build bit for bit. With one, the vertex is
// written in the raw slot frame (longitudinal, lateral, vertical) =
// (px, pz, py), multiplied as a row vector by the row-major matrix, and
// mapped back:
//
//   slot  = (px, pz, py)
//   moved = slot * transform          (row 3 is the translation)
//   world = (moved[0], moved[2], -moved[1])
//
// Only the affine part is read: elements 3, 7, 11 and 15, the fourth
// column of the row-vector convention, are not. Every transform this
// project produces leaves that column at (0, 0, 0, 1), where reading it
// would change nothing, and a placement that did not would be a projective
// part transform the retail draw path has never been seen to use. See
// docs/RENDERER.md, "Instances", for the evidence and for that divergence.
//
// This is the one rule for the placement; render_scene draws with it and
// Inspector::world_bounds measures with it, so an overlay can never
// describe a position the frame does not show.
Vec3 placed_vertex(const MeshVertex& vertex, const float* transform) noexcept;

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

// Optional observation at the actual CPU submission boundary, before clipping.
// One nonempty material range of one copy. kNoHandle means no placement/texture.
// The observer must not mutate the scene; exceptions propagate to the caller.
struct ReferenceDraw {
    std::size_t group = 0;
    std::size_t mesh = 0;
    std::size_t instance = kNoHandle;
    std::size_t texture = kNoHandle;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    bool depth_less_equal = true;
    bool depth_write = true;
    bool color_write_rgb = true;
    CullMode cull = CullMode::none;
    RenderBlendMode blend = RenderBlendMode::disabled;
    bool alpha_test = false;
    std::uint8_t alpha_ref = kRenderAlphaRef;
};
using ReferenceDrawObserver = std::function<void(const ReferenceDraw&)>;

enum class ShadingMode { none, flat };

const char* shading_mode_name(ShadingMode mode) noexcept;

// Fixed directional light for flat shading, normalized at use.
constexpr double kLightX = 0.35;
constexpr double kLightY = 0.85;
constexpr double kLightZ = 0.40;

// Whether the renderer applies the retail material render state recovered
// in docs/MATERIALS.md.
enum class MaterialStateMode {
    // Every draw is opaque. Texel alpha reaches nothing but the diagnostic
    // alpha_cutoff, and there is no blending. This is what the renderer has
    // always done and it stays the default, so a frame rendered without
    // selecting the retail state is byte for byte the frame of any earlier
    // build.
    off,
    // Apply the proven state: the texture-info alpha test and its GREATER
    // 0xAA rule, the texture-info blend selection, the flag-controlled
    // coefficient gates, and the bounded transparent draw order. State is
    // taken only from a draw's own resolved texture; a draw with no
    // resolved texture has no proven state and stays opaque.
    retail
};

// "off" or "retail".
const char* material_state_mode_name(MaterialStateMode mode) noexcept;

// Explicit values for the car-effect coefficients the retail data does not
// yet yield. Nothing here is a retail value and the renderer never invents
// one: the coefficient objects, the harmonic lighting and the EnvMapTex
// cube map are all still undecoded (docs/MATERIALS.md). The defaults make
// the environment term vanish and leave the alpha lane at the identity 1,
// which is an assumption and not a retail value; selecting
// MaterialStateMode::retail alone therefore changes no coefficient.
// Supplying values is a diagnostic.
struct MaterialCoefficients {
    // diffuse_scale.w = alpha_min + vdotn * alpha_range. The effect uses it
    // as the output alpha scale and as the environment term scale. The
    // defaults 1 and 0 leave the source alpha equal to the texel alpha,
    // which is the identity, not a claim about the retail coefficient.
    double alpha_min = 1.0;
    double alpha_range = 0.0;
    // EnvmapMin and EnvmapRange, per channel, and the constant colour used
    // in place of a cube-map sample. All-zero min and range mean no term.
    double environment_min[3] = {0.0, 0.0, 0.0};
    double environment_range[3] = {0.0, 0.0, 0.0};
    unsigned char environment_color[3] = {0, 0, 0};
    // EnvmapPower. The effect stores a float exponent; the reference takes
    // only a whole number and evaluates it by repeated multiplication, so
    // the frame does not depend on a C library pow().
    std::uint32_t environment_power = 1;
    // Stands for the runtime global the coefficient upload also tests
    // before it keeps the environment vectors.
    bool environment_enabled = true;
};

constexpr double kMaxMaterialCoefficient = 16.0;
constexpr std::uint32_t kMaxEnvironmentPower = 64;

// Checks that every coefficient is finite, that alpha_min, alpha_range and
// the environment vectors are in 0..kMaxMaterialCoefficient, and that
// environment_power is at most kMaxEnvironmentPower.
bool validate_material_coefficients(
    const MaterialCoefficients& coefficients, std::string& error);

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
    // Off by default; see MaterialStateMode. The alpha_cutoff above stays a
    // separate diagnostic, and with the retail state selected a textured
    // fragment must pass both tests.
    MaterialStateMode material_state = MaterialStateMode::off;
    MaterialCoefficients coefficients;
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

// Bytes allocated per frame pixel: RGBA8, f64 depth, u32 triangle id and
// the u8 blended mark.
constexpr std::uint64_t kFrameBytesPerPixel = 17;

struct RenderStats {
    std::uint64_t meshes = 0;
    std::uint64_t meshes_rendered = 0;
    std::uint64_t meshes_skipped_invalid = 0;
    // Copies that actually submitted a triangle, so 30 instances of 24
    // meshes never reads as 30 meshes. It is counted from the draw loop and
    // not from the scene: a copy of a mesh whose every material group is
    // undrawable submits nothing and is not counted here, although
    // meshes_rendered still counts the mesh. For the stock car a copy is a
    // PreparedInstance; for a profile that carries no instances each mesh
    // is one copy, drawn in its own space.
    std::uint64_t instances = 0;
    // Of those, the copies whose PreparedInstance::placement_exact is set
    // and the copies where it is not. A profile with no instances counts
    // every mesh as exact: its own authored space is where the mesh is,
    // and no placement is being claimed for it. instances_stand_in counts
    // copies the profile placed from a documented stand-in rather than
    // from the transform the retail draw path applies, so a frame with a
    // nonzero count shows at least one part in a position the project has
    // not reproduced.
    std::uint64_t instances_exact = 0;
    std::uint64_t instances_stand_in = 0;
    std::uint64_t material_groups = 0;
    std::uint64_t groups_resolved = 0;
    std::uint64_t groups_unresolved = 0;
    std::uint64_t groups_unusable = 0;
    std::uint64_t groups_invalid = 0;
    // Drawable groups the retail material state gave an alpha test or a
    // blend. Both stay 0 with MaterialStateMode::off.
    std::uint64_t groups_alpha_tested = 0;
    std::uint64_t groups_blended = 0;
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
    // Fragments written by a blended draw, and those of them that landed on
    // a pixel a blended draw had already written. The second counter reports
    // how much transparency stacks in the frame. It is a diagnostic, not a
    // proof of order independence: a blended draw writes depth, as the
    // retail path does, so the draw order also decides which later blended
    // fragments are rejected outright. See docs/RENDERER.md.
    std::uint64_t fragments_blended = 0;
    std::uint64_t fragments_blend_overlap = 0;
    std::uint64_t pixels_drawn = 0;
    std::uint64_t pixels_fallback = 0;
    std::uint64_t pixels_blended = 0;
    // Drawable groups per retail draw queue row, in row order. All zero
    // with MaterialStateMode::off, which assigns no rows.
    std::uint64_t draw_rows[kRenderDrawLayerCount] = {};
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
    // 1 where a blended fragment has been written, 0 elsewhere. Always 0
    // with MaterialStateMode::off.
    const std::vector<unsigned char>& blended() const noexcept {
        return blended_;
    }

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
    std::vector<unsigned char> blended_;
};

// Per-triangle material state for the rasterizer. Everything below the
// shade factor is the retail material state; its defaults are the opaque
// state, so a caller that does not set them gets the historical behaviour.
struct RasterMaterial {
    const RenderTexture* texture = nullptr;  // null draws the fallback
    unsigned char fallback[4] = {255, 255, 255, 255};
    // Flat shade factor 0..256 applied to RGB.
    std::uint32_t shade = 256;
    bool fallback_pattern = true;
    // Discard a textured fragment unless its source alpha is strictly
    // above alpha_ref (D3DCMP_GREATER).
    bool alpha_test = false;
    std::uint8_t alpha_ref = kRenderAlphaRef;
    RenderBlendMode blend = RenderBlendMode::disabled;
    // Source alpha is the texel alpha times this factor, rounded and
    // clamped to 0..255. 1 is the identity.
    double alpha_scale = 1.0;
    // The diffuse coefficient gate is closed. The traced upload then zeros
    // every lane of DiffuseMin and DiffuseRange, which makes the whole
    // effect output zero, colour as well as alpha, whatever the undecoded
    // coefficients and lighting hold. The fragment is written black and
    // contributes nothing through either blend.
    bool diffuse_zeroed = false;
    // Environment term for this triangle, in 0..255 units per channel and
    // already scaled by everything but the texel alpha; the fragment adds
    // environment[k] * texel_alpha / 255. Zero unless the environment gate
    // keeps the term. It is added to textured fragments only.
    double environment[3] = {0.0, 0.0, 0.0};
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
//
// A prepared mesh is drawn once per instance that names it, in ascending
// instance order, with PreparedInstance::transform applied. A profile that
// carries no instances draws each of its meshes exactly once in its own
// space, by the same arithmetic as before instances existed, so its frames
// are unchanged byte for byte.
//
// The composition. PreparedInstance::transform is row-major in the
// geometry file's raw slot frame (longitudinal, lateral, vertical) =
// (px, pz, py), row-vector, and this renderer's world frame is Y-up
// (px, py, -pz). A vertex is therefore placed as
//
//   slot  = (v.px, v.pz, v.py)
//   moved = slot * transform          (row-vector, row 3 is the translation)
//   world = (moved[0], moved[2], -moved[1])
//
// and the camera then looks at that world position. The instance transform
// is applied before the camera and after nothing: the scene holds one car
// at the car's own matrix, which is the identity here, so a body part's
// identity transform leaves the vertex exactly where the per-mesh path put
// it. docs/RENDERER.md states the evidence for each half.
bool render_scene(
    const PreparedCarScene& scene,
    const RenderOptions& options,
    const RenderLimits& limits,
    Frame& frame,
    RenderStats& stats,
    std::string& error,
    const ReferenceDrawObserver& observe = {});

struct FrameWriteResult {
    std::string error;
    // Files created by the failed call that are still present after the
    // rollback attempt.
    std::vector<std::filesystem::path> not_removed;
    // Test hook, called with the complete partial file before publication;
    // returning false makes the write fail and roll back.
    std::function<bool(const std::filesystem::path&)> before_rename;
};

// True when the two paths name one file, for paths that need not exist.
//
// It is the comparison every "this output must differ from that file"
// refusal in this project rests on, and it is asked mostly about paths that
// do not exist yet: an output, a temporary "<target>.partial", a generated
// texture dump. fs::equivalent answers only for paths that both exist, so
// it is tried first and the names are compared when it cannot:
//
// - Both sides are made absolute before they are normalized. Normalizing
//   alone leaves a bare "out.tga" relative and "./out.tga" absolute, and
//   the two then compare unequal although they name one file.
// - "." and ".." are removed. Symbolic links are resolved wherever the
//   standard library resolves them, which on this Windows toolchain is
//   nowhere: fs::weakly_canonical dereferences no junction and no directory
//   symbolic link there, so two paths reaching one directory through
//   different links stay different.
// - On Windows the extended-length prefix "\\?\" is removed and a
//   "\\server\share" path is left absolute, because this standard library
//   parses neither as a root and fs::absolute would otherwise rewrite both
//   onto the current drive.
// - Two spellings that differ only in case are one file wherever the
//   filesystem folds case. The filesystem is asked, read-only, by spelling
//   a name it already holds with the case flipped and seeing whether it
//   still answers; nothing is created, opened for writing or changed. When
//   it cannot be asked, the answer is that they are one file, because this
//   decides a refusal. The fold itself is the OS ordinal fold on Windows
//   and A-Z elsewhere.
//
// What it does not see, and therefore lets through: a junction or symbolic
// link in the middle of the path (above), an 8.3 short name against its
// long name, a second drive letter for one directory (subst, a mapped
// share), a network path against the local path for the same file, and
// case folding outside ASCII on a platform whose standard library is not
// asked. It also compares names and not contents: two hard links to a file
// that exists are equivalent, but two names for a file that does not exist
// yet can only be compared as names.
//
// It does not throw. Every caller uses it to refuse and none has a handler,
// so an allocation that throws while comparing answers "one file", which is
// the direction that cannot publish a file at a name the caller did not
// ask for.
bool same_file_path(const std::filesystem::path& a, const std::filesystem::path& b);

// True when write_frame_tga has exclusive-create and no-replace primitives
// on this platform (Windows and POSIX). Elsewhere it fails closed.
bool frame_writer_supported() noexcept;

// Writes the frame as a TGA through write_tga, never replacing any
// existing file:
// - A target that names any source path, compared with same_file_path, is
//   refused, and an existing target is refused up front.
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
