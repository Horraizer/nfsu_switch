#pragma once

#include "nfsu/texture.h"
#include "nfsu/track.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// Bounded decoding of the static collision of one track, and bounded
// queries against it.
//
// docs/TRACK.md decodes the geometry a region draws and
// docs/TRACK_OBJECTS.md decodes where that geometry is placed. Neither
// touches collision, and neither container holds any: the five large
// STREAM*.BUN files carry only geometry, scenery, textures and animation.
// The collision of a track lives in its own file, Tracks/TRACKB<NNNN>.lzc,
// which is a JDLZ-compressed image of a flat chunk tree. Render geometry
// and collision are therefore separate by construction here: they are
// different chunk ids, in different files, decoded by different headers,
// and nothing in this one reads a vertex buffer or a texture.
//
// Nothing here scans a directory or follows a streaming table. A caller
// names one file; exactly that file is read, decompressed into a bounded
// buffer and parsed. See docs/TRACK_COLLISION.md for the byte evidence, the
// retail code each record size comes from, and what is still unknown.

// The container compression is JDLZ, the same one the texture packs use:
// nfsu::jdlz_decompress in nfsu/texture.h is the decoder and this header
// adds none of its own. load_track_collision below decompresses through it
// when the file carries a JDLZ header. A track image is three orders of
// magnitude larger than a texture blob and leans much harder on the
// near-distance run encoding, so tests/track_collision_tests.cpp round
// trips the stream shapes a track container actually uses.

// The two flag bytes a step reads before it runs, which is the most a
// decompressor that stops on a full output can leave behind. Each of the
// 31 retail tracks leaves one or two.
constexpr std::size_t kCollisionMaxUnusedPackedBytes = 2;

// ---------------------------------------------------------------------
// The collision chunks
// ---------------------------------------------------------------------

// speed.exe 0x00441380 is the loader: one function, five compares, each
// case writing a pointer and a count into the single global structure at
// 0x0077B130. Every record size below is one of that function's shifts, not
// a size inferred from the data.
//
// 0x004413A1: shr ecx, 3. The quadtree nodes.
constexpr std::uint32_t kCollisionTreeId = 0x00034130U;
// 0x004413CD: shr edx, 4. The leaf cells.
constexpr std::uint32_t kCollisionCellListId = 0x00034131U;
// 0x004413EB: shr ecx, 5. The collision polygons.
constexpr std::uint32_t kCollisionPolygonListId = 0x00034132U;
// 0x0044142C: shr ecx, 1. The index list the cells slice.
constexpr std::uint32_t kCollisionIndexListId = 0x00034133U;
// 0x00441446: two float triples read out of a 16-byte aligned record.
constexpr std::uint32_t kCollisionBoundsId = 0x00034134U;

// 0x0044138B and 0x004413B6 are lea reg, [chunk + 0x17] and 0x00441440
// is add ecx, 0x17 (83 C1 17), and each is followed by and reg,
// 0xFFFFFFF0, so the tree, the cells and the bounds start at the first
// 16-byte boundary at or after their payload. The polygons and the
// index list are taken at the payload itself (0x004413E2 and 0x00441423,
// lea reg, [chunk + 8]) and are never aligned.
constexpr std::uint64_t kCollisionRecordAlignment = 16;
// The filler an alignment skips, as in every other chunk of this format.
constexpr unsigned char kCollisionFillerByte = 0x11U;

constexpr std::size_t kCollisionTreeNodeSize = 8;
constexpr std::size_t kCollisionTreeNodeEntries = 4;
constexpr std::size_t kCollisionCellRecordSize = 16;
constexpr std::size_t kCollisionPolygonRecordSize = 32;
constexpr std::size_t kCollisionPolygonCorners = 4;
// A minimum float triple at +0 and a maximum at +16. The loader reads
// +0, +4, +8, +0x10, +0x14 and +0x18 and no further, so the last byte it
// touches is +0x1B and it proves only 28 bytes. 32 is what every retail
// payload holds after its alignment, and the parser requires that; the
// four bytes at +0x0C and at +0x1C are simply never read, which is not
// the same as being proven to be padding.
constexpr std::size_t kCollisionBoundsRecordSize = 32;
constexpr std::size_t kCollisionBoundsMaxOffset = 16;

