#pragma once

#include "nfsu/memory_arena.h"
#include "nfsu/track_collision.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// The static collision of a whole race track, loaded once, with bounded
// contact queries on top of it: issue #158, for the vehicle to track
// collision of issue #60.
//
// WHERE THE WHOLE CIRCUIT'S COLLISION IS. There is no per-section
// collision to assemble. speed.exe keeps exactly one static collision set
// resident: the chunk loader at 0x00441ED0 hands every one of the five
// collision chunks to 0x00441380, which stores a pointer and a count into
// the single global structure at 0x0077B130 - a second set would overwrite
// the first, not join it - and the game flow loads that set once per race,
// from TRACKS\TRACKB%d.LZC formatted with the current track number
// (0x004497BE, in GameFlowLoadingTrackSpecificPart1). The other files a
// race on track 1003 needs - STREAML1RA.BUN, the empty STREAML1RA_1003.BUN,
// TEX1003TRACK.BIN and GLOBALB.BUN - walk as complete chunk trees with no
// collision chunk at any depth. So the collision of
// the whole L1RA circuit of event 1 is the whole of Tracks/TRACKB1003.lzc,
// which docs/TRACK_COLLISION.md's decoder already parses; this header adds
// what a vehicle needs on top of that decode. The evidence, the coverage of
// the circuit's route and the totals are in docs/TRACK_COLLISION.md under
// "The whole circuit".
//
// UNITS AND FRAME. Every coordinate here is a world unit - the unit of the
// render geometry and of the scenery translations, about one metre
// (docs/TRACK_RENDER.md) - in the RAW SLOT ORDER of the collision records:
// component 0 is px, component 1 is pz, component 2 is py, the vertical,
// and a larger py is higher. Nothing here applies the renderer's map;
// track_slot_to_render below is that map, applied once at the boundary.
//
// NORMALS. A contact normal is unit length, in the same raw slot order,
// and faces the query: its dot product with the segment direction is
// negative, so it points back toward the segment's start. Collision
// polygons are two-sided, which is why a wall hit from either side reports
// the normal of the side it was hit from.
//
// ROAD OR WALL. A triangle is road when the vertical component of its unit
// normal, in magnitude, is at least kTrackContactRoadMinUp, and wall
// otherwise. That is this project's rule and not a recovered one: the
// retail surface type is not decoded (the polygon's +1..+3 bytes are still
// unknown), and the threshold is a compatibility approximation chosen where
// the retail data is sparse. docs/TRACK_COLLISION.md gives the measured
// distribution it was chosen from.

// A slope of 60 degrees from the horizontal. The retail triangles cluster at
// the two ends: over all 31 tracks, 394766 of the 496280 that are not
// degenerate have |normal.py| of 0.95 or more and 96808 have less than
// 0.05, while 909 lie between 0.3 and 0.7. Moving this anywhere in that
// band reclassifies at most those 909.
constexpr float kTrackContactRoadMinUp = 0.5F;

enum class TrackContactKind : std::uint8_t {
    road = 0,
    wall = 1,
};

const char* track_contact_kind_name(TrackContactKind kind) noexcept;

// One of the two triangles of a polygon, derived once at load: (0,1,2) is
// triangle 0 and (0,2,3) triangle 1, the split docs/TRACK_COLLISION.md
// justifies. The normal is (c1 - c0) x (c2 - c0) normalised, in raw slot
// order, with the orientation the corner order gives it; a query turns it to
// face the query. A triangle whose cross product is zero or not finite is
// degenerate: its normal is zero, it is counted, and no query ever hits it.
struct TrackContactTriangle {
    float normal[3] = {0.0F, 0.0F, 0.0F};
    // |normal[2]|, the cosine of the triangle's slope.
    float up = 0.0F;
    TrackContactKind kind = TrackContactKind::wall;
    bool degenerate = true;
};

struct TrackContactCounts {
    std::uint64_t road_triangles = 0;
    std::uint64_t wall_triangles = 0;
    std::uint64_t degenerate_triangles = 0;
};

