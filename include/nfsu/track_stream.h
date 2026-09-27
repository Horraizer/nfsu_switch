#pragma once

#include "nfsu/gpu_upload.h"
#include "nfsu/memory_budget.h"
#include "nfsu/scene.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"
#include "nfsu/track.h"
#include "nfsu/track_draw_list.h"
#include "nfsu/track_objects.h"
#include "nfsu/track_zones.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// The whole track a race loads, rather than one bounded region of it.
//
// A track's TRACKB####.LZC carries the streaming table of its STREAM file:
// chunk 0x00034107, one 56-byte record per streaming section, naming the
// section, the file it lives in, the byte range it occupies there and the
// bytes it keeps once loaded, plus one byte per record that marks the
// sections this track keeps resident. speed.exe decides from that table,
// from two fixed lists and from the visibility zones which sections to load;
// docs/TRACK_WHOLE.md traces every step of that rule with its addresses.
//
// This layer decodes the table, applies the recovered rule to pick a set of
// sections, reads exactly those byte ranges and nothing else, parses each
// with the existing bounded parsers, and merges them into one geometry
// report and one scenery report that nfsu::build_track_scene places as one
// scene. It scans no directory and follows no path it was not given.

// ---------------------------------------------------------------------------
// The streaming table (0x00034107) and its file sizes (0x00034108)
// ---------------------------------------------------------------------------

constexpr std::uint32_t kTrackStreamTableId = 0x00034107U;
constexpr std::uint32_t kTrackStreamFileSizesId = 0x00034108U;

// speed.exe 0x004442BA..0x004442C7 divides the payload size by 56
// (0x24924925, then a shift of 5 after the divide-by-7 correction).
constexpr std::size_t kTrackStreamRecordSize = 56;

// Offsets inside one record.
constexpr std::size_t kTrackStreamNameOffset = 0;
constexpr std::size_t kTrackStreamNameSize = 8;
// s16 section number: 0x00444D30 compares it as a word against each
// requested number.
constexpr std::size_t kTrackStreamNumberOffset = 8;
// The track byte: 0x00444CB0 appends every record whose byte here is not
// zero to the list of sections to load.
constexpr std::size_t kTrackStreamTrackFlagOffset = 10;
// Written by the loader: +11 marks a record already requested (0x00444D51)
// and +12 is its load state (0x00444DFB, 0x004446FA, 0x00444747). Both are
// read before they are written, so the decoder requires them zero.
constexpr std::size_t kTrackStreamRequestedOffset = 11;
constexpr std::size_t kTrackStreamStateOffset = 12;
// u32 file index: 0x004446F0 selects the file name at 0x14 + 64 * index of
// the streamer; index 1 is TRACKS\STREAM%s.BUN (+0x54) and index 2 is
// TRACKS\STREAM%s_%d.BUN (+0x94), both copied there by 0x004445C0.
constexpr std::size_t kTrackStreamFileIndexOffset = 16;
// u32 byte offset and u32 byte count read (0x004446E6 and 0x004446E3).
constexpr std::size_t kTrackStreamDataOffsetOffset = 20;
constexpr std::size_t kTrackStreamSizeOffset = 24;
// u32 bytes the section keeps once loaded: 0x00444B2B sums it for the
// streaming pool. It is below the byte count exactly for the sections that
// carry a texture pack.
constexpr std::size_t kTrackStreamResidentSizeOffset = 28;
// u32 not read by any instruction traced here. It is close to, but not
// equal to, the byte sum of the section.
constexpr std::size_t kTrackStreamUnknownOffset = 32;
// +36 to +55 are written by the loader (the request frame at +36, the read
// request at +40, the memory at +44, two words at +48 and +52) and are zero
// in every retail record.
constexpr std::size_t kTrackStreamRuntimeOffset = 36;

constexpr std::size_t kTrackStreamFileCount = 3;
constexpr std::size_t kTrackStreamFileSizesSize = kTrackStreamFileCount * 4;
constexpr std::uint32_t kTrackStreamFileStream = 1;
constexpr std::uint32_t kTrackStreamFilePack = 2;

// The four section numbers 0x00444B6E..0x00444B83 always request, in that
// order: 2500, 2400, 2600 and 2499. 0x00444B0F..0x00444B24 counts the same
// four towards the streaming pool.
constexpr std::size_t kTrackStreamFixedCount = 4;
constexpr std::int16_t kTrackStreamFixedSections[kTrackStreamFixedCount] = {
    2500, 2400, 2600, 2499};

// 0x004499A0 add eax, 0x200000: the headroom the loader adds to the pool
// 0x00444AF0 computes.
constexpr std::uint64_t kTrackStreamPoolHeadroom = 0x200000;