// +6 of a polygon. 0x00441400 walks every record of the chunk immediately
// after loading it and 0x00441403 stores 0xFFFF into this halfword, so whatever the
// file holds there is scratch the runtime overwrites before anything reads
// it. The parser keeps the stored value and never uses it.
constexpr std::size_t kCollisionPolygonScratchOffset = 6;
constexpr std::uint16_t kCollisionPolygonScratchValue = 0xFFFFU;

// The three quantised coordinate groups, four int16 each: one value per
// corner. Slot order is the raw slot order of TrackVertex and of
// TrackInstance::position, so slot 1 is pz and slot 2 is py. Slot 2 is the
// vertical by measurement; slots 0 and 1 are px and pz by inference, from
// two range containments against the scenery instance translations and
// from the fact that all 290995 retail polygon centroids land on scenery
// under that reading and 150767 under the swapped one.
constexpr std::size_t kCollisionPolygonSlot0Offset = 8;
constexpr std::size_t kCollisionPolygonSlot1Offset = 16;
constexpr std::size_t kCollisionPolygonSlot2Offset = 24;

// The quantisation. The two horizontal slots are stored in eighths of a
// world unit and the vertical in 256ths, and the vertical carries a
// whole-unit base at record +4 on top of that. All three come from the
// retail transform at speed.exe 0x0043FD20, which shifts a horizontal
// slot left by 13 and shifts a vertical left by 8 after adding the base,
// then multiplies by the 1/65536 held at 0x006CC83C:
//
//     world slot 0 = slot0[i] / 8
//     world slot 1 = slot1[i] / 8
//     world slot 2 = vertical_base + slot2[i] / 256
//
// The stored 0x00034134 bounds agree with those divisors but cannot prove
// them alone: every one of the 42466 saturated wall records carries a zero
// base, so the stored vertical bound sits at the clamp whether the base is
// added or not, and both readings reproduce it on all 31 tracks. The code
// settles it; the bounds only corroborate.
constexpr float kCollisionHorizontalUnit = 1.0F / 8.0F;
constexpr float kCollisionVerticalUnit = 1.0F / 256.0F;
// The vertical is stored in a 16-bit field, so it saturates at 32512 rather
// than at 32767: every retail track's stored vertical bounds are exactly
// -127.0 and +127.0 and its vertical walls carry exactly those two values.
constexpr std::int32_t kCollisionVerticalSaturation = 32512;

// The quadtree. Its shape is not stored anywhere in the file; it is what
// reproduces the cell centres. Walking the nodes from node 0 over a square
// centred on the world origin with the half extent below, taking the four
// entry slots as the (-,-), (+,-), (-,+) and (+,+) quadrants of the two
// horizontal slots in that order, and treating the entries of the
// kCollisionTreeDepth-th level as cell indices, lands every leaf exactly on
// the centre its 0x00034131 record stores, for all 31 retail tracks and all
// 11898 of their cells, with no mismatch. None of the other 23 quadrant
// orders, no other depth from 1 to 14 and no other root half extent tried
// does; docs/TRACK_COLLISION.md carries that search. Entry 0 means empty at
// every level, and cell 0 is a sentinel no entry names.
constexpr std::uint32_t kCollisionTreeDepth = 10;
constexpr float kCollisionRootHalfExtent = 16384.0F;
// 2 * 16384 / 2^10. A leaf covers this square of the two horizontal slots
// and is unbounded vertically.
constexpr float kCollisionCellSize = 32.0F;

