#pragma once

#include "nfsu/vehicle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace nfsu {

// The chase camera of issue #59: where the camera sits behind the car, which
// way it looks, how it lags behind a turn, and how it stays out of a wall.
// docs/CONTROLS_CAMERA.md labels every rule RECOVERED (read from speed.exe
// or GLOBALB.BUN, with the address) or APPROXIMATION (invented here, with the
// reason).
//
// What speed.exe does, as far as it was traced. The in-race camera is built
// by the switch at 0x0042C7D7, case 1 (the object of 0x3A0 bytes, vtable
// 0x006C7FC8, update 0x00489690), which the name "CubicCameraMover(DCC)" at
// 0x006C8160 fits. Each car type carries its own camera anchors at
// CarTypeInfo +0xB50, one per point of view (POV); the update looks up the
// anchor of the current POV (0x0047D680), turns a target heading out of the
// car's heading and its velocity, lags behind it with an angle spring, and
// places the eye at the anchor's offset in the lagged frame. POVs 0, 1 and 4
// get that chase treatment (the id tests at 0x00489E5E..0x00489E6D); 2 and 3
// are fixed to the car and are not implemented here.
//
// DETERMINISM. Angles are kept as speed.exe keeps them, 65536 units to the
// turn in 16-bit integers, so the lag and every wrap are integer arithmetic.
// The floating-point rest uses +, -, *, /, comparisons and the vehicle
// library's vehicle_sqrt, vehicle_sin and vehicle_cos, which are built from
// those; the arctangent below is too. Nothing calls a library mathematics
// function or reads a clock, and the library is compiled with contraction
// disabled (cmake/drive_libraries.cmake), so a recorded input stream gives
// the same camera on the desktop and on the console.

// ---------------------------------------------------------------------------
// Angles
// ---------------------------------------------------------------------------

// 65536 units to the turn, as speed.exe's 16-bit angles.
constexpr double kChaseAngleUnitsPerTurn = 65536.0;

// The arctangent, in basic operations only (fdlibm's reduction and
// polynomial), radians. Returns 0 for NaN.
double chase_atan(double x) noexcept;

// The angle of the vector (x, y), counter-clockwise from +x, in 16-bit units
// rounded to the nearest (half away from zero) and wrapped to 0..65535; 0
// for the zero vector and for a vector that is not finite. It stands in for
// speed.exe's bATan (0x00564DB0), whose own rounding was not traced.
std::uint16_t chase_angle(double x, double y) noexcept;

// An angle in units, as radians and as degrees; the degrees are exact.
double chase_angle_radians(std::uint16_t units) noexcept;
double chase_angle_degrees(std::uint16_t units) noexcept;

// ---------------------------------------------------------------------------
// The anchors: CarTypeInfo +0xB50
// ---------------------------------------------------------------------------

// One 16-byte anchor. The reader is 0x0047D680, which matches the POV
// against +0x0; the update reads the rest at 0x00489EB3..0x00489EFC.
struct ChaseCameraAnchor {
    // +0x0: the point of view this anchor is for.
    std::int16_t pov = 0;
    // +0x2: an angle, 65536 units to the turn, read into the eye's pitch
    // (0x0048AAF5..0x0048AB2D rotates by its negation). Read here as a
    // look-down angle: it is 0 for POVs 0 and 1 and 3040 (16.70 degrees)
    // for POV 4, the high one. The axis of the rotation at 0x0040BA40 was
    // not traced, so "down" is an inference from which POV has it.
    std::uint16_t pitch = 0;
    // +0x4: along the car's heading, metres; negative is behind. The update
    // negates it into the camera's distance (0x00489ECB, 0x00489EDE).
    float along = 0.0F;
    // +0x8: height, metres.
    float height = 0.0F;
    // +0xC: across, metres, toward the car's left in this model's frame
    // (docs/VEHICLE.md). 0 for every chase anchor of every car.
    float across = 0.0F;
};

constexpr std::size_t kChaseCameraBlockOffset = 0xB50;
constexpr std::size_t kChaseCameraBlockBytes = 0xB0;
constexpr std::size_t kChaseCameraListCapacity = 5;

