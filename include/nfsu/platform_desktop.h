#pragma once

#include "nfsu/platform.h"

#include <cstdint>
#include <iosfwd>
#include <string>

namespace nfsu {

// The desktop implementations of the services that are not a window. The
// window's own three - display, input and lifecycle - are the Win32 backend
// in src/inspect_window_win32.cpp, and the filesystem is the argument
// conversion in src/arg_path.cpp, reached through desktop_file_system() in
// include/nfsu/arg_path.h. docs/PLATFORM_SERVICES.md lists all six.
//
// This is platform code, and the one library of the six service families
// that reads a clock: tests/platform_neutrality.cmake classifies it as a
// platform library so that no neutral library can do the same.

// std::chrono::steady_clock, in microseconds. No caller in the programs
// uses it yet; the controller path is timed by whichever TimingService its
// loop is handed.
class DesktopTiming final : public TimingService {
public:
    std::uint64_t now_us() override;
};

// "<level>: <line>" and a newline, flushed, on a stream the caller owns
// and keeps alive: std::cerr for a program. Every byte of the line outside
// printable ASCII is written as \xHH, a newline included, so one call is
// one line of ASCII whatever it was given. No caller uses it yet.
class DesktopLogging final : public LoggingService {
public:
    explicit DesktopLogging(std::ostream& stream) noexcept : stream_(stream) {}
    void write(LogLevel level, const std::string& line) override;

private:
    std::ostream& stream_;
};

} // namespace nfsu