// 0x00444D5B cmp ecx, 0x100: the loader stops requesting sections once 256
// are requested.
constexpr std::size_t kTrackStreamRequestLimit = 256;

// A section whose file offset is not a multiple of this is refused rather
// than parsed. That is this decoder's rule, not the game's: a
// section is parsed as its own buffer for its texture packs and at its
// absolute offset for its geometry, and the two agree only when the offset
// keeps every 128-byte alignment of the format. Every retail offset is a
// multiple of 2048.
constexpr std::uint64_t kTrackStreamSectionAlignment = 128;

struct TrackStreamRecord {
    std::size_t index = 0;
    // Absolute offset of the record inside the decompressed image.
    std::uint64_t offset = 0;
    std::string name;
    std::int16_t number = 0;
    std::uint8_t track_flag = 0;
    std::uint32_t file_index = 0;
    std::uint32_t data_offset = 0;
    std::uint32_t size = 0;
    std::uint32_t resident_size = 0;
    std::uint32_t unknown = 0;
};

struct TrackStreamLimits {
    std::uint64_t max_image_bytes = 256ULL * 1024ULL * 1024ULL;
    std::size_t max_chunks = 200000;
    std::size_t max_records = 65536;
};

struct TrackStreamTable {
    std::uint64_t image_size = 0;
    std::uint64_t table_offset = 0;
    std::uint64_t table_size = 0;
    std::vector<TrackStreamRecord> records;
    bool has_file_sizes = false;
    std::uint64_t file_sizes_offset = 0;
    std::uint32_t file_sizes[kTrackStreamFileCount] = {0, 0, 0};
    std::vector<TrackIssue> issues;
    bool valid = false;

    bool ok() const noexcept { return issues.empty(); }
};

// Parses the streaming table of one decompressed TRACKB image. The image
// must be a whole number of top-level chunks and hold exactly one
// 0x00034107, a non-zero whole number of 56-byte records, and at most one
// 0x00034108 of exactly 12 bytes. Refused, with the record's offset: a name
// that is empty, not printable ASCII or not NUL-terminated inside its eight
// bytes, a non-zero loader-written byte, a file index other than 0, 1 or 2,
// a resident size above the byte count, a byte range that passes the size
// 0x00034108 declares for its file, and a section number two records share
// (0x00444D30 would never reach the second). Never throws.
TrackStreamTable parse_track_stream_table(
    const unsigned char* data,
    std::size_t size,
    const TrackStreamLimits& limits = TrackStreamLimits{});

// The index of the first record numbered `number`, the one 0x00444D30
// reaches, or kTrackStreamNoRecord.
constexpr std::size_t kTrackStreamNoRecord = static_cast<std::size_t>(-1);
std::size_t find_track_stream_record(
    const TrackStreamTable& table, std::int32_t number) noexcept;

// The streaming pool speed.exe allocates for a track: 0x00444AF0 over the
// records whose track byte is set or whose number is one of the four fixed
// ones - the larger of the sum of their resident sizes and the largest
// byte count plus resident size of any one of them that keeps less than it
// reads - plus kTrackStreamPoolHeadroom (0x004499A0). Saturates.
std::uint64_t track_stream_pool_bytes(const TrackStreamTable& table) noexcept;

// ---------------------------------------------------------------------------
// Which sections a track loads
// ---------------------------------------------------------------------------

enum class TrackStreamSet {
    // The recovered rule with no position: the four fixed numbers, the
    // 0x00034154 list keyed on the track, and every record whose track byte
    // is set, requested in that order and resolved as 0x00444D0E does,
    // including its 256-request stop. This is what the loader requests
    // before a car has a zone, and docs/TRACK_WHOLE.md shows the zones along
    // the track's own route add nothing to it. The track byte counts only
    // until that first set is loaded: 0x0044533B then stores 1 at the
    // streamer's +0x178, and later requests leave the flagged records out,
    // so they stay resident only until a request needs their room.
    permanent,
    // NOT a retail rule: the permanent set plus every section any zone of
    // the zone file lists. It is the most a position anywhere on the map
    // could ask for, and it is a bound.
    zones,
    // NOT a retail rule: the permanent set plus every other record that
    // names the stream or the pack file, in table order. It is the whole
    // stream, and it is the outer bound.
    stream,
};

// "permanent", "zones" or "stream". Stable.
const char* track_stream_set_name(TrackStreamSet set) noexcept;

enum class TrackStreamReason {
    fixed,        // 0x00444B6E..0x00444B83
    extra_list,   // the 0x00034154 record keyed on the track, 0x00444BA4
    track_flag,   // the record's track byte, 0x00444CB0
    zone_list,    // a zone's own list; the zones set only
    stream_bound, // every remaining record; the stream set only
};

// "fixed", "extra-list", "track-flag", "zone-list" or "stream-bound".
const char* track_stream_reason_name(TrackStreamReason reason) noexcept;

