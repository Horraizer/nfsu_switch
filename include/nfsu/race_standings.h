#pragma once
#include <array>
#include <cstddef>

namespace nfsu {
// Four-racer circuit presentation. Retail 434ED0 preserves the previous
// order on equal progress; two completed cars compare their finish places.
struct RaceStandingSample { double progress=0; unsigned finish_place=0; };
struct RaceStandings {
    std::array<unsigned,4> order{{0,1,2,3}};
    unsigned count=1;
    void update(const std::array<RaceStandingSample,4>& samples,unsigned racers) noexcept {
        if(racers<1 || racers>4)return;
        if(count!=racers)order={{0,1,2,3}};
        count=racers;
        for(unsigned i=1;i<count;++i)for(unsigned j=i;j>0;--j) {
            const auto& a=samples[order[j-1]];const auto& b=samples[order[j]];
            const bool swap=a.finish_place && b.finish_place ? b.finish_place<a.finish_place :
                b.progress>a.progress;
            if(!swap)break;
            const auto held=order[j];order[j]=order[j-1];order[j-1]=held;
        }
    }
    unsigned position(unsigned actor) const noexcept {
        for(unsigned i=0;i<count;++i)if(order[i]==actor)return i+1;
        return 0;
    }
};
}
