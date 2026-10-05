#pragma once
#include <array>
#include <cstdint>
namespace nfsu {
// Original 0x564B10 fixed-angle polynomial, not host-library sine.
float frontend_fixed_sine(std::uint16_t phase) noexcept;
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
}
