#include "nfsu/audio_playback.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace nfsu::audio {
namespace {

std::int16_t sample_at(const PcmClip& clip, std::size_t frame,
                       std::uint32_t channel) noexcept {
    return clip.pcm[frame * clip.channels +
                    (clip.channels == 1 ? 0 : channel)];
}

std::int32_t interpolate(std::int32_t first, std::int32_t second,
                         std::uint64_t numerator,
                         std::uint64_t denominator) noexcept {
    return first + static_cast<std::int32_t>(
        (static_cast<std::int64_t>(second - first) *
         static_cast<std::int64_t>(numerator)) /
        static_cast<std::int64_t>(denominator));
}

constexpr std::uint32_t kGainRampFrames = 240;

std::int16_t clamp16(std::int64_t value) noexcept {
    return static_cast<std::int16_t>(std::max<std::int64_t>(-32768,
        std::min<std::int64_t>(32767, value)));
}

std::int16_t le16(const std::uint8_t* data) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8));
}

struct StreamStopped {};

} // namespace

bool PcmClip::valid() const noexcept {
    if (rate < 8000 || rate > 192000 || (channels != 1 && channels != 2) ||
        pcm.empty() || pcm.size() % channels || frames() > 100000000)
        return false;
    return !loop_end || (loop_end <= frames() && loop_start < loop_end);
}

bool Mixer::play(std::size_t slot, const PcmClip* clip, bool loop,
                 std::uint32_t gain_q15, std::uint32_t pitch_q16,
                 bool fade_in) noexcept {
    if (slot >= voices_.size() || !clip || !clip->valid() || !pitch_q16)
        return false;
    const auto gain = std::min(gain_q15, 32768U);
    voices_[slot] = {clip, 0, fade_in ? 0U : gain, gain,
                     fade_in ? 0U : gain, fade_in ? kGainRampFrames : 0U,
                     std::min(pitch_q16, 262144U), loop};
    return true;
}

void Mixer::control(std::size_t slot, std::uint32_t gain_q15,
                    std::uint32_t pitch_q16) noexcept {
    if (slot >= voices_.size()) return;
    auto& voice = voices_[slot];
    const auto gain = std::min(gain_q15, 32768U);
    if (gain != voice.target_gain_q15) {
        voice.ramp_origin_q15 = voice.gain_q15;
        voice.target_gain_q15 = gain;
        voice.ramp_left = kGainRampFrames;
    }
    voice.pitch_q16 = std::max(1U, std::min(pitch_q16, 262144U));
}

void Mixer::stop(std::size_t slot) noexcept {
    if (slot < voices_.size()) voices_[slot] = {};
}

bool Mixer::active(std::size_t slot) const noexcept {
    return slot < voices_.size() && voices_[slot].clip;
}

