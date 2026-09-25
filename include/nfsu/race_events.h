#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// The race events of Underground mode, and the track table they point into.
//
// Two tables in two files. The track table is chunk 0x00034201 of
// GLOBAL/GLOBALB.LZC, one record per track number (1001, 1102, 1210, ...):
// the stream region the track lives in and the number every per-track file
// name is built from. The event table is chunk 0x00034A02 inside the
// container 0x80034A00 of FRONTEND/FRONTB.LZC, one record per career event:
// its event number, its race type, and one to eight races, each a track
// number, a lap count and a direction.
//
// Every record size and field offset below is named with the speed.exe
// instruction that reads it. docs/RACE_EVENTS.md carries the disassembly,
// the retail measurements that corroborate each field, and what is still
// unknown. Nothing here scans a directory: a caller names the two files and
// exactly those are read, and nothing is written.

// ---------------------------------------------------------------------
// The track table: GLOBAL/GLOBALB.LZC, chunk 0x00034201
// ---------------------------------------------------------------------

// 0x00442044 `cmp dword ptr [eax], 0x34201` in the loader, which keeps the
// payload pointer at 0x00736168 and payload / 0xFC at 0x0073616C
// (0x00442058..0x00442068, a division by 252 through a magic multiply).
constexpr std::uint32_t kTrackInfoChunkId = 0x00034201U;
// 0x0044850C and 0x00441FFB `add ecx, 0xfc` in the two lookups by number.
constexpr std::size_t kTrackInfoRecordSize = 0xFC;
// +0x40, the region name: "TRACKS\STREAM%s.BUN" (0x006C9B88) is formatted
// with the current record + 0x40 at 0x004496FD..0x0044970D, and
// "TRACKS\STREAM%s_%d.BUN" (0x006C9B70) with the same string at
// 0x00449728..0x0044972C. The next string field starts at +0x48.
constexpr std::size_t kTrackInfoRegionOffset = 0x40;
constexpr std::size_t kTrackInfoRegionSize = 8;
// +0x69, a signed byte 0x004ABC62 copies to 0x0078A318. When it is non-zero
// 0x00439408 skips the comparison against the race's lap count 0x0078A31C
// at 0x0043941A. It is 1 on the fourteen retail sprint and drag tracks and 0
// on the other seventeen. Only that one reader is traced, so the byte is
// named for what it does there and not for what it might mean.
constexpr std::size_t kTrackInfoLapSkipOffset = 0x69;
// +0x6C, the track number and the lookup key: 0x00448503 `movsx edi, word
// ptr [ecx + 0x6c]`, compared with the wanted number at 0x00448507. The same
// word is the %d of "TRACKS\STREAM%s_%d.BUN" (0x00449723) and becomes the
// current track number 0x0078A2F0 when a career race starts (0x004ABC58).
constexpr std::size_t kTrackInfoNumberOffset = 0x6C;
// +0x6E, a second 16-bit number. It equals +0x6C in all 31 retail records
// and no reader of it has been traced; it is kept raw.
constexpr std::size_t kTrackInfoNumberCopyOffset = 0x6E;
// +0x74, the quick-race mode mask. 0x004AB49F switches on a quick-race mode
// 0..6 and tests one bit each (table 0x004AB4EC: 0x01, 0x04, 0x40, 0x10,
// 0x20, 0x02, 0x08), and 0x004BA204 leaves a track with bit 0x08 out of the
// reverse-direction pass of the quick-race track list. Which mode is which
// is not decoded; the mask is reported raw.
constexpr std::size_t kTrackInfoModeMaskOffset = 0x74;

// One decoded track record. Only fields with a traced reader are decoded.
struct TrackInfoRecord {
    std::size_t index = 0;
    // Absolute offset of the record in the parsed image.
    std::uint64_t offset = 0;
    std::string region;
    std::int8_t lap_skip = 0;
    std::int16_t number = 0;
    std::int16_t number_copy = 0;
    std::uint32_t mode_mask = 0;
};

// ---------------------------------------------------------------------
// The event table: FRONTEND/FRONTB.LZC, chunk 0x00034A02 in 0x80034A00
// ---------------------------------------------------------------------

