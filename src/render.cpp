#include "nfsu/render.h"

#include "nfsu/image.h"
// rollback_created_files, which the POSIX writer's rollback calls.
#include "nfsu/texture_export.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
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

// path::u8string() returns std::u8string once the library defines
// __cpp_lib_char8_t (true from C++20 on devkitA64's GCC 16 and MinGW's
// GCC 13 alike once built as C++20), and std::string before that. Every
// caller below appends the result to a std::string with operator+,
// which std::u8string does not accept, so the conversion has to be
// explicit; std::u8string and std::string hold the same UTF-8 bytes,
// so the copy changes no output on either standard.
std::string path_to_utf8(const fs::path& path) {
#if defined(__cpp_lib_char8_t)
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    return path.u8string();
#endif
}

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
    const double length = det_sqrt(dot(v, v));
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
    for (int k=0;k<7;++k) out.surface[k]=a.surface[k]+(b.surface[k]-a.surface[k])*t;
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

void sample_linear_repeat(const RenderTexture& texture, double u, double v,
                          std::uint32_t (&color)[4]) noexcept {
    if (!std::isfinite(u)) u = 0.0;
    if (!std::isfinite(v)) v = 0.0;
    // D3D's linear sample is centred on texels: normalized coordinate 0
    // lies halfway between the last and first texels when addressing wraps.
    const double x = (u - std::floor(u)) * texture.width - 0.5;
    const double y = (v - std::floor(v)) * texture.height - 0.5;
    const double left = std::floor(x), top = std::floor(y);
    const double fx = x - left, fy = y - top;
    const auto wrap = [](double i, std::uint32_t n) {
        const auto signed_i = static_cast<std::int64_t>(i);
        const auto signed_n = static_cast<std::int64_t>(n);
        return static_cast<std::size_t>((signed_i % signed_n + signed_n) % signed_n);
    };
    const std::size_t x0 = wrap(left, texture.width), x1 = wrap(left + 1, texture.width);
    const std::size_t y0 = wrap(top, texture.height), y1 = wrap(top + 1, texture.height);
    const auto* p00 = &texture.rgba[(y0 * texture.width + x0) * 4];
    const auto* p10 = &texture.rgba[(y0 * texture.width + x1) * 4];
    const auto* p01 = &texture.rgba[(y1 * texture.width + x0) * 4];
    const auto* p11 = &texture.rgba[(y1 * texture.width + x1) * 4];
    for (int c = 0; c < 4; ++c) {
        const double upper = p00[c] * (1.0 - fx) + p10[c] * fx;
        const double lower = p01[c] * (1.0 - fx) + p11[c] * fx;
        color[c] = static_cast<std::uint32_t>(std::floor(
            upper * (1.0 - fy) + lower * fy + 0.5));
    }
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

// ---------------------------------------------------------------------------
// Comparing two paths that need not exist
//
// The writers below refuse a target that names a source, and the viewer
// refuses a dump that names the screenshot. Both are asked about paths that
// do not exist yet, where fs::equivalent answers nothing, so the names are
// compared instead - and every spelling of one name has to reach one form,
// or the guard passes a pair it was written to refuse.
// ---------------------------------------------------------------------------

using PathChar = fs::path::value_type;
using PathText = fs::path::string_type;

PathChar lower_ascii(PathChar value) noexcept {
    if (value >= PathChar('A') && value <= PathChar('Z')) {
        return static_cast<PathChar>(value - PathChar('A') + PathChar('a'));
    }
    return value;
}

PathText flip_ascii_case(const PathText& text) {
    PathText out = text;
    for (PathChar& value : out) {
        if (value >= PathChar('a') && value <= PathChar('z')) {
            value = static_cast<PathChar>(value - PathChar('a') + PathChar('A'));
        } else if (value >= PathChar('A') && value <= PathChar('Z')) {
            value = static_cast<PathChar>(value - PathChar('A') + PathChar('a'));
        }
    }
    return out;
}

// Whether the two spellings differ in nothing but case.
//
// On Windows the question is asked of the OS, because the names this
// program compares are not all ASCII - this repository is checked out under
// a Cyrillic path - and NTFS folds far more than A-Z.
// CompareStringOrdinal with bIgnoreCase is the ordinal, locale-independent
// fold Win32 name lookup uses. Elsewhere, and if that call fails, only
// A-Z is folded: folding beyond it is the filesystem's own table, which
// this cannot read, so two names differing only outside ASCII are compared
// exactly and may be let through on a filesystem that would fold them.
bool same_name_nocase(const PathText& a, const PathText& b) noexcept {
#if defined(NFSU_WRITER_WINDOWS)
    const std::size_t cap = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (a.size() <= cap && b.size() <= cap) {
        const int result =
            CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                 static_cast<int>(b.size()), TRUE);
        if (result != 0) {
            return result == CSTR_EQUAL;
        }
    }
#endif
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower_ascii(a[i]) != lower_ascii(b[i])) {
            return false;
        }
    }
    return true;
}

bool is_separator(PathChar value) noexcept {
    return value == PathChar('/') || value == PathChar('\\');
}

