#pragma once

#include "nfsu/sha256.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// The minimum fixed-step vehicle model of issue #58: one car, four wheels, a
// rigid body on four spring-damper struts, a friction-circle tyre, an engine
// on its recovered torque curve and a gearbox with the recovered automatic
// shift rule. docs/VEHICLE.md is the reference: every constant below is
// labelled there as RECOVERED (read from the retail files or speed.exe, with
// its source) or APPROXIMATION (invented here, with its rationale).
//
// DETERMINISM. The step uses only +, -, *, / and comparisons on IEEE-754
// doubles, compiled with floating-point contraction disabled
// (cmake/vehicle_libraries.cmake), plus the square root, sine and cosine
// below, which are built from those same operations. No libm function is
// called in the step, nothing reads a clock, no state is uninitialised and no
// unordered container is iterated. The same inputs therefore give the same
// bits on every platform whose double arithmetic is IEEE-754 binary64 without
// excess precision: x86-64 with SSE2 and AArch64 both are, and src/vehicle.cpp
// refuses to compile where FLT_EVAL_METHOD says otherwise (32-bit x87).
//
// FRAMES AND UNITS. SI units throughout: metres, seconds, kilograms, newtons,
// radians. The world frame is right-handed with z up, which is the raw slot
// order of the track collision records (component 2 is the vertical, a larger
// value is higher), so a track contact adapter passes coordinates through
// unchanged. The body frame is the frame of the CarTypeInfo wheel table:
// +x forward, +y toward the car's left, +z up (docs/PART_PLACEMENT.md). A
// positive steer turns the car to its left, which is a positive yaw rate.

// ---------------------------------------------------------------------------
// The fixed step
// ---------------------------------------------------------------------------

// 120 steps per second, two per tick of the game's 60 Hz world clock
// (0x00585D70 counts 1/60 s ticks: RECOVERED). The halving is an
// APPROXIMATION chosen for stability: the recovered struts are stiff
// (35 kN/m with a progressive term) and the tyre-to-wheel coupling is stiffer
// still, and 120 Hz keeps every explicit term well inside its stability
// bound while still dividing the 60 Hz display rate evenly.
constexpr std::uint32_t kVehicleStepsPerSecond = 120;
constexpr double kVehicleStepSeconds = 1.0 / 120.0;

// ---------------------------------------------------------------------------
// Deterministic arithmetic
// ---------------------------------------------------------------------------

// Square root by Newton's iteration from an exponent-halved first guess, in
// basic operations only. Not guaranteed correctly rounded, but the same bits
// everywhere. Returns 0 for zero, negative and NaN arguments and the argument
// itself for +infinity.
double vehicle_sqrt(double x) noexcept;

// Sine and cosine by Cody-Waite reduction to [-pi/4, pi/4] and Taylor
// polynomials, in basic operations only. Accurate to about 1e-15 for
// |x| < 1e6; outside that range, and for non-finite x, they return 0 and 1.
double vehicle_sin(double x) noexcept;
double vehicle_cos(double x) noexcept;

// ---------------------------------------------------------------------------
// The recovered handling of one car type
// ---------------------------------------------------------------------------

// CarTypeInfo, chunk 0x00034600 of GLOBAL/GLOBALB.BUN: 35 records of 0xC90
// bytes (docs/PART_PLACEMENT.md). Every offset below is relative to the start
// of one record. The setup sub-object at +0x110 is what 0x00575960 copies per
// car instance, and the physics code reaches these fields through a pointer
// to that sub-object, so the code addresses quoted in docs/VEHICLE.md use
// sub-object offsets: record offset = sub-object offset + 0x110.
constexpr std::size_t kVehicleRecordSize = 0xC90;
constexpr std::size_t kVehicleWheelCount = 4;
constexpr std::size_t kVehicleGearSlots = 8;
constexpr std::size_t kVehicleTorquePoints = 9;
constexpr std::size_t kVehicleEngineBrakePoints = 3;

// Gear slot indices, as the game numbers them (0x004757D0): 0 is reverse,
// 1 neutral, 2 first gear, and slot 1 + n is gear n.
constexpr int kVehicleGearReverse = 0;
constexpr int kVehicleGearNeutral = 1;
constexpr int kVehicleGearFirst = 2;

