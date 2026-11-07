#include "nfsu/intro_movie.h"
#include "nfsu/audio_output.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace nfsu {
namespace {
// Original North American bootflow: LS_EAlogo, LS_PSAMovie, LS_IntroFMV.
// speed.exe 0x4de1e0 / 0x6fa1a4; movie name table 0x6f9170 maps 19 to e3_title.
constexpr const char* sequence[]={"na_boot.mad","PSA.mad","e3_title.mad"};
class MovieAudio {
    MovieReader& movie_;
    std::vector<std::int16_t> chunk_;
    std::size_t cursor_=0;
    std::array<std::int16_t,2> a_{},b_{};
    unsigned phase_=0;
    bool started_=false,eof_=false;
public:
    std::atomic<std::uint64_t> rendered{0};
    std::atomic<bool> finished{false};
    explicit MovieAudio(MovieReader& m):movie_(m){}
    bool sample(std::array<std::int16_t,2>& value) {
        if(cursor_==chunk_.size()) {
            if(!movie_.next_audio(chunk_)) return false;
            cursor_=0;
        }
        value={chunk_[cursor_],chunk_[cursor_+1]};cursor_+=2;return true;
    }
    void render(std::int16_t* dest,std::size_t frames) {
        if(!started_) {
            if(!sample(a_)) throw std::runtime_error("empty movie audio");
            if(!sample(b_)) {b_=a_;eof_=true;}
            started_=true;
        }
        const auto duration=(movie_.audio_frames()*48000+movie_.audio_rate()-1)/movie_.audio_rate();
        auto count=rendered.load();
        for(std::size_t i=0;i<frames;++i) {
            if(count>=duration) { dest[i*2]=dest[i*2+1]=0;finished=true;continue; }
            for(unsigned ch=0;ch<2;++ch)
                dest[i*2+ch]=static_cast<std::int16_t>((std::int64_t(a_[ch])*(48000-phase_)+std::int64_t(b_[ch])*phase_)/48000);
            ++count;
            phase_+=movie_.audio_rate();
            if(phase_>=48000) {
                phase_-=48000;a_=b_;
                if(!eof_ && !sample(b_)) {b_=a_;eof_=true;}
            }
        }
        rendered=count;
    }
};
IntroOutcome control_outcome(IntroControl c) {
    return c==IntroControl::quit?IntroOutcome::quit:IntroOutcome::skipped;
}
}
static IntroResult play_sequence(const std::string& root,const char* const* names,std::size_t count,
                                 const IntroPoll& poll,const IntroPresent& present) {
    IntroResult result;
    if(!poll||!present) {result.outcome=IntroOutcome::error;result.error="intro callbacks missing";return result;}
    try {
        for(std::size_t index=0;index<count;++index) {
            const auto* name=names[index];
            const auto initial=poll(); if(initial!=IntroControl::continue_playback) {result.outcome=control_outcome(initial);return result;}
            MovieReader movie(root+"/Movies/"+name);
            MovieFrame frame;
            if(!movie.next_video(frame)) throw std::runtime_error("movie contains no video");
            MovieAudio pcm(movie);
            audio::Output output;
            std::string error;
            if(!output.start([&](std::int16_t* dest,std::size_t frames){pcm.render(dest,frames);},error))
                throw std::runtime_error(error);
            bool video=true;
            std::uint64_t video_end=0;
            unsigned no_progress_polls=0;
            std::uint64_t last_clock=0;
            const auto audio_end=(movie.audio_frames()*48000+movie.audio_rate()-1)/movie.audio_rate();
            for(;;) {
                const auto c=poll();
                if(c!=IntroControl::continue_playback) {result.outcome=control_outcome(c);output.stop();return result;}
                error=output.error();if(!error.empty()) throw std::runtime_error(error);
                // Use the actual device sample position, including output queue
                // latency, as the common clock. A late frame cannot slow audio.
                const auto clock=output.stats().played_frames;
                // Count active iterations: wall time includes HOME/sleep process
                // suspension and must not trigger a false failure on resume.
                if(clock!=last_clock) {last_clock=clock;no_progress_polls=0;}
                else ++no_progress_polls;
                if(no_progress_polls>2000)
                    throw std::runtime_error("movie audio device clock stalled");
                if(video && clock*65535>=frame.timestamp_units*48) {
                    // Still decode every predictive frame when presentation is
                    // late; only presentation can be dropped safely.
                    // All three original Movie objects use Size 640x480.
                    // The MAD samples are non-square; native type7 rendering
                    // uses FNG geometry, not the encoded texture aspect.
                    frame.display_width=640;frame.display_height=480;
                    if(clock*65535<(frame.timestamp_units+frame.duration_units)*48) present(frame);
                    video_end=frame.timestamp_units+frame.duration_units;
                    video=movie.next_video(frame);
                    continue;
                }
                if(!video && pcm.finished && clock>=audio_end && clock*65535>=video_end*48) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            output.stop();++result.movies_completed;
        }
    } catch(const std::exception& e) {result.outcome=IntroOutcome::error;result.error=e.what();}
    return result;
}
IntroResult play_career_movie(const std::string& root,const std::string& name,
                              const IntroPoll& poll,const IntroPresent& present) {
    if(name.size()<5 || name.size()>32 || name.substr(name.size()-4)!=".mad" ||
       name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.")!=std::string::npos ||
       name.find("..")!=std::string::npos) {
        IntroResult result;result.outcome=IntroOutcome::error;result.error="invalid career movie basename";return result;
    }
    const char* names[]={name.c_str()};return play_sequence(root,names,1,poll,present);
}
IntroResult play_original_intro(const std::string& root,const IntroPoll& poll,const IntroPresent& present) {
    return play_sequence(root,sequence,std::size(sequence),poll,present);
}
IntroResult play_underground_prologue(const std::string& root,const IntroPoll& poll,const IntroPresent& present) {
    // Original MU_UG_NewCareerFMV replaces the authored placeholder "carsel"
    // with this event0 string. It is separate from the boot e3_title movie.
    constexpr const char* movie[]={"01_DAY.mad"};
    return play_sequence(root,movie,1,poll,present);
}
IntroResult play_underground_epilogue(const std::string& root,const IntroPoll& poll,const IntroPresent& present) {
    // UG0 victory bypasses PostRaceResults/StyleAnimator (49E14D/49E0C9),
    // then selects MU_UG_NewCareerExtroFMV (4AB78B..4AB7AE). Its constructor
    // formats event0+0xC4 with %s_S for the fresh-career branch (4E880A).
    constexpr const char* movie[]={"02_FAN_s.mad"};
    return play_sequence(root,movie,1,poll,present);
}
} // namespace nfsu
