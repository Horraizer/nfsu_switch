#pragma once
#include "nfsu/race_frame_stats.h"
#include <cstddef>
#include <cstdio>

namespace nfsu {
// Completed present-to-present intervals, independent of simulation work or
// packet cadence. The owning game keeps this fixed histogram on the heap.
class LanFrameStats {
public:
    void presented(std::uint64_t completed_us,bool racing) {
        if(!racing){have_previous_=false;return;}
        if(have_previous_&&completed_us>previous_us_){
            RaceFrameSample sample;sample.frame_ms=double(completed_us-previous_us_)*.001;
            stats_.add(sample);
        }
        previous_us_=completed_us;have_previous_=true;
    }
    const RaceFrameStats& stats()const noexcept{return stats_;}
    void summary(char* text,std::size_t size,bool host)const {
        const auto bin=stats_.percentile_bin(95);
        const double p95=bin==RaceFrameStats::kOverflowBin?-1.:(bin+1)*RaceFrameStats::kBinWidthMs;
        std::snprintf(text,size,
            "LAN_FRAMES role=%s samples=%llu mean_ms=%.3f p95_bin_upper_ms=%.2f over_25=%llu max_ms=%.3f histogram_bin_ms=0.25",
            host?"host":"guest",(unsigned long long)stats_.count(),stats_.mean().frame_ms,p95,
            (unsigned long long)stats_.over_budget(),stats_.max_ms());
    }
private:
    RaceFrameStats stats_;
    std::uint64_t previous_us_=0;
    bool have_previous_=false;
};
}