struct VehicleWheelGeometry {
    // +0x120 + k * 0x30: +0x00 longitudinal, +0x04 lateral, +0x08 vertical,
    // body frame, metres. The strut's attachment point.
    float longitudinal = 0.0F;
    float lateral = 0.0F;
    float vertical = 0.0F;
    // +0x10: rolling radius, metres. The suspension compression adds it to
    // the strut's signed distance to the ground (0x00475A82).
    float radius = 0.0F;
};

// One axle's tyre block: +0x1E0 front, +0x200 rear (0x20 bytes each). The
// tyre objects keep a pointer to it (0x005868FC) and its fields are read by
// the force code at 0x005875E8 (+0x00), 0x00587783 (+0x00), 0x005877AD
// (+0x04), 0x005877C9 (+0x08), 0x005878AB (+0x0C) and 0x0058754C (+0x10).
struct VehicleAxleTyre {
    float slip_scale = 0.0F;         // +0x00, compared with and divides a slip in degrees
    float lateral_load_scale = 0.0F; // +0x04, divides the load term of the lateral curve
    float lateral_scale = 0.0F;      // +0x08, multiplies the lateral curve
    float static_friction = 0.0F;    // +0x0C, peak friction coefficient
    float sliding_friction = 0.0F;   // +0x10, sliding friction coefficient
};

// One axle's strut block: +0x280 front, +0x2A0 rear (0x20 bytes each),
// reached at 0x00475A94 as sub-object +0x170 + axle * 0x20. The force rule
// at 0x00475B2E..0x00475BAC is
//   F = (1 + progressive * max(x, 0)) * spring * x + damping(v) * v
//       + preload_k + anti_roll * (x - x_partner),  clamped to F >= 0
// where x is the compression in metres, v its rate, damping(v) is
// damping_bump for v > 0 and damping_rebound otherwise, and preload_k is the
// wheel's share of the car's weight written by 0x00475460 (the lever rule
// about the centre of mass below), so the car rests at x = 0. The record's
// units are kN, kN/m and kN s/m.
struct VehicleAxleStrut {
    float progressive = 0.0F;     // +0x00, 1/m
    float spring = 0.0F;          // +0x04, kN/m
    float damping_bump = 0.0F;    // +0x08, kN s/m, compression rate > 0
    float damping_rebound = 0.0F; // +0x0C, kN s/m, compression rate <= 0
    float anti_roll = 0.0F;       // +0x10, kN/m on the difference within the axle
    float bump_limit = 0.0F;      // +0x14, m: compression is clamped here
    float droop_limit = 0.0F;     // +0x18, m: below this the wheel is off the ground
};

// The fields of one CarTypeInfo record this model reads. Values are kept as
// the stored floats, bit for bit; the model converts them to SI once.
struct VehicleHandling {
    // +0x110, +0x114, +0x118: the centre of mass in the body frame, metres.
    // 0x004745B8..0x004745CF copies it into the rigid body (+0x1D0),
    // 0x00588541..0x00588558 subtracts it from a wheel's position, and the
    // strut preload of 0x00475460 levers the weight about its first
    // component when the flag at 0x006FFDE4 is set, as it is in the retail
    // data.
    std::array<float, 3> centre_of_mass{};
    std::array<VehicleWheelGeometry, kVehicleWheelCount> wheels{};
    std::array<VehicleAxleTyre, 2> tyres{};     // [0] front, [1] rear
    float mass = 0.0F;                          // +0x220, tonnes (weight = 9.8 * mass kN)
    // +0x224, +0x228, +0x22C. Observed only: they equal the real car's
    // length, width and height, and the inertia below is the box inertia of
    // the mass over them; no reader of the three was traced.
    float length = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    // +0x230, +0x244, +0x258: the diagonal of the 4x4 inertia matrix at
    // +0x230, tonnes m^2 about the body axes. The rigid body keeps a pointer
    // to +0x220 (0x0047415D, 0x00584806) and 0x00584B3D inverts the matrix
    // at pointer +0x10. The other twelve entries are zero in every retail
    // record.
    std::array<float, 3> inertia{};
    std::array<VehicleAxleStrut, 2> struts{};   // [0] front, [1] rear
    float shift_fraction = 0.0F;                // +0x2C0
    float final_drive_rear = 0.0F;              // +0x2C8, sub +0x1B8: wheels 2 and 3
    float final_drive_front = 0.0F;             // +0x2CC, sub +0x1BC: wheels 0 and 1
    float rear_drive_fraction = 0.0F;           // +0x2D0, sub +0x1C0
    std::uint32_t forward_gears = 0;            // +0x2D8, sub +0x1C8
    std::array<float, kVehicleGearSlots> gear_ratios{}; // +0x2E0, sub +0x1D0
    float idle_rpm = 0.0F;                      // +0x300, sub +0x1F0
    float redline_rpm = 0.0F;                   // +0x304, sub +0x1F4
    float max_rpm = 0.0F;                       // +0x308, sub +0x1F8
    std::array<float, kVehicleTorquePoints> torque{};              // +0x310, kN m
    std::array<float, kVehicleEngineBrakePoints> engine_brake{};   // +0x358, kN m
    float drag = 0.0F;                          // +0x370, kN per (m/s)^2
};

