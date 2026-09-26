#include "nfsu/track_contact.h"

#include "nfsu/det_sqrt.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace nfsu {
namespace {

static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 float32 required");

// The split docs/TRACK_COLLISION.md justifies and query_track_collision_ray
// uses: triangle 0 is corners (0,1,2) and triangle 1 is (0,2,3).
constexpr std::size_t kTriangleCorners[2][3] = {{0, 1, 2}, {0, 2, 3}};
constexpr std::size_t kTrianglesPerPolygon = 2;

std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    return b > max - a ? max : a + b;
}

std::uint64_t saturating_multiply(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    if (a != 0 && b > max / a) {
        return max;
    }
    return a * b;
}

bool finite3(const float v[3]) noexcept {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

TrackContactTriangle derive_triangle(
    const TrackCollisionPolygon& polygon,
    std::size_t triangle) {
    const float* a = polygon.corner[kTriangleCorners[triangle][0]];
    const float* b = polygon.corner[kTriangleCorners[triangle][1]];
    const float* c = polygon.corner[kTriangleCorners[triangle][2]];
    const float edge1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float edge2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float cross[3] = {edge1[1] * edge2[2] - edge1[2] * edge2[1],
                            edge1[2] * edge2[0] - edge1[0] * edge2[2],
                            edge1[0] * edge2[1] - edge1[1] * edge2[0]};
    const float length = std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] +
                                   cross[2] * cross[2]);
    TrackContactTriangle out;
    // A repeated corner, or three collinear ones, give a zero cross product.
    // Such a triangle has no normal and no area, and the determinant test
    // below can never accept it either, so it is marked and skipped.
    if (!std::isfinite(length) || !(length > 0.0F)) {
        return out;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        out.normal[axis] = cross[axis] / length;
    }
    out.up = std::fabs(out.normal[2]);
    out.kind = out.up >= kTrackContactRoadMinUp ? TrackContactKind::road
                                                : TrackContactKind::wall;
    out.degenerate = false;
    return out;
}

void fill_resident(TrackContactWorld& world) {
    TrackContactMemory& memory = world.memory;
    const TrackCollisionReport& report = world.collision;
    memory.polygon_bytes = saturating_multiply(report.polygons.size(),
                                               sizeof(TrackCollisionPolygon));
    memory.cell_bytes =
        saturating_multiply(report.cells.size(), sizeof(TrackCollisionCell));
    memory.index_bytes =
        saturating_multiply(report.indices.size(), sizeof(std::uint32_t));
    memory.node_bytes =
        saturating_multiply(report.nodes.size(), sizeof(TrackCollisionTreeNode));
    memory.triangle_bytes =
        saturating_multiply(world.triangles.size(), sizeof(TrackContactTriangle));
    const std::uint64_t parts[] = {memory.polygon_bytes, memory.cell_bytes,
                                   memory.index_bytes, memory.node_bytes,
                                   memory.triangle_bytes};
    memory.resident_bytes = 0;
    memory.resident_allocations = 0;
    for (std::uint64_t part : parts) {
        memory.resident_bytes = saturating_add(memory.resident_bytes, part);
        memory.resident_allocations += part != 0 ? 1U : 0U;
    }
    memory.stored_bytes = saturating_add(
        saturating_add(report.cost.source_polygon_bytes,
                       report.cost.source_cell_bytes),
        saturating_add(report.cost.source_index_bytes,
                       report.cost.source_tree_bytes));
}

