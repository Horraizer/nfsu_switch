#pragma once
#include "nfsu/racing_line.h"
#include "nfsu/race_route.h"
#include "nfsu/race_timing.h"
#include <array>
#include <string>
namespace nfsu {
struct RaceGapSegment {float from=0,to=0,length=0,speed=0,raw_from=0,raw_to=0;};
// 432E80/433340: authored road speeds mapped onto 256 progress bins.
struct RaceGapMap {
    std::array<float,256> rate{},elapsed{};
    float lap_seconds=0;
    bool valid=false;
    bool build(const RaceRouteTrack&,const RaceRoute&,const RacingLineTrack&,unsigned category,
               std::vector<RaceGapSegment>* evidence=nullptr);
    float time(float progress) const noexcept;
};
struct RaceGapActor {float progress=0;bool finished=false;int lap=0;std::int32_t finish_ticks=0;};
std::int32_t race_gap_ticks(const RaceGapMap&,const RaceGapActor& row,const RaceGapActor& player,
                           std::int32_t since_go) noexcept;
std::string race_gap_text(std::int32_t ticks);
enum class RaceLeaderboardMode {gaps,names,cars};
// 4A2B10: one branch per update; phase changes take effect next update.
struct RaceLeaderboardCycle {
    RaceLeaderboardMode mode=RaceLeaderboardMode::gaps,shown=RaceLeaderboardMode::gaps;
    std::int32_t since=0;
    bool cars_next=false,initialized=false;
    void update(std::int32_t now) noexcept;
};
bool load_race_car_labels(const std::filesystem::path&,const std::array<std::string,4>&,
                          std::array<std::string,4>&,std::string&);
}