// Record offsets of the fields above, for decoders and for reports.
constexpr std::size_t kVehicleCentreOfMassOffset = 0x110;
constexpr std::size_t kVehicleInertiaOffset = 0x230;
constexpr std::size_t kVehicleInertiaStride = 0x14;
constexpr std::size_t kVehicleWheelOffset = 0x120;
constexpr std::size_t kVehicleWheelStride = 0x30;
constexpr std::size_t kVehicleTyreOffset = 0x1E0;
constexpr std::size_t kVehicleAxleStride = 0x20;
constexpr std::size_t kVehicleMassOffset = 0x220;
constexpr std::size_t kVehicleLengthOffset = 0x224;
constexpr std::size_t kVehicleWidthOffset = 0x228;
constexpr std::size_t kVehicleHeightOffset = 0x22C;
constexpr std::size_t kVehicleStrutOffset = 0x280;
constexpr std::size_t kVehicleShiftFractionOffset = 0x2C0;
constexpr std::size_t kVehicleFinalRearOffset = 0x2C8;
constexpr std::size_t kVehicleFinalFrontOffset = 0x2CC;
constexpr std::size_t kVehicleRearFractionOffset = 0x2D0;
constexpr std::size_t kVehicleForwardGearsOffset = 0x2D8;
constexpr std::size_t kVehicleGearRatioOffset = 0x2E0;
constexpr std::size_t kVehicleIdleRpmOffset = 0x300;
constexpr std::size_t kVehicleRedlineRpmOffset = 0x304;
constexpr std::size_t kVehicleMaxRpmOffset = 0x308;
constexpr std::size_t kVehicleTorqueOffset = 0x310;
constexpr std::size_t kVehicleEngineBrakeOffset = 0x358;
constexpr std::size_t kVehicleDragOffset = 0x370;

// The 240SX's record, index 18 of chunk 0x00034600 in the retail
// Global/GLOBALB.BUN (SHA-256 39DF38FA...8DB9), at absolute file offset
// 1736752. A small numeric table from a retail file, kept in the source
// under the owner's rule of PR #169 comment 3860: it states its source and
// the SHA-256 of the bytes it reproduces, and nfsu_vehicle_tests rebuilds
// those bytes from it. It reproduces 316 of the record's 3216 bytes, the 79
// fields encode_vehicle_handling writes, in 23 runs from +0x110 to +0x373
// (docs/VEHICLE.md lists them); every other byte of the record is not held
// here. Those 316 bytes, taken in offset order, hash to
// kVehicle240sxRecordBytesSha256. `nfsu_probe vehicle-sim --handling`
// compares a decoded record against this table field by field.
VehicleHandling stock_240sx_handling();
constexpr std::size_t kVehicle240sxRecordBytes = 316;
constexpr const char* kVehicle240sxRecordBytesSha256 =
    "54394827CC13AEE7DE095524810B790A238FFC24763E7C2A2BA236A1A2430DBD";

struct VehicleHandlingIssue {
    // Offset of the offending field inside the record.
    std::size_t offset = 0;
    std::string message;
};

