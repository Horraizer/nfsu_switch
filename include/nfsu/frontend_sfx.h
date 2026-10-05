#pragma once
#include "nfsu/audio_playback.h"
#include <filesystem>
#include <mutex>
namespace nfsu::audio {
enum class FrontendSound { up, down, select, back };
// Original common bank cues, mixed into the existing menu device.
class FrontendSfx {
public:
    bool load(const std::filesystem::path& root, std::string& error);
    void play(FrontendSound cue);
    void reset();
    bool active();
    void mix(std::int16_t* stereo, std::size_t frames);
private:
    std::mutex mutex_;
    std::array<PcmClip,4> clips_;
    Mixer mixer_;
    std::size_t voice_ = 0;
    bool ready_ = false;
};
}
