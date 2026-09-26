#include "nfsu/drive_controls.h"

#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The driving controls of issue #59. include/nfsu/drive_controls.h states
// each rule and docs/CONTROLS_CAMERA.md labels it. Plain IEEE arithmetic
// with contraction disabled by cmake/drive_libraries.cmake, and no library
// mathematics at all: the same frames give the same inputs everywhere.

#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "the driving controls need double arithmetic without excess precision"
#endif

namespace nfsu {

const std::array<float, 2 * kDriveRetailThrottlePoints> kDriveRetailThrottleTable = {
    0.0F,   0.0F,    // 0x00700374
    0.575F, 0.437F,  // 0x0070037C
    0.797F, 0.62F,   // 0x00700384
    0.894F, 0.746F,  // 0x0070038C
    0.95F,  0.866F,  // 0x00700394
    1.0F,   1.0F};   // 0x0070039C

namespace {

constexpr double kStep = kVehicleStepSeconds;

std::int64_t clamp_axis(std::int32_t value) noexcept {
    if (value < -kAxisMax) {
        return -std::int64_t{kAxisMax};
    }
    if (value > kAxisMax) {
        return kAxisMax;
    }
    return value;
}

double clamp(double value, double lo, double hi) noexcept {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

bool finite(double value) noexcept {
    return (value - value) == 0.0;
}

bool analog_action(DriveAction action) noexcept {
    return action == DriveAction::steer || action == DriveAction::throttle ||
           action == DriveAction::brake;
}

bool known_action(DriveAction action) noexcept {
    return static_cast<std::size_t>(action) < kDriveActionCount;
}

const char* part_suffix(DriveAxisPart part) noexcept {
    switch (part) {
    case DriveAxisPart::both:
        return "";
    case DriveAxisPart::positive:
        return "+";
    case DriveAxisPart::negative:
        return "-";
    }
    return "?";
}

std::string control_name(const DriveBinding& binding) {
    if (binding.control.kind == GamepadControl::Kind::button) {
        if (binding.control.index >= kGamepadButtonCount) {
            return "button " + std::to_string(binding.control.index);
        }
        return gamepad_button_name(static_cast<GamepadButton>(binding.control.index));
    }
    if (binding.control.index >= kGamepadAxisCount) {
        return "axis " + std::to_string(binding.control.index);
    }
    return std::string(gamepad_axis_name(static_cast<GamepadAxis>(binding.control.index))) +
           part_suffix(binding.part);
}

// True when the two bindings read overlapping travel of one control.
bool overlaps(const DriveBinding& a, const DriveBinding& b) noexcept {
    if (a.control.kind != b.control.kind || a.control.index != b.control.index) {
        return false;
    }
    if (a.control.kind == GamepadControl::Kind::button) {
        return true;
    }
    return a.part == DriveAxisPart::both || b.part == DriveAxisPart::both || a.part == b.part;
}

bool rate_ok(double value, bool zero_allowed) noexcept {
    if (!finite(value)) {
        return false;
    }
    if (zero_allowed && value == 0.0) {
        return true;
    }
    return value > 0.0 && value <= kMaxDriveRate;
}

// 0x00454F30, the pair-table evaluator speed.exe applies the throttle curve
// with: at or below the first input the first output, at or past the last
// the last, otherwise linear between the two pairs the value falls between.
double pair_table(const float* pairs, std::size_t count, double x) noexcept {
    if (!(x > static_cast<double>(pairs[0]))) {
        return static_cast<double>(pairs[1]);
    }
    if (!(x < static_cast<double>(pairs[2 * (count - 1)]))) {
        return static_cast<double>(pairs[2 * (count - 1) + 1]);
    }
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const double x0 = static_cast<double>(pairs[2 * i]);
        const double y0 = static_cast<double>(pairs[2 * i + 1]);
        const double x1 = static_cast<double>(pairs[2 * i + 2]);
        const double y1 = static_cast<double>(pairs[2 * i + 3]);
        if (x >= x0 && x < x1) {
            return y0 + (x - x0) / (x1 - x0) * (y1 - y0);
        }
    }
    return static_cast<double>(pairs[2 * (count - 1) + 1]);
}

double shape_throttle(const DriveControlsConfig& config, double value) noexcept {
    if (!(value > 0.0)) {
        return 0.0;
    }
    double shaped = value;
    if (config.throttle_curve == DriveThrottleCurve::retail) {
        shaped = drive_retail_throttle_curve(shaped);
    }
    shaped = config.throttle_floor + (1.0 - config.throttle_floor) * shaped;
    return clamp(shaped, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// The text form
// ---------------------------------------------------------------------------

struct Line {
    std::size_t number = 0;
    std::vector<std::string> fields;
};

bool split_lines(const std::string& text, std::vector<Line>& lines, std::string& error) {
    std::size_t number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        const bool last = end == std::string::npos;
        if (last) {
            end = text.size();
        }
        ++number;
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        for (const char c : line) {
            const unsigned char byte = static_cast<unsigned char>(c);
            if (byte < 0x20U || byte > 0x7EU) {
                error = "line " + std::to_string(number) +
                        ": a byte outside printable ASCII";
                return false;
            }
        }
        if (!line.empty() && line[0] != '#') {
            Line parsed;
            parsed.number = number;
            std::size_t i = 0;
            while (i < line.size()) {
                while (i < line.size() && line[i] == ' ') {
                    ++i;
                }
                const std::size_t field = i;
                while (i < line.size() && line[i] != ' ') {
                    ++i;
                }
                if (i > field) {
                    parsed.fields.push_back(line.substr(field, i - field));
                }
            }
            if (!parsed.fields.empty()) {
                lines.push_back(parsed);
            }
        }
        if (last) {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool parse_count(const std::string& text, std::uint64_t limit, std::uint64_t& out) noexcept {
    if (text.empty() || text.size() > 10 || (text.size() > 1 && text[0] == '0')) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10U + static_cast<std::uint64_t>(c - '0');
    }
    if (value > limit) {
        return false;
    }
    out = value;
    return true;
}

bool find_button(const std::string& name, std::uint8_t& index) noexcept {
    for (std::size_t i = 0; i < kGamepadButtonCount; ++i) {
        if (name == gamepad_button_name(static_cast<GamepadButton>(i))) {
            index = static_cast<std::uint8_t>(i);
            return true;
        }
    }
    return false;
}

bool find_axis(const std::string& name, std::uint8_t& index) noexcept {
    for (std::size_t i = 0; i < kGamepadAxisCount; ++i) {
        if (name == gamepad_axis_name(static_cast<GamepadAxis>(i))) {
            index = static_cast<std::uint8_t>(i);
            return true;
        }
    }
    return false;
}

bool find_action(const std::string& name, DriveAction& action) noexcept {
    for (std::size_t i = 0; i < kDriveActionCount; ++i) {
        if (name == drive_action_name(static_cast<DriveAction>(i))) {
            action = static_cast<DriveAction>(i);
            return true;
        }
    }
    return false;
}

bool parse_control(const std::string& token, DriveBinding& binding) noexcept {
    std::uint8_t index = 0;
    if (find_button(token, index)) {
        binding.control = gamepad_control(static_cast<GamepadButton>(index));
        binding.part = DriveAxisPart::both;
        return true;
    }
    std::string name = token;
    DriveAxisPart part = DriveAxisPart::both;
    if (!name.empty() && (name.back() == '+' || name.back() == '-')) {
        part = name.back() == '+' ? DriveAxisPart::positive : DriveAxisPart::negative;
        name.pop_back();
    }
    if (!find_axis(name, index)) {
        return false;
    }
    binding.control = gamepad_control(static_cast<GamepadAxis>(index));
    binding.part = part;
    return true;
}

// A fixed decimal as the vehicle script reads one, then re-formatted and
// compared, so that "0.50", "+1" and "00.3" are refused as not canonical:
// the file is a statement a person reads, and two spellings of one value
// are one too many.
bool parse_decimal(const std::string& text, double& out) {
    if (!parse_vehicle_decimal(text, out)) {
        return false;
    }
    if (text[0] == '-' || text[0] == '+') {
        return false;
    }
    const std::size_t dot = text.find('.');
    const std::string whole = dot == std::string::npos ? text : text.substr(0, dot);
    if (whole.size() > 1 && whole[0] == '0') {
        return false;
    }
    if (dot != std::string::npos) {
        const std::string fraction = text.substr(dot + 1);
        if (fraction.empty() || fraction.back() == '0') {
            return false;
        }
    }
    return true;
}

std::string format_decimal(double value) {
    // Every value a configuration holds came from parse_decimal or from the
    // defaults, which are all written with at most six decimals; print the
    // shortest of up to six decimals that reads back to the same double.
    if (!(value >= 0.0)) {
        return "0";
    }
    const std::uint64_t scale = 1000000U;
    const double scaled = value * static_cast<double>(scale) + 0.5;
    const std::uint64_t units = static_cast<std::uint64_t>(scaled);
    std::string whole = std::to_string(units / scale);
    std::string fraction = std::to_string(units % scale);
    while (fraction.size() < 6) {
        fraction.insert(fraction.begin(), '0');
    }
    while (!fraction.empty() && fraction.back() == '0') {
        fraction.pop_back();
    }
    return fraction.empty() ? whole : whole + "." + fraction;
}

} // namespace

// ---------------------------------------------------------------------------
// Names, defaults and validation
// ---------------------------------------------------------------------------

const char* drive_action_name(DriveAction action) noexcept {
    switch (action) {
    case DriveAction::steer:
        return "steer";
    case DriveAction::throttle:
        return "throttle";
    case DriveAction::brake:
        return "brake";
    case DriveAction::handbrake:
        return "handbrake";
    case DriveAction::shift_up:
        return "shift-up";
    case DriveAction::shift_down:
        return "shift-down";
    case DriveAction::camera:
        return "camera";
    case DriveAction::reset:
        return "reset";
    }
    return "unknown";
}

DriveControlsConfig default_drive_controls_config() {
    DriveControlsConfig config;
    const auto button = [&config](DriveAction action, GamepadButton control, bool invert) {
        DriveBinding binding;
        binding.action = action;
        binding.control = gamepad_control(control);
        binding.invert = invert;
        config.bindings.push_back(binding);
    };
    DriveBinding stick;
    stick.action = DriveAction::steer;
    stick.control = gamepad_control(GamepadAxis::left_x);
    config.bindings.push_back(stick);
    button(DriveAction::steer, GamepadButton::dpad_left, true);
    button(DriveAction::steer, GamepadButton::dpad_right, false);
    button(DriveAction::throttle, GamepadButton::trigger_right, false);
    button(DriveAction::brake, GamepadButton::trigger_left, false);
    button(DriveAction::handbrake, GamepadButton::face_south, false);
    button(DriveAction::shift_up, GamepadButton::shoulder_right, false);
    button(DriveAction::shift_down, GamepadButton::shoulder_left, false);
    button(DriveAction::camera, GamepadButton::face_north, false);
    button(DriveAction::reset, GamepadButton::select, false);
    return config;
}

bool validate_drive_controls_config(const DriveControlsConfig& config, std::string& error) {
    if (config.bindings.empty()) {
        error = "the table binds nothing";
        return false;
    }
    if (config.bindings.size() > kMaxDriveBindings) {
        error = "the table holds " + std::to_string(config.bindings.size()) +
                " bindings; at most " + std::to_string(kMaxDriveBindings);
        return false;
    }
    for (std::size_t i = 0; i < config.bindings.size(); ++i) {
        const DriveBinding& binding = config.bindings[i];
        const std::string where = "binding " + std::to_string(i) + ": ";
        if (!known_action(binding.action)) {
            error = where + "names no action";
            return false;
        }
        const std::string action = drive_action_name(binding.action);
        const bool button = binding.control.kind == GamepadControl::Kind::button;
        const bool axis = binding.control.kind == GamepadControl::Kind::axis;
        if ((!button && !axis) || (button && binding.control.index >= kGamepadButtonCount) ||
            (axis && binding.control.index >= kGamepadAxisCount)) {
            error = where + "names no button and no axis";
            return false;
        }
        const std::string name = control_name(binding);
        if (static_cast<std::uint8_t>(binding.part) > 2U) {
            error = where + "reads no known part of " + name;
            return false;
        }
        if (button && binding.part != DriveAxisPart::both) {
            error = where + name + " is a button and has no half";
            return false;
        }
        if (axis && !analog_action(binding.action)) {
            error = where + name + " is a stick axis and cannot drive " + action;
            return false;
        }
        if (binding.invert && binding.action != DriveAction::steer) {
            error = where + action + " cannot be inverted" +
                    (analog_action(binding.action) ? "; bind the other half of the axis" : "");
            return false;
        }
        if (axis && binding.part == DriveAxisPart::both &&
            (binding.action == DriveAction::throttle || binding.action == DriveAction::brake)) {
            error = where + action + " reads 0..1 and " + name +
                    " is signed; bind " + name + "+ or " + name + "-";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (overlaps(config.bindings[j], binding)) {
                error = where + name + " is already read by binding " + std::to_string(j) + " (" +
                        drive_action_name(config.bindings[j].action) + " " +
                        control_name(config.bindings[j]) + ")";
                return false;
            }
        }
    }
    for (std::size_t a = 0; a < kGamepadAxisCount; ++a) {
        const DriveAxisCalibration& zone = config.axes[a];
        if (zone.inner < 0 || zone.outer > kAxisMax || zone.inner >= zone.outer) {
            error = std::string("axis ") + gamepad_axis_name(static_cast<GamepadAxis>(a)) +
                    ": the calibration needs 0 <= inner < outer <= " + std::to_string(kAxisMax);
            return false;
        }
    }
    if (!finite(config.throttle_floor) || config.throttle_floor < 0.0 ||
        config.throttle_floor > kMaxDriveThrottleFloor) {
        error = "throttle-floor must be within 0..0.9";
        return false;
    }
    if (config.throttle_curve != DriveThrottleCurve::linear &&
        config.throttle_curve != DriveThrottleCurve::retail) {
        error = "throttle-curve is neither linear nor retail";
        return false;
    }
    if (!rate_ok(config.steer.digital_rate, false) ||
        !rate_ok(config.steer.digital_rate_speed, false)) {
        error = "steer-digital-rate needs a rate and a speed above 0 and at most 1000";
        return false;
    }
    if (!rate_ok(config.steer.centre_rate, false)) {
        error = "steer-centre-rate needs a rate above 0 and at most 1000";
        return false;
    }
    if (!rate_ok(config.steer.analog_rate, true)) {
        error = "steer-analog-rate needs 0 or a rate above 0 and at most 1000";
        return false;
    }
    if (!finite(config.reverse_speed) || !(config.reverse_speed > 0.0) ||
        config.reverse_speed > kMaxDriveReverseSpeed ||
        config.reverse_hold_steps > kMaxDriveReverseHoldSteps) {
        error = "reverse-assist needs a speed above 0 and at most 5 m/s and at most 1200 steps";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The text form
// ---------------------------------------------------------------------------

bool parse_drive_controls_config(const std::string& text,
                                 DriveControlsConfig& out,
                                 std::string& error) {
    if (text.size() > kMaxDriveConfigBytes) {
        error = "the file holds " + std::to_string(text.size()) + " bytes; at most " +
                std::to_string(kMaxDriveConfigBytes);
        return false;
    }
    std::vector<Line> lines;
    if (!split_lines(text, lines, error)) {
        return false;
    }
    if (lines.empty() || lines[0].fields.size() != 2 || lines[0].fields[0] != "nfsu-controls" ||
        lines[0].fields[1] != "1") {
        error = "line 1: the header must be 'nfsu-controls 1'";
        return false;
    }
    // Header first: a file whose first non-comment line is not the header is
    // refused on that line rather than on line 1 when a comment precedes it.
    if (lines[0].number != 1) {
        error = "line " + std::to_string(lines[0].number) +
                ": the header 'nfsu-controls 1' must be the first line";
        return false;
    }
    DriveControlsConfig config = default_drive_controls_config();
    bool bound = false;
    std::vector<std::size_t> binding_lines;
    std::vector<std::string> seen;
    for (std::size_t n = 1; n < lines.size(); ++n) {
        const Line& line = lines[n];
        const std::string& directive = line.fields[0];
        const std::string where = "line " + std::to_string(line.number) + ": " + directive + ": ";
        const std::size_t args = line.fields.size() - 1;
        if (directive != "bind") {
            for (const std::string& earlier : seen) {
                if (earlier == directive + (directive == "axis" && args >= 1 ? " " + line.fields[1] : "")) {
                    error = where + "is given twice";
                    return false;
                }
            }
            seen.push_back(directive + (directive == "axis" && args >= 1 ? " " + line.fields[1] : ""));
        }
        if (directive == "bind") {
            if (args != 2 && args != 3) {
                error = where + "needs <action> <control> [invert]";
                return false;
            }
            DriveBinding binding;
            if (!find_action(line.fields[1], binding.action)) {
                error = where + "unknown action '" + line.fields[1] + "'";
                return false;
            }
            if (!parse_control(line.fields[2], binding)) {
                error = where + "unknown control '" + line.fields[2] + "'";
                return false;
            }
            if (args == 3) {
                if (line.fields[3] != "invert") {
                    error = where + "unknown keyword '" + line.fields[3] + "'; only 'invert'";
                    return false;
                }
                binding.invert = true;
            }
            if (!bound) {
                config.bindings.clear();
                bound = true;
            }
            if (config.bindings.size() == kMaxDriveBindings) {
                error = where + "more than " + std::to_string(kMaxDriveBindings) + " bindings";
                return false;
            }
            config.bindings.push_back(binding);
            binding_lines.push_back(line.number);
        } else if (directive == "axis") {
            std::uint8_t index = 0;
            std::uint64_t inner = 0;
            std::uint64_t outer = 0;
            if (args != 3) {
                error = where + "needs <axis> <inner> <outer>";
                return false;
            }
            if (!find_axis(line.fields[1], index)) {
                error = where + "unknown axis '" + line.fields[1] + "'";
                return false;
            }
            if (!parse_count(line.fields[2], static_cast<std::uint64_t>(kAxisMax), inner) ||
                !parse_count(line.fields[3], static_cast<std::uint64_t>(kAxisMax), outer)) {
                error = where + "inner and outer must be whole numbers from 0 to 32767";
                return false;
            }
            if (inner >= outer) {
                error = where + line.fields[1] + " needs inner below outer";
                return false;
            }
            config.axes[index].inner = static_cast<std::int32_t>(inner);
            config.axes[index].outer = static_cast<std::int32_t>(outer);
        } else if (directive == "throttle-floor") {
            if (args != 1 || !parse_decimal(line.fields[1], config.throttle_floor) ||
                config.throttle_floor > kMaxDriveThrottleFloor) {
                error = where + "needs one canonical decimal from 0 to 0.9";
                return false;
            }
        } else if (directive == "throttle-curve") {
            if (args != 1 || (line.fields[1] != "linear" && line.fields[1] != "retail")) {
                error = where + "needs 'linear' or 'retail'";
                return false;
            }
            config.throttle_curve = line.fields[1] == "retail" ? DriveThrottleCurve::retail
                                                               : DriveThrottleCurve::linear;
        } else if (directive == "steer-digital-rate") {
            if (args != 2 || !parse_decimal(line.fields[1], config.steer.digital_rate) ||
                !parse_decimal(line.fields[2], config.steer.digital_rate_speed)) {
                error = where + "needs <per-second> <speed>, canonical decimals";
                return false;
            }
        } else if (directive == "steer-centre-rate") {
            if (args != 1 || !parse_decimal(line.fields[1], config.steer.centre_rate)) {
                error = where + "needs one canonical decimal";
                return false;
            }
        } else if (directive == "steer-analog-rate") {
            if (args != 1 || !parse_decimal(line.fields[1], config.steer.analog_rate)) {
                error = where + "needs one canonical decimal";
                return false;
            }
        } else if (directive == "reverse-assist") {
            if ((args != 1 && args != 3) ||
                (line.fields[1] != "on" && line.fields[1] != "off")) {
                error = where + "needs <on|off> [<speed> <hold steps>]";
                return false;
            }
            config.reverse_assist = line.fields[1] == "on";
            if (args == 3) {
                std::uint64_t hold = 0;
                if (!parse_decimal(line.fields[2], config.reverse_speed) ||
                    !parse_count(line.fields[3], kMaxDriveReverseHoldSteps, hold)) {
                    error = where + "needs a canonical decimal speed and whole hold steps";
                    return false;
                }
                config.reverse_hold_steps = static_cast<std::uint32_t>(hold);
            }
        } else if (directive == "nfsu-controls") {
            error = "line " + std::to_string(line.number) + ": the header is given twice";
            return false;
        } else {
            error = "line " + std::to_string(line.number) + ": unknown directive '" + directive +
                    "'";
            return false;
        }
    }
    std::string invalid;
    if (!validate_drive_controls_config(config, invalid)) {
        // Name the line of the binding a table refusal is about.
        const std::string prefix = "binding ";
        if (invalid.compare(0, prefix.size(), prefix) == 0 && bound) {
            const std::size_t colon = invalid.find(':');
            std::uint64_t index = 0;
            if (colon != std::string::npos &&
                parse_count(invalid.substr(prefix.size(), colon - prefix.size()), 1000, index) &&
                index < binding_lines.size()) {
                error = "line " + std::to_string(binding_lines[index]) + ": bind:" +
                        invalid.substr(colon + 1);
                return false;
            }
        }
        error = "the configuration is refused: " + invalid;
        return false;
    }
    out = config;
    return true;
}

std::string format_drive_controls_config(const DriveControlsConfig& config) {
    std::string text = "nfsu-controls 1\n";
    for (const DriveBinding& binding : config.bindings) {
        text += "bind ";
        text += drive_action_name(binding.action);
        text += ' ';
        text += control_name(binding);
        if (binding.invert) {
            text += " invert";
        }
        text += '\n';
    }
    for (std::size_t a = 0; a < kGamepadAxisCount; ++a) {
        text += "axis ";
        text += gamepad_axis_name(static_cast<GamepadAxis>(a));
        text += ' ' + std::to_string(config.axes[a].inner) + ' ' +
                std::to_string(config.axes[a].outer) + '\n';
    }
    text += "throttle-floor " + format_decimal(config.throttle_floor) + '\n';
    text += std::string("throttle-curve ") +
            (config.throttle_curve == DriveThrottleCurve::retail ? "retail" : "linear") + '\n';
    text += "steer-digital-rate " + format_decimal(config.steer.digital_rate) + ' ' +
            format_decimal(config.steer.digital_rate_speed) + '\n';
    text += "steer-centre-rate " + format_decimal(config.steer.centre_rate) + '\n';
    text += "steer-analog-rate " + format_decimal(config.steer.analog_rate) + '\n';
    text += std::string("reverse-assist ") + (config.reverse_assist ? "on" : "off") + ' ' +
            format_decimal(config.reverse_speed) + ' ' +
            std::to_string(config.reverse_hold_steps) + '\n';
    return text;
}

// ---------------------------------------------------------------------------
// Reading frames and stepping
// ---------------------------------------------------------------------------

double drive_axis_value(const DriveAxisCalibration& calibration, std::int32_t raw) noexcept {
    const std::int64_t value = clamp_axis(raw);
    const std::int64_t magnitude = value < 0 ? -value : value;
    if (magnitude <= calibration.inner) {
        return 0.0;
    }
    double scaled = 1.0;
    if (magnitude < calibration.outer) {
        scaled = static_cast<double>(magnitude - calibration.inner) /
                 static_cast<double>(calibration.outer - calibration.inner);
    }
    return value < 0 ? -scaled : scaled;
}

double drive_retail_throttle_curve(double value) noexcept {
    return pair_table(kDriveRetailThrottleTable.data(), kDriveRetailThrottlePoints, value);
}

DriveControls::DriveControls() : config_(default_drive_controls_config()) {}

bool DriveControls::configure(const DriveControlsConfig& config, std::string& error) {
    if (!validate_drive_controls_config(config, error)) {
        return false;
    }
    config_ = config;
    reset();
    return true;
}

void DriveControls::reset() noexcept {
    frame_ = DriveFrameState{};
    for (std::size_t slot = 0; slot < kMaxGamepads; ++slot) {
        connected_[slot] = false;
        latched_[slot] = 0;
    }
    held_.fill(false);
    pending_up_ = false;
    pending_down_ = false;
    steer_ = 0.0;
    reverse_count_ = 0;
}

void DriveControls::update(const GamepadFrame& frame) {
    double digital[3] = {0.0, 0.0, 0.0};
    double analog[3] = {0.0, 0.0, 0.0};
    bool from_button[3] = {false, false, false};
    std::array<bool, kDriveActionCount> held{};
    for (std::size_t slot = 0; slot < kMaxGamepads; ++slot) {
        const GamepadState& pad = frame.pads[slot];
        if (!pad.connected) {
            connected_[slot] = false;
            latched_[slot] = 0;
            continue;
        }
        const std::uint32_t buttons = pad.buttons & kGamepadButtonMask;
        if (!connected_[slot]) {
            connected_[slot] = true;
            latched_[slot] = buttons;
        }
        latched_[slot] &= buttons;
        const std::uint32_t live = buttons & ~latched_[slot];
        for (const DriveBinding& binding : config_.bindings) {
            const std::size_t action = static_cast<std::size_t>(binding.action);
            if (binding.control.kind == GamepadControl::Kind::button) {
                const GamepadButton button = static_cast<GamepadButton>(binding.control.index);
                if ((live & gamepad_button_bit(button)) == 0U) {
                    continue;
                }
                if (analog_action(binding.action)) {
                    digital[action] += binding.invert ? -1.0 : 1.0;
                    from_button[action] = true;
                } else {
                    held[action] = true;
                }
                continue;
            }
            const std::size_t axis = binding.control.index;
            double value = drive_axis_value(config_.axes[axis], pad.axes[axis]);
            if (binding.part == DriveAxisPart::positive) {
                value = value > 0.0 ? value : 0.0;
            } else if (binding.part == DriveAxisPart::negative) {
                value = value < 0.0 ? -value : 0.0;
            }
            analog[action] += binding.invert ? -value : value;
        }
    }
    const std::size_t steer = static_cast<std::size_t>(DriveAction::steer);
    const std::size_t throttle = static_cast<std::size_t>(DriveAction::throttle);
    const std::size_t brake = static_cast<std::size_t>(DriveAction::brake);
    DriveFrameState state;
    state.steer_digital = clamp(digital[steer], -1.0, 1.0);
    state.throttle_digital = clamp(digital[throttle], 0.0, 1.0);
    state.brake_digital = clamp(digital[brake], 0.0, 1.0);
    state.steer_analog = clamp(analog[steer], -1.0, 1.0);
    state.throttle_analog = shape_throttle(config_, clamp(analog[throttle], 0.0, 1.0));
    state.brake_analog = clamp(analog[brake], 0.0, 1.0);
    // A button source that is held but cancelled by an opposite one still
    // decides the source: two buttons pushing both ways are a centred
    // digital steer, not a hand-over to the stick.
    state.steer_from_button = from_button[steer];
    state.throttle_from_button = from_button[throttle];
    state.brake_from_button = from_button[brake];
    const auto edge = [&](DriveAction action) {
        const std::size_t index = static_cast<std::size_t>(action);
        return held[index] && !held_[index];
    };
    state.handbrake = held[static_cast<std::size_t>(DriveAction::handbrake)];
    state.shift_up = edge(DriveAction::shift_up);
    state.shift_down = edge(DriveAction::shift_down);
    state.camera = edge(DriveAction::camera);
    state.reset = edge(DriveAction::reset);
    held_ = held;
    frame_ = state;
    pending_up_ = pending_up_ || state.shift_up;
    pending_down_ = pending_down_ || state.shift_down;
}

VehicleInput DriveControls::step(double forward_speed, int gear, VehicleGearbox gearbox) {
    const double speed_signed = finite(forward_speed) ? forward_speed : 0.0;
    const double speed = speed_signed < 0.0 ? -speed_signed : speed_signed;

    // The steering filter.
    const bool button = frame_.steer_from_button;
    const double target = button ? frame_.steer_digital : frame_.steer_analog;
    const bool outward = (target > 0.0 && steer_ >= 0.0 && target > steer_) ||
                         (target < 0.0 && steer_ <= 0.0 && target < steer_);
    double rate = config_.steer.centre_rate;
    if (outward) {
        rate = button ? config_.steer.digital_rate /
                            (1.0 + speed / config_.steer.digital_rate_speed)
                      : config_.steer.analog_rate;
    }
    if (outward && !button && rate == 0.0) {
        steer_ = target;
    } else {
        // Toward the target, never past it; a reversal first returns to
        // centre at the centre rate, then leaves it at the outward rate.
        const double limit = rate * kStep;
        double goal = target;
        if ((steer_ > 0.0 && target < 0.0) || (steer_ < 0.0 && target > 0.0)) {
            goal = 0.0;
        }
        if (goal > steer_) {
            steer_ = steer_ + limit < goal ? steer_ + limit : goal;
        } else if (goal < steer_) {
            steer_ = steer_ - limit > goal ? steer_ - limit : goal;
        }
    }

    double throttle = frame_.throttle_from_button ? frame_.throttle_digital : frame_.throttle_analog;
    double brake = frame_.brake_from_button ? frame_.brake_digital : frame_.brake_analog;

    VehicleInput input;
    input.steer = -steer_;
    input.handbrake = frame_.handbrake;
    if (pending_up_) {
        input.shift = VehicleShift::up;
    } else if (pending_down_) {
        input.shift = VehicleShift::down;
    }
    const bool requested = pending_up_ || pending_down_;
    pending_up_ = false;
    pending_down_ = false;

    if (config_.reverse_assist && gearbox == VehicleGearbox::automatic) {
        const bool slow = speed < config_.reverse_speed;
        const bool reverse = gear == kVehicleGearReverse;
        const bool asking = reverse ? (throttle > 0.0 && !(brake > 0.0))
                                    : (brake > 0.0 && !(throttle > 0.0));
        if (slow && asking && !requested) {
            ++reverse_count_;
            if (reverse_count_ >= config_.reverse_hold_steps) {
                input.shift = reverse ? VehicleShift::up : VehicleShift::down;
                reverse_count_ = 0;
            }
        } else {
            reverse_count_ = 0;
        }
        if (reverse) {
            const double swap = throttle;
            throttle = brake;
            brake = swap;
        }
    }
    input.throttle = throttle;
    input.brake = brake;
    return input;
}

} // namespace nfsu