// Decodes the fields above out of one CarTypeInfo record, little-endian, and
// checks that the model can run on them: every float finite, mass, radii,
// friction and final drives positive, idle < redline <= max rpm, 1 to 6
// forward gears each with a positive ratio, a rear drive fraction in [0, 1],
// droop_limit < 0 < bump_limit. `size` must be exactly kVehicleRecordSize.
// Never throws; returns false and fills `issues` on any refusal.
bool decode_vehicle_handling(const unsigned char* record,
                             std::size_t size,
                             VehicleHandling& out,
                             std::vector<VehicleHandlingIssue>& issues);

// The same checks on a table built in memory.
bool check_vehicle_handling(const VehicleHandling& handling,
                            std::vector<VehicleHandlingIssue>& issues);

// Writes the fields above into a record image at their offsets and leaves
// every other byte untouched: the inverse of decode_vehicle_handling, for
// fixtures.
void encode_vehicle_handling(const VehicleHandling& handling,
                             unsigned char* record,
                             std::size_t size) noexcept;

// Field-by-field comparison of the stored bits. Returns the record offset of
// the first differing field, or kVehicleRecordSize when they are equal.
std::size_t compare_vehicle_handling(const VehicleHandling& a,
                                     const VehicleHandling& b) noexcept;

// ---------------------------------------------------------------------------
// The approximations
// ---------------------------------------------------------------------------

// Every value here is invented by this project, not read from the game.
// docs/VEHICLE.md gives the rationale for each.
struct VehicleApproximations {
    // The record stores tonnes and kN, which the weight rule at 0x00463C51
    // (9.8 * mass) and the strut values (35 kN/m) imply; the model runs in
    // kg and N. The factor is an inference about units, not a traced value.
    double unit_scale = 1000.0;
    // Wheel, tyre and brake disc about the axle, kg m^2.
    double wheel_inertia = 1.2;
    // Brake torque per wheel at full brake input, N m: below what the
    // recovered static friction holds on each axle at the loads full braking
    // puts on it, so no wheel locks, and about 1.05 g from the brakes alone.
    // The handbrake has no torque of its own: 0x00461E45 makes it the brake
    // axis at its full value when no service brake is applied.
    double brake_torque_front = 1400.0;
    double brake_torque_rear = 450.0;
    // Road-wheel steering lock at standstill, rad, and the speed at which
    // the lock has halved, m/s: lock(v) = max_steer / (1 + (v / ref)^2).
    // Falling with the square of the speed keeps what a full input asks of
    // the tyres much the same at every speed, as the path radius a given
    // acceleration allows grows with the square of the speed: with the
    // tyres below, 0.96 to 1.34 g from 30 down to 15 m/s, under the 1.55 g
    // the car holds at the limit (docs/VEHICLE.md, "Drivability").
    double max_steer = 0.55;
    double steer_speed_reference = 12.0;
    // How fast the road wheels follow the input, rad/s.
    double steer_rate = 3.0;
    // Tyre force per unit load per unit slip, before the friction limit:
    // longitudinal per unit slip ratio, lateral per unit tan(slip angle).
    // The rear is twice as stiff as the front so that the car understeers
    // below the limit and full steering input does not spin it
    // (docs/VEHICLE.md, "The spin matrix").
    double longitudinal_stiffness = 12.0;
    double cornering_stiffness_front = 10.0;
    double cornering_stiffness_rear = 20.0;
    // The slip denominator never falls below this speed, m/s, so the tyre is
    // a damper rather than a divide by zero at a standstill.
    double slip_speed_floor = 2.0;
    // Rolling resistance per unit load, and the speed over which it fades in
    // from zero, m/s.
    double rolling_resistance = 0.015;
    double rolling_fade_speed = 0.5;
    // Bump stop rate beyond the strut's bump limit, N/m.
    double bump_stop_rate = 200000.0;
    // The ground probe starts this far above each strut's attachment point,
    // so a strut pushed into a slope still finds the surface, m.
    double probe_lift = 0.5;
    // Multiplies the recovered engine braking curve. At full strength it
    // brakes the car at about 0.5 g in first gear, where the recovered
    // automatic gearbox keeps it below 64 km/h, and with the brake on it
    // locks the rear wheels.
    double engine_brake_scale = 0.5;
    // Where a tyre's force acts, as a fraction of the way from the contact
    // point up to the centre of mass's height along the body's vertical: 0
    // at the contact, 1 at the centre of mass (no load transfer at all).
    // At the contact, the recovered centre of mass (about 0.5 m up) and
    // friction (1.8) roll the car over at about 1.46 g, before the tyres
    // slide; the game does not, so its tyre forces cannot act there with
    // these values, and where they act was not traced. 0.6 leaves an
    // effective height of about 0.2 m. At most 1.
    double tyre_force_lift = 0.6;
    // Limit the engine's torque at each wheel on the ground, driving or
    // braking, to what the tyre's peak friction has left beside the lateral
    // force its slip angle asks for: |T| <= radius * sqrt((mu_static *
    // load)^2 - Fy^2). An ideal traction control, which stops power
    // oversteer and lift-off oversteer at full steering input; the recovered
    // wheel coupling at 0x00475667..0x00475797 was not reproduced, so how the
    // game keeps its cars from them is unknown.
    bool traction_limit = true;
    // Steps the automatic gearbox waits after a shift before it may shift
    // again: 36 is 0.3 s.
    std::uint32_t auto_shift_hold_steps = 36;
};

