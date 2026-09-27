#pragma once

#include "nfsu/audio.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>

namespace nfsu::audio {

constexpr std::uint32_t kOutputRate = 48000;
constexpr std::size_t kMixVoices = 8;

// PCM is interleaved signed 16-bit at its original rate. Loop end is exclusive.
struct PcmClip {
    std::vector<std::int16_t> pcm;
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    std::uint32_t loop_start = 0;
    std::uint32_t loop_end = 0;
    std::size_t frames() const noexcept { return channels ? pcm.size() / channels : 0; }
    bool valid() const noexcept;
};

// The caller serializes control and render calls. No allocation, clock or I/O
// occurs in render; all arithmetic and voice order are fixed.
class Mixer {
public:
    bool play(std::size_t slot, const PcmClip* clip, bool loop,
              std::uint32_t gain_q15 = 32768, std::uint32_t pitch_q16 = 65536) noexcept;
    void control(std::size_t slot, std::uint32_t gain_q15,
                 std::uint32_t pitch_q16) noexcept;
    void stop(std::size_t slot) noexcept;
    bool active(std::size_t slot) const noexcept;
    std::uint64_t clipped_samples() const noexcept { return clipped_samples_; }
    // Optional streamed voice is summed before the final clamp.
    void render(std::int16_t* stereo, std::size_t frames,
                const std::int16_t* stream = nullptr,
                std::uint32_t stream_gain_q15 = 0) noexcept;
private:
    struct Voice {
        const PcmClip* clip = nullptr;
        std::uint64_t phase_q32 = 0;
        std::uint32_t gain_q15 = 0;
        std::uint32_t target_gain_q15 = 0;
        std::uint32_t ramp_origin_q15 = 0;
        std::uint32_t ramp_left = 0;
        std::uint32_t pitch_q16 = 65536;
        bool loop = false;
    };
    std::array<Voice, kMixVoices> voices_{};
    std::uint64_t clipped_samples_ = 0;
};

// A fixed-capacity stereo frame queue. push/pop are nonblocking; short pop
// produces silence. push_wait applies backpressure to a decoder thread.
class StereoRing {
public:
    explicit StereoRing(std::size_t capacity_frames);
    std::size_t push(const std::int16_t* stereo, std::size_t frames);
    std::size_t pop(std::int16_t* stereo, std::size_t frames);
    bool push_wait(const std::int16_t* stereo, std::size_t frames);
    void close();
    std::size_t size() const;
    std::size_t capacity() const noexcept { return capacity_; }
    std::uint64_t underruns() const;
    std::uint64_t overruns() const;
private:
    void write_frames(const std::int16_t* stereo, std::size_t frames);
    mutable std::mutex mutex_;
    std::condition_variable space_;
    std::vector<std::int16_t> data_;
    const std::size_t capacity_;
    std::size_t head_ = 0, tail_ = 0, size_ = 0;
    std::uint64_t underruns_ = 0, overruns_ = 0;
    bool closed_ = false;
};

// The decoder reads one AST block at a time on its own thread. Its sink
// resamples to 48 kHz stereo and waits for room in a bounded ring.
class AstStreamer {
public:
    explicit AstStreamer(std::size_t capacity_frames = 8192);
    ~AstStreamer();
    AstStreamer(const AstStreamer&) = delete;
    AstStreamer& operator=(const AstStreamer&) = delete;
    bool start(std::unique_ptr<std::istream> input, const Segment& segment,
               std::string& error);
    void stop();
    std::size_t pop(std::int16_t* stereo, std::size_t frames) {
        return ring_.pop(stereo, frames);
    }
    const StereoRing& ring() const noexcept { return ring_; }
    bool finished() const noexcept { return finished_.load(); }
    std::string error() const;
private:
    StereoRing ring_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> finished_{false};
    mutable std::mutex error_mutex_;
    std::string error_;
};

} // namespace nfsu::audio