// One 32-byte record of 0x00034132: one collision polygon. Retail names
// these TrackPolygon<n> at 0x0046A4D9, where n is this record's position in
// the array, and wraps each in a TrackPolygonCollisionBody.
struct TrackCollisionPolygon {
    std::size_t index = 0;
    // Absolute offset of the record inside the decompressed image.
    std::uint64_t offset = 0;
    // +0 to +3, kept raw. +0 is zero in every retail record and is
    // runtime scratch like +6. +1 and +3 are read by 0x0043FF4D and
    // 0x0043FFDD, which is enough to know they are not padding; this
    // decoder does not claim what they hold. See docs/TRACK_COLLISION.md
    // for what an independent review measured about them and for the
    // issue that owns them.
    unsigned char unknown0[4] = {0, 0, 0, 0};
    // +4, the whole-unit part of this polygon's vertical. speed.exe
    // 0x0043FD20 is the routine that reads a record into world space, and
    // at 0x0043FD2E..0x0043FD3C it forms ((base << 8) + slot2) << 8 and
    // multiplies by 1/65536 at 0x0043FD6B, which is base + slot2 / 256.
    // It is non-zero in 240793 of the 290995 retail records and runs from
    // -27 to 94, so a decoder that drops it is wrong about the height of
    // five records in six.
    std::int16_t vertical_base = 0;
    // +6 as the file stores it. The loader overwrites it, so it carries no
    // meaning; it is kept only so that a caller can see what was there.
    std::uint16_t stored_scratch = 0;
    // The three quantised groups, exactly as stored.
    std::int16_t slot0[kCollisionPolygonCorners] = {0, 0, 0, 0};
    std::int16_t slot1[kCollisionPolygonCorners] = {0, 0, 0, 0};
    std::int16_t slot2[kCollisionPolygonCorners] = {0, 0, 0, 0};
    // The same four corners in world units, in the raw slot order
    // (slot 0, slot 1, slot 2). Corners are in perimeter order, with two
    // exact measurements behind that and one exception:
    //
    //   * taken as the cycle 0-1-2-3, 290983 of the 290995 retail
    //     polygons are convex in the horizontal projection. The twelve
    //     that are not are non-planar folds whose corners 1 and 3 share a
    //     horizontal point at two different heights, so their projection
    //     is a creased path and convexity is not defined for them;
    //     docs/TRACK_COLLISION.md names all twelve.
    //   * no other cyclic order is convex for any of the 156980 polygons
    //     that neither repeat an adjacent corner horizontally nor project
    //     to zero area. The remaining 134015 are convex in another order
    //     too, because a retraced triangle is convex whichever way round
    //     it is read, so they carry no evidence either way.
    //
    // 85710 records repeat an adjacent corner in all three slots, which
    // makes them triangles; 134003 do so in the horizontal projection
    // alone, and 48237 project to zero area, which is what a wall seen
    // from above is.
    float corner[kCollisionPolygonCorners][3] = {};
};

