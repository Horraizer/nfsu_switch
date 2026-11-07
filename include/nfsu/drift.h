#pragma once
#include <cstdint>
namespace nfsu {
// PC USA 1.4 43E540. Grade is the recovered performance class (57F150),
// not the selected Easy/Medium/Hard difficulty. Angles are radians.
struct DriftScoreSample {
    float speed=0,slip=0,yaw=0,progress=0;
    unsigned grade=0;
    bool blocked=false;
    unsigned phase=2; // 0 qualified, 1 unqualified, 2 waiting, 3/4 terminal.
};
// Updates the route-progress high-water mark even on a non-scoring sample.
// Returns the base factor; the style rule supplies rate, time and multiplier.
float drift_score_factor(const DriftScoreSample&,float& highwater) noexcept;
// 479350, selected by runtime drift byte78A345 (not adjacent byte78A346).
// Torque is about the body's up axis, in original kN m units. wheel_scale
// is published to two wheel components for the next force preparation.
struct DriftForces {float torque=0,wheel_scale=1;};
DriftForces drift_forces(std::uint16_t heading_minus_velocity,float yaw,
                         float rear_drive_fraction,unsigned difficulty) noexcept;
}
