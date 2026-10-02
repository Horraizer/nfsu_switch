#pragma once

#include "nfsu/input.h"
#include "nfsu/vehicle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Driving controls: a controller frame (include/nfsu/input.h) becomes one
// VehicleInput (include/nfsu/vehicle.h) per fixed vehicle step. Issue #59;
// docs/CONTROLS_CAMERA.md states every rule below and labels each one
// RECOVERED (read from speed.exe, with the address) or APPROXIMATION
// (invented here, with the reason).
//
// DETERMINISM. Nothing here reads a clock or calls a library mathematics
// function: the frame carries its own time, the step is the vehicle's fixed
// 1/120 s, and every value is built from integers, +, -, *, / and
// comparisons, compiled with floating-point contraction disabled
// (cmake/drive_libraries.cmake). A stick is read axis by axis, so there is
// no square root on this path at all - unlike the radial dead zone of the
// inspection camera, whose square root issue #165 is routing through a
// correctly rounded one. A recorded sequence of frames therefore gives the
// same inputs, bit for bit, on the desktop and on the console.

// The actions a binding can drive.
enum class DriveAction : std::uint8_t {
    // Signed, -1 full left to +1 full right, as the player sees it. The
    // vehicle's steer is the other way round (+1 turns left), and step()
    // negates it.
    steer,
    // 0..1.
    throttle,
    brake,
    // Held while any source is held.
    handbrake,
    // Edges: each fires once, on the frame its source goes down.
    shift_up,
    shift_down,
    // The next chase view (JOY_EVENT_CAMERA_POV_CHANGE in speed.exe's event
    // table), for the camera to take.
    camera,
    // A request to put the car back on the road, for issue #60 to take
    // (JOY_EVENT_RESET_CAR). The controls only report it.
    reset
};

constexpr std::size_t kDriveActionCount = 8;

// "steer", "throttle", ..., "reset": the names the config format uses.
const char* drive_action_name(DriveAction action) noexcept;

// Which part of a stick axis a binding reads.
enum class DriveAxisPart : std::uint8_t {
    // The whole travel, signed: -1..1.
    both,
    // Only the travel toward +32767, read as 0..1.
    positive,
    // Only the travel toward -32767, read as 0..1.
    negative
};

struct DriveBinding {
    DriveAction action = DriveAction::steer;
    // A button or one axis of one stick, as input.h names them.
    GamepadControl control;
    // Axes only.
    DriveAxisPart part = DriveAxisPart::both;
    // Steer only: a button counts -1 instead of +1, an axis reads the other
    // way round. A throttle or brake cannot be inverted - bind the other half
    // of the axis instead - and a digital action has nothing to invert.
    bool invert = false;
};

constexpr std::size_t kMaxDriveBindings = 32;

// One axis's calibration, raw units. |value| at or below `inner` reads 0
// exactly; at or above `outer` it reads full; in between it is rescaled
// linearly, with one integer subtraction and one division. -32768 reads as
// -32767, as everywhere in input.h. Each axis has its own zone, which is
// the point: a worn stick drifts on one axis, not on a circle.
struct DriveAxisCalibration {
    std::int32_t inner = 3000;
    std::int32_t outer = 30000;
};

// The response of an analog throttle.
enum class DriveThrottleCurve : std::uint8_t {
    // The value as the axis gives it, after the floor below.
    linear,
    // The six-point curve speed.exe applies to its throttle at 0x004619A8
    // (table 0x00700374 through the descriptor at 0x00706074), when a flag
    // it tests at 0x00461994 is 1; what sets the flag was not traced.
    // Selectable, not the default: docs/CONTROLS_CAMERA.md says why.
    retail
};

// How the steering input moves toward what the controller asks for, per
// second of vehicle time. Baseline APPROXIMATION: speed.exe's input layer
// passes the steering byte through untouched (0x00461868..0x004618A4), and
// #251's later steering path is now traced in docs/STEERING_RECOVERY.md.
// The baseline vehicle model already
// narrows the lock with speed and slews the road wheels at 3 rad/s; what it
// cannot do is tell a button from a stick. A button jumps from 0 to full,
// and at 30 m/s the model's full lock is reached in 25 ms, so a tap is a
// jerk. These rates are what a button gets instead.
struct DriveSteerFilter {
    // A button-driven steer moves away from centre at
    //   rate(v) = digital_rate / (1 + v / digital_rate_speed)
    // per second, v the car's speed: full input in 0.2 s at rest, 0.4 s at
    // 20 m/s, 0.6 s at 40 m/s. Speed-sensitive, and on top of the model's own
    // speed-dependent lock, which it does not replace.
    double digital_rate = 5.0;
    double digital_rate_speed = 20.0;
    // Toward centre - a release, or the first half of a reversal - at this
    // rate, from either kind of source: 0.125 s from full lock.
    double centre_rate = 8.0;
    // A stick-driven steer away from centre. 0 passes it through unfiltered:
    // the stick is its own filter, and a rate would only add lag.
    double analog_rate = 0.0;
};