void Mixer::render(std::int16_t* stereo, std::size_t frames,
                   const std::int16_t* stream,
                   std::uint32_t stream_gain_q15) noexcept {
    if (!stereo) return;
    std::array<std::uint64_t, kMixVoices> steps{};
    for (std::size_t i = 0; i < voices_.size(); ++i)
        if (voices_[i].clip)
            steps[i] = std::max<std::uint64_t>(1,
                (static_cast<std::uint64_t>(voices_[i].clip->rate) *
                 voices_[i].pitch_q16 * 65536ULL) / kOutputRate);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        std::int64_t left = 0, right = 0;
        if (stream) {
            const auto gain = std::min(stream_gain_q15, 32768U);
            left += static_cast<std::int64_t>(stream[frame * 2]) * gain / 32768;
            right += static_cast<std::int64_t>(stream[frame * 2 + 1]) * gain / 32768;
        }
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            auto& voice = voices_[i];
            if (!voice.clip) continue;
            const auto& clip = *voice.clip;
            const std::uint64_t loop_begin =
                static_cast<std::uint64_t>(clip.loop_end ? clip.loop_start : 0) << 32;
            const std::uint64_t loop_end =
                static_cast<std::uint64_t>(clip.loop_end ? clip.loop_end : clip.frames()) << 32;
            if (voice.loop && voice.phase_q32 >= loop_end)
                voice.phase_q32 = loop_begin +
                    (voice.phase_q32 - loop_end) % (loop_end - loop_begin);
            const auto index = static_cast<std::size_t>(voice.phase_q32 >> 32);
            if (index >= clip.frames()) { voice = {}; continue; }
            if (voice.ramp_left) {
                const auto elapsed = kGainRampFrames - voice.ramp_left + 1;
                const auto delta = static_cast<std::int32_t>(voice.target_gain_q15) -
                                   static_cast<std::int32_t>(voice.ramp_origin_q15);
                voice.gain_q15 = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(voice.ramp_origin_q15) +
                    delta * static_cast<std::int32_t>(elapsed) /
                    static_cast<std::int32_t>(kGainRampFrames));
                --voice.ramp_left;
            }
            std::size_t next = index + 1;
            if (voice.loop && next >= static_cast<std::size_t>(clip.loop_end ?
                    clip.loop_end : clip.frames()))
                next = clip.loop_end ? clip.loop_start : 0;
            else if (next >= clip.frames()) next = index;
            const auto fraction = voice.phase_q32 & 0xFFFFFFFFULL;
            const auto sample_left = interpolate(sample_at(clip, index, 0),
                sample_at(clip, next, 0), fraction, 1ULL << 32);
            const auto sample_right = interpolate(sample_at(clip, index, 1),
                sample_at(clip, next, 1), fraction, 1ULL << 32);
            left += static_cast<std::int64_t>(sample_left) * voice.gain_q15 / 32768;
            right += static_cast<std::int64_t>(sample_right) * voice.gain_q15 / 32768;
            voice.phase_q32 += steps[i];
        }
        clipped_samples_ += left < -32768 || left > 32767;
        clipped_samples_ += right < -32768 || right > 32767;
        stereo[frame * 2] = clamp16(left);
        stereo[frame * 2 + 1] = clamp16(right);
    }
}

void OutputFade::apply(std::int16_t* stereo, std::size_t frames,
                       bool muted) noexcept {
    if (!stereo) return;
    for (std::size_t i = 0; i < frames; ++i) {
        if (muted) {
            if (level_) --level_;
        } else if (level_ < kGainRampFrames) ++level_;
        const auto level = static_cast<std::int32_t>(level_);
        stereo[i * 2] = static_cast<std::int16_t>(
            static_cast<std::int32_t>(stereo[i * 2]) * level /
            static_cast<std::int32_t>(kGainRampFrames));
        stereo[i * 2 + 1] = static_cast<std::int16_t>(
            static_cast<std::int32_t>(stereo[i * 2 + 1]) * level /
            static_cast<std::int32_t>(kGainRampFrames));
    }
}

StereoRing::StereoRing(std::size_t capacity_frames) :
    data_([&] {
        if (!capacity_frames || capacity_frames > 1U << 20)
            throw std::invalid_argument("bad stereo ring capacity");
        return capacity_frames * 2;
    }()), capacity_(capacity_frames) {}

void StereoRing::write_frames(const std::int16_t* stereo, std::size_t frames) {
    for (std::size_t i = 0; i < frames; ++i) {
        data_[tail_ * 2] = stereo[i * 2];
        data_[tail_ * 2 + 1] = stereo[i * 2 + 1];
        tail_ = (tail_ + 1) % capacity_;
    }
    size_ += frames;
}

std::size_t StereoRing::push(const std::int16_t* stereo, std::size_t frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stereo || closed_) return 0;
    const auto accepted = std::min(frames, capacity_ - size_);
    write_frames(stereo, accepted);
    overruns_ += frames - accepted;
    return accepted;
}

std::size_t StereoRing::pop(std::int16_t* stereo, std::size_t frames) {
    if (!stereo) return 0;
    std::unique_lock<std::mutex> lock(mutex_);
    const auto available = std::min(frames, size_);
    for (std::size_t i = 0; i < available; ++i) {
        stereo[i * 2] = data_[head_ * 2];
        stereo[i * 2 + 1] = data_[head_ * 2 + 1];
        head_ = (head_ + 1) % capacity_;
    }
    for (std::size_t i = available; i < frames; ++i)
        stereo[i * 2] = stereo[i * 2 + 1] = 0;
    size_ -= available;
    underruns_ += frames - available;
    lock.unlock();
    space_.notify_one();
    return available;
}