// The block: two counts, eight bytes that every retail record holds as zero,
// then two lists of up to five anchors. 0x0047D680 reads the second list,
// at +0x60 with the count at +0x4, when the view layout at 0x007364A0 is 3,
// and the first, at +0x10 with the count at +0x0, otherwise. 0x00408370
// sets that layout from which of three view lists hold a view: 3 when the
// first and the second do, which reads as two players sharing the screen.
// A single-player race uses the first list.
struct ChaseCameraTable {
    std::uint32_t single_count = 0;
    std::uint32_t shared_count = 0;
    std::array<ChaseCameraAnchor, kChaseCameraListCapacity> single{};
    std::array<ChaseCameraAnchor, kChaseCameraListCapacity> shared{};
};

// Decodes the block out of one CarTypeInfo record of kVehicleRecordSize
// bytes, little-endian. Refused, naming the offset: a size that is not the
// record's, a count above 5 (the first list would run into the second), a
// POV id outside 0..4 in a counted entry, a POV named twice in one list,
// and a counted chase anchor (POV 0, 1 or 4) whose offsets are not finite
// or whose `along` is not negative.
bool decode_chase_camera_table(const unsigned char* record,
                               std::size_t size,
                               ChaseCameraTable& out,
                               std::string& error);

// Writes the block's 176 bytes at +0xB50 of a record image; every other
// byte, and the eight zero bytes, are left as they are.
void encode_chase_camera_table(const ChaseCameraTable& table,
                               unsigned char* record,
                               std::size_t size) noexcept;

// The 240SX's block, record 18 of chunk 0x00034600 of the retail
// Global/GLOBALB.BUN (SHA-256 39DF38FA...8DB9), bytes 1739648..1739823 of the
// file (+0xB50..+0xBFF of the record at 1736752). A small numeric table from
// a retail file, kept under the owner's rule of PR #169 comment 3860:
// nfsu_drive_tests writes it into an all-zero record image and requires the
// 176 bytes to hash to kChaseCamera240sxBlockSha256, the hash of the same
// bytes in the retail file.
ChaseCameraTable stock_240sx_chase_camera_table();
constexpr const char* kChaseCamera240sxBlockSha256 =
    "608892D5C3C134AD0CB55BBCAD56A343C3563766513BD70D61067EDE771C9EE9";

// The anchor of `pov` in the single-player list, or in the shared-screen
// one; false when the list does not name it.
bool find_chase_anchor(const ChaseCameraTable& table,
                       bool shared,
                       int pov,
                       ChaseCameraAnchor& out) noexcept;

// ---------------------------------------------------------------------------
// The recovered rules' constants
// ---------------------------------------------------------------------------

// Constants of speed.exe the update reads, as the floats and words it
// holds, all RECOVERED; docs/CONTROLS_CAMERA.md gives each reader.
struct ChaseCameraRules {
    // 0x006FBDF4: the field of view per POV 0..4, in angle units, written
    // into the view (+0xC4) at 0x0048ACA4. 13020 is 71.52 degrees.
    std::array<std::uint16_t, 5> fov{{13020, 13020, 16384, 16384, 13020}};
    // 0x00705B10, through the descriptor at 0x00705B20 and the evaluator at
    // 0x00583F20: the weight of the velocity against the heading, 0 at or
    // below 8 m/s and 1 at or above 13, linear in between.
    float blend_speed_low = 8.0F;
    float blend_weight_low = 0.0F;
    float blend_speed_high = 13.0F;
    float blend_weight_high = 1.0F;
    // 0x006B6C70 and 0x006B6C74: the lag spring of 0x00489300,
    //   rate(a) = -sign(a) * lag_gain * atan16(lag_scale * |a|)
    // in units per second for a lag of `a` units, atan16 being the
    // arctangent in angle units.
    float lag_scale = 0.000261F;
    float lag_gain = 1.9088F;
};

// The bytes those constants reproduce, in this order: the five FOV words
// (0x006FBDF4, 10 bytes), the two blend pairs (0x00705B10, 16 bytes) and the
// two lag floats (0x006B6C70, 8 bytes), 34 bytes that hash to this value in
// speed.exe (SHA-256 C0FAD450...74B7). nfsu_drive_tests rebuilds them.
constexpr std::size_t kChaseCameraRuleBytes = 34;
constexpr const char* kChaseCameraRulesSha256 =
    "BF6027AE2C9B62D8E3FFF05782302CAF98D178BD3AAF1BD1D52D90F800874B52";
