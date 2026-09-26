#pragma once

#include "nfsu/race_route.h"
#include "nfsu/sha256.h"
#include "nfsu/track_contact.h"
#include "nfsu/vehicle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// The vehicle of issue #58 on the track collision of issue #158: the ground
// its wheels stand on, the collision of its body with the track, and the
// resets that put it back when it leaves the track - issue #60.
// docs/VEHICLE_TRACK.md is the reference: every rule below is labelled there
// as RECOVERED (read from speed.exe or the retail files, with the address)
// or APPROXIMATION (invented here, with the reason).
//
// UNITS AND FRAMES. The vehicle's world frame (include/nfsu/vehicle.h) is
// the track's raw slot order: x is px, y is pz, z is py, the vertical, and
// both are right-handed with z up, so nothing is rotated between the two.
// One world unit is taken as one metre (docs/TRACK_COLLISION.md, "Units,
// frame and normals": the body measures 4.374 units against a 4.31 m car),
// so nothing is scaled either. The only conversion is precision: the track
// is float and the vehicle double, so a query's ends are rounded to float
// once and its answer widened back exactly. A heading is an angle from +x
// toward +y, counter-clockwise seen from above; the game stores it as a
// 16-bit unit of 1/65536 of a turn (track_angle), and a heading taken from
// the game is that unit times 2 pi / 65536.
//
// DETERMINISM. Everything here is IEEE-754 arithmetic compiled without
// contraction (cmake/vehicle_track_libraries.cmake) and square roots from
// vehicle_sqrt or det_sqrt; no library mathematics, no clock, no
// unordered container. With the model's own rules, one input sequence gives
// the same telemetry bits in the desktop Debug and Release builds and on the
// console; docs/VEHICLE_TRACK.md pins them.

// ---------------------------------------------------------------------------
// Recovered constants
// ---------------------------------------------------------------------------

// Every value is the float speed.exe holds, widened exactly.

// The world clock: 0x00585D70 counts 1/60 s ticks, and the model takes two
// steps of 1/120 s a tick (docs/VEHICLE.md). The checks the game's car
// update makes once a frame run here once a tick, after its second step.
constexpr std::uint32_t kVehicleTrackStepsPerTick = 2;

