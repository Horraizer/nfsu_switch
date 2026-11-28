#pragma once

#include "nfsu/race_timing.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace nfsu {
inline bool race_finish_terminal(const RaceRacerTiming& result) noexcept {
    return result.finish_valid||result.status==RacerStatus::knocked_out||
        result.status==RacerStatus::blown_engine||result.status==RacerStatus::totalled||
        result.status==RacerStatus::out_of_time;
}
//585E22..585E6A:6F08A0 accumulates world seconds at60Hz with a float
//residue.6F08A4 separately counts calls. Neither is the4000Hz race timer.
class RaceWorldFrameClock {
public:
    void advance(double seconds) noexcept {
        if(!std::isfinite(seconds)||seconds<=0||seconds>1)return;
        const float total=remainder_+float(seconds);
        const auto whole=static_cast<unsigned>(total*60.F);
        frames_+=whole;remainder_=total-float(whole)*(1.F/60.F);
    }
    std::uint32_t frames()const noexcept {return frames_;}
    float remainder()const noexcept {return remainder_;}
private:
    std::uint32_t frames_=0;float remainder_=0;
};
//42C2FB..42C322 writes180 units, or360 during camera events6/7.
inline unsigned race_finish_driver_delay(unsigned camera_event) noexcept {
    return camera_event==6||camera_event==7?360:180;
}
class RaceFinishHold {
public:
    // Observe after the world update, before585E00's end-of-frame clock.
    // Then advance once with the exact world interval, including prefinish.
    bool observe(const RaceTimingState& timing,unsigned humans,
                 const std::array<unsigned,4>& camera_events={}) noexcept {
        if(timing.step<previous_||timing.restarts!=restarts_){phases_={};clock_={};}
        previous_=timing.step;restarts_=timing.restarts;
        if(!humans||humans>phases_.size()||humans>timing.racers.size())return false;
        bool ready=timing.outcome==RaceOutcome::finished;
        for(unsigned i=0;i<humans;++i) {
            auto& phase=phases_[i];
            if(!race_finish_terminal(timing.racers[i])){phase={};ready=false;continue;}
            if(!phase.active)phase={clock_.frames(),race_finish_driver_delay(camera_events[i]),true};
            //423645..423654 compares the native60Hz world serial strictly.
            ready&=clock_.frames()>phase.begin+phase.delay;
        }
        return ready;
    }
    void advance(double seconds) noexcept {clock_.advance(seconds);}
    std::uint32_t frames()const noexcept {return clock_.frames();}
private:
    struct Phase {std::uint32_t begin=0,delay=0;bool active=false;};
    std::array<Phase,4> phases_{};RaceWorldFrameClock clock_;
    std::size_t previous_=0,restarts_=0;
};
}