// What a loaded world costs, in bytes. Every figure is a size times a
// sizeof, so it is the same on every 64-bit build and does not depend on
// how an allocator rounds a request.
struct TrackContactMemory {
    // Resident for as long as the world lives: the decoded arrays of the
    // collision report and the triangle table this layer adds. Five
    // allocations.
    std::uint64_t polygon_bytes = 0;
    std::uint64_t cell_bytes = 0;
    std::uint64_t index_bytes = 0;
    std::uint64_t node_bytes = 0;
    std::uint64_t triangle_bytes = 0;
    std::uint64_t resident_bytes = 0;
    std::uint64_t resident_allocations = 0;
    // The same five arrays as the file stores them, which is what retail
    // keeps resident: it points into the loaded image rather than decoding.
    std::uint64_t stored_bytes = 0;
    // Held only while loading, and released before the load returns: the
    // packed file and the decompressed image.
    std::uint64_t packed_bytes = 0;
    std::uint64_t image_bytes = 0;
    // The most held at once: both load buffers and every resident array,
    // because the report is built while the image is still alive.
    std::uint64_t load_peak_bytes = 0;
};

// One track's static collision, ready for contact queries.
struct TrackContactWorld {
    TrackCollisionReport collision;
    // Two per polygon, polygon-major: entry 2 * p + t is polygon p's
    // triangle t.
    std::vector<TrackContactTriangle> triangles;
    TrackContactCounts counts;
    TrackContactMemory memory;
    // True only when the collision report is valid and the triangle table
    // was derived from it. A world that is not valid answers no query.
    bool valid = false;
};

// Derives the triangle table from a parsed report and fills the counts and
// the resident figures. The load figures stay zero; load_track_contact_world
// fills them. A report that is not valid gives a world that is not valid.
TrackContactWorld build_track_contact_world(TrackCollisionReport collision);

// Reads one TRACKB file read-only, exactly as load_track_collision does -
// one file, decompressed into a bounded buffer that is released before this
// returns - and builds the world from it. Nothing else is read and nothing
// is written.
TrackContactWorld load_track_contact_world(
    const std::filesystem::path& path,
    const TrackCollisionLimits& limits = TrackCollisionLimits{});

// DERIVED: the resident bytes of the largest world the limits admit, every
// array at its limit. Saturates at UINT64_MAX rather than wrapping. It is
// the figure a budget has to hold for any collision load_track_contact_world
// can accept, as track_region_upload_ceiling is for a region's upload.
std::uint64_t track_contact_resident_ceiling(const TrackCollisionLimits& limits) noexcept;

// ---------------------------------------------------------------------
// The memory plan of issue #52
// ---------------------------------------------------------------------

// The collision is loaded once per race and lives until the race ends, which
// is the persistent category of docs/MEMORY_BUDGETS.md. Its load buffers are
// released when the load returns, which is not a frame, so they are placed
// in the streaming arena, whose released ranges are reused, rather than in
// the one-frame transient arena.
struct TrackContactPlacement {
    bool placed = false;
    // The refusal, and which of the seven requests it refused, when placed
    // is false. The part names are "polygons", "cells", "indices", "nodes",
    // "triangles", "packed file" and "image".
    ArenaRefusal refusal = ArenaRefusal::none;
    std::string refused_part;
    std::uint64_t persistent_capacity = 0;
    std::uint64_t persistent_used_bytes = 0;
    std::uint64_t persistent_extent = 0;
    std::uint64_t streaming_capacity = 0;
    // The streaming arena's peak over the load: both buffers at once.
    std::uint64_t streaming_peak_bytes = 0;
    std::uint64_t streaming_peak_extent = 0;
};

// The alignment every request below asks for. It is what the platform
// allocator guarantees for any object, the CPU arenas' own max_alignment in
// switch_memory_budget, and a request above an arena's max_alignment is
// refused rather than rounded down.
constexpr std::uint64_t kTrackContactAllocationAlignment = 16;

