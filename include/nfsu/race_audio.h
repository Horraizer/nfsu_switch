#pragma once

#include "nfsu/race_session.h"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace nfsu::audio {

// Copied only after a race tick. It has no pointer back into RaceSession.
struct RaceAudioSnapshot {
    double rpm = 0;
    double throttle = 0;
    double speed_mps = 0;
    double wheel_slip = 0;
    double impact = 0;
    std::uint32_t contact_events = 0;
};

RaceAudioSnapshot race_audio_snapshot(const RaceSession& session) noexcept;

// Reads retail audio and drives the platform output. Missing banks or a
// missing device leave the race running with an explicit diagnostic.
class RaceAudioService {
public:
    RaceAudioService();
    ~RaceAudioService();
    RaceAudioService(const RaceAudioService&) = delete;
    RaceAudioService& operator=(const RaceAudioService&) = delete;
    bool start(const std::filesystem::path& retail_root, std::string& diagnostic);
    void on_tick(const RaceAudioSnapshot& snapshot);
    void set_suspended(bool suspended) noexcept;
    void stop();
    std::string diagnostic() const;
    std::size_t resident_bytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nfsu::audio
