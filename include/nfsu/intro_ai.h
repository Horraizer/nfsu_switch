#pragma once
#include "nfsu/race_session.h"
#include "nfsu/ai_road.h"
#include <memory>
namespace nfsu {
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
// Loads once; owns its decoded roads and copied hull bounds. No actor pointers.
class IntroAiPlanner {
public:
    IntroAiPlanner();
    ~IntroAiPlanner();
    void set_recovery_trace(bool);
    // Borrowed by main-thread world services for this planner's lifetime.
    std::uint32_t* world_random()noexcept;
    bool load(const std::filesystem::path&,const std::array<int,3>&,
              const std::array<const VehicleHull*,4>&,bool retail_speed=true,
              unsigned track_number=1003,int direction=0,bool drag=false,float catchup_weight=0,int laps=2,bool point_to_point=false,unsigned traffic_selector=0,bool catchup_enabled=true);
    bool camera_road(AiRoadChain&,int&)const;
    bool drag_human_snapshot(IntroDragSnapshot&)const;
    bool rail_snapshot(unsigned rival,IntroRailSnapshot&)const;
    bool observe_human(const RaceSession&);
    bool plan(const RaceSession&,RaceRivalPlans&,int held_lane=0);
    std::string difficulty_line() const;
    unsigned evaluations=0,pairs=0,side_choices=0,fallbacks=0;
    std::array<float,3> minimum_speed{{1e9F,1e9F,1e9F}},maximum_speed{};
private:
    class State;
    std::unique_ptr<State> state_;
    bool trace_recovery_=false;
};
}
