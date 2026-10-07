// The driving controls, the chase camera and the drive session of issue #59
// (nfsu_drive_controls, nfsu_chase_camera, nfsu_drive_session;
// docs/CONTROLS_CAMERA.md):
//
//   nfsu_drive_tests                          the unit tests below
//   nfsu_drive_tests --write-demo FILE        writes the built-in
//                                             demonstration in the recorded
//                                             form of docs/INPUT.md
//   nfsu_drive_tests --write-controls FILE    writes the default controls
//                                             in the remapping file's form
//
// Both writers refuse to replace a file; tests/drive_replay_guards.cmake
// feeds what they write to `nfsu_probe drive-replay`. Where a test
// recomputes one of the libraries' own expressions it does so in the same
// operation order and compares bits, which is why this test is built with
// their floating-point options.

#include "nfsu/arg_path.h"
#include "nfsu/chase_camera.h"
#include "nfsu/drive_controls.h"
#include "nfsu/drive_session.h"
#include "nfsu/input.h"
#include "nfsu/render.h"
#include "nfsu/sha256.h"
#include "nfsu/vehicle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

// For a condition that sets `error`: the message is joined inside, after
// the condition has been evaluated, where a message built among the
// arguments could read `error` before the call had set it.
void check_error(bool condition, const std::string& message, const std::string& error) {
    check(condition, message + error);
}

std::string num(double value) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.17g", value);
    return text;
}

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

constexpr double kTestPi = 3.141592653589793;

nfsu::GamepadFrame pad(std::uint32_t buttons, std::int32_t left_x = 0, std::int32_t left_y = 0,
                       std::int32_t right_x = 0, std::int32_t right_y = 0) {
    nfsu::GamepadFrame frame;
    frame.pads[0].connected = true;
    frame.pads[0].buttons = buttons;
    frame.pads[0].axes[0] = left_x;
    frame.pads[0].axes[1] = left_y;
    frame.pads[0].axes[2] = right_x;
    frame.pads[0].axes[3] = right_y;
    return frame;
}

std::uint32_t bit(nfsu::GamepadButton button) {
    return nfsu::gamepad_button_bit(button);
}

nfsu::DriveControlsConfig parse_or_fail(const std::string& text) {
    nfsu::DriveControlsConfig config;
    std::string error;
    const bool parsed = nfsu::parse_drive_controls_config(text, config, error);
    check(parsed, "a configuration that should parse was refused: " + error);
    return config;
}

nfsu::DriveControls controls_with(const nfsu::DriveControlsConfig& config) {
    nfsu::DriveControls controls;
    std::string error;
    const bool configured = controls.configure(config, error);
    check(configured, "configure refused: " + error);
    return controls;
}

// ---------------------------------------------------------------------------
// The configuration
// ---------------------------------------------------------------------------

void test_config_round_trip() {
    const nfsu::DriveControlsConfig config = nfsu::default_drive_controls_config();
    std::string error;
    check(nfsu::validate_drive_controls_config(config, error), "default refused: " + error);
    check(config.bindings.size() == 10, "the default layout binds ten controls");
    const std::string text = nfsu::format_drive_controls_config(config);
    for (const char* line :
         {"nfsu-controls 1\n", "bind steer left_x\n", "bind steer dpad_left invert\n",
          "bind steer dpad_right\n", "bind throttle trigger_right\n", "bind brake trigger_left\n",
          "bind handbrake face_south\n", "bind shift-up shoulder_right\n",
          "bind shift-down shoulder_left\n", "bind camera face_north\n", "bind reset select\n",
          "axis left_x 3000 30000\n", "throttle-floor 0.3\n", "throttle-curve linear\n",
          "steer-digital-rate 5 20\n", "steer-centre-rate 8\n", "steer-analog-rate 0\n",
          "reverse-assist on 1 30\n"}) {
        check(contains(text, line), std::string("the default text holds ") + line);
    }
    const nfsu::DriveControlsConfig again = parse_or_fail(text);
    check(nfsu::format_drive_controls_config(again) == text, "the default text round-trips");
    check(again.throttle_floor == config.throttle_floor &&
              again.steer.digital_rate == config.steer.digital_rate,
          "the round trip keeps every value bit for bit");
    check(again.bindings.size() == config.bindings.size(),
          "the round trip keeps the table's " + std::to_string(config.bindings.size()) +
              " bindings: " + std::to_string(again.bindings.size()));
    for (std::size_t i = 0; i < config.bindings.size() && i < again.bindings.size(); ++i) {
        const nfsu::DriveBinding& a = config.bindings[i];
        const nfsu::DriveBinding& b = again.bindings[i];
        check(a.action == b.action && a.control.kind == b.control.kind &&
                  a.control.index == b.control.index && a.part == b.part && a.invert == b.invert,
              "binding " + std::to_string(i) + " round-trips");
    }
    // Every value the parser can produce comes back bit for bit, at the
    // edges of each range and through the six decimal places.
    const char* const kDecimals[] = {"0",     "0.000001", "0.1",       "0.3",
                                     "0.333333", "0.9",   "1",         "4.999999",
                                     "5",     "7.25",     "123.456789", "999.999999",
                                     "1000"};
    for (const char* decimal : kDecimals) {
        const std::string value = decimal;
        std::string file = "nfsu-controls 1\nsteer-centre-rate " + value +
                           "\nsteer-analog-rate " + value + "\nsteer-digital-rate " + value +
                           " " + value + "\n";
        nfsu::DriveControlsConfig parsed;
        std::string error;
        const bool rates_ok = value != "0" && nfsu::parse_drive_controls_config(file, parsed, error);
        if (value != "0") {
            check(rates_ok, "rates of " + value + " parse: " + error);
            const nfsu::DriveControlsConfig back = parse_or_fail(nfsu::format_drive_controls_config(parsed));
            check(back.steer.centre_rate == parsed.steer.centre_rate &&
                      back.steer.analog_rate == parsed.steer.analog_rate &&
                      back.steer.digital_rate == parsed.steer.digital_rate &&
                      back.steer.digital_rate_speed == parsed.steer.digital_rate_speed,
                  "rates of " + value + " round-trip bit for bit");
        }
        const double number = [&value] {
            double out = 0.0;
            nfsu::parse_vehicle_decimal(value, out);
            return out;
        }();
        if (number <= 0.9) {
            file = "nfsu-controls 1\nthrottle-floor " + value + "\n";
            const bool floor_ok = nfsu::parse_drive_controls_config(file, parsed, error);
            check(floor_ok, "a floor of " + value + " parses: " + error);
            const nfsu::DriveControlsConfig back = parse_or_fail(nfsu::format_drive_controls_config(parsed));
            check(back.throttle_floor == parsed.throttle_floor,
                  "a floor of " + value + " round-trips bit for bit");
        }
        if (number > 0.0 && number <= 5.0) {
            file = "nfsu-controls 1\nreverse-assist on " + value + " 7\n";
            const bool speed_ok = nfsu::parse_drive_controls_config(file, parsed, error);
            check(speed_ok, "a reverse speed of " + value + " parses: " + error);
            const nfsu::DriveControlsConfig back = parse_or_fail(nfsu::format_drive_controls_config(parsed));
            check(back.reverse_speed == parsed.reverse_speed && back.reverse_hold_steps == 7,
                  "a reverse speed of " + value + " round-trips bit for bit");
        }
    }
    // A value the file cannot state is refused by validate, by directive,
    // and nothing the parser accepts can be one.
    struct Unstated {
        double nfsu::DriveControlsConfig::*field;
        double nfsu::DriveSteerFilter::*rate;
        double value;
        std::string directive;
    };
    // Negative zero is its own case, not a fixed decimal that reads back to
    // it: representable() must refuse it for every field whose range check
    // lets -0.0 through (throttle-floor's `< 0.0` and steer-analog-rate's
    // `zero_allowed && value == 0.0`, both true for -0.0). The other four
    // fields require a rate or a speed strictly above 0, so -0.0 is already
    // refused before reaching representable(), by a different, unrelated
    // message; adding it there would not exercise this fix.
    const Unstated unstated[] = {
        {nullptr, &nfsu::DriveSteerFilter::centre_rate, 1e-7, "steer-centre-rate"},
        {nullptr, &nfsu::DriveSteerFilter::centre_rate, 1.0 / 3.0, "steer-centre-rate"},
        {nullptr, &nfsu::DriveSteerFilter::analog_rate, 0.1 + 0.2, "steer-analog-rate"},
        {nullptr, &nfsu::DriveSteerFilter::analog_rate, -0.0, "steer-analog-rate"},
        {nullptr, &nfsu::DriveSteerFilter::digital_rate_speed, 20.0000001, "steer-digital-rate"},
        {&nfsu::DriveControlsConfig::throttle_floor, nullptr, 0.30000001, "throttle-floor"},
        {&nfsu::DriveControlsConfig::throttle_floor, nullptr, -0.0, "throttle-floor"},
        {&nfsu::DriveControlsConfig::reverse_speed, nullptr, 2.0 / 3.0, "reverse-assist"},
    };
    for (const Unstated& u : unstated) {
        nfsu::DriveControlsConfig bad = nfsu::default_drive_controls_config();
        if (u.field != nullptr) {
            bad.*u.field = u.value;
        } else {
            bad.steer.*u.rate = u.value;
        }
        std::string error;
        check_error(!nfsu::validate_drive_controls_config(bad, error) &&
                  error == u.directive +
                               " holds a value the file cannot state: not a fixed decimal of at"
                               " most six places",
              "an unstatable " + u.directive + " of " + num(u.value) + " is refused: ", error);
    }

    // CR LF, comments, blank lines and spaces are accepted; a directive
    // replaces only its own default, and the first bind replaces the table.
    const nfsu::DriveControlsConfig mixed = parse_or_fail(
        "nfsu-controls 1\r\n# a comment\r\n\r\n  bind   throttle right_y+\r\n"
        "bind brake right_y-\r\nbind steer left_x\r\naxis right_y 1000 31000\r\n"
        "throttle-curve retail\r\nreverse-assist off\r\n");
    check(mixed.bindings.size() == 3 && mixed.axes[3].inner == 1000 &&
              mixed.axes[3].outer == 31000 && mixed.axes[0].inner == 3000 &&
              mixed.throttle_curve == nfsu::DriveThrottleCurve::retail && !mixed.reverse_assist &&
              mixed.throttle_floor == 0.3,
          "a partial file keeps the other defaults");
}

