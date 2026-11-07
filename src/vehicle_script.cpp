#include "nfsu/vehicle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

namespace {

VehicleScriptSegment segment(std::uint32_t steps, double throttle, double brake, double steer,
                             bool handbrake = false,
                             VehicleShift shift = VehicleShift::none) {
    VehicleScriptSegment s;
    s.steps = steps;
    s.input.throttle = throttle;
    s.input.brake = brake;
    s.input.steer = steer;
    s.input.handbrake = handbrake;
    s.input.shift = shift;
    return s;
}

std::string line_error(std::size_t line, const std::string& message) {
    return "line " + std::to_string(line) + ": " + message;
}

// Splits on spaces and tabs; nothing else separates.
std::vector<std::string> split_fields(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (const char c : line) {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) {
                fields.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        fields.push_back(current);
    }
    return fields;
}

bool parse_steps(const std::string& text, std::uint32_t& out) {
    if (text.empty() || text.size() > 9) {
        return false;
    }
    std::uint32_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10U + static_cast<std::uint32_t>(c - '0');
    }
    out = value;
    return true;
}

} // namespace

bool parse_vehicle_decimal(const std::string& text, double& out) noexcept {
    std::size_t at = 0;
    bool negative = false;
    if (at < text.size() && text[at] == '-') {
        negative = true;
        ++at;
    }
    std::uint64_t mantissa = 0;
    std::size_t integer_digits = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        mantissa = mantissa * 10U + static_cast<std::uint64_t>(text[at] - '0');
        ++integer_digits;
        ++at;
    }
    std::size_t decimals = 0;
    if (at < text.size() && text[at] == '.') {
        ++at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            mantissa = mantissa * 10U + static_cast<std::uint64_t>(text[at] - '0');
            ++decimals;
            ++at;
        }
        if (decimals == 0) {
            return false;
        }
    }
    if (at != text.size() || integer_digits == 0 || integer_digits > 9 || decimals > 6) {
        return false;
    }
    // At most 15 digits, so the mantissa and the power of ten are exact
    // doubles and the one division is the only rounding: every platform
    // reads the same value.
    double power = 1.0;
    for (std::size_t i = 0; i < decimals; ++i) {
        power = power * 10.0;
    }
    const double value = static_cast<double>(mantissa) / power;
    out = negative ? -value : value;
    return true;
}

std::uint64_t VehicleScript::total_steps() const noexcept {
    std::uint64_t total = 0;
    for (const VehicleScriptSegment& s : segments) {
        total += s.steps;
    }
    return total;
}

VehicleInput VehicleScript::input_at(std::uint64_t index) const noexcept {
    std::uint64_t start = 0;
    for (const VehicleScriptSegment& s : segments) {
        if (index < start + s.steps) {
            VehicleInput input = s.input;
            if (index != start) {
                input.shift = VehicleShift::none;
            }
            return input;
        }
        start += s.steps;
    }
    return VehicleInput{};
}

bool parse_vehicle_script(const std::string& text, VehicleScript& out, std::string& error,
                          const VehicleScriptLimits& limits) {
    if (text.size() > limits.max_bytes) {
        error = "the script is " + std::to_string(text.size()) + " bytes, more than the limit of " +
                std::to_string(limits.max_bytes);
        return false;
    }
    VehicleScript script;
    script.name = "file";
    bool have_speed = false;
    bool have_gearbox = false;
    std::uint64_t total = 0;
    std::size_t line_number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        start = end + 1;
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        for (const char c : line) {
            const unsigned char u = static_cast<unsigned char>(c);
            if ((u < 0x20 && c != '\t') || u > 0x7E) {
                error = line_error(line_number, "a byte outside printable ASCII");
                return false;
            }
        }
        const std::vector<std::string> fields = split_fields(line);
        if (fields.empty() || fields[0][0] == '#') {
            if (end == text.size()) {
                break;
            }
            continue;
        }
        if (fields[0] == "speed" || fields[0] == "gearbox") {
            if (!script.segments.empty()) {
                error = line_error(line_number, "'" + fields[0] + "' must come before the first segment");
                return false;
            }
            if (fields.size() != 2) {
                error = line_error(line_number, "'" + fields[0] + "' takes exactly one value");
                return false;
            }
            if (fields[0] == "speed") {
                if (have_speed) {
                    error = line_error(line_number, "'speed' is given twice");
                    return false;
                }
                double speed = 0.0;
                if (!parse_vehicle_decimal(fields[1], speed) || speed < -100.0 || speed > 100.0) {
                    error = line_error(line_number,
                                       "the speed must be a fixed decimal from -100 to 100 m/s");
                    return false;
                }
                script.speed = speed;
                have_speed = true;
            } else {
                if (have_gearbox) {
                    error = line_error(line_number, "'gearbox' is given twice");
                    return false;
                }
                if (fields[1] == "automatic") {
                    script.gearbox = VehicleGearbox::automatic;
                } else if (fields[1] == "manual") {
                    script.gearbox = VehicleGearbox::manual;
                } else {
                    error = line_error(line_number, "the gearbox must be automatic or manual");
                    return false;
                }
                have_gearbox = true;
            }
        } else {
            if (fields.size() != 6) {
                error = line_error(line_number,
                                   "a segment is <steps> <throttle> <brake> <steer> "
                                   "<handbrake 0|1> <none|up|down>, " +
                                       std::to_string(fields.size()) + " fields given");
                return false;
            }
            VehicleScriptSegment s;
            if (!parse_steps(fields[0], s.steps) || s.steps == 0) {
                error = line_error(line_number,
                                   "the step count must be a whole number from 1 to 999999999");
                return false;
            }
            const struct {
                const std::string& text;
                double& value;
                double lo;
                double hi;
                const char* name;
            } numbers[] = {
                {fields[1], s.input.throttle, 0.0, 1.0, "the throttle must be a fixed decimal from 0 to 1"},
                {fields[2], s.input.brake, 0.0, 1.0, "the brake must be a fixed decimal from 0 to 1"},
                {fields[3], s.input.steer, -1.0, 1.0, "the steer must be a fixed decimal from -1 to 1"},
            };
            for (const auto& n : numbers) {
                if (!parse_vehicle_decimal(n.text, n.value) || n.value < n.lo || n.value > n.hi) {
                    error = line_error(line_number, n.name);
                    return false;
                }
            }
            if (fields[4] == "0" || fields[4] == "1") {
                s.input.handbrake = fields[4] == "1";
            } else {
                error = line_error(line_number, "the handbrake must be 0 or 1");
                return false;
            }
            if (fields[5] == "none") {
                s.input.shift = VehicleShift::none;
            } else if (fields[5] == "up") {
                s.input.shift = VehicleShift::up;
            } else if (fields[5] == "down") {
                s.input.shift = VehicleShift::down;
            } else {
                error = line_error(line_number, "the shift must be none, up or down");
                return false;
            }
            if (script.segments.size() >= limits.max_segments) {
                error = line_error(line_number, "more than " + std::to_string(limits.max_segments) +
                                                    " segments");
                return false;
            }
            total += s.steps;
            if (total > limits.max_total_steps) {
                error = line_error(line_number, "more than " +
                                                    std::to_string(limits.max_total_steps) +
                                                    " steps in total");
                return false;
            }
            script.segments.push_back(s);
        }
        if (end == text.size()) {
            break;
        }
    }
    if (script.segments.empty()) {
        error = "the script has no segment";
        return false;
    }
    out = script;
    return true;
}

