#include "nfsu/render.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define NFSU_WRITER_WINDOWS 1
#elif defined(__has_include)
#if __has_include(<unistd.h>) && __has_include(<fcntl.h>)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define NFSU_WRITER_POSIX 1
#endif
#endif

// x87 extended precision would change rounding; the renderer needs plain
// IEEE double arithmetic (SSE2 on x86, any AArch64).
static_assert(FLT_EVAL_METHOD == 0, "renderer requires FLT_EVAL_METHOD == 0");

namespace nfsu {

namespace {

namespace fs = std::filesystem;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr std::int64_t kSubpixelBits = 8;
constexpr std::int64_t kSubpixelScale = std::int64_t{1} << kSubpixelBits;
constexpr std::int64_t kSubpixelHalf = kSubpixelScale / 2;

// Taylor series around 0, accurate to well below 1 ulp for |x| <= pi / 4.
double sin_poly(double x) noexcept {
    const double x2 = x * x;
    double p = 1.0 / 355687428096000.0;           // 1/17!
    p = p * x2 - 1.0 / 1307674368000.0;           // 1/15!
    p = p * x2 + 1.0 / 6227020800.0;              // 1/13!
    p = p * x2 - 1.0 / 39916800.0;                // 1/11!
    p = p * x2 + 1.0 / 362880.0;                  // 1/9!
    p = p * x2 - 1.0 / 5040.0;                    // 1/7!
    p = p * x2 + 1.0 / 120.0;                     // 1/5!
    p = p * x2 - 1.0 / 6.0;                       // 1/3!
    return x + x * x2 * p;
}

double cos_poly(double x) noexcept {
    const double x2 = x * x;
    double p = 1.0 / 20922789888000.0;            // 1/16!
    p = p * x2 - 1.0 / 87178291200.0;             // 1/14!
    p = p * x2 + 1.0 / 479001600.0;               // 1/12!
    p = p * x2 - 1.0 / 3628800.0;                 // 1/10!
    p = p * x2 + 1.0 / 40320.0;                   // 1/8!
    p = p * x2 - 1.0 / 720.0;                     // 1/6!
    p = p * x2 + 1.0 / 24.0;                      // 1/4!
    p = p * x2 - 0.5;                             // 1/2!
    return 1.0 + x2 * p;
}

// Splits degrees into a quadrant 0..3 and a remainder in [-45, 45].
void reduce_degrees(double degrees, int& quadrant, double& radians) noexcept {
    if (!std::isfinite(degrees)) {
        quadrant = 0;
        radians = 0.0;
        return;
    }
    double d = std::fmod(degrees, 360.0);  // exact
    if (d < 0.0) {
        d += 360.0;
    }
    const double q = std::floor((d + 45.0) / 90.0);
    const double r = d - 90.0 * q;
    quadrant = static_cast<int>(q) & 3;  // q is in 0..4
    radians = r * kDegToRad;
}

Vec3 sub(const Vec3& a, const Vec3& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

double dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

bool normalize(Vec3& v) noexcept {
    const double length = std::sqrt(dot(v, v));
    if (!(length > 0.0) || !std::isfinite(length)) {
        return false;
    }
    v = {v.x / length, v.y / length, v.z / length};
    return true;
}

bool finite_vertex(const ClipVertex& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::isfinite(v.w) && std::isfinite(v.u) && std::isfinite(v.v) &&
           std::isfinite(v.r) && std::isfinite(v.g) && std::isfinite(v.b) &&
           std::isfinite(v.a);
}

ClipVertex lerp(const ClipVertex& a, const ClipVertex& b, double t) noexcept {
    ClipVertex out;
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    out.z = a.z + (b.z - a.z) * t;
    out.w = a.w + (b.w - a.w) * t;
    out.u = a.u + (b.u - a.u) * t;
    out.v = a.v + (b.v - a.v) * t;
    out.r = a.r + (b.r - a.r) * t;
    out.g = a.g + (b.g - a.g) * t;
    out.b = a.b + (b.b - a.b) * t;
    out.a = a.a + (b.a - a.a) * t;
    return out;
}

// Signed distance to one clip plane; inside when >= 0.
double plane_distance(const ClipVertex& v, int plane, double near_plane) noexcept {
    switch (plane) {
    case 0: return v.w - near_plane;
    case 1: return kGuardBand * v.w - v.x;
    case 2: return kGuardBand * v.w + v.x;
    case 3: return kGuardBand * v.w - v.y;
    default: return kGuardBand * v.w + v.y;
    }
}

std::size_t clip_plane(
    const ClipVertex* input,
    std::size_t count,
    int plane,
    double near_plane,
    ClipVertex* output) noexcept {
    std::size_t written = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const ClipVertex& current = input[i];
        const ClipVertex& next = input[(i + 1) % count];
        const double dc = plane_distance(current, plane, near_plane);
        const double dn = plane_distance(next, plane, near_plane);
        const bool current_in = dc >= 0.0;
        const bool next_in = dn >= 0.0;
        if (current_in) {
            output[written++] = current;
        }
        if (current_in != next_in) {
            // Always interpolate from the inside vertex so that an edge
            // shared by two triangles gives the same point in both.
            if (current_in) {
                output[written++] = lerp(current, next, dc / (dc - dn));
            } else {
                output[written++] = lerp(next, current, dn / (dn - dc));
            }
        }
    }
    return written;
}

// floor(a / b) for b > 0.
std::int64_t floor_div(std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

std::int64_t edge_value(
    std::int64_t ax, std::int64_t ay,
    std::int64_t bx, std::int64_t by,
    std::int64_t px, std::int64_t py) noexcept {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Top-left rule for edges of a triangle with positive edge-function area in
// y-down screen space: top edges are horizontal with dx > 0, left edges have
// dy < 0.
bool top_left(std::int64_t dx, std::int64_t dy) noexcept {
    return dy < 0 || (dy == 0 && dx > 0);
}

// Wrapped nearest texel index for coordinate t on an axis of size texels.
std::uint32_t wrap_texel(double t, std::uint32_t size) noexcept {
    if (!std::isfinite(t) || size == 0) {
        return 0;
    }
    const double f = t - std::floor(t);  // [0, 1]; 1 only by rounding
    const double scaled = f * static_cast<double>(size);
    if (!(scaled >= 0.0)) {
        return 0;
    }
    if (scaled >= static_cast<double>(size)) {
        return size - 1;
    }
    return static_cast<std::uint32_t>(scaled);
}

// Parity of floor(t * 8): the fallback checker.
bool checker_odd(double t) noexcept {
    if (!std::isfinite(t)) {
        return false;
    }
    const double cell = std::floor(t * 8.0);
    if (!std::isfinite(cell)) {
        return false;
    }
    return std::fabs(std::fmod(cell, 2.0)) == 1.0;
}

std::uint32_t channel(double value) noexcept {
    if (!(value > 0.0)) {  // also NaN
        return 0;
    }
    if (value >= 255.0) {
        return 255;
    }
    return static_cast<std::uint32_t>(std::floor(value + 0.5));
}

std::uint32_t mix32(std::uint32_t x) noexcept {
    x ^= x >> 16U;
    x *= 0x7FEB352DU;
    x ^= x >> 15U;
    x *= 0x846CA68BU;
    x ^= x >> 16U;
    return x;
}

bool same_file(const fs::path& a, const fs::path& b) {
    std::error_code error;
    if (fs::equivalent(a, b, error) && !error) {
        return true;
    }
    const fs::path abs_a = fs::absolute(a, error).lexically_normal();
    if (error) {
        return false;
    }
    const fs::path abs_b = fs::absolute(b, error).lexically_normal();
    return !error && abs_a == abs_b;
}

// True unless the path is known not to exist. A symlink counts as an entry
// even when it dangles. MinGW's symlink_status follows links on Windows, so
// Windows asks GetFileAttributesW, which does not follow a final link.
bool path_exists(const fs::path& path) {
#if defined(NFSU_WRITER_WINDOWS)
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    const DWORD code = GetLastError();
    return code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND;
#else
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    return status.type() != fs::file_type::not_found;
#endif
}

} // namespace

double det_sin_deg(double degrees) noexcept {
    int quadrant = 0;
    double x = 0.0;
    reduce_degrees(degrees, quadrant, x);
    switch (quadrant) {
    case 0: return sin_poly(x);
    case 1: return cos_poly(x);
    case 2: return -sin_poly(x);
    default: return -cos_poly(x);
    }
}

double det_cos_deg(double degrees) noexcept {
    int quadrant = 0;
    double x = 0.0;
    reduce_degrees(degrees, quadrant, x);
    switch (quadrant) {
    case 0: return cos_poly(x);
    case 1: return -sin_poly(x);
    case 2: return -cos_poly(x);
    default: return sin_poly(x);
    }
}

bool validate_camera(const OrbitCamera& camera, std::string& error) {
    const auto in_range = [](double value, double lo, double hi) {
        return std::isfinite(value) && value >= lo && value <= hi;
    };
    if (!in_range(camera.yaw_deg, -kMaxYawMagnitude, kMaxYawMagnitude)) {
        error = "yaw must be finite and within +-36000 degrees";
        return false;
    }
    if (!in_range(camera.pitch_deg, kMinPitch, kMaxPitch)) {
        error = "pitch must be within -89..89 degrees";
        return false;
    }
    if (!in_range(camera.distance, kMinDistance, kMaxDistance)) {
        error = "distance must be within 0.01..10000";
        return false;
    }
    if (!in_range(camera.target.x, -kMaxTargetMagnitude, kMaxTargetMagnitude) ||
        !in_range(camera.target.y, -kMaxTargetMagnitude, kMaxTargetMagnitude) ||
        !in_range(camera.target.z, -kMaxTargetMagnitude, kMaxTargetMagnitude)) {
        error = "target components must be within +-10000";
        return false;
    }
    if (!in_range(camera.fov_y_deg, kMinFov, kMaxFov)) {
        error = "vertical field of view must be within 1..170 degrees";
        return false;
    }
    if (!in_range(camera.near_plane, kMinNear, kMaxNear)) {
        error = "near plane must be within 0.0001..10";
        return false;
    }
    if (!in_range(camera.far_plane, kMinNear, kMaxFar) ||
        !(camera.far_plane > camera.near_plane)) {
        error = "far plane must be greater than near and at most 100000";
        return false;
    }
    return true;
}

Vec3 orbit_eye(const OrbitCamera& camera) noexcept {
    const double cp = det_cos_deg(camera.pitch_deg);
    const double sp = det_sin_deg(camera.pitch_deg);
    const double cy = det_cos_deg(camera.yaw_deg);
    const double sy = det_sin_deg(camera.yaw_deg);
    return {camera.target.x + camera.distance * cp * cy,
            camera.target.y + camera.distance * sp,
            camera.target.z + camera.distance * cp * sy};
}

bool make_camera_frame(
    const OrbitCamera& camera,
    std::uint32_t width,
    std::uint32_t height,
    CameraFrame& frame,
    std::string& error) {
    if (!validate_camera(camera, error)) {
        return false;
    }
    if (width == 0 || height == 0) {
        error = "frame dimensions must be positive";
        return false;
    }
    const double cp = det_cos_deg(camera.pitch_deg);
    const double sp = det_sin_deg(camera.pitch_deg);
    const double cy = det_cos_deg(camera.yaw_deg);
    const double sy = det_sin_deg(camera.yaw_deg);
    frame.eye = orbit_eye(camera);
    frame.forward = {-cp * cy, -sp, -cp * sy};
    const Vec3 world_up{0.0, 1.0, 0.0};
    frame.right = cross(frame.forward, world_up);
    if (!normalize(frame.forward) || !normalize(frame.right)) {
        error = "camera basis is degenerate";
        return false;
    }
    frame.up = cross(frame.right, frame.forward);
    if (!normalize(frame.up)) {
        error = "camera basis is degenerate";
        return false;
    }
    const double half = camera.fov_y_deg * 0.5;
    frame.focal_y = det_cos_deg(half) / det_sin_deg(half);
    frame.focal_x = frame.focal_y * static_cast<double>(height) /
                    static_cast<double>(width);
    frame.near_plane = camera.near_plane;
    frame.far_plane = camera.far_plane;
    return true;
}

ClipVertex project_point(const CameraFrame& frame, const Vec3& world) noexcept {
    const Vec3 d = sub(world, frame.eye);
    const double vx = dot(frame.right, d);
    const double vy = dot(frame.up, d);
    const double vz = dot(frame.forward, d);
    ClipVertex out;
    out.x = vx * frame.focal_x;
    out.y = vy * frame.focal_y;
    out.z = (vz - frame.near_plane) * frame.far_plane /
            (frame.far_plane - frame.near_plane);
    out.w = vz;
    return out;
}

std::size_t clip_triangle(
    const ClipVertex (&input)[3],
    double near_plane,
    ClipVertex (&out)[kMaxClipVertices]) {
    for (const ClipVertex& v : input) {
        if (!finite_vertex(v)) {
            return 0;
        }
    }
    // Each plane adds at most one vertex: 3 + 5 = 8 <= kMaxClipVertices.
    ClipVertex buffer[kMaxClipVertices];
    ClipVertex* source = out;
    ClipVertex* target = buffer;
    std::copy(std::begin(input), std::end(input), source);
    std::size_t count = 3;
    for (int plane = 0; plane < 5 && count > 0; ++plane) {
        count = clip_plane(source, count, plane, near_plane, target);
        std::swap(source, target);
    }
    if (count < 3) {
        return 0;
    }
    if (source != out) {
        std::copy(source, source + count, out);
    }
    return count;
}

namespace {

// The distinct hashes a record claims, in the order info, hash table,
// blob table.
std::size_t claimed_hashes(const TextureRecord& record,
                           std::array<std::uint32_t, 3>& out) {
    std::size_t count = 0;
    const std::uint32_t candidates[3] = {
        record.hash, record.table_hash,
        static_cast<std::uint32_t>(record.blob_entry[0]) |
            (static_cast<std::uint32_t>(record.blob_entry[1]) << 8U) |
            (static_cast<std::uint32_t>(record.blob_entry[2]) << 16U) |
            (static_cast<std::uint32_t>(record.blob_entry[3]) << 24U)};
    const std::size_t used = record.storage == TextureStorage::jdlz ? 3 : 2;
    for (std::size_t i = 0; i < used; ++i) {
        if (std::find(out.begin(), out.begin() + count, candidates[i]) ==
            out.begin() + count) {
            out[count++] = candidates[i];
        }
    }
    return count;
}

// check_pairing is false only for decode_render_textures, whose callers
// may pass hand-built packs without a file size or parse source.
bool decode_sources(
    const std::vector<RenderTextureSource>& sources,
    bool check_pairing,
    const RenderTextureLimits& limits,
    RenderTextureSet& set,
    std::string& error) {
    set = RenderTextureSet{};
    RenderTextureSet result;
    const bool label = sources.size() > 1 ||
                       (sources.size() == 1 && sources[0].packs.size() > 1);
    // Hash -> earliest source that claims it.
    std::map<std::uint32_t, std::size_t> claims;
    try {
        bool blocked = false;
        for (std::size_t s = 0; s < sources.size(); ++s) {
            const RenderTextureSource& source = sources[s];
            if (blocked) {
                result.blocked_sources.push_back(s);
                continue;
            }
            if (source.bytes == nullptr) {
                error = "texture source " + std::to_string(s) + " has no bytes";
                return false;
            }
            for (std::size_t p = 0; p < source.packs.size(); ++p) {
                const TexturePack* pack = source.packs[p];
                if (pack == nullptr ||
                    (check_pairing && (pack->parse_source != source.bytes->data() ||
                                       pack->file_size != source.bytes->size()))) {
                    error = "texture source " + std::to_string(s) + " pack " +
                            std::to_string(p) +
                            " was not parsed from the bytes of that source";
                    return false;
                }
            }
            bool complete = source.complete;
            std::vector<std::uint32_t> source_claims;
            for (std::size_t p = 0; p < source.packs.size(); ++p) {
                const TexturePack& pack = *source.packs[p];
                complete = complete && (!check_pairing || pack.tables_parsed);
                for (const TextureRecord& record : pack.textures) {
                    std::array<std::uint32_t, 3> hashes{};
                    const std::size_t count = claimed_hashes(record, hashes);
                    source_claims.insert(source_claims.end(), hashes.begin(),
                                         hashes.begin() + count);
                    bool shadowed = false;
                    for (std::size_t k = 0; k < count && !shadowed; ++k) {
                        const auto found = claims.find(hashes[k]);
                        if (found != claims.end()) {
                            result.shadowed.push_back(ShadowedTexture{
                                hashes[k], s, p, record.index, found->second});
                            shadowed = true;
                        }
                    }
                    bool wanted = true;
                    if (source.wanted != nullptr) {
                        wanted = false;
                        for (std::size_t k = 0; k < count && !wanted; ++k) {
                            wanted = std::binary_search(source.wanted->begin(),
                                                        source.wanted->end(), hashes[k]);
                        }
                    }
                    if (shadowed) {
                        // Claims no earlier source holds still block later
                        // sources, so keep them resolving as unusable.
                        for (std::size_t k = 0; k < count && wanted; ++k) {
                            if (claims.find(hashes[k]) == claims.end()) {
                                result.unusable_claims.push_back(
                                    UnusableClaim{hashes[k], s, p, record.index});
                            }
                        }
                        continue;
                    }
                    if (!wanted) {
                        ++result.not_wanted;
                        continue;
                    }
                    result.textures.emplace_back();
                    RenderTexture& texture = result.textures.back();
                    texture.hash = record.hash;
                    texture.claims = hashes;
                    texture.claim_count = count;
                    texture.source = s;
                    texture.pack = p;
                    texture.record = record.index;
                    if (!record.valid) {
                        texture.problem = record.problem.empty()
                                              ? "invalid texture record"
                                              : record.problem;
                        continue;
                    }
                    // The retained top level is width * height * 4 bytes;
                    // both are at most 65535, so the product cannot
                    // overflow 64 bits.
                    const std::uint64_t retained =
                        std::uint64_t{record.width} * record.height * 4U;
                    if (retained > limits.max_total_bytes ||
                        result.decoded_bytes > limits.max_total_bytes - retained) {
                        error = "decoded textures would exceed " +
                                std::to_string(limits.max_total_bytes) +
                                " bytes at texture " + std::to_string(record.index) +
                                (label ? " of source " + std::to_string(s) +
                                             " pack " + std::to_string(p)
                                       : std::string()) +
                                " (" + std::to_string(result.decoded_bytes) +
                                " retained, " + std::to_string(retained) + " more)";
                        return false;
                    }
                    if (limits.before_allocate) {
                        limits.before_allocate(retained);
                    }
                    const std::vector<unsigned char>& bytes = *source.bytes;
                    std::vector<unsigned char> payload;
                    std::size_t unused = 0;
                    std::uint64_t top = 0;
                    std::string problem;
                    if (!texture_payload(bytes.data(), bytes.size(), record, payload,
                                         unused, problem) ||
                        !level_size(record.format, record.width, record.height, top) ||
                        top > payload.size() ||
                        !decode_level(record.format, record.width, record.height,
                                      payload.data(), static_cast<std::size_t>(top),
                                      texture.rgba, problem) ||
                        texture.rgba.size() != retained) {
                        texture.rgba.clear();
                        texture.rgba.shrink_to_fit();
                        texture.problem =
                            problem.empty() ? "texture decode failed" : problem;
                        continue;
                    }
                    texture.width = record.width;
                    texture.height = record.height;
                    texture.valid = true;
                    result.decoded_bytes += retained;
                }
            }
            // Claims of this source apply to later sources only.
            for (const std::uint32_t hash : source_claims) {
                claims.emplace(hash, s);
            }
            blocked = !complete;
        }
    } catch (const std::bad_alloc&) {
        error = "out of memory while decoding textures (" +
                std::to_string(result.decoded_bytes) + " bytes retained)";
        return false;
    }
    set = std::move(result);
    return true;
}

} // namespace

bool decode_render_textures(
    const std::vector<unsigned char>& bytes,
    const TexturePack& pack,
    const RenderTextureLimits& limits,
    RenderTextureSet& set,
    std::string& error) {
    set = RenderTextureSet{};
    std::vector<RenderTextureSource> sources;
    try {
        sources.resize(1);
        sources[0].bytes = &bytes;
        sources[0].packs.push_back(&pack);
    } catch (const std::bad_alloc&) {
        error = "out of memory while decoding textures (0 bytes retained)";
        return false;
    }
    return decode_sources(sources, false, limits, set, error);
}

bool decode_render_texture_sources(
    const std::vector<RenderTextureSource>& sources,
    const RenderTextureLimits& limits,
    RenderTextureSet& set,
    std::string& error) {
    return decode_sources(sources, true, limits, set, error);
}

bool RenderTexture::claims_hash(std::uint32_t value) const noexcept {
    if (hash == value) {
        return true;
    }
    const std::size_t used = std::min(claim_count, claims.size());
    return std::find(claims.begin(), claims.begin() + used, value) !=
           claims.begin() + used;
}

const char* binding_kind_name(BindingKind kind) noexcept {
    switch (kind) {
    case BindingKind::resolved: return "resolved";
    case BindingKind::unresolved: return "unresolved";
    case BindingKind::unusable: return "unusable";
    case BindingKind::invalid_slot: return "invalid_slot";
    }
    return "unknown";
}

MaterialBinding resolve_material(
    const Mesh& mesh,
    const MeshMaterial& material,
    const RenderTextureSet& textures) noexcept {
    MaterialBinding binding;
    if (material.texture_slot >= mesh.texture_hashes.size()) {
        binding.kind = BindingKind::invalid_slot;
        return binding;
    }
    binding.hash = mesh.texture_hashes[material.texture_slot];
    std::size_t matches = 0;
    std::size_t usable = 0;
    for (std::size_t i = 0; i < textures.textures.size(); ++i) {
        const RenderTexture& texture = textures.textures[i];
        if (!texture.claims_hash(binding.hash)) {
            continue;
        }
        ++matches;
        const bool ok = texture.valid && texture.width > 0 &&
                        texture.height > 0 &&
                        texture.rgba.size() ==
                            std::size_t{texture.width} * texture.height * 4U;
        if (ok) {
            ++usable;
            binding.texture_index = i;
        }
    }
    for (const UnusableClaim& claim : textures.unusable_claims) {
        if (claim.hash == binding.hash) {
            ++matches;
            break;
        }
    }
    if (matches == 0) {
        binding.kind = BindingKind::unresolved;
    } else if (matches == 1 && usable == 1) {
        binding.kind = BindingKind::resolved;
    } else {
        binding.kind = BindingKind::unusable;
        binding.texture_index = 0;
    }
    return binding;
}

void fallback_color(std::uint32_t hash, unsigned char (&rgba)[4]) noexcept {
    const std::uint32_t mixed = mix32(hash);
    rgba[0] = static_cast<unsigned char>(64U + (mixed & 0xFFU) * 160U / 256U);
    rgba[1] = static_cast<unsigned char>(64U + ((mixed >> 8U) & 0xFFU) * 160U / 256U);
    rgba[2] = static_cast<unsigned char>(64U + ((mixed >> 16U) & 0xFFU) * 160U / 256U);
    rgba[3] = 255;
}

const char* cull_mode_name(CullMode mode) noexcept {
    switch (mode) {
    case CullMode::none: return "none";
    case CullMode::back: return "back";
    case CullMode::front: return "front";
    }
    return "unknown";
}

const char* shading_mode_name(ShadingMode mode) noexcept {
    switch (mode) {
    case ShadingMode::none: return "none";
    case ShadingMode::flat: return "flat";
    }
    return "unknown";
}

bool Frame::reset(
    std::uint32_t width,
    std::uint32_t height,
    const unsigned char (&background)[4],
    const RenderLimits& limits,
    std::string& error) {
    if (width == 0 || height == 0 || width > limits.max_width ||
        height > limits.max_height) {
        error = "frame size " + std::to_string(width) + "x" +
                std::to_string(height) + " is outside 1.." +
                std::to_string(limits.max_width) + " x 1.." +
                std::to_string(limits.max_height);
        return false;
    }
    const std::uint64_t pixels = std::uint64_t{width} * height;
    if (pixels > limits.max_pixels) {
        error = "frame has " + std::to_string(pixels) +
                " pixels; the limit is " + std::to_string(limits.max_pixels);
        return false;
    }
    try {
        std::vector<unsigned char> rgba(static_cast<std::size_t>(pixels) * 4U);
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i + 0] = background[0];
            rgba[i + 1] = background[1];
            rgba[i + 2] = background[2];
            rgba[i + 3] = background[3];
        }
        std::vector<double> depth(static_cast<std::size_t>(pixels), 1.0);
        std::vector<std::uint32_t> ids(static_cast<std::size_t>(pixels),
                                       kNoTriangle);
        rgba_ = std::move(rgba);
        depth_ = std::move(depth);
        ids_ = std::move(ids);
    } catch (const std::bad_alloc&) {
        error = "not enough memory for the frame";
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

struct Rasterizer::ScreenVertex {
    std::int64_t x = 0;  // fixed point, kSubpixelBits fraction bits
    std::int64_t y = 0;
    double z = 0.0;      // z / w, affine in screen space
    double q = 0.0;      // 1 / w
    double uq = 0.0;
    double vq = 0.0;
    double rq = 0.0;
    double gq = 0.0;
    double bq = 0.0;
};

Rasterizer::Rasterizer(Frame& frame, const RenderOptions& options,
                       const RenderLimits& limits, RenderStats& stats)
    : frame_(frame), options_(options), limits_(limits), stats_(stats) {}

bool Rasterizer::draw_triangle(
    const ClipVertex (&vertices)[3],
    const RasterMaterial& material,
    std::uint32_t id,
    std::string& error) {
    ClipVertex polygon[kMaxClipVertices];
    const std::size_t count =
        clip_triangle(vertices, options_.camera.near_plane, polygon);
    if (count == 0) {
        ++stats_.triangles_outside;
        return true;
    }
    bool clipped = count != 3;
    for (std::size_t i = 0; i < 3 && !clipped; ++i) {
        const ClipVertex& a = polygon[i];
        const ClipVertex& b = vertices[i];
        clipped = a.x != b.x || a.y != b.y || a.w != b.w;
    }
    if (clipped) {
        ++stats_.triangles_clipped;
    }

    const double width = static_cast<double>(frame_.width_);
    const double height = static_cast<double>(frame_.height_);
    ScreenVertex screen[kMaxClipVertices];
    for (std::size_t i = 0; i < count; ++i) {
        const ClipVertex& v = polygon[i];
        if (!(v.w > 0.0)) {
            ++stats_.triangles_degenerate_screen;
            return true;
        }
        const double q = 1.0 / v.w;
        // Guard-band clipping keeps |x / w| <= g up to rounding; the clamp
        // bounds the fixed-point range exactly.
        const double nx = std::clamp(v.x * q, -kGuardBand, kGuardBand);
        const double ny = std::clamp(v.y * q, -kGuardBand, kGuardBand);
        const double sx = (nx + 1.0) * 0.5 * width;
        const double sy = (1.0 - ny) * 0.5 * height;
        // |sx|, |sy| <= (g + 1) / 2 * 8192 < 2^16, so fixed values stay
        // below 2^24 and edge products below 2^50.
        screen[i].x = static_cast<std::int64_t>(
            std::floor(sx * static_cast<double>(kSubpixelScale) + 0.5));
        screen[i].y = static_cast<std::int64_t>(
            std::floor(sy * static_cast<double>(kSubpixelScale) + 0.5));
        screen[i].z = v.z * q;
        screen[i].q = q;
        screen[i].uq = v.u * q;
        screen[i].vq = v.v * q;
        screen[i].rq = v.r * q;
        screen[i].gq = v.g * q;
        screen[i].bq = v.b * q;
    }

    bool any_drawn = false;
    bool facing_counted = false;
    for (std::size_t i = 1; i + 1 < count; ++i) {
        const ScreenVertex* a = &screen[0];
        const ScreenVertex* b = &screen[i];
        const ScreenVertex* c = &screen[i + 1];
        const std::int64_t area =
            edge_value(a->x, a->y, b->x, b->y, c->x, c->y);
        if (area == 0) {
            continue;
        }
        // In y-down screen space a counter-clockwise triangle, the source
        // front face, has negative area.
        const bool front = area < 0;
        if (!facing_counted) {
            ++(front ? stats_.triangles_front : stats_.triangles_back);
            facing_counted = true;
        }
        if ((options_.cull == CullMode::back && !front) ||
            (options_.cull == CullMode::front && front)) {
            continue;
        }
        ++stats_.raster_triangles;
        if (stats_.raster_triangles > limits_.max_raster_triangles) {
            error = "raster triangle limit exceeded";
            return false;
        }
        if (area < 0) {
            std::swap(b, c);
        }
        if (!fill(a, b, c, area < 0 ? -area : area, material, id, error)) {
            return false;
        }
        any_drawn = true;
    }
    if (!facing_counted) {
        ++stats_.triangles_degenerate_screen;
    } else if (any_drawn) {
        ++stats_.triangles_rasterized;
    } else {
        ++stats_.triangles_culled;
    }
    return true;
}

bool Rasterizer::fill(
    const ScreenVertex* a,
    const ScreenVertex* b,
    const ScreenVertex* c,
    std::int64_t area,
    const RasterMaterial& material,
    std::uint32_t id,
    std::string& error) {
    const std::int64_t width = frame_.width_;
    const std::int64_t height = frame_.height_;
    const std::int64_t min_x = std::min({a->x, b->x, c->x});
    const std::int64_t max_x = std::max({a->x, b->x, c->x});
    const std::int64_t min_y = std::min({a->y, b->y, c->y});
    const std::int64_t max_y = std::max({a->y, b->y, c->y});
    // Pixel i samples at i * scale + scale / 2.
    const std::int64_t x0 = std::max<std::int64_t>(
        0, -floor_div(-(min_x - kSubpixelHalf), kSubpixelScale));
    const std::int64_t x1 = std::min<std::int64_t>(
        width - 1, floor_div(max_x - kSubpixelHalf, kSubpixelScale));
    const std::int64_t y0 = std::max<std::int64_t>(
        0, -floor_div(-(min_y - kSubpixelHalf), kSubpixelScale));
    const std::int64_t y1 = std::min<std::int64_t>(
        height - 1, floor_div(max_y - kSubpixelHalf, kSubpixelScale));
    if (x0 > x1 || y0 > y1) {
        return true;
    }
    const std::uint64_t tests =
        static_cast<std::uint64_t>(x1 - x0 + 1) *
        static_cast<std::uint64_t>(y1 - y0 + 1);
    if (tests > limits_.max_pixel_tests - stats_.pixel_tests) {
        error = "pixel test limit exceeded";
        return false;
    }
    stats_.pixel_tests += tests;

    // Edge k is opposite vertex k.
    const ScreenVertex* v[3] = {a, b, c};
    std::int64_t step_x[3];
    std::int64_t step_y[3];
    std::int64_t row[3];
    std::int64_t bias[3];
    const std::int64_t px0 = x0 * kSubpixelScale + kSubpixelHalf;
    const std::int64_t py0 = y0 * kSubpixelScale + kSubpixelHalf;
    for (int k = 0; k < 3; ++k) {
        const ScreenVertex* s = v[(k + 1) % 3];
        const ScreenVertex* e = v[(k + 2) % 3];
        const std::int64_t dx = e->x - s->x;
        const std::int64_t dy = e->y - s->y;
        step_x[k] = -dy * kSubpixelScale;
        step_y[k] = dx * kSubpixelScale;
        // Samples exactly on an edge belong to the triangle only for
        // top-left edges; the bias turns that into a sign test.
        bias[k] = top_left(dx, dy) ? 0 : 1;
        row[k] = edge_value(s->x, s->y, e->x, e->y, px0, py0) - bias[k];
    }

    const double inv_area = 1.0 / static_cast<double>(area);
    const RenderTexture* texture = material.texture;
    const bool fallback = texture == nullptr;
    for (std::int64_t y = y0; y <= y1; ++y) {
        std::int64_t e[3] = {row[0], row[1], row[2]};
        for (std::int64_t x = x0; x <= x1; ++x) {
            if ((e[0] | e[1] | e[2]) >= 0) {
                ++stats_.fragments;
                // Undo the fill-rule bias to get exact barycentrics.
                const double l[3] = {
                    static_cast<double>(e[0] + bias[0]) * inv_area,
                    static_cast<double>(e[1] + bias[1]) * inv_area,
                    static_cast<double>(e[2] + bias[2]) * inv_area};
                const double z = l[0] * a->z + l[1] * b->z + l[2] * c->z;
                const std::size_t index =
                    static_cast<std::size_t>(y) * frame_.width_ +
                    static_cast<std::size_t>(x);
                if (!(z <= 1.0)) {
                    ++stats_.fragments_far_discarded;
                } else {
                    const double q = l[0] * a->q + l[1] * b->q + l[2] * c->q;
                    const double iq = q > 0.0 ? 1.0 / q : 0.0;
                    const double u =
                        (l[0] * a->uq + l[1] * b->uq + l[2] * c->uq) * iq;
                    double tv =
                        (l[0] * a->vq + l[1] * b->vq + l[2] * c->vq) * iq;
                    if (options_.flip_v) {
                        tv = 1.0 - tv;
                    }
                    std::uint32_t color[4];
                    bool keep = true;
                    if (fallback) {
                        const bool dark = checker_odd(u) != checker_odd(tv);
                        for (int k = 0; k < 3; ++k) {
                            const std::uint32_t base = material.fallback[k];
                            color[k] = dark && material.fallback_pattern
                                           ? base * 3U / 4U
                                           : base;
                        }
                        color[3] = 255;
                    } else {
                        const std::uint32_t tx = wrap_texel(u, texture->width);
                        const std::uint32_t ty = wrap_texel(tv, texture->height);
                        const unsigned char* texel =
                            &texture->rgba[(std::size_t{ty} * texture->width + tx) * 4U];
                        for (int k = 0; k < 4; ++k) {
                            color[k] = texel[k];
                        }
                        keep = color[3] >= options_.alpha_cutoff;
                    }
                    if (!keep) {
                        ++stats_.fragments_alpha_discarded;
                    } else if (!(z < frame_.depth_[index] ||
                                 (z == frame_.depth_[index] &&
                                  (frame_.ids_[index] == kNoTriangle ||
                                   id > frame_.ids_[index])))) {
                        ++stats_.fragments_depth_failed;
                    } else {
                        if (options_.vertex_color) {
                            const double vc[3] = {
                                (l[0] * a->rq + l[1] * b->rq + l[2] * c->rq) * iq,
                                (l[0] * a->gq + l[1] * b->gq + l[2] * c->gq) * iq,
                                (l[0] * a->bq + l[1] * b->bq + l[2] * c->bq) * iq};
                            for (int k = 0; k < 3; ++k) {
                                color[k] = (color[k] * channel(vc[k]) + 127U) / 255U;
                            }
                        }
                        const std::uint32_t shade = std::min<std::uint32_t>(material.shade, 256U);
                        unsigned char* out = &frame_.rgba_[index * 4U];
                        for (int k = 0; k < 3; ++k) {
                            out[k] = static_cast<unsigned char>(
                                std::min<std::uint32_t>(255U, (color[k] * shade + 128U) >> 8U));
                        }
                        out[3] = 255;
                        frame_.depth_[index] = z;
                        frame_.ids_[index] = id;
                        ++stats_.fragments_written;
                    }
                }
            }
            e[0] += step_x[0];
            e[1] += step_x[1];
            e[2] += step_x[2];
        }
        row[0] += step_y[0];
        row[1] += step_y[1];
        row[2] += step_y[2];
    }
    return true;
}

std::uint64_t count_unresolved_hashes(
    const GeometryReport& geometry,
    const RenderTextureSet& textures) {
    std::set<std::uint32_t> hashes;
    for (const Mesh& mesh : geometry.meshes) {
        if (!mesh.valid) {
            continue;
        }
        for (const MeshMaterial& material : mesh.materials) {
            const MaterialBinding binding =
                resolve_material(mesh, material, textures);
            if (binding.kind == BindingKind::unresolved) {
                hashes.insert(binding.hash);
            }
        }
    }
    return hashes.size();
}

bool render_scene(
    const GeometryReport& geometry,
    const RenderTextureSet& textures,
    const RenderOptions& options,
    const RenderLimits& limits,
    Frame& frame,
    RenderStats& stats,
    std::string& error) {
    stats = RenderStats{};
    CameraFrame camera;
    if (!make_camera_frame(options.camera, std::max<std::uint32_t>(options.width, 1),
                           std::max<std::uint32_t>(options.height, 1), camera,
                           error)) {
        return false;
    }
    std::uint64_t source_triangles = 0;
    for (const Mesh& mesh : geometry.meshes) {
        if (mesh.valid) {
            source_triangles += mesh.triangles.size();
        }
    }
    if (source_triangles > limits.max_source_triangles ||
        source_triangles >= kNoTriangle) {
        error = "scene has " + std::to_string(source_triangles) +
                " triangles; the limit is " +
                std::to_string(limits.max_source_triangles);
        return false;
    }
    if (!frame.reset(options.width, options.height, options.background, limits,
                     error)) {
        return false;
    }

    Vec3 light{kLightX, kLightY, kLightZ};
    normalize(light);
    Rasterizer rasterizer(frame, options, limits, stats);
    std::vector<unsigned char> id_fallback;
    id_fallback.reserve(static_cast<std::size_t>(source_triangles));
    std::uint32_t next_id = 0;
    for (const Mesh& mesh : geometry.meshes) {
        ++stats.meshes;
        if (!mesh.valid) {
            ++stats.meshes_skipped_invalid;
            continue;
        }
        ++stats.meshes_rendered;
        const std::uint64_t index_total = std::uint64_t{mesh.triangles.size()} * 3U;
        for (const MeshMaterial& material : mesh.materials) {
            ++stats.material_groups;
            const MaterialBinding binding = resolve_material(mesh, material, textures);
            const std::uint64_t first = material.first_index;
            const std::uint64_t count = material.index_count;
            if (binding.kind == BindingKind::invalid_slot || first % 3 != 0 ||
                count % 3 != 0 || first > index_total || count > index_total - first) {
                ++stats.groups_invalid;
                continue;
            }
            RasterMaterial raster;
            switch (binding.kind) {
            case BindingKind::resolved:
                ++stats.groups_resolved;
                raster.texture = &textures.textures[binding.texture_index];
                break;
            case BindingKind::unresolved:
                ++stats.groups_unresolved;
                break;
            default:
                ++stats.groups_unusable;
                break;
            }
            fallback_color(binding.hash, raster.fallback);

            for (std::uint64_t t = first / 3; t < (first + count) / 3; ++t) {
                const std::uint32_t id = next_id++;
                const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(t)];
                ++stats.triangles_submitted;
                id_fallback.push_back(raster.texture == nullptr ? 1 : 0);
                ++(raster.texture == nullptr ? stats.triangles_fallback
                                             : stats.triangles_resolved);
                const std::uint16_t indices[3] = {triangle.a, triangle.b, triangle.c};
                if (indices[0] >= mesh.vertices.size() ||
                    indices[1] >= mesh.vertices.size() ||
                    indices[2] >= mesh.vertices.size()) {
                    ++stats.triangles_bad_index;
                    continue;
                }
                ClipVertex clip[3];
                Vec3 world[3];
                for (int k = 0; k < 3; ++k) {
                    const MeshVertex& source = mesh.vertices[indices[k]];
                    world[k] = {source.px, source.py, -static_cast<double>(source.pz)};
                    clip[k] = project_point(camera, world[k]);
                    clip[k].u = source.u;
                    clip[k].v = source.v;
                    clip[k].r = source.color[0];
                    clip[k].g = source.color[1];
                    clip[k].b = source.color[2];
                    clip[k].a = source.color[3];
                }
                Vec3 normal = cross(sub(world[1], world[0]), sub(world[2], world[0]));
                const bool has_normal = normalize(normal);
                if (!has_normal) {
                    ++stats.triangles_degenerate_source;
                }
                raster.shade = 256;
                if (options.shading == ShadingMode::flat && has_normal) {
                    if (dot(normal, sub(camera.eye, world[0])) < 0.0) {
                        normal = {-normal.x, -normal.y, -normal.z};
                    }
                    const double k = 0.35 + 0.65 * std::max(0.0, dot(normal, light));
                    raster.shade = static_cast<std::uint32_t>(
                        std::floor(std::clamp(k, 0.0, 1.0) * 256.0 + 0.5));
                }
                if (!rasterizer.draw_triangle(clip, raster, id, error)) {
                    return false;
                }
            }
        }
    }
    stats.unresolved_hashes = count_unresolved_hashes(geometry, textures);

    bool first_pixel = true;
    for (std::uint32_t y = 0; y < frame.height(); ++y) {
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::uint32_t id = frame.ids()[std::size_t{y} * frame.width() + x];
            if (id == kNoTriangle) {
                continue;
            }
            ++stats.pixels_drawn;
            if (id < id_fallback.size() && id_fallback[id] != 0) {
                ++stats.pixels_fallback;
            }
            if (first_pixel) {
                stats.min_x = stats.max_x = x;
                stats.min_y = stats.max_y = y;
                first_pixel = false;
            } else {
                stats.min_x = std::min(stats.min_x, x);
                stats.max_x = std::max(stats.max_x, x);
                stats.min_y = std::min(stats.min_y, y);
                stats.max_y = std::max(stats.max_y, y);
            }
        }
    }
    return true;
}

namespace {

// Same wording as rollback_created_files, for a removal done by handle.
[[maybe_unused]] std::string removed_summary(const fs::path& path, bool removed) {
    if (removed) {
        return "removed all 1 files created by this export";
    }
    return "removed 0 of 1 files created by this export; could not remove: " +
           path.u8string();
}

// A "<target>.partial" file created exclusively by this process.
class PartialFile {
public:
    enum class Open { ok, exists, failed };
    enum class Publish { ok, target_exists, failed };

