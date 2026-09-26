#pragma once

#include "nfsu/input.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// The services a platform supplies to this project, and nothing else it
// supplies. docs/PLATFORM_SERVICES.md is the contract: who owns what, which
// thread calls what, what each call does when it fails, what the desktop
// implements and what a libnx implementation would have to.
//
// Six interfaces, each cut to what a caller in this repository actually
// asks of it: filesystem, input, timing, logging, lifecycle and display.
// None of them names a platform type. Three rules hold for all six:
//
// - One thread. Every call is made from the thread that runs the session
//   (the window thread on the desktop, the main thread on a console) and
//   no implementation calls back into the caller from any other thread.
// - Nothing throws. A failure is a return value, or for the calls that
//   cannot fail by contract, a documented degraded result. Only an
//   allocation failure inside the standard library can escape, as it can
//   from any other function here.
// - The caller owns the service. Whoever constructs an implementation
//   keeps it alive for as long as anything holds a reference to it; the
//   code that uses a service stores a reference and never deletes it.

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------

// What the programs need from a filesystem beyond what the C++ standard
// library already gives every platform: turning a command-line argument into
// a path it can open, and - since issue #47 - three read-only calls the
// asset check (include/nfsu/assets.h) makes to find and verify the retail
// files a feature needs: a bounded directory listing, a file's size, and a
// bounded read of a byte range. Reading a file stays std::ifstream in the
// parsers, which every supported toolchain provides; see
// docs/PLATFORM_SERVICES.md for what that leaves the platform to supply.
//
// Every call below is read-only. None creates, writes, renames, removes or
// locks anything, and a reason is always a printable ASCII sentence that
// does not repeat the path: the caller names the path.
class FileSystemService {
public:
    virtual ~FileSystemService() = default;

    // Converts one argument. Returns false, leaving `out` untouched, when the
    // argument cannot name a file on this platform, and sets `reason` to a
    // printable ASCII sentence that names the argument and says what is
    // wrong with it. A null `text` is refused. The desktop implementation is
    // src/arg_path.cpp (docs/PATH_ARGUMENTS.md).
    virtual bool argument_to_path(
        const char* text, std::filesystem::path& out, std::string& reason) = 0;

    // The names directly inside `directory`, in the order the platform
    // lists them, without "." and "..": each the bytes the file system
    // stores, with no directory part. Returns false, with `names` emptied
    // and `reason` set, when the directory cannot be listed - "does not
    // exist", "is not a directory", or a failure to read it - and when it
    // holds more than `max_names` names, which is refused rather than
    // truncated so that no caller ever matches against half a listing.
    virtual bool list_directory(
        const std::filesystem::path& directory,
        std::size_t max_names,
        std::vector<std::string>& names,
        std::string& reason) = 0;

    // The size in bytes of the regular file at `path`. False, with `size`
    // untouched and `reason` set, when there is nothing there, when it is a
    // directory or anything else that is not a regular file, or when it
    // cannot be sized.
    virtual bool file_size(
        const std::filesystem::path& path, std::uint64_t& size, std::string& reason) = 0;

    // Reads bytes offset .. offset + count - 1 of the file at `path` into
    // `buffer`, which holds at least `count` bytes (it may be null when
    // `count` is 0), and sets `got` to the number read: fewer than `count`
    // only where the file ends first, and 0 for an offset at or past its
    // end. The file is opened for reading only and closed again before the
    // call returns. False, with `got` 0 and `reason` set, when the file
    // cannot be opened, positioned or read.
    virtual bool read_file(
        const std::filesystem::path& path,
        std::uint64_t offset,
        unsigned char* buffer,
        std::size_t count,
        std::size_t& got,
        std::string& reason) = 0;
};

// Checks arguments `first` through `argc - 1` through `files`, in order, and
// stops at the first it refuses: `reason` is then "argument <k> " followed
// by the service's sentence. A null argv accepts, since there is nothing to
// check. `first` is at least 0; argv holds argc entries. This is the loop
// nfsu::check_arguments ran before it sat behind the service, moved here
// unchanged so that the desktop and a console share it.
bool check_argument_paths(
    FileSystemService& files, int argc, char** argv, int first, std::string& reason);

// The path of an argument the service accepts, and an empty path, which
// names no file and fails at the first open, for one it refuses.
std::filesystem::path argument_path_or_empty(FileSystemService& files, const char* text);

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

enum class PointerButton : std::uint8_t {
    primary,
    secondary,
    middle
};

// The keys the inspection session gives a meaning to (docs/INSPECTOR.md).
// A backend maps its own key codes onto these and reports any other key as
// Key::other, which the session ignores.
enum class Key : std::uint8_t {
    other,
    digit_1,
    digit_2,
    digit_3,
    digit_4,
    digit_5,
    b,
    c,
    f,
    g,
    h,
    m,
    n,
    o,
    p,
    q,
    r,
    s,
    t,
    u,
    v,
    x,
    escape
};

// One wheel notch, in the units InputEvent::wheel is reported in. It is
// the unit Windows reports, so the desktop backend passes its value
// through; a backend whose wheel reports whole notches multiplies.
constexpr std::int32_t kWheelNotch = 120;