// Places the load buffers in the streaming arena, then the five resident
// arrays in the persistent arena, then releases the load buffers, exactly in
// the order a load holds them. On success the resident arrays stay placed in
// the plan and the load buffers do not. On a refusal every request this call
// placed is released again, so the plan is left as it was apart from its
// counters, and placement says which request was refused and why.
bool place_track_contact_memory(
    const TrackContactMemory& memory,
    MemoryPlan& plan,
    TrackContactPlacement& placement);

// ---------------------------------------------------------------------
// Contact queries
// ---------------------------------------------------------------------

enum class TrackContactStatus : std::uint8_t {
    // Something was met; the contact is filled.
    hit = 0,
    // The game's index covers the query and nothing on it is met: a wheel
    // over this is airborne, not out of bounds.
    clear = 1,
    // No leaf cell lies under any part of the query: the index places no
    // collision there at all. This is the out-of-bounds signal of issue #60.
    outside = 2,
    // The world is not valid, an argument is malformed or not finite, or a
    // cap was reached. A cap refuses rather than truncating, because a
    // truncated collision answer is a wrong one.
    refused = 3,
};

const char* track_contact_status_name(TrackContactStatus status) noexcept;

struct TrackContact {
    // The polygon's index in the collision report, which is n in the
    // TrackPolygon<n> retail names it by, and which of its two triangles.
    std::size_t polygon = 0;
    std::size_t triangle = 0;
    TrackContactKind kind = TrackContactKind::road;
    // World units from the segment's start to the contact point.
    float distance = 0.0F;
    // The contact point, raw slot order.
    float point[3] = {0.0F, 0.0F, 0.0F};
    // Unit length, raw slot order, facing the segment's start.
    float normal[3] = {0.0F, 0.0F, 0.0F};
    // |normal[2]|, the cosine of the slope met.
    float up = 0.0F;
};

struct TrackContactQueryLimits {
    // Leaf cells one query may visit. A query that would visit more is
    // refused.
    std::size_t max_cells = 16384;
    // The longest segment, in world units. Longer is refused, which bounds
    // the swept box and with it the walk.
    float max_length = 4096.0F;
};

// The first contact along the segment from `from` to `to`, both world units
// in raw slot order: the nearest triangle the game's index names under the
// segment's horizontal footprint that the segment crosses, road or wall. A
// tie is broken by the lower polygon index and then the lower triangle, so
// a query answers the same way on every run. A segment of zero length, or
// longer than limits.max_length, is refused.
//
// It answers out of the same index and carries the same guarantee as
// query_track_collision_ray (see WHAT A QUERY ANSWERS in track_collision.h):
// a contact is real; clear and outside mean the index places nothing there,
// which for a retail image misses nothing that crosses the segment with
// positive area. Because every cell a query visits lies under the query,
// a query inside one part of the track answers exactly as it would over a
// collision image holding only that part; tests/track_contact_tests.cpp
// holds that over synthetic multi-section images.
TrackContactStatus query_track_contact_segment(
    const TrackContactWorld& world,
    const float from[3],
    const float to[3],
    TrackContact& contact,
    const TrackContactQueryLimits& limits = TrackContactQueryLimits{});

// The surface under a point: the segment from `rise` world units above it
// straight down to `drop` world units below it, which is the column a wheel
// probes. rise and drop must be finite and not negative, and not both zero.
// The first contact from the top is returned, so under a bridge the deck is
// met before the road beneath it.
TrackContactStatus query_track_contact_point(
    const TrackContactWorld& world,
    const float point[3],
    float rise,
    float drop,
    TrackContact& contact,
    const TrackContactQueryLimits& limits = TrackContactQueryLimits{});

// ---------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------

// The renderer's world map, docs/FORMATS.md: x = px, y = py, z = -pz. It is
// a proper rotation (determinant +1), so a normal converts the same way and
// stays unit length. The two are exact inverses; neither rounds.
void track_slot_to_render(const float slot[3], float render[3]) noexcept;
void track_render_to_slot(const float render[3], float slot[3]) noexcept;

} // namespace nfsu