// One 16-byte record of 0x00034131: one leaf cell of the quadtree and the
// slice of the index list that names the polygons overlapping it.
struct TrackCollisionCell {
    std::size_t index = 0;
    std::uint64_t offset = 0;
    // +0 and +4. The centre of the cell in world units, slot 0 then
    // slot 1. Cell 0 is the sentinel: retail stores 999999.0 in both and
    // an empty slice, and no tree entry names it.
    float center[2] = {0.0F, 0.0F};
    // +8 and +12. A contiguous run of the 0x00034133 index list.
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

// One 8-byte record of 0x00034130: four quadtree entry slots. Node 0 is the
// root.
struct TrackCollisionTreeNode {
    std::size_t index = 0;
    std::uint64_t offset = 0;
    // Quadrant order is (-slot0, -slot1), (+slot0, -slot1), (-slot0,
    // +slot1), (+slot0, +slot1). A zero entry is empty. Above the leaf
    // level an entry is a node index; at the leaf level it is a cell index.
    std::uint16_t entries[kCollisionTreeNodeEntries] = {0, 0, 0, 0};
    // Filled by the parser's walk: the square this node covers, in world
    // units, slot 0 then slot 1.
    float center[2] = {0.0F, 0.0F};
    float half_extent = 0.0F;
    std::uint32_t depth = 0;
    bool reached = false;
};

// Deterministic parse cost of one collision image.
struct TrackCollisionCost {
    std::uint64_t region_bytes = 0;
    std::uint64_t packed_bytes = 0;
    // Packed bytes the decompressor did not read. nfsu::jdlz_decompress
    // stops as soon as the output is full, so the flag byte or two the
    // next step would have read is left over; retail's own loop is driven
    // by the packed size instead and reads them. Anything beyond
    // kCollisionMaxUnusedPackedBytes is data this decoder cannot account
    // for and is refused.
    std::uint64_t unused_packed_bytes = 0;
    std::uint64_t chunks = 0;
    std::uint64_t polygons = 0;
    std::uint64_t cells = 0;
    std::uint64_t indices = 0;
    std::uint64_t tree_nodes = 0;
    // Bytes as stored: polygons * 32, cells * 16, indices * 2, nodes * 8.
    std::uint64_t source_polygon_bytes = 0;
    std::uint64_t source_cell_bytes = 0;
    std::uint64_t source_index_bytes = 0;
    std::uint64_t source_tree_bytes = 0;
    // Cell/polygon pairs the index list spells out, and how many polygons
    // no cell names. The second is reported, never refused: a polygon that
    // no cell reaches is unreachable by any query but is not malformed.
    std::uint64_t cell_polygon_pairs = 0;
    std::uint64_t unreferenced_polygons = 0;
    // Polygons whose vertical group holds a saturated value, which is what
    // a wall that spans the whole vertical range looks like.
    std::uint64_t saturated_vertical_polygons = 0;
    // Polygons that repeat an adjacent corner and so describe a triangle.
    std::uint64_t degenerate_corner_polygons = 0;
    // The 0x00034134 record, and the axis-aligned box over every decoded
    // corner. The two agree exactly in every retail track.
    bool has_bounds = false;
    float stored_min[3] = {0.0F, 0.0F, 0.0F};
    float stored_max[3] = {0.0F, 0.0F, 0.0F};
    float corner_min[3] = {0.0F, 0.0F, 0.0F};
    float corner_max[3] = {0.0F, 0.0F, 0.0F};
};

struct TrackCollisionLimits {
    // Bytes the loader will read from disk for one .lzc, and bytes the
    // decompressed image is allowed to reach. The largest retail track is
    // just over one megabyte packed and just under four unpacked.
    std::uint64_t max_packed_bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t max_region_bytes = 256ULL * 1024ULL * 1024ULL;
    std::size_t max_chunks = 200000;
    // A 0x00034133 entry is a u16, so a longer polygon array holds records
    // no cell can name.
    std::uint64_t max_polygons = 65536;
    // A tree entry is a u16 too, and so is a cell index.
    std::uint64_t max_cells = 65536;
    std::uint64_t max_tree_nodes = 65536;
    std::uint64_t max_indices = 1ULL << 22;
};

// One decoded collision image.
struct TrackCollisionReport {
    // Size of the file the image came from; 0 for an in-memory parse.
    std::uint64_t file_size = 0;
    // The bytes that were parsed, as an offset and a length inside the
    // decompressed image.
    TrackRegionSelector region;
    std::vector<TrackCollisionPolygon> polygons;
    std::vector<TrackCollisionCell> cells;
    std::vector<std::uint32_t> indices;
    std::vector<TrackCollisionTreeNode> nodes;
    std::vector<TrackIssue> issues;
    TrackCollisionCost cost;
    // True only when all five chunks were found and every rule held. A
    // report that is not valid carries no query answer: the query entry
    // points below refuse it rather than searching a structure that failed
    // its own checks.
    bool valid = false;

