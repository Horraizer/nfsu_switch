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
                 std::uint32_t gain_q15, std::uint32_t pitch_q16) noexcept {
    if (slot >= voices_.size() || !clip || !clip->valid() || !pitch_q16)
        return false;
    voices_[slot] = {clip, 0, std::min(gain_q15, 32768U),
                     std::min(pitch_q16, 262144U), loop};
    return true;
}

void Mixer::control(std::size_t slot, std::uint32_t gain_q15,
                    std::uint32_t pitch_q16) noexcept {
    if (slot >= voices_.size()) return;
    voices_[slot].gain_q15 = std::min(gain_q15, 32768U);
    voices_[slot].pitch_q16 = std::max(1U, std::min(pitch_q16, 262144U));
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
                 voices_[i].pitch_q16) / kOutputRate);
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
                static_cast<std::uint64_t>(clip.loop_end ? clip.loop_start : 0) << 16;
            const std::uint64_t loop_end =
                static_cast<std::uint64_t>(clip.loop_end ? clip.loop_end : clip.frames()) << 16;
            if (voice.loop && voice.phase_q16 >= loop_end)
                voice.phase_q16 = loop_begin +
                    (voice.phase_q16 - loop_end) % (loop_end - loop_begin);
            const auto index = static_cast<std::size_t>(voice.phase_q16 >> 16);
            if (index >= clip.frames()) { voice = {}; continue; }
            left += static_cast<std::int64_t>(sample_at(clip, index, 0)) *
                    voice.gain_q15 / 32768;
            right += static_cast<std::int64_t>(sample_at(clip, index, 1)) *
                     voice.gain_q15 / 32768;
            voice.phase_q16 += steps[i];
        }
        stereo[frame * 2] = clamp16(left);
        stereo[frame * 2 + 1] = clamp16(right);
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
                        const Segment& segment, std::string& error) {
    if (worker_.joinable() || !input || !*input || !segment.frames ||
        segment.rate < 8000 || segment.rate > 192000 ||
        (segment.channels != 1 && segment.channels != 2)) {
        error = "invalid AST stream source";
        return false;
    }
    stopping_ = false;
    finished_ = false;
    worker_ = std::thread([this, input = std::move(input), segment]() mutable {
        try {
            std::array<std::int16_t, 512 * 2> converted{};
            std::size_t used = 0;
            std::uint64_t phase = 0;
            const auto flush = [&] {
                if (used && !ring_.push_wait(converted.data(), used)) throw StreamStopped{};
                used = 0;
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
                        phase += kOutputRate;
                        while (phase >= segment.rate) {
                            converted[used * 2] = left;
                            converted[used * 2 + 1] = right;
                            if (++used == 512) flush();
                            phase -= segment.rate;
                        }
                    }
                });
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