// 0x005A00A4 `cmp dword ptr [eax], 0x80034a00`: the loader walks this
// container's children and dispatches on their ids.
constexpr std::uint32_t kRaceEventContainerId = 0x80034A00U;
// 0x005A00C2 `sub eax, 0x34a01` and 0x005A00C9 `dec eax`: the child
// 0x00034A02 is handed to 0x005A0060 with its payload and size.
constexpr std::uint32_t kRaceEventChunkId = 0x00034A02U;
// 0x005A0060 divides the payload size by 332 (magic 0x3159721F, shift 38)
// and 0x005A0095 `add eax, 0x14c` steps the records.
constexpr std::size_t kRaceEventRecordSize = 0x14C;
// 0x005A0086 `cmp ecx, 0x100`: the record's +0 indexes a 256-entry pointer
// table at 0x00733FF8, and a record whose number is not below 256 is never
// entered into it.
constexpr std::uint32_t kRaceEventNumberLimit = 0x100;

// +0x00, the event number: the key of that pointer table (0x005A0084,
// 0x005A008E), and the %d of "Event %d" on the brief screen
// (0x004C5A3D..0x004C5A47 read it straight out of the record).
constexpr std::size_t kRaceEventNumberOffset = 0x00;
// +0x04, the event kind. 0x004C4924 reads it and 0x004C492C..0x004C4934
// append "_TOURNAMENT" to the mode icon name when it is 1; 0x005A1015
// refuses to report an event available when it is 8. No other value's
// meaning is recovered.
constexpr std::size_t kRaceEventKindOffset = 0x04;
constexpr std::uint32_t kRaceEventKindTournament = 1;
constexpr std::uint32_t kRaceEventKindNeverAvailable = 8;
// +0x08, the race type: 0x004C4939 `mov eax, dword ptr [eax + 8]`, then
// `cmp eax, 5` and the jump table at 0x004C49D4, which names the mode icon
// CIRCUIT, DRAG, SPRINT, DRIFT, TIME_TRIAL and LAP_KNOCKOUT for 0..5.
constexpr std::size_t kRaceEventTypeOffset = 0x08;
// +0x78, eight race slots of four bytes: 0x005A2454 `lea esi, [ebp +
// 0x7a]`, 0x005A2463 `movsx ecx, word ptr [esi - 2]` (the track number),
// 0x005A2460 `movsx eax, byte ptr [esi]` (+2, the lap count), 0x005A2467
// `movsx edx, byte ptr [esi + 1]` (+3, the direction), and 0x005A250B
// `add esi, 4`. The race list the slots are appended to holds at most
// eight entries (0x005A2485 `cmp eax, 8`), and the count follows at +0x98.
constexpr std::size_t kRaceEventRaceOffset = 0x78;
constexpr std::size_t kRaceEventRaceStride = 4;
constexpr std::size_t kRaceEventRaceSlots = 8;
constexpr std::size_t kRaceSlotTrackOffset = 0;
constexpr std::size_t kRaceSlotLapsOffset = 2;
constexpr std::size_t kRaceSlotDirectionOffset = 3;
// +0x98, how many race slots are used: 0x005A2444 and 0x005A2504.
constexpr std::size_t kRaceEventRaceCountOffset = 0x98;
// +0x9C, six 16-bit event numbers that must be won first: 0x005A1038 `lea
// edi, [ecx + 0x9c]`, 0x005A1040 `movsx ecx, word ptr [edi]` and
// 0x005A107D `add edi, 2`. The mode word follows at +0xA8.
constexpr std::size_t kRaceEventPrerequisiteOffset = 0x9C;
constexpr std::size_t kRaceEventPrerequisiteSlots = 6;
// +0xA8, how the prerequisites combine: 0x005A101F reads it, 0 takes every
// listed event (0x005A1029 falls through to the all-of loop), 1 takes any
// of them (0x005A108A), and any other value makes the event unavailable
// (0x005A108E).
constexpr std::size_t kRaceEventPrerequisiteModeOffset = 0xA8;
constexpr std::uint16_t kPrerequisitesAllOf = 0;
constexpr std::uint16_t kPrerequisitesAnyOf = 1;
// +0xAA, how many prerequisite slots are used: 0x005A102B `movsx eax, word
// ptr [ecx + 0xaa]`.
constexpr std::size_t kRaceEventPrerequisiteCountOffset = 0xAA;