struct VehicleSpec {
    VehicleHandling handling;
    VehicleApproximations approx;
};

// stock_240sx_handling() with the default approximations.
VehicleSpec stock_240sx_vehicle_spec();

// ---------------------------------------------------------------------------
// The ground the caller supplies
// ---------------------------------------------------------------------------

struct VehicleVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// #275 low-support angular damping at 0x00474DD0..0x00474E85.
// Curve pairs preserve binary32 source values. Torque uses SI mass and
// world angular velocity; state 1 or >=2 grounded wheels disables it.
// The zero-wheel alignment pass after 0x00474E8B is separate and unresolved.
std::array<std::array<float, 2>, 3> vehicle_low_support_curve() noexcept;
VehicleVec3 vehicle_low_support_torque(const VehicleVec3& angular_velocity,
                                     double mass, unsigned grounded_wheels,
                                     unsigned collision_state) noexcept;

// Unit quaternion, body to world.
struct VehicleQuat {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct VehicleGroundHit {
    bool hit = false;
    // Distance from the segment's start to the surface, metres.
    double distance = 0.0;
    // Unit surface normal in the world frame, facing up out of the ground.
    VehicleVec3 normal{0.0, 0.0, 1.0};
    // Surface identifier, passed through to telemetry. The retail surface
    // table at 0x006EF2F0 (stride 0x94) scales friction per surface; this
    // model uses surface 0's factors, 1.0 and 1.0, for every surface.
    std::uint32_t surface = 0;
};

// What the model asks of the world: for each wheel, once a step, the first
// ground surface along a segment from `from`, in the unit direction
// `direction` (the car body's down axis), no longer than `length`. Issue #60
// implements this over the track collision; an implementation must answer
// deterministically and must not keep state between calls that changes an
// answer. The model treats as a miss a hit whose distance is not on the
// segment or whose normal is not finite, has no length, or faces away from
// the car, and it makes the normal unit length itself.
class VehicleGround {
public:
    virtual ~VehicleGround() = default;
    virtual VehicleGroundHit cast(const VehicleVec3& from,
                                  const VehicleVec3& direction,
                                  double length) const = 0;
};

// The horizontal plane z = height, facing up, surface 0.
class VehicleFlatGround final : public VehicleGround {
public:
    explicit VehicleFlatGround(double height = 0.0) noexcept : height_(height) {}
    VehicleGroundHit cast(const VehicleVec3& from,
                          const VehicleVec3& direction,
                          double length) const override;

private:
    double height_ = 0.0;
};

// ---------------------------------------------------------------------------
// Input, state and telemetry
// ---------------------------------------------------------------------------

enum class VehicleShift : std::uint8_t {
    none = 0,
    up = 1,
    down = 2,
};

enum class VehicleGearbox : std::uint8_t {
    // The recovered rule of 0x004757D0: first gear or above, shifted up and
    // down by engine speed. A down request at a standstill in first selects
    // reverse, and an up request in reverse selects first.
    automatic = 0,
    // Up and down requests move one slot at a time, reverse to top gear.
    manual = 1,
};

// One step's controls. Throttle and brake are 0..1, steer -1 (right) to +1
// (left). A value out of range is clamped and a non-finite one read as 0, so
// a caller cannot put the state out of its domain. `shift` is an edge: it
// acts on the step it is given on.
struct VehicleInput {
    double throttle = 0.0;
    double brake = 0.0;
    double steer = 0.0;
    bool handbrake = false;
    VehicleShift shift = VehicleShift::none;
    // #251 caller-computed road-wheel angle. When enabled, this replaces
    // the approximate lock AND slew. Legacy input/telemetry bytes omit it.
    bool road_steering = false;
    double road_angle = 0;
    double road_target = 0;
    double steer_history = 0;
    double steer_limit_deg = 0, steer_rate_deg_s = 0, steer_shift_factor = 0;
    int steering_mode = 0;
};

struct VehicleWheelState {
    // Rolling speed of the wheel, rad/s; positive rolls the car forward.
    double spin = 0.0;
    // Strut compression as the force rule uses it, metres, clamped to
    // [droop_limit, bump_limit], and its value one step earlier.
    double compression = 0.0;
    double previous_compression = 0.0;
    bool contact = false;
};

struct VehicleState {
    std::uint64_t step = 0;
    VehicleVec3 position;          // centre of mass, world
    VehicleQuat orientation;       // body to world
    VehicleVec3 velocity;          // world, m/s
    VehicleVec3 angular_velocity;  // world, rad/s
    std::array<VehicleWheelState, kVehicleWheelCount> wheels{};
    double steer_angle = 0.0;      // road-wheel angle of wheels 0 and 1, rad
    double engine_rpm = 0.0;       // clamped to [idle, max], as the game stores it
    double wheel_rpm = 0.0;        // the same before the clamp
    int gear = kVehicleGearFirst;
    VehicleGearbox gearbox = VehicleGearbox::automatic;
    // Steps left before the automatic gearbox may shift again.
    std::uint32_t shift_hold = 0;
};

// Where reset() puts the car: the ground point under the centre of mass,
// the heading (angle of the body's +x axis from world +x, counter-clockwise
// seen from above) and a forward speed. The ground there is taken as level.
// The state's position is the centre of mass; the body frame's origin, which
// the car's meshes are placed in, is position - R * centre_of_mass.
struct VehiclePose {
    VehicleVec3 ground;
    double heading = 0.0;
    double speed = 0.0;
    VehicleGearbox gearbox = VehicleGearbox::automatic;
};

struct VehicleWheelTelemetry {
    double compression = 0.0;
    double load = 0.0;          // strut force, N
    double spin = 0.0;          // rad/s
    double slip_ratio = 0.0;    // (spin * radius - v_long) / max(|v_long|, floor)
    double slip_lateral = 0.0;  // v_lat / max(|v_long|, floor)
    VehicleVec3 ground_point, ground_normal; // diagnostic only; omitted from bytes
    double force_long = 0.0;    // N, along the wheel's heading on the ground
    double force_lat = 0.0;     // N, toward the wheel's left on the ground
    double drive_torque = 0.0;  // N m from the engine, as the recovered split gives it
    double drive_applied = 0.0; // N m of it that reached the wheel, after the traction limit
    std::uint32_t surface = 0;
    bool contact = false;
    bool sliding = false;
};

// One record per step, describing the state at the END of the step and the
// forces that produced it.
struct VehicleTelemetry {
    std::uint64_t step = 0;     // steps completed, 1 for the first step
    VehicleVec3 position;
    VehicleVec3 velocity;
    VehicleVec3 forward;        // the body's +x axis, world
    VehicleVec3 angular_velocity;
    double forward_speed = 0.0; // velocity along `forward`, m/s
    double lateral_speed = 0.0; // velocity along the body's +y axis, m/s
    double engine_rpm = 0.0;
    double wheel_rpm = 0.0;
    double engine_torque = 0.0; // N m at the crankshaft
    int gear = kVehicleGearFirst;
    double throttle = 0.0;      // the inputs as applied, after clamping
    double brake = 0.0;
    double steer = 0.0;
    bool handbrake = false;
    double steer_angle = 0.0;
    // Diagnostic only; omitted from the legacy binary encoding and hash.
    double requested_steer_angle = 0.0; // rad, before road-wheel slew
    VehicleVec3 low_support_torque; // N m; diagnostic, omitted from legacy bytes
    unsigned grounded_wheels = 0; // support counted before integration
    double steering_history = 0.0; // diagnostic; omitted from legacy bytes
    double steering_limit_deg = 0, steering_rate_deg_s = 0, steering_shift_factor = 0;
    int steering_mode = 0;
    std::array<VehicleWheelTelemetry, kVehicleWheelCount> wheels{};
};

// The fixed binary form of one record: every double as its IEEE-754 bits,
// every integer and flag as an unsigned integer, all little-endian, in the
// order docs/VEHICLE.md lists. It is what the telemetry hash covers: the
// step (u64), four vectors, five doubles, the gear (u32), three doubles,
// the handbrake (u32), the steer angle, then per wheel nine doubles and
// three u32 (surface, contact, sliding).
constexpr std::size_t kVehicleTelemetryRecordBytes =
    8 + 4 * 24 + 5 * 8 + 4 + 3 * 8 + 4 + 8 + kVehicleWheelCount * (9 * 8 + 3 * 4);
void encode_vehicle_telemetry(const VehicleTelemetry& record,
                              std::array<std::uint8_t, kVehicleTelemetryRecordBytes>& out) noexcept;

// The SHA-256 of the encoded telemetry of the built-in "mixed" script, 2400
// steps from rest on flat ground with the stock 240SX, as the desktop Debug
// and Release builds produce it: the pin docs/VEHICLE.md records, which
// nfsu_vehicle_tests requires and nfsu_switch_vehicle compares the
// console's run against.
constexpr const char* kVehiclePinnedMixedSha256 = "D1A186D7F3ED92AE63D0E5A4F3BEF471B3922FCB408FFD1B5DEF7C6182689A5C";

// SHA-256 over the encoded records of a run, in step order.
class VehicleTelemetryHash {
public:
    void add(const VehicleTelemetry& record) noexcept;
    std::uint64_t records() const noexcept { return records_; }
    Sha256Digest finish() noexcept;

private:
    Sha256 sha_;
    std::uint64_t records_ = 0;
};

// ---------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------

class VehicleModel {
public:
    // Checks the handling (check_vehicle_handling) and the approximations,
    // then derives everything the step needs once: SI values, inertia, the
    // torque curves and the shift tables. A model that is not valid refuses
    // every reset and step.
    explicit VehicleModel(const VehicleSpec& spec);