void test_config_refusals() {
    struct Case {
        std::string text;
        std::string expected;
    };
    const std::vector<Case> cases = {
        {"", "line 1: the header must be 'nfsu-controls 1'"},
        {"nfsu-controls 2\n", "line 1: the header must be 'nfsu-controls 1'"},
        {"# first\nnfsu-controls 1\n", "line 2: the header 'nfsu-controls 1' must be the first"},
        {"nfsu-controls 1\nbind\tsteer left_x\n", "line 2: a byte outside printable ASCII"},
        {"nfsu-controls 1\njump now\n", "line 2: unknown directive 'jump'"},
        {"nfsu-controls 1\nnfsu-controls 1\n", "line 2: the header is given twice"},
        {"nfsu-controls 1\nbind fly left_x\n", "line 2: bind: unknown action 'fly'"},
        {"nfsu-controls 1\nbind steer left_z\n", "line 2: bind: unknown control 'left_z'"},
        {"nfsu-controls 1\nbind steer face_south+\n",
         "line 2: bind: unknown control 'face_south+'"},
        {"nfsu-controls 1\nbind steer left_x backwards\n",
         "line 2: bind: unknown keyword 'backwards'"},
        {"nfsu-controls 1\nbind steer\n", "line 2: bind: needs <action> <control> [invert]"},
        {"nfsu-controls 1\nbind handbrake left_x\n",
         "line 2: bind: left_x is a stick axis and cannot drive handbrake"},
        {"nfsu-controls 1\nbind throttle right_y\n",
         "line 2: bind: throttle reads 0..1 and right_y is signed"},
        {"nfsu-controls 1\nbind throttle right_y+ invert\n",
         "line 2: bind: throttle cannot be inverted"},
        {"nfsu-controls 1\nbind shift-up shoulder_right invert\n",
         "line 2: bind: shift-up cannot be inverted"},
        {"nfsu-controls 1\nbind steer dpad_left\nbind brake dpad_left\n",
         "line 3: bind: dpad_left is already read by binding 0 (steer dpad_left)"},
        {"nfsu-controls 1\nbind steer left_x\nbind throttle left_x+\n",
         "line 3: bind: left_x+ is already read by binding 0 (steer left_x)"},
        {"nfsu-controls 1\naxis left_x 30000 3000\n",
         "line 2: axis: left_x needs inner below outer"},
        {"nfsu-controls 1\naxis left_x 3000 40000\n",
         "line 2: axis: inner and outer must be whole numbers from 0 to 32767"},
        {"nfsu-controls 1\naxis left_x 03000 30000\n",
         "line 2: axis: inner and outer must be whole numbers"},
        {"nfsu-controls 1\naxis left_w 3000 30000\n", "line 2: axis: unknown axis 'left_w'"},
        {"nfsu-controls 1\naxis left_x 1 2\naxis left_x 3 4\n", "line 3: axis: is given twice"},
        {"nfsu-controls 1\nthrottle-floor 0.95\n",
         "line 2: throttle-floor: needs one canonical decimal from 0 to 0.9"},
        {"nfsu-controls 1\nthrottle-floor 0.50\n", "line 2: throttle-floor: needs one canonical"},
        {"nfsu-controls 1\nthrottle-floor -0.1\n", "line 2: throttle-floor: needs one canonical"},
        {"nfsu-controls 1\nthrottle-floor 0.3\nthrottle-floor 0.2\n",
         "line 3: throttle-floor: is given twice"},
        {"nfsu-controls 1\nthrottle-curve cubic\n",
         "line 2: throttle-curve: needs 'linear' or 'retail'"},
        {"nfsu-controls 1\nsteer-digital-rate 5\n",
         "line 2: steer-digital-rate: needs <per-second> <speed>"},
        {"nfsu-controls 1\nsteer-centre-rate 0\n",
         "line 2: steer-centre-rate: needs a rate above 0 and at most 1000"},
        {"nfsu-controls 1\nsteer-analog-rate 2000\n",
         "line 2: steer-analog-rate: needs 0 or a rate above 0 and at most 1000"},
        {"nfsu-controls 1\n# rates\nsteer-digital-rate 0 20\n",
         "line 3: steer-digital-rate: needs a rate and a speed above 0 and at most 1000"},
        {"nfsu-controls 1\nsteer-digital-rate 5 1001\n",
         "line 2: steer-digital-rate: needs a rate and a speed above 0 and at most 1000"},
        {"nfsu-controls 1\nthrottle-floor 00.3\n",
         "line 2: throttle-floor: needs one canonical decimal from 0 to 0.9"},
        {"# a comment\nbind steer left_x\n", "line 1: the header must be 'nfsu-controls 1'"},
        {"nfsu-controls 1\n" + [] {
             std::string binds;
             for (int i = 0; i < 33; ++i) {
                 binds += "bind steer left_x\n";
             }
             return binds;
         }(),
         "line 34: bind: more than 32 bindings"},
        {"nfsu-controls 1\nreverse-assist maybe\n",
         "line 2: reverse-assist: needs <on|off> [<speed> <hold steps>]"},
        {"nfsu-controls 1\nreverse-assist on 6 30\n",
         "line 2: reverse-assist: needs a speed above 0 and at most 5 m/s"},
        {"nfsu-controls 1\nreverse-assist on 0 30\n",
         "line 2: reverse-assist: needs a speed above 0 and at most 5 m/s"},
        {"nfsu-controls 1\nreverse-assist on 1 5000\n",
         "line 2: reverse-assist: needs a canonical decimal speed and whole hold steps"},
        {"nfsu-controls 1\n" + std::string(nfsu::kMaxDriveConfigBytes, '#'),
         "bytes; at most 65536"},
    };
    for (const Case& c : cases) {
        nfsu::DriveControlsConfig config = nfsu::default_drive_controls_config();
        config.throttle_floor = 0.125;
        std::string error;
        const bool accepted = nfsu::parse_drive_controls_config(c.text, config, error);
        check(!accepted, "refused: " + c.expected);
        check(contains(error, c.expected),
              "the refusal names its line and token: expected '" + c.expected + "', got '" +
                  error + "'");
        check(config.throttle_floor == 0.125 && config.bindings.size() == 10,
              "a refused file changes nothing: " + c.expected);
    }
    // A table set in memory is refused the same way, by binding.
    nfsu::DriveControlsConfig table = nfsu::default_drive_controls_config();
    table.bindings.clear();
    std::string error;
    check(!nfsu::validate_drive_controls_config(table, error) &&
              error == "the table binds nothing",
          "an empty table is refused");
    table = nfsu::default_drive_controls_config();
    table.bindings[3].part = nfsu::DriveAxisPart::positive;
    check_error(!nfsu::validate_drive_controls_config(table, error) &&
              error == "binding 3: trigger_right is a button and has no half",
          "a half on a button is refused: ", error);
    table = nfsu::default_drive_controls_config();
    table.axes[1].inner = -1;
    check_error(!nfsu::validate_drive_controls_config(table, error) &&
              contains(error, "axis left_y: the calibration needs 0 <= inner < outer"),
          "a negative inner edge is refused: ", error);
    nfsu::DriveControls controls;
    const double floor_before = controls.config().throttle_floor;
    table.axes[1].inner = 0;
    table.throttle_floor = std::numeric_limits<double>::quiet_NaN();
    check(!controls.configure(table, error) && controls.config().throttle_floor == floor_before,
          "configure refuses a NaN floor and keeps its configuration");
}

// ---------------------------------------------------------------------------
// Frames to inputs
// ---------------------------------------------------------------------------

void test_axis_calibration() {
    const nfsu::DriveAxisCalibration zone;
    check(nfsu::drive_axis_value(zone, 0) == 0.0, "centre reads 0");
    check(nfsu::drive_axis_value(zone, 3000) == 0.0 && nfsu::drive_axis_value(zone, -3000) == 0.0,
          "exactly on the inner edge reads 0 exactly");
    check(nfsu::drive_axis_value(zone, 3001) == 1.0 / 27000.0,
          "one unit past the inner edge: " + num(nfsu::drive_axis_value(zone, 3001)));
    check(nfsu::drive_axis_value(zone, 29999) == 26999.0 / 27000.0, "one unit short of outer");
    check(nfsu::drive_axis_value(zone, 30000) == 1.0 && nfsu::drive_axis_value(zone, 32767) == 1.0,
          "at and past the outer edge reads full");
    check(nfsu::drive_axis_value(zone, -32768) == -1.0 &&
              nfsu::drive_axis_value(zone, 40000) == 1.0 &&
              nfsu::drive_axis_value(zone, -40000) == -1.0,
          "-32768 and out-of-range values read as the clamped extreme");
    for (std::int32_t raw = 0; raw <= 32767; raw += 7) {
        check(nfsu::drive_axis_value(zone, -raw) == -nfsu::drive_axis_value(zone, raw),
              "the zone is symmetric at " + std::to_string(raw));
    }
    nfsu::DriveAxisCalibration wide;
    wide.inner = 0;
    wide.outer = 32767;
    check(nfsu::drive_axis_value(wide, 16384) == 16384.0 / 32767.0, "a zone of 0..32767");
    // Per axis: a stick pushed up and right steers exactly as far as one
    // pushed right alone, where a radial zone would have mixed them.
    nfsu::DriveControls controls;
    controls.update(pad(0, 16500, 0));
    const double right_only =
        controls.step(0.0, nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic).steer;
    controls.reset();
    controls.update(pad(0, 16500, 32767));
    const double diagonal =
        controls.step(0.0, nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic).steer;
    check(right_only == diagonal && right_only == -0.5,
          "the steer axis is read alone: " + num(right_only) + " " + num(diagonal));
}

