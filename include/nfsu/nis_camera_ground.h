#pragma once

namespace nfsu {
// Original48E54B..48E5B3: terrain history belongs to the camera mover.
// Re-recording a presentation clock must not advance this world update.
struct NisCameraGround {
    float height=0;
    double clock=-1;
    bool valid=false;

    float sample(float ground,double next_clock) noexcept {
        if(!valid || next_clock<clock) {
            height=ground;valid=true;
        }else if(next_clock!=clock) {
            const float retained=(1.F-.03F)*height;
            const float incoming=.03F*ground;
            height=retained+incoming;
        }
        clock=next_clock;
        return height;
    }
};
}
