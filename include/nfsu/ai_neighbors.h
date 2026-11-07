#pragma once
#include "nfsu/ai_road.h"
#include "nfsu/ai_collision_prediction.h"
#include <cstddef>

namespace nfsu {
// Published world-node snapshot, in original registration order. The caller
// resolves shape bounds and refreshes pose/velocity before this update.
struct AiNeighborActor {
    std::uint32_t id=0;
    AiDriverPoint position{},velocity{},forward{};
    float progress=0;
    std::array<float,2> bounds_min{},bounds_max{};
    bool enabled=true;
};
struct AiNeighborInput {
    std::uint32_t self_id=0;
    AiDriverPoint position{},velocity{},forward{},acceleration{},projection_vector{};
    // Original57A5F0 passes global7372A0 to429C50. The caller supplies that
    // vector; it is not inferred from car acceleration or track inclination.
    std::uint16_t heading=0;
    float progress=0,horizon=0,track_length=0;
    int category=0,ride_category=0;
    bool circuit=false;
};
struct AiNeighborEntry {
    std::array<float,2> relative_velocity{},half_extent{};
    float distance=0,lateral=0;
    std::array<float,6> lane_lateral{};
    unsigned cell=0;
    std::uint32_t id=0;
};
struct AiNeighborhood {
    std::array<AiNeighborEntry,16> entries{};
    unsigned count=0;
    int self_index=-1;
};
// Original57A910, ordinary forward road context. Includes4569B0 predictive
// admission and stable descending relevance order. Fixed scratch/capacity,
// no allocation; false leaves output unchanged. Not an action selector.
bool ai_neighbors(const AiRoadChain&,const AiNeighborInput&,
                  const AiNeighborActor*,std::size_t,AiNeighborhood&) noexcept;
struct AiObstacleQueryInput {int category=0;float offset=0,clearance=0,absolute_speed=0;};
struct AiObstacleQueryResult {std::uint32_t id=0;float time=99999,distance=99999;};
using AiPhysicalObstacleQuery=bool (*)(void*,std::uint32_t,AiPhysicalObstacleResult&) noexcept;
//44F470 combines each admitted road neighbor with its actual44F1B0 query.
// Visits entries immediately before self in reverse order; neither a finite
// corridor nor minimum-time sorting substitutes for that retained ordering.
// Callback refusal is unsupported, never a clear-path result.
bool ai_obstacle_query(const AiNeighborhood&,const AiObstacleQueryInput&,
                        AiPhysicalObstacleQuery,void*,AiObstacleQueryResult&) noexcept;
} // namespace nfsu
