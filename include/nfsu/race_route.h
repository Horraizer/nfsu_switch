#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nfsu {

// The route of one race: where the start and finish line are, which split
// boundaries a car passes, how progress around the track is measured, when a
// lap counts, and when the car is going the wrong way.
//
// None of it is a list of gates in space. The game measures a car's progress
// as a percentage along one closed spline, the "track path" of chunk
// 0x80034147 in TRACKB####.LZC, and every start line, split time boundary
// and finish line is a percentage on that spline, found once per race by
// projecting a named marker of chunk 0x00034146 onto it. A lap counts when
// the car's percentage passes the finish percentage going forward, inside a
// window of 25 percent. docs/RACE_ROUTE.md carries the disassembly every
// constant below is named from, the retail numbers for event 1 on track
// 1003, and what is approximation and what is unknown.
//
// Every record size, field offset and constant here is named with the
// speed.exe instruction that reads it. Nothing here scans a directory: a
// caller names one TRACKB file and exactly that file is read, and nothing
// is written.

// ---------------------------------------------------------------------
// The position markers: chunk 0x00034146
// ---------------------------------------------------------------------

// 0x00441ED6 `cmp dword ptr [ecx], 0x34146` in the loader.
constexpr std::uint32_t kTrackMarkerChunkId = 0x00034146U;
// 0x00441EE1 `lea eax, [ecx + 0x17]` and `and eax, 0xfffffff0`: the first
// record starts at the payload rounded up to 16 bytes, and 0x00441EF0..
// 0x00441EF7 divide what is left by 48 (magic 0xAAAAAAAB, shift 37).
constexpr std::size_t kTrackMarkerAlignment = 16;
constexpr std::size_t kTrackMarkerRecordSize = 0x30;
// +0x08, the name hash the two lookups compare (0x0043FED0 and 0x0043FF12
// `cmp dword ptr [esi + 8], ...`), a bStringHash of the name: -1, then
// hash * 33 + byte for every byte (0x00434679..0x00434692).
constexpr std::size_t kTrackMarkerHashOffset = 0x08;
// +0x0C, a number 0x004346FC compares with the lookup's parameter.
constexpr std::size_t kTrackMarkerParamOffset = 0x0C;
// +0x10, the position; 0x0047CB26..0x0047CB32 copy +0x10 and +0x14, and
// 0x00434722 projects the point at +0x10 onto the track path.
constexpr std::size_t kTrackMarkerPositionOffset = 0x10;
// +0x20, a 16-bit angle 0x0047CB35 reads and turns by 0x4000.
constexpr std::size_t kTrackMarkerAngleOffset = 0x20;

// ---------------------------------------------------------------------
// The track path: container 0x80034147
// ---------------------------------------------------------------------

// 0x004434D5 `cmp ecx, 0x80034147`. The loader does not read the ids of
// the three children; it takes them in order, a path table, a node table
// and a zone table, and divides each size by its record size.
constexpr std::uint32_t kTrackPathContainerId = 0x80034147U;
constexpr std::uint32_t kTrackPathRecordChunkId = 0x00034148U;
constexpr std::uint32_t kTrackPathNodeChunkId = 0x00034149U;
constexpr std::uint32_t kTrackPathZoneChunkId = 0x0003414AU;
// 0x004434EA..0x004434EF: size / 40 (magic 0xCCCCCCCD, shift 37).
constexpr std::size_t kTrackPathRecordSize = 40;
// 0x004434FE..0x00443503 and 0x00443531: size / 20 (the same magic,
// shift 36).
constexpr std::size_t kTrackPathNodeSize = 20;
// 0x00443513..0x00443528: size / 180 (magic 0x6C16C16D), and 0x0044348A
// `imul edx, edx, 0xb4` strides the zones.
constexpr std::size_t kTrackPathZoneSize = 180;
// 0x004434AA `cmp esi, 0xb`: the loader indexes the zones by eleven types.
constexpr std::uint32_t kTrackPathZoneTypes = 11;
// The zone types this layer reads, named by the string table 0x00700964 the
// executable keeps for them: NO_WRONG_WAY, WRONG_WAY, RESET and
// RESET_TO_POINT are types 0 to 3, the four 0x00430E55..0x00430E91 look up.
constexpr std::uint32_t kZoneNoWrongWay = 0;
constexpr std::uint32_t kZoneWrongWay = 1;
constexpr std::uint32_t kZoneReset = 2;
constexpr std::uint32_t kZoneResetToPoint = 3;
// (180 - 0x3C) / 8: the most polygon vertices a zone record can hold.
constexpr std::size_t kTrackPathZoneMaxVertices = 15;