// Moller-Trumbore, in exactly the arithmetic query_track_collision_ray uses,
// so that wherever that query names the nearest surface a segment along the
// same line gives the same polygon, triangle, distance and point. It also
// hands back the determinant, whose sign says which side was hit:
// det = edge1 . (direction x edge2) = -(direction . (edge1 x edge2)).
//
// Its barycentrics are rounded in float, so it is not watertight: a point
// exactly on an edge two triangles share can round outside both, and the
// review of issue #158 found road seams on retail data that it misses from
// either side, and wall quads' own (0,2) diagonals that it misses while it
// meets a farther surface (issue #167). conservative_triangle below is the
// test that decides whether a nearer surface was missed.
bool segment_triangle(
    const float origin[3],
    const float direction[3],
    const float a[3],
    const float b[3],
    const float c[3],
    float max_distance,
    float& distance,
    float& determinant) {
    const float edge1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float edge2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float pvec[3] = {
        direction[1] * edge2[2] - direction[2] * edge2[1],
        direction[2] * edge2[0] - direction[0] * edge2[2],
        direction[0] * edge2[1] - direction[1] * edge2[0]};
    const float det =
        edge1[0] * pvec[0] + edge1[1] * pvec[1] + edge1[2] * pvec[2];
    // A degenerate triangle and a segment lying in the triangle's plane both
    // land here, and neither is a contact.
    if (!(std::fabs(det) > 0.0F)) {
        return false;
    }
    const float inv = 1.0F / det;
    const float tvec[3] = {
        origin[0] - a[0], origin[1] - a[1], origin[2] - a[2]};
    const float u =
        (tvec[0] * pvec[0] + tvec[1] * pvec[1] + tvec[2] * pvec[2]) * inv;
    if (u < 0.0F || u > 1.0F) {
        return false;
    }
    const float qvec[3] = {
        tvec[1] * edge1[2] - tvec[2] * edge1[1],
        tvec[2] * edge1[0] - tvec[0] * edge1[2],
        tvec[0] * edge1[1] - tvec[1] * edge1[0]};
    const float v = (direction[0] * qvec[0] + direction[1] * qvec[1] +
                     direction[2] * qvec[2]) *
                    inv;
    if (v < 0.0F || u + v > 1.0F) {
        return false;
    }
    const float t =
        (edge2[0] * qvec[0] + edge2[1] * qvec[1] + edge2[2] * qvec[2]) * inv;
    if (!std::isfinite(t) || t < 0.0F || t > max_distance) {
        return false;
    }
    distance = t;
    determinant = det;
    return true;
}

// The unit roundoff of double, 2^-53.
constexpr double kUnitRoundoff = 1.0 / 9007199254740992.0;

// A bound on the rounding of one 3x3 determinant below, relative to its
// permanent - the same sum with every product taken in magnitude. Each of
// the determinant's six terms is the product of three differences of two
// floats and meets at most eight roundings on its way to the result: the
// three differences, the product of two of them, their difference inside a
// cross product, the product with the third, and two sums. So the error is
// at most gamma(8) = 8u / (1 - 8u) times the exact permanent (Higham,
// Accuracy and Stability of Numerical Algorithms, 3.1), and the permanent as
// computed is within a few u of the exact one. 16u, twice that, is the bound.
// Every input is a finite float, so nothing here overflows or underflows: a
// product of three differences of finite floats, when it is not zero, lies
// between 2^-447 and 2^387 in magnitude.
constexpr double kDeterminantBound = 16.0 * kUnitRoundoff;

// How much nearer, as a fraction of the segment, a surface the conservative
// test meets has to be, for certain, before it replaces the answer of issue
// #39's arithmetic: 2^-23, one rounding of a float distance as long as the
// segment. A difference below it is one the answer's float distance cannot
// express, and there #39's answer stands bit for bit.
constexpr double kNearerMargin = 1.0 / 8388608.0;

// The segment as conservative_triangle decides on it: its start exactly,
// to - from rounded at most once per component, and its closed bounding box,
// whose corners are the endpoints' own floats.
struct Sweep {
    double start[3] = {0.0, 0.0, 0.0};
    double delta[3] = {0.0, 0.0, 0.0};
    double length = 0.0;
    float low[3] = {0.0F, 0.0F, 0.0F};
    float high[3] = {0.0F, 0.0F, 0.0F};
};

Sweep sweep_of(const float from[3], const float to[3]) {
    Sweep sweep;
    double squared = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        sweep.start[axis] = static_cast<double>(from[axis]);
        sweep.delta[axis] =
            static_cast<double>(to[axis]) - static_cast<double>(from[axis]);
        squared += sweep.delta[axis] * sweep.delta[axis];
        sweep.low[axis] = std::min(from[axis], to[axis]);
        sweep.high[axis] = std::max(from[axis], to[axis]);
    }
    // det_sqrt, not std::sqrt: MinGW's out-of-line libm sqrt is not
    // correctly rounded at -O0 (issue #165, the same defect issue #125
    // found in the renderer). squared is a sum of squared doubles, so this
    // is the one double-precision library sqrt on this file's replayed
    // path; the two float ones (derive_triangle's face normal and
    // query_track_contact_segment's own direction length) round once by
    // construction and need no change.
    sweep.length = det_sqrt(squared);
    return sweep;
}

// What conservative_triangle finds. `plane` says the segment's line is not
// within rounding of parallel to the triangle's plane, and then `along` is
// where it crosses that plane, as a fraction of the segment, within `error`.
// `met` says the closed triangle is met; `side` is then the sign of
// (to - from) . normal: -1 when the stored normal already faces the start.
struct Crossing {
    bool plane = false;
    double along = 0.0;
    double error = 0.0;
    bool met = false;
    int side = 0;
};

