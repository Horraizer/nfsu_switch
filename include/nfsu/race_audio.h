#pragma once

#include "nfsu/audio.h"
#include "nfsu/collision_audio.h"
#include "nfsu/race_session.h"

#include <cstdint>
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nfsu::audio {

// Copied only after a race tick. It has no pointer back into RaceSession.
struct RaceAudioSnapshot {
    double rpm = 0;
    double throttle = 0;
    double speed_mps = 0;
    double wheel_slip = 0;
    std::uint64_t tick = 0;
    // Latest 120 Hz physics step, independent of countdown/race tick epoch.
    std::uint64_t physics_step = 0;
    std::array<CollisionContact, 512> contacts{};
    std::size_t contact_count = 0;
    bool collision_reset = false;
    int countdown = 0;
    int completed_laps = 0;
    bool finished = false;
};

RaceAudioSnapshot race_audio_snapshot(const RaceSession& session) noexcept;
std::array<double, 4> engine_layer_weights(double rpm) noexcept;
std::vector<Segment> race_music_playlist(const Bank& bank,
                                         const std::vector<Segment>& indexed);

enum class RaceInterfaceCue : std::uint8_t {
    none, countdown, go, lap, finish
};

// Consumes only copied race state. Finish takes precedence over a final-lap
// crossing on the same tick.
class RaceCueScheduler {
public:
    RaceInterfaceCue update(const RaceAudioSnapshot& snapshot) noexcept;
private:
    int countdown_ = 0;
    int completed_laps_ = 0;
    bool finished_ = false;
};

// Reads retail audio and drives the platform output. Missing banks or a
// missing device leave the race running with an explicit diagnostic.
class RaceAudioService {
public:
    struct Stats {
        std::uint64_t ring_underruns = 0;
        std::size_t ring_fill = 0;
        std::size_t ring_capacity = 0;
        std::uint64_t output_late = 0;
        std::uint32_t queued_frames = 0;
        std::uint32_t thread_us = 0;
        std::uint64_t mix_clips = 0;
        std::uint64_t mix_overloads = 0;
        std::uint64_t mix_limited_frames = 0;
    };
    RaceAudioService();
    ~RaceAudioService();
    RaceAudioService(const RaceAudioService&) = delete;
    RaceAudioService& operator=(const RaceAudioService&) = delete;
    bool start(const std::filesystem::path& retail_root, std::string& diagnostic,
               const std::function<void(const char*)>& progress = {},
               const std::function<bool()>& decoder_thread_setup = {},
               bool original_ig_playlist = false);
    void on_tick(const RaceAudioSnapshot& snapshot);
    void set_suspended(bool suspended) noexcept;
    void stop();
    std::string diagnostic() const;
    Stats stats() const noexcept;
    std::size_t resident_bytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nfsu::audio
