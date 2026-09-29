#pragma once

#include "nfsu/frame_profile_stats.h"

#include <cstdint>

namespace nfsu {

struct DekoSceneFrame {
    std::uint64_t tick = 0;
    std::uint32_t track_draws = 0;
    std::uint32_t car_draws = 0;
    bool active = false;
};

// Accumulates completed GPU scene pairs since the last explicit profile reset.
// The tick range exposes the window when a long run was resumed near its end.
struct DekoGpuProfile {
    ProfileDistribution scene;
    std::uint64_t dropped = 0;
    std::uint64_t invalid = 0;
    std::uint64_t first_tick = 0;
    std::uint64_t last_tick = 0;
    std::uint64_t worst_tick = 0;
    std::uint32_t worst_track_draws = 0;
    std::uint32_t worst_car_draws = 0;
    void add_scene(double ms, const DekoSceneFrame& frame) {
        const auto previous_count = scene.count();
        const auto previous_max = scene.max();
        scene.add(ms);
        if (scene.count() == previous_count) return;
        if (!previous_count) first_tick = last_tick = frame.tick;
        else {
            if (frame.tick < first_tick) first_tick = frame.tick;
            if (frame.tick > last_tick) last_tick = frame.tick;
        }
        if (ms > previous_max) {
            worst_tick = frame.tick;
            worst_track_draws = frame.track_draws;
            worst_car_draws = frame.car_draws;
        }
    }
    std::uint64_t span_ticks() const {
        return scene.count() ? last_tick - first_tick + 1 : 0;
    }
};

} // namespace nfsu