void cross_with_magnitude(const double p[3], const double q[3], double out[3],
                          double magnitude[3]) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const std::size_t j = (axis + 1) % 3;
        const std::size_t k = (axis + 2) % 3;
        const double first = p[j] * q[k];
        const double second = p[k] * q[j];
        out[axis] = first - second;
        magnitude[axis] = std::fabs(first) + std::fabs(second);
    }
}

double dot(const double p[3], const double q[3]) {
    return p[0] * q[0] + p[1] * q[1] + p[2] * q[2];
}

double dot_magnitude(const double p[3], const double magnitude[3]) {
    return std::fabs(p[0]) * magnitude[0] + std::fabs(p[1]) * magnitude[1] +
           std::fabs(p[2]) * magnitude[2];
}

// -1, 0 or +1: the sign of an exact value computed as `value` with an error
// of at most `bound`, and 0 where the rounding could hide it.
int certain_sign(double value, double bound) {
    if (value > bound) {
        return 1;
    }
    return value < -bound ? -1 : 0;
}

// Whether the closed segment meets a closed triangle, decided on the
// segment's own endpoints rather than on a rounded direction. With the
// corners moved to the segment's start, A, B and C, and d = to - from, five
// determinants decide it:
//
//   u = d . (B x C), v = d . (C x A), w = d . (A x B)
//       which side of each edge the segment's line passes; the line meets
//       the closed triangle exactly when none of them has the opposite sign
//       to the others, and u + v + w = d . ((B - A) x (C - A)) = d . normal;
//   start = A . (B x C), end = start - (u + v + w)
//       which side of the triangle's plane each endpoint lies on; the
//       segment reaches the plane when they do not share a sign, at the
//       fraction start / (u + v + w) of its length.
//
// Each is taken in double with its own rounding bound (kDeterminantBound),
// and a sign the rounding could hide counts as 0, which rejects nothing. So
// the test is conservative: every closed triangle the exact segment meets is
// met - on an edge or a corner, a shared one or a triangle's own outer edge,
// and at either end of the segment - except where the segment lies within
// rounding of parallel to the plane, which is edge-on and never a contact,
// as a segment in the plane is not. The converse is not exact: a triangle
// the segment misses by less than that rounding - about 1e-14 world units on
// a sweep a few units long, growing with the square of the distances from
// the segment's start - may be met. Nothing about it depends on which
// triangle a corner belongs to, so a point on a shared edge is met by both
// triangles, never by neither.
Crossing conservative_triangle(const Sweep& sweep, const float a[3],
                               const float b[3], const float c[3]) {
    Crossing out;
    double corner_a[3];
    double corner_b[3];
    double corner_c[3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        corner_a[axis] = static_cast<double>(a[axis]) - sweep.start[axis];
        corner_b[axis] = static_cast<double>(b[axis]) - sweep.start[axis];
        corner_c[axis] = static_cast<double>(c[axis]) - sweep.start[axis];
    }
    double bc[3];
    double bc_magnitude[3];
    double ca[3];
    double ca_magnitude[3];
    double ab[3];
    double ab_magnitude[3];
    cross_with_magnitude(corner_b, corner_c, bc, bc_magnitude);
    cross_with_magnitude(corner_c, corner_a, ca, ca_magnitude);
    cross_with_magnitude(corner_a, corner_b, ab, ab_magnitude);
    const double* d = sweep.delta;
    const double u = dot(d, bc);
    const double u_bound = kDeterminantBound * dot_magnitude(d, bc_magnitude);
    const double v = dot(d, ca);
    const double v_bound = kDeterminantBound * dot_magnitude(d, ca_magnitude);
    const double w = dot(d, ab);
    const double w_bound = kDeterminantBound * dot_magnitude(d, ab_magnitude);
    const double start = dot(corner_a, bc);
    const double start_bound =
        kDeterminantBound * dot_magnitude(corner_a, bc_magnitude);
    const double det = u + v + w;
    const double det_bound =
        u_bound + v_bound + w_bound +
        4.0 * kUnitRoundoff * (std::fabs(u) + std::fabs(v) + std::fabs(w));
    const double end = start - det;
    const double end_bound = start_bound + det_bound +
                             4.0 * kUnitRoundoff * (std::fabs(start) + std::fabs(det));
    // Within rounding of parallel to the plane: edge-on, never a contact.
    if (!(std::fabs(det) > det_bound)) {
        return out;
    }
    out.plane = true;
    out.along = start / det;
    out.error = (start_bound + std::fabs(out.along) * det_bound) /
                    (std::fabs(det) - det_bound) +
                4.0 * kUnitRoundoff * std::fabs(out.along);
    const int side = det > 0.0 ? 1 : -1;
    // The line passes outside an edge for certain.
    if (certain_sign(u, u_bound) == -side || certain_sign(v, v_bound) == -side ||
        certain_sign(w, w_bound) == -side) {
        return out;
    }
    // The plane lies before the start, or beyond the end, for certain.
    if (certain_sign(start, start_bound) == -side ||
        certain_sign(end, end_bound) == side) {
        return out;
    }
    out.met = true;
    out.side = side;
    return out;
}

