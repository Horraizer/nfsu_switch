#pragma once
#include "nfsu/race_session.h"
#include "nfsu/ai_road.h"
#include <memory>
namespace nfsu {
struct AiPathCollisionWorld;
// Borrowed only during read(): decision input cannot replace AI state.
struct IntroAiWorldObserver {
    void* context=nullptr;
    void(*read)(void*,const AiPathCollisionWorld&)=nullptr;
};
// Read-only retained controller state for source replay/diagnostics. The action
// path is the raw retained path, not the resampled publication sent to Driver.
struct IntroDragSnapshot {
    DragLaneState lane{};
    AiRoadContext road{};
    AiRoadHorizons horizons{};
    AiPlannedPath action{};
};
struct IntroRailSnapshot {
    WorldObstacleBinding binding;WorldObstacleStatus status;
    int action=0;unsigned reads=0;
};
// Shared PC/Switch ordinary UG0 adapter. Recovered road/path/driver components;
// Catch-up uses actual race progress/rank and retained human speed history.
// Original ordinary action owner/strategy; road-seeded human intent is a port adapter.
// Loads once; owns decoded roads and copied hull bounds. Reset callbacks borrow
// the plan's RaceSession until its physics/placement phase completes.
class IntroAiPlanner {
public:
    IntroAiPlanner();
    ~IntroAiPlanner();
    void set_recovery_trace(bool);
    // Borrowed by main-thread world services for this planner's lifetime.
    std::uint32_t* world_random()noexcept;
    bool load(const std::filesystem::path&,const std::array<int,3>&,
              const std::array<const VehicleHull*,4>&,bool retail_speed=true,
              unsigned track_number=1003,int direction=0,bool drag=false,float catchup_weight=0,int laps=2,bool point_to_point=false,unsigned traffic_selector=0,bool catchup_enabled=true,
              int decision_start=0,bool limit_career_pace=false);
    bool camera_road(const AiRoadContextInput&,const AiRoadPolicyInput&,AiRoadContext&)const;
    bool drag_human_snapshot(IntroDragSnapshot&)const;
    bool human_road_cursor(AiRoadCursor&)const noexcept;
    bool rail_snapshot(unsigned rival,IntroRailSnapshot&)const;
    bool observe_human(const RaceSession&);
    bool plan(RaceSession&,RaceRivalPlans&,int held_lane=0,const RaceSession* obstacle_world=nullptr);
    bool plan(RaceSession&,RaceRivalPlans&,int held_lane,const RaceSession*,IntroAiWorldObserver);
    // Diagnostic controller on a read-only world; never advances its solvers.
    bool inspect_plan(const RaceSession&,RaceRivalPlans&,int held_lane=0,
                      const RaceSession* obstacle_world=nullptr,IntroAiWorldObserver observer={});
    std::string difficulty_line() const;
    unsigned evaluations=0,pairs=0,side_choices=0,fallbacks=0;
    std::array<float,3> minimum_speed{{1e9F,1e9F,1e9F}},maximum_speed{};
private:
    class State;
    std::unique_ptr<State> state_;
    bool trace_recovery_=false;
};
}