void test_calibration_through_update() {
    // Each axis read with its own zone: steer bound to one axis at a time,
    // that axis given a zone no other axis has, the value read back from
    // update() at a raw position where the zones disagree.
    static const char* const kAxes[] = {"left_x", "left_y", "right_x", "right_y"};
    for (int a = 0; a < 4; ++a) {
        const std::int32_t inner = 1000 + 1500 * a;
        const std::int32_t outer = 31000 - 1000 * a;
        std::string text = std::string("nfsu-controls 1\nbind steer ") + kAxes[a] + "\n";
        for (int b = 0; b < 4; ++b) {
            const std::int32_t zone_inner = b == a ? inner : 5000;
            const std::int32_t zone_outer = b == a ? outer : 20000;
            text += std::string("axis ") + kAxes[b] + " " + std::to_string(zone_inner) + " " +
                    std::to_string(zone_outer) + "\n";
        }
        nfsu::DriveControls controls = controls_with(parse_or_fail(text));
        const std::int32_t raw = 16000;
        std::int32_t axes[4] = {0, 0, 0, 0};
        axes[a] = raw;
        controls.update(pad(0, axes[0], axes[1], axes[2], axes[3]));
        const double want = static_cast<double>(raw - inner) / static_cast<double>(outer - inner);
        const double other = static_cast<double>(raw - 5000) / static_cast<double>(20000 - 5000);
        check(controls.frame_state().steer_analog == want && want != other,
              std::string("axis ") + kAxes[a] + " is read with its own zone: " +
                  num(controls.frame_state().steer_analog) + " against " + num(want));
    }
    // A file's zone on a throttle half reaches step(): right_y at 1000 and
    // 31000 reads 16000 as exactly 0.5, where the default zone reads 13/27.
    nfsu::DriveControls controls = controls_with(parse_or_fail(
        "nfsu-controls 1\nbind throttle right_y+\naxis right_y 1000 31000\n"
        "throttle-floor 0\n"));
    controls.update(pad(0));
    controls.update(pad(0, 0, 0, 0, 16000));
    check(controls.step(10.0, nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic).throttle ==
              0.5,
          "right_y 1000..31000 throttles 0.5 at 16000");
    controls.update(pad(0, 0, 0, 0, 2000));
    check(controls.step(10.0, nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic).throttle ==
              1.0 / 30.0,
          "and 1/30 at 2000, inside the default zone's inner edge");
}

void test_sources_and_remapping() {
    using nfsu::GamepadButton;
    const auto first = nfsu::kVehicleGearFirst;
    const auto automatic = nfsu::VehicleGearbox::automatic;
    // The default layout: ZR is throttle, exactly 1, unaffected by the floor.
    nfsu::DriveControls controls;
    controls.update(pad(0));
    controls.update(pad(bit(GamepadButton::trigger_right)));
    nfsu::VehicleInput input = controls.step(0.0, first, automatic);
    check(input.throttle == 1.0 && input.brake == 0.0, "ZR is full throttle");
    controls.update(pad(bit(GamepadButton::trigger_left)));
    input = controls.step(10.0, first, automatic);
    check(input.throttle == 0.0 && input.brake == 1.0, "ZL is full brake");
    controls.update(pad(bit(GamepadButton::face_south)));
    check(controls.step(10.0, first, automatic).handbrake, "B is the handbrake");

    // Analog throttle and brake on the two halves of the right stick.
    nfsu::DriveControlsConfig config = parse_or_fail(
        "nfsu-controls 1\nbind steer left_x\nbind throttle right_y+\nbind brake right_y-\n");
    controls = controls_with(config);
    controls.update(pad(0, 0, 0, 0, 16500));
    input = controls.step(10.0, first, automatic);
    const double half = 13500.0 / 27000.0;
    check(input.throttle == config.throttle_floor + (1.0 - config.throttle_floor) * half &&
              input.brake == 0.0,
          "half the upper travel is floor + (1 - floor) / 2: " + num(input.throttle));
    controls.update(pad(0, 0, 0, 0, -16500));
    input = controls.step(10.0, first, automatic);
    check(input.throttle == 0.0 && input.brake == half, "the lower half brakes, unshaped");
    controls.update(pad(0, 0, 0, 0, 2999));
    check(controls.step(10.0, first, automatic).throttle == 0.0,
          "inside the dead zone there is no throttle at all, not the floor");

    // The retail curve.
    config.throttle_curve = nfsu::DriveThrottleCurve::retail;
    config.throttle_floor = 0.0;
    controls = controls_with(config);
    controls.update(pad(0, 0, 0, 0, 16500));
    input = controls.step(10.0, first, automatic);
    const double f = (half - 0.0) / (static_cast<double>(0.575F) - 0.0);
    const double expected = (1.0 - f) * 0.0 + f * static_cast<double>(0.437F);
    check(input.throttle == expected, "the retail curve's first segment: " + num(input.throttle));
    // A second-segment point, past the y0 == 0 case where (1 - f) * y0 +
    // f * y1 and the algebraically equal y0 + f * (y1 - y0) are bit-
    // identical: at x = 0.78872 the two forms round differently (review
    // note N3), so this pins the game's own form rather than the other one.
    const double x0b = static_cast<double>(0.575F);
    const double y0b = static_cast<double>(0.437F);
    const double x1b = static_cast<double>(0.797F);
    const double y1b = static_cast<double>(0.62F);
    const double fb = (0.78872 - x0b) / (x1b - x0b);
    const double expectedb = (1.0 - fb) * y0b + fb * y1b;
    check(nfsu::drive_retail_throttle_curve(0.78872) == expectedb,
          "the retail curve's second segment: " + num(nfsu::drive_retail_throttle_curve(0.78872)));
    check(nfsu::drive_retail_throttle_curve(0.0) == 0.0 &&
              nfsu::drive_retail_throttle_curve(1.0) == 1.0 &&
              nfsu::drive_retail_throttle_curve(static_cast<double>(0.95F)) ==
                  static_cast<double>(0.866F) &&
              nfsu::drive_retail_throttle_curve(-1.0) == 0.0 &&
              nfsu::drive_retail_throttle_curve(2.0) == 1.0,
          "the retail curve's points and ends");

    // Remapping: throttle on A. ZR then does nothing.
    config = parse_or_fail("nfsu-controls 1\nbind throttle face_east\nbind brake face_west\n");
    controls = controls_with(config);
    controls.update(pad(bit(GamepadButton::trigger_right)));
    check(controls.step(0.0, first, automatic).throttle == 0.0, "a remapped ZR drives nothing");
    controls.update(pad(bit(GamepadButton::face_east)));
    check(controls.step(0.0, first, automatic).throttle == 1.0, "A drives the throttle");

    // A digital value that is not centred decides (0x0046186E, 0x00461957,
    // 0x00461DDE); a centred one hands the action to the analog source.
    controls = nfsu::DriveControls();
    controls.update(pad(0));
    controls.update(pad(bit(GamepadButton::dpad_left), 16500));
    check(controls.frame_state().steer_from_button &&
              controls.frame_state().steer_digital == -1.0 &&
              controls.frame_state().steer_analog == half,
          "D-pad left and a stick right: the button decides");
    controls.update(pad(0));
    controls.update(pad(bit(GamepadButton::dpad_left) | bit(GamepadButton::dpad_right), 32767));
    check(!controls.frame_state().steer_from_button &&
              controls.frame_state().steer_digital == 0.0 &&
              controls.frame_state().steer_analog == 1.0,
          "two opposite buttons are a centred digital steer, which hands over to the stick");
    input = controls.step(0.0, first, automatic);
    check(controls.steer() == 1.0 && input.steer == -1.0,
          "and step() then steers with the stick, at once");

    // The same rule where it acts, in step(), for each of the three actions,
    // with both sources held on one frame.
    controls = nfsu::DriveControls();
    controls.update(pad(0));
    controls.update(pad(bit(GamepadButton::dpad_left), 32767));
    input = controls.step(0.0, first, automatic);
    check(controls.steer() == -5.0 * nfsu::kVehicleStepSeconds &&
              input.steer == 5.0 * nfsu::kVehicleStepSeconds,
          "D-pad left held with the stick full right steers left, at the button's rate: " +
              num(controls.steer()));
    input = controls.step(0.0, first, automatic);
    check(controls.steer() == -10.0 * nfsu::kVehicleStepSeconds,
          "and keeps steering left on the next step");
    nfsu::DriveControlsConfig both = parse_or_fail(
        "nfsu-controls 1\nbind steer left_x\nbind throttle trigger_right\n"
        "bind throttle right_y+\nbind brake trigger_left\nbind brake right_y-\n");
    const double shaped = both.throttle_floor + (1.0 - both.throttle_floor) * half;
    controls = controls_with(both);
    controls.update(pad(0));
    controls.update(pad(0, 0, 0, 0, 16500));
    input = controls.step(10.0, first, automatic);
    check(input.throttle == shaped, "the stick alone gives its shaped throttle");
    controls.update(pad(bit(GamepadButton::trigger_right), 0, 0, 0, 16500));
    input = controls.step(10.0, first, automatic);
    check(input.throttle == 1.0, "ZR held with the stick half up: ZR decides, 1 not " +
                                     num(shaped) + ": " + num(input.throttle));
    controls.update(pad(0, 0, 0, 0, -16500));
    input = controls.step(10.0, first, automatic);
    check(input.brake == half, "the stick alone gives its brake");
    controls.update(pad(bit(GamepadButton::trigger_left), 0, 0, 0, -16500));
    input = controls.step(10.0, first, automatic);
    check(input.brake == 1.0,
          "ZL held with the stick half down: ZL decides, 1 not 0.5: " + num(input.brake));
    // Two slots add and clamp.
    nfsu::GamepadFrame two = pad(0, 22000);
    two.pads[1].connected = true;
    two.pads[1].axes[0] = 22000;
    controls = nfsu::DriveControls();
    controls.update(two);
    check(controls.frame_state().steer_analog == 1.0, "two sticks add and clamp to 1");
    two.pads[1].axes[0] = -22000;
    controls.update(two);
    check(controls.frame_state().steer_analog == 0.0, "two sticks pushing apart cancel exactly");
}

