#include "nfsu/race_events.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

// A fixture name no other process shares: several sessions run ctest from
// their own build directories at once, and issue #122 and #124 both came
// from test binaries that shared a fixed temp name.
std::string unique_fixture(const std::string& label) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return "nfsu_race_events_" + label + "_" + std::to_string(pid);
}

void put_u16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<unsigned char>(value & 0xFFU);
    bytes[at + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

void put_u32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

void append_chunk(Bytes& image, std::uint32_t id, const Bytes& payload) {
    const std::size_t at = image.size();
    image.resize(at + 8);
    put_u32(image, at, id);
    put_u32(image, at + 4, static_cast<std::uint32_t>(payload.size()));
    image.insert(image.end(), payload.begin(), payload.end());
}

// ---------------------------------------------------------------------
// Synthetic track records
// ---------------------------------------------------------------------

struct TrackSpec {
    std::int16_t number;
    std::string region;
    std::int8_t lap_skip;
    std::uint32_t mask;
};

Bytes track_record(const TrackSpec& spec) {
    Bytes record(nfsu::kTrackInfoRecordSize, 0);
    // Bytes the parser does not decode are filled, so a parser that read a
    // field from the wrong offset reads a value no field holds.
    for (std::size_t i = 0; i < record.size(); ++i) {
        record[i] = static_cast<unsigned char>(0xA0U + (i % 7U));
    }
    for (std::size_t i = 0; i < nfsu::kTrackInfoRegionSize; ++i) {
        record[nfsu::kTrackInfoRegionOffset + i] =
            i < spec.region.size() ? static_cast<unsigned char>(spec.region[i]) : 0;
    }
    record[nfsu::kTrackInfoLapSkipOffset] = static_cast<unsigned char>(spec.lap_skip);
    put_u16(record, nfsu::kTrackInfoNumberOffset, static_cast<std::uint16_t>(spec.number));
    put_u16(record, nfsu::kTrackInfoNumberCopyOffset,
            static_cast<std::uint16_t>(spec.number + 7));
    put_u32(record, nfsu::kTrackInfoModeMaskOffset, spec.mask);
    return record;
}

std::vector<TrackSpec> fixture_tracks() {
    return {
        {1003, "L1RA", 0, 0x71},
        {1102, "L1RA", 1, 0x24},
        {1210, "L1RB", 1, 0x02},
        {1301, "L1RB", 0, 0x08},
    };
}

Bytes track_payload(const std::vector<TrackSpec>& tracks) {
    Bytes payload;
    for (const TrackSpec& spec : tracks) {
        const Bytes record = track_record(spec);
        payload.insert(payload.end(), record.begin(), record.end());
    }
    return payload;
}

// A padding chunk, the table, and an unrelated chunk after it, as the retail
// file has chunks on both sides of the table.
Bytes globalb_image(const Bytes& table_payload) {
    Bytes image;
    append_chunk(image, 0x00000000U, Bytes(24, 0));
    append_chunk(image, nfsu::kTrackInfoChunkId, table_payload);
    append_chunk(image, 0x00034202U, Bytes(12, 0x11));
    return image;
}

constexpr std::uint64_t kTrackPayloadOffset = 32 + 8;

// ---------------------------------------------------------------------
// Synthetic event records
// ---------------------------------------------------------------------

struct RaceSpec {
    std::int16_t track;
    std::int8_t laps;
    std::int8_t direction;
};

struct EventSpec {
    std::uint32_t number;
    std::uint32_t kind;
    std::uint32_t type;
    std::vector<RaceSpec> races;
    std::uint16_t mode;
    std::vector<std::int16_t> prerequisites;
};

Bytes event_record(const EventSpec& spec) {
    Bytes record(nfsu::kRaceEventRecordSize, 0);
    for (std::size_t i = 0; i < record.size(); ++i) {
        record[i] = static_cast<unsigned char>(0x30U + (i % 11U));
    }
    put_u32(record, nfsu::kRaceEventNumberOffset, spec.number);
    put_u32(record, nfsu::kRaceEventKindOffset, spec.kind);
    put_u32(record, nfsu::kRaceEventTypeOffset, spec.type);
    // Every slot the event does not use keeps the fill, which is not a
    // track the table knows: the game never reads an unused slot and the
    // parser must not either.
    for (std::size_t r = 0; r < spec.races.size(); ++r) {
        const std::size_t slot = nfsu::kRaceEventRaceOffset + r * nfsu::kRaceEventRaceStride;
        put_u16(record, slot + nfsu::kRaceSlotTrackOffset,
                static_cast<std::uint16_t>(spec.races[r].track));
        record[slot + nfsu::kRaceSlotLapsOffset] =
            static_cast<unsigned char>(spec.races[r].laps);
        record[slot + nfsu::kRaceSlotDirectionOffset] =
            static_cast<unsigned char>(spec.races[r].direction);
    }
    put_u32(record, nfsu::kRaceEventRaceCountOffset,
            static_cast<std::uint32_t>(spec.races.size()));
    for (std::size_t p = 0; p < spec.prerequisites.size(); ++p) {
        put_u16(record, nfsu::kRaceEventPrerequisiteOffset + p * 2,
                static_cast<std::uint16_t>(spec.prerequisites[p]));
    }
    put_u16(record, nfsu::kRaceEventPrerequisiteModeOffset, spec.mode);
    put_u16(record, nfsu::kRaceEventPrerequisiteCountOffset,
            static_cast<std::uint16_t>(spec.prerequisites.size()));
    return record;
}

// Numbers out of order and with a gap, as the retail table has a gap at 85
// and ends with 120..122. Event 10 is kind 3 (a time trial, as retail
// events 15, 47, 68 and 77 are), and its prerequisite {9} is never won by
// test_availability's main sequence, so it stays closed there until that
// test sets its status directly. Event 11 is any-of {10}, exercising the
// kind-3 half of the prerequisite rule (0x005A1068 cmp esi,3) the same way
// event 9 already exercises the kind-2 half on event 7.
std::vector<EventSpec> fixture_events() {
    return {
        {0, 0, 0, {{1003, 2, 0}}, 0, {0}},
        {1, 0, 0, {{1003, 2, 0}}, 0, {0}},
        {2, 0, 2, {{1102, 2, 0}}, 0, {0}},
        {5, 0, 5, {{1003, 3, 1}}, 0, {0}},
        {3, 0, 3, {{1301, 4, 0}}, 0, {1, 2}},
        {6, 1, 0, {{1003, 2, 0}, {1003, 2, 1}, {1301, 4, 0}}, 0, {1, 2, 5}},
        {7, 2, 4, {{1102, 2, 1}}, 1, {3, 6}},
        {9, 0, 1, {{1210, 2, 1}}, 1, {7}},
        {120, 8, 0, {{1003, 3, 0}}, 0, {0}},
        {10, 3, 4, {{1102, 2, 0}}, 0, {9}},
        {11, 0, 0, {{1003, 2, 0}}, 1, {10}},
    };
}

Bytes event_payload(const std::vector<EventSpec>& events) {
    Bytes payload;
    for (const EventSpec& spec : events) {
        const Bytes record = event_record(spec);
        payload.insert(payload.end(), record.begin(), record.end());
    }
    return payload;
}

// The container holds the event chunk and the 0x00034A01 sibling the retail
// container holds, and unrelated chunks sit on both sides of it.
Bytes frontb_image(const Bytes& events_payload) {
    Bytes container;
    append_chunk(container, nfsu::kRaceEventChunkId, events_payload);
    append_chunk(container, 0x00034A01U, Bytes(32, 0));
    Bytes image;
    append_chunk(image, 0x00030210U, Bytes(40, 0x22));
    append_chunk(image, nfsu::kRaceEventContainerId, container);
    append_chunk(image, 0x00034A09U, Bytes(8, 0));
    return image;
}

constexpr std::uint64_t kEventContainerOffset = 48;
constexpr std::uint64_t kEventChunkOffset = kEventContainerOffset + 8;
constexpr std::uint64_t kEventPayloadOffset = kEventChunkOffset + 8;

// A literal-only JDLZ blob, which the decoder in nfsu_core accepts: one
// flag byte per eight literals, and the second flag stream read once and
// never consumed because there is no match.
Bytes jdlz_literals(const Bytes& raw) {
    Bytes blob = {'J', 'D', 'L', 'Z', 0x02, 0x10, 0x00, 0x00};
    blob.resize(16, 0);
    put_u32(blob, 8, static_cast<std::uint32_t>(raw.size()));
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (i % 8 == 0) {
            blob.push_back(0x00);
            if (i == 0) {
                blob.push_back(0x00);
            }
        }
        blob.push_back(raw[i]);
    }
    put_u32(blob, 12, static_cast<std::uint32_t>(blob.size()));
    return blob;
}