bool StereoRing::push_wait(const std::int16_t* stereo, std::size_t frames) {
    if (!stereo) return false;
    std::unique_lock<std::mutex> lock(mutex_);
    std::size_t done = 0;
    while (done < frames) {
        space_.wait(lock, [&] { return closed_ || size_ < capacity_; });
        if (closed_) return false;
        const auto count = std::min(frames - done, capacity_ - size_);
        write_frames(stereo + done * 2, count);
        done += count;
    }
    return true;
}

void StereoRing::close() {
    { std::lock_guard<std::mutex> lock(mutex_); closed_ = true; }
    space_.notify_all();
}

std::size_t StereoRing::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

std::uint64_t StereoRing::underruns() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return underruns_;
}

std::uint64_t StereoRing::overruns() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return overruns_;
}

AstStreamer::AstStreamer(std::size_t capacity_frames) : ring_(capacity_frames) {}
AstStreamer::~AstStreamer() { stop(); }

bool AstStreamer::start(std::unique_ptr<std::istream> input,
                        const Segment& segment, std::string& error,
                        const std::function<bool()>& thread_setup) {
    if (worker_.joinable() || !input || !*input || !segment.frames ||
        segment.rate < 8000 || segment.rate > 192000 ||
        (segment.channels != 1 && segment.channels != 2)) {
        error = "invalid AST stream source";
        return false;
    }
    stopping_ = false;
    finished_ = false;
    worker_ = std::thread([this, input = std::move(input), segment,
                           thread_setup]() mutable {
        try {
            if (thread_setup && !thread_setup())
                throw std::runtime_error("audio decoder core setup failed");
            std::array<std::int16_t, 512 * 2> converted{};
            std::size_t used = 0;
            std::uint64_t source_index = 0, output_index = 0;
            std::array<std::int16_t, 2> previous{};
            const auto flush = [&] {
                if (used && !ring_.push_wait(converted.data(), used)) throw StreamStopped{};
                used = 0;
            };
            const auto emit = [&](std::int16_t left, std::int16_t right) {
                converted[used * 2] = left;
                converted[used * 2 + 1] = right;
                if (++used == 512) flush();
            };
            decode_ast_segment(*input, segment,
                [&](const std::uint8_t* bytes, std::size_t size) {
                    const std::size_t stride = segment.channels * 2;
                    if (size % stride) throw std::runtime_error("unaligned AST PCM block");
                    for (std::size_t frame = 0; frame < size / stride; ++frame) {
                        if (stopping_) throw StreamStopped{};
                        const auto left = le16(bytes + frame * stride);
                        const auto right = segment.channels == 1 ? left :
                            le16(bytes + frame * stride + 2);
                        if (!source_index) {
                            emit(left, right);
                            ++output_index;
                        } else {
                            const auto end = source_index * kOutputRate;
                            const auto begin = (source_index - 1) * kOutputRate;
                            while (output_index * segment.rate <= end) {
                                const auto fraction = output_index * segment.rate - begin;
                                emit(static_cast<std::int16_t>(interpolate(
                                         previous[0], left, fraction, kOutputRate)),
                                     static_cast<std::int16_t>(interpolate(
                                         previous[1], right, fraction, kOutputRate)));
                                ++output_index;
                            }
                        }
                        previous = {left, right};
                        ++source_index;
                    }
                });
            while (output_index * segment.rate < source_index * kOutputRate) {
                emit(previous[0], previous[1]);
                ++output_index;
            }
            flush();
        } catch (const StreamStopped&) {
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(error_mutex_);
            error_ = e.what();
        }
        finished_ = true;
    });
    return true;
}

void AstStreamer::stop() {
    stopping_ = true;
    ring_.close();
    if (worker_.joinable()) worker_.join();
}

std::string AstStreamer::error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_;
}

} // namespace nfsu::audio
