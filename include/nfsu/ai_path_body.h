#pragma once
#include "nfsu/ai_driver.h"
#include "nfsu/vehicle.h"

namespace nfsu {
// Selector4 owns these channels across Driver path publications. Recreating
// them each frame removes the original heading and slip history (463260).
struct AiPathBodyChannel {
    float value=0,derivative=0,target=0,target_derivative=0;
    float a=0,b=0,c=0,d=0,phase=0,duration=1,end=1;
    unsigned status=0;
    bool continuous=true;
    void set(float) noexcept;
    void step(float) noexcept;
};
struct AiPathBodyState {
    AiDriverPath path;
    float parameter=0,rate=0,acceleration=0,points_per_metre=0,elapsed=0;
    // Lateral displacement, heading X/Y, slip X/Y.
    std::array<AiPathBodyChannel,5> channels{};
    std::array<float,30> curvature_history{};
    float curvature_sum=0,curvature_average=0;
    unsigned curvature_count=0,curvature_cursor=0;
    bool sliding=false;
};
struct AiPathBodySample { PlayerRigidVector point{},tangent{},curvature{}; };
AiPathBodySample ai_path_body_sample(const AiDriverPath&,float parameter) noexcept;
// 59D430/59D540: first local nearest knot, then perpendicular projection.
float ai_path_body_project(const AiDriverPath&,const PlayerRigidVector& position) noexcept;
// 463260: new physical component, not a Driver path update.
void ai_path_body_seed(AiPathBodyState&,std::uint16_t heading,float speed,float yaw) noexcept;
// 463910: retain heading/slip/filter history while republishing the spline.
void ai_path_body_publish(AiPathBodyState&,const PlayerRigidVector& position,
                          const PlayerRigidVector& velocity) noexcept;
struct AiPathBodyDemand {
    float forward=0,desired=0,duration=.5F,scale=1,drafting=0,gearing_limit=0;
};
// 463910/456830: signed speed, native curve indexing and slope acceleration.
void ai_path_body_speed_target(AiPathBodyState&,const VehicleHandling&,
    const std::array<float,64>& acceleration,const AiPathBodyDemand&) noexcept;
// 463F20..46427D. Caller subsequently runs4278D0 wheel placement, publishes
// finite-difference velocity and Actor3D0, then wheel effects and427A30.
struct AiPathBodyStep { PlayerRigidVector position{};std::uint16_t heading=0;float speed=0; };
AiPathBodyStep ai_path_body_step(AiPathBodyState&,const VehicleHandling&,float dt) noexcept;
// 463D50: nine-step departure forecast using publication-time downforce.
bool ai_path_body_departure(const AiPathBodyState&,const VehicleHandling&) noexcept;
struct AiPathBodyMotion {
    PlayerRigidVector velocity{};
    float yaw=0,magnitude=0;
};
// 4642AD..464405: after wheel placement, publish displacement-derived
// velocity and signed heading change. Caller supplies a positive dt.
AiPathBodyMotion ai_path_body_motion(const PlayerRigidVector& previous,
    const PlayerRigidVector& current,std::uint16_t previous_heading,
    std::uint16_t heading,float scheduled_speed,float dt) noexcept;
} // namespace nfsu
