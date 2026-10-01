#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Controller input, independent of any controller API. See docs/INPUT.md.
//
// A backend reports what every controller slot holds, once per frame, as a
// GamepadFrame. Everything after that - the dead zones, the remapping
// table, what a connect or a disconnect does, which action fires on which
// frame - is decided here from the frames alone. Nothing in this layer reads
// a clock: a frame carries its own time, stamped by whoever sampled it. A
// recorded sequence of frames therefore replays to the same actions, and
// through them to the same camera, every time it is replayed, and the
// recorded text form below is how a test states one.

// The buttons every supported controller has, named by where they sit
// rather than by the letter printed on them, because controller families
// print different letters in the same place. docs/INPUT.md maps them.
enum class GamepadButton : std::uint8_t {
    face_south,
    face_east,
    face_west,
    face_north,
    shoulder_left,
    shoulder_right,
    trigger_left,
    trigger_right,
    start,
    select,
    stick_left,
    stick_right,
    dpad_up,
    dpad_down,
    dpad_left,
    dpad_right
};

constexpr std::size_t kGamepadButtonCount = 16;

// Every bit a GamepadButton can occupy. A bit outside it is not a button:
// it is ignored when the frame is read and dropped when it is recorded.
constexpr std::uint32_t kGamepadButtonMask = 0xFFFFU;

constexpr std::uint32_t gamepad_button_bit(GamepadButton button) noexcept {
    return 1U << static_cast<unsigned>(button);
}

// A stable lowercase ASCII name, "face_south", used by the recorded form.
const char* gamepad_button_name(GamepadButton button) noexcept;

// The two sticks, one axis each. x is positive to the right and y positive
// away from the player, the way both HID and the Switch report a stick.
enum class GamepadAxis : std::uint8_t {
    left_x,
    left_y,
    right_x,
    right_y
};

constexpr std::size_t kGamepadAxisCount = 4;

const char* gamepad_axis_name(GamepadAxis axis) noexcept;

// Raw stick values. kAxisMax is full deflection. kAxisMin is accepted
// because a signed 16-bit source can report it, and it is read as
// -kAxisMax so that the two directions are the same length. A live value
// outside the range is clamped into it; the recorded form refuses one.
constexpr std::int32_t kAxisMax = 32767;
constexpr std::int32_t kAxisMin = -32768;

// One controller slot. A slot that is not connected contributes nothing,
// whatever its other fields hold.
struct GamepadState {
    bool connected = false;
    // gamepad_button_bit of every button held.
    std::uint32_t buttons = 0;
    std::int32_t axes[kGamepadAxisCount] = {0, 0, 0, 0};
};

// Slots a frame carries. Eight players is the most any supported platform
// reports; a backend fills the ones it has and leaves the rest
// disconnected.
constexpr std::size_t kMaxGamepads = 8;

// Every slot, sampled at one time.
struct GamepadFrame {
    // Microseconds on a monotonic clock whose origin is arbitrary. Only the
    // difference between two frames is ever used.
    std::uint64_t time_us = 0;
    GamepadState pads[kMaxGamepads];
    // Optional host polling metadata for diagnostics. Replays leave it
    // unavailable; controls and legacy encoders read time_us only.
    bool sample_clock_available = false;
    std::uint64_t sample_time_us = 0;
    std::uint64_t display_frame = 0;
};

// ---------------------------------------------------------------------------
// Dead zones
// ---------------------------------------------------------------------------

// A radial dead zone for one stick, in raw units. At or inside `inner` the
// stick reads as centred, exactly. At or beyond `outer` it reads as fully
// deflected, with its direction kept. In between, the length is rescaled
// linearly from inner..outer onto 0..1 and the direction kept, so the
// output is continuous across both edges. Radial rather than per axis,
// because a per-axis zone snaps a diagonal onto the nearer axis.
struct StickDeadZone {
    std::int32_t inner = 3000;
    std::int32_t outer = 30000;
};

// Checks 0 <= inner < outer <= kAxisMax.
bool validate_stick_dead_zone(const StickDeadZone& zone, std::string& error);

// A stick after its dead zone: each component in -1..1 and the length at
// most 1.
struct StickVector {
    double x = 0.0;
    double y = 0.0;
};

// Applies the zone to one raw stick position. Both edges are decided by an
// exact integer comparison of the squared length, never by a rounded one,
// so a position exactly on `inner` is centred on every platform. The zone
// must have validated.
StickVector apply_stick_dead_zone(
    const StickDeadZone& zone, std::int32_t x, std::int32_t y) noexcept;

// ---------------------------------------------------------------------------
// The remapping table
// ---------------------------------------------------------------------------

// What a binding reads: one button, or one axis of one stick.
struct GamepadControl {
    enum class Kind : std::uint8_t { button, axis };
    Kind kind = Kind::button;
    // A GamepadButton or a GamepadAxis value, according to kind.
    std::uint8_t index = 0;
};

GamepadControl gamepad_control(GamepadButton button) noexcept;
GamepadControl gamepad_control(GamepadAxis axis) noexcept;

// How an action is read. The table below binds controls to action ids; the
// set of actions and their kinds belongs to whoever consumes them, the
// inspection camera today (include/nfsu/input_camera.h) and driving
// controls later.
enum class ActionKind : std::uint8_t {
    // A continuous value in -1..1: a stick axis, or a button read as 1
    // while it is held.
    analog,
    // On or off, with the frame it came on reported: a button.
    digital
};

struct InputBinding {
    GamepadControl control;
    std::uint16_t action = 0;
    // Reads an axis the other way round, or makes a button count as -1
    // instead of 1. Meaningless for a digital action, and refused there.
    bool invert = false;
};

