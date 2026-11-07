#pragma once
#include "nfsu/ai_action.h"
#include "nfsu/ai_collision_prediction.h"
#include "nfsu/race_limits.h"
namespace nfsu {
constexpr unsigned kAiPathWorldActors=1+kRaceRivalCapacity+kRaceTrafficCapacity;
struct AiPathActorSnapshot {
    std::uint32_t id=0;
    AiPlannedPath path;
    AiDriverPoint position{},velocity{},acceleration{},forward{1,0,0},bound_min{},bound_max{};
    std::uint16_t heading=0;
    float signed_speed=0,absolute_speed=0; // retained Actor+3D0/+3D4, not fresh body norm
    int role=2,action=4,selector=3;
    bool traffic_prediction_ready=false;
};
struct AiPathCollisionWorld {
    std::array<AiPathActorSnapshot,kAiPathWorldActors> actors{};
    unsigned count=0,self=0;
    // Original457590 order, after456780 eligibility; self is not eligible.
    std::array<unsigned,16> eligible{};
    unsigned eligible_count=0;
    std::uint32_t tick=0;
    std::array<float,3> retain_parameters{};
    float prediction_horizon=0,normal_scale=0,human_scale=0,slow_scale=0,slow_speed=0;
    mutable unsigned pair_queries=0;
    bool road_selected=true; // same-epoch original context38C, not path nonemptiness
};
//456DE0 straight fallback, with length=max(50,speed*4). This is not the
//complete45A250/45A2F0 or4576B0 role/action/road-boundary producer.
//Retained for the isolated456DE0 contract; world traffic uses the full caller.
bool ai_traffic_prediction(const AiDriverPoint&,const AiDriverPoint&,std::uint16_t,AiPlannedPath&) noexcept;
//4576B0 role3 producer: retain the Driver path inside its road, apply45A510
//except action7, and use45A250's zero-acceleration forecast near/outside edge.
// outside is the actual44ACB0 selected raw-road-point result.
bool ai_traffic_world_prediction(const AiPathActorSnapshot&,const AiPlannedPath& driver_path,
    float outside,unsigned action,std::uint32_t tick,float horizon,AiPlannedPath&) noexcept;

struct AiPathSelectionInput {
    std::array<std::uint32_t,16> ids{}; // already admitted456780 order
    unsigned count=0;
    std::uint32_t ignored=0;
    std::uint8_t path_count=0;
    float absolute_speed=0;
    bool road_selected=true;
};
struct AiPathSelectionResult {
    std::uint32_t target=0;
    float time=99999;
    bool time_written=false;
    unsigned visits=0,queries=0; // distinct original eligible visits / pair calls
};
using AiPathPairEvaluator=bool(*)(void*,unsigned ordinal,bool& hit,float& time) noexcept;
//457590 caller only. Equal pair times preserve the FIRST actor. Known empty
//eligible lists write99999; absent roads/empty paths leave caller time alone.
//Pair/eligibility production are separate owners. Callback refusal terminates
//without publishing result; callback side effects are not rolled back.
bool ai_path_select_collision(const AiPathSelectionInput&,AiPathPairEvaluator,void*,
                               AiPathSelectionResult&) noexcept;
//457590 + ordinary racer/human domain of4576B0, through existing original
// cursor, prediction, shape and overlap code. Traffic must carry an explicitly
// prepared ordinary prediction; reverse recovery uses456CE0/456D50.
// Input actor snapshots stay frozen.
bool ai_path_collision_query(const void*,const AiPlannedPath&,std::uint32_t ignored,
                             float horizon,std::uint32_t& obstacle,float& time);
struct AiPathCandidate {
    AiContactManeuver descriptor;
    AiManeuverResult geometry;
    bool side_admitted=false,accepted=false;
    float score=-1,collision_time=99999;
    std::uint32_t obstacle=0;
};
struct AiPathCollisionQuery {
    const void* context=nullptr;
    //457590 owner supplies same-epoch world snapshots and original eligibility.
    // A refused query MUST return false, never a fabricated clear path.
    bool (*run)(const void*,const AiPlannedPath&,std::uint32_t ignored,
                float horizon,std::uint32_t& obstacle,float& time)=nullptr;
};
//45B050: original geometry -> fitness -> side score/publication -> collision.
// The query receives the temporary3E8 target; the final world publication is
// still45BFB0's responsibility. False preserves candidate.
bool ai_path_evaluate(const AiActionPathInput&,const AiPathFitnessInput&,
    const AiPathCollisionQuery&,float side_score_scale,float horizon,
    AiPathCandidate&) noexcept;
struct AiPathChoiceNeighbor {
    std::uint32_t id=0;
    bool eligible=false; // actual456780 result
    unsigned sides=0; // actual45C0D0:0 neither,1 both,2 left,3 right
    float velocity_dot=0; // actual refreshed horizontal velocities
};
struct AiPathChoiceInput {
    AiContactManeuver current;
    bool current_available=false,try_current=true;
    float horizon=0;
    std::array<AiPathChoiceNeighbor,16> neighbors{};
    unsigned count=0;
};
struct AiPathChoiceOps {
    bool (*evaluate)(void*,AiPathCandidate&)=nullptr;
    bool (*available)(void*,const AiContactManeuver&,bool&)=nullptr;
    bool (*fallback)(void*,AiPlannedPath&)=nullptr;
};
struct AiPathChoiceResult {
    AiPathCandidate selected;
    bool found=false; // original45B150 AL, not path nonemptiness
    unsigned evaluations=0;
};
//45B150/45B900: original candidate order, tie preference, failed retained-path
// backup and final fallback. Callbacks resolve existing geometry/world inputs;
// no nearest-route replacement or invented side-admission policy.45BFB0 final
// target publication is separate. No allocation; false preserves result.
bool ai_path_choose(const AiPathChoiceInput&,const AiPathChoiceOps&,void*,
                    AiPathChoiceResult&) noexcept;
//45BAE0: following one target uses a distinct ordered search and retained
//fallback. It is not the all-neighbor45B150 best-score search.
struct AiTargetPathChoiceInput {
    AiContactManeuver current;
    std::uint32_t target=0;
    unsigned sides=0;
    bool current_available=false,fast=false,try_current=true;
};
struct AiTargetPathChoiceOps {
    bool (*evaluate)(void*,AiPathCandidate&)=nullptr;
    bool (*available)(void*,const AiContactManeuver&,bool&)=nullptr;
    bool (*room)(void*,const AiContactManeuver&,bool&)=nullptr;
    bool (*fallback)(void*,const AiContactManeuver&,AiPlannedPath&)=nullptr;
};
bool ai_path_choose_target(const AiTargetPathChoiceInput&,const AiTargetPathChoiceOps&,
                           void*,AiPathChoiceResult&) noexcept;
struct AiPathPlannerInput {
    AiPathChoiceInput choice;
    AiActionPathInput path;
    AiPathFitnessInput fitness;
    std::array<AiManeuverInput,16> targets{};
    std::array<std::uint32_t,16> target_ids{};
    std::array<bool,16> target_available{};
    unsigned target_count=0;
    float side_score_scale=0,fallback_margin=0;
};
// Connect45B150's choice to45AE10 geometry,459B30 fitness and457590 collision
// on one frozen world frame. Context, eligibility and strategy are supplied
// by their existing owners. No synthetic score or clear-path callback here.
bool ai_plan_path(const AiPathPlannerInput&,const AiPathCollisionWorld&,
                  AiPathChoiceResult&) noexcept;
// Ordinary racer456780/45C0D0. Globals remain configuration inputs.
struct AiPathNeighborPolicy {
    float eligibility_padding=0,width_margin=0,center_limit=0,lateral_limit=0;
    float distant_limit=0,gap_margin=0,curvature_limit=0;
};
bool ai_path_neighbor_policy(const AiRoadChain&,const AiNeighborhood&,unsigned target,
    int category,const AiPathNeighborPolicy&,bool& eligible,unsigned& sides) noexcept;
// All three decisions read exactly the same immutable actor/path publication.
// No result is published on partial failure. The caller advances physics only
// after this batch, and owns action45BFB0's separate target publication.
bool ai_plan_three_paths(const std::array<AiPathPlannerInput,3>&,
    const AiPathCollisionWorld&,const std::array<unsigned,3>& self,
    const std::array<std::array<unsigned,16>,3>& eligible,
    const std::array<unsigned,3>& eligible_count,
    std::array<AiPathChoiceResult,3>&,unsigned& pair_queries) noexcept;
} // namespace nfsu