    PartialFile() = default;
    PartialFile(const PartialFile&) = delete;
    PartialFile& operator=(const PartialFile&) = delete;
    ~PartialFile() { close(); }

#if defined(NFSU_WRITER_WINDOWS)
    Open create(const fs::path& path) {
        path_ = path;
        // CREATE_NEW fails if any entry exists at the name, and
        // FILE_FLAG_OPEN_REPARSE_POINT never follows a symlink or junction.
        // No write or delete sharing, so nobody else can change or rename
        // the file while the handle is open. DELETE access allows the
        // handle-based rename and removal below.
        handle_ = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE,
                              FILE_SHARE_READ, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                              nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            return code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS
                       ? Open::exists
                       : Open::failed;
        }
        return Open::ok;
    }

    bool write(const std::string& data) {
        std::size_t done = 0;
        while (done < data.size()) {
            const DWORD chunk = static_cast<DWORD>(
                std::min<std::size_t>(data.size() - done, 1U << 30));
            DWORD written = 0;
            if (!WriteFile(handle_, data.data() + done, chunk, &written, nullptr) ||
                written == 0) {
                return false;
            }
            done += written;
        }
        return FlushFileBuffers(handle_) != 0;
    }

    // Renames the open file to target without replacing an existing entry;
    // the handle stays attached to the same file throughout.
    Publish publish(const fs::path& target, std::string& error) {
        std::error_code ec;
        const std::wstring name = fs::absolute(target, ec).wstring();
        if (ec || name.empty()) {
            error = "cannot resolve the target path";
            return Publish::failed;
        }
        const std::size_t name_bytes = name.size() * sizeof(wchar_t);
        std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + name_bytes);
        auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        info->ReplaceIfExists = FALSE;
        info->RootDirectory = nullptr;
        info->FileNameLength = static_cast<DWORD>(name_bytes);
        std::memcpy(info->FileName, name.data(), name_bytes);
        if (!SetFileInformationByHandle(handle_, FileRenameInfo, info,
                                        static_cast<DWORD>(buffer.size()))) {
            const DWORD code = GetLastError();
            if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS) {
                return Publish::target_exists;
            }
            error = "Windows error " + std::to_string(code);
            return Publish::failed;
        }
        close();
        return Publish::ok;
    }

    // Deletes the file through the open handle, so only this file can be
    // removed even if the name is reused concurrently.
    std::vector<fs::path> discard(std::string& summary) {
        bool removed = false;
        if (handle_ != INVALID_HANDLE_VALUE) {
            FILE_DISPOSITION_INFO disposition{};
            disposition.DeleteFile = TRUE;
            removed = SetFileInformationByHandle(handle_, FileDispositionInfo,
                                                 &disposition,
                                                 sizeof(disposition)) != 0;
            close();
        }
        summary = removed_summary(path_, removed);
        if (removed) {
            return {};
        }
        return {path_};
    }