// One 0x00034148 record, 40 bytes.
struct TrackPathRecord {
    std::uint64_t offset = 0;
    // +0x00, not read by the traced code.
    std::uint8_t byte0 = 0;
    // +0x01, non-zero for a closed path (0x004432E4: the spline type is 0,
    // closed, when it is set and 2, open, when it is not).
    std::uint8_t closed = 0;
    // +0x02 and +0x04, the node range the nearest-node searches walk
    // (0x00443651 and 0x00443655), and +0x06, the spline's control-point
    // count (0x00443302), read from the nodes starting at +0x02.
    std::int16_t first = 0;
    std::int16_t end = 0;
    std::int16_t count = 0;
    // +0x08 and +0x10, added and multiplied into a distance along the path
    // (0x00443C2E `fadd [edi + 8]`, 0x00443C26 `fmul [edi + 0x10]`).
    float distance_offset = 0.0F;
    // +0x0C, the length of the path. Record 0's is the divisor every
    // progress fraction uses (0x004432A9..0x004432AF store it at
    // 0x007372B0, and 0x00443C7B divides by it).
    float length = 0.0F;
    float distance_scale = 0.0F;
    // +0x14 and +0x1C, two points no traced instruction reads. In retail
    // they are the first and the last node.
    float start_point[2] = {0.0F, 0.0F};
    float end_point[2] = {0.0F, 0.0F};
};

// One 0x00034149 record, 20 bytes: a control point of the path's spline.
struct TrackPathNode {
    std::uint64_t offset = 0;
    // +0x00 and +0x04 (0x0044339D..0x004433A3 copy them into the spline).
    float x = 0.0F;
    float y = 0.0F;
    // +0x08, the index of the path record this node belongs to
    // (0x00443B02 `movsx eax, byte ptr [ecx + 8]`).
    std::int8_t path = 0;
    // +0x09, not read by the traced code.
    std::uint8_t byte9 = 0;
    // +0x0A, +0x0C and +0x0E, three node indices 0x00443AB3..0x00443AFD
    // test as nearer candidates; -1 is none.
    std::int16_t links[3] = {-1, -1, -1};
    // +0x10, the node's distance along the path, which 0x004437E0 reads.
    float distance = 0.0F;
};

// One 0x0003414A record, 180 bytes: a zone polygon with a type.
struct TrackPathZone {
    std::uint64_t offset = 0;
    // +0x00, the type (0x00443492 `cmp dword ptr [edx], esi`).
    std::uint32_t type = 0;
    // +0x04, the point RESET_TO_POINT puts a car at (0x00427F01..0x00427F07).
    float point[2] = {0.0F, 0.0F};
    // +0x0C, a direction; WRONG_WAY measures a car against it instead of
    // the path (0x00430EA9..0x00430EB1).
    float direction[2] = {0.0F, 0.0F};
    // +0x14, a height 0x00427ED9..0x00427EEC compares a car's +0x68 with.
    float height = 0.0F;
    // +0x18, the race direction the zone applies to, or -1 for both
    // (0x00443710..0x0044371E).
    std::int8_t direction_filter = -1;
    // +0x1C, +0x20 and +0x24, three distances along the path, which
    // 0x004435E0 turns around for a reverse race. Not used by this layer.
    float distances[3] = {0.0F, 0.0F, 0.0F};
    // +0x28 to +0x34, the box tested first (0x00443720..0x00443750).
    float box_min[2] = {0.0F, 0.0F};
    float box_max[2] = {0.0F, 0.0F};
    // +0x38 and +0x3C, the polygon handed to 0x005679B0 (0x00443752..
    // 0x0044375C).
    std::int32_t vertex_count = 0;
    std::vector<float> vertices;
};

