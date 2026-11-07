#include "nfsu/platform_desktop.h"

#include <chrono>
#include <ostream>
#include <string>

// The desktop clock and log. include/nfsu/platform_desktop.h says what each
// promises; both are ordinary standard C++, and they are platform code only
// in the sense that matters here: they read the machine's clock and write
// to the machine's console, which is exactly what a neutral library must
// never do for itself.

namespace nfsu {

std::uint64_t DesktopTiming::now_us() {
    const auto since = std::chrono::steady_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(since).count();
    // steady_clock never goes backwards, and its epoch is in the past on
    // every platform this builds for; a negative count is refused anyway,
    // so the unsigned result cannot wrap.
    return micros < 0 ? 0U : static_cast<std::uint64_t>(micros);
}

void DesktopLogging::write(LogLevel level, const std::string& line) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out = log_level_name(level);
    out += ": ";
    for (const char ch : line) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte >= 0x20U && byte <= 0x7EU) {
            out += ch;
        } else {
            out += "\\x";
            out += kHex[byte >> 4U];
            out += kHex[byte & 0x0FU];
        }
    }
    out += '\n';
    stream_ << out;
    stream_.flush();
}

} // namespace nfsu
