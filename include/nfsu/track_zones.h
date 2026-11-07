#pragma once

#include "nfsu/track.h"
#include "nfsu/track_visibility.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// The visibility zones of one track: the polygons that pick a zone from a
// camera position, and the section list each zone draws.
//
// docs/TRACK_VISIBILITY.md recovered the culling path and left one thing
// undecoded: the record layout inside the chunk 0x80034150, which is what
// turns "the camera is here" into "these sections are drawn". This header
// is that layout. Every record size and every field offset below comes from
// an instruction in speed.exe, not from a pattern in the data; the retail
// addresses are named on each one and docs/TRACK_ZONES.md carries the
// disassembly and the corpus measurements.
//
// The container is Tracks/TRACKB<NNNN>.lzc, the same JDLZ-compressed chunk
// image docs/TRACK_COLLISION.md reads static collision out of, and this
// header adds no decompressor of its own: load_track_zones below sniffs the
// JDLZ header and calls nfsu::jdlz_decompress, exactly as
// load_track_collision does.
//
// Nothing here scans a directory or follows a streaming table. A caller
// names one file; exactly that file is read and nothing is written.

// ---------------------------------------------------------------------
// The chunks
// ---------------------------------------------------------------------

// 0x00446AB4 `cmp dword ptr [eax], 0x80034150` in the reset callback, and
// 0x00446DB5 `cmp ecx, 0x80034150` in the loader that walks the children.
constexpr std::uint32_t kTrackZoneContainerId = 0x80034150U;

// The four child ids the loader at 0x00446DB0 compares, in the order it
// compares them, each with the record stride its own loop advances by.
//
// 0x00446DF2 `cmp eax, 0x34151`, stride 0x00446E5F `add ecx, 0x8c`.
constexpr std::uint32_t kTrackZonePolygonId = 0x00034151U;
// 0x00446E6D `cmp eax, 0x34152`, stride 0x00446EF3 `add ecx, 0x94`.
constexpr std::uint32_t kTrackZoneId = 0x00034152U;
// 0x00446F0C `cmp eax, 0x34153`, stride 0x00446F3D `add eax, 0x110`.
constexpr std::uint32_t kTrackZoneTrackListId = 0x00034153U;
// 0x00446F47 `cmp eax, 0x34154`, stride 0x00446F6D `add eax, 0x20`.
constexpr std::uint32_t kTrackZoneExtraListId = 0x00034154U;

constexpr std::size_t kTrackZonePolygonRecordSize = 140;
constexpr std::size_t kTrackZoneRecordSize = 148;
constexpr std::size_t kTrackZoneTrackListRecordSize = 272;
constexpr std::size_t kTrackZoneExtraListRecordSize = 32;

// Every record of all four chunks opens with two pointer fields the loader
// overwrites the moment it links the record into a list: 0x00446E43 and
// 0x00446E4B write +0 and 0x00446E48 writes +4. What the file holds there
// is therefore not read by anything, and the parser keeps it raw. In all
// 4278 retail records of all four chunks both words read 11.
constexpr std::size_t kTrackZoneLinkNextOffset = 0;
constexpr std::size_t kTrackZoneLinkPrevOffset = 4;
constexpr std::uint32_t kTrackZoneStoredLinkValue = 11;

// ---------------------------------------------------------------------
// 0x00034151: one zone polygon
// ---------------------------------------------------------------------

// +8, read as an int16 by 0x00446E20 `movsx eax, word ptr [ecx + 8]` to
// decide which of two lists the record joins, and again by 0x00446EB0 as
// the key a 0x00034152 record is matched against.
constexpr std::size_t kTrackZonePolygonKeyOffset = 8;
// +0xa, the vertex count: 0x00446FE6 `movsx ecx, word ptr [eax + 0xa]`.
constexpr std::size_t kTrackZonePolygonVertexCountOffset = 10;
// +0xc, the vertices: 0x00446FEA `add eax, 0xc`, strided 8 bytes by
// 0x005679E3 `lea edx, [eax*8]`.
constexpr std::size_t kTrackZonePolygonVertexOffset = 12;
constexpr std::size_t kTrackZoneVertexSize = 8;
// (140 - 12) / 8. The record has room for exactly this many and no
// instruction bounds the count, so the parser does: a count above this
// would read past the record. Retail counts run 4 to 16.
constexpr std::size_t kTrackZoneMaxVertices = 16;

