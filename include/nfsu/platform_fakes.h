#pragma once

#include "nfsu/platform.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace nfsu {

// Deterministic implementations of the six services in include/nfsu/platform.h,
// for tests. Each one does exactly what the test scripts and records what it
// was asked, so that a test states its input once and asserts on what the
// code under test did with it. None reads a clock, the environment or any
// file, so a test that drives them gives the same result on every run and
// every machine. docs/PLATFORM_SERVICES.md lists them beside the desktop
// implementations.

// Accepts every argument as the path of its own bytes, except the ones a
// test told it to refuse.
class FakeFileSystem final : public FileSystemService {
public:
    // Refuses `argument` from now on, with `reason` as the sentence.
    void refuse(const std::string& argument, const std::string& reason);

    bool argument_to_path(
        const char* text, std::filesystem::path& out, std::string& reason) override;

    // Every argument asked about, in order; "(null)" for a null one.
    const std::vector<std::string>& asked() const noexcept { return asked_; }

private:
    std::vector<std::pair<std::string, std::string>> refusals_;
    std::vector<std::string> asked_;
};

class FakeInput final : public InputService {
public:
    // Queues an event for next_event.
    void push_event(const InputEvent& event);
    // Queues the pads one sample_gamepads call reports. Its time is ignored:
    // the caller stamps samples from TimingService.
    void push_gamepads(const GamepadFrame& frame);

    bool next_event(InputEvent& event) override;
    void set_pointer_capture(bool captured) override;
    // The next queued pads, or nothing connected once the queue is empty.
    void sample_gamepads(GamepadFrame& frame) override;

    // Whether the last capture call asked for the pointer, and every call.
    bool captured() const noexcept { return captured_; }
    const std::vector<bool>& capture_calls() const noexcept { return capture_calls_; }
    std::size_t pending_events() const noexcept { return events_.size(); }
    std::size_t samples() const noexcept { return samples_; }

private:
    std::deque<InputEvent> events_;
    std::deque<GamepadFrame> gamepads_;
    std::vector<bool> capture_calls_;
    bool captured_ = false;
    std::size_t samples_ = 0;
};

// A clock that moves only when told to.
class FakeTiming final : public TimingService {
public:
    // Reads start at `start_us` and advance by `step_us` after each one.
    explicit FakeTiming(std::uint64_t start_us = 0, std::uint64_t step_us = 0) noexcept;

    // Queues the time one read returns; stepping resumes from it afterwards.
    // A queued time earlier than a time already returned is raised to that
    // time, so the fake keeps the monotonic promise of the contract even
    // when a test does not. A test of what a consumer does with a clock
    // going backwards feeds the consumer directly instead.
    void push_time(std::uint64_t time_us);

    std::uint64_t now_us() override;

    std::size_t reads() const noexcept { return reads_; }

private:
    std::deque<std::uint64_t> queued_;
    std::uint64_t next_us_ = 0;
    std::uint64_t step_us_ = 0;
    std::uint64_t last_us_ = 0;
    std::size_t reads_ = 0;
};

// Records every line as it was passed, "<level>: <line>", without escaping
// it, so that a test sees exactly what a caller sent.
class FakeLogging final : public LoggingService {
public:
    void write(LogLevel level, const std::string& line) override;
    const std::vector<std::string>& lines() const noexcept { return lines_; }

private:
    std::vector<std::string> lines_;
};

class FakeLifecycle final : public LifecycleService {
public:
    // pump() answers true `frames` times and false from then on, and false
    // from the first call after an exit request.
    explicit FakeLifecycle(std::size_t frames) noexcept;

    // Called at the start of every pump that answers true, with its number
    // from 0, so that a test can deliver the input of frame k on frame k.
    void on_pump(std::function<void(std::size_t)> hook);

    bool pump() override;
    void request_exit() override;

    // Pumps that answered true, and exit requests.
    std::size_t pumps() const noexcept { return pumps_; }
    std::size_t exit_requests() const noexcept { return exit_requests_; }

private:
    std::size_t budget_ = 0;
    std::size_t pumps_ = 0;
    std::size_t exit_requests_ = 0;
    bool stopped_ = false;
    std::function<void(std::size_t)> hook_;
};

class FakeDisplay final : public DisplayService {
public:
    FakeDisplay(int width, int height) noexcept;

    // What surface_size and begin_frame report from now on.
    void set_surface(int width, int height) noexcept;
    // The next `count` begin_frame calls answer false, as a BeginPaint that
    // fails does.
    void refuse_frames(std::size_t count) noexcept;

    void surface_size(int& width, int& height) override;
    bool begin_frame(int& width, int& height) override;
    void draw_frame(const DisplayImage& image, const std::vector<std::string>& overlay) override;
    void end_frame() override;
    void request_redraw() override;

    // One drawn frame: the surface it was drawn on and a copy of what it
    // was given.
    struct Presented {
        int surface_width = 0;
        int surface_height = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<unsigned char> bgra;
        std::vector<std::string> overlay;
    };

    // Frames drawn, in order. A frame begun and ended with nothing drawn in
    // it is counted by frames_ended and is not here.
    const std::vector<Presented>& presented() const noexcept { return presented_; }
    std::size_t frames_begun() const noexcept { return begun_; }
    std::size_t frames_ended() const noexcept { return ended_; }
    std::size_t redraw_requests() const noexcept { return redraw_requests_; }
    // Calls that broke the contract: draw_frame or end_frame with no frame
    // begun, a second draw_frame in one frame, a begin_frame inside one,
    // and an image whose pixels are null or whose side is 0.
    std::size_t protocol_errors() const noexcept { return protocol_errors_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::size_t refusals_ = 0;
    bool in_frame_ = false;
    bool drawn_ = false;
    std::vector<Presented> presented_;
    std::size_t begun_ = 0;
    std::size_t ended_ = 0;
    std::size_t redraw_requests_ = 0;
    std::size_t protocol_errors_ = 0;
};

} // namespace nfsu