// The race type names, in the order of the jump table at 0x004C49D4.
enum class RaceType : std::uint32_t {
    circuit = 0,
    drag = 1,
    sprint = 2,
    drift = 3,
    time_trial = 4,
    lap_knockout = 5,
};

// "circuit", "drag", "sprint", "drift", "time-trial", "lap-knockout", or
// "unknown" for any other value.
const char* race_type_name(std::uint32_t type) noexcept;

// "forward" for 0, "reverse" for 1, "unknown" otherwise. docs/RACE_EVENTS.md
// says what the reverse reading rests on.
const char* race_direction_name(std::int8_t direction) noexcept;

struct RaceSlot {
    // Absolute offset of the slot, which is also the offset of its track
    // number; the lap count is at +2 and the direction at +3.
    std::uint64_t offset = 0;
    std::int16_t track = 0;
    std::int8_t laps = 0;
    std::int8_t direction = 0;
};

struct RaceEventRecord {
    // Position of the record in the chunk, which is not its event number.
    std::size_t position = 0;
    std::uint64_t offset = 0;
    std::uint32_t number = 0;
    std::uint32_t kind = 0;
    std::uint32_t type = 0;
    std::uint32_t race_count = 0;
    // The used slots only, race_count of them on a valid table.
    std::vector<RaceSlot> races;
    std::uint16_t prerequisite_mode = 0;
    std::int16_t prerequisite_count = 0;
    // The used prerequisite slots only.
    std::vector<std::int16_t> prerequisites;
};

// ---------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------

struct RaceTableIssue {
    std::uint64_t offset = 0;
    std::string message;
};

struct RaceTableLimits {
    // The largest file either loader reads, as stored.
    std::uint64_t max_file_size = 256ULL * 1024ULL * 1024ULL;
    // The largest image either parser accepts, after JDLZ.
    std::uint64_t max_image_size = 256ULL * 1024ULL * 1024ULL;
    std::size_t max_top_level_chunks = 1U << 20;
};

struct TrackInfoTable {
    // Bytes of the parsed image, and of the file as stored when a loader
    // read it (0 otherwise).
    std::uint64_t image_size = 0;
    std::uint64_t stored_size = 0;
    bool jdlz = false;
    // Absolute offsets of the chunk header and of record 0.
    std::uint64_t chunk_offset = 0;
    std::uint64_t record_offset = 0;
    std::vector<TrackInfoRecord> records;
    std::vector<RaceTableIssue> issues;
    // True only when the whole table was read and every check passed.
    bool valid = false;

    bool ok() const noexcept { return valid && issues.empty(); }
};

struct RaceEventTable {
    std::uint64_t image_size = 0;
    std::uint64_t stored_size = 0;
    bool jdlz = false;
    std::uint64_t container_offset = 0;
    std::uint64_t chunk_offset = 0;
    std::uint64_t record_offset = 0;
    std::vector<RaceEventRecord> records;
    std::vector<RaceTableIssue> issues;
    bool valid = false;

    bool ok() const noexcept { return valid && issues.empty(); }
};

// Parses the track table of an in-memory, already decompressed image such
// as GLOBALB.BUN. Never throws for malformed input. Rules: the image is at
// most limits.max_image_size bytes; its top-level chunks tile it exactly;
// exactly one of them is 0x00034201; its payload is a non-zero whole number
// of 252-byte records (the game floors the division and would ignore a
// remainder, so a remainder is refused rather than guessed about); every
// region is NUL-terminated inside its 8 bytes, non-empty and printable
// ASCII, because it becomes part of a file name; every track number is
// positive; and no two records share a number, because the game's linear
// lookup would never reach the second.
TrackInfoTable parse_track_info_table(
    const unsigned char* data,
    std::size_t size,
    const RaceTableLimits& limits = RaceTableLimits{});