// One 0x00034146 record, 48 bytes.
struct TrackMarker {
    std::uint64_t offset = 0;
    std::uint32_t name_hash = 0;
    std::uint32_t param = 0;
    float position[3] = {0.0F, 0.0F, 0.0F};
    std::uint16_t angle = 0;
};

struct RaceRouteIssue {
    std::uint64_t offset = 0;
    std::string message;
};

// Not a retail rule: the largest magnitude a node or marker coordinate may
// have on either horizontal axis. Retail nodes and markers stay within
// 2332 of the origin (2331.67, on 1106); a coordinate of 1e30 is finite,
// but its square is not, and the percents built from it mean nothing.
// docs/RACE_ROUTE.md.
constexpr float kRaceRouteCoordinateLimit = 1.0e6F;

struct RaceRouteLimits {
    std::uint64_t max_file_size = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t max_image_size = 64ULL * 1024ULL * 1024ULL;
    std::size_t max_top_level_chunks = 1U << 16;
    std::size_t max_nodes = 1U << 14;
    std::size_t max_zones = 1U << 12;
    std::size_t max_markers = 1U << 12;
};

// What one TRACKB image holds for the route.
struct RaceRouteTrack {
    std::uint64_t image_size = 0;
    std::uint64_t stored_size = 0;
    bool jdlz = false;
    std::uint64_t container_offset = 0;
    std::uint64_t path_chunk_offset = 0;
    std::uint64_t node_chunk_offset = 0;
    std::uint64_t zone_chunk_offset = 0;
    std::uint64_t marker_chunk_offset = 0;
    std::uint64_t marker_record_offset = 0;
    std::vector<TrackPathRecord> paths;
    std::vector<TrackPathNode> nodes;
    std::vector<TrackPathZone> zones;
    std::vector<TrackMarker> markers;
    std::vector<RaceRouteIssue> issues;
    bool valid = false;

    bool ok() const noexcept { return valid && issues.empty(); }
};

// Parses an in-memory, already decompressed TRACKB image. Never throws for
// malformed input. Rules: the image is at most limits.max_image_size bytes
// and its top-level chunks tile it; exactly one 0x80034147 whose payload is
// exactly the three children 0x00034148, 0x00034149 and 0x0003414A in that
// order, each a whole number of records (the loader floors the divisions
// and would ignore a remainder; it is refused rather than guessed about);
// exactly one 0x00034146, a whole number of 48-byte records after the
// 16-byte alignment. Each path: a node range inside the node table, a count
// equal to end - first and at least 3, a finite, positive length and
// finite scale and offset. Each node: finite coordinates and distance,
// coordinates within kRaceRouteCoordinateLimit of the origin on each axis, a
// path index naming a path record (0x00443B02 indexes the path table with
// it unchecked), and links that are -1 or a node index. Zones: types below
// 11 and in non-decreasing order (0x00443450 indexes the table by the first
// record of each type, so an unsorted table would hide zones), a vertex
// count of 0 to 15, finite numbers. Markers: finite positions, within
// kRaceRouteCoordinateLimit on each horizontal axis.
RaceRouteTrack parse_race_route_track(const unsigned char* data,
                                      std::size_t size,
                                      const RaceRouteLimits& limits = RaceRouteLimits{});

// Reads the file read-only, decompresses it when it starts with a JDLZ
// header (the game loads TRACKS\TRACKB%d.LZC, 0x006C9B38), and parses it.
RaceRouteTrack load_race_route_track(const std::filesystem::path& path,
                                     std::vector<unsigned char>& image,
                                     const RaceRouteLimits& limits = RaceRouteLimits{});

// The name hash of a marker: the bStringHash 0x00434679..0x00434692 inline.
std::uint32_t track_marker_hash(std::string_view name) noexcept;

// The marker names this layer knows, or nullptr for any other hash.
const char* track_marker_name(std::uint32_t hash) noexcept;

// ---------------------------------------------------------------------
// The spline, and a car's coordinate on it
// ---------------------------------------------------------------------