private:
    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    fs::path path_;
#elif defined(NFSU_WRITER_POSIX)
    Open create(const fs::path& path) {
        path_ = path;
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
        // O_CREAT | O_EXCL fails if any entry, including a dangling
        // symlink, exists at the name.
        fd_ = ::open(path.c_str(), flags, 0644);
        if (fd_ < 0) {
            return errno == EEXIST ? Open::exists : Open::failed;
        }
        return Open::ok;
    }

    bool write(const std::string& data) {
        std::size_t done = 0;
        while (done < data.size()) {
            const ssize_t n = ::write(fd_, data.data() + done, data.size() - done);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                return false;
            }
            done += static_cast<std::size_t>(n);
        }
        if (::fsync(fd_) != 0) {
            return false;
        }
        const int fd = fd_;
        fd_ = -1;
        return ::close(fd) == 0;
    }

    // link() fails with EEXIST instead of replacing the target. The partial
    // name is removed afterwards.
    Publish publish(const fs::path& target, std::string& error) {
        if (::link(path_.c_str(), target.c_str()) != 0) {
            const int code = errno;
            if (code == EEXIST) {
                return Publish::target_exists;
            }
            error = std::strerror(code);
            return Publish::failed;
        }
        if (::unlink(path_.c_str()) != 0) {
            error = "published, but the partial name could not be removed";
            linked_target_ = target;
            return Publish::failed;
        }
        return Publish::ok;
    }

    // Removal by name: another process that can write to the directory
    // could swap the entry in between (documented residual).
    std::vector<fs::path> discard(std::string& summary) {
        close();
        std::vector<fs::path> created;
        if (!linked_target_.empty()) {
            created.push_back(linked_target_);
        }
        created.push_back(path_);
        return rollback_created_files(created, summary);
    }