// ---------------------------------------------------------------------
// 0x00034152: one zone
// ---------------------------------------------------------------------

// +8, an int16: 0x00446EB0 `movsx ebx, word ptr [ecx + 8]` reads it to find
// this zone's polygon, and 0x004464F5 `movsx eax, word ptr [ebp + 8]` reads
// it again as the identity of the zone the camera is in.
constexpr std::size_t kTrackZoneKeyOffset = 8;
// +0xa. No instruction reads it. Zero in all 2110 retail records.
constexpr std::size_t kTrackZoneUnknownOffset = 10;
// +0xc, where 0x00446EEC `mov dword ptr [ecx + 0xc], edx` writes the
// address of the matching 0x00034151 record, and 0x00446FE3
// `mov eax, dword ptr [esi + 0xc]` reads it back to test the polygon. It is
// a runtime pointer: zero in all 2110 retail records.
constexpr std::size_t kTrackZonePolygonLinkOffset = 12;
// +0x10, the section count: 0x0044649E `mov eax, dword ptr [ebp + 0x10]`.
constexpr std::size_t kTrackZoneSectionCountOffset = 16;
// +0x14, the int16 section numbers: 0x004464A7 `lea ecx, [ebp + 0x14]`.
constexpr std::size_t kTrackZoneSectionOffset = 20;
// 0x00446C10 `cmp edx, 0x40` refuses to append a 65th number, and
// (148 - 20) / 2 is the same 64. Retail counts run 4 to 59.
constexpr std::size_t kTrackZoneMaxSections = 64;

// ---------------------------------------------------------------------
// 0x00034153 and 0x00034154: two more section lists
// ---------------------------------------------------------------------

// Both are keyed and looked up the same way, by a whole dword at +8:
// 0x004446B0 `cmp dword ptr [ecx + 8], edx` for 0x00034153 and 0x00444BA4
// `cmp dword ptr [eax + 8], ecx` for 0x00034154. Neither is on the path a
// camera position takes, so neither changes a visible set here; they are
// decoded because they share the container and because leaving two of its
// four children unread would be the same gap this header exists to close.
constexpr std::size_t kTrackZoneListKeyOffset = 8;
// +0xc and +0x10. 0x00444BB4 `mov edx, dword ptr [eax + 0xc]` is the count
// and 0x00444BBF `lea esi, [eax + 0x10]` the array, copied as `edx` int16
// by the `rep movsd` / `rep movsw` pair at 0x00444BC6 and 0x00444BCD. That
// is the proof for 0x00034154. For 0x00034153 the same two offsets are the
// reading this header uses, from the shared record shape and from the data;
// the instruction that walks its array was not traced. See
// docs/TRACK_ZONES.md.
constexpr std::size_t kTrackZoneListCountOffset = 12;
constexpr std::size_t kTrackZoneListSectionOffset = 16;
// (272 - 16) / 2 and (32 - 16) / 2.
constexpr std::size_t kTrackZoneTrackListCapacity = 128;
constexpr std::size_t kTrackZoneExtraListCapacity = 8;

// ---------------------------------------------------------------------
// The draw buffer
// ---------------------------------------------------------------------

// 0x004465B0 reserves 0x100 bytes and 0x004465C1 `push 0x80` passes the cap
// in int16 entries, so at most this many section numbers reach the walker
// in one frame. It can bite under a zone: the 2600..2699 band alone is 100
// numbers and a zone list may add 59 more.
constexpr std::size_t kTrackZoneDrawBufferLimit = 128;

// ---------------------------------------------------------------------
// The polygon test
// ---------------------------------------------------------------------

