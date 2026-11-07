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

std::string FakeFileSystem::tree_name(const std::string& path) {
    std::string name;
    std::string component;
    const auto flush = [&]() {
        if (!component.empty()) {
            if (!name.empty()) {
                name += '/';
            }
            name += component;
            component.clear();
        }
    };
    for (const char c : path) {
        if (c == '/') {
            flush();
        } else {
            component += c;
        }
    }
    flush();
    return name;
}

void FakeFileSystem::add_parents(const std::string& name) {
    std::size_t slash = name.find('/');
    while (slash != std::string::npos) {
        nodes_[name.substr(0, slash)].directory = true;
        slash = name.find('/', slash + 1);
    }
}

void FakeFileSystem::add_directory(const std::string& path) {
    const std::string name = tree_name(path);
    add_parents(name);
    nodes_[name].directory = true;
}

void FakeFileSystem::add_file(const std::string& path, std::vector<unsigned char> bytes) {
    const std::string name = tree_name(path);
    add_parents(name);
    Node& node = nodes_[name];
    node = Node{};
    node.bytes = std::move(bytes);
}

void FakeFileSystem::report_size(const std::string& path, std::uint64_t size) {
    Node& node = nodes_[tree_name(path)];
    node.size_reported = true;
    node.reported_size = size;
}

void FakeFileSystem::fail_listing(const std::string& path, const std::string& reason) {
    Node& node = nodes_[tree_name(path)];
    node.listing_fails = true;
    node.listing_reason = reason;
}

void FakeFileSystem::fail_read(
    const std::string& path, std::uint64_t offset, const std::string& reason) {
    Node& node = nodes_[tree_name(path)];
    node.read_fails = true;
    node.read_fail_offset = offset;
    node.read_reason = reason;
}

bool FakeFileSystem::list_directory(
    const std::filesystem::path& directory,
    std::size_t max_names,
    std::vector<std::string>& names,
    std::string& reason) {
    names.clear();
    const std::string name = tree_name(directory.generic_string());
    listed_.push_back(name);
    const auto found = nodes_.find(name);
    if (found == nodes_.end()) {
        reason = "does not exist";
        return false;
    }
    if (!found->second.directory) {
        reason = "is not a directory";
        return false;
    }
    if (found->second.listing_fails) {
        reason = found->second.listing_reason;
        return false;
    }
    const std::string prefix = name + "/";
    for (const auto& entry : nodes_) {
        if (entry.first.size() > prefix.size() &&
            entry.first.compare(0, prefix.size(), prefix) == 0 &&
            entry.first.find('/', prefix.size()) == std::string::npos) {
            if (names.size() == max_names) {
                names.clear();
                reason = "holds more than " + std::to_string(max_names) + " names";
                return false;
            }
            names.push_back(entry.first.substr(prefix.size()));
        }
    }
    return true;
}

bool FakeFileSystem::file_size(
    const std::filesystem::path& path, std::uint64_t& size, std::string& reason) {
    const std::string name = tree_name(path.generic_string());
    sized_.push_back(name);
    const auto found = nodes_.find(name);
    if (found == nodes_.end()) {
        reason = "does not exist";
        return false;
    }
    if (found->second.directory) {
        reason = "is a directory, not a file";
        return false;
    }
    size = found->second.size_reported ? found->second.reported_size
                                       : static_cast<std::uint64_t>(found->second.bytes.size());
    return true;
}

bool FakeFileSystem::read_file(
    const std::filesystem::path& path,
    std::uint64_t offset,
    unsigned char* buffer,
    std::size_t count,
    std::size_t& got,
    std::string& reason) {
    got = 0;
    ReadCall call;
    call.path = tree_name(path.generic_string());
    call.offset = offset;
    call.count = count;
    reads_.push_back(call);
    const auto found = nodes_.find(call.path);
    if (found == nodes_.end()) {
        reason = "does not exist";
        return false;
    }
    const Node& node = found->second;
    if (node.directory) {
        reason = "is a directory, not a file";
        return false;
    }
    if (node.read_fails && count > 0 && offset + count > node.read_fail_offset) {
        reason = node.read_reason;
        return false;
    }
    if (offset >= node.bytes.size()) {
        return true;
    }
    const std::uint64_t available = node.bytes.size() - offset;
    const std::size_t take =
        available < count ? static_cast<std::size_t>(available) : count;
    for (std::size_t i = 0; i < take; ++i) {
        buffer[i] = node.bytes[static_cast<std::size_t>(offset) + i];
    }
    got = take;
    reads_.back().got = take;
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