// Body against track: the solver 0x0046BAE0 and the bodies' virtual
// functions it calls. The restitution and friction of the car body and of a
// track polygon body are the same functions, 0x00466900 and 0x00466910, and
// the solver takes the larger of the two bodies' values of each.
constexpr double kVehicleTrackRestitution = static_cast<double>(0.1F);   // [0x006CC7B8]
constexpr double kVehicleTrackFriction = static_cast<double>(0.3F);      // [0x006CC7B4]
// 0x0046C00C..0x0046C0A3: against a body that cannot move, a car whose up
// axis has a vertical component above 0.7 - upright - slides with friction
// 0.1 instead, and takes the fast-car rule below.
constexpr double kVehicleTrackUprightUp = static_cast<double>(0.7F);          // [0x006CCA04]
constexpr double kVehicleTrackUprightFriction = static_cast<double>(0.1F);    // 0x0046C09B
// 0x0046C223..0x0046C3D9: an upright car faster than 30 m/s against a body
// that cannot move loses a horizontal impulse of 0.4 t m/s against its
// velocity, and the arm the impulse turns it by is shortened by
// |J| / (|J| + v / 1.75), with J in t m/s and v in m/s, when |J| + v > 1.
constexpr double kVehicleTrackFastSpeed = static_cast<double>(30.0F);  // [0x006CCAC0]
constexpr double kVehicleTrackFastDrag = static_cast<double>(0.4F);    // [0x006B7258]
constexpr double kVehicleTrackFastArm = static_cast<double>(1.75F);    // [0x006B7260]
// 0x0046C620: a car's hit on a wall polygon - the vertical part of its
// normal below 0.1 - whose arm, made unit, lies within 0.9 of the car's
// forward axis only in cosine, not closer - a side hit - takes half the
// restitution.
constexpr float kVehicleTrackSideHitWall = 0.1F;                            // [0x006CC7B8]
constexpr double kVehicleTrackSideHitAlong = static_cast<double>(0.9F);     // [0x006B7250]
constexpr double kVehicleTrackSideHitScale = static_cast<double>(0.5F);     // [0x006B7254]
// 0x0046A080 and 0x0046A260: the car collides as if it had at least this
// mass, tonnes (the 240SX's is 1.22).
constexpr double kVehicleTrackCollisionMass = static_cast<double>(1.85F);  // [0x006B727C]
// 0x0046BE69..0x0046BEA5: the normal impulse is at least this speed over the
// larger inverse mass of the two bodies, so a touching car always leaves at
// 0.3 m/s at least.
constexpr double kVehicleTrackSeparationSpeed = static_cast<double>(0.3F);  // [0x0070009C]
// 0x0046C0E4..0x0046C0FB: the friction impulse ramps in below this
// tangential speed, m/s, and is zero below the second.
constexpr double kVehicleTrackSlideSpeed = static_cast<double>(1.0F);       // [0x007000A8]
constexpr double kVehicleTrackSlideEpsilon = static_cast<double>(1.0e-5F);  // [0x006CCB7C]
// 0x00584F60: the rigid body clamps each impulse to 1000 t m/s and each
// angular impulse to 10 t m^2/s.
constexpr double kVehicleTrackImpulseLimit = static_cast<double>(1000.0F);  // [0x006B5B4C]
constexpr double kVehicleTrackAngularLimit = static_cast<double>(10.0F);    // [0x006B5B50]
// 0x0046A0C0 and 0x0046A260: the angular part of a collision - the inverse
// inertia in the solver's denominator and the angular impulse applied - is
// scaled by 1 for a car whose +0x38 is 1 and by 0.4 otherwise. +0x38 is a
// collision state: the constructor sets 0 (0x0042575C); 0x00465D00, called
// from the collision record's update (0x004658B0), sets 1 (0x00465EC0) after
// a car-vs-car or car-vs-kind-4 hit of 30 t m/s or more while it is 0; and
// the frame update 0x0042A530 returns it to 0 once all four wheels are down
// and the time since that hit (+0x4CC) has run out. A track polygon is kind
// 3, so a car that meets only the track takes 0.4.
constexpr double kVehicleTrackAngularFactor = static_cast<double>(0.4F);   // [0x006B7280]
// 0x0046B260: the separation search. The bodies are moved apart along the
// track polygon's normal, turned to face the car (0x0046B070), by a
// distance that starts at 0.005 and grows by a step that grows by 0.01 each
// time until it reaches 0.1, until they no longer intersect, for at most 20
// tries; the vertical part of the move is held to 0.15.
constexpr float kVehicleTrackSeparationFirstStep = 0.005F;  // 0x0046B3FA
constexpr float kVehicleTrackSeparationStepGrowth = 0.01F;  // [0x006CC79C]
constexpr float kVehicleTrackSeparationStepLimit = 0.1F;    // [0x006CC7B8]
constexpr float kVehicleTrackSeparationVertical = 0.15F;    // [0x006B724C]
constexpr std::uint32_t kVehicleTrackSeparationTries = 20;  // 0x0046B5A6

