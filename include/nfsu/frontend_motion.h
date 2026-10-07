#pragma once
#include <array>
#include "nfsu/car_shading.h"
#include <cstdint>
namespace nfsu {
// Original 0x564B10 fixed-angle polynomial, not host-library sine.
float frontend_fixed_sine(std::uint16_t phase) noexcept;
// Native 0x4B18F7..0x4B192A uses wheel 0 radius for all four wheels.
std::uint16_t frontend_wheel_phase(std::uint16_t phase, float travel, float radius) noexcept;
// Original5443F0 type9 quad. All vectors are in the raw slot frame.
// View visibility is decided by the caller; this function never invents it.
struct FrontendRoadGlow {
    float corners[4][3]{};
    unsigned char rgba[4]{};
};
bool frontend_road_glow(const float (&point)[3],const float (&toward_eye)[3],
    const float (&right)[3],const float (&up)[3],float facing,float depth_scale,
    unsigned mode,FrontendRoadGlow& out) noexcept;
// Original5443F0 type0 inner/outer, type1 tail, or type9 road descriptor.
// Type0/1 caller supplies actual marker facing and native opacity.
bool frontend_car_glow(const float (&point)[3],const float (&toward_eye)[3],
    const float (&right)[3],const float (&up)[3],float facing,float depth_scale,
    unsigned mode,unsigned descriptor,float opacity,FrontendRoadGlow& out) noexcept;
struct FrontendRoadPlacement {
    unsigned group = 0;
    unsigned family = 0; // 0: _A, 1: _Z, 2: _Y, from 0x4AE530.
    float longitudinal = 0;
    float vertical = 0.291F;
};
struct FrontendRoad {
    float offset = 0;
    float speed_kmh = 50;
    float target_kmh = 50;
    float velocity = 0;
    float delta = 0;
    // The original clock counts 4000 ticks/second. Update once per frame.
    void update(float seconds, std::uint32_t ticks, bool forced_speed = false) noexcept;
    // Native view ids select road models and the number of translated copies.
    // Return 0 for a view that does not draw this environment.
    unsigned placements(unsigned view, std::array<FrontendRoadPlacement,5>& out) const noexcept;
};
// Original4AE1F0 fills20 of32 road slots. Lateral/color/attenuation are
// explicit unresolved startup inputs; callers must not invent defaults.
unsigned frontend_road_lights(const FrontendRoad& road,const float (&markers)[4][3],
    float lateral,const float (&rgb)[3],const float (&attenuation)[3],
    std::array<FrontendPointLight,32>& out) noexcept;
}