// Steps until the filter reaches `target`, at most `limit`; -1 if never.
int steps_to(nfsu::DriveControls& controls, double speed, double target, int limit) {
    for (int i = 1; i <= limit; ++i) {
        controls.step(speed, nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic);
        if (controls.steer() == target) {
            return i;
        }
    }
    return -1;
}

void test_steer_filter() {
    using nfsu::GamepadButton;
    const nfsu::DriveSteerFilter filter;
    // A button steer, speed-sensitive: 0.2 s at rest, 0.4 s at 20 m/s,
    // 0.6 s at 40 m/s, in 1/120 s steps.
    const double speeds[] = {0.0, 20.0, 40.0};
    const int expected[] = {24, 48, 72};
    for (int k = 0; k < 3; ++k) {
        nfsu::DriveControls controls;
        controls.update(pad(0));
        controls.update(pad(bit(GamepadButton::dpad_right)));
        const nfsu::VehicleInput first =
            controls.step(speeds[k], nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic);
        const double rate = filter.digital_rate / (1.0 + speeds[k] / filter.digital_rate_speed);
        check(controls.steer() == 0.0 + rate * nfsu::kVehicleStepSeconds &&
                  first.steer == -controls.steer(),
              "the first step moves by rate(v) / 120 and the vehicle's steer is negated at " +
                  num(speeds[k]));
        const int steps = steps_to(controls, speeds[k], 1.0, 400) + 1;
        check(steps == expected[k] || steps == expected[k] + 1,
              "full button steer takes " + std::to_string(steps) + " steps at " +
                  num(speeds[k]) + " m/s");
    }
    // Release: back to centre at 8/s, 15 steps.
    nfsu::DriveControls controls;
    controls.update(pad(0));
    controls.update(pad(bit(GamepadButton::dpad_right)));
    steps_to(controls, 0.0, 1.0, 100);
    controls.update(pad(0));
    const int back = steps_to(controls, 0.0, 0.0, 100);
    check(back == 15 || back == 16, "release returns in " + std::to_string(back) + " steps");
    // Reversal: to centre at the centre rate, then out at the button rate.
    controls.update(pad(bit(GamepadButton::dpad_right)));
    steps_to(controls, 0.0, 1.0, 100);
    controls.update(pad(bit(GamepadButton::dpad_left)));
    controls.step(0.0, nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic);
    check(controls.steer() == 1.0 - filter.centre_rate * nfsu::kVehicleStepSeconds,
          "a reversal first moves toward centre at the centre rate");
    const int through = steps_to(controls, 0.0, 0.0, 100);
    const int out = steps_to(controls, 0.0, -1.0, 100);
    check(through > 0 && out >= 23 && out <= 25,
          "then crosses and leaves centre at the button rate: " + std::to_string(out));
    // A stick passes through; its release is filtered at the centre rate.
    controls = nfsu::DriveControls();
    controls.update(pad(0, 32767));
    const nfsu::VehicleInput input =
        controls.step(30.0, nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic);
    check(controls.steer() == 1.0 && input.steer == -1.0,
          "a stick at full right steers full right at once, which is -1 to the model");
    controls.update(pad(0, 0));
    controls.step(30.0, nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic);
    check(controls.steer() == 1.0 - filter.centre_rate * nfsu::kVehicleStepSeconds,
          "a released stick returns at the centre rate");
    // An analog rate, when one is configured.
    nfsu::DriveControlsConfig config = nfsu::default_drive_controls_config();
    config.steer.analog_rate = 4.0;
    controls = controls_with(config);
    controls.update(pad(0, -32767));
    controls.step(0.0, nfsu::kVehicleGearFirst, nfsu::VehicleGearbox::automatic);
    check(controls.steer() == 0.0 - 4.0 * nfsu::kVehicleStepSeconds,
          "steer-analog-rate limits a stick");
}

void test_edges_and_latching() {
    using nfsu::GamepadButton;
    const auto first = nfsu::kVehicleGearFirst;
    const auto manual = nfsu::VehicleGearbox::manual;
    nfsu::DriveControls controls;
    controls.update(pad(0));
    controls.update(pad(bit(GamepadButton::shoulder_right)));
    check(controls.frame_state().shift_up, "R down is a shift-up edge");
    check(controls.step(10.0, first, manual).shift == nfsu::VehicleShift::up,
          "the edge reaches the first step");
    check(controls.step(10.0, first, manual).shift == nfsu::VehicleShift::none,
          "and not the second");
    controls.update(pad(bit(GamepadButton::shoulder_right)));
    check(!controls.frame_state().shift_up &&
              controls.step(10.0, first, manual).shift == nfsu::VehicleShift::none,
          "a held R does not repeat");
    controls.update(pad(bit(GamepadButton::face_north) | bit(GamepadButton::select)));
    check(controls.frame_state().camera && controls.frame_state().reset,
          "X and - report the camera and reset edges");
    controls.update(pad(bit(GamepadButton::face_north) | bit(GamepadButton::select)));
    check(!controls.frame_state().camera && !controls.frame_state().reset,
          "held, they report nothing more");

    // A button down as its slot connects is latched until seen up.
    controls = nfsu::DriveControls();
    controls.update(pad(bit(GamepadButton::trigger_right) | bit(GamepadButton::shoulder_left)));
    nfsu::VehicleInput input = controls.step(0.0, first, manual);
    check(input.throttle == 0.0 && input.shift == nfsu::VehicleShift::none,
          "buttons held at connect fire nothing");
    controls.update(pad(0));
    controls.update(pad(bit(GamepadButton::trigger_right)));
    check(controls.step(0.0, first, manual).throttle == 1.0,
          "released and pressed again, it acts");
    // A disconnect ends the gesture on its frame.
    const nfsu::GamepadFrame gone;
    controls.update(gone);
    input = controls.step(10.0, first, manual);
    check(input.throttle == 0.0 && input.brake == 0.0 &&
              controls.frame_state().steer_analog == 0.0,
          "a disconnected pad contributes nothing");
    controls.update(pad(bit(GamepadButton::trigger_right)));
    check(controls.step(10.0, first, manual).throttle == 0.0,
          "reconnecting with ZR held is a new connect, latched");
    // Bits outside the sixteen buttons are ignored.
    controls = nfsu::DriveControls();
    controls.update(pad(0));
    controls.update(pad(0xFFFF0000U));
    check(controls.step(0.0, first, manual).throttle == 0.0, "stray bits are not buttons");
}

void test_reverse_assist() {
    using nfsu::GamepadButton;
    const auto first = nfsu::kVehicleGearFirst;
    const auto reverse = nfsu::kVehicleGearReverse;
    const auto automatic = nfsu::VehicleGearbox::automatic;
    nfsu::DriveControls controls;
    controls.update(pad(0));
    controls.update(pad(bit(GamepadButton::trigger_left)));
    int shifted_at = -1;
    for (int i = 1; i <= 40 && shifted_at < 0; ++i) {
        const nfsu::VehicleInput input = controls.step(0.2, first, automatic);
        check(input.brake == 1.0 && input.throttle == 0.0, "in first the brake brakes");
        if (input.shift == nfsu::VehicleShift::down) {
            shifted_at = i;
        }
    }
    check(shifted_at == 30, "the brake held 30 steps below 1 m/s asks for a down shift: " +
                                std::to_string(shifted_at));
    nfsu::VehicleInput input = controls.step(-0.5, reverse, automatic);
    check(input.throttle == 1.0 && input.brake == 0.0 && input.shift == nfsu::VehicleShift::none,
          "in reverse the brake drives");
    controls.update(pad(bit(GamepadButton::trigger_right)));
    shifted_at = -1;
    for (int i = 1; i <= 40 && shifted_at < 0; ++i) {
        input = controls.step(-0.3, reverse, automatic);
        check(input.brake == 1.0 && input.throttle == 0.0, "in reverse the throttle brakes");
        if (input.shift == nfsu::VehicleShift::up) {
            shifted_at = i;
        }
    }
    check(shifted_at == 30,
          "the throttle held in reverse asks for first: " + std::to_string(shifted_at));
    // Not while moving, not with both pedals, not with the manual gearbox,
    // not when switched off.
    controls.update(pad(bit(GamepadButton::trigger_left)));
    bool any = false;
    for (int i = 0; i < 100; ++i) {
        any = any || controls.step(3.0, first, automatic).shift != nfsu::VehicleShift::none;
        any = any || controls.step(0.0, first, nfsu::VehicleGearbox::manual).shift !=
                         nfsu::VehicleShift::none;
    }
    controls.update(pad(bit(GamepadButton::trigger_left) | bit(GamepadButton::trigger_right)));
    for (int i = 0; i < 100; ++i) {
        any = any || controls.step(0.0, first, automatic).shift != nfsu::VehicleShift::none;
    }
    check(!any, "no request while moving, with the manual gearbox or with both pedals");
    nfsu::DriveControlsConfig config = nfsu::default_drive_controls_config();
    config.reverse_assist = false;
    controls = controls_with(config);
    controls.update(pad(0));
    controls.update(pad(bit(GamepadButton::trigger_left)));
    for (int i = 0; i < 100; ++i) {
        input = controls.step(0.0, reverse, automatic);
        any = any || input.shift != nfsu::VehicleShift::none || input.throttle != 0.0;
    }
    check(!any, "reverse-assist off asks nothing and swaps nothing");
}