// A margin on the fraction of the segment in box_meets_segment's clip, 2^-30,
// far above the clip's own rounding: each fraction is a difference of two
// floats divided by another such difference, three roundings, so within 4u
// of its exact value, and a fraction that decides anything lies in [0, 1].
constexpr double kClipMargin = 1.0 / 1073741824.0;

// Whether the segment can meet the triangle's closed bounding box. First
// the two boxes, compared as floats and so exactly; then the segment is
// clipped to the triangle's box axis by axis, in double, and rejected only
// when the parts of it within each axis's slab are apart by more than
// kClipMargin. A triangle this rejects cannot meet the segment.
bool box_meets_segment(const Sweep& sweep, const float a[3], const float b[3],
                       const float c[3]) {
    float low[3];
    float high[3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        low[axis] = std::min(a[axis], std::min(b[axis], c[axis]));
        high[axis] = std::max(a[axis], std::max(b[axis], c[axis]));
        if (high[axis] < sweep.low[axis] || low[axis] > sweep.high[axis]) {
            return false;
        }
    }
    double enter = 0.0;
    double leave = 1.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        // No movement along this axis: the box test above already holds the
        // segment within the slab.
        if (sweep.delta[axis] == 0.0) {
            continue;
        }
        double first =
            (static_cast<double>(low[axis]) - sweep.start[axis]) / sweep.delta[axis];
        double second =
            (static_cast<double>(high[axis]) - sweep.start[axis]) / sweep.delta[axis];
        if (first > second) {
            std::swap(first, second);
        }
        enter = std::max(enter, first);
        leave = std::min(leave, second);
    }
    return enter <= leave + kClipMargin;
}

// Lexicographic on (distance, polygon, triangle), so the answer does not
// depend on the order the cells are visited in or on how many of them name
// the same polygon.
bool better(float distance, std::size_t polygon, std::size_t triangle,
            const TrackContact& best) {
    if (distance != best.distance) {
        return distance < best.distance;
    }
    if (polygon != best.polygon) {
        return polygon < best.polygon;
    }
    return triangle < best.triangle;
}

// Shewchuk's error bound for the 2x2 determinant square_meets takes in
// double is (3 + 16 * 2^-53) * 2^-53, about 3.3e-16, times the sum of the
// magnitudes of its two products. This is three times that.
constexpr double kSideErrorBound = 1.0e-15;

// The horizontal projection of a segment, which is what selects cells: the
// index is two-dimensional.
struct Footprint {
    float start[2] = {0.0F, 0.0F};
    float low[2] = {0.0F, 0.0F};
    float high[2] = {0.0F, 0.0F};
    double delta[2] = {0.0, 0.0};
};

Footprint footprint_of(const float from[3], const float to[3]) {
    Footprint footprint;
    for (std::size_t axis = 0; axis < 2; ++axis) {
        footprint.start[axis] = from[axis];
        footprint.low[axis] = std::min(from[axis], to[axis]);
        footprint.high[axis] = std::max(from[axis], to[axis]);
        footprint.delta[axis] =
            static_cast<double>(to[axis]) - static_cast<double>(from[axis]);
    }
    return footprint;
}

// Whether the closed square of half extent `half` about `center` meets the
// footprint. Two convex shapes in the plane are apart exactly when some axis
// separates them, and for a segment and an axis-aligned square the axes to
// try are the square's two and the segment's normal. The first two are the
// box test query_track_collision_cells makes, which is all a column needs.
// The third asks whether all four corners lie strictly on one side of the
// segment's line, which is what keeps a diagonal sweep from selecting a
// cell its bounding box reaches and the segment does not. A corner nearer
// the line than the determinant's rounding counts as on it, so a doubtful
// square is kept: a kept square can only add polygons for the triangle test
// to reject, while a dropped one could lose a contact.
bool square_meets(const Footprint& footprint, float center0, float center1,
                  float half) {
    const float low0 = center0 - half;
    const float high0 = center0 + half;
    const float low1 = center1 - half;
    const float high1 = center1 + half;
    if (high0 < footprint.low[0] || low0 > footprint.high[0] ||
        high1 < footprint.low[1] || low1 > footprint.high[1]) {
        return false;
    }
    const float xs[2] = {low0, high0};
    const float zs[2] = {low1, high1};
    int above = 0;
    int below = 0;
    for (float x : xs) {
        for (float z : zs) {
            const double left =
                footprint.delta[0] *
                (static_cast<double>(z) - static_cast<double>(footprint.start[1]));
            const double right =
                footprint.delta[1] *
                (static_cast<double>(x) - static_cast<double>(footprint.start[0]));
            const double side = left - right;
            const double bound =
                kSideErrorBound * (std::fabs(left) + std::fabs(right));
            if (side > bound) {
                ++above;
            } else if (side < -bound) {
                ++below;
            } else {
                return true;
            }
        }
    }
    return above != 4 && below != 4;
}