    bool valid() const noexcept { return valid_; }
    const std::string& error() const noexcept { return error_; }
    const VehicleSpec& spec() const noexcept { return spec_; }

    // The automatic gearbox's thresholds by gear slot, from the recovered
    // rule at 0x0046D1C0: shift up from slot g when the wheel-derived rpm
    // exceeds upshift_rpm()[g], down when it falls below downshift_rpm()[g].
    const std::array<double, kVehicleGearSlots>& upshift_rpm() const noexcept { return upshift_; }
    const std::array<double, kVehicleGearSlots>& downshift_rpm() const noexcept { return downshift_; }

    // The recovered torque curves at an engine speed, kN m as stored
    // (0x00583EA0 over the 9 points and the 3 points).
    double torque_curve(double rpm) const noexcept;
    double engine_brake_curve(double rpm) const noexcept;

    // Puts the car on its struts with its centre of mass over the pose's
    // ground point, which it takes as level: every strut at zero
    // compression, where the recovered preloads carry the weight, the body
    // pitched as the two axles' different radii put it, moving forward at
    // the pose's speed with the wheels rolling at it, first gear in either
    // gearbox mode, and then the automatic shift rule applied once. Returns
    // false on an invalid model or a non-finite pose.
    bool reset(VehicleState& state, const VehiclePose& pose) const;