const std::vector<std::string>& builtin_vehicle_script_names() {
    static const std::vector<std::string> names = {
        "rest", "accelerate", "brake", "corner", "slide", "top-speed", "manual", "mixed"};
    return names;
}

bool builtin_vehicle_script(const std::string& name, VehicleScript& out) {
    VehicleScript s;
    s.name = name;
    std::vector<VehicleScriptSegment>& g = s.segments;
    if (name == "rest") {
        // Two seconds of nothing from rest: the car must stay where reset()
        // put it.
        g.push_back(segment(240, 0.0, 0.0, 0.0));
    } else if (name == "accelerate") {
        // Half a second at rest, then full throttle for thirty seconds in the
        // automatic gearbox.
        g.push_back(segment(60, 0.0, 0.0, 0.0));
        g.push_back(segment(3600, 1.0, 0.0, 0.0));
    } else if (name == "brake") {
        // From 100 km/h, full brake for eight seconds.
        s.speed = 100.0 / 3.6;
        g.push_back(segment(960, 0.0, 1.0, 0.0));
    } else if (name == "corner") {
        // From 20 m/s, half throttle and a constant steer to the left for
        // ten seconds.
        s.speed = 20.0;
        g.push_back(segment(1200, 0.5, 0.0, 0.2));
    } else if (name == "slide") {
        // From 25 m/s: straight for half a second, full left input for
        // three seconds, then the handbrake - max(service, 0.5 x handbrake),
        // 0x00474B4B - with half the input for one second, then straight
        s.speed = 25.0;
        g.push_back(segment(60, 0.3, 0.0, 0.0));
        g.push_back(segment(360, 0.3, 0.0, 1.0));
        g.push_back(segment(120, 0.0, 0.0, 0.5, true));
        g.push_back(segment(240, 0.0, 0.0, 0.0));
    } else if (name == "top-speed") {
        // Two minutes of full throttle from rest.
        g.push_back(segment(14400, 1.0, 0.0, 0.0));
    } else if (name == "manual") {
        // The manual gearbox: first gear into the rev limiter, then up
        // through second and third, then back down to second.
        s.gearbox = VehicleGearbox::manual;
        g.push_back(segment(600, 1.0, 0.0, 0.0));
        g.push_back(segment(480, 1.0, 0.0, 0.0, false, VehicleShift::up));
        g.push_back(segment(480, 1.0, 0.0, 0.0, false, VehicleShift::up));
        g.push_back(segment(240, 0.0, 0.0, 0.0, false, VehicleShift::down));
    } else if (name == "mixed") {
        // The pinned sequence of docs/VEHICLE.md: every control and every
        // regime in twenty seconds.
        g.push_back(segment(120, 0.0, 0.0, 0.0));
        g.push_back(segment(720, 1.0, 0.0, 0.0));
        g.push_back(segment(240, 0.5, 0.0, 0.4));
        g.push_back(segment(240, 0.5, 0.0, -0.4));
        g.push_back(segment(240, 0.0, 0.8, 0.0));
        g.push_back(segment(120, 0.0, 0.0, 0.6, true));
        g.push_back(segment(360, 0.6, 0.0, 0.0));
        g.push_back(segment(360, 0.0, 0.0, 0.0));
    } else {
        return false;
    }
    out = s;
    return true;
}

} // namespace nfsu