// Out of bounds: 0x00426B03..0x00426B59, every 32nd frame (the frame
// counter 0x006F08A4 plus 7, modulo 32, is 0), a car with no collision
// polygon under its position, nor under the point one unit further along
// +x, is flagged for a reset.
constexpr std::uint32_t kVehicleTrackBoundsPeriod = 32;
constexpr std::uint32_t kVehicleTrackBoundsPhase = 7;
constexpr double kVehicleTrackBoundsSideStep = static_cast<double>(1.0F);  // [0x006CC7BC]
// Sunk: 0x0042A4D0, every frame, a car with all four wheels on the ground
// whose ground query 0x00440A40 answers a surface more than 0.25 above its
// position is reset. The query is approximated by a column from 1.0 above
// the position (the reach of its first pass) down to 0.25 above it.
constexpr double kVehicleTrackSunkMargin = static_cast<double>(0.25F);  // [0x006CC90C]
constexpr double kVehicleTrackSunkReach = static_cast<double>(1.0F);    // 0x00440B1A
// The reset: 0x004314A0 puts the player's car on the nearest track path
// node with ground under it at speed 0 (0x004317C1); a RESET_TO_POINT zone
// puts it at the zone's point at 8.0 (0x00427F24).
constexpr double kVehicleTrackResetToPointSpeed = static_cast<double>(8.0F);  // [0x00700D58]
// 0x00427CD2..0x00427D10: 0x00427C50 sets the velocity along the car's
// forward axis to the speed plus 0.1, the stored speed to the speed, and
// each wheel's spin to the speed over its radius.
constexpr double kVehicleTrackResetSpeedLead = static_cast<double>(0.1F);  // [0x006CC7B8]

// ---------------------------------------------------------------------------
// The car's collision hull
// ---------------------------------------------------------------------------

// The 240SX's collision shape is a convex hull of 37 points, 240SX_CV,
// stored in Cars/240SX/GEOMETRY.BIN (SHA-256 00220219...F619): container
// 0x80034020 at file offset 4778496, whose 0x00039200 header names it
// (hash 0xC93AE898 at +0x18) and whose 0x00039201 child, payload at file
// offset 4778608, holds one 0x30-byte record per point with the point at
// +0x10 (loader 0x00472B00..0x00472BD7, shape type 1). The car's collision
// body finds it by the model's hash (0x00469C22..0x00469C4D) and
// 0x00472E40 moves every point by minus the car's centre of mass, in float,
// once. A small numeric table from a retail file, kept under the owner's
// rule of PR #169 comment 3860: it reproduces 444 of the chunk's 1776 bytes
// - the 37 points, 12 bytes each at +0x10 + 0x30 k - which hash, in that
// order, to kVehicle240sxHullBytesSha256; nfsu_vehicle_track_tests rebuilds
// them. The other fields of each record (+0x00..+0x0F, +0x1C..+0x2F) and the
// 0x00039202 child that follows are not held here.
constexpr std::size_t kVehicle240sxHullPoints = 37;
constexpr std::size_t kVehicleHullRecordSize = 0x30;
constexpr std::size_t kVehicleHullPointOffset = 0x10;
constexpr std::size_t kVehicle240sxHullChunkBytes = 1776;
constexpr std::uint64_t kVehicle240sxHullFileOffset = 4778608;
constexpr std::size_t kVehicle240sxHullBytes = 444;
constexpr const char* kVehicle240sxHullBytesSha256 =
    "FCB0D6B795AEB3A1E0F34ED4CB0C2BFF3EB4560DA56AC00CFDD587226CE2A019";

// A car's collision hull, in the frame of its meshes (the wheel table's
// body frame: +x forward, +y left, +z up), as the file stores it.
struct VehicleHull {
    std::vector<std::array<float, 3>> points;
};

VehicleHull stock_240sx_hull();

// Writes the points at +0x10 + 0x30 k of a 0x00039201 payload image and
// leaves every other byte alone: the inverse of what the table was read
// from, for the rebuild test. `size` must hold every record.
bool encode_vehicle_hull(const VehicleHull& hull, unsigned char* payload,
                         std::size_t size) noexcept;

// When a model has no hull of its own, 0x00469C79..0x00469C99 builds a box
// shape from these corners, about the centre of mass. Not used for the
// 240SX, which has its hull; kept for a car that lacks one.
constexpr float kVehicleFallbackBoxMin[3] = {-2.217F, -0.971F, -0.2F};
constexpr float kVehicleFallbackBoxMax[3] = {2.165F, 0.971F, 0.624F};

// ---------------------------------------------------------------------------
// The ground the wheels stand on
// ---------------------------------------------------------------------------