// A closed uniform Catmull-Rom spline through the path's nodes. The basis is
// the matrix 0x0059C85D..0x0059CA3A write at 0x007376B0 (and its derivative
// at 0x007376F0); every control point carries a fourth value, the running
// arc length 0x0059D9F0 measures with 16 chords per segment, which the
// spline interpolates like a coordinate and which is the distance along the
// path. docs/RACE_ROUTE.md says why float arithmetic is the faithful model.
struct TrackPathSpline {
    // x, y and the running length, one triple per control point.
    std::vector<float> points;
    std::size_t count = 0;
    // The arc length 0x0059D9F0 leaves at +0x14 of the spline.
    float spline_length = 0.0F;
    // The length the fractions divide by: path record 0's +0x0C.
    float path_length = 0.0F;
    float distance_scale = 1.0F;
    float distance_offset = 0.0F;
    // The first node of the path in the node table.
    std::size_t first_node = 0;
    // The race runs backwards along the path (0x004435E0, from
    // 0x0078A2FC == 1 at 0x00449921..0x00449938).
    bool reverse = false;
};

// Builds the spline of the one path of `track`. False, with a reason, for a
// track that is not valid, holds more than one path, links nodes, or whose
// path is open: 0x0059CEA0 extrapolates past the ends of an open spline and
// is not implemented here, so a sprint or drag route is refused rather than
// approximated. Also refused, as this project's rule and not the game's: a
// path whose record's length, scale and offset do not measure the spline's
// own loop as 0 to 100 percent, to within 1 percent at either end.
bool build_track_path_spline(const RaceRouteTrack& track,
                             bool reverse,
                             TrackPathSpline& spline,
                             std::string& error);

// A position and its derivative on the spline at a parameter, as
// 0x0059D350 evaluates it: the parameter wrapped into [0, count) by
// 0x0059CE10, then the basis over four control points.
void evaluate_track_path(const TrackPathSpline& spline,
                         float parameter,
                         int derivative,
                         float out[3]) noexcept;

// The 16-bit angle of a direction, 0x4000 a quarter turn counterclockwise
// from +x, exactly as 0x00564DB0 computes it: |y| / length or |x| / length
// through the arcsine table 0x00564CD0 reads at 0x006F2280, with nothing
// but float arithmetic, one square root and a table lookup, so every
// platform gives the same answer. docs/RACE_ROUTE.md traces it.
std::uint16_t track_angle(float x, float y) noexcept;

// 0x00564CD0, the arcsine by table track_angle calls: the 16-bit angle
// whose sine is `s`, 0x4000 a quarter turn, 0x10000 less it for a negative
// `s`. track_angle only ever gives it a ratio of at most 1 / sqrt(2), so it
// is exposed for the tests that reach its other rows.
std::uint32_t track_arcsine_angle(float s) noexcept;

// The arcsine table in the executable's own layout, eight bytes a row: the
// test compares its SHA-256 with the one docs/RACE_ROUTE.md records.
constexpr std::size_t kTrackAngleTableRows = 208;
constexpr std::size_t kTrackAngleTableBytes = kTrackAngleTableRows * 8;
void track_angle_table_bytes(unsigned char out[kTrackAngleTableBytes]) noexcept;

// A car's place on the path: the object 0x004440C0 constructs and
// 0x00443A10 updates.
struct TrackPathCoordinate {
    // The tracked node, an index into the node table, or -1 for none.
    int node = -1;
    // The spline parameter (+0x0C).
    float parameter = 0.0F;
    // The position last given (+0x00 and +0x04).
    float position[2] = {0.0F, 0.0F};
    // The fraction of the path (+0x10), the distance along it (+0x14) and
    // the unit direction of travel (+0x18 and +0x1C), all in race
    // direction.
    float fraction = 0.0F;
    float distance = 0.0F;
    float tangent[2] = {1.0F, 0.0F};
    // How many iterations the last nearest-point search took.
    int iterations = 0;
};

// 0x00443A10 over `query`. Keeps the tracked node when the query is within
// 128 of the last position (0x00443A41), otherwise searches every node
// (0x00443630); moves to the nearer neighbour (0x00443920); searches the
// spline for the nearest parameter from the node, or from the last
// parameter when the node did not change, with 0x0059D540's secant
// iteration to a tolerance of 0.05 (0x00443B57); and reads the distance,
// the fraction and the direction at that parameter.
void update_track_path_coordinate(const TrackPathSpline& spline,
                                  const RaceRouteTrack& track,
                                  TrackPathCoordinate& coordinate,
                                  const float query[2]);

