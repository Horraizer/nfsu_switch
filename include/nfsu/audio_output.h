#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace nfsu::audio {

// Platform output owns its device thread. The callback fills exactly the
// requested count of 48 kHz stereo signed 16-bit frames.
class Output {
public:
    struct Stats {
        std::uint64_t late = 0;
        std::uint64_t played_frames = 0;
        std::uint32_t queued_frames = 0;
        std::uint32_t work_us = 0;
    };
    using Render = std::function<void(std::int16_t*, std::size_t)>;
    Output();
    ~Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    bool start(Render render, std::string& error);
    void stop();
    std::string error() const;
    Stats stats() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nfsu::audio