// What one query met.
enum class VehicleTrackSurface : std::uint8_t {
    none = 0,     // no query was made
    road = 1,     // a road triangle: ground for a wheel
    wall = 2,     // a wall triangle, refused as ground
    clear = 3,    // cells under the query, nothing met: in the air
    outside = 4,  // no cell under the query at all
    refused = 5,  // an argument the query cannot take, or a cap
};

const char* vehicle_track_surface_name(VehicleTrackSurface surface) noexcept;

struct VehicleTrackCast {
    VehicleTrackSurface surface = VehicleTrackSurface::none;
    TrackContactStatus status = TrackContactStatus::refused;
    TrackContact contact;
    VehicleGroundHit hit;
};

// VehicleGround over a track's collision. A cast is one segment query of
// query_track_contact_segment from `from` to from + length * direction, the
// two ends rounded to float. A road contact is ground: its distance is the
// contact point's projection on the cast's own line, measured from the
// unrounded start and held to [0, length], and its normal is the contact's,
// which faces the start. A wall, nothing, no cell or a refusal is no ground;
// a wall is refused because the model takes any upward-facing hit as
// ground, and docs/VEHICLE_TRACK.md says why. The surface id is 0: retail
// surface types are not decoded. The world must outlive this.
class TrackVehicleGround final : public VehicleGround {
public:
    explicit TrackVehicleGround(const TrackContactWorld& world) noexcept : world_(&world) {}

    VehicleGroundHit cast(const VehicleVec3& from,
                          const VehicleVec3& direction,
                          double length) const override;

    // The same query, with everything it met.
    VehicleTrackCast probe(const VehicleVec3& from,
                           const VehicleVec3& direction,
                           double length) const;

private:
    const TrackContactWorld* world_ = nullptr;
};

// ---------------------------------------------------------------------------
// The course: collision, route and a start
// ---------------------------------------------------------------------------

struct VehicleTrackCourse {
    std::string name;
    TrackContactWorld world;
    // The route of the race, whose path nodes the reset puts the car on,
    // whose zones reset it, and whose progress says how far it is round.
    // Without one (has_route false) a reset is requested and reported but
    // cannot be performed.
    RaceRouteTrack track;
    RaceRoute route;
    bool has_route = false;
    // Where a run starts unless told otherwise: the ground point under the
    // centre of mass and the heading.
    VehiclePose start;
    // The marker the start came from, or -1.
    int start_marker = -1;
};

// Loads one TRACKB bundle twice, read-only - its collision
// (load_track_contact_world) and its route (load_race_route_track) - builds
// the route of a race of `laps` in `direction` (0 forward, 1 reverse), and
// puts the start on grid slot `slot`: vehicle_track_grid_pose.
bool load_vehicle_track_course(const std::filesystem::path& trackb,
                               int laps,
                               int direction,
                               std::int8_t lap_skip,
                               int slot,
                               VehicleTrackCourse& course,
                               std::string& error);

// The grid: the ForwardStartingPosition (BackwardStartingPosition for a
// reverse race) marker whose +0x0C number is `slot`, the first in file
// order, as 0x0042265D matches it. Its two horizontal slots are the centre
// of mass's position and its 16-bit angle the heading, used as it is
// (0x0042276B copies it; the start line's own angle is turned by 0x4000 at
// 0x0047CB39, the grid's is not). The ground is the first surface a column
// meets from 2 above the marker's height down to 8 below it; retail copies
// the marker's height (0x00422766), which on track 1003 lies 3 mm above
// the road.
bool vehicle_track_grid_pose(const VehicleTrackCourse& course,
                             int slot,
                             VehiclePose& pose,
                             int& marker,
                             std::string& error);

// A synthetic course: a collision image of the quads, and, when the loop
// holds three or more points, a closed route through them in race order -
// its table holding one node more, a copy of the first, as a retail closed
// path's does - of one forward lap, with the zones (sorted by type, as the
// route layer's index needs) and grid slot 0 at `grid` facing `grid_angle`.
// The path's length is the spline's own, measured by the route layer. With
// no loop there is no route, and the start is the grid point on the first
// surface under it.
// One quad of a synthetic collision image: four corners in perimeter order,
// raw slot order (px, pz, py). px and pz are stored in eighths and py as a
// whole-unit base plus 256ths, as the retail format does, so they must lie
// on those grids and within its ranges; the builder refuses them otherwise.
struct VehicleTrackQuad {
    float corner[4][3] = {};
};