    // Advances the state by kVehicleStepSeconds. Returns false, leaving the
    // state untouched, on an invalid model or a state that is not finite.
    bool step(VehicleState& state,
              const VehicleInput& input,
              const VehicleGround& ground,
              VehicleTelemetry& telemetry) const;

private:
    struct Derived {
        double mass = 0.0;
        double weight = 0.0;
        VehicleVec3 inertia;            // body-frame principal moments
        VehicleVec3 centre_of_mass;     // body frame
        // Each strut's attachment point relative to the centre of mass,
        // body frame, and its static preload (0x00475460), N.
        std::array<VehicleVec3, kVehicleWheelCount> attach{};
        std::array<double, kVehicleWheelCount> preload{};
        std::array<double, kVehicleWheelCount> radius{};
        std::array<double, 2> progressive{};
        std::array<double, 2> spring{};
        std::array<double, 2> damping_bump{};
        std::array<double, 2> damping_rebound{};
        std::array<double, 2> anti_roll{};
        std::array<double, 2> bump_limit{};
        std::array<double, 2> droop_limit{};
        std::array<double, 2> static_friction{};
        std::array<double, 2> sliding_friction{};
        double drag = 0.0;
    };

    void update_engine(VehicleState& state) const noexcept;
    double crank_torque(const VehicleState& state, double throttle) const noexcept;