constexpr double kMaxDriveRate = 1000.0;

struct DriveControlsConfig {
    std::vector<DriveBinding> bindings;
    // Indexed by GamepadAxis.
    std::array<DriveAxisCalibration, kGamepadAxisCount> axes{};
    // An analog throttle above 0 reads floor + (1 - floor) * value.
    // APPROXIMATION, from docs/VEHICLE.md's "Throttle deadband": with the
    // model's engine braking, a throttle at or below about 0.30 is net
    // engine braking at every engine speed, so a stick's lower third would
    // slow the car it is asked to drive. A button reads exactly 1 and is
    // not affected, as that section says a digital throttle is not.
    double throttle_floor = 0.30;
    DriveThrottleCurve throttle_curve = DriveThrottleCurve::linear;
    DriveSteerFilter steer;
    // Brake to reverse, with the automatic gearbox. APPROXIMATION: the
    // model's own rule selects reverse on a down request in first below
    // 1 m/s (docs/VEHICLE.md), and a controller with no shift buttons in
    // reach needs a way to ask. Holding the brake alone below
    // `reverse_speed` for `reverse_hold_steps` steps asks for a down shift;
    // in reverse the throttle and brake swap, so the brake drives backwards
    // and the throttle stops the car; holding the throttle alone there asks
    // for an up shift, back into first. The manual gearbox is never assisted.
    bool reverse_assist = true;
    double reverse_speed = 1.0;
    std::uint32_t reverse_hold_steps = 30;
};

constexpr double kMaxDriveThrottleFloor = 0.9;
constexpr double kMaxDriveReverseSpeed = 5.0;
constexpr std::uint32_t kMaxDriveReverseHoldSteps = 1200;

// The layout a Switch gamepad starts with - the owner's: ZR and ZL are
// digital, the sticks analog:
//
//   left stick x    steer            D-pad left/right  steer (buttons)
//   ZR              throttle         ZL                brake
//   B               handbrake        R / L             shift up / down
//   X               next chase view  -                 reset the car
//
// and every axis at DriveAxisCalibration{}. Named by position: B is
// face_south, X face_north (docs/INPUT.md). + is left for the application
// (pause or exit).
DriveControlsConfig default_drive_controls_config();

// Refuses, naming what it refuses: no binding; more than kMaxDriveBindings;
// an action or a control that is not one; an axis on a digital action; an
// axis part on a button; a throttle or brake read from a whole signed axis
// or inverted; an inverted digital action; one control, or one half of an
// axis, bound twice (a whole axis overlaps both of its halves); an axis
// calibration outside 0 <= inner < outer <= 32767; a floor outside
// 0..kMaxDriveThrottleFloor; a rate outside 0 < rate <= kMaxDriveRate (the
// analog rate may also be 0); a reverse speed outside 0 < v <= 5 m/s; more
// than kMaxDriveReverseHoldSteps hold steps; and a floor, rate, speed or
// reverse speed that the file cannot state - one that is not the double a
// fixed decimal of at most six places reads as, 1e-7 or 1/3 for instance -
// named by its directive. Every configuration this accepts therefore has a
// text form, which is what lets format_drive_controls_config promise a round
// trip.
bool validate_drive_controls_config(const DriveControlsConfig& config, std::string& error);

// The remapping file, ASCII, one directive per line:
//
//   nfsu-controls 1
//   # a comment
//   bind <action> <control> [invert]
//   axis <left_x|left_y|right_x|right_y> <inner> <outer>
//   throttle-floor <decimal>
//   throttle-curve <linear|retail>
//   steer-digital-rate <per-second> <speed m/s>
//   steer-centre-rate <per-second>
//   steer-analog-rate <per-second, 0 for none>
//   reverse-assist <on|off> [<speed m/s> <hold steps>]
//
// <control> is a button name from input.h (face_south, trigger_right, ...),
// an axis name (left_x), or an axis name with + or - for one half
// (right_y+). The first `bind` line replaces the whole default table, so a
// file that binds anything states its whole table; every other directive
// replaces only its own default. Decimals are read as integer / 10^k, as
// the vehicle script reads them, so every platform reads the same double.
//
// Refused, naming the line and the directive or token at fault: a header
// that is missing or different (on line 1, where it belongs) or that follows
// a comment or blank line (on its own line); a byte outside printable ASCII
// (a tab included); an unknown directive, action, control or keyword; a
// wrong number of fields; a number that is not canonical or out of the
// range validate_drive_controls_config states for it; a directive other
// than `bind` given twice; more than kMaxDriveBindings bindings; and a table
// validate_drive_controls_config refuses, with the line of the offending
// binding. Lines end in LF or CR LF; blank lines are ignored. At most
// kMaxDriveConfigBytes. Nothing is changed unless the whole text is
// accepted.
constexpr std::size_t kMaxDriveConfigBytes = 64U * 1024U;

bool parse_drive_controls_config(const std::string& text,
                                 DriveControlsConfig& out,
                                 std::string& error);