// 0x005679B0, exactly. point and each vertex are two floats; both are in
// the file's raw slot order, so they are slot 0 and slot 1 of
// docs/FORMATS.md, which is px and pz. The retail caller
// (0x00446415 `add eax, 0x40`) hands it a three-float world point and the
// function reads only the first two (0x005679B3 and 0x005679B5).
//
// The rule, for i from 0 to count - 1 with j the previous index and
// count - 1 for i == 0:
//
//   * the edge is taken when (v[i].s1 <= p.s1 && p.s1 < v[j].s1) or
//     (v[j].s1 <= p.s1 && p.s1 < v[i].s1). Half open at both ends, so a
//     point level with a vertex crosses exactly one of the two edges that
//     meet there and a horizontal edge is never taken at all - which is
//     also why the division below never divides by zero;
//   * the crossing coordinate is
//     v[i].s0 + (p.s1 - v[i].s1) * (v[j].s0 - v[i].s0) / (v[j].s1 - v[i].s1),
//     in that association (0x00567A3B to 0x00567A4F);
//   * the parity flips when that coordinate is strictly greater than p.s0
//     (0x00567A52 `fcomp`, 0x00567A5B `jne`), so a point exactly on an edge
//     does not flip and is reported by whichever other edges cross.
//
// A count of zero or less returns false without reading a vertex
// (0x00567BC8 `cmp esi, ebx` / `jge`). The `cmp ebx, 4` at 0x005679C5 is
// the guard on a loop unrolled four ways, not a rejection: a triangle runs
// through the tail loop and is tested.
//
// Retail evaluates this in x87 80-bit registers and this function in
// float32. The two can differ for a point within rounding of an edge; no
// other difference is intended, and none of the comparisons above is
// widened or reordered.
bool point_in_track_zone_polygon(
    const float point[2],
    const float* vertices,
    std::size_t vertex_count) noexcept;

// ---------------------------------------------------------------------
// The decoded records
// ---------------------------------------------------------------------

// The value a resolved index carries when nothing was found.
constexpr std::size_t kTrackZoneNone = static_cast<std::size_t>(-1);

// One 140-byte record of 0x00034151.
struct TrackZonePolygon {
    std::size_t index = 0;
    // Absolute offset of the record inside the decompressed image.
    std::uint64_t offset = 0;
    // +0 and +4 as the file stores them. Overwritten by the loader.
    std::uint32_t stored_link[2] = {0, 0};
    // +8. A section number: retail sorts the record into one of two lists
    // on `number / 100` giving a letter in 'A'..'T' and `number % 100`
    // lying in 1..9 (0x00446E2E and 0x00446E36). All 2110 retail keys pass
    // that test, so all 2110 join the first list.
    std::int16_t key = 0;
    // +0xa.
    std::int16_t vertex_count = 0;
    // +0xc. All 16 slots, as stored: the ones at and past vertex_count are
    // kept rather than dropped, and are zero in every retail record.
    float vertices[kTrackZoneMaxVertices][2] = {};
};

// One 148-byte record of 0x00034152.
struct TrackZone {
    std::size_t index = 0;
    std::uint64_t offset = 0;
    std::uint32_t stored_link[2] = {0, 0};
    // +8, matched against TrackZonePolygon::key.
    std::int16_t key = 0;
    // +0xa, unread by the executable and kept raw.
    std::int16_t unknown = 0;
    // +0xc as the file stores it, which is the slot the loader writes the
    // polygon address into. Zero in every retail record.
    std::uint32_t stored_polygon_link = 0;
    // +0x10.
    std::int32_t section_count = 0;
    // +0x14. All 64 slots as stored; the ones at and past section_count are
    // zero in every retail record.
    std::int16_t sections[kTrackZoneMaxSections] = {};
    // The polygon whose key equals this record's, resolved by the parser
    // the way 0x00446EB9 resolves it. kTrackZoneNone when there is none, in
    // which case retail would dereference a null pointer at 0x00446FE6 and
    // this decoder raises an issue instead.
    std::size_t polygon_index = kTrackZoneNone;
};

// One record of 0x00034153 or of 0x00034154. The two differ only in their
// record size and so in how many numbers they can hold.
struct TrackZoneSectionList {
    std::uint32_t chunk_id = 0;
    std::size_t index = 0;
    std::uint64_t offset = 0;
    std::uint32_t stored_link[2] = {0, 0};
    // +8. The track number, which is the four digits of the containing
    // file's name in all 58 retail records of the two chunks.
    std::uint32_t key = 0;
    // +0xc.
    std::int32_t count = 0;
    // +0x10, every slot the record has room for.
    std::vector<std::int16_t> sections;
};