// The leaf cells whose closed 32 by 32 square meets the footprint, found by
// walking the quadtree from the same root, in the same quadrant order and
// to the same depth as query_track_collision_cells, but descending only
// into squares the segment itself meets. For a column the footprint is a
// point and the two walks select the same cells. The cap counts only the
// cells selected. False when the cap is reached, or when the tree names a
// node or a cell past its array, which the parser rules out for a report it
// marked valid and which is checked again here because a world may be
// assembled by hand.
bool segment_cells(const TrackCollisionReport& report,
                   const Footprint& footprint,
                   std::size_t max_cells,
                   std::vector<std::size_t>& out) {
    out.clear();
    struct Frame {
        std::size_t node;
        std::uint32_t depth;
        float center[2];
        float half;
    };
    std::vector<Frame> stack;
    stack.push_back(Frame{0, 0, {0.0F, 0.0F}, kCollisionRootHalfExtent});
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        if (frame.node >= report.nodes.size()) {
            return false;
        }
        const TrackCollisionTreeNode& node = report.nodes[frame.node];
        const float quarter = frame.half * 0.5F;
        for (std::size_t slot = 0; slot < kCollisionTreeNodeEntries; ++slot) {
            const std::uint16_t entry = node.entries[slot];
            if (entry == 0) {
                continue;
            }
            const float c0 =
                frame.center[0] + ((slot & 1U) != 0 ? quarter : -quarter);
            const float c1 =
                frame.center[1] + ((slot & 2U) != 0 ? quarter : -quarter);
            if (!square_meets(footprint, c0, c1, quarter)) {
                continue;
            }
            if (frame.depth + 1 < kCollisionTreeDepth) {
                stack.push_back(Frame{entry, frame.depth + 1, {c0, c1}, quarter});
                continue;
            }
            if (entry >= report.cells.size() || out.size() >= max_cells) {
                return false;
            }
            out.push_back(entry);
        }
    }
    return true;
}

// The nearest triangle issue #39's arithmetic accepts among the named
// polygons, ordered by better(); a polygon named by several cells is simply
// tested again and loses the tie to itself. best.distance starts at the
// segment's length, so a crossing exactly at the segment's end is still
// found.
bool nearest_moller_trumbore(const TrackContactWorld& world,
                             const std::vector<std::uint32_t>& named,
                             const float from[3],
                             const float direction[3],
                             float length,
                             TrackContact& best,
                             float& best_det) {
    bool found = false;
    best = TrackContact{};
    best.distance = length;
    for (const std::uint32_t polygon_index : named) {
        const TrackCollisionPolygon& polygon = world.collision.polygons[polygon_index];
        for (std::size_t t = 0; t < kTrianglesPerPolygon; ++t) {
            if (world.triangles[polygon_index * kTrianglesPerPolygon + t].degenerate) {
                continue;
            }
            const float* a = polygon.corner[kTriangleCorners[t][0]];
            const float* b = polygon.corner[kTriangleCorners[t][1]];
            const float* c = polygon.corner[kTriangleCorners[t][2]];
            float distance = 0.0F;
            float det = 0.0F;
            if (!segment_triangle(from, direction, a, b, c, best.distance, distance,
                                  det)) {
                continue;
            }
            if (found && !better(distance, polygon_index, t, best)) {
                continue;
            }
            found = true;
            best.polygon = polygon_index;
            best.triangle = t;
            best.distance = distance;
            best_det = det;
        }
    }
    return found;
}

// The conservative test on one polygon's triangle: the query uses it for the
// plane of the triangle issue #39's arithmetic names.
Crossing crossing_of(const TrackContactWorld& world, const Sweep& sweep,
                     std::size_t polygon_index, std::size_t t) {
    const TrackCollisionPolygon& polygon = world.collision.polygons[polygon_index];
    return conservative_triangle(sweep, polygon.corner[kTriangleCorners[t][0]],
                                 polygon.corner[kTriangleCorners[t][1]],
                                 polygon.corner[kTriangleCorners[t][2]]);
}

