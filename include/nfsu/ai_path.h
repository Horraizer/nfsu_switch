#pragma once
#include "nfsu/ai_driver.h"
#include "nfsu/racing_line.h"

namespace nfsu {
// Compact original forward road point (vtable6C8F90). The spatial/context
// producer owns ordering, bounds and offset overrides; this layer does not
// choose a route by nearest progress-spline point.
struct AiPathRoadPoint {
    RoutePoint point;
    std::uint16_t point_index=0;
    float distance=0; // original compact chain coordinate +58
    std::array<float,7> offset{},lower{},upper{}; // ordinary, lanes11..16
};
struct AiPlannedPath {
    AiDriverPath path;
    std::array<std::uint8_t,20> route{};
    std::array<std::uint16_t,20> point{};
    std::array<std::uint8_t,20> category{};
    float length=0;
    std::uint32_t cursor_tick=0;
    unsigned cursor=0;
    std::uint32_t prediction_tick=0; // original path+1A8, independent cache
};
// Driver virtual3C:45FAF0 copies the complete action path, then459980
// redistributes its existing point count uniformly by arc length. Route
// metadata is retained, while geometry cursor/prediction caches are cleared
// for paths with more than two points. Refusal preserves the caller's path.
bool ai_path_publish(AiPlannedPath&) noexcept;
struct AiPathExtension {
    int category=0,mode=0;
    float offset=0,offset_step=0,target_offset=0,clearance=0,margin=1;
    AiDriverPoint actor_position{};
    AiDriverPoint actor_velocity{},actor_forward{1,0,0};
    float absolute_speed=0;
};
// 5A7960/5A78B0 forward bounds for the selected ordinary category and lanes.
bool ai_path_road_bounds(AiPathRoadPoint&,int category) noexcept;
// 459710 alone: refresh cached cursor without trimming geometry.
bool ai_path_cursor(AiPlannedPath&,float signed_speed,const AiDriverPoint& position,
                    const AiDriverPoint& velocity,std::uint32_t tick,
                    float minimum_distance_squared,float fast_dot,float slow_dot) noexcept;
struct AiPathFitnessInput {
    AiDriverPoint position{},velocity{};
    float signed_speed=0,absolute_speed=0;
    std::uint32_t tick=0;
    std::array<float,3> retain_parameters{};
    // Current original globals6B7528/6B752C/6B7530, supplied by configuration.
    float minimum_time=0,initial_time=0,lateral_acceleration_limit=0;
    std::uint8_t transition_end=255; // original path+1B0
};
// 459B30/459F70/45A000/45A0B0. Returns the original score (-1 for rejected
// geometry), not obstacle clearance or a chosen action. Only cursor cache is
// updated. False refuses unsupported/nonfinite state, preserving all outputs.
// Short tail segments end acceleration checks at the source count boundary;
// unused speculative stack/point reads do not enter the weighted score.
bool ai_path_fitness(AiPlannedPath&,const AiPathFitnessInput&,float& score) noexcept;
// 459710/45A7D0/45A8C0: select first usable retained point and trim the prefix.
// minimum_distance_squared is the original live global77A4A8, passed by owner.
bool ai_path_retain(AiPlannedPath&,float signed_speed,const AiDriverPoint& position,
                    const AiDriverPoint& velocity,std::uint32_t tick,
                    float minimum_distance_squared,float fast_dot,float slow_dot) noexcept;
struct AiPathPredictionInput {
    AiDriverPoint position{},velocity{},acceleration{};
    unsigned first=0;
    std::uint32_t tick=0;
    float horizon=0; // current original6B7538
};
// 45A5B0: blend the retained intended path with current accelerated motion,
// once per world tick. This is input geometry for collision prediction; it
// neither moves a vehicle nor resolves contact. Transactional on refusal.
bool ai_path_predict(AiPlannedPath&,const AiPathPredictionInput&) noexcept;
// 45C370 forward retained-path extension. The road-context owner supplies its
// selected point and lateral offset for an empty path. For a retained path,
// 5A6150/5A7390 select the next cell and recover the offset from its last point.
// offset_step must already be resolved by the action policy (45C550).
bool ai_path_continue(const AiPathRoadPoint* roads,std::size_t count,
                      std::size_t selected,float initial_offset,
                      const AiPathExtension&,AiPlannedPath&) noexcept;
// 45C640/5A7270/455160/4422B0/4594D0 forward chain emission. Inputs are
// already selected road records, beginning at the original chain cursor.
// No allocation, executable dependency, clock or filesystem in this path.
bool ai_path_extend(const AiPathRoadPoint* roads,std::size_t count,
                    const AiPathExtension&,AiPlannedPath&) noexcept;
//57A530 type6 / ordinary human owner1:456D50 publishes a body forecast.
//57A820 consumes the retained owner magnitude and59C1A0 horizon2, not a
//new body-speed measurement. Drag owner2 and finished owner3 copy Driver.
bool ai_human_world_prediction(const AiDriverPoint& position,const AiDriverPoint& velocity,
    const AiDriverPoint& forward,float published_magnitude,AiPlannedPath&) noexcept;
//45A2F0 with its456DE0 fallback. Source interval6B7534 is.1; horizontal
//acceleration chooses sampled motion or the straight fallback. Samples use
//4594D0's no-actor admission, including short-step rejection. This primitive
//does not choose the role/action/raw-road-boundary branch in4576B0.
bool ai_motion_prediction(const AiDriverPoint& position,const AiDriverPoint& velocity,
    const AiDriverPoint& acceleration,std::uint16_t heading,float maximum_length,
    AiPlannedPath&) noexcept;
// Original5A6310: transition from self's next cell toward target cell-2,
// emitting a lateral maneuver within road bounds. Offset interpolation and
// per-point clipping deliberately follow source order. clipped is the retained
// original path1B1 flag; it is not permission to discard an obstructed path.
bool ai_path_transition(const AiPathRoadPoint* roads,std::size_t count,
                         int start_cell,int target_cell,float from,float to,
                         float half_width,bool extra_margin,const AiPathExtension&,
                         AiPlannedPath&,bool& clipped) noexcept;
struct AiManeuverVehicle {
    AiDriverPoint position{},velocity{};
    float lateral=0,half_width=0,distance=0;
    int cell=0,role=0;
    bool racer=false,alternate_controller=false;
};
struct AiManeuverInput {
    AiManeuverVehicle self,target;
    int side=2; // original descriptor2/3: negative/positive side
    bool extra_clearance=false;
    // Original6B74F8,6B74FC,6B7500,6B7504,6B7508,6B750C,6B7510,6B74F4.
    std::array<float,8> policy{};
    float self_signed_speed=0;
    // Current4596B0 cursor, resolved by the target path's owner before this
    // consumer. It must refer to the same refreshed target body/path epoch.
    AiPlannedPath target_path;
    unsigned target_cursor=0;
};
struct AiManeuverResult {
    AiPlannedPath path;
    bool maneuver=false,clipped=false;
    std::uint8_t transition_end=0;
};
// 45C760/45CEA0/45CB00 on resolved original compact context and fresh bodies.
// No closest-route substitution, query hook, allocation or executable at run
// time. Missing target/context refuses; target cursor production is separate.
bool ai_path_maneuver(const AiPathRoadPoint*,std::size_t,const AiPathExtension&,
                      const AiManeuverInput&,AiManeuverResult&) noexcept;
} // namespace nfsu