// ---------------------------------------------------------------------
// The boundaries: the start line, the split boundaries, the finish line
// ---------------------------------------------------------------------

// 0x00434810 builds at most five, and the per-car arrays that record them
// hold five (0x004333EC `mov edx, 5`).
constexpr std::size_t kRaceBoundaryLimit = 5;

struct RaceBoundary {
    std::string name;
    // The marker it was projected from, an index into track.markers, or -1
    // when the marker is missing and the default percentage stands in.
    int marker = -1;
    // Percent of the path, in race direction.
    float percent = 0.0F;
};

// The race laps rule, 0x004342A4..0x004342D6: the lap count of the career
// race (0x0078A31C), or 1 when the track's lap-skip byte is set and the
// count is not 0 already. Two more flags force 1 there (0x0078A344 and
// 0x007361A8); what sets them is not traced, and this takes both as clear.
int race_laps(int event_laps, std::int8_t lap_skip) noexcept;

struct RaceRoute {
    TrackPathSpline spline;
    std::vector<RaceBoundary> boundaries;
    // +0x53E and +0x53F of the race status: which boundary is the start
    // line and which the finish line. They are the same one on a circuit.
    int start = -1;
    int finish = -1;
    int laps = 0;
    bool reverse = false;
    bool point_to_point = false;
};

// 0x00434810 for one race: the start line, the three split boundaries and,
// on a point-to-point track (lap-skip set), the finish line, each the
// percentage of its marker's projection (0x00434640), or a default when the
// marker is missing - 5 for the start, the start plus 33 and 66 for two
// splits, the start plus 95 for a finish, less 100 above 100, and no
// boundary at all for the third split. `direction` is 0 forward or 1
// reverse, and decides the marker names and the split order. False, with a
// reason, when the path spline cannot be built.
bool build_race_route(const RaceRouteTrack& track,
                      int laps,
                      int direction,
                      std::int8_t lap_skip,
                      RaceRoute& route,
                      std::string& error);

// ---------------------------------------------------------------------
// Progress, laps and the wrong way
// ---------------------------------------------------------------------

// 0x00433650: a boundary is crossed forward when the car was behind it and
// is now at or past it, backward when the reverse holds, and only when the
// car is now within 25 percent of it (0x004336F3 `fcomp [0x6CCB18]`).
constexpr float kRaceCrossingWindow = 25.0F;
// 0x00426BCF and 0x00426BE2: a coordinate more than 300 from its tracked
// node is searched again from every node on a frame whose counter is a
// multiple of 60.
constexpr float kRaceRecoverDistance = 300.0F;
constexpr int kRaceRecoverPeriod = 60;

enum class RaceEventKind {
    // A boundary crossed forward, as 0x00433650 counts it.
    crossed_forward,
    // A boundary crossed backward: a reverse crossing.
    crossed_backward,
    // The car's progress moved past a boundary in one step and landed more
    // than 25 percent beyond it, so the game counts no crossing. This is
    // this layer's report; the game records nothing.
    missed_forward,
    missed_backward,
    // The lap counter reached a new highest value: a lap is complete.
    lap_complete,
    // The highest lap reached the race's lap count (0x00435A30..0x00435A4F).
    finished,
    wrong_way_on,
    wrong_way_off,
    // The car entered a RESET or RESET_TO_POINT zone, reported once on
    // entry. The game puts it back on the track there (0x00427ED0) and the
    // wrong-way test answers no; this layer answers no and does not move it.
    reset_zone,
    // The coordinate was searched again from every node (0x00426BFA).
    recovered,
    // The coordinate went further than 5000 from its tracked node: the game
    // sets the car's +0x87C there (0x00426BA2), the flag a RESET zone also
    // sets (0x00427F53), and does not search. Reported on the step it
    // happens, not again until the car has come back within 5000.
    off_path,
};

const char* race_event_kind_name(RaceEventKind kind) noexcept;