enum class InputEventType : std::uint8_t {
    // The drawable surface is now x by y pixels. Reported through input
    // because it is ordered with the pointer: a move that follows a resize
    // has to be converted with the new size.
    resize,
    // `button` went down at x, y.
    pointer_down,
    // A pointer button went up at x, y. Which one is not reported, because
    // the session ends whatever drag is running on any release.
    pointer_up,
    // The pointer is at x, y.
    pointer_move,
    // The platform took the pointer away mid-drag - focus lost, capture
    // taken by another window. The drag ends without a click.
    pointer_cancel,
    // The wheel turned by `wheel`, positive away from the user.
    wheel,
    // `key` went down, including when a held key repeats.
    key_down
};

struct InputEvent {
    InputEventType type = InputEventType::pointer_move;
    PointerButton button = PointerButton::primary;
    // Surface pixels from the top left for a pointer event; the new width
    // and height for resize.
    std::int32_t x = 0;
    std::int32_t y = 0;
    // 1 / kWheelNotch of a notch per unit.
    std::int32_t wheel = 0;
    Key key = Key::other;
};

InputEvent resize_event(std::int32_t width, std::int32_t height) noexcept;
InputEvent pointer_down_event(PointerButton button, std::int32_t x, std::int32_t y) noexcept;
InputEvent pointer_up_event(std::int32_t x, std::int32_t y) noexcept;
InputEvent pointer_move_event(std::int32_t x, std::int32_t y) noexcept;
InputEvent pointer_cancel_event() noexcept;
InputEvent wheel_event(std::int32_t wheel) noexcept;
InputEvent key_event(Key key) noexcept;

class InputService {
public:
    virtual ~InputService() = default;

    // Takes the oldest event not yet taken. False when none is queued; it
    // never waits for one.
    virtual bool next_event(InputEvent& event) = 0;

    // Asks for every pointer event until it is given back, including those
    // outside the surface (true), or gives it back (false). Best effort: a
    // platform that cannot capture, or that has nothing to capture, as a
    // touch screen does not, does nothing. Losing a capture it was granted
    // is reported as a pointer_cancel event.
    virtual void set_pointer_capture(bool captured) = 0;

    // Fills every slot of frame.pads with the controllers' state now: a
    // slot with no controller, or one that could not be read, is left
    // disconnected. frame.time_us is left alone; the caller stamps it from
    // TimingService, so that one clock times every sample.
    virtual void sample_gamepads(GamepadFrame& frame) = 0;
};

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

class TimingService {
public:
    virtual ~TimingService() = default;

    // Microseconds on a monotonic clock with an arbitrary origin. Never
    // smaller than a value it returned before; cannot fail.
    virtual std::uint64_t now_us() = 0;
};

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

enum class LogLevel : std::uint8_t {
    info,
    warning,
    error
};

// "info", "warning" or "error".
const char* log_level_name(LogLevel level) noexcept;

class LoggingService {
public:
    virtual ~LoggingService() = default;

    // Records one line of diagnostic text. Best effort: it cannot fail, and
    // a line that cannot be recorded is lost. The implementation keeps the
    // record ASCII by escaping every byte outside printable ASCII, so a
    // caller may pass raw bytes, such as a file name, as they are.
    virtual void write(LogLevel level, const std::string& line) = 0;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

class LifecycleService {
public:
    virtual ~LifecycleService() = default;

    // Lets the platform do its own work - wait for and dispatch a window
    // message, service the system applet - and reports whether the session
    // may go on. False once it must stop; it stays false after that. It may
    // block while there is nothing to do.
    virtual bool pump() = 0;

    // Asks to end the session the way a user closing it would. pump()
    // returns false once the platform has acted on it, which need not be
    // the next call.
    virtual void request_exit() = 0;
};

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

// One frame to show: width * height pixels of B, G, R, 255, rows top down.
// The pixels belong to the caller and are read only during draw_frame.
struct DisplayImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const unsigned char* bgra = nullptr;
};

class DisplayService {
public:
    virtual ~DisplayService() = default;

    // The drawable surface, in pixels. 0 by 0 while there is none, for
    // example while a window is minimized.
    virtual void surface_size(int& width, int& height) = 0;

    // Starts presenting one frame and reports the surface size at that
    // moment. False when nothing can be presented now; the implementation
    // has then already cleaned up after itself, and the caller does not
    // call end_frame.
    virtual bool begin_frame(int& width, int& height) = 0;

    // Shows `image` stretched over the whole surface, nearest neighbour,
    // with the overlay lines drawn on top from the top left in a box sized
    // to them. At most once between begin_frame and end_frame. Lines are
    // printable ASCII. Best effort: a surface that cannot be drawn to shows
    // what it showed before.
    virtual void draw_frame(
        const DisplayImage& image, const std::vector<std::string>& overlay) = 0;

    // Finishes the frame begin_frame started. Called whether or not a
    // frame was drawn.
    virtual void end_frame() = 0;

    // Asks for a frame to be presented soon. It never presents one itself.
    virtual void request_redraw() = 0;
};

} // namespace nfsu
