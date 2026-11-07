#include "nfsu/platform.h"

#include <filesystem>
#include <string>

// The code that is written against the service interfaces and nothing
// else: it runs unchanged on every platform, because it only ever reaches
// the platform through a service it was handed. include/nfsu/platform.h is
// the contract.

namespace nfsu {

bool check_argument_paths(
    FileSystemService& files, int argc, char** argv, int first, std::string& reason) {
    if (argv == nullptr) {
        return true;
    }
    for (int i = first; i < argc; ++i) {
        std::filesystem::path ignored;
        std::string detail;
        if (!files.argument_to_path(argv[i], ignored, detail)) {
            reason = "argument " + std::to_string(i) + " " + detail;
            return false;
        }
    }
    return true;
}

std::filesystem::path argument_path_or_empty(FileSystemService& files, const char* text) {
    std::filesystem::path path;
    std::string ignored;
    if (!files.argument_to_path(text, path, ignored)) {
        return std::filesystem::path();
    }
    return path;
}

InputEvent resize_event(std::int32_t width, std::int32_t height) noexcept {
    InputEvent event;
    event.type = InputEventType::resize;
    event.x = width;
    event.y = height;
    return event;
}

InputEvent pointer_down_event(PointerButton button, std::int32_t x, std::int32_t y) noexcept {
    InputEvent event;
    event.type = InputEventType::pointer_down;
    event.button = button;
    event.x = x;
    event.y = y;
    return event;
}

InputEvent pointer_up_event(std::int32_t x, std::int32_t y) noexcept {
    InputEvent event;
    event.type = InputEventType::pointer_up;
    event.x = x;
    event.y = y;
    return event;
}

InputEvent pointer_move_event(std::int32_t x, std::int32_t y) noexcept {
    InputEvent event;
    event.type = InputEventType::pointer_move;
    event.x = x;
    event.y = y;
    return event;
}

InputEvent pointer_cancel_event() noexcept {
    InputEvent event;
    event.type = InputEventType::pointer_cancel;
    return event;
}

InputEvent wheel_event(std::int32_t wheel) noexcept {
    InputEvent event;
    event.type = InputEventType::wheel;
    event.wheel = wheel;
    return event;
}

InputEvent key_event(Key key) noexcept {
    InputEvent event;
    event.type = InputEventType::key_down;
    event.key = key;
    return event;
}

const char* log_level_name(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::info:
        return "info";
    case LogLevel::warning:
        return "warning";
    case LogLevel::error:
        return "error";
    }
    return "error";
}

} // namespace nfsu
