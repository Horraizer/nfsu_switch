#include "nfsu/track_residency.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void check_eq_u64(std::uint64_t got, std::uint64_t want, const std::string& what) {
    check(got == want, what + ": got " + std::to_string(got) + ", want " +
                           std::to_string(want));
}

void check_eq_i64(std::int64_t got, std::int64_t want, const std::string& what) {
    check(got == want, what + ": got " + std::to_string(got) + ", want " +
                           std::to_string(want));
}

void check_text(const std::string& got, const std::string& want,
                const std::string& what) {
    check(got == want, what + ":\n  got  '" + got + "'\n  want '" + want + "'");
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// ---------------------------------------------------------------------------
// A TRACKB image, built as bytes and parsed by the real parsers, so that a
// fixture goes through every rule a retail image does. The format's numbers
// are literals here and tied to the headers in test_constants.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kTableChunk = 0x00034107U;
constexpr std::uint32_t kSizesChunk = 0x00034108U;
constexpr std::size_t kRecord = 56;
constexpr std::uint32_t kZoneContainer = 0x80034150U;
constexpr std::uint32_t kPolygonChunk = 0x00034151U;
constexpr std::uint32_t kZoneChunk = 0x00034152U;
constexpr std::uint32_t kTrackListChunk = 0x00034153U;
constexpr std::uint32_t kExtraListChunk = 0x00034154U;
constexpr std::uint32_t kTrack = 900;

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFFU));
    }
}

void append_f32(Bytes& bytes, float value) {
    append_u32(bytes, float_bits(value));
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[offset + static_cast<std::size_t>(i)] =
            static_cast<unsigned char>((value >> (8 * i)) & 0xFFU);
    }
}