struct TrackStreamRequest {
    std::int32_t number = 0;
    TrackStreamReason reason = TrackStreamReason::fixed;
    // The record the number resolves to, or kTrackStreamNoRecord.
    std::size_t record = kTrackStreamNoRecord;
    // False for a number with no record and for a record an earlier
    // request already selected (the +11 byte of 0x00444D4A).
    bool selected = false;
};

struct TrackStreamSelection {
    TrackStreamSet set = TrackStreamSet::permanent;
    std::uint32_t track = 0;
    // The 0x00034154 record the loader takes for this track: the first
    // whose key is the track number. extra_list is its position in
    // TrackZoneReport::section_lists, which holds the 0x00034153 records too.
    bool has_extra_list = false;
    std::size_t extra_list = 0;
    std::vector<TrackStreamRequest> requests;
    // Selected record indices, in the order they were first requested,
    // and why each one was.
    std::vector<std::size_t> records;
    std::vector<TrackStreamReason> reasons;
    std::uint64_t requests_without_record = 0;
    std::uint64_t repeated_requests = 0;
    // Records the stream set leaves out because their file index is 0: no
    // step of the loader can request them, because every such record is
    // number 0, and what file index 0 names was not traced.
    std::uint64_t excluded_records = 0;
    // The permanent set stopped at kTrackStreamRequestLimit.
    bool stopped_at_limit = false;

    // The route check: the 0x00034153 record keyed on the track lists the
    // zones 0x00444A28..0x00444A4F lets a car's look-ahead points enter.
    // These count the zones it names, the ones with no zone record, the
    // section numbers their lists name, those with a record, and those
    // with a record that this selection does not hold.
    bool has_route_list = false;
    std::uint64_t route_zones = 0;
    std::uint64_t route_zones_missing = 0;
    std::uint64_t route_numbers = 0;
    std::uint64_t route_numbers_with_record = 0;
    std::uint64_t route_numbers_outside = 0;
};

// Applies `set` for `track`. The zone report supplies the 0x00034154,
// 0x00034153 and 0x00034152 records; it must be valid. Fails, with `error`
// set, on a table or zone report that is not valid.
bool select_track_stream_sections(
    const TrackStreamTable& table,
    const TrackZoneReport& zones,
    std::uint32_t track,
    TrackStreamSet set,
    TrackStreamSelection& selection,
    std::string& error);

// Approximation for event 1: read the nonresident Y-numbered texture
// sections of STREAML1RA to resolve geometry hashes absent from the
// recovered permanent set. These are display sources only, never residency
// requests. The caller keeps them separate from simulation telemetry.
bool select_track_texture_spillover_1003(
    const TrackStreamTable& table,
    const std::vector<std::size_t>& resident,
    TrackStreamSelection& selection,
    std::string& error);

// ---------------------------------------------------------------------------
// Loading the selected sections
// ---------------------------------------------------------------------------

struct TrackStreamSection {
    std::size_t record = 0;
    TrackStreamReason reason = TrackStreamReason::fixed;
    std::string name;
    std::int32_t number = 0;
    std::uint32_t file_index = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t resident_size = 0;
    // Exactly the section's bytes. The texture packs below were parsed from
    // this buffer, so it must stay where it is.
    std::vector<unsigned char> bytes;
    bool loaded = false;
    // Top-level chunks of the section, by kind.
    std::uint64_t chunks = 0;
    std::uint64_t geometry_roots = 0;
    std::uint64_t scenery_sections = 0;
    std::uint64_t texture_packs = 0;
    std::uint64_t padding_chunks = 0;
    std::uint64_t other_chunks = 0;
    // Parsed only when the section holds a chunk of that kind. Geometry and
    // scenery offsets are absolute file offsets; pack offsets are relative
    // to `bytes`.
    TrackRegionReport geometry;
    TrackObjectReport objects;
    TexturePackSet packs;
    // Where this section's meshes and scenery sections landed in the
    // merged reports of TrackStreamLoad.
    std::size_t first_mesh = 0;
    std::size_t mesh_count = 0;
    std::size_t first_scenery = 0;
    std::size_t scenery_count = 0;
    // Read and walk problems of this section. Parser issues stay in
    // geometry, objects and packs.
    std::vector<TrackIssue> issues;
};

struct TrackStreamFiles {
    std::filesystem::path stream;
    bool has_pack = false;
    std::filesystem::path pack;
};

struct TrackStreamLoadLimits {
    // Optional service/measurement checkpoint; exceptions propagate to cancel.
    std::function<void()> poll;
    // Over every section read.
    std::uint64_t max_total_bytes = 1ULL << 30;
    TrackLimits geometry;
    TrackObjectLimits objects;
    TextureLimits textures;
};

