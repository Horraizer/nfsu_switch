#include "nfsu/track_contact.h"

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
// so that wherever that query meets something a segment down the same
// column gives the same polygon, triangle, distance and point. It also hands
// back the determinant, whose sign says which side was hit:
// det = edge1 . (direction x edge2) = -(direction . (edge1 x edge2)).
//
// Its bounds are strict and its barycentrics are rounded, so it is not
// watertight: a point exactly on an edge two triangles share can round
// outside both, and the review of issue #158 found road seams on retail
// data that it misses from either side. watertight_triangle below closes
// them.
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

// The frame of the watertight test of Woop, Benthin and Wald (2013): kz is
// the axis the direction is longest along, kx and ky the other two in an
// order that keeps the frame's handedness, and the shear maps the direction
// onto kz.
struct Shear {
    std::size_t kx = 0;
    std::size_t ky = 1;
    std::size_t kz = 2;
    float sx = 0.0F;
    float sy = 0.0F;
    float sz = 1.0F;
};

Shear shear_for(const float direction[3]) {
    Shear shear;
    std::size_t kz = 0;
    for (std::size_t axis = 1; axis < 3; ++axis) {
        if (std::fabs(direction[axis]) > std::fabs(direction[kz])) {
            kz = axis;
        }
    }
    std::size_t kx = (kz + 1) % 3;
    std::size_t ky = (kx + 1) % 3;
    if (direction[kz] < 0.0F) {
        std::swap(kx, ky);
    }
    shear.kx = kx;
    shear.ky = ky;
    shear.kz = kz;
    shear.sx = direction[kx] / direction[kz];
    shear.sy = direction[ky] / direction[kz];
    shear.sz = 1.0F / direction[kz];
    return shear;
}

// A watertight segment/triangle test. Each corner is moved to the segment's
// start and sheared into the frame above in float, by the same operations
// whichever triangle it belongs to, so a corner two triangles share lands
// on the same two floats in both. An edge function over two such corners is
// then the difference of two products that are the same two products, in
// the other order, in the triangle across the edge: it comes out with the
// same value and the opposite sign in each. A point exactly on a shared edge
// therefore gives zero in both and is met by both, and a point beside it is
// met by the one it lies in, never by neither. The bounds are closed, so a
// triangle's own outer edge is met too. The products are taken in double,
// where a product of two floats is exact and the difference is rounded once,
// so each edge function also has the sign of the exact value: a point off
// an outer edge by less than a float rounding is not taken as on it.
//
// The determinant is the sum of the edge functions, which is
// -(direction . (edge1 x edge2)) scaled by 1 / |direction[kz]|, so its sign
// means what segment_triangle's does. A segment edge-on to the triangle
// gives zero and is not a contact, as there.
bool watertight_triangle(
    const float origin[3],
    const Shear& shear,
    const float a[3],
    const float b[3],
    const float c[3],
    float max_distance,
    float& distance,
    float& determinant) {
    const float* corners[3] = {a, b, c};
    float x[3];
    float y[3];
    float z[3];
    for (std::size_t i = 0; i < 3; ++i) {
        const float relative[3] = {corners[i][0] - origin[0],
                                   corners[i][1] - origin[1],
                                   corners[i][2] - origin[2]};
        x[i] = relative[shear.kx] - shear.sx * relative[shear.kz];
        y[i] = relative[shear.ky] - shear.sy * relative[shear.kz];
        z[i] = shear.sz * relative[shear.kz];
    }
    // The edge opposite each corner: u over (b, c), v over (c, a), w over
    // (a, b).
    const double u = static_cast<double>(x[2]) * static_cast<double>(y[1]) -
                     static_cast<double>(y[2]) * static_cast<double>(x[1]);
    const double v = static_cast<double>(x[0]) * static_cast<double>(y[2]) -
                     static_cast<double>(y[0]) * static_cast<double>(x[2]);
    const double w = static_cast<double>(x[1]) * static_cast<double>(y[0]) -
                     static_cast<double>(y[1]) * static_cast<double>(x[0]);
    if ((u < 0.0 || v < 0.0 || w < 0.0) && (u > 0.0 || v > 0.0 || w > 0.0)) {
        return false;
    }
    const double det = u + v + w;
    if (!(std::fabs(det) > 0.0)) {
        return false;
    }
    const double t = (u * static_cast<double>(z[0]) + v * static_cast<double>(z[1]) +
                      w * static_cast<double>(z[2])) /
                     det;
    if (!std::isfinite(t) || t < 0.0) {
        return false;
    }
    const float along = static_cast<float>(t);
    if (!std::isfinite(along) || along > max_distance) {
        return false;
    }
    distance = along;
    determinant = det > 0.0 ? 1.0F : -1.0F;
    return true;
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

enum class TriangleTest : std::uint8_t {
    moller_trumbore = 0,
    watertight = 1,
};

// The nearest crossing one test accepts among the named polygons, ordered by
// better(); a polygon named by several cells is simply tested again and
// loses the tie to itself. best.distance starts at the segment's length, so
// a crossing exactly at the segment's end is still found.
bool nearest_crossing(const TrackContactWorld& world,
                      const std::vector<std::uint32_t>& named,
                      const float from[3],
                      const float direction[3],
                      float length,
                      TriangleTest test,
                      TrackContact& best,
                      float& best_det) {
    const Shear shear = shear_for(direction);
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
            const bool met =
                test == TriangleTest::moller_trumbore
                    ? segment_triangle(from, direction, a, b, c, best.distance,
                                       distance, det)
                    : watertight_triangle(from, shear, a, b, c, best.distance,
                                          distance, det);
            if (!met) {
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

    // Issue #39's arithmetic decides first, so that wherever its ray meets
    // something this answers with the same polygon, triangle, distance and
    // point. Only when it meets nothing anywhere along the segment does the
    // watertight test decide, and what it adds lies on a triangle's edge:
    // a seam between two polygons that rounding put outside both, or a
    // polygon's outer edge.
    TrackContact best;
    float best_det = 0.0F;
    if (!nearest_crossing(world, named, from, direction, length,
                          TriangleTest::moller_trumbore, best, best_det) &&
        !nearest_crossing(world, named, from, direction, length,
                          TriangleTest::watertight, best, best_det)) {
        return TrackContactStatus::clear;
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