// Parses the event table of an in-memory, already decompressed image such as
// FrontB.lzc after JDLZ. Never throws for malformed input. Rules: the size
// limit; top-level chunks tile the image; exactly one top-level 0x80034A00;
// its children tile its payload; exactly one child 0x00034A02, whose payload
// is a non-zero whole number of 332-byte records. Each record: its number
// below 256 and not shared with another record (the pointer table would
// drop one of them); a race count of 1..8; every used race slot a positive
// track number and a lap count of at least 1; a prerequisite mode of 0 or
// 1; a prerequisite count of 0..6; and every listed prerequisite the number
// of a record in this table, because the availability test reads the
// prerequisite's record without checking it exists (0x005A1044..0x005A1060).
// An unused race or prerequisite slot is not read by the game and is not
// checked.
RaceEventTable parse_race_event_table(
    const unsigned char* data,
    std::size_t size,
    const RaceTableLimits& limits = RaceTableLimits{});

// Read the file read-only, decompress it when it starts with a JDLZ header
// (the game loads both tables from .LZC files), and parse the image. The
// image is returned in `image` so a caller can report from it.
TrackInfoTable load_track_info_table(
    const std::filesystem::path& path,
    std::vector<unsigned char>& image,
    const RaceTableLimits& limits = RaceTableLimits{});

RaceEventTable load_race_event_table(
    const std::filesystem::path& path,
    std::vector<unsigned char>& image,
    const RaceTableLimits& limits = RaceTableLimits{});

// ---------------------------------------------------------------------
// Lookups, file names and the Underground order
// ---------------------------------------------------------------------

// The first record whose number is `number`, as the linear scans at
// 0x004484F0 and 0x004ABC3D return it, or nullptr. Refuses a table that is
// not valid.
const TrackInfoRecord* find_track_info(const TrackInfoTable& table,
                                       std::int32_t number) noexcept;

// The record whose event number is `number`, or nullptr. Refuses a table
// that is not valid.
const RaceEventRecord* find_race_event(const RaceEventTable& table,
                                       std::uint32_t number) noexcept;

// One issue per used race slot whose track number is not in the track table.
// A career race looks its track up with the scan at 0x004ABC3D and reads
// +0x6C of the result without testing it (0x004ABC58), so such a slot would
// not load. Returns nothing when either table is not valid, because a table
// that did not parse cannot be checked against.
std::vector<RaceTableIssue> check_race_tracks(const RaceEventTable& events,
                                              const TrackInfoTable& tracks);

// The per-track file names the loading game flow formats, without the
// "TRACKS\" directory: "STREAM%s.BUN" (0x006C9B88 at 0x0044970D),
// "STREAM%s_%d.BUN" (0x006C9B70 at 0x0044972C), "TEX%dTRACK.BIN"
// (0x006C9C00 at 0x0044953E) and "TRACKB%d.LZC" (0x006C9B38 at 0x004497BE).
// The texture and bundle names take the current track number 0x0078A2F0,
// which a career race sets from the record's +0x6C (0x004ABC58..0x004ABC5C).
std::string track_stream_file_name(const TrackInfoRecord& record);
std::string track_stream_pack_file_name(const TrackInfoRecord& record);
std::string track_texture_file_name(std::int32_t number);
std::string track_bundle_file_name(std::int32_t number);

// The event a new career enters before any other: the handler at 0x004B37D0
// resets the career (0x004B3837 call 0x005A0A00, which zeroes the 256 event
// status bytes at +0x34C) and enters event 0 (0x004B383C `xor edi, edi`,
// 0x004B383E call 0x005A0CE0).
constexpr std::uint32_t kNewCareerEventNumber = 0;
// The status byte 0x005A0E60 writes when an event is won.
constexpr std::uint8_t kEventStatusWon = 2;

// Whether the availability test at 0x005A0FC0 would offer `event` with the
// given 256 status bytes, one per event number. It is false for event number
// 0 (0x005A0FC0..0x005A0FC7), when the event's own status is 2 or 4
// (0x005A1005, 0x005A100D), and when its kind is 8 (0x005A1015); event 0
// is reached only through the new-career path above. Otherwise a listed
// prerequisite counts when its status is 2,
// or when its status is 4 or 5 and its kind is 2 or 3 (0x005A1054..0x005A106D);
// mode 0 needs every one and mode 1 any one. Refuses (returns false for) a
// table that is not valid.
bool race_event_available(const RaceEventTable& table,
                          const RaceEventRecord& event,
                          const std::vector<std::uint8_t>& status) noexcept;

} // namespace nfsu
