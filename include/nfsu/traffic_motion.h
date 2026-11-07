#pragma once
#include <cstdint>
#include <array>
namespace nfsu {
// Ordinary TrafficMover5754B0 planar state. Height/body publication belongs
// to the road owner. Collided traffic switches to the dynamic body adapter.
struct TrafficMotion {float x=0,y=0,heading=0,speed=0;};
struct TrafficMotionInput {
    float front_axle=0,rear_axle=0,throttle=0,brake=0,dt=0;
    std::uint16_t steering=0;
};
bool traffic_motion_step(const TrafficMotionInput&,TrafficMotion&) noexcept;
//575727..57581D after the road owner supplies the new COM position.
// Velocity follows the actual displacement (including slope), scaled to the
// mover speed; angular velocity uses the wrapped published heading change.
struct TrafficBodyPublication {
    std::array<float,3> velocity{};float magnitude=0,forward=0,yaw_rate=0;
    std::uint16_t heading=0;
};
bool traffic_motion_publish(const std::array<float,3>& previous,
    const std::array<float,3>& current,float heading,float speed,float dt,
    std::uint16_t previous_heading,TrafficBodyPublication&) noexcept;
}
