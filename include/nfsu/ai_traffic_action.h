#pragma once
#include "nfsu/ai_action.h"
#include "nfsu/ai_traffic_intersection.h"
#include "nfsu/race_route.h"

namespace nfsu {
//4420B0: raw point+84 bits25/26 admit forward/backward traffic spawning.
//Header lane availability alone also includes lanes reserved for racers.
bool ai_traffic_spawn_point_allowed(const std::array<std::uint8_t,4>& flags,bool forward) noexcept;
//599C40: callers supply a valid point index; leave ten points ahead.
bool ai_traffic_drag_spawn_margin(unsigned point,unsigned count,bool forward) noexcept;
// 599B20 / 442820 with the startup caller's zero reference progress.
AiRoadCursor ai_traffic_drag_grid_point(const RacingLineTrack&,const TrackPathSpline&,
                                      bool reverse,float target,bool forward) noexcept;
//59B3E0/59B670/59B6A0: selector1..3 chooses the zone's min/max pair.
//Absent zone or a nearby stopped car suppresses the draw, returning99999.
bool ai_traffic_spawn_spacing(const TrackPathZone*,unsigned,bool,std::uint32_t&,float&) noexcept;
//59B1E0: traffic is protected on both sides; a racer only ahead of itself.
bool ai_traffic_spawn_gap_blocked(float spawn_minus_actor,float spacing,bool traffic) noexcept;
//583350 role3 gate. Frame is global6F08A4, not this actor's physics tick.
//Slot/count refer to the world's complete role3 group. Retained controls
//continue between full updates, including after physical2->3 promotion.
bool ai_traffic_full_update(std::uint32_t frame,unsigned slot,unsigned count,
                            bool& full) noexcept;

enum class AiTrafficStrategyQuery {obstacle,avoidance};
using AiTrafficStrategyEvaluator=bool(*)(void*,AiTrafficStrategyQuery,bool&,AiActionCandidate&);
//44E020 strategy4: lazy obstacle18, avoidance3, then default8. Queries at
//equal priority still execute; the owner rejects the constructed candidate.
//The evaluator owns real geometry and reaction side effects; absence of a
//producer is failure, never an empty road. No allocation in this selector.
bool ai_traffic_strategy_choose(int current_kind,int current_priority,
    AiTrafficStrategyEvaluator,void*,AiActionCandidate&) noexcept;

struct AiTrafficActionOwner {
    AiOwnedAction current;
    unsigned physical_selector=2; // actor+28; distinct from role/controller
};
struct AiTrafficActionOwnerOps {
    bool (*predicate)(void*,const AiOwnedAction&,AiActionPredicate,bool&)=nullptr;
    bool (*choose)(void*,int kind,int priority,AiOwnedAction&)=nullptr;
    bool (*initial)(void*,AiOwnedAction&)=nullptr;
    bool (*cleanup)(void*,const AiOwnedAction&)=nullptr;
    void (*release)(void*,const AiOwnedAction&)=nullptr;
    bool (*promote)(void*)=nullptr; // real426110 construction/publication
    bool (*update)(void*,const AiOwnedAction&)=nullptr; //583850 path/speed
};
//58350D..5835DB. Unlike44B94E, even a completed current action survives
//candidate selection/construction. Accepted1/3/9 promotes selector2 before
//update. There is no requested-kind field or fallback4 path retry. Callback
//effects are not rolled back: false terminates the tick before controls are
//published. Factory storage must hold current and a distinct candidate.
bool ai_traffic_action_owner_step(const AiTrafficActionOwnerOps&,void*,
                                  AiTrafficActionOwner&) noexcept;
// Strategy4's native453920/453A60/453BA0/453C60 state. Traffic strategy's
// virtual0C is5314D0 (false); the racer-only fast-target suppression is not
// executed in this domain. Queries must be real44F470 producers.
struct AiTrafficAvoidanceState {
    std::uint32_t target=0;
    float time=0,clearance=30;
};
struct AiTrafficAvoidanceInput {
    const AiNeighborhood* neighbors=nullptr;
    int category=11;
    float absolute_speed=0,controller_cap=0;
};
struct AiTrafficAvoidanceOps {
    bool (*query)(void*,int category,float offset,float clearance,AiObstacleQueryResult&)=nullptr;
    bool (*admitted)(void*,std::uint32_t,bool&)=nullptr;
    bool (*transition)(void*,int from,int to,bool&)=nullptr;
    bool (*nearest_lane)(void*,int&)=nullptr;
    bool (*is_traffic)(void*,std::uint32_t,bool&)=nullptr;
};
struct AiTrafficAvoidanceOutput {
    int category=11;
    bool reset_maneuver=false,actor_override_enabled=false;
    std::array<float,4> actor_override{};
    AiActionSpeedState speed;
};
bool ai_traffic_avoidance_offer(const AiTrafficAvoidanceInput&,const AiTrafficAvoidanceOps&,
    void*,AiTrafficAvoidanceState&,bool& found) noexcept;
bool ai_traffic_avoidance_complete(const AiTrafficAvoidanceInput&,const AiTrafficAvoidanceOps&,
    void*,AiTrafficAvoidanceState&,bool& complete) noexcept;
bool ai_traffic_avoidance_update(const AiTrafficAvoidanceInput&,const AiTrafficAvoidanceOps&,
    void*,AiTrafficAvoidanceState&,AiTrafficAvoidanceOutput&) noexcept;
struct AiTrafficStopAction {
    int intersection=-1,mode=1;
    bool stopped=false;
};
struct AiTrafficStopCandidate {int intersection=-1,mode=0,priority=-1;};
//4543D0/454330, PC1.4 source constants: accepted mode1 still consumes the
//shared WORLD random draw. The supplied reference comes from44D570, never a
//type9 first_obstacle. Missing producer state must refuse before this call.
bool ai_traffic_stop_offer(const AiTrafficIntersectionRef&,std::uint32_t& random,
                           AiTrafficStopCandidate&) noexcept;
//454420: absence completes; unchanged intersection preserves state/RNG;
//a changed one resets stopped, selects mode and consumes its original draw.
bool ai_traffic_stop_complete(const AiTrafficIntersectionRef&,std::uint32_t& random,
                              AiTrafficStopAction&,bool& complete) noexcept;
struct AiTrafficStopInput {
    int route=-1;
    float distance=0,absolute_speed=0,controller_cap=0;
    AiObstacleQueryResult obstacle;
    bool obstacle_admitted=false; // actual44EFD0, after44F470
};
struct AiTrafficStopState {
    bool stopped=false; // action204: distance<0 and absolute speed<.5
};
struct AiTrafficStopOutput {
    AiActionSpeedState speed;
    // Actual actor88C/890..89C override. Its consumer is not classified as
    // horn/headlights; preserve the four source floats without that inference.
    bool actor_override_enabled=false;
    std::array<float,4> actor_override{};
};
//454490 control stage. Caller owns the actual first_limited intersection,
//5A6E10 distance,44F470/44EFD0 query and45AE10 path update. This kernel does
//not fabricate those producers or advance action lifetime. Shared selection,
//stop latch and output are transactional on invalid consumed input.
//Final auxiliary1 overwrites the intermediate obstacle-braking auxiliary.
//Factory454170 and complete454420 reset the latch on intersection changes.
bool ai_traffic_stop_update(const AiTrafficStopInput&,AiTrafficIntersection&,
                            AiTrafficStopState&,AiTrafficStopOutput&) noexcept;
} // namespace nfsu
