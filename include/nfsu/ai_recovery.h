#pragma once
#include "nfsu/ai_driver.h"
#include "nfsu/ai_path.h"
#include "nfsu/racing_line.h"
#include <cstddef>

namespace nfsu {
struct AiRecoveryMotion {
    // Actor+70 movement and actor+90 body direction are distinct producers.
    std::array<float,2> position{},prediction_vector{},body_direction{},target{};
    float signed_speed=0,absolute_speed=0;
    std::uint16_t heading=0;
};
// Original44E9C0, including its4556D0 target-direction override. Return class
// 0 means no recovery,1 reverse/recover,2 reverse movement not along the road.
// RoutePoint widths are the original+24/+28 fields, not the visual pavement.
bool ai_recovery_road_class(const RoutePoint&,const AiRecoveryMotion&,bool target_override,int&) noexcept;
// Original464F20 movement-based matrix cache. Actor publication owns
// invalidation: set valid=false when the original actor refreshes its cache.
struct AiRecoveryContactBasis {
    std::array<AiDriverPoint,3> rows{};
    bool valid=false;
};
// Original4650D0; contact/position are worldXY, movement is actor+70 XYZ.
// Reuses a valid basis even if motion changed. No physics quaternion shortcut.
// Refusal preserves both the cache and output.
bool ai_recovery_contact_local(const std::array<float,2>& contact,
    const std::array<float,2>& position,const AiDriverPoint& movement,
    float signed_speed,std::uint16_t heading,AiRecoveryContactBasis&,
    std::array<float,2>& result) noexcept;
// Original44ECD0 after4650D0's local contact projection, with native hull bounds.
int ai_recovery_contact_class(const std::array<float,2>& local,float minimum_x,float maximum_x) noexcept;
struct AiRecoveryContact {std::array<float,2> local{};std::uint32_t created_tick=0;};
struct AiRecoveryInput {
    bool race_time_hold=false,selected_road=true,supported=true;
    int physics_selector=3;
    std::uint32_t tick=0;
    float minimum_x=0,maximum_x=0;
    AiRecoveryMotion motion;
    const RoutePoint* road=nullptr;
    const AiRecoveryContact* contacts=nullptr;
    std::size_t contact_count=0;
};
struct AiRecoveryRequest {bool requested=false,clear_road=false;};
struct AiRecoveryHistory {
    std::array<float,5> samples{};
    float sum=0,average=0;
    unsigned count=0,cursor=0;
    std::int32_t timer=6000;
};
//462430/586640/584270: sample filtered fractional race speed every1.5s.
bool ai_recovery_history_step(float dt,float fractional_speed,float path_length,AiRecoveryHistory&) noexcept;
//4338C0's stored rate and462AE0's minimum recovery hold.
bool ai_recovery_progress_rate(float now,float previous,float dt,float& rate) noexcept;
float ai_recovery_hold(const RoutePoint*,const std::array<float,2>&,const AiRecoveryHistory&) noexcept;
// Original451B00. A requested recovery calls the controller road-clear entry;
// returned clear_road is that command, to apply before constructing action10.
bool ai_recovery_request(const AiRecoveryInput&,AiRecoveryRequest&) noexcept;
//435CA0: the recovery decision uses the race clock and original actor ID.
bool ai_recovery_race_hold(std::uint32_t tick,std::uint32_t race_start,int actor_id,
                           int track,bool bypass) noexcept;
//451D50/451CB0 use raw point +3 and fall back to the last point on a null
//result. Apply that fallback to an out-of-range lookahead in the port.
const RoutePoint& ai_recovery_lookahead(const Route&,std::size_t selected) noexcept;
// Controller462AA0 cooldown; update last_recovery only on action construction.
bool ai_recovery_available(std::uint32_t tick,std::uint32_t last_recovery) noexcept;
struct AiRecoveryLifetime {
    // Constructor4519F0 start and base44A080 timeout epoch, respectively.
    std::uint32_t start_tick=0,timeout_tick=0;
    float hold_seconds=-1,duration=5;
};
struct AiRecoveryExit {
    unsigned status=0; // 44F700: normal1 overrides abort2 overrides timeout3.
    bool clear_road=false,reset_driver=false,clear_actor_recovery=false;
};
// 451DC0/451F10/454C50 and their actual44F700 precedence. lookahead is the
// original selected raw point+3; caller must supply it without a guessed wrap.
bool ai_recovery_finish(const AiRecoveryLifetime&,const AiRecoveryInput&,
                        const RoutePoint& lookahead,int category,AiRecoveryExit&) noexcept;
struct AiRecoveryPlan {
    AiPlannedPath path;
    float speed=0;
    bool enabled=false,route_speed=false,catchup=false;
};
// 452070: fresh path starts at451D50 lookahead, carrying the SELECTED point's
// metadata; then the existing45C370 chain extends it. Publication disables
// route speed/catchup and requests negative speed. No physical controls here.
bool ai_recovery_plan(const RoutePoint& lookahead,const AiPathRoadPoint& selected,
                      const AiPathRoadPoint* chain,std::size_t count,
                      const AiPathExtension&,int controller_kind,AiRecoveryPlan&) noexcept;
} // namespace nfsu