// ---------------------------------------------------------------------------
// The camera's recovered tables
// ---------------------------------------------------------------------------

std::vector<unsigned char> record_with(const nfsu::ChaseCameraTable& table) {
    std::vector<unsigned char> record(nfsu::kVehicleRecordSize, 0);
    nfsu::encode_chase_camera_table(table, record.data(), record.size());
    return record;
}

std::vector<unsigned char> float_bytes(float value) {
    std::vector<unsigned char> out(4);
    std::memcpy(out.data(), &value, 4);
    return out;
}

void test_camera_tables() {
    const nfsu::ChaseCameraTable stock = nfsu::stock_240sx_chase_camera_table();
    const std::vector<unsigned char> record = record_with(stock);
    const nfsu::Sha256Digest block =
        nfsu::sha256(record.data() + nfsu::kChaseCameraBlockOffset, nfsu::kChaseCameraBlockBytes);
    check(nfsu::sha256_hex(block) == nfsu::kChaseCamera240sxBlockSha256,
          "the built-in anchors rebuild the retail bytes: " + nfsu::sha256_hex(block));
    std::vector<unsigned char> ones(nfsu::kVehicleRecordSize, 0xFF);
    nfsu::encode_chase_camera_table(stock, ones.data(), ones.size());
    std::size_t written = 0;
    for (std::size_t i = 0; i < ones.size(); ++i) {
        written += ones[i] == record[i] ? 1U : 0U;
    }
    check(written == nfsu::kChaseCameraBlockBytes - 8,
          "the encoder writes 168 bytes and leaves the eight zero ones: " +
              std::to_string(written));

    nfsu::ChaseCameraTable decoded;
    std::string error;
    const bool decoded_ok =
        nfsu::decode_chase_camera_table(record.data(), record.size(), decoded, error);
    check(decoded_ok, "the built-in block decodes: " + error);
    check(record_with(decoded) == record, "decode and encode round-trip");
    nfsu::ChaseCameraAnchor anchor;
    check(nfsu::find_chase_anchor(stock, false, 4, anchor) && anchor.pitch == 3040 &&
              anchor.height == 3.45F && anchor.along == -5.8F,
          "POV 4: 3040 units down, 3.45 m up, 5.8 m back");
    check(nfsu::find_chase_anchor(stock, true, 0, anchor) && anchor.along == -8.2F &&
              !nfsu::find_chase_anchor(stock, false, 7, anchor),
          "the shared-screen list, and a POV no list names");
    check(decoded.single[2].along > 0.0F,
          "POV 2's anchor, in front of the car, is not a chase anchor and is kept");

    std::array<std::uint8_t, nfsu::kChaseCameraRuleBytes> rules{};
    nfsu::encode_chase_camera_rules(nfsu::ChaseCameraRules{}, rules);
    check(nfsu::sha256_hex(nfsu::sha256(rules.data(), rules.size())) ==
              nfsu::kChaseCameraRulesSha256,
          "the rule constants rebuild speed.exe's 34 bytes");
    std::array<std::uint8_t, 48> curve{};
    std::memcpy(curve.data(), nfsu::kDriveRetailThrottleTable.data(), curve.size());
    check(nfsu::sha256_hex(nfsu::sha256(curve.data(), curve.size())) ==
              nfsu::kDriveRetailThrottleTableSha256,
          "the throttle curve rebuilds speed.exe's 48 bytes");

    struct Refusal {
        std::size_t offset;
        std::vector<unsigned char> bytes;
        std::string expected;
    };
    const std::vector<Refusal> refusals = {
        {0xB50, {6, 0, 0, 0}, "+0xB50: 6 anchors; the list holds at most 5"},
        {0xB54, {9, 0, 0, 0}, "+0xB54: 9 anchors; the list holds at most 5"},
        {0xB60, {5, 0}, "+0xB60: POV 5 is outside 0..4"},
        {0xB70, {0, 0}, "+0xB70: POV 0 is named twice in one list"},
        {0xB64, float_bytes(0.5F), "+0xB64: a chase anchor must sit behind the car"},
        {0xB68, float_bytes(std::numeric_limits<float>::quiet_NaN()),
         "+0xB64: a chase anchor's offsets are not finite"},
    };
    for (const Refusal& r : refusals) {
        std::vector<unsigned char> bad = record;
        std::copy(r.bytes.begin(), r.bytes.end(),
                  bad.begin() + static_cast<std::ptrdiff_t>(r.offset));
        nfsu::ChaseCameraTable out;
        out.single_count = 77;
        const bool accepted = nfsu::decode_chase_camera_table(bad.data(), bad.size(), out, error);
        check(!accepted && error == r.expected && out.single_count == 77,
              "refused by offset: expected '" + r.expected + "', got '" + error + "'");
    }
    check(!nfsu::decode_chase_camera_table(record.data(), record.size() - 1, decoded, error),
          "a record of the wrong size is refused");
}

// ---------------------------------------------------------------------------
// Angles, the target heading and the lag
// ---------------------------------------------------------------------------

void test_angles() {
    double worst = 0.0;
    for (int i = -4000; i <= 4000; ++i) {
        const double x = static_cast<double>(i) * 0.0037 * (1.0 + std::fabs(i) * 0.01);
        const double diff = std::fabs(nfsu::chase_atan(x) - std::atan(x));
        worst = std::max(worst, diff / std::max(1.0, std::fabs(std::atan(x))));
    }
    check(worst <= 4.5e-16, "chase_atan within 2 ulp of the library's: " + num(worst));
    check(nfsu::chase_atan(1e300) == nfsu::chase_atan(1e30) &&
              nfsu::chase_atan(std::numeric_limits<double>::quiet_NaN()) == 0.0 &&
              nfsu::chase_atan(-0.0) == 0.0,
          "chase_atan at its ends");
    struct Case {
        double x;
        double y;
        std::uint16_t units;
    };
    const Case cases[] = {{1, 0, 0},     {0, 1, 16384},     {-1, 0, 32768},
                          {0, -1, 49152}, {1, 1, 8192},     {-1, 1, 24576},
                          {-1, -1, 40960}, {1, -1, 57344},  {0, 0, 0},
                          {5, 1e-9, 0},  {-3, -1e-12, 32768}};
    for (const Case& c : cases) {
        check(nfsu::chase_angle(c.x, c.y) == c.units,
              "angle of (" + num(c.x) + ", " + num(c.y) + ") is " +
                  std::to_string(nfsu::chase_angle(c.x, c.y)));
    }
    check(nfsu::chase_angle(std::numeric_limits<double>::quiet_NaN(), 1.0) == 0 &&
              nfsu::chase_angle(1.0, std::numeric_limits<double>::infinity()) == 0,
          "a vector that is not finite has angle 0");
    int mismatches = 0;
    for (int u = 0; u < 65536; u += 13) {
        const double r = nfsu::chase_angle_radians(static_cast<std::uint16_t>(u));
        if (nfsu::chase_angle(nfsu::vehicle_cos(r), nfsu::vehicle_sin(r)) != u) {
            ++mismatches;
        }
    }
    check(mismatches == 0, "units -> radians -> units is exact: " + std::to_string(mismatches));
    check(nfsu::chase_angle_degrees(13020) == 71.52099609375 &&
              nfsu::chase_angle_degrees(3040) == 16.69921875,
          "degrees of 16-bit angles are exact");
}

nfsu::ChaseCameraSubject subject_at(double heading_units, double speed, double slip_units = 0.0) {
    const double h = heading_units * 2.0 * kTestPi / 65536.0;
    const double v = (heading_units + slip_units) * 2.0 * kTestPi / 65536.0;
    nfsu::ChaseCameraSubject s;
    s.forward = {std::cos(h), std::sin(h), 0.0};
    s.velocity = {speed * std::cos(v), speed * std::sin(v), 0.0};
    return s;
}

