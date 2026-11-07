#pragma once
#include "nfsu/traffic_audio.h"
#include <algorithm>
#include <cmath>
namespace nfsu::audio {
struct BackfireSoundCue {bool emit=false;unsigned variant=0;int gain=0;};
//527480 decision boundary, before5272F0 category/position/bank output.
//The separate1154 curve is not an input to this timer or its sound controls.
struct BackfireSoundController {
    float wait=0;unsigned state=0;int gain=0;
    BackfireSoundCue tick(int engine,float rpm,bool throttle_on,float dt,
                         TrafficAudioRandom& random,unsigned& counter)noexcept {
        if(engine<2||engine>3||!std::isfinite(rpm)||!std::isfinite(dt)||dt<0)return {};
        if(throttle_on||rpm<=2000.F){state=0;gain=0;return {};}
        if(state==0){state=1;wait=(random.real(1.5F-.05F)+.05F)+.2F;return {};}
        if(state==2){state=1;const float fraction=std::clamp(rpm/8000.F,0.F,1.F);
            wait=((1.5F-.05F)*random.real(1.F)+.05F)*fraction;gain=0;return {};}
        if(state!=1)return {};
        wait-=dt;if(!(wait<0.F))return {};
        const unsigned low=engine==2?18000:27000,high=engine==2?31500:32767;
        const float amount=rpm<3000.F?0.F:rpm*.00011111111234640703F;
        gain=int(float(low+random.integer(high-low))*amount);
        counter=(counter+1)%5;state=2;return {true,counter,gain};
    }
};
} //namespace nfsu::audio
