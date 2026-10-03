#pragma once

#include "nfsu/race_timing.h"
#include <array>
#include <cstdint>

namespace nfsu {

// A value copy from one committed timing state, with no renderer-owned clock
// or transient state. Retry replaces the entire snapshot, including the hold.
struct RaceLapHudSnapshot {
    bool valid = false;
    std::size_t generation = 0, step = 0, actor = 0;
    std::int32_t world_ticks = 0, start_ticks = 0, best_ticks = 0;
    std::int32_t held_since = 0, held_ticks = 0;
    bool start_valid = false, hold_valid = false, finished = false;
    int highest_lap = 0, laps = 0;
};

RaceLapHudSnapshot race_lap_hud_snapshot(const RaceRoute& route,
    const RaceTimingState& timing, std::size_t actor) noexcept;

// The selected language producer is recovered, but its live event0 value is
// unknown. Callers must explicitly supply a recovered option; unknown does not select
// the colon/decimal appearance just because a reference still uses it.
enum class RaceLapHudStyle { unknown, native1, native2, native3, native4,
                            native5, native6, native_fallback };
using RaceLapHudText = std::array<char, 32>;
// Only recovered selected language IDs are supported. No registry/default guess.
RaceLapHudStyle race_lap_hud_style(bool known, int selected_language) noexcept;
struct RaceLapHudLanguage { bool known = false; int selected = -1; };
bool format_lap_hud_ticks(std::int32_t ticks, RaceLapHudStyle style,
                          RaceLapHudText& text) noexcept;

struct RaceLapHudBinding {
    const char* object;
    const char* default_resource;
    std::uint16_t resource_index;
    float x, y, depth, scale;
    std::array<unsigned char, 4> rgba;
    std::uint32_t justification; // Raw Sj value; not a guessed alignment.
    std::uint32_t label_hash;
};
constexpr std::size_t kRaceLapHudFields = 7;
const std::array<RaceLapHudBinding, kRaceLapHudFields>& race_lap_hud_bindings() noexcept;

struct RaceLapHudField {
    RaceLapHudText text{};
    std::uint32_t localized_hash = 0;
};
struct RaceLapHudPanel {
    RaceLapHudSnapshot snapshot{};
    // Order: current, current label, best, best label, total, number, lap label.
    std::array<RaceLapHudField, kRaceLapHudFields> fields{};
    bool current_valid = false, current_held = false, formatting_valid = false;
    // This partial implementation deliberately cannot submit screen geometry.
    // Live event0 profile/lifecycle, runtime hierarchy/font, viewport and
    // shader/material inputs need original evidence before opening this gate.
    bool presentation_ready = false;
};

// Pure native ordinary-circuit consumer. Never advances, clamps a completed
// lap to the target, chooses a visibility mask, or invents localization text.
void build_race_lap_hud_panel(const RaceLapHudSnapshot& snapshot,
    RaceLapHudStyle style, RaceLapHudPanel& panel) noexcept;
void build_race_lap_hud_panel(const RaceLapHudSnapshot& snapshot,
    RaceLapHudLanguage language, RaceLapHudPanel& panel) noexcept;

} // namespace nfsu