struct VehicleTrackLayout {
    std::string name;
    std::vector<VehicleTrackQuad> quads;
    std::vector<std::array<float, 2>> loop;
    std::vector<TrackPathZone> zones;
    float grid[3] = {0.0F, 0.0F, 0.0F};
    std::uint16_t grid_angle = 0;
};

bool build_vehicle_track_course(const VehicleTrackLayout& layout,
                                VehicleTrackCourse& course,
                                std::string& error);

// The proving ground: a synthetic course built in code, with no retail data,
// that holds one of everything docs/VEHICLE_TRACK.md tests - a seam between
// two road slabs, a slope, walls met square, at 45 and 15 degrees and an
// oblique one, an open edge over no cell, a RESET and a RESET_TO_POINT
// zone, and a pad more than 5000 from the route - with a closed route of 24
// nodes round it. Its layout is in docs/VEHICLE_TRACK.md.
bool build_vehicle_proving_ground(VehicleTrackCourse& course, std::string& error);

// A collision image holding the quads, with each listed in every 32 by 32
// leaf cell whose half-open square [c - 16, c + 16) meets the quad's closed
// horizontal box, and the quadtree over those cells: what
// parse_track_collision reads. Returns an empty image, with the reason, for
// a corner off the grids.
std::vector<unsigned char> build_vehicle_track_collision_image(
    const std::vector<VehicleTrackQuad>& quads,
    std::string& error);

// The separation search of 0x0046B410..0x0046B492 along one contact's
// normal, in float as retail computes it: the move of the first try whose
// reach along the normal - its vertical part held to +-0.15 - is at least
// `needed`, or false after kVehicleTrackSeparationTries tries.
bool vehicle_track_separation(const float normal[3], double needed,
                              VehicleVec3& move) noexcept;

// ---------------------------------------------------------------------------
// Resets
// ---------------------------------------------------------------------------

enum class VehicleTrackReset : std::uint8_t {
    none = 0,
    zone = 1,            // a RESET zone (RECOVERED, 0x00427F53)
    zone_to_point = 2,   // a RESET_TO_POINT zone (RECOVERED, 0x00427F01..0x00427F36)
    off_path = 3,        // more than 5000 from the tracked node (RECOVERED, 0x00426BA2)
    out_of_bounds = 4,   // no collision under the car (RECOVERED, 0x00426B59)
    sunk = 5,            // a surface just above the car (0x0042A51B; query approximated)
    manual = 6,          // asked for; retail's JOY_EVENT_RESET_CAR, handler not traced
    upside_down = 7,     // APPROXIMATION, off by default
    stuck = 8,           // APPROXIMATION, off by default
};

const char* vehicle_track_reset_name(VehicleTrackReset reason) noexcept;

// What a reset did.
struct VehicleTrackResetInfo {
    VehicleTrackReset reason = VehicleTrackReset::none;
    std::uint64_t step = 0;
    bool performed = false;
    // The path node it put the car on, or -1; the zone, or -1.
    int node = -1;
    int zone = -1;
    std::uint16_t heading_units = 0;
    VehiclePose pose;
};

// ---------------------------------------------------------------------------
// The approximations
// ---------------------------------------------------------------------------

// Every value here is invented by this project, not read from the game;
// docs/VEHICLE_TRACK.md gives the reason for each.
struct VehicleTrackApproximations {
    // The body's collision with the track. Off, only the wheels meet it.
    bool body_contact = true;
    // No rule for an upside-down or a stuck car was found for the player;
    // both are off (0) unless a caller asks. Steps the car must spend with
    // its up axis pointing below the horizon, moving slower than the speed,
    // before a reset is asked for.
    std::uint32_t upside_down_steps = 0;
    double upside_down_speed = 2.0;
    // Steps with at least this throttle and less than this speed.
    std::uint32_t stuck_steps = 0;
    double stuck_throttle = 0.5;
    double stuck_speed = 0.5;
};