void test_target_heading() {
    const nfsu::ChaseCameraRules rules;
    nfsu::ChaseCameraSubject s;
    check(nfsu::chase_target_heading(rules, s) == 0, "at rest facing +x: 0");
    s = subject_at(5461.0, 20.0);
    check(std::abs(static_cast<int>(nfsu::chase_target_heading(rules, s)) - 5461) <= 1,
          "moving along its heading: the heading");
    s.forward = {1.0, 0.0, 0.0};
    s.velocity = {0.0, 5.0, 0.0};
    check(nfsu::chase_target_heading(rules, s) == 0,
          "below 8 m/s the velocity has no weight, whatever its direction");
    s.velocity = {0.0, 20.0, 0.0};
    check(nfsu::chase_target_heading(rules, s) == 16384,
          "above 13 m/s the velocity alone: " +
              std::to_string(nfsu::chase_target_heading(rules, s)));
    s.velocity = {10.5 * std::cos(kTestPi / 4), 10.5 * std::sin(kTestPi / 4), 0.0};
    check(nfsu::chase_target_heading(rules, s) == 4096,
          "at 10.5 m/s, half and half: 22.5 degrees, " +
              std::to_string(nfsu::chase_target_heading(rules, s)));
    s.velocity = {-20.0, 0.0, 0.0};
    check(nfsu::chase_target_heading(rules, s) == 0,
          "reversing, the velocity is reflected off the heading and the camera stays put");
    s.velocity = {-20.0, 5.0, 0.0};
    check(nfsu::chase_target_heading(rules, s) == nfsu::chase_angle(20.0, 5.0),
          "reflected, its sideways part is kept");
    // The weight is the recovered pair table: 0 at 8, 1 at 13, linear.
    for (int tenth = 70; tenth <= 140; ++tenth) {
        const double speed = tenth / 10.0;
        s.velocity = {0.0, speed, 0.0};
        const std::uint16_t got = nfsu::chase_target_heading(rules, s);
        const double t = speed <= 8.0 ? 0.0 : (speed >= 13.0 ? 1.0 : (speed - 8.0) / 5.0);
        const std::uint16_t want = nfsu::chase_angle(1.0 - t, t);
        check(std::abs(static_cast<int>(got) - static_cast<int>(want)) <= 1,
              "blend at " + num(speed) + " m/s: " + std::to_string(got) + " against " +
                  std::to_string(want));
    }
}

std::int16_t expected_lag(std::int16_t lag, std::int16_t delta, double dt) {
    const nfsu::ChaseCameraRules rules;
    const std::int16_t moved = static_cast<std::int16_t>(
        static_cast<std::uint16_t>((static_cast<std::int32_t>(lag) + delta) & 0xFFFF));
    const std::int32_t magnitude = moved < 0 ? -static_cast<std::int32_t>(moved) : moved;
    const double atan16 = static_cast<double>(nfsu::chase_angle(
        1.0, static_cast<double>(magnitude) * static_cast<double>(rules.lag_scale)));
    double rate = -(atan16 * static_cast<double>(rules.lag_gain));
    if (moved < 0) {
        rate = -rate;
    }
    const std::int32_t change = static_cast<std::int32_t>(rate * 1.0 * dt);
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>((static_cast<std::int32_t>(moved) + change) & 0xFFFF));
}

void test_lag() {
    const nfsu::ChaseCameraRules rules;
    const double dt = 1.0 / 60.0;
    check(nfsu::chase_lag_step(rules, 1.0, 0, 0, dt) == 0, "no lag, no target move: none");
    const std::int16_t lag = nfsu::chase_lag_step(rules, 1.0, 0, 8192, dt);
    check(lag == expected_lag(0, 8192, dt) && lag == 8192 - 376,
          "a 45-degree jump is taken up by the lag and starts to decay at 376 units a frame: " +
              std::to_string(lag));
    // The decay: the spring's time constant is 1 / (1.9088 * 0.000261 *
    // 65536 / 2 pi), 0.192 s, and truncation leaves a dead band of a few
    // units.
    std::int16_t previous = lag;
    int frames = 1;
    bool monotone = true;
    while (frames < 240) {
        const std::int16_t next = nfsu::chase_lag_step(rules, 1.0, previous, 0, dt);
        check(next == expected_lag(previous, 0, dt), "each frame is the recovered expression");
        monotone = monotone && next <= previous && next >= 0;
        previous = next;
        ++frames;
        if (frames == 30) {
            check(previous < 1000 && previous > 300,
                  "half a second decays it to under an eighth: " + std::to_string(previous));
        }
    }
    check(monotone, "the decay is monotone and does not overshoot");
    check(previous > 0 && previous <= 12,
          "it stops in the truncation dead band: " + std::to_string(previous));
    for (int a = -32767; a <= 32767; a += 311) {
        const std::int16_t up =
            nfsu::chase_lag_step(rules, 1.0, static_cast<std::int16_t>(a), 0, dt);
        const std::int16_t down =
            nfsu::chase_lag_step(rules, 1.0, static_cast<std::int16_t>(-a), 0, dt);
        check(up == -down, "the spring is odd at " + std::to_string(a));
    }
    const std::int16_t big = nfsu::chase_lag_step(rules, 1.0, 32000, 0, dt);
    const int change = 32000 - big;
    check(change > 450 && change < 522,
          "far off, the spring saturates below 1.9088 quarter turns a second: " +
              std::to_string(change));
    const std::int16_t wrapped = nfsu::chase_lag_step(rules, 1.0, 32000, 1000, dt);
    check(wrapped > -32536 && wrapped < -32000,
          "past half a turn the lag wraps, as the 16-bit angle does: " + std::to_string(wrapped));
    check(nfsu::chase_lag_step(rules, 0.0, 100, 50, dt) == 150,
          "a lag factor of 0 holds the camera still");
}

// ---------------------------------------------------------------------------
// The camera
// ---------------------------------------------------------------------------

class WallWorld final : public nfsu::ChaseCameraWorld {
public:
    explicit WallWorld(double x) : x_(x) {}
    double clear_fraction(const nfsu::VehicleVec3& from,
                          const nfsu::VehicleVec3& to) const override {
        if (!(from.x > x_) || !(to.x < x_)) {
            return 1.0;
        }
        return (from.x - x_) / (from.x - to.x);
    }

private:
    double x_;
};

class FixedWorld final : public nfsu::ChaseCameraWorld {
public:
    explicit FixedWorld(double value) : value_(value) {}
    double clear_fraction(const nfsu::VehicleVec3&, const nfsu::VehicleVec3&) const override {
        return value_;
    }

private:
    double value_;
};

bool same(const nfsu::VehicleVec3& a, const nfsu::VehicleVec3& b, double tolerance) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

void test_camera_place() {
    const nfsu::ChaseCameraOpenWorld open;
    nfsu::ChaseCamera camera;
    check(camera.valid() && camera.pov() == 0, "the stock camera starts at POV 0");
    nfsu::ChaseCameraSubject subject;
    nfsu::ChaseCameraView view;
    check(camera.reset(subject, open, view), "reset");
    check(same(view.eye, {-5.8F, 0.0, 1.7F}, 1e-12) && same(view.forward, {1.0, 0.0, 0.0}, 0.0) &&
              same(view.up, {0.0, 0.0, 1.0}, 0.0) && view.fov == 13020 && view.reach == 1.0 &&
              view.heading == 0 && view.lag == 0 && view.near_plane == 0.5,
          "POV 0: 5.8 m back, 1.7 m up, level, 71.52 degrees");
    check(camera.set_pov(1) && camera.reset(subject, open, view) &&
              same(view.eye, {-4.7F, 0.0, 1.7F}, 1e-12),
          "POV 1: 4.7 m back");
    check(camera.set_pov(4) && camera.reset(subject, open, view) &&
              same(view.eye, {-5.8F, 0.0, 3.45F}, 1e-12) && view.pitch == 3040 &&
              near(view.forward.z, -std::sin(3040 * 2.0 * kTestPi / 65536.0), 1e-15),
          "POV 4: 3.45 m up, looking 16.7 degrees down");
    check(!camera.set_pov(2) && !camera.set_pov(3) && !camera.set_pov(5) && camera.pov() == 4,
          "the car-fixed POVs are refused");
    camera.next_pov();
    const int after_4 = camera.pov();
    camera.next_pov();
    const int after_0 = camera.pov();
    camera.next_pov();
    check(after_4 == 0 && after_0 == 1 && camera.pov() == 4, "next_pov cycles 4, 0, 1, 4");
    // The anchor follows the subject's origin; the pivot is above it.
    camera.set_pov(0);
    subject.origin = {100.0, -20.0, 3.0};
    subject.forward = {0.0, 1.0, 0.0};
    check(camera.reset(subject, open, view) && view.heading == 16384 &&
              same(view.eye, {100.0, -20.0 - static_cast<double>(5.8F), 3.0 + 1.7F}, 1e-9) &&
              same(view.pivot, {100.0, -20.0, 3.0 + 1.7F}, 1e-12),
          "facing +y the eye is 5.8 m down y");
    check(!camera.update(subject, 0.0, open, view) && !camera.update(subject, 0.2, open, view),
          "dt outside 0 < dt <= 0.1 is refused");
    subject.origin.x = std::numeric_limits<double>::infinity();
    check(!camera.update(subject, 1.0 / 60.0, open, view),
          "a subject that is not finite is refused");
    const nfsu::ChaseCameraTable table;
    const nfsu::ChaseCamera empty(table, nfsu::ChaseCameraRules{},
                                  nfsu::ChaseCameraApproximations{});
    check(!empty.valid() && contains(empty.error(), "names no chase POV"),
          "a table with no chase anchor is refused");
    nfsu::ChaseCameraApproximations bad;
    bad.near_plane = 0.0;
    check(!nfsu::ChaseCamera(nfsu::stock_240sx_chase_camera_table(), nfsu::ChaseCameraRules{},
                             bad)
               .valid(),
          "a near plane of 0 is refused");
}