    bool ok() const noexcept { return issues.empty(); }
};

// Parses one image. data must be the bytes of a decompressed collision
// container and base_offset the offset inside that image they start at,
// because the 16-byte alignment of three of the five chunks is relative to
// the start of the image. Never throws for malformed input: every problem
// is reported in TrackCollisionReport::issues.
//
// Top-level chunks that are not one of the five are counted and otherwise
// ignored, because a retail .lzc holds about forty of them and this decoder
// claims nothing about any of the others.
TrackCollisionReport parse_track_collision(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t base_offset,
    const TrackCollisionLimits& limits = TrackCollisionLimits{});

// Reads path read-only, decompresses it when it carries a JDLZ header and
// otherwise parses it as it stands, then parses the whole image. bytes
// receives the image. Nothing else on disk is read and nothing is written.
TrackCollisionReport load_track_collision(
    const std::filesystem::path& path,
    std::vector<unsigned char>& bytes,
    const TrackCollisionLimits& limits = TrackCollisionLimits{});

// ---------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------

// Every query below is bounded twice over: by the quadtree, which stops a
// search at the first empty quadrant, and by these caps, which bound the
// work and the answer even for a query box that covers the whole world.
//
// WHAT A QUERY ANSWERS, EXACTLY.
//
// A query walks the quadtree, takes the polygons the selected leaf cells
// name, and filters those. It therefore answers out of the game's own
// index, and the guarantee is a property of that index, not of the shapes
// alone. Stating it the other way round - "every polygon whose box
// overlaps" - is wrong, and measurably so: on Tracks/TRACKB1207.lzc the
// box (-336.875, -118.25, -1000) to (-325.625, -101.625, 1000) selects
// only cell 304 and returns only polygon 3429, while polygon 3428, whose
// own box is px -342.5..-273.875 and pz -123.875..-73.375, overlaps that
// query box and is not returned. It is named by cells 167, 168, 183, 299
// and 305 and not by 304, because its footprint is a triangle that does
// not reach into 304 at all. No overlapping shape was missed there; the
// promise was simply larger than the mechanism.
//
// What holds, for a report parsed from a retail image:
//
//   * SOUND. Every polygon returned really does have an axis-aligned box
//     overlapping the query volume. All 408569 polygon/cell pairs in the
//     31 retail tracks have the polygon overlapping the cell that names
//     it, tested exactly, so nothing returned is there by accident.
//   * COMPLETE FOR REAL OVERLAP. Every polygon whose footprint overlaps
//     the query box with positive area is returned. Three exact counts
//     over the corpus close that chain: of the 341498 polygon/square
//     pairs whose overlap encloses any area, 0 lie in a square the cell
//     list omits, and 0 are unnamed by their cell. So a real overlap is
//     always reachable through a cell the walk selects.
//   * NOT A SHAPE TEST. The answer may also contain polygons whose box
//     overlaps the query volume while the polygon itself does not, as
//     polygon 3429 may above. Treat it as a broad phase and do the exact
//     test yourself if you need one.
//
// What a negative answer means. An empty result, or a ray that reports no
// hit, means THE GAME'S INDEX PLACES NOTHING HERE. It does not mean the
// space is empty. Two gaps are measured rather than argued: 2075 of the
// 410644 polygon/cell contacts in the corpus are boundary touches that
// enclose no area and that the exporter named in some cases and not
// others, so a polygon meeting the query volume only along a cell edge
// may be reached through a neighbouring cell or not at all.
//
// And the guarantee is narrower than `valid`. Completeness above is a
// measured property of retail exporter output; parse_track_collision does
// not verify it and cannot. Rule 7 checks only the forward direction, that
// every polygon a cell names overlaps that cell. A hand-built image can
// satisfy all nine parser rules, report valid == true, and still answer
// both queries incompletely, because nothing requires a cell to name every
// polygon lying in it. For an image that did not come from the game, the
// SOUND bullet holds and the COMPLETE bullet does not.
struct TrackCollisionQueryLimits {
    // Leaf cells one query may visit.
    std::size_t max_cells = 65536;
    // Polygons one box query may return.
    std::size_t max_results = 1ULL << 20;
};

// Axis-aligned box overlap, broad phase. min and max are world units in
// the raw slot order (slot 0, slot 1, slot 2); min[i] must not exceed
// max[i]. Fills out, ascending and without repeats, with the polygons the
// selected leaf cells name whose own axis-aligned box also overlaps the
// query box. Read that as written, and see WHAT A QUERY ANSWERS above:
// this is not "every polygon whose box overlaps", it is "every such
// polygon the grid reaches". Every polygon whose footprint overlaps the
// box with positive area is in the answer; some whose box overlaps and
// whose footprint does not may be too.
//
// Returns false when the report is not valid, when the box is malformed,
// or when a cap above was reached. out is cleared first either way.
//
// The vertical extent of the box is used: the quadtree indexes the two
// horizontal slots only, so the vertical narrows the answer after the cells
// are chosen and never before.
bool query_track_collision_box(
    const TrackCollisionReport& report,
    const float min[3],
    const float max[3],
    std::vector<std::size_t>& out,
    const TrackCollisionQueryLimits& limits = TrackCollisionQueryLimits{});

// What a ray hit.
struct TrackCollisionHit {
    std::size_t polygon = 0;
    // Which of the polygon's two triangles, corners (0,1,2) then (0,2,3).
    std::size_t triangle = 0;
    // Distance along direction, in multiples of its length: a direction
    // that is not normalised gives a parametric t, not a length.
    float distance = 0.0F;
    // origin + distance * direction, in the same raw slot order.
    float point[3] = {0.0F, 0.0F, 0.0F};
};

// The nearest polygon a bounded ray meets, searched through the same grid
// and carrying the same guarantee and the same limits as the box query
// above: a hit is real, a miss means the index places nothing on the ray.
// origin and direction are world units in the raw slot order; the ray is
// origin + t * direction for t in [0, max_distance], so nothing behind the
// origin is ever hit. Each polygon is tested as the two triangles (0,1,2)
// and (0,2,3); the convex perimeter order makes that a valid
// triangulation. For the twelve retail polygons that are not convex it is
// the only one of the two diagonals that keeps any footprint at all, the
// other giving two triangles of zero horizontal area; see
// docs/TRACK_COLLISION.md. A degenerate triangle is skipped rather than
// hit.
//
// A polygon named by several of the selected cells is triangle-tested once
// per cell. That cannot change the answer, and de-duplicating would cost
// an allocation proportional to the polygon count on every query, which is
// the wrong trade for the small queries this is built for.
//
// Returns true and fills hit only when something was hit. A report that is
// not valid, a direction of zero length, a non-finite argument, a
// max_distance that is not positive and a search that reached a cap all
// return false; hit is left untouched.
bool query_track_collision_ray(
    const TrackCollisionReport& report,
    const float origin[3],
    const float direction[3],
    float max_distance,
    TrackCollisionHit& hit,
    const TrackCollisionQueryLimits& limits = TrackCollisionQueryLimits{});

// The leaf cells a world-space box of the two horizontal slots touches, in
// the order the quadtree walk finds them, without repeats. Cell 0 is never
// returned. This is what both queries above are built on, exposed because
// it is the part a caller may want to bound itself.
bool query_track_collision_cells(
    const TrackCollisionReport& report,
    float min_slot0,
    float min_slot1,
    float max_slot0,
    float max_slot1,
    std::vector<std::size_t>& out,
    const TrackCollisionQueryLimits& limits = TrackCollisionQueryLimits{});

} // namespace nfsu