private:
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_ = -1;
    fs::path path_;
    fs::path linked_target_;
#else
    Open create(const fs::path&) { return Open::failed; }
    bool write(const std::string&) { return false; }
    Publish publish(const fs::path&, std::string&) { return Publish::failed; }
    std::vector<fs::path> discard(std::string& summary) {
        summary = "removed all 0 files created by this export";
        return {};
    }

private:
    void close() {}
#endif
};

} // namespace

bool frame_writer_supported() noexcept {
#if defined(NFSU_WRITER_WINDOWS) || defined(NFSU_WRITER_POSIX)
    return true;
#else
    return false;
#endif
}

bool write_frame_tga(
    const Frame& frame,
    const std::filesystem::path& target,
    const std::vector<std::filesystem::path>& sources,
    FrameWriteResult& result) {
    result.error.clear();
    result.not_removed.clear();
    if (frame.width() == 0 || frame.height() == 0) {
        result.error = "frame is empty";
        return false;
    }
    fs::path partial = target;
    partial += ".partial";
    for (const fs::path& source : sources) {
        if (same_file(source, target) || same_file(source, partial)) {
            result.error = "output path must differ from every input file";
            return false;
        }
    }
    // Early refusal only; publication below never replaces a target that
    // appears later.
    if (path_exists(target)) {
        result.error = "refusing to overwrite " + target.filename().u8string();
        return false;
    }
    if (!frame_writer_supported()) {
        result.error = "no exclusive-create/no-replace file primitives on this "
                       "platform; refusing to write";
        return false;
    }

    std::string data;
    {
        std::ostringstream stream(std::ios::binary);
        std::string error;
        try {
            if (!write_tga(stream, frame.width(), frame.height(), frame.rgba(),
                           error)) {
                result.error = error;
                return false;
            }
            data = std::move(stream).str();
        } catch (const std::bad_alloc&) {
            result.error = "out of memory while encoding the TGA";
            return false;
        }
    }

    PartialFile file;
    const PartialFile::Open opened = file.create(partial);
    if (opened == PartialFile::Open::exists) {
        result.error = "temporary file already exists: " +
                       partial.filename().u8string();
        return false;
    }
    if (opened == PartialFile::Open::failed) {
        // Nothing was created.
        result.error = "failed to create " + partial.filename().u8string();
        return false;
    }

    // From here on the partial file was created by this call; it is removed
    // through the open handle where the platform allows it.
    const auto fail = [&](const std::string& message) {
        std::string summary;
        result.not_removed = file.discard(summary);
        result.error = message + "; " + summary;
        return false;
    };

    if (!file.write(data)) {
        return fail("failed to write " + partial.filename().u8string());
    }
    if (result.before_rename && !result.before_rename(partial)) {
        return fail("write aborted before rename");
    }
    std::string publish_error;
    switch (file.publish(target, publish_error)) {
    case PartialFile::Publish::ok:
        return true;
    case PartialFile::Publish::target_exists:
        return fail("target appeared during the write, not replaced: " +
                    target.filename().u8string());
    case PartialFile::Publish::failed:
        break;
    }
    return fail("failed to publish " + target.filename().u8string() +
                (publish_error.empty() ? "" : " (" + publish_error + ")"));
}

} // namespace nfsu