struct TrackZoneCost {
    std::uint64_t region_bytes = 0;
    std::uint64_t packed_bytes = 0;
    // Packed bytes nfsu::jdlz_decompress left unread, for the same reason
    // kCollisionMaxUnusedPackedBytes exists in nfsu/track_collision.h.
    std::uint64_t unused_packed_bytes = 0;
    // Top-level chunks of the whole image, and children of the container.
    std::uint64_t chunks = 0;
    std::uint64_t container_children = 0;
    std::uint64_t polygons = 0;
    std::uint64_t zones = 0;
    std::uint64_t polygon_vertices = 0;
    std::uint64_t zone_sections = 0;
    // Records of any of the four chunks whose two opening words are not
    // both kTrackZoneStoredLinkValue. Zero in all 4278 retail records.
    // Counted and never refused: nothing reads those words, so a different
    // value cannot make a report wrong. It says the image did not come from
    // the retail exporter, which is worth knowing and is not an error.
    std::uint64_t records_with_unexpected_link = 0;
    // The axis-aligned box over every stored vertex the counts reach, in
    // slot 0 and slot 1. Meaningful only when polygon_vertices is non-zero.
    float vertex_min[2] = {0.0F, 0.0F};
    float vertex_max[2] = {0.0F, 0.0F};
};

struct TrackZoneLimits {
    std::uint64_t max_packed_bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t max_region_bytes = 256ULL * 1024ULL * 1024ULL;
    std::size_t max_chunks = 200000;
    std::uint64_t max_polygons = 65536;
    std::uint64_t max_zones = 65536;
    std::uint64_t max_section_lists = 65536;
};

// One decoded 0x80034150.
struct TrackZoneReport {
    std::uint64_t file_size = 0;
    TrackRegionSelector region;
    std::vector<TrackZonePolygon> polygons;
    std::vector<TrackZone> zones;
    // 0x00034153 then 0x00034154, each in record order. A chunk with an
    // empty payload contributes nothing and is not an error: three retail
    // tracks carry one and one carries two.
    std::vector<TrackZoneSectionList> section_lists;
    std::vector<TrackIssue> issues;
    TrackZoneCost cost;
    // True when the container was found, every record parsed and every rule
    // below held. find_track_zone, track_zone_residency and
    // track_zone_draw_list all refuse a report that is not valid.
    //
    // This parser stops at the first problem, so valid and ok() cannot
    // disagree: every rule that raises an issue returns immediately and
    // leaves valid false. They are two names for one predicate here, and
    // ok() exists so that a caller reads the same member it reads on a
    // TrackObjectReport or a TrackCollisionReport.
    bool valid = false;

    bool ok() const noexcept { return issues.empty(); }
};

// Parses one decompressed image. data must be the whole image and
// base_offset the offset the bytes start at. Never throws for malformed
// input: every problem becomes an issue.
//
// The rules, each of which makes the report not valid:
//
//   1. the image walks as `(u32 id, u32 payload size)` chunks from
//      base_offset to the end, with no trailing bytes;
//   2. it holds exactly one top-level kTrackZoneContainerId, whose payload
//      walks as chunks the same way;
//   3. every child of the container is one of the four ids above, and no id
//      appears twice;
//   4. kTrackZonePolygonId and kTrackZoneId are both present, and each
//      payload is a whole number of its record size;
//   5. every polygon's vertex count is in 1..kTrackZoneMaxVertices, and
//      every zone's section count in 0..kTrackZoneMaxSections;
//   6. no two polygons share a key, so the lookup at 0x00446EB9 cannot be
//      ambiguous, and no two zones share one;
//   7. every zone's key names a polygon.
//
// Rule 6 is stated as the executable behaves, not as the data happens to
// look: retail takes the first match in list order and would silently use
// one of two records with the same key. A duplicate is refused here because
// the answer would then depend on a list order this decoder does not
// reproduce.
TrackZoneReport parse_track_zones(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t base_offset,
    const TrackZoneLimits& limits = TrackZoneLimits{});