// The Windows extended-length prefix, removed so that the path underneath
// it reaches the same form as the plain spelling of the same file. This
// standard library does not parse it, and fs::absolute turns
// "\\?\C:\dir\out.tga" into "C:\?\C:\dir\out.tga", which would hide the
// alias between the two spellings entirely.
fs::path strip_extended_prefix(const fs::path& path) {
#if defined(NFSU_WRITER_WINDOWS)
    const PathText& text = path.native();
    if (text.size() >= 4 && is_separator(text[0]) && is_separator(text[1]) &&
        text[2] == PathChar('?') && is_separator(text[3])) {
        const PathText rest = text.substr(4);
        if (rest.size() >= 4 && same_name_nocase(rest.substr(0, 3), PathText(L"UNC")) &&
            is_separator(rest[3])) {
            PathText unc(2, PathChar('\\'));
            unc += rest.substr(4);
            return fs::path(unc);
        }
        return fs::path(rest);
    }
#endif
    return path;
}

// "\\server\share\..." - absolute, but not to this standard library, which
// parses no root name for it. The device namespace "\\.\" and the extended
// prefix "\\?\" are not this.
bool is_unc(const fs::path& path) noexcept {
    const PathText& text = path.native();
    return text.size() >= 3 && is_separator(text[0]) && is_separator(text[1]) &&
           !is_separator(text[2]) && text[2] != PathChar('?') && text[2] != PathChar('.');
}

// The one form every spelling of this path reaches: absolute first, then
// normalized.
//
// fs::weakly_canonical alone is not enough, and getting that wrong is how a
// generated texture can be published at the screenshot's path. It
// canonicalizes the longest prefix that exists and normalizes the rest, so
// for "out.tga", where nothing exists, it returns the relative "out.tga",
// while for "./out.tga" the "." does exist and it returns an absolute path.
// The two spellings of one name then compare unequal. Making the path
// absolute first removes that difference: "." and ".." collapse the same
// way whether or not the components around them exist.
//
// What it does not reach: on this Windows toolchain weakly_canonical
// dereferences no junction and no directory symbolic link, so two paths
// through different links to one directory stay different here. Neither is
// an 8.3 short name the same file as its long name, nor a second drive
// letter (subst, a mapped share) the same as the first. Those are
// disclosed in the header rather than claimed away.
fs::path absolute_name(const fs::path& raw) {
    const fs::path path = strip_extended_prefix(raw);
    std::error_code error;
    if (is_unc(path)) {
        // Already absolute. fs::absolute would put the current drive in
        // front of it, which both hides the alias between two spellings of
        // one network path and invents one between a network path and an
        // unrelated local path. Only the normalization applies; the leading
        // pair of separators is put back because lexically_normal keeps one.
        const fs::path body = fs::path(path.native().substr(1)).lexically_normal();
        PathText out(1, PathChar('\\'));
        out += body.native();
        return fs::path(out);
    }
    fs::path absolute = fs::absolute(path, error);
    if (error) {
        absolute = path;
    }
    const fs::path canonical = fs::weakly_canonical(absolute, error);
    if (error) {
        return absolute.lexically_normal();
    }
    return canonical;
}

// The deepest ancestor of the path that exists and is a directory, or an
// empty path when none does. That directory is the one that would hold the
// file, so it is the one whose name matching decides whether two spellings
// differing only in case are one file.
fs::path existing_directory(const fs::path& path) {
    std::error_code error;
    fs::path probe = path.parent_path();
    while (!probe.empty()) {
        if (fs::is_directory(probe, error)) {
            return probe;
        }
        const fs::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }
    return {};
}

// Asks the directory itself: take a name it already holds, spell it with
// the ASCII case flipped, and see whether the directory still answers to
// it. That is the exact question - does *this* directory match names
// without regard to case. It is read-only in the sense that matters:
// entries are listed and resolved, and nothing is created, opened for
// writing or changed.
//
// An entry the filesystem will not resolve - a dangling symbolic link, a
// junction whose target is gone - is skipped, and that is load-bearing.
// fs::equivalent fails for such an entry for a reason that has nothing to
// do with case, and taking that failure for an answer reports a
// case-folding volume as case-sensitive: one dangling junction sorting
// first in the output directory would be enough to let a dump named like
// the screenshot through.
//
// `answered` is false when the directory cannot be listed, or holds no name
// with an ASCII letter that the filesystem will resolve.
bool entries_ignore_case(const fs::path& directory, bool& answered) {
    answered = false;
    if (directory.empty()) {
        return false;
    }
    std::error_code error;
    fs::directory_iterator entry(directory, fs::directory_options::skip_permission_denied,
                                 error);
    if (error) {
        return false;
    }
    const fs::directory_iterator end;
    // Bounded, because only the first usable entry is needed and a
    // directory can be very large.
    for (std::size_t seen = 0; entry != end && seen < 64; entry.increment(error), ++seen) {
        if (error) {
            return false;
        }
        const PathText name = entry->path().filename().native();
        const PathText flipped = flip_ascii_case(name);
        if (flipped == name) {
            continue;
        }
        std::error_code status_error;
        const fs::file_status status = fs::status(entry->path(), status_error);
        if (status_error || !fs::exists(status)) {
            continue;
        }
        std::error_code compare_error;
        answered = true;
        return fs::equivalent(entry->path(), directory / flipped, compare_error) &&
               !compare_error;
    }
    return false;
}