struct RaceProgressEvent {
    RaceEventKind kind = RaceEventKind::crossed_forward;
    std::size_t step = 0;
    // The boundary, for the four crossing kinds, or the zone index for
    // reset_zone; -1 otherwise.
    int index = -1;
    // The lap counter after the event (+0x08 of the per-car record).
    int lap = 0;
    // The car's percent of the path after the step.
    float percent = 0.0F;
};

// The per-car record 0x004333C0 initialises and 0x004338C0 updates, with
// the wrong-way flag of 0x00430730 beside it.
struct RaceProgress {
    TrackPathCoordinate coordinate;
    // +0x1C, the percent at the previous step.
    float previous_percent = 0.0F;
    // +0x08, -1 until the car first crosses the finish line forward. A
    // signed byte in the game, and kept in that range: 129 backward
    // crossings from -1 wrap it to 126.
    int lap = -1;
    // +0x09, the highest lap reached, also a signed byte.
    int max_lap = 0;
    // +0x04 and +0x06: 5 * lap + boundary of the last forward crossing, and
    // the highest of those; -1 before any. Signed 16-bit.
    int checkpoint = -1;
    int max_checkpoint = -1;
    // The boundary the car reaches next in race order.
    int next_boundary = -1;
    bool finished = false;
    std::size_t finish_step = 0;
    bool wrong_way = false;
    float previous_position[2] = {0.0F, 0.0F};
    std::size_t step = 0;
    // The RESET or RESET_TO_POINT zone the car was in at the last step, or
    // -1.
    int zone = -1;
    // The coordinate was more than 5000 from its node at the last step.
    bool off_path = false;
};

// 0x004333C0: the coordinate of `position`, searched from every node, and
// the percent the first update compares against.
void start_race_progress(const RaceRoute& route,
                         const RaceRouteTrack& track,
                         const float position[2],
                         RaceProgress& progress);

// One fixed step of `step_seconds` to `position`: the coordinate update
// (0x00443A10), the off-path and recovery rules (0x00426B63..0x00426BFD),
// the wrong-way test (0x00430E00) and the lap update (0x004338C0), in that
// order. A finished car is not updated again (0x00435603). Events are
// appended.
//
// The wrong-way test compares the path direction with the car's heading
// and with its velocity; a position alone has neither, so both are taken
// to be the direction of the step, and the speed its length over
// `step_seconds`. The test's wheel-contact gate (+0x3CC >= 4) is taken as
// met, and a reset zone's height gate as passed. These are approximations,
// and docs/RACE_ROUTE.md names them.
void step_race_progress(const RaceRoute& route,
                        const RaceRouteTrack& track,
                        RaceProgress& progress,
                        const float position[2],
                        float step_seconds,
                        std::vector<RaceProgressEvent>& events);

// Whole laps completed: the lap counter, never below zero.
int race_laps_completed(const RaceProgress& progress) noexcept;

// ---------------------------------------------------------------------
// A synthetic drive, for replaying a route without a car
// ---------------------------------------------------------------------

// Not a retail rule: a deterministic position sequence this project builds
// to exercise a route end to end. It starts on the first grid marker of the
// race direction (ForwardStartingPosition or BackwardStartingPosition), goes
// to the first path node ahead of the grid in race order, and visits nodes
// in race order until it has passed the finish line `laps` more times after
// the first time - ending on the first node past the line - or,
// `backward`, the same number of nodes in the opposite order starting from
// the node behind the grid. Consecutive points
// are joined by straight steps of at most `spacing`. With `skip_from` and
// `skip_to` both at least 0, the first time the drive reaches node
// skip_from it jumps to the next skip_to in one step and leaves out the
// nodes between; the drive still ends where it would have.
struct RaceDrive {
    // x, y pairs, one per position; the first is the grid.
    std::vector<float> points;
    int grid_marker = -1;
    std::vector<int> nodes;
    std::size_t skipped = 0;
};

bool make_race_drive(const RaceRoute& route,
                     const RaceRouteTrack& track,
                     bool backward,
                     int skip_from,
                     int skip_to,
                     float spacing,
                     RaceDrive& drive,
                     std::string& error);

// The percent of a node, measured from a fresh coordinate as a marker is.
float race_node_percent(const RaceRoute& route,
                        const RaceRouteTrack& track,
                        std::size_t node);

} // namespace nfsu