    VehicleSpec spec_;
    Derived d_;
    std::array<double, kVehicleGearSlots> upshift_{};
    std::array<double, kVehicleGearSlots> downshift_{};
    bool valid_ = false;
    std::string error_;
};

// The uniform-table curve of 0x00583EA0: `count` values spread evenly from
// x0 to x1. u = (x - x0) * (count - 1) / (x1 - x0); i = u truncated toward
// zero; below index 0 the first value, at or past the last index the last
// value, otherwise values[i] and values[i + 1] mixed by u - floor(u). The
// truncation and the floor differ for -1 < u < 0, and the recovered code
// keeps that quirk; so does this.
double vehicle_uniform_curve(const float* values, std::size_t count, double x0,
                             double x1, double x) noexcept;

// ---------------------------------------------------------------------------
// Scripted input sequences
// ---------------------------------------------------------------------------

struct VehicleScriptSegment {
    std::uint32_t steps = 0;
    VehicleInput input;
};

struct VehicleScript {
    std::string name;
    // Initial forward speed, m/s, and gearbox mode.
    double speed = 0.0;
    VehicleGearbox gearbox = VehicleGearbox::automatic;
    std::vector<VehicleScriptSegment> segments;

    std::uint64_t total_steps() const noexcept;
    // The input on step `index` (0-based): the segment covering it, with its
    // shift request only on the segment's first step. Past the end, no input.
    VehicleInput input_at(std::uint64_t index) const noexcept;
};

struct VehicleScriptLimits {
    std::size_t max_bytes = 1U << 20;
    std::size_t max_segments = 100000;
    std::uint64_t max_total_steps = 12000000;  // 100000 s
};

// The text form, one directive per line, '#' starting a comment line:
//   speed <m/s>          optional, before any segment; initial forward speed
//   gearbox <automatic|manual>   optional, before any segment
//   <steps> <throttle> <brake> <steer> <handbrake 0|1> <none|up|down>
// Numbers are fixed decimals: an optional '-', at most 9 integer digits, an
// optional '.' and at most 6 decimals, parsed exactly as integer / 10^k so
// that every platform reads the same double. Refusals name the line.
bool parse_vehicle_script(const std::string& text,
                          VehicleScript& out,
                          std::string& error,
                          const VehicleScriptLimits& limits = VehicleScriptLimits{});

// The built-in scripts, by name: "rest", "accelerate", "brake", "corner",
// "slide", "top-speed", "manual" and "mixed". docs/VEHICLE.md says what each
// one drives. Returns false for an unknown name.
bool builtin_vehicle_script(const std::string& name, VehicleScript& out);
const std::vector<std::string>& builtin_vehicle_script_names();

// Parses one fixed decimal as the script parser does. Exposed for tests.
bool parse_vehicle_decimal(const std::string& text, double& out) noexcept;

} // namespace nfsu