// The nearest triangle conservative_triangle meets among the named
// polygons, in the same order: its distance is the crossing's fraction of
// the segment times the segment's length, rounded to float and clamped to
// the query's own length, so a contact at either end is found and two
// triangles that share the crossing point tie as they do under better().
// `crossing` receives the winner's own crossing.
bool nearest_conservative(const TrackContactWorld& world,
                          const std::vector<std::uint32_t>& named,
                          const Sweep& sweep,
                          float length,
                          TrackContact& best,
                          Crossing& crossing) {
    bool found = false;
    best = TrackContact{};
    best.distance = length;
    for (const std::uint32_t polygon_index : named) {
        const TrackCollisionPolygon& polygon = world.collision.polygons[polygon_index];
        for (std::size_t t = 0; t < kTrianglesPerPolygon; ++t) {
            if (world.triangles[polygon_index * kTrianglesPerPolygon + t].degenerate) {
                continue;
            }
            const float* a = polygon.corner[kTriangleCorners[t][0]];
            const float* b = polygon.corner[kTriangleCorners[t][1]];
            const float* c = polygon.corner[kTriangleCorners[t][2]];
            if (!box_meets_segment(sweep, a, b, c)) {
                continue;
            }
            const Crossing here = conservative_triangle(sweep, a, b, c);
            if (!here.met) {
                continue;
            }
            const double fraction = std::min(1.0, std::max(0.0, here.along));
            const float distance =
                std::min(static_cast<float>(fraction * sweep.length), length);
            if (found && !better(distance, polygon_index, t, best)) {
                continue;
            }
            found = true;
            best.polygon = polygon_index;
            best.triangle = t;
            best.distance = distance;
            crossing = here;
        }
    }
    return found;
}

} // namespace

const char* track_contact_kind_name(TrackContactKind kind) noexcept {
    switch (kind) {
    case TrackContactKind::road:
        return "road";
    case TrackContactKind::wall:
        return "wall";
    }
    return "unknown";
}

const char* track_contact_status_name(TrackContactStatus status) noexcept {
    switch (status) {
    case TrackContactStatus::hit:
        return "hit";
    case TrackContactStatus::clear:
        return "clear";
    case TrackContactStatus::outside:
        return "outside";
    case TrackContactStatus::refused:
        return "refused";
    }
    return "unknown";
}

TrackContactWorld build_track_contact_world(TrackCollisionReport collision) {
    TrackContactWorld world;
    world.collision = std::move(collision);
    if (world.collision.valid) {
        const std::size_t polygons = world.collision.polygons.size();
        world.triangles.resize(polygons * kTrianglesPerPolygon);
        for (std::size_t p = 0; p < polygons; ++p) {
            for (std::size_t t = 0; t < kTrianglesPerPolygon; ++t) {
                TrackContactTriangle& triangle =
                    world.triangles[p * kTrianglesPerPolygon + t];
                triangle = derive_triangle(world.collision.polygons[p], t);
                if (triangle.degenerate) {
                    ++world.counts.degenerate_triangles;
                } else if (triangle.kind == TrackContactKind::road) {
                    ++world.counts.road_triangles;
                } else {
                    ++world.counts.wall_triangles;
                }
            }
        }
        world.valid = true;
    }
    fill_resident(world);
    world.memory.load_peak_bytes = world.memory.resident_bytes;
    return world;
}

TrackContactWorld load_track_contact_world(
    const std::filesystem::path& path,
    const TrackCollisionLimits& limits) {
    std::vector<unsigned char> image;
    TrackCollisionReport report = load_track_collision(path, image, limits);
    const std::uint64_t image_bytes = image.size();
    // The image is not needed once it is parsed: every array the report
    // holds is a copy. Released here, before the triangle table is built,
    // which is why the peak below does not count the two together.
    std::vector<unsigned char>().swap(image);
    const std::uint64_t packed_bytes = report.cost.packed_bytes;

    TrackContactWorld world = build_track_contact_world(std::move(report));
    TrackContactMemory& memory = world.memory;
    memory.packed_bytes = packed_bytes;
    memory.image_bytes = image_bytes;
    // load_track_collision keeps the packed file and the image alive while
    // it parses, and the parse builds the report's four arrays: that is the
    // first peak. The triangle table is built after both buffers are gone:
    // that is the second. The parser's own temporaries - two bit vectors
    // and a walk stack, each bounded by an array above - are not counted.
    const std::uint64_t while_parsing = saturating_add(
        saturating_add(packed_bytes, image_bytes),
        memory.resident_bytes - memory.triangle_bytes);
    memory.load_peak_bytes = std::max(while_parsing, memory.resident_bytes);
    return world;
}

