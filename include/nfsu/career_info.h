#pragma once
#include "nfsu/race_events.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
namespace nfsu {
enum class CareerInfoScreen { rankings, statistics };
// Native4EA090/4EA460: bounded pages, initial category0, no wrap or cue at
// either edge. The menu is transient and never changes career progression.
class CareerInfoMenu {
public:
    bool active()const noexcept{return active_;}
    CareerInfoScreen screen()const noexcept{return screen_;}
    unsigned category()const noexcept{return category_;}
    unsigned last()const noexcept{return screen_==CareerInfoScreen::rankings?3:4;}
    unsigned revision()const noexcept{return revision_;}
    void open(CareerInfoScreen screen)noexcept{screen_=screen;category_=0;active_=true;++revision_;}
    void close()noexcept{active_=false;++revision_;}
    bool move(int direction)noexcept {
        if(!active_||!direction)return false;
        const auto old=category_;
        if(direction<0){if(category_)--category_;}else if(category_<last())++category_;
        if(old==category_)return false;
        return true;
    }
private:
    CareerInfoScreen screen_=CareerInfoScreen::rankings;
    unsigned category_=0,revision_=0;bool active_=false;
};
using CareerInfoValues=std::map<std::string,std::string>;
struct CareerModeStatistics {std::uint32_t starts=0,wins=0;std::int64_t style=0;};
struct CareerStatistics {
    // Old saves contain no mode/mileage history. Never reconstruct it from
    // event status: retries and losses cannot be inferred from that bitset.
    bool partial_history=false;
    std::uint32_t starts=0,wins=0,losses=0,totalled=0;
    double metres=0;
    std::array<CareerModeStatistics,4> modes{}; //circuit,sprint,drag,drift
};
struct CareerRaceReport {
    bool started=false,won=false,totalled=false;
    double metres=0;
    std::int64_t style=0;
};
//4AB880 classifies statistics by authored track flags, not selected UI event.
// This also classifies knockout/time-trial correctly on a circuit/sprint road.
inline int career_statistics_mode(std::uint32_t track_mask)noexcept {
    if(track_mask&1)return 0;
    if(track_mask&2)return 2;
    if(track_mask&4)return 1;
    if(track_mask&8)return 3;
    return -1;
}
void record_career_statistics(CareerStatistics&,std::uint32_t track_mask,const CareerRaceReport&)noexcept;
}
