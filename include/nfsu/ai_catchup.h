#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace nfsu {
struct AiCatchupPoint {float x=0,y=0;};
// Initialized original tables selected by event length/type, difficulty and
// reference-player rank. These are configuration inputs, not new tuning.
struct AiCatchupTuning {
    std::array<AiCatchupPoint,11> lower{},upper{};
    std::array<float,16> rank_lower{},rank_upper{};
    std::array<float,2> history_below{},history_above{};
    float interval=0,grace=0,maximum_gap=0,difficulty_scale=0;
};
struct AiCatchupInput {
    std::uint32_t tick=0,target_id=0;
    std::int32_t elapsed_ticks=0;
    int difficulty=0,self_rank=0,target_rank=0,human_count=1;
    bool has_race=true,enabled=true,finished=false,special_context=false;
    bool action_allows=true,suppressed=false,comparison_enabled=true,wrap_progress=false;
    float self_progress=0,target_progress=0,route_length=0,self_speed=0,target_speed=0;
    float target_weight=0,target_history=0;
};
struct AiCatchupState {
    std::uint32_t updated_tick=0,target_id=0;
    float speed_scale=1,physical_scale=1;
};
// Original44BD10/44C010, ordinary single-human reference ownership. Multiple
// human reference selection is not implemented and refuses when needed.
// No allocation, RNG draw or file access. Refusal leaves state unchanged.
bool ai_catchup_update(const AiCatchupInput&,const AiCatchupTuning&,AiCatchupState&) noexcept;
// Original GLOBALB34B00 selection: length, traffic density, context and rank.
//44C0E0..44C101 chooses the alternate bank for traffic selectors2/3.
bool ai_catchup_tuning(const unsigned char*,std::size_t,int context,int laps,
    bool point_to_point,unsigned traffic_selector,unsigned target_rank,AiCatchupTuning&) noexcept;
struct AiCatchupHistory {
    std::array<float,10> samples{};
    unsigned count=0,cursor=0;
    float sum=0,average=0;
    std::int32_t timer=0;
};
//430758/584270: actual human speed versus the authored category's road speed.
bool ai_catchup_history_step(float dt,float speed,float road_speed,bool road_enabled,
    bool has_road,AiCatchupHistory&) noexcept;
} // namespace nfsu
