#include "nfsu/platform_fakes.h"

#include <utility>

// The test implementations of the platform services. include/nfsu/platform_fakes.h
// says what each one promises.

namespace nfsu {

void FakeFileSystem::refuse(const std::string& argument, const std::string& reason) {
    refusals_.emplace_back(argument, reason);
}

bool FakeFileSystem::argument_to_path(
    const char* text, std::filesystem::path& out, std::string& reason) {
    if (text == nullptr) {
        asked_.push_back("(null)");
        reason = "is missing";
        return false;
    }
    const std::string argument(text);
    asked_.push_back(argument);
    for (const auto& refusal : refusals_) {
        if (refusal.first == argument) {
            reason = refusal.second;
            return false;
        }
    }
    out = std::filesystem::path(argument);
    return true;
}

void FakeInput::push_event(const InputEvent& event) { events_.push_back(event); }

void FakeInput::push_gamepads(const GamepadFrame& frame) { gamepads_.push_back(frame); }

bool FakeInput::next_event(InputEvent& event) {
    if (events_.empty()) {
        return false;
    }
    event = events_.front();
    events_.pop_front();
    return true;
}

void FakeInput::set_pointer_capture(bool captured) {
    capture_calls_.push_back(captured);
    captured_ = captured;
}

void FakeInput::sample_gamepads(GamepadFrame& frame) {
    ++samples_;
    for (GamepadState& pad : frame.pads) {
        pad = GamepadState{};
    }
    if (gamepads_.empty()) {
        return;
    }
    for (std::size_t slot = 0; slot < kMaxGamepads; ++slot) {
        frame.pads[slot] = gamepads_.front().pads[slot];
    }
    gamepads_.pop_front();
}

FakeTiming::FakeTiming(std::uint64_t start_us, std::uint64_t step_us) noexcept
    : next_us_(start_us), step_us_(step_us) {}

void FakeTiming::push_time(std::uint64_t time_us) { queued_.push_back(time_us); }

std::uint64_t FakeTiming::now_us() {
    std::uint64_t value = next_us_;
    if (!queued_.empty()) {
        value = queued_.front();
        queued_.pop_front();
    }
    if (reads_ > 0 && value < last_us_) {
        value = last_us_;
    }
    ++reads_;
    last_us_ = value;
    next_us_ = value + step_us_;
    return value;
}

void FakeLogging::write(LogLevel level, const std::string& line) {
    lines_.push_back(std::string(log_level_name(level)) + ": " + line);
}

FakeLifecycle::FakeLifecycle(std::size_t frames) noexcept : budget_(frames) {}

void FakeLifecycle::on_pump(std::function<void(std::size_t)> hook) { hook_ = std::move(hook); }

bool FakeLifecycle::pump() {
    if (stopped_ || pumps_ >= budget_) {
        stopped_ = true;
        return false;
    }
    const std::size_t frame = pumps_;
    ++pumps_;
    if (hook_) {
        hook_(frame);
    }
    return true;
}

void FakeLifecycle::request_exit() {
    ++exit_requests_;
    stopped_ = true;
}

FakeDisplay::FakeDisplay(int width, int height) noexcept : width_(width), height_(height) {}

void FakeDisplay::set_surface(int width, int height) noexcept {
    width_ = width;
    height_ = height;
}

void FakeDisplay::refuse_frames(std::size_t count) noexcept { refusals_ = count; }

void FakeDisplay::surface_size(int& width, int& height) {
    width = width_;
    height = height_;
}

bool FakeDisplay::begin_frame(int& width, int& height) {
    if (in_frame_) {
        ++protocol_errors_;
    }
    if (refusals_ > 0) {
        --refusals_;
        return false;
    }
    ++begun_;
    in_frame_ = true;
    drawn_ = false;
    width = width_;
    height = height_;
    return true;
}

void FakeDisplay::draw_frame(const DisplayImage& image, const std::vector<std::string>& overlay) {
    if (!in_frame_ || drawn_ || image.bgra == nullptr || image.width == 0 ||
        image.height == 0) {
        ++protocol_errors_;
        return;
    }
    drawn_ = true;
    Presented frame;
    frame.surface_width = width_;
    frame.surface_height = height_;
    frame.width = image.width;
    frame.height = image.height;
    const std::size_t bytes = static_cast<std::size_t>(image.width) * image.height * 4U;
    frame.bgra.assign(image.bgra, image.bgra + bytes);
    frame.overlay = overlay;
    presented_.push_back(std::move(frame));
}

void FakeDisplay::end_frame() {
    if (!in_frame_) {
        ++protocol_errors_;
        return;
    }
    in_frame_ = false;
    ++ended_;
}

void FakeDisplay::request_redraw() { ++redraw_requests_; }

} // namespace nfsu