void test_camera_follow() {
    // A car turning left at 30 degrees a second at 20 m/s: the camera lags
    // behind by a constant angle where the spring's rate matches the turn.
    const nfsu::ChaseCameraOpenWorld open;
    nfsu::ChaseCamera camera;
    nfsu::ChaseCameraView view;
    const double rate_units = 30.0 / 360.0 * 65536.0;
    double heading = 0.0;
    camera.reset(subject_at(0.0, 20.0), open, view);
    std::vector<int> lags;
    std::vector<int> headings;
    for (int frame = 1; frame <= 180; ++frame) {
        heading += rate_units / 60.0;
        check(camera.update(subject_at(heading, 20.0), 1.0 / 60.0, open, view), "update");
        lags.push_back(view.lag);
        headings.push_back(view.heading);
        if (frame == 1) {
            check(view.lag > 0 && view.heading < view.target,
                  "the camera turns less than the car on the first frame");
        }
    }
    const int last = lags.back();
    for (std::size_t i = lags.size() - 30; i < lags.size(); ++i) {
        check(std::abs(lags[i] - last) <= 2, "the lag settles: " + std::to_string(lags[i]));
    }
    check(last > 900 && last < 1250,
          "the settled lag is about 5.9 degrees: " + std::to_string(last));
    const int step = static_cast<int>(
        static_cast<std::uint16_t>(headings.back() - headings[headings.size() - 2]));
    check(std::abs(step - 91) <= 2, "the camera then turns with the car: " + std::to_string(step));
    // The eye stays on its circle round the anchor.
    const double dx = view.eye.x - view.pivot.x;
    const double dy = view.eye.y - view.pivot.y;
    check(near(std::sqrt(dx * dx + dy * dy), static_cast<double>(5.8F), 1e-9),
          "the eye stays 5.8 m from the anchor");
}

void test_camera_collision() {
    nfsu::ChaseCamera camera;
    camera.set_pov(1);
    nfsu::ChaseCameraView view;
    const nfsu::ChaseCameraSubject subject;
    const WallWorld wall(-3.0);
    check(camera.reset(subject, wall, view) && near(view.eye.x, -2.8, 1e-12) &&
              near(view.reach, 2.8 / static_cast<double>(4.7F), 1e-12),
          "a wall 3 m behind pulls the eye to 0.2 m short of it: " + num(view.eye.x));
    check(camera.update(subject, 1.0 / 60.0, wall, view) && near(view.eye.x, -2.8, 1e-12),
          "and holds it there");
    const nfsu::ChaseCameraOpenWorld open;
    check(camera.update(subject, 1.0 / 60.0, open, view) && near(view.eye.x, -2.9, 1e-12),
          "once clear it moves back out at 6 m/s: " + num(view.eye.x));
    int frames = 1;
    while (view.eye.x > static_cast<double>(-4.7F) + 1e-9 && frames < 100) {
        camera.update(subject, 1.0 / 60.0, open, view);
        ++frames;
    }
    check(frames == 19 && view.reach == 1.0 &&
              same(view.eye, {static_cast<double>(-4.7F), 0.0, static_cast<double>(1.7F)}, 1e-12),
          "all the way out in 19 frames: " + std::to_string(frames));
    const WallWorld close(-2.0);
    check(camera.update(subject, 1.0 / 60.0, close, view) && near(view.eye.x, -1.8, 1e-12),
          "a nearer obstruction pulls it in at once");
    const WallWorld far(-6.0);
    camera.reset(subject, far, view);
    check(view.reach == 1.0 && near(view.eye.x, static_cast<double>(-4.7F), 1e-12),
          "a wall past the eye changes nothing");
    const FixedWorld nan(std::numeric_limits<double>::quiet_NaN());
    camera.reset(subject, nan, view);
    check(view.reach == 1.0, "a clear fraction that is not finite reads as clear");
    const FixedWorld blocked(-0.5);
    camera.reset(subject, blocked, view);
    check(view.reach == 0.0 && same(view.eye, view.pivot, 0.0),
          "a negative fraction reads as 0 and the eye sits at the pivot");
}

// ---------------------------------------------------------------------------
// The renderer's side
// ---------------------------------------------------------------------------

void test_render_camera() {
    nfsu::ChaseCameraView view;
    view.eye = {10.0, -5.0, 2.0};
    view.heading = 5000;
    view.pitch = 3040;
    view.fov = 13020;
    const double h = 5000 * 2.0 * kTestPi / 65536.0;
    const double p = 3040 * 2.0 * kTestPi / 65536.0;
    view.forward = {std::cos(p) * std::cos(h), std::cos(p) * std::sin(h), -std::sin(p)};
    nfsu::OrbitCamera camera;
    std::string error;
    check_error(nfsu::drive_render_camera(view, nfsu::ChaseFovMode::vertical_4_3, 1280, 720, camera,
                                    error),
          "the renderer takes the camera: ", error);
    const nfsu::Vec3 eye = nfsu::orbit_eye(camera);
    check(near(eye.x, 10.0, 1e-12) && near(eye.y, 2.0, 1e-12) && near(eye.z, 5.0, 1e-12),
          "the renderer's eye is the view's, in its y-up frame");
    nfsu::CameraFrame frame;
    check_error(nfsu::make_camera_frame(camera, 1280, 720, frame, error), "a camera frame: ",
                error);
    check(near(frame.forward.x, view.forward.x, 1e-12) &&
              near(frame.forward.y, view.forward.z, 1e-12) &&
              near(frame.forward.z, -view.forward.y, 1e-12),
          "it looks along the view's forward vector");
    const double want = 2.0 * std::atan(std::tan(71.52099609375 / 2.0 * kTestPi / 180.0) * 0.75) *
                        180.0 / kTestPi;
    check(near(camera.fov_y_deg, want, 1e-12) && near(camera.fov_y_deg, 56.75, 0.01),
          "4:3 height kept: 56.75 degrees, " + num(camera.fov_y_deg));
    check(near(nfsu::drive_vertical_fov_degrees(13020, nfsu::ChaseFovMode::horizontal, 1280, 720),
               44.11, 0.01) &&
              nfsu::drive_vertical_fov_degrees(13020, nfsu::ChaseFovMode::vertical_4_3, 320,
                                               900) == camera.fov_y_deg,
          "horizontal on 16:9 is 44.11 degrees; the 4:3 height ignores the frame");
    check(!nfsu::drive_render_camera(view, nfsu::ChaseFovMode::horizontal, 0, 720, camera, error),
          "a zero-sized frame is refused");

    // The car placed at its pose: a vertex at the body origin lands on the
    // subject's origin, and one on the body's +x axis along its forward.
    const nfsu::VehicleModel model(nfsu::stock_240sx_vehicle_spec());
    nfsu::VehiclePose pose;
    pose.ground = {30.0, -12.0, 1.0};
    pose.heading = 1.2;
    nfsu::VehicleState state;
    check(model.reset(state, pose), "reset at a pose");
    const std::vector<nfsu::PreparedInstance> base(1);
    std::vector<nfsu::PreparedInstance> placed(1);
    check(nfsu::drive_place_instances(base, state, model.spec(), placed), "placement");
    const nfsu::ChaseCameraSubject subject = nfsu::chase_subject(state, model.spec());
    nfsu::MeshVertex vertex;
    const nfsu::Vec3 at_origin = nfsu::placed_vertex(vertex, placed[0].transform);
    check(near(at_origin.x, subject.origin.x, 1e-4) && near(at_origin.y, subject.origin.z, 1e-4) &&
              near(at_origin.z, -subject.origin.y, 1e-4),
          "the body origin lands on the subject's origin");
    vertex.px = 1.0F;
    const nfsu::Vec3 ahead = nfsu::placed_vertex(vertex, placed[0].transform);
    check(near(ahead.x - at_origin.x, subject.forward.x, 1e-5) &&
              near(ahead.z - at_origin.z, -subject.forward.y, 1e-5),
          "the body's +x lands along its forward");
    std::vector<nfsu::PreparedInstance> wrong(2);
    check(!nfsu::drive_place_instances(base, state, model.spec(), wrong),
          "a mismatched instance count is refused");

    // The ground: a checker of two shades under a level camera.
    nfsu::RenderOptions options;
    options.width = 96;
    options.height = 54;
    view.eye = {0.0, 0.0, 1.7};
    view.heading = 0;
    view.pitch = 0;
    view.forward = {1.0, 0.0, 0.0};
    check(nfsu::drive_render_camera(view, nfsu::ChaseFovMode::vertical_4_3, 96, 54,
                                    options.camera, error),
          "a level camera");
    nfsu::Frame image;
    const unsigned char background[4] = {40, 44, 52, 255};
    check(image.reset(96, 54, background, nfsu::RenderLimits{}, error), "a frame");
    nfsu::RenderStats stats;
    check_error(nfsu::drive_draw_ground(options, {0.0, 0.0, 0.0}, image, stats, error),
          "the ground draws: ", error);
    std::size_t base_shade = 0;
    std::size_t dark_shade = 0;
    std::size_t sky = 0;
    for (std::size_t i = 0; i + 3 < image.rgba().size(); i += 4) {
        const unsigned char r = image.rgba()[i];
        base_shade += r == 112 ? 1U : 0U;
        dark_shade += r == 84 ? 1U : 0U;
        sky += r == 40 ? 1U : 0U;
    }
    check(base_shade > 100 && dark_shade > 100 && sky > 1000 && stats.triangles_rasterized > 0,
          "two shades below the horizon, the sky above: " + std::to_string(base_shade) + " " +
              std::to_string(dark_shade) + " " + std::to_string(sky));
    nfsu::Frame small;
    check(small.reset(10, 10, background, nfsu::RenderLimits{}, error) &&
              !nfsu::drive_draw_ground(options, {0.0, 0.0, 0.0}, small, stats, error),
          "a frame of another size is refused");
}

// ---------------------------------------------------------------------------
// The session and the pin
// ---------------------------------------------------------------------------

struct DemoRun {
    std::string digest;
    std::uint64_t frames = 0;
    std::uint64_t bytes = 0;
    double top_speed = 0.0;
    double lowest_speed = 0.0;
    bool reversed = false;
    int pov_before = -1;
    int pov_after = -1;
    int final_gear = 0;
    double final_speed = 0.0;
    double throttle_phase_top = 0.0;
};

