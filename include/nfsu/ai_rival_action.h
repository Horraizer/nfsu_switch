#pragma once
#include "nfsu/ai_action.h"
#include "nfsu/ai_path_choice.h"
#include "nfsu/ai_recovery.h"
#include "nfsu/world_motion.h"

namespace nfsu {
// One actual controller epoch. All pointers are borrowed for step() only.
// Neighbor metadata is indexed in the same order as the original sorted list.
struct AiRivalActionInput {
    const AiPathPlannerInput* planner=nullptr;
    const AiPathCollisionWorld* world=nullptr;
    const AiNeighborhood* neighbors=nullptr;
    const AiRoadChain* road=nullptr;
    std::array<AiStrategyActor,16> actors{};
    std::array<AiActionNeighborState,16> motion{};
    const AiStrategyContact* contacts=nullptr;
    std::size_t contact_count=0;
    const AiRecoveryInput* recovery=nullptr;
    const RoutePoint* recovery_ahead=nullptr;
    const AiPathRoadPoint* recovery_selected=nullptr;
    std::array<float,6> slowdown{};
    AiDriverPoint up{0,0,1};
    std::uint32_t tick=0;
    std::uint32_t* random=nullptr;
    float race_seconds=0,target_probability=0;
    float recovery_hold_seconds=1.3F;
    int ride_category=0,mode_priority=200;
    unsigned support=4;
    bool hold=false,finished=false,subtract_lengths=true,road_reset=false;
    bool obstacle_available=false,obstacle_passed=false,obstacle_blocked=false;
    bool traffic_continuation=true,force_traffic_action=false; //78A344 or7361A8
    bool drag_strategy=false,wall_contact=false;
    int nearest_lane=-1;
    int nearest_owner_lane=-1; //453600 uses owner's neighborhood raw point
    AiLaneTransitionInput lane_transition;
    const AiRoadFlags* lane_flags=nullptr;
    bool obstacle_marker_present=false,obstacle_marker_animation=false;
    void* obstacle_context=nullptr;
    bool(*obstacle_read)(void*,WorldObstacleStatus&)=nullptr;
    void(*obstacle_cleanup)(void*)=nullptr;
    //44C250 updates raw road/category in place, preserving this epoch's
    //compact chain and neighbors. Query and cleanup consume it immediately.
    void* road_context=nullptr;
    bool(*reset_road)(void*)=nullptr;
    //45031B/45036D/4504C6 write the category synchronously. This updates
    //category-dependent views without rebuilding roads or sorting neighbors.
    void* category_context=nullptr;
    bool(*set_category)(void*,int)=nullptr;
};
struct AiRivalActionOutput {
    AiOwnedAction action;
    AiPlannedPath path;
    AiActionSpeedState speed;
    AiContactManeuver maneuver;
    std::uint32_t target=0;
    bool clear_road=false,reset_driver=false,rollover_reset=false;
    bool clear_driver_brakes=false; //453830, cleanup of action17 only
    bool path_published=false; // do not resample an unchanged driver path
    bool check_nitrous=false; // this action update reached its request producer
    int lane_category=-1; // an accepted4528B0/453600 lane publication
    unsigned evaluations=0,pair_queries=0;
};
// Strategies1/2 and the44B94E action owner share retained storage. The
//explicit strategy input preserves their distinct query order and actions.
class AiRivalActionController {
public:
    // 44B831 creates the initial action before road admission. No geometry,
    // predicates or random draws are performed by this initialization.
    bool initialize(std::uint32_t tick,bool drag) noexcept;
    //44BAF0 action virtual10, with an empty neighborhood on raw reselection.
    AiRoadCategoryProbe road_category_probe(bool force_traffic_action) const noexcept;
    bool road_requires_lane(bool force_traffic_action) const noexcept;
    //44BAF0 with retained neighbors: probe without publishing a Driver path
    //or replacing the current descriptor. Collision-ignore is not restored.
    bool reselect_category(const AiRivalActionInput&,int&) noexcept;
    // 44D337 changes the retained descriptor even if road admission stops.
    void reset_road_maneuver() noexcept;
    bool step(const AiRivalActionInput&,AiRivalActionOutput&) noexcept;
    // Between full44B1B0 decisions: update blocking5, or the retained side
    // maneuver targeting a present human. Never run predicates or factory.
    // Other actions keep their previous driver publications unchanged.
    bool refresh(const AiRivalActionInput&,AiRivalActionOutput&) noexcept;
    const AiOwnedAction& current() const noexcept {return owner_.current;}
    const AiContactManeuver& maneuver() const noexcept {return maneuver_;}
private:
    struct Record {
        AiOwnedAction owned;
        AiContactActionState state;
        std::uint32_t epoch=0,last_update=0,start=0;
        float duration=-1,collision_time=99999,complete_after=0;
        float recovery_hold_seconds=-1;
        bool found=false,side_admitted=false,reset=false,forward_initialized=false,obstacle_marker=false;
        bool check_nitrous=false;
    };
    std::array<Record,2> records_{};
    AiActionOwner owner_;
    // 44CE90 starts without an admitted maneuver. The adjacent-road reset
    // is different: it installs forward kind1.
    AiContactManeuver maneuver_{0,0,0,false};
    AiManeuverGate gate_;
    AiReactionState reaction_;
    std::uint32_t next_handle_=1,last_recovery_=0,collision_ignore_=0;
    const AiRivalActionInput* input_=nullptr;
    AiRivalActionOutput pending_;
    AiActionCandidate candidate_;
    AiPathChoiceResult candidate_path_;
    float candidate_time_=99999;
    Record* find(std::uint32_t) noexcept;
    const AiNeighborEntry* neighbor(std::uint32_t) const noexcept;
    const AiActionNeighborState* motion(std::uint32_t) const noexcept;
    bool path(const AiContactManeuver&,AiManeuverResult&,int mode=0);
    bool evaluate(AiPathCandidate&);
    bool choose(AiPathChoiceResult&);
    bool target_path(std::uint32_t,AiPathChoiceResult&);
    void publish_collision_ignore(const AiPathCandidate&) noexcept;
    bool collision(std::uint32_t&,float&) const;
    bool room(const AiContactManeuver&) const;
    bool predictive(float,float) const;
    bool following(AiActionCandidate&);
    bool react(std::uint32_t);
    bool blocked(const AiNeighborEntry&,AiActionSpeedState&,float) const;
    bool offset_path(const AiNeighborEntry*,AiPathCandidate&,bool&);
    bool construct(const AiActionCandidate&,float,AiOwnedAction&);
    bool update_record(Record&);
    bool restore_ride_category();
    bool read_obstacle(WorldObstacleStatus&)const;
    bool drag_forward_path(AiManeuverResult&,bool refresh);
    bool drag_avoidance_path(const AiNeighborEntry&,AiManeuverResult&,bool& clear);
    bool predicates(Record&,AiActionPredicate,bool&);
    static bool query(void*,AiStrategyQuery,bool&,AiActionCandidate&);
    static bool predicate(void*,const AiOwnedAction&,AiActionPredicate,bool&);
    static bool create(void*,int,const AiOwnedAction&,AiOwnedAction&);
    static bool cleanup(void*,const AiOwnedAction&);
    static void release(void*,const AiOwnedAction&);
    static bool update(void*,const AiOwnedAction&,bool&);
    static bool contact_path(void*,const AiContactManeuver&,AiPlannedPath&);
    static bool contact_class(void*,int&);
};
} // namespace nfsu