// The canonical text of a configuration: every directive, the bindings in
// table order, LF line ends. For a configuration validate_drive_controls_config
// accepts, parsing it gives the same configuration, every double bit for bit;
// for one it refuses the text is written anyway and parsing it may refuse it
// or read a nearby value.
std::string format_drive_controls_config(const DriveControlsConfig& config);

// What one controller frame asked for, before any per-step filtering.
struct DriveFrameState {
    // Buttons first: the sum of the button sources of each analog action,
    // clamped - steer to -1..1, throttle and brake to 0..1.
    double steer_digital = 0.0;
    double throttle_digital = 0.0;
    double brake_digital = 0.0;
    // Then the axes, likewise, the throttle after its floor and curve.
    double steer_analog = 0.0;
    double throttle_analog = 0.0;
    double brake_analog = 0.0;
    // The digital value decides on this frame: a button source is held and
    // the buttons' sum is not centred. RECOVERED rule: speed.exe takes the
    // digital steer byte unless it is centred, 0x80 (0x0046186E), and the
    // digital throttle and brake bytes unless they are 0 (0x00461957,
    // 0x00461DDE), and hands the action to the analog byte otherwise; so
    // does step(). A throttle or brake button cannot be inverted, so for
    // them a held button is a digital value of 1. Two opposite steer buttons
    // add to a centred digital steer, which hands the steer to the stick as
    // a centred byte would; what byte the game's own joystick layer makes of
    // two opposite buttons was not traced (APPROXIMATION).
    bool steer_from_button = false;
    bool throttle_from_button = false;
    bool brake_from_button = false;
    bool handbrake = false;
    // Edges, on this frame only.
    bool shift_up = false;
    bool shift_down = false;
    bool camera = false;
    bool reset = false;
};

// Turns controller frames into vehicle inputs. One update() per controller
// frame, then one step() per vehicle step of that frame - two at 60 frames
// a second - in order. The frame rules are input.h's, for the same reasons:
//
// - A disconnected slot contributes nothing from the frame it disconnects
//   on, and a gesture it was making ends there without firing anything.
// - A button already down when its slot connects counts as up until it has
//   been seen up.
// - Sources add up, slot by slot in ascending order and within a slot in
//   table order, then clamp; opposite sources cancel exactly.
// - An edge fires on the frame its action changes from not held to held,
//   and reaches the first step() after it, once.
class DriveControls {
public:
    // default_drive_controls_config().
    DriveControls();

    // Validates; changes nothing on a refusal. On success forgets every
    // earlier frame and the steering filter, as reset() does.
    bool configure(const DriveControlsConfig& config, std::string& error);
    const DriveControlsConfig& config() const noexcept { return config_; }

    // Reads one controller frame.
    void update(const GamepadFrame& frame);
    const DriveFrameState& frame_state() const noexcept { return frame_; }

    // The input for the next vehicle step. `forward_speed` is the car's
    // speed along its heading, m/s (VehicleTelemetry::forward_speed of the
    // step before; 0 before the first), `gear` its gear slot and `gearbox`
    // its mode, for the speed-sensitive steer rate and the reverse assist.
    // raw_steering bypasses only the approximate input slew for #251's
    // recovered road-wheel path; binding, calibration and edges still run.
    VehicleInput step(double forward_speed, int gear, VehicleGearbox gearbox,
                      bool raw_steering = false);

    // The steering filter's value, player's sign (+1 right).
    double steer() const noexcept { return steer_; }

    // Forgets every earlier frame, the steering filter, pending edges and
    // the reverse assist's count; keeps the configuration.
    void reset() noexcept;

private:
    DriveControlsConfig config_;
    DriveFrameState frame_;
    bool connected_[kMaxGamepads] = {};
    std::uint32_t latched_[kMaxGamepads] = {};
    std::array<bool, kDriveActionCount> held_{};
    bool pending_up_ = false;
    bool pending_down_ = false;
    double steer_ = 0.0;
    std::uint32_t reverse_count_ = 0;
};

// The value of one axis after its calibration, -1..1. Exposed for tests.
double drive_axis_value(const DriveAxisCalibration& calibration, std::int32_t raw) noexcept;

// The retail throttle curve, 0..1 onto 0..1: speed.exe's pair-table
// evaluation (0x00454F30) of the six points at 0x00700374, in double.
double drive_retail_throttle_curve(double value) noexcept;

// The six (input, output) pairs of that curve as the floats speed.exe holds
// at 0x00700374..0x007003A3, and the SHA-256 of those 48 bytes. A small
// numeric table from a retail file, kept under the owner's rule of PR #169
// comment 3860: nfsu_drive_tests encodes the table back to the 48 bytes and
// requires this hash. speed.exe is SHA-256
// C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7.
constexpr std::size_t kDriveRetailThrottlePoints = 6;
extern const std::array<float, 2 * kDriveRetailThrottlePoints> kDriveRetailThrottleTable;
constexpr const char* kDriveRetailThrottleTableSha256 =
    "33B5F909CC5C21AB7A2B6D13DE3DD6CB330681B17038E6B47D7DD6B874455B56";

} // namespace nfsu
