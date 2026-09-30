#pragma once

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace nfsu {
struct DekoSceneTarget {
    std::uint32_t width = 0, height = 0;
    bool upscale = false;
};

// Fixed 16:9 A/B targets. Zero means the caller's unchanged native target.
// Scaling is a presentation-only policy; it never changes recorded commands,
// camera projection, visibility, simulation, or the CPU reference viewport.
inline bool deko_parse_scene_height(const char* text, std::uint32_t& height) {
    if (!text) return false;
    for (const auto value : {720U, 540U, 450U, 360U}) {
        const char* name = value == 720 ? "720" : value == 540 ? "540" :
                           value == 450 ? "450" : "360";
        if (std::strcmp(text, name) == 0) { height = value; return true; }
    }
    return false;
}

inline bool plan_deko_scene_target(std::uint32_t width, std::uint32_t height,
                                  bool presentation, std::uint32_t requested_height,
                                  DekoSceneTarget& out) {
    out = {};
    if (!width || !height || width > 16384 || height > 16384) return false;
    if (!requested_height) { out = {width, height, false}; return true; }
    if (width != 1280 || height != 720) return false;
    if (requested_height == 720) { out = {width, height, false}; return true; }
    if (!presentation || (requested_height != 540 && requested_height != 450 &&
                          requested_height != 360)) return false;
    out = {requested_height * 16 / 9, requested_height, true};
    return true;
}
} // namespace nfsu
