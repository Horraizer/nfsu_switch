#include "nfsu/input.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The recorded form of controller input: include/nfsu/input.h states the
// grammar and docs/INPUT.md the reasons for it. The parser is strict on
// purpose. A recording is what a test pins a camera result to, so a line
// that could be read two ways is refused rather than read one of them, and
// a refusal names the line.

namespace nfsu {

namespace {

constexpr const char* kHeader = "nfsu-input 1";

// The canonical spelling of a number: "0", or a nonzero digit followed by
// digits. No sign, no leading zero, no spaces. Returns false on anything
// else and on a value above `maximum`.
bool parse_unsigned(const std::string& text, std::uint64_t maximum, std::uint64_t& out) {
    if (text.empty() || text.size() > 20) {
        return false;
    }
    if (text.size() > 1 && text[0] == '0') {
        return false;
    }
    std::uint64_t value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        // value * 10 + digit <= maximum, asked without overflowing and
        // without subtracting a digit from a maximum smaller than it.
        if (digit > maximum || value > (maximum - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    out = value;
    return true;
}

// The same with an optional leading '-', inside kAxisMin..kAxisMax. "-0"
// is refused, because it is a second spelling of 0.
bool parse_axis(const std::string& text, std::int32_t& out) {
    const bool negative = !text.empty() && text[0] == '-';
    const std::string digits = negative ? text.substr(1) : text;
    std::uint64_t magnitude = 0;
    const std::uint64_t limit = negative ? static_cast<std::uint64_t>(-std::int64_t{kAxisMin})
                                         : static_cast<std::uint64_t>(kAxisMax);
    if (!parse_unsigned(digits, limit, magnitude)) {
        return false;
    }
    if (negative && magnitude == 0) {
        return false;
    }
    const std::int64_t value = negative ? -static_cast<std::int64_t>(magnitude)
                                        : static_cast<std::int64_t>(magnitude);
    out = static_cast<std::int32_t>(value);
    return true;
}

bool parse_buttons(const std::string& text, std::uint32_t& out, std::string& error) {
    out = 0;
    if (text == "-") {
        return true;
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t plus = text.find('+', start);
        const std::string name =
            text.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        std::size_t found = kGamepadButtonCount;
        for (std::size_t i = 0; i < kGamepadButtonCount; ++i) {
            if (name == gamepad_button_name(static_cast<GamepadButton>(i))) {
                found = i;
                break;
            }
        }
        if (found == kGamepadButtonCount) {
            error = name.empty() ? "an empty button name" : "an unknown button \"" + name + "\"";
            return false;
        }
        const std::uint32_t bit = gamepad_button_bit(static_cast<GamepadButton>(found));
        if ((out & bit) != 0U) {
            error = "button " + name + " named twice";
            return false;
        }
        out |= bit;
        if (plus == std::string::npos) {
            return true;
        }
        start = plus + 1;
    }
}

std::vector<std::string> fields_of(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t index = 0;
    while (index < line.size()) {
        while (index < line.size() && line[index] == ' ') {
            ++index;
        }
        if (index == line.size()) {
            break;
        }
        const std::size_t end = line.find(' ', index);
        const std::size_t stop = end == std::string::npos ? line.size() : end;
        fields.push_back(line.substr(index, stop - index));
        index = stop;
    }
    return fields;
}

std::string buttons_text(std::uint32_t buttons) {
    std::string out;
    for (std::size_t i = 0; i < kGamepadButtonCount; ++i) {
        const GamepadButton button = static_cast<GamepadButton>(i);
        if ((buttons & gamepad_button_bit(button)) == 0U) {
            continue;
        }
        if (!out.empty()) {
            out += '+';
        }
        out += gamepad_button_name(button);
    }
    return out.empty() ? std::string("-") : out;
}

std::int32_t recorded_axis(std::int32_t value) noexcept {
    if (value < kAxisMin) {
        return kAxisMin;
    }
    if (value > kAxisMax) {
        return kAxisMax;
    }
    return value;
}

} // namespace

bool parse_input_recording(
    const std::string& text,
    std::vector<GamepadFrame>& frames,
    std::string& error) {
    if (text.size() > kMaxRecordingBytes) {
        error = "the recording is longer than " + std::to_string(kMaxRecordingBytes) +
                " bytes";
        return false;
    }
    std::vector<GamepadFrame> parsed;
    GamepadFrame current;
    // Whether `current` has been started, and whether it was started by a
    // line that names no slot. The second is what makes a frame either
    // empty or a set of slots, never both.
    bool open = false;
    bool open_empty = false;
    std::size_t line_number = 0;
    std::size_t start = 0;
    bool header_seen = false;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string::npos && start == text.size()) {
            break;
        }
        const std::size_t stop = newline == std::string::npos ? text.size() : newline;
        std::string line = text.substr(start, stop - start);
        start = stop + 1;
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string where = "line " + std::to_string(line_number) + ": ";
        for (std::size_t i = 0; i < line.size(); ++i) {
            const unsigned char byte = static_cast<unsigned char>(line[i]);
            if (byte < 0x20U || byte > 0x7EU) {
                error = where + "byte " + std::to_string(i + 1) +
                        " is outside printable ASCII";
                return false;
            }
        }
        if (!header_seen) {
            if (line != kHeader) {
                error = where + "expected the header \"" + std::string(kHeader) + "\"";
                return false;
            }
            header_seen = true;
            continue;
        }
        const std::vector<std::string> fields = fields_of(line);
        if (fields.empty() || fields[0][0] == '#') {
            continue;
        }
        if (fields.size() != 1 && fields.size() != 7) {
            error = where + "expected a time, or a time and six slot fields, and found " +
                    std::to_string(fields.size()) + " fields";
            return false;
        }
        std::uint64_t time_us = 0;
        if (!parse_unsigned(fields[0], UINT64_MAX, time_us)) {
            error = where + "the time is not a canonical decimal number of microseconds";
            return false;
        }
        const bool names_slot = fields.size() == 7;
        if (open && time_us == current.time_us) {
            if (open_empty) {
                error = where + "the frame at time " + fields[0] +
                        " was already stated with nothing connected";
                return false;
            }
            if (!names_slot) {
                error = where + "a time alone states a frame with nothing connected, and "
                        "the frame at time " + fields[0] + " already names a slot";
                return false;
            }
        } else {
            if (open && time_us < current.time_us) {
                error = where + "time " + fields[0] + " is earlier than the frame before it";
                return false;
            }
            if (open) {
                if (parsed.size() >= kMaxRecordingFrames) {
                    error = where + "the recording holds more than " +
                            std::to_string(kMaxRecordingFrames) + " frames";
                    return false;
                }
                parsed.push_back(current);
            }
            current = GamepadFrame{};
            current.time_us = time_us;
            open = true;
            open_empty = !names_slot;
        }
        if (!names_slot) {
            continue;
        }
        std::uint64_t slot = 0;
        if (!parse_unsigned(fields[1], kMaxGamepads - 1U, slot)) {
            error = where + "the slot must be a number from 0 to " +
                    std::to_string(kMaxGamepads - 1U);
            return false;
        }
        GamepadState& pad = current.pads[slot];
        if (pad.connected) {
            error = where + "slot " + fields[1] + " is named twice in the frame at time " +
                    fields[0];
            return false;
        }
        std::string detail;
        if (!parse_buttons(fields[2], pad.buttons, detail)) {
            error = where + detail;
            return false;
        }
        for (std::size_t axis = 0; axis < kGamepadAxisCount; ++axis) {
            if (!parse_axis(fields[3 + axis], pad.axes[axis])) {
                error = where + "axis " +
                        gamepad_axis_name(static_cast<GamepadAxis>(axis)) +
                        " must be a canonical decimal number from " +
                        std::to_string(kAxisMin) + " to " + std::to_string(kAxisMax);
                return false;
            }
        }
        pad.connected = true;
    }
    if (!header_seen) {
        error = "line 1: expected the header \"" + std::string(kHeader) + "\"";
        return false;
    }
    if (open) {
        if (parsed.size() >= kMaxRecordingFrames) {
            error = "the recording holds more than " + std::to_string(kMaxRecordingFrames) +
                    " frames";
            return false;
        }
        parsed.push_back(current);
    }
    frames.insert(frames.end(), parsed.begin(), parsed.end());
    return true;
}

std::string format_input_recording(const std::vector<GamepadFrame>& frames) {
    std::string out = std::string(kHeader) + "\n";
    for (const GamepadFrame& frame : frames) {
        const std::string time = std::to_string(frame.time_us);
        bool any = false;
        for (std::size_t slot = 0; slot < kMaxGamepads; ++slot) {
            const GamepadState& pad = frame.pads[slot];
            if (!pad.connected) {
                continue;
            }
            any = true;
            out += time;
            out += ' ';
            out += std::to_string(slot);
            out += ' ';
            out += buttons_text(pad.buttons & kGamepadButtonMask);
            for (std::size_t axis = 0; axis < kGamepadAxisCount; ++axis) {
                out += ' ';
                out += std::to_string(recorded_axis(pad.axes[axis]));
            }
            out += '\n';
        }
        if (!any) {
            out += time;
            out += '\n';
        }
    }
    return out;
}

} // namespace nfsu