void encode_chase_camera_rules(const ChaseCameraRules& rules,
                               std::array<std::uint8_t, kChaseCameraRuleBytes>& out) noexcept;

// ---------------------------------------------------------------------------
// The approximations
// ---------------------------------------------------------------------------

// How the POV's angle becomes the renderer's field of view. That the angle
// is the horizontal field of view is an INFERENCE: the projection that reads
// view +0xC4 was not traced. It is what EA's renderers of the period hold,
// and the game drew 4:3, where 71.52 degrees across is 56.75 degrees high.
enum class ChaseFovMode : std::uint8_t {
    // The angle is the horizontal field of view whatever the viewport: a
    // wider screen shows the same width and less height, as the retail game
    // would on a 16:9 mode. On 16:9 the chase POVs are 44.11 degrees high.
    horizontal,
    // The angle is the horizontal field of view of a 4:3 view, and the
    // vertical one that gives - 56.75 degrees for the chase POVs - is kept
    // on any viewport, so a 16:9 screen shows more to the sides instead of
    // less above and below. The default: the Switch's screen is 16:9, and
    // this is the picture the game drew on its own 4:3 screen, widened.
    vertical_4_3
};

// Everything invented, with the reason in docs/CONTROLS_CAMERA.md.
struct ChaseCameraApproximations {
    ChaseFovMode fov_mode = ChaseFovMode::vertical_4_3;
    // Clip planes, m. 0.5 is the value 0x0048ACD0 writes into the view at
    // +0xBC for every chase POV; that +0xBC is the near plane was not
    // traced. The far plane is invented: far enough for a street.
    double near_plane = 0.5;
    double far_plane = 2000.0;
    // Camera collision. The segment from the pivot - the anchor point
    // raised to the eye's height - to the eye is asked of the world; a hit
    // pulls the eye in along it to `collision_margin` short of the hit, at
    // once, and once the way is clear the eye moves back out at
    // `collision_return_rate` m/s. No world query was found on the traced
    // path of 0x00489690; its one obstruction rule keeps the eye out of a
    // sphere around its own car (0x0048A96F..0x0048AA60), which a chase
    // anchor 4.7 m or more behind does not reach, and it is not reproduced.
    double collision_margin = 0.2;
    double collision_return_rate = 6.0;
    // The lag factor speed.exe keeps at view +0x68 and ramps by 0.24316 a
    // frame toward 1 when car +0x3CC is above 2 and toward 0 otherwise
    // (0x0048A389..0x0048A3F0); what +0x3CC holds was not traced, so the
    // factor is held at 1, its value from the constructor (0x0048947B).
    double lag_factor = 1.0;
};

// ---------------------------------------------------------------------------
// The world a camera may collide with
// ---------------------------------------------------------------------------

// The caller's collision, as the vehicle model takes its ground: issue
// #60's track contact layer can answer it. `clear_fraction` returns the
// fraction of the segment from `from` to `to` that is free, in 0..1: 1 when
// nothing is hit, the distance to the first hit over the segment's length
// otherwise. It must answer deterministically and keep no state that
// changes an answer. A value that is not finite, or outside 0..1, is read
// as 1 or clamped.
class ChaseCameraWorld {
public:
    virtual ~ChaseCameraWorld() = default;
    virtual double clear_fraction(const VehicleVec3& from, const VehicleVec3& to) const = 0;
};

// Nothing to hit: the default.
class ChaseCameraOpenWorld final : public ChaseCameraWorld {
public:
    double clear_fraction(const VehicleVec3& from, const VehicleVec3& to) const override;
};

// ---------------------------------------------------------------------------
// The camera
// ---------------------------------------------------------------------------

// What the camera follows, in the vehicle's world frame (z up).
struct ChaseCameraSubject {
    // The anchor point: the car's body-frame origin, which its meshes are
    // placed in. APPROXIMATION: speed.exe adds the offsets to car +0x60
    // (0x0048A826), whose relation to the centre of mass was not traced;
    // the anchors share the frame of the wheel and part tables, whose origin
    // this is.
    VehicleVec3 origin;
    // The body's +x axis, and the velocity of the centre of mass, m/s.
    VehicleVec3 forward{1.0, 0.0, 0.0};
    VehicleVec3 velocity;
};

// The subject of a vehicle state: origin = position - R * centre of mass.
ChaseCameraSubject chase_subject(const VehicleState& state, const VehicleSpec& spec) noexcept;