// What the first lap (issue #159) runs with: everything as above, and the
// upside-down reset on after one second (120 steps), so that an overturned
// car is put back on the route when no reset button is mapped. Retail has
// no such rule for the player; docs/VEHICLE_TRACK.md says why it is on.
VehicleTrackApproximations vehicle_track_first_lap_approximations() noexcept;

// ---------------------------------------------------------------------------
// Telemetry
// ---------------------------------------------------------------------------

enum class VehicleTrackBoundsCheck : std::uint8_t {
    not_run = 0,
    ground = 1,  // a polygon under the car, or one unit along +x
    none = 2,    // neither: out of bounds
};

// One record per step: the model's record for the step, which describes the
// car before its body met the track, then the state the next step starts
// from - after the body's collision and after a reset performed on this
// step - and what this layer did.
struct VehicleTrackTelemetry {
    std::uint64_t step = 0;  // steps of this run, 1 for the first
    VehicleTelemetry model;
    VehicleVec3 position;
    VehicleQuat orientation;
    VehicleVec3 velocity;
    VehicleVec3 angular_velocity;
    std::array<VehicleTrackSurface, kVehicleWheelCount> wheel_surface{};
    // The hull: points whose probe met the track, polygons resolved, and
    // of those the ones met while approaching, which took an impulse.
    std::uint32_t hull_hits = 0;
    std::uint32_t contacts = 0;
    std::uint32_t impulses = 0;
    // The deepest contact: its polygon (or 0xFFFFFFFF), kind and depth.
    std::uint32_t deepest_polygon = 0xFFFFFFFFU;
    VehicleTrackSurface deepest_surface = VehicleTrackSurface::none;
    double deepest_depth = 0.0;
    // The separation and the impulse applied, summed over the contacts.
    VehicleVec3 separation;
    VehicleVec3 impulse;
    // Whether this step ended a 60 Hz tick, and the checks made on it.
    bool tick = false;
    VehicleTrackBoundsCheck bounds = VehicleTrackBoundsCheck::not_run;
    VehicleTrackReset reset = VehicleTrackReset::none;    // performed on this step
    VehicleTrackReset pending = VehicleTrackReset::none;  // asked for, to be performed
    std::uint32_t resets = 0;                              // performed so far
    // The race progress after this step, as the route layer keeps it.
    std::int32_t lap = -1;
    float percent = 0.0F;
    std::int32_t zone = -1;
    bool off_path = false;
};

// The fixed binary form: every double as its IEEE-754 bits, every integer,
// enum and flag as a u32 (the step a u64, the percent its float bits), all
// little-endian, in declaration order, the model's record in its own form.
constexpr std::size_t kVehicleTrackTelemetryRecordBytes =
    8 + kVehicleTelemetryRecordBytes + 24 + 32 + 24 + 24 + kVehicleWheelCount * 4 +
    3 * 4 + 4 + 4 + 8 + 24 + 24 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4;

void encode_vehicle_track_telemetry(
    const VehicleTrackTelemetry& record,
    std::array<std::uint8_t, kVehicleTrackTelemetryRecordBytes>& out) noexcept;

class VehicleTrackTelemetryHash {
public:
    void add(const VehicleTrackTelemetry& record) noexcept;
    std::uint64_t records() const noexcept { return records_; }
    Sha256Digest finish() noexcept;

private:
    Sha256 sha_;
    std::uint64_t records_ = 0;
};

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

// One car on one course. The model and the course must outlive it.
class VehicleTrackSession {
public:
    VehicleTrackSession(const VehicleModel& model,
                        const VehicleTrackCourse& course,
                        const VehicleTrackApproximations& approx = VehicleTrackApproximations{},
                        const VehicleHull& hull = stock_240sx_hull());

    bool valid() const noexcept { return valid_; }
    const std::string& error() const noexcept { return error_; }

