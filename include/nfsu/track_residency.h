#pragma once

#include "nfsu/sha256.h"
#include "nfsu/track_stream.h"
#include "nfsu/track_zones.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

namespace nfsu {

// Section residency along a path: which sections of a track's stream are
// requested, loaded and released while one car drives a path, step by step,
// within a byte budget.
//
// speed.exe runs one streamer (the object at 0x0077A980) once per frame
// (0x00447A06 -> 0x00445050 -> 0x00445180). It tracks up to two positions -
// the two human players' cars, copied in every frame by 0x0048E985..
// 0x0048E9FE - turns each into a zone key with a look-ahead along the car's
// velocity (0x00444870), commits a new key only after it has been sampled
// three times in a row (0x00444FB0), and on a committed change rebuilds the
// list of sections it wants (0x004443E0, then 0x00444B50). It places the
// wanted sections that are not resident, at most two per pass, while they
// fit the pool (0x00444D80), and when one does not fit it releases loaded
// sections no current request names, least recently requested first
// (0x00444EE0, 0x00444780). docs/TRACK_STREAMING.md carries the disassembly
// behind every rule below, and says which parts of this model are not
// retail.
//
// This library is that streamer as a deterministic function of a table, a
// zone report, a per-record cost and one position per fixed step. It reads
// no file and no clock; the same inputs give the same sequence of loads and
// releases and the same per-step bytes, and track_residency_sequence_sha256
// pins the whole sequence.
//
// The same in every build, too: the step and the path sampling call no
// library mathematics at all. A square root is the one operation they need
// that IEEE 754 arithmetic does not spell directly, and a compiler is free to
// take it from the C library in one build and inline the hardware
// instruction in another; MinGW's libm sqrt is not correctly rounded, so the
// two disagreed (review comment 3630, F1). track_residency_sqrt below is
// correctly rounded by construction, in integer arithmetic, and every root
// here goes through it. The library is also compiled with floating-point
// contraction disabled, on the desktop and on the console, where a
// fused-instruction census checks its object (switch/CMakeLists.txt).

// ---------------------------------------------------------------------------
// The tracked slot: 0x00444870 and 0x00444FB0
// ---------------------------------------------------------------------------

// 0x0044502D `cmp edi, 2`: the streamer samples two slots. 0x0048E990..
// 0x0048E9F8 fill them from the two player records, so a single-player race
// has one live slot and the other reads kTrackResidencyNoZone.
constexpr std::size_t kTrackResidencySlots = 2;

// The key of a slot that is not live, or of a point in no accepted zone:
// 0x00444FD3 `or eax, 0xffffffff` and 0x00444914 `mov word ptr [...], 0xffff`.
constexpr std::int32_t kTrackResidencyNoZone = -1;

// 0x00444870 samples three points, 0x00444A66 `cmp ebp, 3`: the slot's own
// position and two look-ahead points at 0.5 and 1.0 of the look-ahead
// offset (0x00444910 `fild` the index, 0x0044491B `fmul` 0.5 at 0x006CC7DC).
constexpr std::size_t kTrackResidencyLookAheadPoints = 3;
constexpr float kTrackResidencyLookAheadFraction = 0.5F;

// The look-ahead offset is the velocity times 1.5 (0x006B781C), clamped to a
// length of 100 (0x006B7820) - 0x004448A5..0x004448E9 - and is zero when the
// speed is above 178.8109130859375 (0x006CCCA4, bits 0x4332CF98),
// 0x00444891 `fcomp` / 0x0044489C `jne`.
constexpr float kTrackResidencyLookAheadSeconds = 1.5F;
constexpr float kTrackResidencyLookAheadDistance = 100.0F;
constexpr float kTrackResidencyLookAheadMaxSpeed = 178.8109130859375F;

// Above this speed (0x006CCCA0, 17.88109016418457, bits 0x418F0C79)
// 0x0044493D asks the path structure at 0x007372B0 for a key before it
// asks the zone polygons. That structure is not decoded (issue #61), so
// this model never consults it: every key comes from the polygons. That is
// the one place track_residency_slot_key is knowingly not retail at race
// speeds. The constant is named so that the report can say which samples
// it applies to.
constexpr float kTrackResidencyRouteQuerySpeed = 17.88109016418457F;

// 0x00445013 `cmp dword ptr [esi], 3`: a sampled key replaces the committed
// one only once it has been sampled this many times in a row.
constexpr std::int32_t kTrackResidencyCommitCount = 3;
// 0x00444FFA: a slot whose count is -1 - which 0x00444840 stores when a car
// is placed, and 0x00446FA5 when the zones load - takes its first new key
// at once, as if it had been sampled 1000 times.
constexpr std::int32_t kTrackResidencyFirstSampleCount = 1000;

// ---------------------------------------------------------------------------
// Placement: 0x00444D80, 0x00444EE0, 0x00444780
// ---------------------------------------------------------------------------

// 0x00444E31 `cmp eax, 1` / `jg`: a placement pass stops after its second
// placement and marks the request deferred (+0x15F).
constexpr std::size_t kTrackResidencyPlacementsPerPass = 2;
// 0x00444DD7 `lea edi, [ecx + 0x400]`: a section is placed only when its
// byte count plus this fits...
constexpr std::uint64_t kTrackResidencyPlacementSlack = 0x400;
// ...the pool's largest free block (0x00565810) less this, 0x00444DC9
// `sub eax, 0x58`.
constexpr std::uint64_t kTrackResidencyBlockHeader = 0x58;
// 0x00444F00 and 0x00444F08 `cmp ..., 0x9c3c00` / `jg`: one call of
// 0x00444EE0 stops releasing, and gives the frame up, once it has released
// more than this many bytes. The test comes BEFORE each release, so a call
// that has released anything up to this many bytes still releases one more
// record of any size: one call can release this many bytes plus its largest
// record, and a limit of 0 still releases one.
constexpr std::uint64_t kTrackResidencyReleasePassLimit = 0x9C3C00;
// 0x006CC8B0: the counter 0x00444D47 stamps a request with advances 60
// times a second (0x00585D70). One step of this model is one such tick.
constexpr std::uint32_t kTrackResidencyTicksPerSecond = 60;
// 0x004451AF and 0x004452FB `cmp ..., 3`: at most this many reads are in
// flight at once.
constexpr std::size_t kTrackResidencyReadsInFlight = 3;

// ---------------------------------------------------------------------------
// One sample of the tracked car
// ---------------------------------------------------------------------------

struct TrackResidencySample {
    // Raw slot 0 and slot 1: the frame the zone polygons are stored in
    // (docs/TRACK_ZONES.md). 0x0048E9CE..0x0048E9D9 copy the car's +0x40 and
    // +0x44 here.
    float position[2] = {0.0F, 0.0F};
    // Units per second in the same two slots: the car's +0x1E0 and +0x1E4,
    // 0x0048E9DC..0x0048E9EB.
    float velocity[2] = {0.0F, 0.0F};
};

// What 0x00444870 saw at each of its three points.
struct TrackResidencyLookAhead {
    float points[kTrackResidencyLookAheadPoints][2] = {};
    // The zone each point lies in, found as 0x00446FD0 finds it, or
    // kTrackZoneNone.
    std::size_t zones[kTrackResidencyLookAheadPoints] = {
        kTrackZoneNone, kTrackZoneNone, kTrackZoneNone};
    // The key each point contributes: its zone's key when accepted, else
    // kTrackResidencyNoZone. The slot's own point accepts any zone
    // (0x00444A20); a look-ahead point only a zone the route list names
    // (0x00444A28..0x00444A4F).
    std::int32_t keys[kTrackResidencyLookAheadPoints] = {
        kTrackResidencyNoZone, kTrackResidencyNoZone, kTrackResidencyNoZone};
    // True when the speed is above kTrackResidencyRouteQuerySpeed, where
    // retail would have asked the path structure first.
    bool route_query_skipped = false;
    std::int32_t key = kTrackResidencyNoZone;
};

// 0x00444870 without the path structure. `route` is the 0x00034153 record
// keyed on the track (TRACK_WHOLE.md: the zones the loader lets a car's
// look-ahead enter), or null when the track has none, in which case a
// look-ahead point accepts any zone as retail does when +0x598 is null
// (0x00444A2E). The result is the key of the farthest point, counting out
// from the car, before the first point with no accepted key
// (0x00444A7B..0x00444A9C).
//
// Retail computes the offsets in x87 registers (fsqrt at 0x00444887) and
// this in float32, with contraction disabled and the speed's root taken by
// track_residency_sqrtf; the two can pick different zones for a point within
// rounding of a polygon edge.
std::int32_t track_residency_slot_key(
    const TrackZoneReport& zones,
    const TrackZoneSectionList* route,
    const TrackResidencySample& sample,
    TrackResidencyLookAhead* detail = nullptr) noexcept;

// ---------------------------------------------------------------------------
// A square root that is the same in every build
// ---------------------------------------------------------------------------

// The square root of x rounded to the nearest double, ties to even - the
// result IEEE 754 requires of sqrt - computed with integer arithmetic only:
// no library call, no hardware square root, no floating-point operation. So
// it is the same in a build that would have called the C library and in one
// that would have inlined sqrtsd. +0 and -0 give themselves, +infinity gives
// itself, and a negative number or a NaN gives a quiet NaN.
double track_residency_sqrt(double x) noexcept;

// The same for a float: track_residency_sqrt of the float widened to double,
// then narrowed. Rounding twice is harmless here because 53 >= 2 * 24 + 2
// (Figueroa: a square root rounded to p' >= 2p + 2 bits and then to p bits is
// the square root rounded once to p bits).
float track_residency_sqrtf(float x) noexcept;

// ---------------------------------------------------------------------------
// The streamer
// ---------------------------------------------------------------------------

enum class TrackResidencyPolicy {
    // The rules traced from speed.exe, with the deviations
    // docs/TRACK_STREAMING.md lists.
    retail,
    // NOT retail: the request never carries the track byte, and every
    // rebuild releases at once every loaded section the new request does not
    // name. The resident set is then exactly the current request, which is
    // the least a zone-driven streamer can keep; it is a bound, not a rule.
    zone_only,
};

// "retail" or "zone-only". Stable.
const char* track_residency_policy_name(TrackResidencyPolicy policy) noexcept;

// What one record costs, in whatever currency the budget is in.
struct TrackResidencyCost {
    // What must be free to place the record, and what it occupies while its
    // read is in flight: retail's byte count, +24 (0x00444DD4).
    std::uint64_t place_bytes = 0;
    // What it occupies once loaded: retail's kept bytes, +28.
    std::uint64_t held_bytes = 0;
};

// The table's own currency: place_bytes is the byte count read (+24) and
// held_bytes the bytes kept once loaded (+28), for every record in table
// order. That a section shrinks to its kept bytes once loaded is inferred
// from the pool 0x00444AF0 sizes, not traced (docs/TRACK_STREAMING.md).
std::vector<TrackResidencyCost> track_residency_table_costs(
    const TrackStreamTable& table);

struct TrackResidencyOptions {
    TrackResidencyPolicy policy = TrackResidencyPolicy::retail;
    // The pool every placement is checked against. Retail's is
    // track_stream_pool_bytes(table).
    std::uint64_t budget_bytes = 0;
    std::uint64_t placement_slack = kTrackResidencyPlacementSlack;
    std::uint64_t block_header = kTrackResidencyBlockHeader;
    // At least 1.
    std::size_t placements_per_pass = kTrackResidencyPlacementsPerPass;
    std::uint64_t release_pass_limit = kTrackResidencyReleasePassLimit;
    // The steps the start set may take to load before the path begins;
    // simulate_track_residency fails past it rather than looping.
    std::size_t max_start_steps = 65536;
};

struct TrackResidencyStep {
    // 0-based over the whole run: the start steps first, then one step per
    // path sample.
    std::size_t index = 0;
    // False for the steps that load the start set before the car is placed.
    bool on_path = false;
    std::size_t sample = 0;
    // Slot 0: what 0x00444870 returned this step, and the key committed
    // after it. kTrackResidencyNoZone off the path.
    std::int32_t sampled_key = kTrackResidencyNoZone;
    std::int32_t committed_key = kTrackResidencyNoZone;
    // The request was rebuilt this step (0x004451CD..0x0044526F).
    bool rebuilt = false;
    // A placement pass ran this step (0x00444EE0).
    bool placed = false;
    // Record indices whose read completed this step, in the order they were
    // placed (docs/TRACK_STREAMING.md: retail starts reads in another order,
    // which changes nothing while every placed read starts at once).
    std::vector<std::size_t> loads;
    // Record indices released this step, in the order they were released.
    std::vector<std::size_t> releases;
    // Place bytes of requested records the last pass of this step could not
    // place.
    std::uint64_t unplaced_bytes = 0;
    // Nothing was left to release and a requested record still did not fit:
    // 0x00444F48 stores 1 at +0x160. The record stays unloaded; nothing
    // crashes.
    bool over_budget = false;
    // 0x00444EE0 released more than release_pass_limit and gave the step up
    // (0x00444F42).
    bool release_capped = false;
    // +0x15D after the step: every request placed and read, or given up.
    bool set_loaded = false;
    std::size_t requested = 0;
    std::size_t resident = 0;
    // held_bytes over every loaded record after the step.
    std::uint64_t held_bytes = 0;
};

struct TrackResidencyRun {
    std::vector<TrackResidencyStep> steps;
    // Steps before the path, and whether the start set finished loading in
    // them.
    std::size_t start_steps = 0;
    bool start_loaded = false;
    std::uint64_t peak_held_bytes = 0;
    std::size_t peak_step = 0;
    std::uint64_t rebuilds = 0;
    std::uint64_t loads = 0;
    std::uint64_t releases = 0;
    std::uint64_t over_budget_steps = 0;
    std::uint64_t unplaced_steps = 0;
    std::uint64_t release_capped_steps = 0;
    // Path samples above kTrackResidencyRouteQuerySpeed.
    std::uint64_t route_query_samples = 0;
    // Per record, after the last step.
    std::vector<std::uint8_t> resident;
};

// Incremental equivalent of simulate_track_residency, for a live car.
// Borrowed table/zones must outlive this non-copyable session. Costs/options
// are snapshotted by start; callers may release those small input buffers.
// start returns the startup transitions; advance returns one path transition.
class TrackResidencySession {
public:
    TrackResidencySession();
    ~TrackResidencySession();
    bool start(const TrackStreamTable&, const TrackZoneReport&, std::uint32_t,
               const std::vector<TrackResidencyCost>&, const TrackResidencyOptions&,
               std::vector<TrackResidencyStep>& startup, std::string& error);
    bool advance(const TrackResidencySample&, TrackResidencyStep&, std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Runs the streamer: first the start steps - no live slot, a rebuild forced
// as 0x00446F9E forces one when the zones load - until the start set is
// loaded, then one step per sample with slot 0 live, placed as 0x00444840
// places a car. The table and the zone report must be valid, `costs` must
// hold one entry per table record, and options.placements_per_pass must be
// at least 1. Fails, with `error` set, on any of those or when the start
// set does not finish within options.max_start_steps.
bool simulate_track_residency(
    const TrackStreamTable& table,
    const TrackZoneReport& zones,
    std::uint32_t track,
    const std::vector<TrackResidencyCost>& costs,
    const std::vector<TrackResidencySample>& samples,
    const TrackResidencyOptions& options,
    TrackResidencyRun& run,
    std::string& error);

// One step as one ASCII line, with every field of TrackResidencyStep and
// the loads and releases by section number:
//
//   step 3 start sample 0 key -1 zone -1 rebuild 0 placed 1 loads 2600,2499
//   releases - unplaced 0 over 0 capped 0 loaded 0 requested 5 resident 4
//   held 1234
//
// (one line, no newline). Stable: the sequence hash is over these lines.
std::string track_residency_step_line(
    const TrackStreamTable& table, const TrackResidencyStep& step);

// SHA-256 of every step line of the run, each followed by '\n'.
Sha256Digest track_residency_sequence_sha256(
    const TrackStreamTable& table, const TrackResidencyRun& run);

// ---------------------------------------------------------------------------
// A path
// ---------------------------------------------------------------------------

struct TrackResidencyVertex {
    // Raw slot order: slot 2 is the height.
    double slot[3] = {0.0, 0.0, 0.0};
};

struct TrackResidencyPathLimits {
    std::size_t max_vertices = 100000;
    std::size_t max_samples = 10000000;
    double max_coordinate = 1000000.0;
};

// Parses a path: one vertex per line, three decimal numbers - slot 0, slot 1
// and slot 2 - separated by spaces, tabs or carriage returns. Blank lines and lines whose
// first non-blank character is '#' are skipped. Refused, with the line
// number: a line with another number of fields, a field that is not a
// finite decimal number or lies outside max_coordinate, and more than
// max_vertices vertices; then a path of fewer than two vertices or of zero
// horizontal length. CR LF and LF endings are both accepted.
bool parse_track_residency_path(
    const std::string& text,
    std::vector<TrackResidencyVertex>& vertices,
    std::string& error,
    const TrackResidencyPathLimits& limits = TrackResidencyPathLimits{});

// Where a path sample lies, in the frame a renderer draws it.
struct TrackResidencyPathPoint {
    double slot[3] = {0.0, 0.0, 0.0};
    // The unit direction of travel in slot 0 and slot 1.
    double direction[2] = {1.0, 0.0};
    double distance = 0.0;
};

// Samples a path at a constant horizontal speed, one sample per tick of
// ticks_per_second: sample k lies at distance min(k * speed /
// ticks_per_second, length) along the slot 0 / slot 1 polyline, and the
// last sample is the path's end. Its velocity is `speed` along the segment
// it lies on (the last segment for the end); its height is interpolated.
// Fails on a speed that is not positive and finite, on zero ticks, and past
// max_samples.
bool sample_track_residency_path(
    const std::vector<TrackResidencyVertex>& vertices,
    double speed,
    std::uint32_t ticks_per_second,
    std::vector<TrackResidencySample>& samples,
    std::vector<TrackResidencyPathPoint>& points,
    std::string& error,
    const TrackResidencyPathLimits& limits = TrackResidencyPathLimits{});

// The horizontal length of a path, in slot 0 and slot 1.
double track_residency_path_length(
    const std::vector<TrackResidencyVertex>& vertices) noexcept;

// A HAND-GIVEN polyline around the circuit of race 1 of Underground mode
// (event 1, track 1003, stream L1RA). It is NOT the game's route: the route
// and checkpoints are issue #61's and are not recovered. It was drawn by
// hand over a top-down CPU render of the permanent set, along the streets
// through the 13 zones of the 0x00034153 record keyed on 1003 in that
// record's order, with every height read from the static collision
// (nfsu_probe track-collision); docs/TRACK_STREAMING.md says how. The
// text is in the format parse_track_residency_path reads.
extern const char* const kTrackResidencyHandPath1003;

} // namespace nfsu