bool has_issue(const std::vector<nfsu::RaceTableIssue>& issues,
               std::uint64_t offset,
               const std::string& text) {
    for (const nfsu::RaceTableIssue& issue : issues) {
        if (issue.offset == offset && issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void dump_issues(const std::vector<nfsu::RaceTableIssue>& issues) {
    for (const nfsu::RaceTableIssue& issue : issues) {
        std::cerr << "  issue at " << issue.offset << ": " << issue.message << '\n';
    }
}

nfsu::TrackInfoTable parse_tracks(const Bytes& image) {
    return nfsu::parse_track_info_table(image.data(), image.size());
}

nfsu::RaceEventTable parse_events(const Bytes& image) {
    return nfsu::parse_race_event_table(image.data(), image.size());
}

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

void test_track_table() {
    const Bytes image = globalb_image(track_payload(fixture_tracks()));
    const nfsu::TrackInfoTable table = parse_tracks(image);
    dump_issues(table.issues);
    check(table.ok(), "the fixture track table parses");
    check(table.chunk_offset == 32, "track chunk offset");
    check(table.record_offset == kTrackPayloadOffset, "track record offset");
    check(table.records.size() == 4, "four track records");
    if (table.records.size() != 4) {
        return;
    }
    const nfsu::TrackInfoRecord& drag = table.records[2];
    check(drag.index == 2, "record index");
    check(drag.offset == kTrackPayloadOffset + 2 * nfsu::kTrackInfoRecordSize,
          "record offset");
    check(drag.number == 1210, "track number from +0x6C");
    check(drag.number_copy == 1217, "track number copy from +0x6E, kept raw");
    check(drag.region == "L1RB", "region from +0x40");
    check(drag.lap_skip == 1, "lap-skip byte from +0x69");
    check(drag.mode_mask == 0x02U, "mode mask from +0x74");
    check(table.records[0].lap_skip == 0 && table.records[0].mode_mask == 0x71U,
          "first record's own fields");

    check(nfsu::find_track_info(table, 1301) == &table.records[3], "lookup by number");
    check(nfsu::find_track_info(table, 1217) == nullptr,
          "the lookup keys on +0x6C, not on the copy at +0x6E");
    check(nfsu::find_track_info(table, 9999) == nullptr, "an absent number");

    check(nfsu::track_stream_file_name(drag) == "STREAML1RB.BUN", "stream name");
    check(nfsu::track_stream_pack_file_name(drag) == "STREAML1RB_1210.BUN",
          "stream pack name");
    check(nfsu::track_texture_file_name(1210) == "TEX1210TRACK.BIN", "texture name");
    check(nfsu::track_bundle_file_name(1210) == "TRACKB1210.LZC", "bundle name");
}

void test_track_table_refusals() {
    const Bytes good = track_payload(fixture_tracks());
    {
        Bytes payload = good;
        payload.resize(payload.size() - 4);
        const nfsu::TrackInfoTable table = parse_tracks(globalb_image(payload));
        check(!table.valid && has_issue(table.issues, 32, "not a non-zero whole number"),
              "a payload that is not whole records is refused");
    }
    {
        const nfsu::TrackInfoTable table = parse_tracks(globalb_image(Bytes()));
        check(!table.valid && has_issue(table.issues, 32, "not a non-zero whole number"),
              "an empty payload is refused");
    }
    {
        Bytes image;
        append_chunk(image, 0x00034202U, Bytes(12, 0));
        const nfsu::TrackInfoTable table = parse_tracks(image);
        check(!table.valid &&
                  has_issue(table.issues, 0, "expected exactly one chunk 0x00034201, found 0"),
              "a file without the table is refused");
    }
    {
        Bytes image = globalb_image(good);
        append_chunk(image, nfsu::kTrackInfoChunkId, good);
        const nfsu::TrackInfoTable table = parse_tracks(image);
        check(!table.valid && has_issue(table.issues, 32, "found 2"),
              "two tables are refused");
    }
    {
        Bytes image = globalb_image(good);
        image.push_back(0);
        const nfsu::TrackInfoTable table = parse_tracks(image);
        check(!table.valid && has_issue(table.issues, image.size() - 1,
                                        "smaller than a chunk header"),
              "trailing bytes are refused");
    }
    {
        Bytes image = globalb_image(good);
        put_u32(image, 32 + 4, static_cast<std::uint32_t>(good.size() + 1000));
        const nfsu::TrackInfoTable table = parse_tracks(image);
        check(!table.valid && has_issue(table.issues, 32, "extends beyond its parent"),
              "a chunk that overruns the file is refused");
    }
    {
        std::vector<TrackSpec> tracks = fixture_tracks();
        tracks[1].region = "L1RAXXXX";
        const nfsu::TrackInfoTable table = parse_tracks(globalb_image(track_payload(tracks)));
        check(!table.valid &&
                  has_issue(table.issues,
                            kTrackPayloadOffset + nfsu::kTrackInfoRecordSize +
                                nfsu::kTrackInfoRegionOffset,
                            "not NUL-terminated"),
              "an unterminated region is refused");
    }
    {
        std::vector<TrackSpec> tracks = fixture_tracks();
        tracks[0].region = "";
        const nfsu::TrackInfoTable table = parse_tracks(globalb_image(track_payload(tracks)));
        check(!table.valid && has_issue(table.issues,
                                        kTrackPayloadOffset + nfsu::kTrackInfoRegionOffset,
                                        "empty or not printable"),
              "an empty region is refused");
    }
    {
        std::vector<TrackSpec> tracks = fixture_tracks();
        tracks[0].region = std::string("L1\tA");
        const nfsu::TrackInfoTable table = parse_tracks(globalb_image(track_payload(tracks)));
        check(!table.valid && has_issue(table.issues,
                                        kTrackPayloadOffset + nfsu::kTrackInfoRegionOffset,
                                        "empty or not printable"),
              "a region that is not printable ASCII is refused");
    }
    {
        std::vector<TrackSpec> tracks = fixture_tracks();
        tracks[3].number = -5;
        const nfsu::TrackInfoTable table = parse_tracks(globalb_image(track_payload(tracks)));
        check(!table.valid &&
                  has_issue(table.issues,
                            kTrackPayloadOffset + 3 * nfsu::kTrackInfoRecordSize +
                                nfsu::kTrackInfoNumberOffset,
                            "track number -5 is not positive"),
              "a negative track number is refused");
    }
    {
        std::vector<TrackSpec> tracks = fixture_tracks();
        tracks[3].number = 1102;
        const nfsu::TrackInfoTable table = parse_tracks(globalb_image(track_payload(tracks)));
        check(!table.valid &&
                  has_issue(table.issues,
                            kTrackPayloadOffset + 3 * nfsu::kTrackInfoRecordSize +
                                nfsu::kTrackInfoNumberOffset,
                            "shared with an earlier record"),
              "a duplicate track number is refused");
        check(nfsu::find_track_info(table, 1003) == nullptr,
              "the lookup refuses a table that is not valid");
    }
    {
        const Bytes image = globalb_image(good);
        nfsu::RaceTableLimits limits;
        limits.max_image_size = image.size() - 1;
        const nfsu::TrackInfoTable table =
            nfsu::parse_track_info_table(image.data(), image.size(), limits);
        check(!table.valid && has_issue(table.issues, 0, "over the limit"),
              "the image size limit is applied");
    }
    {
        const nfsu::TrackInfoTable table = nfsu::parse_track_info_table(nullptr, 0);
        check(!table.valid && has_issue(table.issues, 0, "no image bytes"),
              "a null image is refused");
    }
}

void test_event_table() {
    const Bytes image = frontb_image(event_payload(fixture_events()));
    const nfsu::RaceEventTable table = parse_events(image);
    dump_issues(table.issues);
    check(table.ok(), "the fixture event table parses");
    check(table.container_offset == kEventContainerOffset, "container offset");
    check(table.chunk_offset == kEventChunkOffset, "event chunk offset");
    check(table.record_offset == kEventPayloadOffset, "event record offset");
    check(table.records.size() == 11, "eleven event records");
    if (table.records.size() != 11) {
        return;
    }

    const nfsu::RaceEventRecord& knockout = table.records[3];
    const std::uint64_t base = kEventPayloadOffset + 3 * nfsu::kRaceEventRecordSize;
    check(knockout.position == 3 && knockout.offset == base, "record position and offset");
    check(knockout.number == 5, "event number from +0x00");
    check(knockout.kind == 0, "kind from +0x04");
    check(knockout.type == 5, "type from +0x08");
    check(knockout.race_count == 1 && knockout.races.size() == 1, "one race");
    if (knockout.races.size() == 1) {
        check(knockout.races[0].offset == base + nfsu::kRaceEventRaceOffset,
              "race slot offset");
        check(knockout.races[0].track == 1003, "race track from slot +0");
        check(knockout.races[0].laps == 3, "race laps from slot +2");
        check(knockout.races[0].direction == 1, "race direction from slot +3");
    }
    check(knockout.prerequisite_mode == nfsu::kPrerequisitesAllOf &&
              knockout.prerequisite_count == 1 && knockout.prerequisites.size() == 1 &&
              knockout.prerequisites[0] == 0,
          "prerequisites from +0x9C, +0xA8 and +0xAA");

    const nfsu::RaceEventRecord& tournament = table.records[5];
    check(tournament.number == 6 && tournament.kind == nfsu::kRaceEventKindTournament,
          "the tournament record");
    check(tournament.races.size() == 3, "a tournament's three races");
    if (tournament.races.size() == 3) {
        check(tournament.races[1].offset == tournament.offset + nfsu::kRaceEventRaceOffset +
                                                nfsu::kRaceEventRaceStride,
              "the second race slot is four bytes on");
        check(tournament.races[1].track == 1003 && tournament.races[1].laps == 2 &&
                  tournament.races[1].direction == 1,
              "the second race");
        check(tournament.races[2].track == 1301 && tournament.races[2].laps == 4,
              "the third race");
    }
    check(tournament.prerequisites == std::vector<std::int16_t>({1, 2, 5}),
          "a tournament's prerequisites");

    const nfsu::RaceEventRecord& any_of = table.records[6];
    check(any_of.prerequisite_mode == nfsu::kPrerequisitesAnyOf &&
              any_of.prerequisites == std::vector<std::int16_t>({3, 6}),
          "an any-of prerequisite list");

    check(nfsu::find_race_event(table, 9) == &table.records[7], "lookup by event number");
    check(nfsu::find_race_event(table, 4) == nullptr, "an absent event number");
}

void test_event_table_refusals() {
    const auto refused = [](std::vector<EventSpec> events, std::size_t record,
                            std::size_t field, const std::string& text,
                            const std::string& label) {
        const nfsu::RaceEventTable table =
            parse_events(frontb_image(event_payload(events)));
        const std::uint64_t at =
            kEventPayloadOffset + record * nfsu::kRaceEventRecordSize + field;
        check(!table.valid && has_issue(table.issues, at, text), label);
        if (table.valid || !has_issue(table.issues, at, text)) {
            dump_issues(table.issues);
        }
    };
    {
        std::vector<EventSpec> events = fixture_events();
        events[4].number = 256;
        refused(events, 4, nfsu::kRaceEventNumberOffset, "is not below 256",
                "an event number of 256 is refused");
    }
    {
        std::vector<EventSpec> events = fixture_events();
        events[4].number = 2;
        refused(events, 4, nfsu::kRaceEventNumberOffset, "shared with an earlier record",
                "a duplicate event number is refused");
    }
    {
        std::vector<EventSpec> events = fixture_events();
        events[2].races.clear();
        refused(events, 2, nfsu::kRaceEventRaceCountOffset, "uses 0 race slots",
                "an event with no race is refused");
    }
    {
        std::vector<EventSpec> events = fixture_events();
        events[2].races.assign(9, RaceSpec{1102, 2, 0});
        // Nine slots do not fit: the ninth writes over the count at +0x98,
        // which the builder then overwrites with 9.
        refused(events, 2, nfsu::kRaceEventRaceCountOffset, "uses 9 race slots",
                "an event with nine races is refused");
    }
    {
        std::vector<EventSpec> events = fixture_events();
        events[5].races[2].track = 0;
        refused(events, 5,
                nfsu::kRaceEventRaceOffset + 2 * nfsu::kRaceEventRaceStride,
                "race 2 names track 0", "a race naming track 0 is refused");
    }
    {
        std::vector<EventSpec> events = fixture_events();
        events[1].races[0].laps = 0;
        refused(events, 1, nfsu::kRaceEventRaceOffset + nfsu::kRaceSlotLapsOffset,
                "has 0 laps", "a race of no laps is refused");
    }
    {
        std::vector<EventSpec> events = fixture_events();
        events[1].mode = 2;
        refused(events, 1, nfsu::kRaceEventPrerequisiteModeOffset,
                "prerequisite mode 2 is neither 0 nor 1",
                "a prerequisite mode of 2 is refused");
    }
    {
        std::vector<EventSpec> events = fixture_events();
        events[1].prerequisites.assign(6, 0);
        const nfsu::RaceEventTable table =
            parse_events(frontb_image(event_payload(events)));
        check(table.ok(), "six prerequisites, the whole list, are accepted");
        Bytes payload = event_payload(events);
        put_u16(payload,
                nfsu::kRaceEventRecordSize + nfsu::kRaceEventPrerequisiteCountOffset, 7);
        const nfsu::RaceEventTable seven = parse_events(frontb_image(payload));
        check(!seven.valid &&
                  has_issue(seven.issues,
                            kEventPayloadOffset + nfsu::kRaceEventRecordSize +
                                nfsu::kRaceEventPrerequisiteCountOffset,
                            "lists 7 prerequisites"),
              "seven prerequisites are refused");
        put_u16(payload,
                nfsu::kRaceEventRecordSize + nfsu::kRaceEventPrerequisiteCountOffset,
                0xFFFF);
        const nfsu::RaceEventTable negative = parse_events(frontb_image(payload));
        check(!negative.valid &&
                  has_issue(negative.issues,
                            kEventPayloadOffset + nfsu::kRaceEventRecordSize +
                                nfsu::kRaceEventPrerequisiteCountOffset,
                            "lists -1 prerequisites"),
              "a negative prerequisite count is refused");
    }
    {
        std::vector<EventSpec> events = fixture_events();
        events[6].prerequisites = {3, 4};
        refused(events, 6, nfsu::kRaceEventPrerequisiteOffset + 2,
                "prerequisite 4 is not an event of this table",
                "a prerequisite naming no event is refused");
    }
    {
        Bytes payload = event_payload(fixture_events());
        payload.resize(payload.size() - 1);
        const nfsu::RaceEventTable table = parse_events(frontb_image(payload));
        check(!table.valid &&
                  has_issue(table.issues, kEventChunkOffset, "not a non-zero whole number"),
              "an event payload that is not whole records is refused");
    }
    {
        Bytes image;
        append_chunk(image, nfsu::kRaceEventChunkId, event_payload(fixture_events()));
        const nfsu::RaceEventTable table = parse_events(image);
        check(!table.valid &&
                  has_issue(table.issues, 0, "expected exactly one chunk 0x80034A00, found 0"),
              "an event chunk outside its container is refused");
    }
    {
        Bytes container;
        append_chunk(container, nfsu::kRaceEventChunkId, event_payload(fixture_events()));
        append_chunk(container, nfsu::kRaceEventChunkId, event_payload(fixture_events()));
        Bytes image;
        append_chunk(image, nfsu::kRaceEventContainerId, container);
        const nfsu::RaceEventTable table = parse_events(image);
        check(!table.valid && has_issue(table.issues, 8, "chunk 0x00034A02, found 2"),
              "two event chunks in one container are refused");
    }
    {
        Bytes container;
        append_chunk(container, 0x00034A01U, Bytes(32, 0));
        Bytes image;
        append_chunk(image, nfsu::kRaceEventContainerId, container);
        const nfsu::RaceEventTable table = parse_events(image);
        check(!table.valid && has_issue(table.issues, 0, "chunk 0x00034A02, found 0"),
              "a container without the event chunk is refused");
    }
    {
        Bytes image = frontb_image(event_payload(fixture_events()));
        put_u32(image, kEventChunkOffset + 4, 0x7FFFFFFFU);
        const nfsu::RaceEventTable table = parse_events(image);
        check(!table.valid &&
                  has_issue(table.issues, kEventChunkOffset, "extends beyond its parent"),
              "an event chunk that overruns its container is refused");
    }
}

void test_names() {
    check(std::string(nfsu::race_type_name(0)) == "circuit", "type 0");
    check(std::string(nfsu::race_type_name(1)) == "drag", "type 1");
    check(std::string(nfsu::race_type_name(2)) == "sprint", "type 2");
    check(std::string(nfsu::race_type_name(3)) == "drift", "type 3");
    check(std::string(nfsu::race_type_name(4)) == "time-trial", "type 4");
    check(std::string(nfsu::race_type_name(5)) == "lap-knockout", "type 5");
    check(std::string(nfsu::race_type_name(6)) == "unknown", "type 6");
    check(std::string(nfsu::race_type_name(0xFFFFFFFFU)) == "unknown", "type -1");
    check(std::string(nfsu::race_direction_name(0)) == "forward", "direction 0");
    check(std::string(nfsu::race_direction_name(1)) == "reverse", "direction 1");
    check(std::string(nfsu::race_direction_name(2)) == "unknown", "direction 2");
    check(std::string(nfsu::race_direction_name(-1)) == "unknown", "direction -1");
}

std::vector<std::uint32_t> opened(const nfsu::RaceEventTable& table,
                                  const std::vector<std::uint8_t>& status) {
    std::vector<std::uint32_t> numbers;
    for (const nfsu::RaceEventRecord& event : table.records) {
        if (nfsu::race_event_available(table, event, status)) {
            numbers.push_back(event.number);
        }
    }
    // The fixture's records are deliberately not in number order, and the
    // answer is a set.
    std::sort(numbers.begin(), numbers.end());
    return numbers;
}

void test_availability() {
    const nfsu::RaceEventTable table = parse_events(frontb_image(event_payload(fixture_events())));
    check(table.ok(), "the availability fixture parses");
    std::vector<std::uint8_t> status(nfsu::kRaceEventNumberLimit, 0);

    check(opened(table, status).empty(), "nothing is open on a new career");
    status[0] = nfsu::kEventStatusWon;
    check(opened(table, status) == std::vector<std::uint32_t>({1, 2, 5}),
          "winning event 0 opens 1, 2 and 5 but never 0 itself or kind 8");

    status[1] = 2;
    check(opened(table, status) == std::vector<std::uint32_t>({2, 5}),
          "a won event is no longer offered, and 3 still needs 2");
    status[1] = 3;
    check(opened(table, status) == std::vector<std::uint32_t>({1, 2, 5}),
          "an entered but unwon event is still offered");
    status[1] = 4;
    check(opened(table, status) == std::vector<std::uint32_t>({2, 5}),
          "status 4 is not offered");
    status[1] = 2;
    status[2] = 2;
    check(opened(table, status) == std::vector<std::uint32_t>({3, 5}),
          "an all-of list opens when every event on it is won");

    // Event 7 is any-of {3, 6}. Event 3 is kind 0, so status 4 on it does
    // not count; status 2 does.
    status[3] = 4;
    check(opened(table, status) == std::vector<std::uint32_t>({5}),
          "status 4 on a kind-0 prerequisite does not count");
    status[3] = 2;
    check(opened(table, status) == std::vector<std::uint32_t>({5, 7}),
          "an any-of list opens on one won prerequisite");

    // Event 9 is any-of {7}, and 7 is kind 2: status 4 or 5 on it counts.
    status[7] = 5;
    check(opened(table, status) == std::vector<std::uint32_t>({5, 7, 9}),
          "status 5 on a kind-2 prerequisite counts, and 7 itself stays offered");
    status[7] = 4;
    check(opened(table, status) == std::vector<std::uint32_t>({5, 9}),
          "status 4 on a kind-2 prerequisite counts");

    // Event 11 is any-of {10}, and 10 is kind 3 (a time trial): status 4
    // or 5 on it counts, exactly as kind 2 does above (0x005A1068
    // cmp esi, 3). Retail events 16, 17, 48, 69, 78 and 112 each depend
    // on a kind-3 event the same way.
    status[10] = 5;
    check(opened(table, status) == std::vector<std::uint32_t>({5, 9, 11}),
          "status 5 on a kind-3 prerequisite counts, matching kind 2");
    status[10] = 4;
    check(opened(table, status) == std::vector<std::uint32_t>({5, 9, 11}),
          "status 4 on a kind-3 prerequisite counts, matching kind 2");
    status[10] = 3;
    check(opened(table, status) == std::vector<std::uint32_t>({5, 9}),
          "an entered but unwon kind-3 prerequisite does not open its dependant");
    status[10] = 0;

    check(!nfsu::race_event_available(table, table.records[1],
                                      std::vector<std::uint8_t>(10, 2)),
          "a status vector shorter than 256 is refused");

    // Event 0 gets an empty all-of list too, which every other rule would
    // offer: only the number test at 0x005A0FC0 keeps it out.
    std::vector<EventSpec> empty_lists = fixture_events();
    empty_lists[0].prerequisites.clear();
    empty_lists[2].prerequisites.clear();
    empty_lists[6].prerequisites.clear();
    const nfsu::RaceEventTable empty_table =
        parse_events(frontb_image(event_payload(empty_lists)));
    check(empty_table.ok(), "empty prerequisite lists parse");
    const std::vector<std::uint8_t> fresh(nfsu::kRaceEventNumberLimit, 0);
    check(opened(empty_table, fresh) == std::vector<std::uint32_t>({2}),
          "an empty all-of list is met, an empty any-of list is not, and "
          "event 0 is never offered");
}

void test_check_race_tracks() {
    const nfsu::TrackInfoTable tracks = parse_tracks(globalb_image(track_payload(fixture_tracks())));
    const nfsu::RaceEventTable events =
        parse_events(frontb_image(event_payload(fixture_events())));
    check(nfsu::check_race_tracks(events, tracks).empty(),
          "every fixture race names a listed track");

    std::vector<EventSpec> specs = fixture_events();
    specs[5].races[1].track = 1999;
    const nfsu::RaceEventTable broken = parse_events(frontb_image(event_payload(specs)));
    const std::vector<nfsu::RaceTableIssue> issues = nfsu::check_race_tracks(broken, tracks);
    check(issues.size() == 1 &&
              has_issue(issues,
                        kEventPayloadOffset + 5 * nfsu::kRaceEventRecordSize +
                            nfsu::kRaceEventRaceOffset + nfsu::kRaceEventRaceStride,
                        "event 6 race 1 names track 1999, which is not in the track table"),
          "a race naming an unlisted track is reported at its slot");

    const nfsu::TrackInfoTable invalid = parse_tracks(globalb_image(Bytes()));
    check(nfsu::check_race_tracks(broken, invalid).empty(),
          "nothing is checked against a track table that is not valid");
}

bool write_file(const fs::path& path, const Bytes& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

void test_load_from_file() {
    std::error_code code;
    const fs::path directory = fs::temp_directory_path(code) / unique_fixture("load");
    fs::remove_all(directory, code);
    fs::create_directories(directory, code);
    check(!code, "the fixture directory is created");

    const Bytes globalb = globalb_image(track_payload(fixture_tracks()));
    const Bytes frontb = frontb_image(event_payload(fixture_events()));
    const Bytes packed = jdlz_literals(frontb);
    check(write_file(directory / "GLOBALB.BUN", globalb), "GLOBALB fixture written");
    check(write_file(directory / "FrontB.lzc", packed), "FrontB fixture written");
    check(write_file(directory / "GLOBALB.LZC", jdlz_literals(globalb)),
          "packed GLOBALB fixture written");

    std::vector<unsigned char> image;
    const nfsu::TrackInfoTable raw_tracks =
        nfsu::load_track_info_table(directory / "GLOBALB.BUN", image);
    check(raw_tracks.ok() && !raw_tracks.jdlz && raw_tracks.stored_size == globalb.size() &&
              raw_tracks.image_size == globalb.size() && image == globalb,
          "a raw GLOBALB loads unchanged");
    const nfsu::TrackInfoTable packed_tracks =
        nfsu::load_track_info_table(directory / "GLOBALB.LZC", image);
    check(packed_tracks.ok() && packed_tracks.jdlz && image == globalb &&
              packed_tracks.records.size() == raw_tracks.records.size(),
          "a JDLZ GLOBALB loads to the same image");

    const nfsu::RaceEventTable events =
        nfsu::load_race_event_table(directory / "FrontB.lzc", image);
    check(events.ok() && events.jdlz && events.stored_size == packed.size() &&
              events.image_size == frontb.size() && image == frontb &&
              events.records.size() == 11,
          "a JDLZ FrontB loads and parses");

    const nfsu::RaceEventTable missing =
        nfsu::load_race_event_table(directory / "absent.lzc", image);
    check(!missing.valid && has_issue(missing.issues, 0, "cannot size the file"),
          "a missing file is reported");

    nfsu::RaceTableLimits small;
    small.max_file_size = packed.size() - 1;
    const nfsu::RaceEventTable too_big =
        nfsu::load_race_event_table(directory / "FrontB.lzc", image, small);
    check(!too_big.valid && has_issue(too_big.issues, 0, "over the limit"),
          "the stored size limit is applied before reading");

    nfsu::RaceTableLimits tight;
    tight.max_image_size = frontb.size() - 1;
    const nfsu::RaceEventTable too_wide =
        nfsu::load_race_event_table(directory / "FrontB.lzc", image, tight);
    check(!too_wide.valid && has_issue(too_wide.issues, 0, "JDLZ:"),
          "the image size limit bounds the decompression");

    Bytes corrupt = packed;
    put_u32(corrupt, 12, static_cast<std::uint32_t>(corrupt.size() + 1));
    check(write_file(directory / "corrupt.lzc", corrupt), "corrupt fixture written");
    const nfsu::RaceEventTable bad =
        nfsu::load_race_event_table(directory / "corrupt.lzc", image);
    check(!bad.valid && has_issue(bad.issues, 0, "JDLZ:"), "a bad JDLZ header is reported");

    fs::remove_all(directory, code);
}

// --write-fixture <globalb> <frontb> <frontb-missing-track>: lays three
// fixture files down for tests/race_events_guards.cmake and runs no check.
// It writes only where it is told and never replaces a file, the same rule
// nfsu_track_probe_tests --write-fixture keeps.
bool write_fixture(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        std::error_code code;
        if (fs::exists(path, code)) {
            std::cerr << "error: --write-fixture " << path
                      << " already exists; this command never replaces a file\n";
            return false;
        }
    }
    std::vector<EventSpec> broken = fixture_events();
    broken[5].races[1].track = 1999;
    const bool ok =
        write_file(paths[0], globalb_image(track_payload(fixture_tracks()))) &&
        write_file(paths[1], jdlz_literals(frontb_image(event_payload(fixture_events())))) &&
        write_file(paths[2], frontb_image(event_payload(broken)));
    if (!ok) {
        std::cerr << "failed to write the fixture\n";
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[1]) == "--write-fixture") {
        return write_fixture({argv[2], argv[3], argv[4]}) ? 0 : 1;
    }
    if (argc != 1) {
        std::cerr << "usage: nfsu_race_event_tests [--write-fixture <globalb> <frontb> "
                     "<frontb-missing-track>]\n";
        return 1;
    }

    test_track_table();
    test_track_table_refusals();
    test_event_table();
    test_event_table_refusals();
    test_names();
    test_availability();
    test_check_race_tracks();
    test_load_from_file();

    if (g_failures != 0) {
        std::cerr << g_failures << " race event test(s) failed\n";
        return 1;
    }
    std::cout << "race event tests passed\n";
    return 0;
}