constexpr std::size_t kMaxInputBindings = 64;
constexpr std::size_t kMaxInputActions = 256;

// Checks a remapping table against the actions it drives. Refused: no
// actions or more than kMaxInputActions; more than kMaxInputBindings
// bindings; an action id past the end of `kinds`; a control that is not a
// button or an axis; a control named by two bindings, because one control
// driving two actions is a remapping mistake rather than a feature; an
// axis bound to a digital action, because a stick has no press; and an
// inverted digital action. The error names the offending binding by its
// position in the table.
bool validate_input_bindings(
    const std::vector<ActionKind>& kinds,
    const std::vector<InputBinding>& bindings,
    std::string& error);

// One action after one frame.
struct ActionState {
    // Analog: every source added up, then clamped to -1..1. Digital: 1
    // while held, 0 otherwise.
    double value = 0.0;
    // Digital only: held on this frame, and held on this frame but not on
    // the one before.
    bool held = false;
    bool pressed = false;
};

// The interval a frame may report. A frame that arrives after a stall - a
// suspended application, a debugger, a dropped connection - moves no
// further than a frame at 10 frames per second would, however long the
// stall was.
constexpr std::uint64_t kMaxFrameIntervalUs = 100000;

// Turns frames into actions. The rules, which docs/INPUT.md states in
// full:
//
// - A disconnected slot contributes nothing, from the frame it disconnects
//   on. A gesture it was making ends there: its sticks stop and its held
//   buttons are released without firing anything.
// - A button that is already down when its slot connects counts as up
//   until it has been seen up. A slot that reconnects mid-press, or a
//   button still held from launching the application, fires nothing.
// - Analog sources add up, slot by slot in ascending order and within a
//   slot in table order, and the sum is clamped to -1..1: two sources
//   pushing opposite ways cancel exactly, and two pushing the same way
//   are no faster than one at full deflection.
// - A digital action is held while any source of it is held, on any slot,
//   and pressed on the frame that changes from not held to held.
// - The interval is the difference from the previous frame's time,
//   clamped to kMaxFrameIntervalUs; the first frame, and a frame whose
//   time is earlier than the previous one, report 0.
class InputMapper {
public:
    // Replaces the actions, the table and the dead zone, and forgets every
    // earlier frame. Refused, with nothing changed, as validate_input_bindings
    // and validate_stick_dead_zone refuse.
    bool configure(
        const std::vector<ActionKind>& kinds,
        const std::vector<InputBinding>& bindings,
        const StickDeadZone& zone,
        std::string& error);

    // Reads one frame. Does nothing before configure has succeeded.
    void update(const GamepadFrame& frame);

    // One entry per action id, as of the last update.
    const std::vector<ActionState>& actions() const noexcept { return actions_; }

    // Microseconds since the previous frame, as of the last update.
    std::uint64_t interval_us() const noexcept { return interval_us_; }

    // Forgets every earlier frame: the next update is a first frame, with
    // every connected slot connecting on it. The configuration is kept.
    void reset() noexcept;

private:
    std::vector<ActionKind> kinds_;
    std::vector<InputBinding> bindings_;
    StickDeadZone zone_;
    std::vector<ActionState> actions_;
    std::vector<double> sums_;
    bool have_previous_ = false;
    std::uint64_t previous_time_us_ = 0;
    std::uint64_t interval_us_ = 0;
    bool connected_[kMaxGamepads] = {};
    // Buttons that were down when their slot connected and have not been
    // seen up since.
    std::uint32_t latched_[kMaxGamepads] = {};
};

// ---------------------------------------------------------------------------
// The recorded form
// ---------------------------------------------------------------------------

// A sequence of frames as ASCII text, one line per connected slot per
// frame, so that a test can state its input by hand and a session can be
// written down and replayed:
//
//   nfsu-input 1
//   # a comment
//   0
//   16000 0 - 0 0 0 0
//   32000 0 face_east+trigger_right 0 0 32767 -120
//   32000 1 - 0 0 0 0
//
// The first line is the header. Then every line is a time in microseconds,
// optionally followed by one connected slot: its number, its held buttons
// as names joined by '+' or '-' for none, and its four axes in GamepadAxis
// order. Consecutive lines with one time are one frame, and a slot that a
// frame does not name is disconnected in it, so a line with a time alone
// is a frame in which nothing is connected. Blank lines and lines starting
// with '#' are ignored; lines end in LF or CR LF.
//
// Refused, with the line number: a missing or different header; a byte
// outside printable ASCII; a field that is not a canonical decimal number
// in range, "-" or a known button name; a button named twice; a slot named
// twice in one frame; a time-only line in a frame that also names a slot;
// a time earlier than the one before; more than kMaxRecordingFrames frames;
// and a text longer than kMaxRecordingBytes. Nothing is appended to frames
// unless the whole text parses.
constexpr std::size_t kMaxRecordingBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaxRecordingFrames = 1000000;

bool parse_input_recording(
    const std::string& text,
    std::vector<GamepadFrame>& frames,
    std::string& error);

// The canonical text of a sequence of frames: LF line ends, one space
// between fields, slots in ascending order, buttons in GamepadButton order.
// It records what update() reads, not what a backend happened to fill in:
// a disconnected slot is left out whatever its fields hold, bits outside
// kGamepadButtonMask are dropped, and axes are clamped into
// kAxisMin..kAxisMax. Parsing the result gives frames that update() reads
// identically to the originals. Frames whose times go backwards produce
// text that parse_input_recording refuses.
std::string format_input_recording(const std::vector<GamepadFrame>& frames);

} // namespace nfsu