std::uint64_t track_contact_resident_ceiling(
    const TrackCollisionLimits& limits) noexcept {
    std::uint64_t total =
        saturating_multiply(limits.max_polygons, sizeof(TrackCollisionPolygon));
    total = saturating_add(
        total, saturating_multiply(limits.max_cells, sizeof(TrackCollisionCell)));
    total = saturating_add(
        total, saturating_multiply(limits.max_indices, sizeof(std::uint32_t)));
    total = saturating_add(
        total,
        saturating_multiply(limits.max_tree_nodes, sizeof(TrackCollisionTreeNode)));
    total = saturating_add(
        total,
        saturating_multiply(saturating_multiply(limits.max_polygons,
                                                kTrianglesPerPolygon),
                            sizeof(TrackContactTriangle)));
    return total;
}

bool place_track_contact_memory(
    const TrackContactMemory& memory,
    MemoryPlan& plan,
    TrackContactPlacement& placement) {
    placement = TrackContactPlacement{};
    MemoryArena& persistent = plan.arena(MemoryCategory::persistent);
    MemoryArena& streaming = plan.arena(MemoryCategory::streaming);
    placement.persistent_capacity = persistent.stats().capacity;
    placement.streaming_capacity = streaming.stats().capacity;

    struct Request {
        const char* part;
        std::uint64_t size;
        MemoryArena* arena;
    };
    // In the order a load holds them: the two buffers first, then the
    // report's arrays while the buffers are alive, then the triangle table.
    const Request requests[] = {
        {"packed file", memory.packed_bytes, &streaming},
        {"image", memory.image_bytes, &streaming},
        {"polygons", memory.polygon_bytes, &persistent},
        {"cells", memory.cell_bytes, &persistent},
        {"indices", memory.index_bytes, &persistent},
        {"nodes", memory.node_bytes, &persistent},
        {"triangles", memory.triangle_bytes, &persistent},
    };
    constexpr std::size_t kRequests = sizeof(requests) / sizeof(requests[0]);
    ArenaAllocation placed[kRequests];
    bool held[kRequests] = {};
    const auto release_held = [&]() {
        for (std::size_t i = kRequests; i > 0; --i) {
            if (held[i - 1]) {
                requests[i - 1].arena->release(placed[i - 1].handle);
                held[i - 1] = false;
            }
        }
    };

    for (std::size_t i = 0; i < kRequests; ++i) {
        // A world built in memory was never loaded, so it has no buffers to
        // place. Nothing else is ever zero in a valid world.
        if (requests[i].size == 0) {
            continue;
        }
        const ArenaRefusal refusal = requests[i].arena->allocate(
            requests[i].size, kTrackContactAllocationAlignment, placed[i]);
        if (refusal != ArenaRefusal::none) {
            release_held();
            placement.refusal = refusal;
            placement.refused_part = requests[i].part;
            return false;
        }
        held[i] = true;
    }

    const ArenaStats streaming_peak = streaming.stats();
    placement.streaming_peak_bytes = streaming_peak.used_bytes;
    placement.streaming_peak_extent = streaming_peak.extent;
    // The load is over: its two buffers go, the resident arrays stay.
    for (std::size_t i = 0; i < 2; ++i) {
        if (held[i]) {
            streaming.release(placed[i].handle);
            held[i] = false;
        }
    }
    const ArenaStats resident = persistent.stats();
    placement.persistent_used_bytes = resident.used_bytes;
    placement.persistent_extent = resident.extent;
    placement.placed = true;
    return true;
}