// One camera, in the vehicle's world frame.
struct ChaseCameraView {
    int pov = 0;
    VehicleVec3 eye;
    // Unit vectors: where it looks, and up (world z tilted by the pitch).
    VehicleVec3 forward{1.0, 0.0, 0.0};
    VehicleVec3 up{0.0, 0.0, 1.0};
    // The pivot the collision segment starts from.
    VehicleVec3 pivot;
    // The camera's heading and the heading it is chasing, angle units, and
    // the lag between them (target - heading, signed).
    std::uint16_t heading = 0;
    std::uint16_t target = 0;
    std::int16_t lag = 0;
    std::uint16_t pitch = 0;
    std::uint16_t fov = 0;
    // How far out along the pivot-to-eye segment the eye is, 0..1: 1 unless
    // something was hit.
    double reach = 1.0;
    double near_plane = 0.5;
    double far_plane = 2000.0;
};

// The POVs the chase camera follows, in the order `next_pov` cycles them;
// the first the table names is where a camera starts. APPROXIMATION of the
// order and of the start: speed.exe cycles a per-player list of four POV
// bytes (0x0042E020) whose initial values were not traced. POV 0 is the
// one that shows the whole car on a 4:3 view.
constexpr std::array<int, 3> kChasePovs{{0, 1, 4}};

class ChaseCamera {
public:
    // The 240SX's table, the recovered rules and the default approximations.
    ChaseCamera();
    ChaseCamera(const ChaseCameraTable& table,
                const ChaseCameraRules& rules,
                const ChaseCameraApproximations& approx);

    bool valid() const noexcept { return valid_; }
    const std::string& error() const noexcept { return error_; }
    const ChaseCameraApproximations& approximations() const noexcept { return approx_; }

    // Selects a chase POV (0, 1 or 4) that the single-player list names.
    // False, with nothing changed, otherwise.
    bool set_pov(int pov) noexcept;
    int pov() const noexcept { return pov_; }
    // The next POV of kChasePovs that the list names. The lag and the
    // heading it chases carry over, so the new view starts where the old one
    // was looking. APPROXIMATION: speed.exe builds its type-1 mover with
    // the lag zeroed (0x00489398) and the previous target seeded from car
    // +0x3B4 (0x0048938E); whether a POV change rebuilds the mover or keeps
    // it was not traced.
    void next_pov() noexcept;

    // Puts the camera straight behind the subject, with no lag and nothing
    // hit, and makes the view.
    bool reset(const ChaseCameraSubject& subject,
               const ChaseCameraWorld& world,
               ChaseCameraView& view);

    // Advances the camera by `dt` seconds, 0 < dt <= 0.1: the target
    // heading, the lag, the eye, the collision. False, with nothing changed,
    // on an invalid camera, a dt out of range or a subject that is not
    // finite.
    bool update(const ChaseCameraSubject& subject,
                double dt,
                const ChaseCameraWorld& world,
                ChaseCameraView& view);

private:
    bool place(const ChaseCameraSubject& subject,
               const ChaseCameraWorld& world,
               double dt,
               bool snap,
               ChaseCameraView& view);

    ChaseCameraTable table_;
    ChaseCameraRules rules_;
    ChaseCameraApproximations approx_;
    bool valid_ = false;
    std::string error_;
    int pov_ = 1;
    bool started_ = false;
    std::uint16_t target_ = 0;
    std::int16_t lag_ = 0;
    double reach_ = 1.0;
};

// The target heading of 0x0048A0B6..0x0048A1C9 for a subject: the heading
// and the velocity's direction mixed by the blend weight of the speed, the
// velocity first reflected off the heading when it points backwards. The
// zero vector reads as the heading. Exposed for tests.
std::uint16_t chase_target_heading(const ChaseCameraRules& rules,
                                   const ChaseCameraSubject& subject) noexcept;

// One frame of the lag spring of 0x00489300 and 0x0048A3F2..0x0048A417: the
// lag after the target moved by `delta` units, truncated toward zero as
// speed.exe's _ftol2 (0x00674898) truncates. Exposed for tests.
std::int16_t chase_lag_step(const ChaseCameraRules& rules,
                            double lag_factor,
                            std::int16_t lag,
                            std::int16_t delta,
                            double dt) noexcept;

} // namespace nfsu
