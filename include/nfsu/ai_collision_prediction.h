#pragma once
#include <array>
#include "nfsu/ai_driver.h"
namespace nfsu {
// Original path node458750: horizontal axes at10/20 and extents40/44.
// The trajectory/shape producer supplies these; no axis or shape is invented.
struct AiPredictionBox {
    std::array<std::array<float,2>,2> axes{{{{1,0}},{{0,1}}}};
    std::array<float,2> extent{};
};
//458750/4586A0/4584F0. Relative position/velocity are second minus first.
// Preserves original interval/sentinel behavior: positive entry time, zero
// for an interval strictly spanning now,99999 for rejected intersection.
// A stationary separated axis contributes the source9999 sentinel interval;
// the caller must apply its actual horizon (not treat every finite time as hit).
// No horizon cap or contact impulse is applied here. False preserves output.
bool ai_prediction_overlap(const AiPredictionBox& first,const AiPredictionBox& second,
    const std::array<float,2>& relative_position,const std::array<float,2>& relative_velocity,
    float& time) noexcept;
struct AiPredictionNode {
    AiDriverPoint position{};
    AiPredictionBox box;
    float height_extent=0;
};
struct AiPredictionPath {
    std::array<AiPredictionNode,20> nodes{};
    unsigned count=0;
};
//458340 and564BD0: first box uses the current heading; subsequent boxes use
// the preceding segment (including the original zero-length fallback).
// Bounds and scale are supplied by the actor-specific458220/458290 owner.
bool ai_prediction_shapes(const AiDriverPath&,const AiDriverPoint& bound_min,
    const AiDriverPoint& bound_max,std::uint16_t heading,float scale,
    AiPredictionPath&) noexcept;
struct AiTrajectoryMotion {
    std::array<float,2> position{};
    float speed=0; // already clamped by the original actor-specific wrapper
    unsigned first=0; // refreshed original cursor at the same body epoch
};
//457C3E..45809C, the pair walk inside4576B0, after path prediction/shape/cursor
// production. Both speeds receive self's projected acceleration, as in source.
// Horizon bounds segment START time; the original does not shorten a segment
// to the remaining horizon.99999 means no admitted intersection. No contact
// impulse, actor registration, path mutation, allocation or fallback policy.
// False preserves time; unsupported arithmetic is not a clear-path verdict.
bool ai_prediction_walk(const AiPredictionPath&,const AiTrajectoryMotion&,
    const AiPredictionPath&,const AiTrajectoryMotion&,float self_acceleration,
    float horizon,float& time) noexcept;
//44F1B0 physical obstacle forecast, distinct from path overlap4576B0.
// Raw local bounds and fresh world pose/velocity belong to the body/world
// owner. The scalar4650D0/464F20 frame uses velocity above its threshold;
// it is not replaced by an assumed heading-aligned corridor.
struct AiPhysicalObstacleInput {
    AiDriverPoint self_min{},self_max{},position{},velocity{};
    std::uint16_t heading=0;
    float signed_speed=0,absolute_speed=0;
    AiDriverPoint other_min{},other_max{},other_position{};
    std::array<float,2> other_forward{},other_velocity{};
    bool other_shape=true;
};
struct AiPhysicalObstacleResult {float distance=99999,time=99999;};
// Preserves original sentinel division: a closing object can have finite
// time even when lateral/forward admission left distance at99999.
// No horizon, action choice, registration or physical impulse is applied.
bool ai_physical_obstacle(const AiPhysicalObstacleInput&,
                           AiPhysicalObstacleResult&) noexcept;
} // namespace nfsu