Bytes chunk(std::uint32_t id, const Bytes& payload) {
    Bytes out;
    append_u32(out, id);
    append_u32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

void append(Bytes& into, const Bytes& more) {
    into.insert(into.end(), more.begin(), more.end());
}

struct RecordSpec {
    const char* name;
    std::int16_t number;
    std::uint8_t track_flag;
    std::uint32_t file;
    std::uint32_t size;
    std::uint32_t kept;
};

// The corridor: five zones in a row along slot 0, each 100 wide and 100
// deep in slot 1, except that E stands 20 units past D with a gap between.
//
//   zone  key  polygon            lists                 on the route
//   A     101  [0,100]            101 111 102           yes
//   B     102  [100,200]          102 112 101 103       yes
//   C     103  [200,300]          103 113 104           yes
//   D     104  [300,400]          104 114               no
//   E     105  [420,500]          105                   yes
//
// Records 6 to 8 carry the track byte. Every record keeps what it reads
// except Y0, which keeps a fifth, as a texture pack does.
const RecordSpec kRecords[] = {
    {"--", 0, 0, 0, 64, 64},
    {"Y0", 2500, 0, 1, 500, 100},
    {"X0", 2400, 0, 1, 1000, 1000},
    {"Z0", 2600, 0, 1, 100, 100},
    {"X99", 2499, 0, 1, 100, 100},
    {"Z3", 2603, 0, 1, 100, 100},
    {"A1", 101, 1, 1, 300, 300},
    {"A11", 111, 1, 1, 300, 300},
    {"B2", 102, 1, 1, 300, 300},
    {"B12", 112, 0, 1, 300, 300},
    {"C3", 103, 0, 1, 300, 300},
    {"C13", 113, 0, 1, 300, 300},
    {"D4", 104, 0, 1, 300, 300},
    {"D14", 114, 0, 1, 300, 300},
    {"E5", 105, 0, 1, 300, 300},
};
constexpr std::size_t kRecordCount = sizeof(kRecords) / sizeof(kRecords[0]);

Bytes polygon_record(std::int16_t key, float x0, float x1) {
    Bytes out;
    append_u32(out, 11);
    append_u32(out, 11);
    append_u16(out, static_cast<std::uint16_t>(key));
    append_u16(out, 4);
    const float corners[4][2] = {{x0, 0.0F}, {x1, 0.0F}, {x1, 100.0F}, {x0, 100.0F}};
    for (std::size_t v = 0; v < 16; ++v) {
        append_f32(out, v < 4 ? corners[v][0] : 0.0F);
        append_f32(out, v < 4 ? corners[v][1] : 0.0F);
    }
    return out;
}

Bytes zone_record(std::int16_t key, const std::vector<std::int16_t>& sections) {
    Bytes out;
    append_u32(out, 11);
    append_u32(out, 11);
    append_u16(out, static_cast<std::uint16_t>(key));
    append_u16(out, 0);
    append_u32(out, 0);
    append_u32(out, static_cast<std::uint32_t>(sections.size()));
    for (std::size_t s = 0; s < 64; ++s) {
        append_u16(out, static_cast<std::uint16_t>(s < sections.size() ? sections[s] : 0));
    }
    return out;
}

// `tail` goes in the slot just past the count: a reader that walked the
// capacity would pick it up.
Bytes list_record(std::size_t size, std::uint32_t key,
                  const std::vector<std::int16_t>& sections, std::int16_t tail) {
    Bytes out;
    append_u32(out, 11);
    append_u32(out, 11);
    append_u32(out, key);
    append_u32(out, static_cast<std::uint32_t>(sections.size()));
    const std::size_t capacity = (size - 16) / 2;
    for (std::size_t s = 0; s < capacity; ++s) {
        std::int16_t value = 0;
        if (s < sections.size()) {
            value = sections[s];
        } else if (s == sections.size()) {
            value = tail;
        }
        append_u16(out, static_cast<std::uint16_t>(value));
    }
    return out;
}

struct Corridor {
    Bytes image;
    nfsu::TrackStreamTable table;
    nfsu::TrackZoneReport zones;
};

// `with_route` false leaves the 0x00034153 chunk empty, so no record is keyed
// on the track. `extra_flagged` appends that many more records with the track
// byte set, numbered from 3000, 10 bytes each.
Corridor make_corridor(bool with_route = true, std::size_t extra_flagged = 0) {
    Bytes table;
    std::uint32_t offset = 0;
    std::vector<RecordSpec> specs(std::begin(kRecords), std::end(kRecords));
    for (std::size_t k = 0; k < extra_flagged; ++k) {
        specs.push_back(RecordSpec{"F", static_cast<std::int16_t>(3000 + k), 1, 1, 10, 10});
    }
    for (const RecordSpec& spec : specs) {
        Bytes record(kRecord, 0);
        for (std::size_t i = 0; spec.name[i] != '\0'; ++i) {
            record[i] = static_cast<unsigned char>(spec.name[i]);
        }
        record[8] = static_cast<unsigned char>(spec.number & 0xFF);
        record[9] = static_cast<unsigned char>((spec.number >> 8) & 0xFF);
        record[10] = spec.track_flag;
        put_u32(record, 16, spec.file);
        put_u32(record, 20, spec.file == 0 ? 0 : offset);
        put_u32(record, 24, spec.size);
        put_u32(record, 28, spec.kept);
        if (spec.file != 0) {
            offset += 2048;
        }
        append(table, record);
    }
    Bytes sizes;
    append_u32(sizes, 64);
    append_u32(sizes, offset);
    append_u32(sizes, 0);

    Bytes polygons;
    append(polygons, polygon_record(101, 0.0F, 100.0F));
    append(polygons, polygon_record(102, 100.0F, 200.0F));
    append(polygons, polygon_record(103, 200.0F, 300.0F));
    append(polygons, polygon_record(104, 300.0F, 400.0F));
    append(polygons, polygon_record(105, 420.0F, 500.0F));
    Bytes zones;
    append(zones, zone_record(101, {101, 111, 102}));
    append(zones, zone_record(102, {102, 112, 101, 103}));
    append(zones, zone_record(103, {103, 113, 104}));
    append(zones, zone_record(104, {104, 114}));
    append(zones, zone_record(105, {105}));
    Bytes track_lists;
    if (with_route) {
        // A neighbour's record first: the streamer takes the one keyed on
        // its own track. The slot past the count names D.
        append(track_lists, list_record(272, 901, {104}, 0));
        append(track_lists, list_record(272, kTrack, {101, 102, 103, 105}, 104));
    }
    Bytes extra_lists;
    append(extra_lists, list_record(32, 901, {2604}, 0));
    append(extra_lists, list_record(32, kTrack, {2603}, 2601));
    Bytes children;
    append(children, chunk(kPolygonChunk, polygons));
    append(children, chunk(kZoneChunk, zones));
    append(children, chunk(kTrackListChunk, track_lists));
    append(children, chunk(kExtraListChunk, extra_lists));

    Corridor corridor;
    append(corridor.image, chunk(0x00000000U, Bytes(24, 0)));
    append(corridor.image, chunk(kTableChunk, table));
    append(corridor.image, chunk(kSizesChunk, sizes));
    append(corridor.image, chunk(kZoneContainer, children));
    corridor.table = nfsu::parse_track_stream_table(corridor.image.data(),
                                                    corridor.image.size());
    corridor.zones = nfsu::parse_track_zones(corridor.image.data(),
                                             corridor.image.size(), 0);
    return corridor;
}

std::size_t record_of(const Corridor& corridor, std::int32_t number) {
    return nfsu::find_track_stream_record(corridor.table, number);
}

// A car standing still at slot 0 `x`, halfway across the corridor: no
// look-ahead, so every point is the car's own.
nfsu::TrackResidencySample at(float x) {
    nfsu::TrackResidencySample sample;
    sample.position[0] = x;
    sample.position[1] = 50.0F;
    return sample;
}

nfsu::TrackResidencySample moving(float x, float speed) {
    nfsu::TrackResidencySample sample = at(x);
    sample.velocity[0] = speed;
    return sample;
}

void repeat(std::vector<nfsu::TrackResidencySample>& samples, float x,
            std::size_t count) {
    for (std::size_t k = 0; k < count; ++k) {
        samples.push_back(at(x));
    }
}

nfsu::TrackResidencyOptions plain_options(std::uint64_t budget) {
    nfsu::TrackResidencyOptions options;
    options.budget_bytes = budget;
    // The two allocator constants off, so that the fixture's arithmetic is
    // the budget's; test_placement_boundary puts them back.
    options.placement_slack = 0;
    options.block_header = 0;
    return options;
}

bool run(const Corridor& corridor, const std::vector<nfsu::TrackResidencySample>& samples,
         const nfsu::TrackResidencyOptions& options, nfsu::TrackResidencyRun& out) {
    std::string error;
    const bool ok = nfsu::simulate_track_residency(
        corridor.table, corridor.zones, kTrack,
        nfsu::track_residency_table_costs(corridor.table), samples, options, out,
        error);
    check(ok, "the simulation runs: " + error);
    if (ok) {
        auto costs = nfsu::track_residency_table_costs(corridor.table);
        auto startup_options = options;
        nfsu::TrackResidencySession live;
        std::vector<nfsu::TrackResidencyStep> incremental;
        check(live.start(corridor.table,corridor.zones,kTrack,costs,startup_options,incremental,error),
              "incremental startup: " + error);
        // Later transitions must not read the caller's expired startup input.
        // Alter valid storage first so regressions fail without a dangling read.
        std::fill(costs.begin(),costs.end(),nfsu::TrackResidencyCost{});
        startup_options = plain_options(0);
        startup_options.policy = nfsu::TrackResidencyPolicy::zone_only;
        for (const auto& sample : samples) {
            nfsu::TrackResidencyStep step;
            check(live.advance(sample,step,error),"incremental sample: " + error);
            incremental.push_back(std::move(step));
        }
        check(incremental.size()==out.steps.size(),"incremental and batch step counts match");
        for (std::size_t i=0;i<std::min(incremental.size(),out.steps.size());++i)
            check(nfsu::track_residency_step_line(corridor.table,incremental[i])==
                  nfsu::track_residency_step_line(corridor.table,out.steps[i]),
                  "incremental and batch transitions match at " + std::to_string(i));
        check(!live.start(corridor.table,corridor.zones,kTrack,costs,options,incremental,error),
              "cannot restart a live streamer");
    }
    return ok;
}

std::string numbers(const Corridor& corridor, const std::vector<std::size_t>& records) {
    std::string text;
    for (const std::size_t r : records) {
        if (!text.empty()) {
            text += ' ';
        }
        text += std::to_string(corridor.table.records[r].number);
    }
    return text;
}

// Every load and every release of the run, in order, as "+n" and "-n".
std::string history(const Corridor& corridor, const nfsu::TrackResidencyRun& run,
                    bool path_only) {
    std::string text;
    for (const nfsu::TrackResidencyStep& step : run.steps) {
        if (path_only && !step.on_path) {
            continue;
        }
        for (const std::size_t r : step.releases) {
            text += (text.empty() ? "-" : " -") +
                    std::to_string(corridor.table.records[r].number);
        }
        for (const std::size_t r : step.loads) {
            text += (text.empty() ? "+" : " +") +
                    std::to_string(corridor.table.records[r].number);
        }
    }
    return text;
}

std::string committed_keys(const nfsu::TrackResidencyRun& run) {
    std::string text;
    for (const nfsu::TrackResidencyStep& step : run.steps) {
        if (!step.on_path) {
            continue;
        }
        if (!text.empty()) {
            text += ' ';
        }
        text += std::to_string(step.committed_key);
    }
    return text;
}

std::string resident_numbers(const Corridor& corridor, const nfsu::TrackResidencyRun& run) {
    std::vector<std::size_t> records;
    for (std::size_t r = 0; r < run.resident.size(); ++r) {
        if (run.resident[r] != 0) {
            records.push_back(r);
        }
    }
    return numbers(corridor, records);
}

// ---------------------------------------------------------------------------

void test_constants() {
    check_eq_u64(nfsu::kTrackResidencySlots, 2, "slots");
    check_eq_i64(nfsu::kTrackResidencyNoZone, -1, "no zone");
    check_eq_u64(nfsu::kTrackResidencyLookAheadPoints, 3, "look-ahead points");
    check_eq_u64(float_bits(nfsu::kTrackResidencyLookAheadFraction), 0x3F000000U,
                 "0x006CC7DC 0.5");
    check_eq_u64(float_bits(nfsu::kTrackResidencyLookAheadSeconds), 0x3FC00000U,
                 "0x006B781C 1.5");
    check_eq_u64(float_bits(nfsu::kTrackResidencyLookAheadDistance), 0x42C80000U,
                 "0x006B7820 100");
    check_eq_u64(float_bits(nfsu::kTrackResidencyLookAheadMaxSpeed), 0x4332CF98U,
                 "0x006CCCA4");
    check_eq_u64(float_bits(nfsu::kTrackResidencyRouteQuerySpeed), 0x418F0C79U,
                 "0x006CCCA0");
    check_eq_i64(nfsu::kTrackResidencyCommitCount, 3, "commit count");
    check_eq_i64(nfsu::kTrackResidencyFirstSampleCount, 1000, "first sample count");
    check_eq_u64(nfsu::kTrackResidencyPlacementsPerPass, 2, "placements per pass");
    check_eq_u64(nfsu::kTrackResidencyPlacementSlack, 0x400, "placement slack");
    check_eq_u64(nfsu::kTrackResidencyBlockHeader, 0x58, "block header");
    check_eq_u64(nfsu::kTrackResidencyReleasePassLimit, 0x9C3C00, "release limit");
    check_eq_u64(nfsu::kTrackResidencyTicksPerSecond, 60, "ticks per second");
    check_eq_u64(nfsu::kTrackResidencyReadsInFlight, 3, "reads in flight");
    check_text(nfsu::track_residency_policy_name(nfsu::TrackResidencyPolicy::retail),
               "retail", "retail name");
    check_text(nfsu::track_residency_policy_name(nfsu::TrackResidencyPolicy::zone_only),
               "zone-only", "zone-only name");
    // The fixture's own literals against the headers they stand for.
    check(kTableChunk == nfsu::kTrackStreamTableId && kRecord == nfsu::kTrackStreamRecordSize &&
              kZoneContainer == nfsu::kTrackZoneContainerId &&
              kTrackListChunk == nfsu::kTrackZoneTrackListId &&
              kExtraListChunk == nfsu::kTrackZoneExtraListId,
          "the fixture's chunk ids and record size");
}

void test_corridor_parses() {
    const Corridor corridor = make_corridor();
    check(corridor.table.valid && corridor.table.ok(), "the corridor table parses");
    check(corridor.zones.valid && corridor.zones.ok(), "the corridor zones parse");
    check_eq_u64(corridor.table.records.size(), kRecordCount, "records");
    check_eq_u64(corridor.zones.zones.size(), 5, "zones");
    const std::vector<nfsu::TrackResidencyCost> costs =
        nfsu::track_residency_table_costs(corridor.table);
    check(costs.size() == kRecordCount && costs[1].place_bytes == 500 &&
              costs[1].held_bytes == 100 && costs[2].place_bytes == 1000 &&
              costs[2].held_bytes == 1000,
          "table costs: the byte count to place and the kept bytes to hold");
}

void test_slot_key() {
    const Corridor corridor = make_corridor();
    const nfsu::TrackZoneSectionList* route = nullptr;
    for (const nfsu::TrackZoneSectionList& list : corridor.zones.section_lists) {
        if (list.chunk_id == kTrackListChunk && list.key == kTrack) {
            route = &list;
        }
    }
    check(route != nullptr, "the corridor has a route record");
    nfsu::TrackResidencyLookAhead detail;

    // Standing still: all three points are the car's.
    check_eq_i64(nfsu::track_residency_slot_key(corridor.zones, route, at(50.0F), &detail),
                 101, "standing in A");
    check(detail.points[2][0] == 50.0F && detail.keys[0] == 101 && detail.keys[2] == 101,
          "a car standing still looks nowhere");
    // Not on the route, standing still: the own point accepts D, the look-ahead
    // points refuse it, and the answer falls back to the own point.
    check_eq_i64(nfsu::track_residency_slot_key(corridor.zones, route, at(350.0F), &detail),
                 104, "standing in D");
    check(detail.keys[0] == 104 && detail.keys[1] == -1 && detail.keys[2] == -1,
          "D is accepted only for the car's own point");

    // Moving at 20: the look-ahead is 30 units, points at +15 and +30.
    check_eq_i64(nfsu::track_residency_slot_key(corridor.zones, route,
                                                moving(80.0F, 20.0F), &detail),
                 102, "A looking into B");
    check(detail.points[1][0] == 95.0F && detail.points[2][0] == 110.0F &&
              detail.points[1][1] == 50.0F,
          "look-ahead points at 0.5 and 1.0 of 1.5 s");
    check(detail.keys[0] == 101 && detail.keys[1] == 101 && detail.keys[2] == 102,
          "keys along the look-ahead");
    check(detail.route_query_skipped, "20 is above the route-query speed");
    check(!([&] {
              nfsu::TrackResidencyLookAhead slow;
              nfsu::track_residency_slot_key(corridor.zones, route,
                                             moving(80.0F, 17.0F), &slow);
              return slow.route_query_skipped;
          }()),
          "17 is not above the route-query speed");
    // C looking into D, which the route does not name: the answer stops at C.
    check_eq_i64(nfsu::track_residency_slot_key(corridor.zones, route,
                                                moving(280.0F, 20.0F), &detail),
                 103, "C looking into D stays C");
    check(detail.zones[2] != nfsu::kTrackZoneNone && detail.keys[2] == -1,
          "D was found and refused");
    // With no route record every zone is accepted.
    check_eq_i64(nfsu::track_residency_slot_key(corridor.zones, nullptr,
                                                moving(280.0F, 20.0F), &detail),
                 104, "no route record: C looks into D");
    // The farthest point before the first gap: D, then the gap, then E.
    check_eq_i64(nfsu::track_residency_slot_key(corridor.zones, route,
                                                moving(395.0F, 20.0F), &detail),
                 104, "a gap stops the look-ahead");
    check(detail.keys[1] == -1 && detail.keys[2] == 105,
          "the far point is in E and still not taken");
    // Outside every zone, looking in: the own point has no key, the two
    // look-ahead points are in A.
    check_eq_i64(nfsu::track_residency_slot_key(corridor.zones, route,
                                                moving(-10.0F, 20.0F), &detail),
                 101, "outside, looking into A");
    check(detail.keys[0] == -1 && detail.keys[1] == 101, "outside keys");
    // Outside with the first look-ahead point outside too: nothing.
    check_eq_i64(nfsu::track_residency_slot_key(corridor.zones, route,
                                                moving(-20.0F, 20.0F), &detail),
                 -1, "outside, the first look-ahead point outside");
    // The distance clamp: at 100 the look-ahead is 100 units, not 150.
    nfsu::track_residency_slot_key(corridor.zones, route, moving(10.0F, 100.0F), &detail);
    check(detail.points[2][0] == 110.0F && detail.points[1][0] == 60.0F,
          "the look-ahead is clamped to 100 units: " +
              std::to_string(detail.points[2][0]));
    // At 66 it is 99 units: under the clamp.
    nfsu::track_residency_slot_key(corridor.zones, route, moving(10.0F, 66.0F), &detail);
    check(detail.points[2][0] == 109.0F, "at 66 the look-ahead is 99 units");
    // Above 178.8109 there is no look-ahead at all; at exactly the constant
    // there still is.
    nfsu::track_residency_slot_key(corridor.zones, route, moving(10.0F, 179.0F), &detail);
    check(detail.points[2][0] == 10.0F, "above the speed cap the look-ahead is zero");
    nfsu::track_residency_slot_key(corridor.zones, route,
                                   moving(10.0F, nfsu::kTrackResidencyLookAheadMaxSpeed),
                                   &detail);
    check(detail.points[2][0] == 110.0F, "at the speed cap the look-ahead is 100");
    // Slot 1 of the velocity counts too: moving in slot 1 only.
    nfsu::TrackResidencySample sideways = at(50.0F);
    sideways.velocity[1] = 20.0F;
    nfsu::track_residency_slot_key(corridor.zones, route, sideways, &detail);
    check(detail.points[2][0] == 50.0F && detail.points[2][1] == 80.0F,
          "a look-ahead along slot 1");
}

void test_start_set() {
    const Corridor corridor = make_corridor();
    nfsu::TrackResidencyRun out;
    if (!run(corridor, {}, plain_options(100000), out)) {
        return;
    }
    // Fixed four, the extra list, the track byte in table order; two a pass,
    // and a pass every other step. A pass that places its second record
    // stops there and marks the request deferred even when nothing is left
    // (0x00444E31), so the eight records take four passes and a fifth that
    // finds nothing: nine steps.
    check_text(history(corridor, out, false), "+2500 +2400 +2600 +2499 +2603 +101 +111 +102",
               "the start set loads in request order");
    check_eq_u64(out.start_steps, 9, "start steps");
    check(out.start_loaded, "the start set loaded");
    check(out.steps.size() == 9 && out.steps[0].rebuilt && !out.steps[1].rebuilt,
          "one forced rebuild");
    check(out.steps[0].loads.size() == 2 && out.steps[1].loads.empty() &&
              !out.steps[1].placed && out.steps[2].placed,
          "a pass, then a step with nothing in flight");
    check(out.steps[6].loads.size() == 2 && !out.steps[7].placed &&
              out.steps[8].placed && out.steps[8].loads.empty(),
          "the last pass finds nothing to place");
    check(out.steps[8].set_loaded && !out.steps[7].set_loaded,
          "the set is loaded on the pass that places nothing");
    check_eq_u64(out.steps[8].held_bytes, 100 + 1000 + 100 + 100 + 100 + 900,
                 "held once loaded: Y0 keeps a fifth");
    check_eq_u64(out.steps[0].held_bytes, 1100, "held after the first pass");
    check_eq_u64(out.steps[8].requested, 8, "requested");
    check_eq_u64(out.steps[8].resident, 8, "resident");
    check_eq_u64(out.peak_held_bytes, 2300, "peak");
    check_eq_i64(out.steps[0].committed_key, -1, "no slot during the start");

    // The same with three a pass: the pass order stays the request order.
    nfsu::TrackResidencyOptions three = plain_options(100000);
    three.placements_per_pass = 3;
    if (run(corridor, {}, three, out)) {
        check_text(history(corridor, out, false),
                   "+2500 +2400 +2600 +2499 +2603 +101 +111 +102",
                   "three a pass, same order");
        // Three, three and the last two: the third pass walks to the end of
        // the list, so it is not deferred and the next step finishes.
        check_eq_u64(out.start_steps, 6, "three a pass: fewer steps");
        check_eq_u64(out.steps[0].loads.size(), 3, "three loaded in one step");
    }
    // Four a pass: only three reads start at once, the fourth on the next
    // step.
    nfsu::TrackResidencyOptions four = plain_options(100000);
    four.placements_per_pass = 4;
    if (run(corridor, {}, four, out)) {
        check(out.steps[0].loads.size() == 3 && out.steps[1].loads.size() == 1,
              "at most three reads in flight");
    }

    // zone-only never asks for the track byte.
    nfsu::TrackResidencyOptions zone_only = plain_options(100000);
    zone_only.policy = nfsu::TrackResidencyPolicy::zone_only;
    if (run(corridor, {}, zone_only, out)) {
        check_text(history(corridor, out, false), "+2500 +2400 +2600 +2499 +2603",
                   "zone-only start set");
    }
}

void test_commit_needs_three_samples() {
    const Corridor corridor = make_corridor();
    std::vector<nfsu::TrackResidencySample> samples;
    repeat(samples, 50.0F, 3);   // A
    repeat(samples, 150.0F, 1);  // one sample in B
    repeat(samples, 50.0F, 2);
    repeat(samples, 150.0F, 2);  // two samples in B
    repeat(samples, 50.0F, 2);
    repeat(samples, 150.0F, 3);  // three
    repeat(samples, 150.0F, 1);
    nfsu::TrackResidencyRun out;
    if (!run(corridor, samples, plain_options(100000), out)) {
        return;
    }
    // The first sample after the car is placed commits at once (0x00444840
    // stores -1 in the count); a new key then needs three samples in a row.
    check_text(committed_keys(out),
               "101 101 101 101 101 101 101 101 101 101 101 101 102 102",
               "commits");
    std::size_t rebuilds = 0;
    for (const nfsu::TrackResidencyStep& step : out.steps) {
        rebuilds += step.on_path && step.rebuilt ? 1U : 0U;
    }
    check_eq_u64(rebuilds, 2, "two rebuilds on the path");
    check(out.steps[out.start_steps].rebuilt &&
              out.steps[out.start_steps].committed_key == 101,
          "the first path step rebuilds");
    check(out.steps[out.start_steps + 12].rebuilt, "the third B sample rebuilds");
    // A rebuild onto a zone whose sections are all resident loads nothing and
    // settles in the same step.
    check(out.steps[out.start_steps].loads.empty() &&
              out.steps[out.start_steps].set_loaded,
          "nothing to load in A");
}

// Enter B, leave it for A, come back: the retail streamer keeps what it
// loaded until the room is needed; zone-only drops it at once.
void test_enter_leave_revisit() {
    const Corridor corridor = make_corridor();
    std::vector<nfsu::TrackResidencySample> samples;
    repeat(samples, 50.0F, 4);
    repeat(samples, 150.0F, 6);
    repeat(samples, 50.0F, 6);
    repeat(samples, 150.0F, 6);
    nfsu::TrackResidencyRun out;
    if (!run(corridor, samples, plain_options(100000), out)) {
        return;
    }
    check_text(history(corridor, out, true), "+112 +103",
               "retail: B's two new sections load once and stay");
    check_text(resident_numbers(corridor, out),
               "2500 2400 2600 2499 2603 101 111 102 112 103",
               "retail: resident at the end");
    // Each change of zone commits on its third sample.
    check_text(committed_keys(out),
               "101 101 101 101 101 101 102 102 102 102 102 102 101 101 101 101 101 101 "
               "102 102 102 102",
               "retail commits");

    nfsu::TrackResidencyOptions zone_only = plain_options(100000);
    zone_only.policy = nfsu::TrackResidencyPolicy::zone_only;
    if (run(corridor, samples, zone_only, out)) {
        // A rebuild releases before the pass places, so each step's
        // releases come before its loads.
        check_text(history(corridor, out, true),
                   "+101 +111 +102 -111 +112 +103 -112 -103 +111 -111 +112 +103",
                   "zone-only: enter, leave and revisit");
        check_text(resident_numbers(corridor, out),
                   "2500 2400 2600 2499 2603 101 102 112 103",
                   "zone-only: resident at the end is B's request");
    }
}

// A budget that forces releases: the loaded, unrequested sections go least
// recently requested first, and among sections requested on the same step
// in table order.
void test_release_order() {
    const Corridor corridor = make_corridor();
    std::vector<nfsu::TrackResidencySample> samples;
    repeat(samples, 50.0F, 4);   // A
    repeat(samples, 150.0F, 6);  // B
    repeat(samples, 250.0F, 6);  // C
    repeat(samples, 150.0F, 6);  // B again
    repeat(samples, 50.0F, 6);   // A again
    nfsu::TrackResidencyRun out;
    if (!run(corridor, samples, plain_options(2600), out)) {
        return;
    }
    // In B, 111 is the one unrequested section. In C, 101, 102 and 112 were
    // all last requested when B was entered, so they go in table order, and
    // two releases make room. Back in B, 113 and 104 go, and B's two
    // sections load in the order B's list names them. Back in A, 112 and
    // 103 were last requested on B's second visit; one release is enough.
    check_text(history(corridor, out, true),
               "-111 +112 +103 -101 -102 +113 +104 -113 -104 +102 +101 -112 +111",
               "the release order under a 2600-byte budget");
    check_eq_u64(out.peak_held_bytes, 2600, "the budget is reached and not passed");
    for (const nfsu::TrackResidencyStep& step : out.steps) {
        check(step.held_bytes <= 2600, "no step holds more than the budget");
        check(!step.over_budget, "every request fits once something is released");
    }
    check_text(resident_numbers(corridor, out),
               "2500 2400 2600 2499 2603 101 111 102 103",
               "resident at the end");
}

// A request that cannot fit even with everything unrequested released: the
// streamer releases all it can, gives the section up, reports the step and
// goes on.
void test_over_budget() {
    Corridor corridor = make_corridor();
    std::vector<nfsu::TrackResidencySample> samples;
    repeat(samples, 50.0F, 4);   // A
    repeat(samples, 350.0F, 5);  // D: 104 and 114
    repeat(samples, 50.0F, 4);   // back in A
    std::vector<nfsu::TrackResidencyCost> costs =
        nfsu::track_residency_table_costs(corridor.table);
    costs[record_of(corridor, 114)].place_bytes = 5000;
    costs[record_of(corridor, 114)].held_bytes = 5000;
    nfsu::TrackResidencyRun out;
    std::string error;
    check(nfsu::simulate_track_residency(corridor.table, corridor.zones, kTrack, costs,
                                         samples, plain_options(2600), out, error),
          "an over-budget request does not fail the run: " + error);
    std::size_t over = 0;
    std::size_t first_over = 0;
    for (const nfsu::TrackResidencyStep& step : out.steps) {
        if (step.over_budget) {
            if (over == 0) {
                first_over = step.index;
            }
            ++over;
        }
    }
    check_eq_u64(over, 1, "one over-budget step");
    check_eq_u64(out.over_budget_steps, 1, "counted");
    if (over == 1) {
        const nfsu::TrackResidencyStep& step = out.steps[first_over];
        check_eq_u64(step.unplaced_bytes, 5000, "the section that did not fit");
        // All three were last requested when A was entered: table order.
        check_text(numbers(corridor, step.releases), "101 111 102",
                   "everything unrequested released first");
        check_text(numbers(corridor, step.loads), "104", "what fits still loads");
        check(!step.set_loaded && out.steps[first_over + 1].set_loaded &&
                  out.steps[first_over + 1].loads.empty(),
              "the set is given up as loaded on the next step");
    }
    check(out.resident[record_of(corridor, 114)] == 0, "114 never loads");
    check_text(committed_keys(out).substr(0, 7), "101 101", "commits start in A");
    check(out.steps.back().committed_key == 101, "the run goes on after it");

    // Without the look-ahead and the three-sample rule in the way: the same
    // section in the start set is refused at the start.
    costs = nfsu::track_residency_table_costs(corridor.table);
    costs[record_of(corridor, 2603)].place_bytes = 100000;
    costs[record_of(corridor, 2603)].held_bytes = 100000;
    check(nfsu::simulate_track_residency(corridor.table, corridor.zones, kTrack, costs, {},
                                         plain_options(2600), out, error),
          "an over-budget start set still finishes: " + error);
    check(out.start_loaded && out.resident[record_of(corridor, 2603)] == 0 &&
              out.over_budget_steps >= 1,
          "the start set gives 2603 up");
}

// One call of 0x00444EE0 stops once it has released more than the limit;
// the next step goes on where it stopped.
void test_release_cap() {
    const Corridor corridor = make_corridor();
    std::vector<nfsu::TrackResidencySample> samples;
    repeat(samples, 50.0F, 4);
    repeat(samples, 350.0F, 8);  // D needs 104 and 114: 600 bytes
    nfsu::TrackResidencyOptions options = plain_options(2300);
    options.release_pass_limit = 250;
    nfsu::TrackResidencyRun out;
    if (!run(corridor, samples, options, out)) {
        return;
    }
    check(out.release_capped_steps >= 1, "a capped step");
    std::string capped;
    for (const nfsu::TrackResidencyStep& step : out.steps) {
        if (step.release_capped) {
            capped += numbers(corridor, step.releases) + ";";
            check(step.loads.empty(), "a capped step starts no read");
        }
    }
    // The limit is tested before each release (0x00444F00), so the first
    // release goes ahead with 0 released and carries the call to 300, past
    // the limit of 250 by one whole record; the next test gives the step up.
    // The next step's call starts its count again, releases one more and
    // fits both.
    check_text(capped, "101;", "one release, then the step is given up");
    check_text(history(corridor, out, true), "-101 -111 +104 +114",
               "the releases finish on the next step");
}

// The retail allocator's two constants: a section is placed when its byte
// count plus 0x400 is at most the free room less 0x58.
void test_placement_boundary() {
    const Corridor corridor = make_corridor();
    nfsu::TrackResidencyOptions options;
    // Y0 and X0 in the first pass: 500 + 0x400 and then 1000 + 0x400 must
    // fit beside Y0's 500 in flight.
    const std::uint64_t exact = 500 + 1000 + 0x400 + 0x58;
    options.budget_bytes = exact;
    nfsu::TrackResidencyRun out;
    if (run(corridor, {}, options, out)) {
        check(out.steps[0].loads.size() == 2, "X0 fits at the exact boundary");
    }
    options.budget_bytes = exact - 1;
    if (run(corridor, {}, options, out)) {
        // X0 is skipped, nothing can be released, and the pass goes on to
        // place Z0 beside Y0.
        check(numbers(corridor, out.steps[0].loads) == "2500 2600" &&
                  out.steps[0].over_budget && out.steps[0].unplaced_bytes == 1000,
              "one byte less and X0 is given up: " +
                  numbers(corridor, out.steps[0].loads));
    }
}

void test_refusals() {
    const Corridor corridor = make_corridor();
    const std::vector<nfsu::TrackResidencyCost> costs =
        nfsu::track_residency_table_costs(corridor.table);
    nfsu::TrackResidencyRun out;
    std::string error;
    nfsu::TrackStreamTable bad_table = corridor.table;
    bad_table.valid = false;
    check(!nfsu::simulate_track_residency(bad_table, corridor.zones, kTrack, costs, {},
                                          plain_options(1000), out, error) &&
              error.find("table") != std::string::npos,
          "an invalid table");
    nfsu::TrackZoneReport bad_zones = corridor.zones;
    bad_zones.valid = false;
    check(!nfsu::simulate_track_residency(corridor.table, bad_zones, kTrack, costs, {},
                                          plain_options(1000), out, error) &&
              error.find("zone") != std::string::npos,
          "an invalid zone report");
    std::vector<nfsu::TrackResidencyCost> short_costs(costs.begin(), costs.end() - 1);
    check(!nfsu::simulate_track_residency(corridor.table, corridor.zones, kTrack,
                                          short_costs, {}, plain_options(1000), out,
                                          error) &&
              error.find("costs hold") != std::string::npos,
          "a cost per record");
    nfsu::TrackResidencyOptions zero = plain_options(1000);
    zero.placements_per_pass = 0;
    check(!nfsu::simulate_track_residency(corridor.table, corridor.zones, kTrack, costs,
                                          {}, zero, out, error),
          "zero placements a pass");
    std::vector<nfsu::TrackResidencyCost> inverted = costs;
    inverted[3].held_bytes = inverted[3].place_bytes + 1;
    check(!nfsu::simulate_track_residency(corridor.table, corridor.zones, kTrack,
                                          inverted, {}, plain_options(100000), out,
                                          error) &&
              error.find("holds") != std::string::npos,
          "a record that holds more than it places");
    nfsu::TrackResidencyOptions slow = plain_options(100000);
    slow.max_start_steps = 3;
    check(!nfsu::simulate_track_residency(corridor.table, corridor.zones, kTrack, costs,
                                          {}, slow, out, error) &&
              error.find("did not finish") != std::string::npos,
          "a start set that takes too long");
    slow.max_start_steps = 8;
    check(!nfsu::simulate_track_residency(corridor.table, corridor.zones, kTrack, costs,
                                          {}, slow, out, error),
          "eight steps are not enough");
    slow.max_start_steps = 9;
    check(nfsu::simulate_track_residency(corridor.table, corridor.zones, kTrack, costs,
                                         {}, slow, out, error),
          "nine steps are enough");
}

// 0x00444D5B stops listing at 256: the 257th new record is marked requested
// and not listed, and the walk ends there.
void test_request_limit() {
    const Corridor corridor = make_corridor(true, 300);
    check(corridor.table.valid, "the long table parses");
    nfsu::TrackResidencyRun out;
    if (!run(corridor, {}, plain_options(100000000), out)) {
        return;
    }
    const nfsu::TrackResidencyStep& last = out.steps.back();
    check_eq_u64(last.requested, 256, "requests stop at 256");
    check_eq_u64(last.resident, 256, "256 load");
    // Fixed four, 2603, the corridor's three flagged records, then 3000 on:
    // the 256th listed is 3247 and 3248 is the one marked and not listed.
    check(out.resident[record_of(corridor, 3247)] == 1 &&
              out.resident[record_of(corridor, 3248)] == 0 &&
              out.resident[record_of(corridor, 3299)] == 0,
          "the 256th loads and the 257th does not");
}

// The release limit is tested before each release and is exclusive: a call
// that has released exactly the limit goes on and releases one more record,
// so one call can release the limit plus a record (0x00444F00 / 0x00444F06
// `jg`). A limit of 0 still releases one record a call.
void test_release_cap_boundary() {
    const Corridor corridor = make_corridor();
    std::vector<nfsu::TrackResidencySample> samples;
    repeat(samples, 50.0F, 4);
    repeat(samples, 350.0F, 8);
    nfsu::TrackResidencyOptions options = plain_options(2300);
    options.release_pass_limit = 300;
    nfsu::TrackResidencyRun out;
    if (!run(corridor, samples, options, out)) {
        return;
    }
    check_eq_u64(out.release_capped_steps, 0, "300 released of a limit of 300 is not capped");
    std::string one_step;
    for (const nfsu::TrackResidencyStep& step : out.steps) {
        if (!step.releases.empty()) {
            one_step = numbers(corridor, step.releases) + " / " +
                       numbers(corridor, step.loads);
        }
    }
    check_text(one_step, "101 111 / 104 114",
               "one call releases 600 bytes against a limit of 300");
    options.release_pass_limit = 299;
    if (run(corridor, samples, options, out)) {
        check_eq_u64(out.release_capped_steps, 1, "299 caps after the first release");
    }
    options.release_pass_limit = 0;
    if (run(corridor, samples, options, out)) {
        std::string capped;
        for (const nfsu::TrackResidencyStep& step : out.steps) {
            if (step.release_capped) {
                capped += numbers(corridor, step.releases) + ";";
            }
        }
        check_text(capped, "101;", "a limit of 0 still releases one record a call");
        check_text(history(corridor, out, true), "-101 -111 +104 +114",
                   "a limit of 0: the second release on the next step");
    }
}

// Review comment 3630, F3a: the release order by stamp, where one release is
// enough and the candidates' stamps differ. After A then B, the unrequested
// loaded sections in C are 111, last requested when A was entered, and 101,
// 102 and 112, last requested when B was. 0x00444780 releases the smallest
// stamp: 111, not 101, which is first in table order among the rest.
void test_release_prefers_least_recent() {
    const Corridor corridor = make_corridor();
    std::vector<nfsu::TrackResidencySample> samples;
    repeat(samples, 50.0F, 4);   // A
    repeat(samples, 150.0F, 6);  // B: 112 and 103 fit beside the start set
    repeat(samples, 250.0F, 6);  // C: 113 fits, 104 needs one release
    nfsu::TrackResidencyRun out;
    if (!run(corridor, samples, plain_options(3200), out)) {
        return;
    }
    check_text(history(corridor, out, true), "+112 +103 -111 +113 +104",
               "the least recently requested goes, not the first in table order");
    check_eq_u64(out.releases, 1, "one release is enough");
    check_text(resident_numbers(corridor, out),
               "2500 2400 2600 2499 2603 101 102 112 103 113 104", "resident at the end");
}

// Review comment 3630, F3b: 0x00444F20 `jg` counts a release that freed no
// bytes as no release at all. The zero-byte section is released, and the
// call then gives the request up (0x00444F23 -> 0x00444830 -> 0x00444F48)
// although 101 could have been released next and would have made room.
void test_zero_byte_release_gives_up() {
    const Corridor corridor = make_corridor();
    std::vector<nfsu::TrackResidencySample> samples;
    repeat(samples, 50.0F, 4);   // A
    repeat(samples, 150.0F, 6);  // B
    repeat(samples, 250.0F, 6);  // C
    std::vector<nfsu::TrackResidencyCost> costs =
        nfsu::track_residency_table_costs(corridor.table);
    costs[record_of(corridor, 111)].place_bytes = 0;
    costs[record_of(corridor, 111)].held_bytes = 0;
    nfsu::TrackResidencyRun out;
    std::string error;
    check(nfsu::simulate_track_residency(corridor.table, corridor.zones, kTrack, costs,
                                         samples, plain_options(2900), out, error),
          "the run: " + error);
    check_text(history(corridor, out, true), "+112 +103 -111 +113",
               "the zero-byte release ends the call, and 104 is given up");
    std::size_t over = 0;
    for (const nfsu::TrackResidencyStep& step : out.steps) {
        if (step.over_budget) {
            ++over;
            check(step.on_path && step.unplaced_bytes == 300 &&
                      numbers(corridor, step.releases) == "111",
                  "the step that gives 104 up");
        }
    }
    check_eq_u64(over, 1, "one step gives up");
    check(out.resident[record_of(corridor, 101)] == 1 &&
              out.resident[record_of(corridor, 104)] == 0,
          "101 stays and 104 never loads");
}


void test_no_route_record() {
    const Corridor corridor = make_corridor(false);
    check(corridor.zones.valid, "the corridor without a route parses");
    std::vector<nfsu::TrackResidencySample> samples;
    samples.push_back(moving(280.0F, 20.0F));
    nfsu::TrackResidencyRun out;
    if (run(corridor, samples, plain_options(100000), out)) {
        check_eq_i64(out.steps.back().sampled_key, 104,
                     "no route record: the look-ahead takes D");
    }
}

// The step line and the hash over it.
void test_sequence_hash() {
    const Corridor corridor = make_corridor();
    std::vector<nfsu::TrackResidencySample> samples;
    repeat(samples, 50.0F, 4);
    repeat(samples, 150.0F, 6);
    repeat(samples, 250.0F, 6);
    nfsu::TrackResidencyRun first;
    nfsu::TrackResidencyRun second;
    if (!run(corridor, samples, plain_options(2600), first) ||
        !run(corridor, samples, plain_options(2600), second)) {
        return;
    }
    const std::string a =
        nfsu::sha256_hex(nfsu::track_residency_sequence_sha256(corridor.table, first));
    const std::string b =
        nfsu::sha256_hex(nfsu::track_residency_sequence_sha256(corridor.table, second));
    check_text(a, b, "the same path gives the same sequence");
    check_text(nfsu::track_residency_step_line(corridor.table, first.steps[0]),
               "step 0 start sample 0 key -1 zone -1 rebuild 1 placed 1 loads 2500,2400 "
               "releases - unplaced 0 over 0 capped 0 loaded 0 requested 8 resident 2 "
               "held 1100",
               "the first step line");
    // C commits on its third sample, sample 12, which is step 9 + 12.
    check_eq_u64(first.steps.size(), 25, "nine start steps and sixteen samples");
    if (first.steps.size() == 25) {
        check_text(nfsu::track_residency_step_line(corridor.table, first.steps[21]),
                   "step 21 path sample 12 key 103 zone 103 rebuild 1 placed 1 loads "
                   "113,104 releases 101,102 unplaced 0 over 0 capped 0 loaded 0 "
                   "requested 8 resident 9 held 2600",
                   "the step that enters C");
    }
    check_text(a, "DCDF801F6A22EF87F278599035B80197ABCE57185DC121C6544033A18CD84F20",
               "the pinned sequence hash");
    // Standing elsewhere in C changes nothing the sequence records; standing
    // in D does.
    samples.back() = at(260.0F);
    nfsu::TrackResidencyRun same;
    if (run(corridor, samples, plain_options(2600), same)) {
        check_text(nfsu::sha256_hex(nfsu::track_residency_sequence_sha256(
                       corridor.table, same)),
                   a, "another point of the same zone gives the same sequence");
    }
    samples.back() = at(350.0F);
    nfsu::TrackResidencyRun moved;
    if (run(corridor, samples, plain_options(2600), moved)) {
        check(nfsu::sha256_hex(nfsu::track_residency_sequence_sha256(corridor.table,
                                                                     moved)) != a,
              "a different path gives a different hash");
    }
}

// ---------------------------------------------------------------------------
// The square root (review comment 3630, F1)
// ---------------------------------------------------------------------------

double double_of(std::uint64_t bits) {
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint64_t bits_of(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// 64 x 64 -> 128 bits, as (high, low), from 32-bit halves.
void multiply_wide(std::uint64_t a, std::uint64_t b, std::uint64_t& high,
                   std::uint64_t& low) {
    const std::uint64_t a0 = a & 0xFFFFFFFFU;
    const std::uint64_t a1 = a >> 32U;
    const std::uint64_t b0 = b & 0xFFFFFFFFU;
    const std::uint64_t b1 = b >> 32U;
    const std::uint64_t p00 = a0 * b0;
    const std::uint64_t p01 = a0 * b1;
    const std::uint64_t p10 = a1 * b0;
    const std::uint64_t p11 = a1 * b1;
    const std::uint64_t middle = (p00 >> 32U) + (p01 & 0xFFFFFFFFU) + (p10 & 0xFFFFFFFFU);
    low = (p00 & 0xFFFFFFFFU) | (middle << 32U);
    high = p11 + (p01 >> 32U) + (p10 >> 32U) + (middle >> 32U);
}

// -1, 0 or 1 as a * 2^sa compares with b * 2^sb, for 128-bit a and b given as
// (high, low) and shifts that keep both within 128 bits.
int compare_scaled(std::uint64_t ah, std::uint64_t al, int sa, std::uint64_t bh,
                   std::uint64_t bl, int sb) {
    const int common = sa < sb ? sa : sb;
    auto shift_left = [](std::uint64_t& h, std::uint64_t& l, int by) {
        for (int k = 0; k < by; ++k) {
            h = (h << 1U) | (l >> 63U);
            l <<= 1U;
        }
    };
    shift_left(ah, al, sa - common);
    shift_left(bh, bl, sb - common);
    if (ah != bh) {
        return ah < bh ? -1 : 1;
    }
    if (al != bl) {
        return al < bl ? -1 : 1;
    }
    return 0;
}

// True when y is sqrt(x) rounded to nearest, checked exactly: x lies strictly
// between the squares of the two midpoints around y. x and y positive,
// normal, and not so far apart in exponent that the shifts overflow (the
// callers stay within [2^-200, 2^200]).
bool is_correctly_rounded_root(double x, double y) {
    const std::uint64_t xb = bits_of(x);
    const std::uint64_t yb = bits_of(y);
    const std::uint64_t xm = (xb & ((std::uint64_t{1} << 52) - 1U)) | (std::uint64_t{1} << 52);
    const int xe = static_cast<int>((xb >> 52) & 0x7FFU) - 1075;
    const std::uint64_t ym = (yb & ((std::uint64_t{1} << 52) - 1U)) | (std::uint64_t{1} << 52);
    const int ye = static_cast<int>((yb >> 52) & 0x7FFU) - 1075;
    // Midpoints (2ym - 1) * 2^(ye-1) and (2ym + 1) * 2^(ye-1); below a power
    // of two the lower neighbour is half as far: (4ym - 1) * 2^(ye-2).
    std::uint64_t lh = 0;
    std::uint64_t ll = 0;
    int ls = 0;
    if (ym == (std::uint64_t{1} << 52)) {
        multiply_wide(4 * ym - 1, 4 * ym - 1, lh, ll);
        ls = 2 * (ye - 2);
    } else {
        multiply_wide(2 * ym - 1, 2 * ym - 1, lh, ll);
        ls = 2 * (ye - 1);
    }
    std::uint64_t uh = 0;
    std::uint64_t ul = 0;
    multiply_wide(2 * ym + 1, 2 * ym + 1, uh, ul);
    const int us = 2 * (ye - 1);
    // x as a 128-bit number shifted to meet each bound: compare x * 2^xe.
    const int lower = compare_scaled(0, xm, xe, lh, ll, ls);
    const int upper = compare_scaled(0, xm, xe, uh, ul, us);
    return lower > 0 && upper < 0;
}

void test_sqrt() {
    // Exact squares, both signs of zero, infinity, a negative and a NaN.
    check(nfsu::track_residency_sqrt(4.0) == 2.0 && nfsu::track_residency_sqrt(1.0) == 1.0 &&
              nfsu::track_residency_sqrt(0.25) == 0.5 &&
              nfsu::track_residency_sqrt(1004004.0) == 1002.0,
          "exact squares");
    check(bits_of(nfsu::track_residency_sqrt(0.0)) == 0 &&
              bits_of(nfsu::track_residency_sqrt(-0.0)) == (std::uint64_t{1} << 63),
          "both zeros keep their sign");
    const double infinity = std::numeric_limits<double>::infinity();
    check(nfsu::track_residency_sqrt(infinity) == infinity, "infinity");
    const double negative = nfsu::track_residency_sqrt(-1.0);
    const double nan = nfsu::track_residency_sqrt(std::numeric_limits<double>::quiet_NaN());
    check(negative != negative && nan != nan, "a negative and a NaN give a NaN");
    // Bit patterns of correctly rounded roots, from a correctly rounded
    // reference (Python's math.sqrt over the hardware instruction).
    check_eq_u64(bits_of(nfsu::track_residency_sqrt(2.0)), 0x3FF6A09E667F3BCDULL, "sqrt 2");
    check_eq_u64(bits_of(nfsu::track_residency_sqrt(3.0)), 0x3FFBB67AE8584CAAULL, "sqrt 3");
    check_eq_u64(bits_of(nfsu::track_residency_sqrt(1e-300)), 0x20CA2FE76A3F9475ULL,
                 "sqrt 1e-300");
    check_eq_u64(bits_of(nfsu::track_residency_sqrt(double_of(1))), 0x1E60000000000000ULL,
                 "sqrt of the smallest subnormal is 2^-537");
    check_eq_u64(bits_of(nfsu::track_residency_sqrt(std::numeric_limits<double>::max())),
                 0x5FEFFFFFFFFFFFFFULL, "sqrt of the largest double");
    // The review's case: 1000^2 + 69^2 = 1004761. MinGW's libm gave
    // 1002.3776733347565 (0x408F53057998EACE); the correctly rounded root,
    // which sqrtsd gives, is 0x408F53057998EACF.
    check_eq_u64(bits_of(nfsu::track_residency_sqrt(1004761.0)), 0x408F53057998EACFULL,
                 "sqrt 1004761, the review's segment");
    check(is_correctly_rounded_root(1004761.0, nfsu::track_residency_sqrt(1004761.0)) &&
              !is_correctly_rounded_root(1004761.0, double_of(0x408F53057998EACEULL)),
          "the exact check tells the two apart");
    // Many values, each checked exactly: a linear congruential walk over the
    // bits of doubles in [2^-200, 2^200].
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    int wrong = 0;
    for (int k = 0; k < 200000; ++k) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::uint64_t fraction = state >> 12U;
        const std::uint64_t biased = 823 + ((state >> 3U) % 400U);
        const double x = double_of((biased << 52U) | fraction);
        if (!is_correctly_rounded_root(x, nfsu::track_residency_sqrt(x))) {
            ++wrong;
        }
    }
    check_eq_u64(static_cast<std::uint64_t>(wrong), 0, "200000 roots, each exact");
    // Every float root agrees with its double root narrowed, and a few by bits.
    check_eq_u64(float_bits(nfsu::track_residency_sqrtf(2.0F)), 0x3FB504F3U, "sqrtf 2");
    check_eq_u64(float_bits(nfsu::track_residency_sqrtf(900.0F)), 0x41F00000U, "sqrtf 900");
    check_eq_u64(float_bits(nfsu::track_residency_sqrtf(1e-40F)), 0x1E3CE4E7U,
                 "sqrtf of a subnormal float");
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

void test_path_parse() {
    std::vector<nfsu::TrackResidencyVertex> vertices;
    std::string error;
    check(nfsu::parse_track_residency_path(
              "# a comment\r\n\r\n  0 0 1\r\n\t10 0 2 \r\n# end\r\n10 5.5 -3e1\n",
              vertices, error),
          "a path with comments, blank lines, CR LF and tabs: " + error);
    check(vertices.size() == 3 && vertices[1].slot[0] == 10.0 &&
              vertices[1].slot[2] == 2.0 && vertices[2].slot[1] == 5.5 &&
              vertices[2].slot[2] == -30.0,
          "the three vertices");
    check(nfsu::parse_track_residency_path("0 0 0\n3 4 0", vertices, error),
          "no final newline");
    // CMake's file(WRITE) on Windows turns a written CR LF into CR CR LF.
    check(nfsu::parse_track_residency_path("0 0 0\r\r\n3\r4 0\r\r\n", vertices,
                                           error) &&
              vertices.size() == 2 && vertices[1].slot[1] == 4.0,
          "carriage returns separate fields: " + error);
    check(std::fabs(nfsu::track_residency_path_length(vertices) - 5.0) < 1e-12,
          "length 5");
    const struct {
        const char* text;
        const char* needle;
    } refusals[] = {
        {"0 0\n1 1 1\n", "line 1 has 2 fields"},
        {"0 0 0\n1 1 1 1\n", "line 2 has 4 fields"},
        {"0 0 0\n1 x 1\n", "line 2 field 2"},
        {"0 0 0\n1 inf 1\n", "line 2 field 2"},
        {"0 0 0\n1 nan 1\n", "line 2 field 2"},
        {"0 0 0\n0x10 0 1\n", "line 2 field 1"},
        {"0 0 0\n1 1 1e\n", "line 2 field 3"},
        {"0 0 0\n1 1 .\n", "line 2 field 3"},
        {"0 0 0\n1 1 1000001\n", "line 2 field 3"},
        {"0 0 0\n1 1 -1000001\n", "line 2 field 3"},
        {"0 0 0\n", "at least two vertices, and this one has 1"},
        {"# only a comment\n", "and this one has 0"},
        {"5 5 0\n5 5 9\n", "no horizontal length"},
    };
    for (const auto& refusal : refusals) {
        error.clear();
        check(!nfsu::parse_track_residency_path(refusal.text, vertices, error) &&
                  error.find(refusal.needle) != std::string::npos,
              std::string("refused: ") + refusal.needle + " (got '" + error + "')");
    }
    check(nfsu::parse_track_residency_path("0 0 0\n1 1 -1000000\n", vertices, error),
          "the coordinate bound is inclusive");
    nfsu::TrackResidencyPathLimits limits;
    limits.max_vertices = 2;
    check(nfsu::parse_track_residency_path("0 0 0\n1 0 0\n", vertices, error, limits),
          "two vertices under a limit of two");
    check(!nfsu::parse_track_residency_path("0 0 0\n1 0 0\n2 0 0\n", vertices, error,
                                            limits) &&
              error.find("more than 2 vertices") != std::string::npos,
          "three vertices over a limit of two");
}

void test_path_sampling() {
    std::vector<nfsu::TrackResidencyVertex> vertices;
    std::string error;
    // 30 along slot 0, a repeated vertex, then 40 along slot 1.
    check(nfsu::parse_track_residency_path("0 0 0\n30 0 3\n30 0 3\n30 40 7\n",
                                           vertices, error),
          "an L");
    std::vector<nfsu::TrackResidencySample> samples;
    std::vector<nfsu::TrackResidencyPathPoint> points;
    // 60 units a second at 60 ticks: one unit a step, 70 units, 71 samples.
    check(nfsu::sample_track_residency_path(vertices, 60.0, 60, samples, points, error),
          "sampling: " + error);
    check_eq_u64(samples.size(), 71, "samples");
    if (samples.size() == 71) {
        check(samples[0].position[0] == 0.0F && samples[0].velocity[0] == 60.0F &&
                  samples[0].velocity[1] == 0.0F,
              "the first sample");
        check(samples[29].position[0] == 29.0F && samples[29].velocity[0] == 60.0F,
              "sample 29 is still on the first leg");
        check(samples[30].position[0] == 30.0F && samples[30].position[1] == 0.0F &&
                  samples[30].velocity[1] == 60.0F && samples[30].velocity[0] == 0.0F,
              "sample 30 starts the second leg; the repeated vertex is skipped");
        check(samples[70].position[1] == 40.0F && points[70].distance == 70.0,
              "the last sample is the end");
        check(std::fabs(points[15].slot[2] - 1.5) < 1e-12 &&
                  std::fabs(points[50].slot[2] - 5.0) < 1e-12,
              "heights are interpolated");
        check(points[40].direction[1] == 1.0, "the direction of travel");
    }
    // A length that is not a whole number of steps: the last sample is the
    // end, closer than a step.
    check(nfsu::sample_track_residency_path(vertices, 40.0, 60, samples, points, error),
          "40 a second");
    check_eq_u64(samples.size(), 106, "ceil(70 / (40 / 60)) + 1");
    if (!samples.empty()) {
        check(samples.back().position[1] == 40.0F, "the end");
    }
    check(!nfsu::sample_track_residency_path(vertices, 0.0, 60, samples, points, error),
          "speed 0");
    check(!nfsu::sample_track_residency_path(vertices, -1.0, 60, samples, points, error),
          "a negative speed");
    check(!nfsu::sample_track_residency_path(vertices, std::nan(""), 60, samples, points,
                                             error),
          "a NaN speed");
    check(!nfsu::sample_track_residency_path(
              vertices, std::numeric_limits<double>::infinity(), 60, samples, points,
              error),
          "an infinite speed");
    check(!nfsu::sample_track_residency_path(vertices, 60.0, 0, samples, points, error),
          "zero ticks");
    nfsu::TrackResidencyPathLimits limits;
    limits.max_samples = 71;
    check(nfsu::sample_track_residency_path(vertices, 60.0, 60, samples, points, error,
                                            limits),
          "71 samples under a limit of 71");
    limits.max_samples = 70;
    check(!nfsu::sample_track_residency_path(vertices, 60.0, 60, samples, points, error,
                                             limits),
          "71 samples over a limit of 70");
}

// Review comment 3630, F1: this path's one segment is 1000 by 69, whose
// length MinGW's libm rounded down by one ulp at -O0 while -O3 inlined
// sqrtsd, and at this speed that ulp decided the sample count: 401 in the
// default and Debug builds, 402 in Release. The correctly rounded length is
// the larger, and ceil(1002.3776733347567 / (150.35665100021347 / 60)) is
// 401, so there are 402 samples in every build now.
void test_path_is_build_independent() {
    std::vector<nfsu::TrackResidencyVertex> vertices;
    std::string error;
    check(nfsu::parse_track_residency_path("0 0 0\n1000 69 0\n", vertices, error),
          "the review's path parses");
    const double length = nfsu::track_residency_path_length(vertices);
    check_eq_u64(bits_of(length), 0x408F53057998EACFULL,
                 "its length is 1002.3776733347567, correctly rounded");
    std::vector<nfsu::TrackResidencySample> samples;
    std::vector<nfsu::TrackResidencyPathPoint> points;
    check(nfsu::sample_track_residency_path(vertices, 150.35665100021347, 60, samples,
                                            points, error),
          "it samples");
    check_eq_u64(samples.size(), 402, "402 samples in every build");
    if (samples.size() == 402) {
        check(points[401].distance == length && samples[401].position[0] == 1000.0F,
              "the last sample is the end");
        check(points[400].distance < length, "the one before it is not");
    }
}

void test_hand_path() {
    std::vector<nfsu::TrackResidencyVertex> vertices;
    std::string error;
    check(nfsu::parse_track_residency_path(nfsu::kTrackResidencyHandPath1003, vertices,
                                           error),
          "the hand path parses: " + error);
    check_eq_u64(vertices.size(), 68, "hand path vertices");
    if (vertices.size() == 68) {
        check(vertices.front().slot[0] == vertices.back().slot[0] &&
                  vertices.front().slot[1] == vertices.back().slot[1],
              "the hand path is closed");
        check(vertices[0].slot[0] == 930.0 && vertices[0].slot[1] == -212.0,
              "it starts in zone 1003");
    }
    const double length = nfsu::track_residency_path_length(vertices);
    check(std::fabs(length - 3411.3099650412996) < 1e-6,
          "hand path length: " + std::to_string(length));
}

} // namespace

int main() {
    test_constants();
    test_corridor_parses();
    test_slot_key();
    test_start_set();
    test_commit_needs_three_samples();
    test_enter_leave_revisit();
    test_release_order();
    test_over_budget();
    test_release_cap();
    test_placement_boundary();
    test_refusals();
    test_request_limit();
    test_release_cap_boundary();
    test_release_prefers_least_recent();
    test_zero_byte_release_gives_up();
    test_no_route_record();
    test_sequence_hash();
    test_path_parse();
    test_path_sampling();
    test_path_is_build_independent();
    test_hand_path();
    test_sqrt();
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed of " << g_checks << '\n';
        return 1;
    }
    std::cout << "all track residency tests passed (" << g_checks << " checks)\n";
    return 0;
}