// Reads path read-only, decompresses it when it carries a JDLZ header and
// otherwise parses it as it stands. bytes receives the image.
TrackZoneReport load_track_zones(
    const std::filesystem::path& path,
    std::vector<unsigned char>& bytes,
    const TrackZoneLimits& limits = TrackZoneLimits{});

// ---------------------------------------------------------------------
// Using a decoded zone set
// ---------------------------------------------------------------------

// 0x00446FD0. The first zone whose polygon holds the point, in record
// order, or kTrackZoneNone. point is slot 0 and slot 1, the same two floats
// point_in_track_zone_polygon reads.
//
// Retail scans a most-recently-used list and moves a hit to the front
// (0x0044700B), so its scan order changes as the camera moves and this one
// does not. That can only change the answer where two polygons hold the
// same point, and over all 31 retail tracks no sampled point lies in two:
// 251100 samples on a 90 by 90 grid of each track's own vertex box gave
// 212046 points in exactly one polygon and 39054 in none, with no point in
// two. See docs/TRACK_ZONES.md. It is a property of this data, not of the
// format, and a report whose polygons did overlap would answer differently
// from retail after the first hit.
std::size_t find_track_zone(
    const TrackZoneReport& report,
    const float point[2]) noexcept;

// The residency a decoded zone stands for, ready for cull_track_region:
// TrackResidencyRule::zone_list with this zone's own numbers, in its own
// order. Returns false and leaves out untouched for an invalid report or an
// index that is not a zone.
//
// A negative stored number is dropped, because 0x004464B3
// `test eax, eax` / `jl` drops it. Nothing else is filtered: the 2600..2699
// band cull_track_region adds is the band retail adds at 0x00446473, and
// track_section_always_resident already carries it.
bool track_zone_residency(
    const TrackZoneReport& report,
    std::size_t zone_index,
    TrackResidency& out);

// 0x00446400's zone arm, in full and in retail's order: every number from
// kTrackAlwaysResidentFirst to kTrackAlwaysResidentLast that is resident,
// ascending, then every non-negative number of the zone's list that is
// resident, in the list's order, stopping at cap entries.
//
// resident_numbers stands for the global section table at 0x0072A008: a
// number is resident when it appears there. Retail appends without checking
// for a repeat (0x00446491 and 0x004464C8 both just store and advance), so
// a zone list naming a number in the 2600..2699 band would put it in the
// answer twice. No retail zone list does - none of the 49964 numbers in the
// corpus is in that band - but the behaviour is reproduced rather than
// tidied, because the walker at 0x004465E0 would draw it twice.
std::vector<std::uint32_t> track_zone_draw_list(
    const TrackZoneReport& report,
    std::size_t zone_index,
    const std::vector<std::uint32_t>& resident_numbers,
    std::size_t cap = kTrackZoneDrawBufferLimit);

// 0x00446400's other arm, for a camera inside no polygon: every loaded
// section whose number modulo 100 is below kTrackNoZoneSubIndexLimit, in
// the order the loaded list holds them, stopping at cap entries. Retail
// walks its own list of loaded sections at 0x0077A978 rather than the
// number table, so the caller supplies that order here.
std::vector<std::uint32_t> track_no_zone_draw_list(
    const std::vector<std::uint32_t>& loaded_numbers,
    std::size_t cap = kTrackZoneDrawBufferLimit);

// The letter range 0x00446BF0 `cmp al, 0x41` and 0x00446BF4 `cmp al, 0x54`
// bound the two runtime list operations to, as a pair of section numbers.
// It is not a filter on what is drawn: the 2600..2699 band is letter 'Z'
// and is drawn under every zone. nfsu::track_section_letter in
// nfsu/track_objects.h is the letter itself.
constexpr std::uint32_t kTrackZoneEditLetterFirst = 0x41U; // 'A'
constexpr std::uint32_t kTrackZoneEditLetterLast = 0x54U;  // 'T'

} // namespace nfsu
