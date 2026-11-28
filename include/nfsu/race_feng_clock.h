#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nfsu {
//585C54/585C60 and585CD5..585D69. This is the UI producer, independent
//of physics. The residue exists before an Animate script is started.
class RaceFengClock {
public:
    double publish(double measured,float minimum=0.F,float maximum=1.F) noexcept {
        if(!std::isfinite(measured)||measured<=0||measured>1)return 0;
        const float interval=std::clamp(float(measured)+remainder_,minimum,maximum);
        const int units=int(interval*3600.F+.5F);
        const int world_ticks=int(float(units)*(4000.F/3600.F)+.5F);
        remainder_=interval-float(world_ticks)*.00025F;
        return float(units)*(1.F/3600.F);
    }
    // Full native producer on the normal60Hz console display path.
    // Gaps and resume replace only the timestamp anchor, not the residue.
    double sample(std::uint64_t now_us) noexcept {
        const auto elapsed=anchored_&&now_us>=previous_?now_us-previous_:0;
        previous_=now_us;anchored_=true;
        return elapsed&&elapsed<=1000000?publish(double(elapsed)/1000000,1.F/60.F,3.F/60.F):0;
    }
    void resume() noexcept {anchored_=false;}
    float remainder()const noexcept {return remainder_;}
private:
    float remainder_=0;std::uint64_t previous_=0;bool anchored_=false;
};
//585DAB..585DE9 adds each prepared real-frame interval to73AD3C.
//This phase clock is independent from the world timer and its60Hz serial.
class RaceFinishLoadingClock {
public:
    RaceFinishLoadingClock(RaceFengClock& producer,std::uint64_t now_ns) noexcept:producer_(producer) {
        // Entry is a display update. Publish it with the previous race timestamp
        //and residue, then latch the phase origin after that clock update.
        producer_.sample(now_ns/1000);
    }
    void sample(std::uint64_t now_ns) noexcept {
        const auto seconds=producer_.sample(now_ns/1000);
        if(seconds>0)ticks_+=static_cast<unsigned>(float(seconds)*4000.F+.5F);
    }
    void resume() noexcept {producer_.resume();}
    std::uint32_t original_ticks()const noexcept {return ticks_;}
private:
    RaceFengClock& producer_;std::uint32_t ticks_=0;
};
}