struct TrackStreamLoad {
    std::uint64_t stream_file_size = 0;
    std::uint64_t pack_file_size = 0;
    std::uint64_t bytes_read = 0;
    // In selection order.
    std::vector<TrackStreamSection> sections;
    // Every loaded section's meshes and scenery sections, in selection
    // order, each renumbered by its position. Offsets stay absolute. The
    // costs are the sections' sums, and the distinct-hash counts are
    // recounted over the merge. A merged report covers no single byte
    // range: its selector's offset is 0 and its size the bytes of the
    // loaded sections.
    TrackRegionReport geometry;
    TrackObjectReport objects;
    // File-level problems: a file that cannot be sized or opened.
    std::vector<TrackIssue> issues;

    // True when no section, file or parser raised an issue.
    bool ok() const noexcept;
};

// Reads the byte range of every selected record out of its file - the
// stream for file index 1, `files.pack` for file index 2 - and parses it.
// Reads nothing else. A section whose range lies outside its file, whose
// offset is not a multiple of kTrackStreamSectionAlignment, or whose bytes
// are not a whole number of top-level chunks is reported and not parsed.
// Then merges the parsed sections into load.geometry and load.objects.
// Fails, with `error` set, only on a limit or an allocation failure.
bool load_track_stream_sections(
    const TrackStreamTable& table,
    const TrackStreamSelection& selection,
    const TrackStreamFiles& files,
    const TrackStreamLoadLimits& limits,
    TrackStreamLoad& load,
    std::string& error);

// The same, over byte images already in memory: images[i] stands for file
// index i and may be null. What load_track_stream_sections calls once it
// has the bytes, and what a test drives without a file.
bool load_track_stream_sections_from_memory(
    const TrackStreamTable& table,
    const TrackStreamSelection& selection,
    const std::vector<unsigned char>* const images[kTrackStreamFileCount],
    const TrackStreamLoadLimits& limits,
    TrackStreamLoad& load,
    std::string& error);

// One RenderTextureSource per loaded section that holds a texture pack, in
// selection order, each pointing into that section's bytes and packs and
// filtered by `wanted`. `owners[k]` is the index into load.sections of
// source k. The load must not move while the sources are used.
void track_stream_texture_sources(
    const TrackStreamLoad& load,
    const std::vector<std::uint32_t>* wanted,
    std::vector<RenderTextureSource>& sources,
    std::vector<std::size_t>& owners);

// ---------------------------------------------------------------------------
// What the whole track costs, and the budgets of issue #52
// ---------------------------------------------------------------------------

// Per-section counts after the merged scene is built.
struct TrackStreamSectionCost {
    std::uint64_t meshes = 0;
    std::uint64_t valid_meshes = 0;
    std::uint64_t empty_meshes = 0;
    std::uint64_t refused_meshes = 0;
    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    std::uint64_t scenery_sections = 0;
    std::uint64_t instances = 0;
    std::uint64_t placed_instances = 0;
    std::uint64_t pack_records = 0;
    std::uint64_t retained_textures = 0;
    std::uint64_t retained_texture_bytes = 0;
};

// Fills one cost per section of `load`, in its order. `source_owner` maps a
// texture source index to the index of the section that supplied it, or to
// kTrackStreamNoRecord for a source outside the stream, such as a
// --texture file. Fails on a scene whose instances name scenery the load
// did not supply.
bool track_stream_section_costs(
    const TrackStreamLoad& load,
    const PreparedTrackScene& scene,
    const std::vector<std::size_t>& source_owner,
    std::vector<TrackStreamSectionCost>& costs,
    std::string& error);

enum class TrackWholePlatform { desktop, switch_plan };

// "desktop" or "switch". Stable.
const char* track_whole_platform_name(TrackWholePlatform platform) noexcept;

struct TrackWholeBudgetRow {
    TrackWholePlatform platform = TrackWholePlatform::desktop;
    // A stable dotted name, e.g. "region.upload".
    std::string name;
    std::uint64_t value = 0;
    std::uint64_t ceiling = 0;
    bool within = true;
};

// What one prepared whole-track scene needs, set against the budgets of
// include/nfsu/memory_budget.h. Desktop rows compare against what the
// default one-region preparation accepts (TrackSceneLimits,
// RenderTextureLimits and TrackDrawListLimits at their defaults, and the
// derived ceilings over them); Switch rows compare against
// switch_memory_budget() and its placeholders. `gpu` is the scene's upload
// table placed by upload_table_requirement. The rows are in a fixed order.
void track_whole_budget_rows(
    const PreparedTrackScene& scene,
    std::uint64_t decoded_texture_bytes,
    std::uint64_t resident_bytes,
    const SceneMemoryRequirement& gpu,
    std::vector<TrackWholeBudgetRow>& rows);

} // namespace nfsu
