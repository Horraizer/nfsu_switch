#pragma once

#include "nfsu/audio.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>

namespace nfsu::audio {

constexpr std::uint32_t kOutputRate = 48000;
constexpr std::size_t kMixVoices = 113;

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

// Recovered652FD0/6533C0/6512F0 software stereo publication. Fresh127
// normal gain, master1, speaker angles90/270; not DirectSound/HRTF mixing.
std::array<float,2> stereo_pan_gains(std::uint16_t azimuth,
                                  std::uint32_t gain_q15) noexcept;

// The caller serializes control and render calls. No allocation, clock or I/O
// occurs in render; all arithmetic and voice order are fixed.
class Mixer {
public:
    bool play(std::size_t slot, const PcmClip* clip, bool loop,
              std::uint32_t gain_q15 = 32768, std::uint32_t pitch_q16 = 65536,
              bool fade_in = false) noexcept;
    void control(std::size_t slot, std::uint32_t gain_q15,
                 std::uint32_t pitch_q16) noexcept;
    // Native64D280 applies angular panning only to mono sources. Generic
    // UI/music playback retains its existing level until explicitly bound.
    void pan(std::size_t slot, std::uint16_t azimuth) noexcept;
    void stop(std::size_t slot) noexcept;
    void release(std::size_t slot) noexcept;
    bool active(std::size_t slot) const noexcept;
    std::uint64_t clipped_samples() const noexcept { return clipped_samples_; }
    // Opt-in stereo-linked peak limiter. Instant attack, linear 50 ms release.
    // A port output policy, not a recovered retail mastering curve. Raw overflow
    // remains observable independently of final PCM saturation.
    void set_peak_limiter(bool enabled) noexcept;
    std::uint64_t overloaded_samples() const noexcept { return overloaded_samples_; }
    std::uint64_t limited_frames() const noexcept { return limited_frames_; }
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
        bool angular_pan = false;
        std::uint16_t azimuth = 0;
        std::uint32_t pan_gain = UINT32_MAX;
        std::array<float,2> channel_gains{};
    };
    std::array<Voice, kMixVoices> voices_{};
    std::uint64_t clipped_samples_ = 0;
    std::uint64_t overloaded_samples_ = 0, limited_frames_ = 0;
    std::uint64_t limiter_gain_q32_ = 1ULL << 32;
    bool peak_limiter_ = false;
};

// Fades the completed stereo mix over five milliseconds on mute and unmute.
class OutputFade {
public:
    bool silent() const noexcept { return level_ == 0; }
    void apply(std::int16_t* stereo, std::size_t frames, bool muted) noexcept;
private:
    std::uint32_t level_ = 0;
};

// Original FE music mix: profile8 music26500, speech profile9 music6300.
// The speech profile attacks over .25s and releases over 2s. This envelope
// applies the original absolute fresh-profile music descriptor gain as well.
// Relative mode preserves a caller's existing master and applies only the
// speech envelope ratio; the original default path is unchanged.
// Voice and menu cues retain their separately owned gain.
class SpeechMusicDuck {
public:
    void apply(std::int16_t* stereo,std::size_t frames,bool speech,bool relative=false) noexcept;
private:
    bool active_=false;
    std::uint32_t origin_=26500,level_=26500,elapsed_=0,duration_=0;
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
               std::string& error,
               const std::function<bool()>& thread_setup = {});
    bool start_playlist(std::unique_ptr<std::istream> input,
                        std::vector<Segment> segments, bool loop,
                        std::string& error,
                        const std::function<bool()>& thread_setup = {});
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