    // Puts the car at the pose (VehicleModel::reset) and starts the race
    // progress there. False on an invalid session or a pose the model
    // refuses.
    bool start(const VehiclePose& pose);
    // The same from a state the model produced, as it is: to resume a run,
    // or to start one a pose cannot describe, such as a car upside down.
    // False on an invalid session or a state that is not finite.
    bool start(const VehicleState& state);

    // One step of 1/120 s: the model's step on the track's ground, then the
    // body's collision with the track, then, on every second step, the
    // checks of one 60 Hz tick and any reset they call for. False, with the
    // session left as it was, when not started or when the model refuses
    // the step.
    bool step(const VehicleInput& input, VehicleTrackTelemetry& telemetry);

    // Asks for a reset on the next tick, as the game's reset button would.
    void request_reset() noexcept;

    const VehicleState& state() const noexcept { return state_; }
    const RaceProgress& progress() const noexcept { return progress_; }
    // The race events of the last tick.
    const std::vector<RaceProgressEvent>& events() const noexcept { return events_; }
    const VehicleTrackResetInfo& last_reset() const noexcept { return last_reset_; }
    std::uint64_t steps() const noexcept { return steps_; }

private:
    void resolve_body(VehicleState& s, VehicleTrackTelemetry& t) const;
    void run_tick(VehicleState& s, const VehicleInput& input, VehicleTrackTelemetry& t);
    bool ground_under(double x, double y, double& z) const;
    bool sunk(const VehicleState& s) const;
    bool reset_to_node(VehicleState& s, VehicleTrackReset reason);
    bool reset_to_point(VehicleState& s, int zone);
    static void lead_velocity(VehicleState& s, double speed) noexcept;

    const VehicleModel* model_ = nullptr;
    const VehicleTrackCourse* course_ = nullptr;
    VehicleTrackApproximations approx_;
    TrackVehicleGround ground_;
    // The hull about the centre of mass, as 0x00472E40 leaves it.
    std::vector<VehicleVec3> hull_;
    double mass_ = 0.0;
    double collision_mass_ = 0.0;
    VehicleVec3 inertia_;
    double column_top_ = 0.0;
    double column_bottom_ = 0.0;
    bool valid_ = false;
    std::string error_;

    bool started_ = false;
    VehicleState state_;
    RaceProgress progress_;
    std::vector<RaceProgressEvent> events_;
    std::uint64_t steps_ = 0;
    std::uint64_t ticks_ = 0;
    VehicleTrackReset pending_ = VehicleTrackReset::none;
    std::uint32_t resets_ = 0;
    std::uint32_t upside_down_count_ = 0;
    std::uint32_t stuck_count_ = 0;
    VehicleTrackResetInfo last_reset_;
};

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

// A built-in run: where it starts, relative to the course's start pose, and
// the input it is given. Offsets are in the start's own frame, metres;
// `turn` is added to the heading in 16-bit units.
struct VehicleTrackScenario {
    std::string name;
    std::string description;
    double forward = 0.0;
    double left = 0.0;
    double up = 0.0;
    std::int32_t turn = 0;
    VehicleScript script;
    // The SHA-256 of its telemetry, as docs/VEHICLE_TRACK.md pins it.
    std::string pinned;
};

enum class VehicleTrackCourseKind : std::uint8_t {
    proving_ground = 0,
    circuit = 1,  // event 1 on track 1003, from its grid slot 0
};

const std::vector<VehicleTrackScenario>& vehicle_track_scenarios(VehicleTrackCourseKind kind);
bool vehicle_track_scenario(VehicleTrackCourseKind kind, const std::string& name,
                            VehicleTrackScenario& out);

// The scenario's start: the course start moved by the offsets and turned.
VehiclePose vehicle_track_scenario_pose(const VehiclePose& start,
                                        const VehicleTrackScenario& scenario) noexcept;

// A heading in 16-bit units as radians: units * 2 pi / 65536.
double vehicle_track_heading(std::int64_t units) noexcept;

} // namespace nfsu
