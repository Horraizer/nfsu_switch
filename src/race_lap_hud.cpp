#include "nfsu/race_lap_hud.h"
#include <cstdio>
#include <cstring>
#include <limits>

namespace nfsu {
namespace {
std::int32_t difference(std::int32_t a, std::int32_t b) noexcept {
    const std::uint32_t bits = static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b);
    std::int32_t value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

constexpr const char* conduit14 = "ConduitMdITC_TT14i.ffn";
constexpr const char* conduit38 = "Font_ConduitMdITCTT38bi.ffn";
// HUD_SingleRace.fng OP/RsRq and SA/Sj/SH, traced in docs/HUD_254.md.
constexpr std::array<RaceLapHudBinding, kRaceLapHudFields> bindings{{
    {"CurrentLapTime", conduit14, 3, -147,-170,100,0.9F, {{150,190,255,255}},2,0},
    {"CurrentLapTimeText", conduit14, 3, -296,-170,100,0.9F, {{150,190,255,255}},0,0x057C8F62},
    {"BestLapTime", conduit14, 3, -148,-148,100,0.9F, {{150,190,255,255}},2,0},
    {"BestLapTimeText", conduit14, 3, -296,-148,100,0.9F, {{150,190,255,255}},0,0xA824620D},
    {"NumberLaps", conduit38, 0, -226,-217,100,0.5F, {{0,128,192,255}},0,0},
    {"LapNumber", "LCD_LET48.ffn", 5, -236,-220,100,1, {{150,190,255,255}},2,0},
    {"LapText", conduit38, 0, -230,-197,100,0.4F, {{0,128,192,255}},0,0x0000BF9C},
}};
} // namespace

RaceLapHudSnapshot race_lap_hud_snapshot(const RaceRoute& route,
    const RaceTimingState& timing, std::size_t actor) noexcept {
    RaceLapHudSnapshot result;
    if (actor >= timing.racers.size() || route.finish < 0 ||
        static_cast<std::size_t>(route.finish) >= kRaceBoundaryLimit) return result;
    const auto& record = timing.racers[actor];
    // Retail highest lap is a signed byte. Reject unsupported port state.
    if (record.highest_lap < -128 || record.highest_lap > 127 || route.laps <= 0) return result;
    result.valid = true;
    result.generation = timing.restarts; result.step = timing.step; result.actor = actor;
    result.world_ticks = timing.step_ticks;
    result.start_ticks = record.crossing_ticks[0];
    result.start_valid = record.crossing_valid[0] && result.start_ticks != 0 &&
                         result.start_ticks != kRaceNoTime;
    result.best_ticks = record.best_split_ticks[static_cast<std::size_t>(route.finish)];
    result.hold_valid = record.lap_shown_valid;
    result.held_since = record.lap_shown_since; result.held_ticks = record.lap_shown_ticks;
    result.highest_lap = record.highest_lap; result.laps = route.laps;
    result.finished = record.status != RacerStatus::not_finished;
    return result;
}

const std::array<RaceLapHudBinding, kRaceLapHudFields>& race_lap_hud_bindings() noexcept {
    return bindings;
}

bool format_lap_hud_ticks(std::int32_t ticks, RaceLapHudStyle style,
                          RaceLapHudText& text) noexcept {
    text = {};
    if (style == RaceLapHudStyle::unknown || ticks == std::numeric_limits<std::int32_t>::min())
        return false; // Native NEG overflows for INT_MIN; unsupported rather than a fake time.
    // 0x00586580 flags4, 0x005864F0 integer fields, 0x00586090 templates.
    const bool negative = ticks < 0;
    const std::int32_t absolute = ticks == kRaceNoTime ? 0 : negative ? -ticks : ticks;
    const int hours = absolute / 14400000;
    const int minutes = absolute / 240000 % 60;
    const int seconds = absolute / 4000 % 60;
    const int hundredths = absolute % 4000 / 40;
    const bool quote = style == RaceLapHudStyle::native1 || style == RaceLapHudStyle::native3;
    const bool colon = style == RaceLapHudStyle::native2 || style == RaceLapHudStyle::native4 ||
                       style == RaceLapHudStyle::native6;
    if (hours)
        std::snprintf(text.data(), text.size(), quote ? "%2dh:%02d'%02d\"" : "%2dh:%02d:%02d",
                      hours, minutes, seconds);
    else
        std::snprintf(text.data(), text.size(), quote ? "%2d'%02d\"%02d" :
                      colon ? "%2d:%02d:%02d" : "%2d:%02d.%02d", minutes, seconds, hundredths);
    if (negative) {
        // Native reserves the first column for the sign. When that column
        // contains a digit it shifts within the same fixed-length template,
        // dropping the final character (including the negative >=10m case).
        if (text[0] != ' ') {
            const auto length = std::strlen(text.data());
            std::memmove(text.data()+1, text.data(), length-1);
        }
        text[0] = '-';
    }
    return true;
}

void build_race_lap_hud_panel(const RaceLapHudSnapshot& snapshot,
    RaceLapHudStyle style, RaceLapHudPanel& panel) noexcept {
    panel = {};
    panel.snapshot = snapshot;
    if (!snapshot.valid || snapshot.highest_lap < -128 || snapshot.highest_lap > 127 ||
        snapshot.laps <= 0) return;
    for (std::size_t i = 0; i < bindings.size(); ++i)
        panel.fields[i].localized_hash = bindings[i].label_hash;
    std::snprintf(panel.fields[4].text.data(), panel.fields[4].text.size(), "/%2d", snapshot.laps);
    std::snprintf(panel.fields[5].text.data(), panel.fields[5].text.size(), "%2d", snapshot.highest_lap+1);
    panel.current_valid = snapshot.start_valid && snapshot.start_ticks != 0 &&
                          snapshot.start_ticks != kRaceNoTime;
    bool current_formatted = style != RaceLapHudStyle::unknown;
    if (panel.current_valid) {
        // x87 multiplies an integer by float at 0x006CCA38 in extended
        // precision, then compares against 3. Double preserves this result;
        // rounding the product to float first would hold at 12000 ticks.
        panel.current_held = snapshot.hold_valid &&
            double(difference(snapshot.world_ticks, snapshot.held_since))*double(0.00025F) <= 3.0;
        const auto ticks = panel.current_held ? snapshot.held_ticks :
                           difference(snapshot.world_ticks, snapshot.start_ticks);
        current_formatted = format_lap_hud_ticks(ticks, style, panel.fields[0].text);
    } else {
        panel.fields[0].localized_hash = 0x61C9B7A2;
    }
    bool best_formatted = style != RaceLapHudStyle::unknown;
    if (snapshot.best_ticks == 0) std::strcpy(panel.fields[2].text.data(), "--:--:--");
    else best_formatted = format_lap_hud_ticks(snapshot.best_ticks, style, panel.fields[2].text);
    panel.formatting_valid = current_formatted && best_formatted;
}
} // namespace nfsu