// The same question asked of the directory's own name instead of its
// contents, for a directory that is empty or holds only digits. It answers
// for the parent, which is the nearest filesystem there is to ask; a
// component with no ASCII letter is skipped upwards, and a Windows drive
// letter is the last resort. `answered` is false when nothing in the path
// can be spelled two ways.
bool components_ignore_case(const fs::path& directory, bool& answered) {
    answered = false;
    std::error_code error;
    fs::path probe = directory;
    while (!probe.empty()) {
        const PathText name = probe.filename().native();
        const PathText flipped = flip_ascii_case(name);
        if (flipped != name) {
            answered = true;
            return fs::equivalent(probe, probe.parent_path() / flipped, error) && !error;
        }
        const fs::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }
    const PathText root = directory.root_name().native();
    const PathText flipped_root = flip_ascii_case(root);
    if (!root.empty() && flipped_root != root) {
        answered = true;
        fs::path other(flipped_root);
        other += directory.root_directory();
        return fs::equivalent(directory.root_path(), other, error) && !error;
    }
    return false;
}

// Whether the filesystem that would hold this path matches names without
// regard to case. When neither probe can be answered - an empty directory
// whose path holds no ASCII letter anywhere - the answer is yes, because
// this decides a refusal: refusing two spellings that may be one file costs
// the caller a rename, while letting them through costs a file published
// at a name the caller did not ask for.
//
// Case sensitivity is a property of a volume, not of a single directory, on
// every filesystem this runs on. Where one directory departs from its
// volume - the per-directory flag Windows sets for WSL, which makes a
// directory case-sensitive on a volume that is not - the entry probe
// answers for that directory, and only the fallback answers for the volume
// instead, which refuses a pair it could have allowed.
bool names_ignore_case(const fs::path& path) {
    const fs::path directory = existing_directory(path);
    bool answered = false;
    const bool by_entry = entries_ignore_case(directory, answered);
    if (answered) {
        return by_entry;
    }
    const bool by_component = components_ignore_case(directory, answered);
    if (answered) {
        return by_component;
    }
    return true;
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
    if (!in_range(camera.roll_deg, -kMaxYawMagnitude, kMaxYawMagnitude) ||
        !in_range(camera.focal_x_override, 0.0, 10000.0) ||
        !in_range(camera.focal_y_override, 0.0, 10000.0)) {
        error = "camera roll or horizontal projection is invalid";
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
    if (camera.roll_deg != 0.0) {
        const double c = det_cos_deg(camera.roll_deg), s = det_sin_deg(camera.roll_deg);
        const Vec3 right = frame.right, up = frame.up;
        frame.right = {right.x*c-up.x*s, right.y*c-up.y*s, right.z*c-up.z*s};
        frame.up = {up.x*c+right.x*s, up.y*c+right.y*s, up.z*c+right.z*s};
    }
    const double half = camera.fov_y_deg * 0.5;
    frame.focal_y = det_cos_deg(half) / det_sin_deg(half);
    frame.focal_x = frame.focal_y * static_cast<double>(height) /
                    static_cast<double>(width);
    if (camera.focal_y_override != 0.0) frame.focal_y = camera.focal_y_override;
    if (camera.focal_x_override != 0.0) frame.focal_x = camera.focal_x_override;
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

// The one placement rule. Its contract, its evidence and the one element
// range it deliberately does not read are at its declaration and in
// docs/RENDERER.md, "Instances". The null branch is the mapping this
// renderer has always used, kept separate from the composition so that a
// profile with no instances is byte-identical with every earlier build:
// running the identity through the multiply would agree on every finite
// vertex but for the sign of a zero component.
Vec3 placed_vertex(const MeshVertex& vertex, const float* transform) noexcept {
    if (transform == nullptr) {
        return {vertex.px, vertex.py, -static_cast<double>(vertex.pz)};
    }
    const double slot[3] = {vertex.px, vertex.pz, vertex.py};
    double moved[3];
    for (int k = 0; k < 3; ++k) {
        moved[k] = slot[0] * static_cast<double>(transform[k]) +
                   slot[1] * static_cast<double>(transform[4 + k]) +
                   slot[2] * static_cast<double>(transform[8 + k]) +
                   static_cast<double>(transform[12 + k]);
    }
    return {moved[0], moved[2], -moved[1]};
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

const char* material_state_mode_name(MaterialStateMode mode) noexcept {
    switch (mode) {
    case MaterialStateMode::off: return "off";
    case MaterialStateMode::retail: return "retail";
    }
    return "unknown";
}

bool validate_material_coefficients(
    const MaterialCoefficients& coefficients, std::string& error) {
    const auto bounded = [&error](double value, const char* name) {
        if (!std::isfinite(value) || value < 0.0 ||
            value > kMaxMaterialCoefficient) {
            error = std::string(name) + " is outside 0.." +
                    std::to_string(static_cast<int>(kMaxMaterialCoefficient));
            return false;
        }
        return true;
    };
    if (!bounded(coefficients.alpha_min, "alpha_min") ||
        !bounded(coefficients.alpha_range, "alpha_range")) {
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        if (!bounded(coefficients.environment_min[k], "environment_min") ||
            !bounded(coefficients.environment_range[k], "environment_range")) {
            return false;
        }
    }
    if (coefficients.environment_power > kMaxEnvironmentPower) {
        error = "environment_power is above " +
                std::to_string(kMaxEnvironmentPower);
        return false;
    }
    return true;
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
        std::vector<unsigned char> blended(static_cast<std::size_t>(pixels), 0);
        rgba_ = std::move(rgba);
        depth_ = std::move(depth);
        ids_ = std::move(ids);
        blended_ = std::move(blended);
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
    double aq = 0.0;
    double rq = 0.0;
    double gq = 0.0;
    double bq = 0.0;
    double surface[7]{};
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
        screen[i].aq = v.a * q;
        screen[i].rq = v.r * q;
        screen[i].gq = v.g * q;
        screen[i].bq = v.b * q;
        for (int k=0;k<7;++k) screen[i].surface[k]=v.surface[k]*q;
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
    // Hoisted so that a draw without the retail state does exactly the
    // integer work it did before.
    const bool environment =
        !fallback && (material.environment[0] != 0.0 ||
                      material.environment[1] != 0.0 ||
                      material.environment[2] != 0.0);
    const bool scaled_alpha = !fallback && material.alpha_scale != 1.0;
    const bool blending = material.blend != RenderBlendMode::disabled;
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
                    double surface[7]{};
                    if (material.car_shading) for (int k=0;k<7;++k)
                        surface[k]=(l[0]*a->surface[k]+l[1]*b->surface[k]+l[2]*c->surface[k])*iq;
                    std::uint32_t color[4];
                    std::uint32_t source_alpha = 255;
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
                        if (material.linear_filter) {
                            sample_linear_repeat(*texture, u, tv, color);
                        } else {
                            const std::uint32_t tx = wrap_texel(u, texture->width);
                            const std::uint32_t ty = wrap_texel(tv, texture->height);
                            const unsigned char* texel =
                                &texture->rgba[(std::size_t{ty} * texture->width + tx) * 4U];
                            for (int k = 0; k < 4; ++k) color[k] = texel[k];
                        }
                        keep = color[3] >= options_.alpha_cutoff;
                        if (scaled_alpha || material.car_shading) {
                            const double value =
                                static_cast<double>(color[3]) * (material.car_shading ? surface[6] : material.alpha_scale);
                            source_alpha = value <= 0.0
                                               ? 0U
                                               : (value >= 255.0
                                                      ? 255U
                                                      : static_cast<std::uint32_t>(
                                                            value + 0.5));
                        } else {
                            source_alpha = color[3];
                        }
                        if (material.sky) {
                            const double va = (l[0]*a->aq + l[1]*b->aq + l[2]*c->aq) * iq;
                            source_alpha = std::min(255U, (color[3] * channel(va) * 2U + 127U) / 255U);
                        }
                        // D3DCMP_GREATER against D3DRS_ALPHAREF.
                        if (keep && material.alpha_test) {
                            keep = source_alpha > material.alpha_ref;
                        }
                    }
                    if (!keep) {
                        ++stats_.fragments_alpha_discarded;
                    } else if (!(z < frame_.depth_[index] ||
                                 (z == frame_.depth_[index] &&
                                  (frame_.ids_[index] == kNoTriangle ||
                                   id > frame_.ids_[index])))) {
                        ++stats_.fragments_depth_failed;
                    } else {
                        if (options_.vertex_color || material.sky) {
                            const double vc[3] = {
                                (l[0] * a->rq + l[1] * b->rq + l[2] * c->rq) * iq,
                                (l[0] * a->gq + l[1] * b->gq + l[2] * c->gq) * iq,
                                (l[0] * a->bq + l[1] * b->bq + l[2] * c->bq) * iq};
                            for (int k = 0; k < 3; ++k) {
                                color[k] = std::min(255U, (color[k] * channel(vc[k]) *
                                    (material.sky ? 2U : 1U) + 127U) / 255U);
                            }
                        }
                        const std::uint32_t shade = std::min<std::uint32_t>(material.shade, 256U);
                        unsigned char* out = &frame_.rgba_[index * 4U];
                        for (int k = 0; k < 3; ++k) {
                            std::uint32_t value =
                                material.diffuse_zeroed
                                    ? 0U
                                    : std::min<std::uint32_t>(
                                          255U, (color[k] * shade + 128U) >> 8U);
                            if (material.car_shading) {
                                const double shaded=color[k]*surface[k] + color[3]*surface[3+k];
                                value=static_cast<std::uint32_t>(std::clamp(std::floor(shaded+0.5),0.0,255.0));
                            }
                            if (environment && !material.car_shading) {
                                // 4 * diffuse_scale.w * envmap_sample *
                                // (EnvmapMin + envvdotn * EnvmapRange), times
                                // the texel alpha, in 0..255 units.
                                const double add = material.environment[k] *
                                                   static_cast<double>(color[3]) / 255.0;
                                const double sum =
                                    static_cast<double>(value) + std::floor(add + 0.5);
                                value = sum >= 255.0 ? 255U
                                                     : static_cast<std::uint32_t>(sum);
                            }
                            // Visual-only lamp glow. The same integer rule
                            // is in both offline fragment shaders.
                            const std::uint32_t lamp = material.lamp_mode == 1
                                ? (k == 2 ? 110U : 120U)
                                : material.lamp_mode == 2
                                ? (k == 0 ? 52U : k == 1 ? 4U : 2U)
                                : material.lamp_mode == 3
                                ? (k == 0 ? 192U : k == 1 ? 28U : 18U) : 0U;
                            value += ((255U - value) * lamp + 127U) / 255U;
                            if (material.reflected_scene && !fallback) {
                                const Frame& reflected = *material.reflected_scene;
                                const double va = std::clamp((l[0]*a->aq + l[1]*b->aq + l[2]*c->aq)*iq, 0.0, 255.0);
                                const double sx = (x + 0.5) * reflected.width() / frame_.width_ - 0.5;
                                const double sy = (y + 0.5) * reflected.height() / frame_.height_ - 0.5;
                                const int ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
                                const double fx = sx-ix, fy = sy-iy;
                                double sample = 0;
                                for (int dy=0; dy<2; ++dy) for (int dx=0; dx<2; ++dx) {
                                    const auto rx = std::clamp(ix+dx, 0, int(reflected.width())-1);
                                    const auto ry = std::clamp(iy+dy, 0, int(reflected.height())-1);
                                    sample += reflected.rgba()[(std::size_t(ry)*reflected.width()+rx)*4+k] *
                                        (dx ? fx : 1-fx) * (dy ? fy : 1-fy);
                                }
                                value = static_cast<std::uint32_t>(std::min(255.0,
                                    value + std::floor(sample * va * color[3] / (255.0*255.0) + 0.5)));
                            }
                            if (blending) {
                                const std::uint32_t dst = out[k];
                                if (material.blend == RenderBlendMode::source_over) {
                                    value = (value * source_alpha +
                                             dst * (255U - source_alpha) + 127U) / 255U;
                                } else {
                                    value = std::min<std::uint32_t>(
                                        255U, (value * source_alpha + 127U) / 255U + dst);
                                }
                            }
                            out[k] = static_cast<unsigned char>(value);
                        }
                        out[3] = 255;
                        if (material.depth_write) frame_.depth_[index] = z;
                        frame_.ids_[index] = id;
                        if (blending) {
                            ++stats_.fragments_blended;
                            if (frame_.blended_[index] != 0) {
                                ++stats_.fragments_blend_overlap;
                            }
                            frame_.blended_[index] = 1;
                        }
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

namespace {
// Approximation: static sky/horizon/ground and one broad light replace the
// untraced runtime HarmonicCoeff and six-face capture. Evaluated per vertex.
void car_surface(const MeshVertex& vertex, const float* transform, const Vec3& world,
                 const Vec3& eye, const CarShadingMaterial& c, double (&out)[7]) {
    Vec3 n{vertex.nx, vertex.ny, -vertex.nz};
    if (transform) {
        const Vec3 r0{transform[0],transform[1],transform[2]};
        const Vec3 r1{transform[4],transform[5],transform[6]};
        const Vec3 r2{transform[8],transform[9],transform[10]};
        const auto a=cross(r1,r2), b=cross(r2,r0), d=cross(r0,r1);
        const double det=dot(r0,a);
        if (std::abs(det)>1e-12) {
            const Vec3 moved{(vertex.nx*a.x+vertex.nz*b.x+vertex.ny*d.x)/det,
                             (vertex.nx*a.y+vertex.nz*b.y+vertex.ny*d.y)/det,
                             (vertex.nx*a.z+vertex.nz*b.z+vertex.ny*d.z)/det};
            n={moved.x,moved.z,-moved.y};
        }
    }
    if (!normalize(n)) n={0,1,0};
    Vec3 view=sub(eye,world); if (!normalize(view)) view={0,1,0};
    if (dot(n,view)<0) n={-n.x,-n.y,-n.z};
    const double v=std::clamp(dot(n,view),0.01,1.0);
    const Vec3 refl{2*v*n.x-view.x,2*v*n.y-view.y,2*v*n.z-view.z};
    Vec3 light{kLightX,kLightY,kLightZ}; normalize(light);
    const double diffuse=0.42+0.58*std::max(0.0,dot(n,light));
    const double spec=std::pow(std::max(0.0,dot(refl,light)),double(c.specular_min[3]));
    const double sv=std::pow(v,double(c.specular_min[3]));
    const double ev=std::pow(v,double(c.specular_range[3]));
    const double sky=std::clamp(refl.y*0.5+0.5,0.0,1.0);
    const double horizon=std::max(0.0,1.0-std::abs(refl.y)*5.0);
    const double band=std::max(0.0,1.0-std::abs(refl.x-0.35)*5.0)*horizon;
    const double ground[3]={0.055,0.045,0.035}, top[3]={0.20,0.27,0.38};
    const double alpha=c.gates[1]*(c.diffuse_min[3]+v*c.diffuse_range[3]);
    out[6]=alpha;
    for (int k=0;k<3;++k) {
        const double environment=ground[k]+sky*(top[k]-ground[k])+0.18*horizon+0.25*band;
        out[k]=c.gates[1]*diffuse*(c.diffuse_min[k]+v*c.diffuse_range[k]);
        out[3+k]=alpha*(c.gates[1]*spec*(c.specular_min[k]+sv*c.specular_range[k])+
                     4*c.gates[2]*environment*(c.environment_min[k]+ev*c.environment_range[k]));
    }
}
} // namespace

bool render_scene(
    const PreparedCarScene& scene,
    const RenderOptions& options,
    const RenderLimits& limits,
    Frame& frame,
    RenderStats& stats,
    std::string& error,
    const ReferenceDrawObserver& observe) {
    stats = RenderStats{};
    CameraFrame camera;
    if (!make_camera_frame(options.camera, std::max<std::uint32_t>(options.width, 1),
                           std::max<std::uint32_t>(options.height, 1), camera,
                           error)) {
        return false;
    }
    // Everything the loop below indexes is checked here, before the frame
    // is allocated: mesh and texture handles, drawable ranges, texture
    // sizes and the id range.
    for (const auto& texture : scene.textures) {
        if (texture.storage != RenderTextureStorage::rgba8) {
            error = "CPU rendering requires RGBA8 textures; GPU blocks are upload-only";
            return false;
        }
    }
    if (!validate_prepared_scene(scene, error)) {
        error = "invalid prepared scene: " + error;
        return false;
    }
    // Which copies of each mesh are drawn, and in what order. The rule
    // lives in the scene layer, next to the prepared scene it describes, so
    // that prepared_triangle_ranges assigns exactly the ids the loop below
    // hands out and an inspector or a second backend reproduces both.
    PreparedMeshInstances copies;
    if (!prepared_mesh_instances(scene, copies, error)) {
        return false;
    }
    // A profile with no instances draws every mesh once in its own space,
    // which is what this renderer has always done. That is deliberately not
    // spelled as an identity transform: running the composition would be
    // the identity map on every finite vertex but for the sign of a zero
    // component, and the two single-source profiles must stay byte-identical
    // with every earlier build.
    const bool instanced = !scene.instances.empty();
    std::uint64_t source_triangles = 0;
    for (const PreparedMesh& mesh : scene.meshes) {
        source_triangles += mesh.triangles.size();
    }
    if (source_triangles > limits.max_source_triangles ||
        source_triangles >= kNoTriangle) {
        error = "scene has " + std::to_string(source_triangles) +
                " triangles; the limit is " +
                std::to_string(limits.max_source_triangles);
        return false;
    }
    const std::uint64_t submitted = prepared_draw_triangles(scene);
    if (submitted > limits.max_submitted_triangles || submitted >= kNoTriangle) {
        error = "scene draws " + std::to_string(submitted) +
                " triangles; the limit is " +
                std::to_string(limits.max_submitted_triangles);
        return false;
    }
    if (scene.draws.size() >= kNoTriangle) {
        error = "scene has " + std::to_string(scene.draws.size()) +
                " material groups; the limit is " +
                std::to_string(kNoTriangle - 1);
        return false;
    }
    if (options.material_state == MaterialStateMode::retail &&
        !validate_material_coefficients(options.coefficients, error)) {
        return false;
    }
    std::vector<unsigned char> id_fallback;
    // One mark per drawn copy, set by the draw loop when that copy actually
    // submits a triangle, so the instance statistics below report what
    // reached the frame rather than restating the scene. A profile with no
    // instances has one copy per mesh, so the marks are indexed by mesh
    // handle there and by instance position everywhere else.
    std::vector<unsigned char> copy_drawn;
    try {
        id_fallback.reserve(static_cast<std::size_t>(submitted));
        copy_drawn.assign(instanced ? scene.instances.size() : scene.meshes.size(), 0);
    } catch (const std::bad_alloc&) {
        error = "not enough memory for the triangle table";
        return false;
    }
    // The order the groups are drawn in. With MaterialStateMode::off it is
    // the prepared order, as it has always been; with the retail state it
    // is the order the retail draw queue produces. The rule itself lives in
    // prepared_draw_order, next to the prepared scene it orders, so that an
    // inspector or a second backend reproduces the triangle ids assigned
    // below instead of restating the queue.
    const bool retail_order = options.material_state == MaterialStateMode::retail;
    std::vector<std::uint32_t> order;
    std::vector<unsigned char> layer;
    if (!prepared_draw_order(scene, retail_order, order, &layer, error)) {
        return false;
    }
    if (retail_order) {
        for (std::size_t i = 0; i < scene.draws.size(); ++i) {
            if (scene.draws[i].drawable) {
                ++stats.draw_rows[layer[i]];
            }
        }
    }

    if (!frame.reset(options.width, options.height, options.background, limits,
                     error)) {
        return false;
    }

    Vec3 light{kLightX, kLightY, kLightZ};
    normalize(light);
    Rasterizer rasterizer(frame, options, limits, stats);
    std::uint32_t next_id = 0;
    stats.meshes = scene.meshes.size() + scene.skipped_invalid_meshes.size();
    stats.meshes_skipped_invalid = scene.skipped_invalid_meshes.size();
    stats.meshes_rendered = scene.meshes.size();
    stats.unresolved_hashes = scene.unresolved_hashes.size();
    const MaterialCoefficients& coefficients = options.coefficients;
    const bool retail_state = options.material_state == MaterialStateMode::retail;
    for (const std::uint32_t d : order) {
        const PreparedDraw& draw = scene.draws[d];
        ++stats.material_groups;
        if (!draw.drawable) {
            ++stats.groups_invalid;
            continue;
        }
        const PreparedMesh& mesh = scene.meshes[draw.mesh];
        const std::uint64_t first = draw.first_index;
        const std::uint64_t count = draw.index_count;
        RasterMaterial raster;
        raster.linear_filter = true;
        switch (draw.binding) {
        case BindingKind::resolved:
            ++stats.groups_resolved;
            raster.texture = &scene.textures[draw.texture];
            break;
        case BindingKind::unresolved:
            ++stats.groups_unresolved;
            break;
        default:
            ++stats.groups_unusable;
            break;
        }
        fallback_color(draw.texture_hash, raster.fallback);
        // Count a group once if any submitted copy carries the state.
        bool counted_alpha_test = false, counted_blend = false;

        // One drawn copy per instance that names this group's mesh, in
        // ascending instance order, so the group's triangles stay one
        // contiguous id range whatever the draw order is. A profile with
        // no instances draws the mesh once, in its own space. How many
        // copies that is comes from PreparedMeshInstances::copies and from
        // nowhere else, which is what keeps this loop and the id ranges
        // prepared_triangle_ranges reports from ever disagreeing.
        const std::size_t copy_first = copies.first[draw.mesh];
        const std::uint64_t copy_count = copies.copies(draw.mesh);
        const RenderTexture* const base_texture = raster.texture;
        for (std::uint64_t c = 0; c < copy_count; ++c) {
            const std::size_t copy =
                instanced ? copies.order[copy_first + static_cast<std::size_t>(c)]
                          : draw.mesh;
            const float* const transform =
                instanced ? scene.instances[copy].transform : nullptr;
            const std::size_t override_texture =
                instanced && draw.texture_hash == kBrakeGlobalHash
                    ? scene.instances[copy].brake_left_texture : kNoHandle;
            raster.texture = override_texture != kNoHandle
                ? &scene.textures[override_texture] : base_texture;
            // Derive state after substitution, just as the command recorder does.
            raster.alpha_test = false;
            raster.blend = RenderBlendMode::disabled;
            // The retail state comes from the bound texture-info record only.
            // A group with no resolved texture has no proven state and keeps
            // the opaque defaults.
            bool environment = false;
            // The coefficient upload zeros whole float4 vectors, W lane
            // included, so a closed diffuse gate leaves diffuse_scale.w at
            // zero: the source alpha and the environment term both vanish.
            bool zero_alpha_lane = false;
            if (retail_state && raster.texture != nullptr) {
                const RenderTexture& texture = *raster.texture;
                raster.alpha_test = render_alpha_test_enabled(texture.alpha_test_mode);
                raster.blend = render_blend_mode_of(texture.alpha_blend_mode);
                if (raster.alpha_test && !counted_alpha_test) {
                    counted_alpha_test = true;
                    ++stats.groups_alpha_tested;
                }
                if (raster.blend != RenderBlendMode::disabled && !counted_blend) {
                    counted_blend = true;
                    ++stats.groups_blended;
                }
                const MaterialCoefficientGates gates = material_coefficient_gates(
                    draw.flags, coefficients.environment_enabled);
                zero_alpha_lane = !gates.diffuse;
                environment = gates.environment && !zero_alpha_lane &&
                              (coefficients.environment_min[0] != 0.0 ||
                               coefficients.environment_min[1] != 0.0 ||
                               coefficients.environment_min[2] != 0.0 ||
                               coefficients.environment_range[0] != 0.0 ||
                               coefficients.environment_range[1] != 0.0 ||
                               coefficients.environment_range[2] != 0.0);
            }
            // vdotn is needed whenever a coefficient the caller supplied can
            // change the fragment: the alpha lane or the environment term.
            const bool needs_view =
                retail_state && raster.texture != nullptr &&
                !zero_alpha_lane &&
                (environment || coefficients.alpha_min != 1.0 ||
                 coefficients.alpha_range != 0.0);

            raster.car_shading = options.car_shading && raster.texture && draw.shading.gates[0] != 0;
            raster.lamp_mode = instanced ? scene.instances[copy].lamp_mode : 0;
            if ((first + count) / 3 > first / 3) {
                copy_drawn[copy] = 1;
            }
            if (observe && count != 0) {
                observe(ReferenceDraw{d, draw.mesh, instanced ? copy : kNoHandle,
                    raster.texture ? (override_texture != kNoHandle
                        ? override_texture : draw.texture) : kNoHandle,
                    draw.first_index, draw.index_count, true, true, true,
                    options.cull, raster.blend, raster.alpha_test, raster.alpha_ref, raster.car_shading});
            }
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
                    world[k] = placed_vertex(source, transform);
                    clip[k] = project_point(camera, world[k]);
                    clip[k].u = source.u;
                    clip[k].v = source.v;
                    clip[k].r = source.color[0];
                    clip[k].g = source.color[1];
                    clip[k].b = source.color[2];
                    clip[k].a = source.color[3];
                    if (raster.car_shading)
                        car_surface(source,transform,world[k],camera.eye,draw.shading,clip[k].surface);
                }
                Vec3 normal = cross(sub(world[1], world[0]), sub(world[2], world[0]));
                const bool has_normal = normalize(normal);
                if (!has_normal) {
                    ++stats.triangles_degenerate_source;
                }
                // The eye-facing normal is shared by the flat shade factor and
                // by the effect's vdotn, and the test is the one flat shading
                // has always used.
                if (has_normal && dot(normal, sub(camera.eye, world[0])) < 0.0) {
                    normal = {-normal.x, -normal.y, -normal.z};
                }
                raster.shade = 256;
                if (options.shading == ShadingMode::flat && has_normal) {
                    const double k = 0.35 + 0.65 * std::max(0.0, dot(normal, light));
                    raster.shade = static_cast<std::uint32_t>(
                        std::floor(std::clamp(k, 0.0, 1.0) * 256.0 + 0.5));
                }
                raster.alpha_scale = zero_alpha_lane ? 0.0 : 1.0;
                raster.diffuse_zeroed = zero_alpha_lane;
                raster.environment[0] = 0.0;
                raster.environment[1] = 0.0;
                raster.environment[2] = 0.0;
                if (needs_view) {
                    // The effect evaluates vdotn per vertex from the stored
                    // normal; the reference evaluates it once per triangle from
                    // the geometric normal and the direction to the eye, the
                    // same flat model its shade factor uses.
                    double vdotn = 0.01;
                    if (has_normal) {
                        Vec3 view = sub(camera.eye, world[0]);
                        if (normalize(view)) {
                            vdotn = std::max(0.01, dot(view, normal));
                        }
                    }
                    const double alpha_scale =
                        coefficients.alpha_min + vdotn * coefficients.alpha_range;
                    raster.alpha_scale = alpha_scale;
                    if (environment) {
                        double envvdotn = 1.0;
                        for (std::uint32_t e = 0; e < coefficients.environment_power; ++e) {
                            envvdotn *= vdotn;
                        }
                        for (int k = 0; k < 3; ++k) {
                            raster.environment[k] =
                                4.0 * alpha_scale *
                                static_cast<double>(coefficients.environment_color[k]) *
                                (coefficients.environment_min[k] +
                                 envvdotn * coefficients.environment_range[k]);
                        }
                    }
                }
                if (!rasterizer.draw_triangle(clip, raster, id, error)) {
                    return false;
                }
            }
        }
    }

    // Drawn copies, counted from the marks the loop above set rather than
    // from the scene, so a backend that skipped a copy reports fewer of
    // them instead of describing a frame it did not draw. A copy of a mesh
    // whose every material group is undrawable submits nothing and is not
    // counted; the `meshes` line above still reports the mesh.
    for (std::size_t i = 0; i < copy_drawn.size(); ++i) {
        if (copy_drawn[i] == 0) {
            continue;
        }
        ++stats.instances;
        // A mesh drawn in its own authored space claims no placement, so a
        // profile with no instances has no stand-in copy.
        ++(!instanced || scene.instances[i].placement_exact
               ? stats.instances_exact
               : stats.instances_stand_in);
    }

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
            if (frame.blended()[std::size_t{y} * frame.width() + x] != 0) {
                ++stats.pixels_blended;
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
           path_to_utf8(path);
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

bool same_file_path(const std::filesystem::path& a, const std::filesystem::path& b) {
    // Every caller uses this to refuse, and neither write_rgba_tga nor the
    // viewer has a handler, so nothing is allowed out. An allocation that
    // throws while comparing two paths answers "one file", which refuses
    // the pair: the direction that cannot publish a file at a name the
    // caller did not ask for.
    try {
        std::error_code error;
        if (fs::equivalent(a, b, error) && !error) {
            return true;
        }
        const fs::path left = absolute_name(a);
        const fs::path right = absolute_name(b);
        if (left == right) {
            return true;
        }
        // One spelling but for case. That is one file wherever the
        // filesystem folds case, so the filesystem is asked rather than
        // guessed at.
        if (!same_name_nocase(left.native(), right.native())) {
            return false;
        }
        return names_ignore_case(left);
    } catch (...) {
        return true;
    }
}

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
    return write_rgba_tga(frame.width(), frame.height(), frame.rgba(), target, sources,
                          result);
}

bool write_rgba_tga(
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgba,
    const std::filesystem::path& target,
    const std::vector<std::filesystem::path>& sources,
    FrameWriteResult& result) {
    result.error.clear();
    result.not_removed.clear();
    if (width == 0 || height == 0) {
        result.error = "image is empty";
        return false;
    }
    fs::path partial = target;
    partial += ".partial";
    for (const fs::path& source : sources) {
        if (same_file_path(source, target) || same_file_path(source, partial)) {
            result.error = "output path must differ from every input file";
            return false;
        }
    }
    // Early refusal only; publication below never replaces a target that
    // appears later.
    if (path_exists(target)) {
        result.error = "refusing to overwrite " + path_to_utf8(target.filename());
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
            if (!write_tga(stream, width, height, rgba, error)) {
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
                       path_to_utf8(partial.filename());
        return false;
    }
    if (opened == PartialFile::Open::failed) {
        // Nothing was created.
        result.error = "failed to create " + path_to_utf8(partial.filename());
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
        return fail("failed to write " + path_to_utf8(partial.filename()));
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
                    path_to_utf8(target.filename()));
    case PartialFile::Publish::failed:
        break;
    }
    return fail("failed to publish " + path_to_utf8(target.filename()) +
                (publish_error.empty() ? "" : " (" + publish_error + ")"));
}

} // namespace nfsu