TrackContactStatus query_track_contact_segment(
    const TrackContactWorld& world,
    const float from[3],
    const float to[3],
    TrackContact& contact,
    const TrackContactQueryLimits& limits) {
    if (!world.valid || from == nullptr || to == nullptr) {
        return TrackContactStatus::refused;
    }
    const TrackCollisionReport& report = world.collision;
    // A world assembled by hand rather than built: the table must be the
    // one build_track_contact_world derives, or an index below would read
    // past it.
    if (world.triangles.size() != report.polygons.size() * kTrianglesPerPolygon ||
        report.nodes.empty()) {
        return TrackContactStatus::refused;
    }
    if (!finite3(from) || !finite3(to) || !std::isfinite(limits.max_length) ||
        !(limits.max_length > 0.0F) || limits.max_cells == 0) {
        return TrackContactStatus::refused;
    }
    const float delta[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    const float length =
        std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    if (!std::isfinite(length) || !(length > 0.0F) || length > limits.max_length) {
        return TrackContactStatus::refused;
    }
    const float direction[3] = {delta[0] / length, delta[1] / length,
                                delta[2] / length};
    if (!finite3(direction)) {
        return TrackContactStatus::refused;
    }

    // The cells the segment's horizontal projection meets, and nothing its
    // bounding box reaches beyond them: outside means no cell lies under
    // any part of the segment.
    std::vector<std::size_t> cells;
    if (!segment_cells(report, footprint_of(from, to), limits.max_cells, cells)) {
        return TrackContactStatus::refused;
    }
    if (cells.empty()) {
        return TrackContactStatus::outside;
    }
    std::vector<std::uint32_t> named;
    for (std::size_t cell_index : cells) {
        const TrackCollisionCell& cell = report.cells[cell_index];
        if (static_cast<std::uint64_t>(cell.first_index) + cell.index_count >
            report.indices.size()) {
            return TrackContactStatus::refused;
        }
        for (std::uint32_t k = 0; k < cell.index_count; ++k) {
            const std::uint32_t polygon_index =
                report.indices[static_cast<std::size_t>(cell.first_index) + k];
            if (polygon_index >= report.polygons.size()) {
                return TrackContactStatus::refused;
            }
            named.push_back(polygon_index);
        }
    }

    // The nearest surface the segment meets, whichever test finds it (issue
    // #167). Both tests run over the triangles that are not degenerate. The
    // conservative test meets every closed triangle the segment meets, so its
    // nearest contact is never beyond the first surface the segment meets by
    // more than a rounding of the float distance. Issue #39's arithmetic is
    // kept wherever it names a nearest surface: its answer stands unless the
    // conservative test meets a triangle whose crossing is nearer, for
    // certain and by more than kNearerMargin, than where the segment crosses
    // the plane of the triangle #39 names - which happens where #39 rounded a
    // nearer surface away, such as a wall quad's own (0,2) diagonal in front
    // of a farther wall. Where #39 meets nothing, the conservative test
    // answers alone.
    const Sweep sweep = sweep_of(from, to);
    TrackContact best;
    float best_det = 0.0F;
    TrackContact nearest;
    Crossing nearest_crossing;
    const bool ray_met =
        nearest_moller_trumbore(world, named, from, direction, length, best, best_det);
    const bool conservative_met =
        nearest_conservative(world, named, sweep, length, nearest, nearest_crossing);
    if (!ray_met && !conservative_met) {
        return TrackContactStatus::clear;
    }
    bool replace = !ray_met;
    if (ray_met && conservative_met) {
        const Crossing named_by_ray =
            crossing_of(world, sweep, best.polygon, best.triangle);
        replace = named_by_ray.plane &&
                  nearest_crossing.along + nearest_crossing.error + kNearerMargin <
                      named_by_ray.along - named_by_ray.error;
    }
    if (replace) {
        best = nearest;
        // side < 0 means (to - from) . normal < 0, which is what a positive
        // determinant of the #39 test means.
        best_det = nearest_crossing.side < 0 ? 1.0F : -1.0F;
    }

    const TrackContactTriangle& triangle =
        world.triangles[best.polygon * kTrianglesPerPolygon + best.triangle];
    // det > 0 means direction . normal < 0: the stored normal already faces
    // the start. Otherwise the far side was hit and the normal is turned.
    const float sign = best_det > 0.0F ? 1.0F : -1.0F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        best.point[axis] = from[axis] + direction[axis] * best.distance;
        best.normal[axis] = sign * triangle.normal[axis];
    }
    best.kind = triangle.kind;
    best.up = triangle.up;
    contact = best;
    return TrackContactStatus::hit;
}

TrackContactStatus query_track_contact_point(
    const TrackContactWorld& world,
    const float point[3],
    float rise,
    float drop,
    TrackContact& contact,
    const TrackContactQueryLimits& limits) {
    if (point == nullptr || !finite3(point) || !std::isfinite(rise) ||
        !std::isfinite(drop) || rise < 0.0F || drop < 0.0F ||
        !(rise + drop > 0.0F)) {
        return TrackContactStatus::refused;
    }
    const float from[3] = {point[0], point[1], point[2] + rise};
    const float to[3] = {point[0], point[1], point[2] - drop};
    return query_track_contact_segment(world, from, to, contact, limits);
}

void track_slot_to_render(const float slot[3], float render[3]) noexcept {
    const float px = slot[0];
    const float pz = slot[1];
    const float py = slot[2];
    render[0] = px;
    render[1] = py;
    render[2] = -pz;
}

void track_render_to_slot(const float render[3], float slot[3]) noexcept {
    const float x = render[0];
    const float y = render[1];
    const float z = render[2];
    slot[0] = x;
    slot[1] = -z;
    slot[2] = y;
}

} // namespace nfsu