DemoRun run_demo(const nfsu::DriveSessionSetup& setup,
                 const std::vector<nfsu::GamepadFrame>& frames) {
    DemoRun run;
    nfsu::DriveSession session;
    std::string error;
    if (!session.start(setup, error)) {
        check(false, "the session starts: " + error);
        return run;
    }
    for (const nfsu::GamepadFrame& frame : frames) {
        if (!session.advance(frame, error)) {
            check(false, "the session advances: " + error);
            return run;
        }
        const double speed = session.telemetry().forward_speed;
        run.top_speed = std::max(run.top_speed, speed);
        run.lowest_speed = std::min(run.lowest_speed, speed);
        run.reversed = run.reversed || session.state().gear == nfsu::kVehicleGearReverse;
        if (session.frames() == 810) {
            run.pov_before = session.view().pov;
        }
        if (session.frames() == 811) {
            run.pov_after = session.view().pov;
        }
        if (session.frames() <= 390) {
            run.throttle_phase_top = std::max(run.throttle_phase_top, speed);
        }
    }
    run.frames = session.frames();
    run.bytes = session.hashed_bytes();
    run.final_gear = session.state().gear;
    run.final_speed = session.telemetry().forward_speed;
    run.digest = nfsu::sha256_hex(session.finish());
    return run;
}

void test_session() {
    const std::vector<nfsu::GamepadFrame> demo = nfsu::drive_demo_recording();
    check(demo.size() == 1380 && demo[60].time_us == 1000000 && demo[1379].time_us == 22983333,
          "the demonstration is 1380 frames at 60 a second");
    const nfsu::DriveSessionSetup setup = nfsu::default_drive_session_setup();
    const DemoRun a = run_demo(setup, demo);
    const DemoRun b = run_demo(setup, demo);
    check(a.digest == b.digest, "the same frames give the same stream");
    check(a.frames == 1380 && a.bytes == 1380 * nfsu::kDriveFrameRecordBytes,
          "every frame is recorded: " + std::to_string(a.bytes));
    check(a.digest == nfsu::kDrivePinnedDemoSha256, "the pinned demonstration: " + a.digest);
    check(a.top_speed > 34.0 && a.throttle_phase_top > 25.0,
          "full throttle reaches past 120 km/h: " + num(a.top_speed * 3.6));
    check(a.reversed && a.lowest_speed < -10.0,
          "the brake held at a standstill reverses the car: " + num(a.lowest_speed));
    check(a.pov_before == 0 && a.pov_after == 1, "X moves the camera from POV 0 to POV 1");
    check(a.final_gear == nfsu::kVehicleGearFirst && a.final_speed > 5.0,
          "the throttle held in reverse brings it back to first and forward");

    // The recorded form replays to the same stream.
    std::vector<nfsu::GamepadFrame> parsed;
    std::string error;
    // A pad timestamp is replay metadata, not elapsed solver time. These
    // checks catch a retained two-slice loop or a wall-clock catch-up loop.
    nfsu::DriveSession cadence, distant_clock;
    check(cadence.start(setup, error) && distant_clock.start(setup, error),
          "cadence sessions start: " + error);
    auto ordinary_pad = demo[0];
    auto distant_pad = ordinary_pad;
    distant_pad.time_us = 900000000;
    check(cadence.advance(ordinary_pad, error) && distant_clock.advance(distant_pad, error),
          "one input frame advances: " + error);
    check(cadence.state().step == 4 &&
              near(cadence.state().simulation_seconds, 1.0 / 60.0, 1e-12) &&
              cadence.telemetry().solver_dt == 1.0 / 240.0 &&
              cadence.hashed_bytes() == nfsu::kDriveFrameRecordBytes,
          "one free-drive frame completes four slices and one record");
    ordinary_pad.time_us = 16666;
    distant_pad.time_us = 1800000000;
    check(cadence.advance(ordinary_pad, error) && distant_clock.advance(distant_pad, error),
          "second input frame advances: " + error);
    check(cadence.frames() == 2 && cadence.state().step == 8 &&
              near(cadence.state().simulation_seconds, 2.0 / 60.0, 1e-12) &&
              cadence.hashed_bytes() == 2 * nfsu::kDriveFrameRecordBytes &&
              cadence.finish() == distant_clock.finish(),
          "a distant timestamp adds no catch-up physics or replay difference");
    const bool recorded = nfsu::parse_input_recording(nfsu::format_input_recording(demo), parsed, error);
    check(recorded, "the demonstration's recorded form parses: " + error);
    check(run_demo(setup, parsed).digest == a.digest, "and replays to the same stream");

    // A remapped table drives a different car: ZR no longer accelerates.
    nfsu::DriveSessionSetup remapped = setup;
    remapped.controls = parse_or_fail("nfsu-controls 1\nbind throttle face_east\n"
                                      "bind brake trigger_left\nbind steer left_x\n");
    const DemoRun c = run_demo(remapped, demo);
    check(c.digest != a.digest && c.throttle_phase_top < 0.5,
          "with ZR unbound the car does not move: " + num(c.throttle_phase_top));
    // Another POV at the start changes the camera and nothing else.
    nfsu::DriveSessionSetup pov4 = setup;
    pov4.pov = 4;
    const DemoRun d = run_demo(pov4, demo);
    check(d.digest != a.digest && d.top_speed == a.top_speed, "POV 4 moves only the camera");
    nfsu::DriveSessionSetup bad = setup;
    bad.pov = 2;
    nfsu::DriveSession session;
    check(!session.start(bad, error) && contains(error, "no POV 2"), "POV 2 is refused: " + error);
    check(!session.advance(demo[0], error) && contains(error, "not started"),
          "an unstarted session does not advance");
}

// ---------------------------------------------------------------------------
// The writers for the probe guards
// ---------------------------------------------------------------------------

int write_new(const fs::path& path, const std::string& text) {
    std::error_code code;
    if (fs::exists(path, code) || code) {
        std::cerr << "error: " << path.string() << " exists; nothing is replaced\n";
        return 1;
    }
    std::ofstream out(path, std::ios::binary);
    out << text;
    out.close();
    if (!out) {
        std::cerr << "error: cannot write " << path.string() << '\n';
        return 1;
    }
    return 0;
}

void test_car_animation() {
    const nfsu::VehicleSpec spec = nfsu::stock_240sx_vehicle_spec();
    nfsu::VehicleState state;
    state.position = {spec.handling.centre_of_mass[0],
                      spec.handling.centre_of_mass[1],
                      spec.handling.centre_of_mass[2]};
    state.steer_angle = 0.25;
    std::vector<nfsu::PreparedInstance> base(10), placed(10);
    for (std::size_t w = 0; w < 4; ++w) {
        const double lateral = w == 0 || w == 3 ? 0.8 : -0.8;
        for (std::size_t i : {w, w + 4}) {
            base[i].part_slot = i < 4 ? 17 : 16;
            base[i].copy = static_cast<std::uint32_t>(w);
            base[i].transform[12] = 1.0F;
            base[i].transform[13] = static_cast<float>(lateral);
            base[i].transform[14] = 0.1F;
            base[i].animation_pivot[0] = 1.0F;
            base[i].animation_pivot[1] = static_cast<float>(lateral);
            base[i].animation_pivot[2] = 0.1F;
        }
        state.wheels[w].compression = 0.05 * (w + 1);
    }
    base[8].part_slot = 13;
    base[9].part_slot = 14;
    nfsu::CarAnimationState visual;
    visual.wheel_phase[0] = kTestPi * 0.5;
    visual.braking = true;
    check(nfsu::drive_place_instances(base, state, spec, placed, &visual),
          "animated car placement");
    check(near(placed[0].transform[2], -1.0, 1e-6) &&
              near(placed[0].transform[14], 0.15, 1e-6),
          "front wheel spins about its axle and rises with suspension");
    check(near(placed[1].transform[1], nfsu::vehicle_sin(0.25), 1e-6) &&
              near(placed[4].transform[1], nfsu::vehicle_sin(0.25), 1e-6) &&
              near(placed[6].transform[1], 0.0, 1e-6),
          "front wheel and brake steer; rear brake does not");
    check(near(placed[4].transform[14], 0.15, 1e-6) &&
              placed[8].lamp_mode == 1 && placed[9].lamp_mode == 3,
          "brake follows suspension and braking lights the rear lamp");
    visual.braking = false;
    check(nfsu::drive_place_instances(base, state, spec, placed, &visual) &&
              placed[8].lamp_mode == 1 && placed[9].lamp_mode == 2,
          "headlights stay on and tail lamps dim without braking");
    check(base[8].lamp_mode == 0 && base[9].lamp_mode == 0 &&
              visual.wheel_phase[0] == kTestPi * 0.5 &&
              state.wheels[0].compression == 0.05,
          "animation leaves prepared source and vehicle state untouched");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-demo") {
        return write_new(nfsu::argument_path(argv[2]),
                         nfsu::format_input_recording(nfsu::drive_demo_recording()));
    }
    if (argc == 3 && std::string(argv[1]) == "--write-controls") {
        return write_new(
            nfsu::argument_path(argv[2]),
            nfsu::format_drive_controls_config(nfsu::default_drive_controls_config()));
    }
    if (argc != 1) {
        std::cerr << "usage: nfsu_drive_tests [--write-demo FILE | --write-controls FILE]\n";
        return 1;
    }
    test_config_round_trip();
    test_config_refusals();
    test_axis_calibration();
    test_calibration_through_update();
    test_sources_and_remapping();
    test_steer_filter();
    test_edges_and_latching();
    test_reverse_assist();
    test_camera_tables();
    test_angles();
    test_target_heading();
    test_lag();
    test_camera_place();
    test_camera_follow();
    test_camera_collision();
    test_render_camera();
    test_car_animation();
    test_session();
    if (g_failures != 0) {
        std::cerr << g_failures << " test failure(s)\n";
        return 1;
    }
    std::cout << "nfsu_drive_tests: all passed\n";
    return 0;
}
