#pragma once
#include "nfsu/ai_path.h"
#include "nfsu/ai_neighbors.h"
#include "nfsu/ai_strategy.h"

namespace nfsu {
struct AiActionOffsetInput {
    int mode=0,physics_selector=3; // actor24 owner/controller kind; not actor28
    float absolute_speed=0,race_seconds=-1;
    // Original OR of78A344,7361A8 and42AB50. The race owner resolves it.
    bool special_context=false;
};
// 45C550, including7061DC/583F20. Amount per path sample, not per second.
bool ai_action_offset_step(const AiActionOffsetInput&,float&) noexcept;
// Resolves the original action's offset policy before retained path extension.
bool ai_action_continue(const AiPathRoadPoint*,std::size_t,std::size_t selected,
                        float initial_offset,const AiActionOffsetInput&,
                        const AiPathExtension&,AiPlannedPath&) noexcept;
// Ordinary type1 maneuver45AE10: retain driver path or rebuild when strategy
// requests it. Rebuild covers original zero6B74B8 (nonzero prefix cropping is
// separate). Caller supplies strategy and45AD30 category-admission results.
bool ai_action_forward_path(const AiPlannedPath& previous_driver_path,
                            const AiPathRoadPoint*,std::size_t,std::size_t selected,
                            float initial_offset,const AiActionOffsetInput&,
                            const AiPathExtension&,float signed_speed,std::uint32_t tick,
                            const std::array<float,3>& retain_parameters,
                            AiPlannedPath& output,bool rebuild=false,bool category_available=true) noexcept;

struct AiSpeedPolicyState {std::uint32_t offset_tick=0;int point_offset=0;};
// Consumed fields of original GLOBALB chunk34B00, copied by462070 to779C78.
// The race selects a signed actor context; -1 uses row0 and suppresses jitter.
struct AiSpeedConfiguration {
    std::array<float,3> road_probabilities{};
    float maneuver_probability=0;
    float context_scale=0,offset_interval=0;
    float drag_launch_min=0,drag_launch_max=0;
    // Consumer order: UP min/max (+2C/+30), DOWN min/max (+24/+28).
    std::array<float,4> drag_shift_offsets{{-100.F,0.F,-100.F,0.F}};
    unsigned offset_amplitude=0;
};
bool ai_speed_configuration(const unsigned char* payload,std::size_t size,
                             int context,AiSpeedConfiguration&) noexcept;
struct AiSpeedPolicyInput {
    const Route* route=nullptr;
    unsigned point=0;
    int category=0,direction=0,context=0;
    std::uint32_t tick=0;
    float offset_interval=0,context_scale=1,catchup_scale=1,strategy_cap=99999;
    unsigned offset_amplitude=0; // original difficulty row77A048 byte
    const RoutePoint* marker_point=nullptr;
    bool queued_override=false,route_speed=true,enabled=true;
    bool debug_constant=false,debug_route=false;
    float action_speed=0,duration=0,auxiliary=0,debug_auxiliary=0;
};
struct AiSpeedPublication {float speed=0,duration=0,auxiliary=0;bool enabled=true;};
// 44C578..44C7EE after original catch-up update44BD10 and path publication.
// The caller owns those producers and supplies their current values, including
// selected difficulty row and strategy cap. World RNG is shared with roads.
// Refusal preserves state, RNG and output. No allocation in this update.
bool ai_action_publish_speed(const AiSpeedPolicyInput&,AiSpeedPolicyState&,
                             std::uint32_t& rng,AiSpeedPublication&) noexcept;

struct AiActionSpeedState {
    float speed=0,duration=0,auxiliary=0;
    bool route_speed=true,enabled=true,catchup=true,ready=false;
};
struct AiActionNeighborState {
    std::uint32_t id=0;
    bool race_actor=false;
    int role=0; // original race row:1 human,2 rival
    AiDriverPoint velocity{}; // refreshed original body-state velocity
    AiStrategyKind strategy=AiStrategyKind::none; // actual owner policy, not actor role
};
// Speed stage4504EF..450654 of fallback action4, AFTER its45AE10 path update.
// Tests only the immediate preceding entry in the original sorted list.
// gap_limit and desired_gap are caller-owned6B7628/6B7620, not tuning here.
// Retained speed/duration survive unless this stage lowers the speed. No
// allocation. Refusal preserves state; caller must not publish that tick.
bool ai_action_fallback_speed(const AiNeighborhood&,int category,
                              const AiActionNeighborState*,std::size_t,
                              float gap_limit,float desired_gap,AiActionSpeedState&) noexcept;

struct AiContactManeuver {int kind=1;std::uint32_t target=0;float parameter=0;bool flag=false;};
// Resolved45AE10 inputs. The owner supplies strategy/category admission and
// the target snapshot; this does not perform a fresh world query per point.
struct AiActionPathInput {
    const AiPathRoadPoint* roads=nullptr;
    std::size_t road_count=0,selected=0;
    float initial_offset=0,signed_speed=0;
    AiActionOffsetInput policy;
    AiPathExtension extension;
    AiManeuverResult previous;
    std::uint32_t tick=0,resolved_target=0;
    std::array<float,3> retain_parameters{};
    const AiManeuverInput* target=nullptr;
    bool rebuild=false,category_available=true;
};
// Ordinary/side descriptors1/2/3, zero6B74B8 rebuild domain. True means the
// update completed, not that the maneuver was admitted: output.maneuver
// carries the original AL. Unsupported descriptors or missing live inputs
// refuse transactionally. Successful1/2/3 dispatch clears controller3E8;
// that world-owner publication remains the caller's responsibility.
bool ai_action_plan_path(const AiContactManeuver&,const AiActionPathInput&,
                          AiManeuverResult&) noexcept;
struct AiContactActionState {
    std::uint32_t target=0;
    AiContactManeuver maneuver;
    AiPlannedPath path;
    AiActionSpeedState speed;
};
// 452270: query at minimum-1, racers only; refresh target while contact lasts.
bool ai_action_contact_complete(const AiNeighborhood&,const std::array<AiStrategyActor,16>&,
    const AiStrategyContact*,std::size_t,AiContactActionState&,bool& complete) noexcept;
struct AiContactActionInput {
    const AiNeighborhood* neighbors=nullptr;
    int category=0,self_role=2;
    bool target_human=false;
    AiDriverPoint target_velocity{},target_direction{},self_direction{};
    float signed_speed=0,controller_speed=0;
    // Original6B769C,6B76A0,6B76A4,6B76A8,6B76AC.
    float duration=0,human_speed_offset=0,other_speed_offset=0,gap_margin=0,minimum_speed=0;
    // Required only for road-class1 with target ahead: original actor+20
    // fields264,268,110,DC,FC and world7000AC. Kept in source units/order.
    std::array<float,6> slowdown{};
};
struct AiContactActionOps {
    bool (*forward_path)(void*,const AiContactManeuver&,AiPlannedPath&)=nullptr;
    // Original45B150 receives the descriptor by reference and may replace it.
    bool (*missing_target_path)(void*,AiContactManeuver&,AiPlannedPath&)=nullptr;
    bool (*road_class)(void*,int&)=nullptr;
};
// Complete4522A0 control flow with explicit real path/road producers. These
// callbacks must not fabricate a successful path; false terminates the tick.
// No allocation. State is published only on success; callback effects are not
// rolled back. Target refresh/completion452270 uses ai_strategy_contact:
// nonzero result replaces target; zero completes without clearing old target.
bool ai_action_contact_update(const AiContactActionInput&,const AiContactActionOps&,
                              void*,AiContactActionState&) noexcept;

struct AiOwnedAction {
    // Nonzero stable handle into the race owner's action storage. No native
    // executable address is needed. Factory owns action-specific path/state.
    std::uint32_t handle=0;
    int kind=7,priority=0;
    bool timed=false;
    unsigned status=0;
};
struct AiActionOwner {
    AiOwnedAction current;
    int requested_kind=20; // original3F4:20 is automatic selection
};
enum class AiActionPredicate {timeout,abort,complete};
struct AiActionOwnerOps {
    bool (*predicate)(void*,const AiOwnedAction&,AiActionPredicate,bool&)=nullptr;
    // kind20 invokes strategy selection, even when a candidate is discarded.
    // A successful factory may return handle0 for no candidate. It must never
    // reuse the current handle; automatic selection constructs before cleanup.
    bool (*create)(void*,int kind,const AiOwnedAction& current,AiOwnedAction&)=nullptr;
    bool (*cleanup)(void*,const AiOwnedAction&)=nullptr;
    void (*release)(void*,const AiOwnedAction&)=nullptr;
    bool (*update)(void*,const AiOwnedAction&,bool& path_ready)=nullptr;
};
// Ordinary44B94E..44B9F1 with44F700/44A0E0/44BC30/44BBD0. Initial strategy
// and its first action must already exist; debug736090 is outside this entry.
// Replacement preserves callback order, including rejected-candidate effects.
// Failure is terminal for this tick: external callback effects cannot roll
// back. Do not publish controls after false. Owner allocates no memory itself.
bool ai_action_owner_step(const AiActionOwnerOps&,void*,AiActionOwner&) noexcept;
} // namespace nfsu
