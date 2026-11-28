#pragma once
#include "nfsu/lan_protocol.h"
#include "nfsu/race_result.h"

namespace nfsu {
inline void write_lan_statistics(LanRaceFrame& frame,const RaceResultStatistics& statistics) {
    for(unsigned actor=0;actor<2;++actor)frame.statistics[actor]=statistics.sample(actor);
    frame.statistics_valid=true;
}
// The guest's visual frame can omit intermediate authority steps. Import the
// authority's completed physical samples instead of integrating its rendered
// snapshots. Reorder both humans together with LanRaceView's local actor zero.
inline bool read_lan_statistics(const LanRaceFrame& frame,unsigned local,RaceResultStatistics& statistics) noexcept {
    if(!frame.statistics_valid||local>1)return false;
    std::array<RaceStatisticsSample,4> samples{};
    samples[0]=frame.statistics[local];samples[1]=frame.statistics[1-local];
    statistics.replace_samples(samples,frame.step);return true;
}
}
