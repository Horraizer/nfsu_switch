#pragma once
#include "nfsu/audio_playback.h"
#include <filesystem>
#include <mutex>
namespace nfsu::audio {
// Native 0x523120 with original 0x5364E0 master defaults and
// FE descriptor 0x6F43B0 (type4: no distance attenuation).
constexpr std::uint32_t kFrontendCueGainQ15 = 16357;
enum class FrontendSound { up, down, select, back, left, right, dialog };
// Original4CD3E0 event/speech-group table6FA738.49BA10 calls it only
//after a career event victory, never for every ordinary finish or loss.
inline int post_race_speech(unsigned event,bool won)noexcept {
    if(!won)return -1;
    constexpr unsigned rows[][2]={{110,5},{109,12},{106,22},{107,45},{76,39},
        {19,49},{22,50},{42,51},{50,54},{63,55},{65,56},{67,57},{73,58},{108,94},{59,19}};
    for(const auto& row:rows)if(row[0]==event)return int(row[1]);
    return -1;
}
// One original English spoken message, mixed on the existing frontend device.
// Decode occurs on the requesting thread; callback mixing only reads PCM.
class FrontendSpeech {
public:
    bool play(const std::filesystem::path& root,int entry,std::string& error);
    void stop();
    bool active();
    void mix(std::int16_t* stereo,std::size_t frames);
private:
    std::mutex mutex_;
    PcmClip clip_;
    Mixer mixer_;
};
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
    std::array<PcmClip,7> clips_;
    Mixer mixer_;
    std::size_t voice_ = 0;
    bool ready_ = false;
};
}
