#include "nfsu/race_timing.h"

#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

namespace {

// The executable's float constants, each the exact float it loads.
// [0x006CCA38] = 0x3983126F, a tick in seconds.
constexpr float kTickSeconds = 0.00025F;
// [0x006CC9FC] = 0x457A0000.
constexpr float kTicksPerSecondFloat = 4000.0F;
// [0x006CC7DC] = 0x3F000000.
constexpr float kHalf = 0.5F;
// [0x006CC7BC] = 0x3F800000.
constexpr float kOne = 1.0F;
// 0x3C23D70A, the resolution the race's calls of 0x00586690 push
// (0x004339B7, 0x004355D0, 0x00434C14 and 0x00434C89 among them), and
// [0x006CC79C], the percent 0x004336E5 scales a distance by.
constexpr float kHundredth = 0.01F;
// [0x006CC7E0] = 0x45610000 and [0x006CC7E4] = 0x3991A2B4: the frame's
// 3600ths of a second and one of them.
constexpr float kFrameUnitsPerSecond = 3600.0F;
constexpr float kFrameUnit = 1.0F / 3600.0F;
// [0x006CC7AC] = 0x40400000, the third countdown step (0x00434DA2).
constexpr float kThree = 3.0F;
// [0x006CCABC] = -50, [0x006CC93C] = 50 and [0x006CC9F8] = 100: the wrap of
// a percent difference on a closed path (0x00433675..0x00433697).
constexpr float kWrapHalf = 50.0F;
constexpr float kWrapWhole = 100.0F;
// 2^31 as a float: the first value _ftol cannot represent.
constexpr float kIntRange = 2147483648.0F;
constexpr float kTicksLimitFloat = 536870912.0F;

constexpr std::size_t kMaxRacers = 64;
constexpr std::int32_t kMaxTicksPerStep = kRaceTicksPerSecond;

// _ftol (0x00674898): truncation toward zero, and 0x80000000 - the x87's
// integer indefinite - for a value it cannot represent or NaN.
std::int32_t truncate_float(float value) noexcept {
    if (!(value > -kIntRange) || !(value < kIntRange)) {
        return static_cast<std::int32_t>(-2147483647 - 1);
    }
    return static_cast<std::int32_t>(value);
}

// `fild dword ptr [...]` then `fmul dword ptr [0x006CCA38]`: the integer is
// loaded exactly and the product rounded once to float, as the x87 does at
// the 24-bit precision Direct3D leaves it in (docs/RACE_ROUTE.md). The
// product of an integer below 2^29 and a 24-bit significand is exact in
// double, so rounding it to float is that single rounding.
float ticks_to_seconds(std::int32_t ticks) noexcept {
    return static_cast<float>(static_cast<double>(ticks) * static_cast<double>(kTickSeconds));
}

// 32-bit arithmetic that wraps as the executable's `add` and `sub` do,
// without the undefined behaviour of a signed overflow.
std::int32_t wrap_add(std::int32_t a, std::int32_t b) noexcept {
    const std::uint32_t sum = static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b);
    return static_cast<std::int32_t>(sum);
}

std::int32_t wrap_sub(std::int32_t a, std::int32_t b) noexcept {
    const std::uint32_t difference =
        static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b);
    return static_cast<std::int32_t>(difference);
}

// +0x04 and +0x06 of the lap record are signed 16-bit (0x0043398B `movsx
// ax`, 0x00433995 `cmp ax, word ptr`).
int wrap_signed_word(int value) noexcept {
    return ((value + 32768) & 0xFFFF) - 32768;
}

// 0x00433675..0x00433697: below -50 add 100, above 50 subtract 100.
float wrap_percent(float difference) noexcept {
    if (difference < -kWrapHalf) {
        difference += kWrapWhole;
    } else if (difference > kWrapHalf) {
        difference -= kWrapWhole;
    }
    return difference;
}

// 0x00433650..0x0043379B, the backdate a crossing is stored with: how far
// past the boundary the car's percent is (0x00433656..0x0043365A, wrapped),
// as a distance (0x004336D5 `fld [0x007372B0]`, the path length, times it,
// 0x004336E5 times 0.01), over the car's speed when that is above 1
// (0x00433761..0x0043377A; 0 otherwise), in ticks, rounded
// (0x00433780..0x0043378C). A forward crossing gives 0 or more. This
// project saturates the tick count at kRaceTicksLimit, where _ftol would
// give 0x80000000 for an absurd distance over a speed just above 1.
std::int32_t crossing_backdate(float percent, float boundary, float path_length,
                               float speed) noexcept {
    const float past = wrap_percent(percent - boundary);
    const float distance = (path_length * past) * kHundredth;
    float seconds = 0.0F;
    if (speed > kOne) {
        seconds = distance / speed;
    }
    const float ticks = seconds * kTicksPerSecondFloat + kHalf;
    if (!(ticks < kTicksLimitFloat)) {
        return kRaceTicksLimit;
    }
    if (!(ticks > -kTicksLimitFloat)) {
        return -kRaceTicksLimit;
    }
    return truncate_float(ticks);
}

int countdown_value_at(const RaceTimingConfig& config, const RaceTimingState& state,
                       std::int32_t now) noexcept {
    const float step = config.countdown_step_seconds;
    if (state.go_valid) {
        // 0x00434D1A..0x00434D45: "go" while the race is at most one step
        // old, `fcomp [ecx + 0xc]`, `test ah, 0x41`.
        const float since = ticks_to_seconds(wrap_sub(now, state.go_ticks));
        return since <= step ? 4 : 0;
    }
    if (state.countdown_valid) {
        // 0x00434D4B..0x00434DBA.
        const float since = ticks_to_seconds(wrap_sub(now, state.countdown_start_ticks));
        if (since < step) {
            return 3;
        }
        if (since < step + step) {
            return 2;
        }
        if (since < step * kThree) {
            return 1;
        }
        return 4;
    }
    return 0;
}

void emit(std::vector<RaceTimingEvent>& events, RaceTimingEventKind kind,
          const RaceTimingState& state, std::int32_t now, int racer = -1) {
    RaceTimingEvent event;
    event.kind = kind;
    event.step = state.step;
    event.now = now;
    event.racer = racer;
    events.push_back(event);
}

bool check_config(const RaceRoute& route, const RaceTimingConfig& config, std::string& error) {
    if(config.time_limit_ticks<0||config.time_limit_ticks>kRaceTicksLimit){error="invalid challenge time limit";return false;}
    if (config.racers == 0 || config.racers > kMaxRacers) {
        error = "a race holds 1 to " + std::to_string(kMaxRacers) + " racers, not " +
                std::to_string(config.racers);
        return false;
    }
    if (config.humans > config.racers) {
        error = "more human racers than racers";
        return false;
    }
    if (config.steps_per_second < 1) {
        error = "the step rate must be at least 1 a second";
        return false;
    }
    if (config.ticks_per_step < 1 || config.ticks_per_step > kMaxTicksPerStep) {
        error = "a step adds 1 to " + std::to_string(kMaxTicksPerStep) + " ticks, not " +
                std::to_string(config.ticks_per_step);
        return false;
    }
    if (!std::isfinite(config.countdown_step_seconds) || !(config.countdown_step_seconds > 0.0F)) {
        error = "the countdown step must be a positive number of seconds";
        return false;
    }
    if (config.start_ticks < 1 || config.start_ticks > kRaceTicksLimit) {
        error = "the world timer must start between 1 and " + std::to_string(kRaceTicksLimit) +
                " ticks";
        return false;
    }
    if (route.boundaries.empty() || route.boundaries.size() > kRaceBoundaryLimit) {
        error = "the route has " + std::to_string(route.boundaries.size()) +
                " boundaries, not 1 to " + std::to_string(kRaceBoundaryLimit);
        return false;
    }
    if (route.start != 0) {
        // 0x004338C0 reads the lap start from the first slot (0x004339EF
        // `mov ecx, [edi + 0x24]`), and 0x00434810 always makes the start
        // line boundary 0.
        error = "the start line is not boundary 0";
        return false;
    }
    if (route.finish < 0 || static_cast<std::size_t>(route.finish) >= route.boundaries.size()) {
        error = "the finish line is not one of the route's boundaries";
        return false;
    }
    if (route.point_to_point && route.laps != 1) {
        // 0x00434C37..0x00434C42 zero the lap counter of a point-to-point
        // race at go; race_laps() has already collapsed its stored lap
        // count to the single pass a point-to-point course runs.
        error = "a point-to-point race is one pass, not " +
                std::to_string(route.laps) + " laps";
        return false;
    }
    if (!std::isfinite(route.spline.path_length) || !(route.spline.path_length > 0.0F)) {
        error = "the route's path length is not a positive number";
        return false;
    }
    for (const RaceBoundary& boundary : route.boundaries) {
        if (!std::isfinite(boundary.percent)) {
            error = "a boundary's percent is not finite";
            return false;
        }
    }
    return true;
}

void build_race(const RaceTimingConfig& config, RaceTimingState& state) {
    const std::size_t restarts = state.restarts;
    state = RaceTimingState{};
    state.restarts = restarts;
    state.now_ticks = config.start_ticks;
    state.step_ticks = config.start_ticks;
    state.racers.assign(config.racers, RaceRacerTiming{});
}

bool is_crossing_kind(RaceEventKind kind) noexcept {
    return kind == RaceEventKind::crossed_forward || kind == RaceEventKind::crossed_backward ||
           kind == RaceEventKind::missed_forward || kind == RaceEventKind::missed_backward;
}

bool check_inputs(const RaceRoute& route, const RaceTimingConfig& config,
                  const RaceTimingInput* inputs, std::string& error) {
    if (inputs == nullptr) {
        error = "no input for the racers";
        return false;
    }
    for (std::size_t r = 0; r < config.racers; ++r) {
        const RaceTimingInput& input = inputs[r];
        const std::string who = "racer " + std::to_string(r) + ": ";
        if (!std::isfinite(input.speed) || input.speed < 0.0F) {
            error = who + "the speed is not a finite number of at least 0";
            return false;
        }
        if (input.event_count > 0 && input.events == nullptr) {
            error = who + "events are counted but not given";
            return false;
        }
        for (std::size_t e = 0; e < input.event_count; ++e) {
            const RaceProgressEvent& event = input.events[e];
            const std::string where = who + "event " + std::to_string(e) + ": ";
            if (event.lap < -128 || event.lap > 127) {
                error = where + "lap " + std::to_string(event.lap) +
                        " is outside a signed byte";
                return false;
            }
            if (!std::isfinite(event.percent)) {
                error = where + "the percent is not finite";
                return false;
            }
            if (is_crossing_kind(event.kind) &&
                (event.index < 0 ||
                 static_cast<std::size_t>(event.index) >= route.boundaries.size())) {
                error = where + "boundary " + std::to_string(event.index) +
                        " is not one of the route's";
                return false;
            }
            if (event.kind == RaceEventKind::reset_zone && event.index < 0) {
                error = where + "a reset zone without a zone";
                return false;
            }
        }
    }
    return true;
}

// 0x00434B50 with 4 (0x00434C0A..0x00434CB9): the race goes.
void go(const RaceRoute& route, RaceTimingState& state, std::int32_t now, std::vector<RaceTimingEvent>& events) {
    const std::int32_t start = race_quantize_ticks(now);
    for (RaceRacerTiming& racer : state.racers) {
        racer.crossing_valid[0] = true;
        racer.crossing_ticks[0] = start;
        racer.highest_checkpoint = 0;
        // 434C37..434C42: open-course races start on lap zero at GO.
        if (route.point_to_point) racer.lap = 0;
    }
    state.stage = RaceStage::racing;
    state.race_start_valid = true;
    state.race_start_ticks = start;
    state.go_valid = true;
    state.go_ticks = now;
    state.go_step = state.step;
    state.countdown_valid = false;
    state.countdown_start_ticks = 0;
    emit(events, RaceTimingEventKind::stage, state, now);
    events.back().value = static_cast<std::int32_t>(RaceStage::racing);
    emit(events, RaceTimingEventKind::go, state, now);
    events.back().ticks = start;
}

// 0x004338C0 for one forward crossing (0x0043398B..0x00433A6D).
void time_crossing(const RaceRoute& route, RaceTimingState& state, std::size_t index,
                   const RaceProgressEvent& event, float speed, std::int32_t now,
                   std::vector<RaceTimingEvent>& events) {
    RaceRacerTiming& racer = state.racers[index];
    const int boundary = event.index;
    const auto slot = static_cast<std::size_t>(boundary);
    racer.checkpoint = wrap_signed_word(5 * event.lap + boundary);
    emit(events, RaceTimingEventKind::untimed, state, now, static_cast<int>(index));
    RaceTimingEvent& line = events.back();
    line.boundary = boundary;
    line.lap = event.lap;
    if (!(racer.checkpoint > racer.highest_checkpoint)) {
        line.value = 1;
        return;
    }
    racer.highest_checkpoint = racer.checkpoint;
    racer.new_checkpoint = true;
    const std::int32_t backdate = crossing_backdate(
        event.percent, route.boundaries[slot].percent, route.spline.path_length, speed);
    const std::int32_t crossing = race_quantize_ticks(wrap_sub(now, backdate));
    line.crossing = crossing;
    line.value = backdate;
    if (racer.crossing_valid[0]) {
        racer.previous_split_ticks[slot] = racer.split_ticks[slot];
        racer.split_ticks[slot] = wrap_sub(crossing, racer.crossing_ticks[0]);
        racer.previous_best_split_ticks[slot] = racer.best_split_ticks[slot];
        if (racer.split_ticks[slot] < racer.best_split_ticks[slot]) {
            racer.best_split_ticks[slot] = racer.split_ticks[slot];
        }
        line.ticks = racer.split_ticks[slot];
        if (boundary == route.finish) {
            line.kind = RaceTimingEventKind::lap;
            const int lap = event.lap;
            if (lap >= 0 && lap < kRaceLapSlots) {
                const auto at = static_cast<std::size_t>(lap);
                racer.lap_ticks[at] = racer.split_ticks[slot];
                racer.lap_total_ticks[at] = racer.lap_ticks[at];
                if (lap > 0) {
                    racer.lap_total_ticks[at] =
                        wrap_add(racer.lap_total_ticks[at], racer.lap_total_ticks[at - 1]);
                }
            }
        } else {
            line.kind = RaceTimingEventKind::split;
        }
    } else {
        line.value = 2;
    }
    racer.crossing_valid[slot] = true;
    racer.crossing_ticks[slot] = crossing;
}

// The first place no racer holds yet (0x00435BD0).
int first_free_place(const RaceTimingState& state) noexcept {
    int place = 1;
    bool taken = true;
    while (taken) {
        taken = false;
        for (const RaceRacerTiming& racer : state.racers) {
            if (racer.position == place) {
                taken = true;
                ++place;
                break;
            }
        }
    }
    return place;
}

} // namespace

std::int32_t race_quantize_ticks(std::int32_t ticks) noexcept {
    // 0x00586693..0x0058669F: seconds over the resolution, stored as float.
    const float scaled = ticks_to_seconds(ticks) / kHundredth;
    // 0x004387E0 truncates it (_ftol, fild) and 0x005866AD..0x005866C5 take
    // one off a truncation above it: the floor.
    float whole = static_cast<float>(truncate_float(scaled));
    if (whole > scaled) {
        whole = whole - kOne;
    }
    // 0x005866C9..0x005866E4: half up.
    if (!(scaled - whole < kHalf)) {
        whole = whole + kOne;
    }
    // 0x005866EA..0x005866F5: back to ticks as a float, which 0x00438800
    // rounds up to a whole number (_ftol, fild, one added when below).
    const float product = (whole * kHundredth) * kTicksPerSecondFloat;
    float ceiling = static_cast<float>(truncate_float(product));
    if (ceiling < product) {
        ceiling = ceiling + kOne;
    }
    // 0x005866FD, then the caller's `fmul [0x006CC9FC]`, `fadd [0x006CC7DC]`
    // and `_ftol`.
    const float seconds = ceiling * kTickSeconds;
    return truncate_float(seconds * kTicksPerSecondFloat + kHalf);
}

std::int32_t race_ticks_per_frame(int frames_per_second) noexcept {
    if (frames_per_second < 1 || frames_per_second > 3600) {
        return 0;
    }
    // The frame, and its whole 3600ths (0x00585CD5..0x00585CE3).
    const float frame = kOne / static_cast<float>(frames_per_second);
    const std::int32_t units = truncate_float(frame * kFrameUnitsPerSecond + kHalf);
    // 0x00585D52..0x00585D63: back to seconds; 0x00585E6C..0x00585E7E: ticks.
    const float seconds = static_cast<float>(units) * kFrameUnit;
    return truncate_float(seconds * kTicksPerSecondFloat + kHalf);
}

std::string format_race_ticks(std::int32_t ticks) {
    // 0x0058658C..0x005865A9: 0 and 0x7FFFFFFF go to 0x00586090 as four
    // zero fields, which it prints as a zero time (0x005863A6..0x00586461).
    if (ticks == kRaceNoTime) {
        ticks = 0;
    }
    const bool negative = ticks < 0;
    const std::int64_t magnitude = negative ? -static_cast<std::int64_t>(ticks) : ticks;
    const std::int64_t seconds_total = magnitude / kRaceTicksPerSecond;
    const std::int64_t milliseconds = (magnitude - seconds_total * kRaceTicksPerSecond) * 1000 /
                                      kRaceTicksPerSecond;
    const std::int64_t hundredths = milliseconds / 10;
    const std::int64_t minutes = seconds_total / 60;
    const std::int64_t seconds = seconds_total % 60;
    std::string text = negative ? "-" : "";
    text += std::to_string(minutes);
    text += ':';
    text += static_cast<char>('0' + seconds / 10);
    text += static_cast<char>('0' + seconds % 10);
    text += '.';
    text += static_cast<char>('0' + hundredths / 10);
    text += static_cast<char>('0' + hundredths % 10);
    return text;
}

const char* race_stage_name(RaceStage stage) noexcept {
    switch (stage) {
    case RaceStage::none:
        return "none";
    case RaceStage::flyby:
        return "flyby";
    case RaceStage::prerace:
        return "prerace";
    case RaceStage::countdown:
        return "countdown";
    case RaceStage::racing:
        return "racing";
    }
    return "unknown";
}

const char* racer_status_name(RacerStatus status) noexcept {
    switch (status) {
    case RacerStatus::blown_engine: return "blown_engine";
    case RacerStatus::totalled: return "totalled";
    case RacerStatus::out_of_time: return "out-of-time";
    case RacerStatus::not_finished:
        return "not-finished";
    case RacerStatus::normal_completion:
        return "finished";
    case RacerStatus::knocked_out:
        return "knocked-out";
    }
    return "unknown";
}

const char* race_outcome_name(RaceOutcome outcome) noexcept {
    switch (outcome) {
    case RaceOutcome::running:
        return "running";
    case RaceOutcome::finished:
        return "finished";
    case RaceOutcome::quit:
        return "quit";
    case RaceOutcome::clock_limit:
        return "clock-limit";
    }
    return "unknown";
}

const char* race_timing_event_kind_name(RaceTimingEventKind kind) noexcept {
    switch (kind) {
    case RaceTimingEventKind::stage:
        return "stage";
    case RaceTimingEventKind::countdown:
        return "countdown";
    case RaceTimingEventKind::go:
        return "go";
    case RaceTimingEventKind::split:
        return "split";
    case RaceTimingEventKind::lap:
        return "lap";
    case RaceTimingEventKind::untimed:
        return "untimed";
    case RaceTimingEventKind::finished:
        return "finished";
    case RaceTimingEventKind::wrong_way_on:
        return "wrong-way-on";
    case RaceTimingEventKind::wrong_way_off:
        return "wrong-way-off";
    case RaceTimingEventKind::reset_zone:
        return "reset-zone";
    case RaceTimingEventKind::off_path:
        return "off-path";
    case RaceTimingEventKind::restarted:
        return "restarted";
    case RaceTimingEventKind::quit:
        return "quit";
    case RaceTimingEventKind::clock_limit:
        return "clock-limit";
    }
    return "unknown";
}

bool make_race_timing_config(int steps_per_second, std::size_t racers,
                             RaceTimingConfig& config, std::string& error) {
    const std::int32_t ticks = race_ticks_per_frame(steps_per_second);
    if (ticks < 1) {
        error = "a step rate of " + std::to_string(steps_per_second) +
                " a second adds no ticks to the world timer";
        return false;
    }
    config = RaceTimingConfig{};
    config.steps_per_second = steps_per_second;
    config.ticks_per_step = ticks;
    config.racers = racers;
    config.humans = racers == 0 ? 0 : 1;
    return true;
}

bool start_race_timing(const RaceRoute& route, const RaceTimingConfig& config,
                       RaceTimingState& state, std::string& error) {
    if (!check_config(route, config, error)) {
        return false;
    }
    state.restarts = 0;
    build_race(config, state);
    return true;
}

bool set_race_stage(RaceTimingState& state, RaceStage stage,
                    std::vector<RaceTimingEvent>& events) {
    if (state.outcome != RaceOutcome::running || stage == RaceStage::racing ||
        static_cast<int>(stage) <= static_cast<int>(state.stage)) {
        return false;
    }
    state.stage = stage;
    emit(events, RaceTimingEventKind::stage, state, state.now_ticks);
    events.back().value = static_cast<std::int32_t>(stage);
    if (stage == RaceStage::countdown) {
        // 0x00434C61..0x00434C73: +0x08 takes the world timer.
        state.countdown_valid = true;
        state.countdown_start_ticks = state.now_ticks;
    }
    return true;
}

int race_countdown_value(const RaceTimingConfig& config, const RaceTimingState& state) noexcept {
    return countdown_value_at(config, state, state.step_ticks);
}

bool race_car_held(const RaceTimingState& state) noexcept {
    return state.stage != RaceStage::racing;
}

bool step_race_timing(const RaceRoute& route, const RaceTimingConfig& config,
                      RaceTimingState& state, const RaceTimingInput* inputs,
                      std::vector<RaceTimingEvent>& events, std::string& error) {
    if (state.outcome == RaceOutcome::quit || state.outcome == RaceOutcome::clock_limit) {
        error = std::string("the race has ended: ") + race_outcome_name(state.outcome);
        return false;
    }
    if (state.racers.size() != config.racers) {
        error = "the race was not started with this configuration";
        return false;
    }
    if (!check_inputs(route, config, inputs, error)) {
        return false;
    }
    if (state.now_ticks > kRaceTicksLimit - config.ticks_per_step) {
        state.outcome = RaceOutcome::clock_limit;
        emit(events, RaceTimingEventKind::clock_limit, state, state.now_ticks);
        error = "the world timer would pass " + std::to_string(kRaceTicksLimit) + " ticks";
        return false;
    }
    const std::int32_t now = state.now_ticks;
    state.step_ticks = now;
    for (RaceRacerTiming& racer : state.racers) {
        // 0x004338C2..0x004338C6: the step's flags start clear.
        racer.new_lap = false;
        racer.new_checkpoint = false;
    }

    // 0x00435570..0x004355B5: the countdown's end.
    if (state.stage == RaceStage::countdown && countdown_value_at(config, state, now) == 4) {
        go(route, state, now, events);
    }
    const int shown = countdown_value_at(config, state, now);
    if (shown != state.countdown_shown) {
        state.countdown_shown = shown;
        emit(events, RaceTimingEventKind::countdown, state, now);
        events.back().value = shown;
    }
    // 0x004355BA..0x004355EF: the race time.
    if (state.stage == RaceStage::racing && state.race_start_valid) {
        state.race_ticks = race_quantize_ticks(wrap_sub(now, state.race_start_ticks));
    }

    // 0x004355F1..0x0043563C: every racer not finished.
    for (std::size_t r = 0; r < state.racers.size(); ++r) {
        RaceRacerTiming& racer = state.racers[r];
        if (racer.status != RacerStatus::not_finished) {
            continue;
        }
        const RaceTimingInput& input = inputs[r];
        for (std::size_t e = 0; e < input.event_count; ++e) {
            const RaceProgressEvent& event = input.events[e];
            switch (event.kind) {
            case RaceEventKind::crossed_forward:
                racer.lap = event.lap;
                time_crossing(route, state, r, event, input.speed, now, events);
                break;
            case RaceEventKind::crossed_backward:
                racer.lap = event.lap;
                break;
            case RaceEventKind::lap_complete:
                // 0x00433973..0x00433987 and the HUD's hold (0x004A19D7..
                // 0x004A19F2), which reads the finish line's split.
                racer.lap = event.lap;
                racer.highest_lap = event.lap;
                racer.new_lap = true;
                racer.lap_shown_valid = true;
                racer.lap_shown_since = now;
                racer.lap_shown_ticks =
                    racer.split_ticks[static_cast<std::size_t>(route.finish)];
                break;
            case RaceEventKind::wrong_way_on:
                if (!racer.wrong_way) {
                    racer.wrong_way = true;
                    racer.wrong_way_since = now;
                    emit(events, RaceTimingEventKind::wrong_way_on, state, now,
                         static_cast<int>(r));
                }
                break;
            case RaceEventKind::wrong_way_off:
                if (racer.wrong_way) {
                    racer.wrong_way = false;
                    emit(events, RaceTimingEventKind::wrong_way_off, state, now,
                         static_cast<int>(r));
                    events.back().ticks = wrap_sub(now, racer.wrong_way_since);
                }
                break;
            case RaceEventKind::reset_zone:
                ++racer.resets;
                emit(events, RaceTimingEventKind::reset_zone, state, now, static_cast<int>(r));
                events.back().value = event.index;
                break;
            case RaceEventKind::off_path:
                emit(events, RaceTimingEventKind::off_path, state, now, static_cast<int>(r));
                events.back().value = event.index;
                break;
            case RaceEventKind::missed_forward:
            case RaceEventKind::missed_backward:
            case RaceEventKind::recovered:
            case RaceEventKind::finished:
                // Nothing the timing reads: a missed boundary is no
                // crossing, and the finish is decided below.
                break;
            }
        }
        if (racer.wrong_way) {
            ++racer.wrong_way_steps;
        }
        // 0x0043560F..0x00435630: a human racer's best lap.
        if (r < config.humans) {
            const std::int32_t best =
                racer.best_split_ticks[static_cast<std::size_t>(route.finish)];
            if (best < state.fastest_lap_ticks) {
                state.fastest_lap_ticks = best;
            }
        }
    }

    //Career type4 uses native runtime mode2 (4B5FDF), not challenge
    //byte78A346.4358FB accepts an under-limit completed split; the normal
    //finish below still completes an over-limit route.5A3098 then decides
    //career success from its recorded time. Never apply435875's unrelated
    //challenge expiry/reason8 merely because a career deadline exists.
    // 0x00435A17..0x00435A5D: a race with laps finishes every racer whose
    // highest lap equals them, through 0x004350D0 with 1.
    if (route.laps != 0) {
        for (std::size_t r = 0; r < state.racers.size(); ++r) {
            RaceRacerTiming& racer = state.racers[r];
            if (racer.status != RacerStatus::not_finished || racer.highest_lap != route.laps) {
                continue;
            }
            const auto finish = static_cast<std::size_t>(route.finish);
            racer.status = RacerStatus::normal_completion;
            racer.position = first_free_place(state);
            racer.finish_valid = racer.crossing_valid[finish] && state.race_start_valid;
            racer.finish_ticks = wrap_sub(racer.crossing_ticks[finish], state.race_start_ticks);
            racer.finish_step = state.step;
            emit(events, RaceTimingEventKind::finished, state, now, static_cast<int>(r));
            events.back().value = racer.position;
            events.back().ticks = racer.finish_ticks;
            events.back().lap = racer.lap;
        }
    }
    if (state.outcome == RaceOutcome::running && config.humans > 0) {
        bool all = true;
        for (std::size_t r = 0; r < config.humans; ++r) {
            all = all && state.racers[r].status != RacerStatus::not_finished;
        }
        if (all) {
            state.outcome = RaceOutcome::finished;
        }
    }

    // 0x00585E00, at the end of the frame (0x00447A1A).
    ++state.step;
    state.now_ticks = wrap_add(now, config.ticks_per_step);
    return true;
}

bool restart_race_timing(const RaceRoute& route, const RaceTimingConfig& config,
                         RaceTimingState& state, std::vector<RaceTimingEvent>& events,
                         std::string& error) {
    if (state.outcome == RaceOutcome::quit) {
        error = "the race was quit";
        return false;
    }
    if (!check_config(route, config, error)) {
        return false;
    }
    const std::size_t step = state.step;
    const std::int32_t now = state.now_ticks;
    ++state.restarts;
    build_race(config, state);
    RaceTimingEvent event;
    event.kind = RaceTimingEventKind::restarted;
    event.step = step;
    event.now = now;
    event.value = static_cast<std::int32_t>(state.restarts);
    events.push_back(event);
    return true;
}

bool quit_race_timing(RaceTimingState& state, std::vector<RaceTimingEvent>& events) {
    if (state.outcome == RaceOutcome::quit) {
        return false;
    }
    state.outcome = RaceOutcome::quit;
    emit(events, RaceTimingEventKind::quit, state, state.now_ticks);
    return true;
}

std::vector<RaceResultRow> race_timing_results(const RaceRoute& route,
                                               const RaceTimingState& state) {
    std::vector<RaceResultRow> rows;
    const auto finish = static_cast<std::size_t>(
        route.finish >= 0 && static_cast<std::size_t>(route.finish) < kRaceBoundaryLimit
            ? route.finish
            : 0);
    for (std::size_t r = 0; r < state.racers.size(); ++r) {
        const RaceRacerTiming& racer = state.racers[r];
        RaceResultRow row;
        row.racer = r;
        row.status = racer.status;
        row.position = racer.position;
        row.total_valid = racer.finish_valid;
        row.total_ticks = racer.finish_ticks;
        row.best_lap_ticks = racer.best_split_ticks[finish];
        row.laps = racer.highest_lap < 0 ? 0 : racer.highest_lap;
        rows.push_back(row);
    }
    // Finished racers by place, then the others by number; a stable
    // insertion sort, since a race holds at most 64.
    for (std::size_t i = 1; i < rows.size(); ++i) {
        for (std::size_t j = i; j > 0; --j) {
            const RaceResultRow& a = rows[j - 1];
            const RaceResultRow& b = rows[j];
            const bool a_placed = a.position > 0;
            const bool b_placed = b.position > 0;
            const bool swap = (!a_placed && b_placed) ||
                              (a_placed && b_placed && b.position < a.position);
            if (!swap) {
                break;
            }
            const RaceResultRow held = rows[j - 1];
            rows[j - 1] = rows[j];
            rows[j] = held;
        }
    }
    return rows;
}

bool race_timing_hud(const RaceRoute& route, const RaceTimingConfig& config,
                     const RaceTimingState& state, std::size_t racer, RaceHud& hud) {
    if (racer >= state.racers.size() || route.finish < 0 ||
        static_cast<std::size_t>(route.finish) >= kRaceBoundaryLimit) {
        return false;
    }
    const RaceRacerTiming& record = state.racers[racer];
    const std::int32_t now = state.step_ticks;
    RaceHud result;
    result.countdown = countdown_value_at(config, state, now);
    result.race_time_valid = state.race_start_valid;
    result.race_ticks = state.race_ticks;
    if (record.crossing_valid[0]) {
        result.lap_time_valid = true;
        // 0x004A19F5..0x004A1A28: the completed lap stays while it is at
        // most 3 s old.
        if (record.lap_shown_valid &&
            ticks_to_seconds(wrap_sub(now, record.lap_shown_since)) <= kRaceLapHoldSeconds) {
            result.lap_time_held = true;
            result.lap_ticks = record.lap_shown_ticks;
        } else {
            result.lap_ticks = wrap_sub(now, record.crossing_ticks[0]);
        }
    }
    // 0x004A1A91..0x004A1ABB: a best lap of 0 shows "--:--:--" (0x006C72DC);
    // any other, the empty kRaceNoTime included, goes through 0x00586580.
    const std::int32_t best = record.best_split_ticks[static_cast<std::size_t>(route.finish)];
    result.best_lap_valid = best != 0;
    result.best_lap_ticks = best;
    result.lap = record.lap < 0 ? 0 : record.lap;
    result.laps = route.laps;
    result.wrong_way = record.wrong_way;
    result.finished = record.status != RacerStatus::not_finished;
    result.position = record.position;
    hud = result;
    return true;
}

bool make_race_step_clock(int steps_per_second, std::uint32_t max_steps,
                          RaceStepClock& clock, std::string& error) {
    if (steps_per_second < 1 || steps_per_second > 1000) {
        error = "the step clock runs 1 to 1000 steps a second, not " +
                std::to_string(steps_per_second);
        return false;
    }
    if (max_steps == 0) {
        error = "the step clock must hand out at least one step at a time";
        return false;
    }
    clock = RaceStepClock{};
    clock.steps_per_second = steps_per_second;
    clock.max_steps = max_steps;
    return true;
}

std::uint32_t advance_race_step_clock(RaceStepClock& clock, std::uint64_t now_us) noexcept {
    if (clock.steps_per_second < 1 || clock.steps_per_second > 1000 || clock.max_steps == 0) {
        return 0;
    }
    if (!clock.started) {
        clock.started = true;
        clock.origin_us = now_us;
        clock.last_us = now_us;
    }
    if (now_us < clock.last_us) {
        now_us = clock.last_us;
    }
    clock.last_us = now_us;
    const std::uint64_t elapsed = now_us - clock.origin_us;
    const auto rate = static_cast<std::uint64_t>(clock.steps_per_second);
    const std::uint64_t due =
        (elapsed / 1000000U) * rate + (elapsed % 1000000U) * rate / 1000000U + 1U;
    const std::uint64_t handed = clock.steps + clock.dropped;
    if (due <= handed) {
        return 0;
    }
    std::uint64_t owed = due - handed;
    if (owed > clock.max_steps) {
        clock.dropped += owed - clock.max_steps;
        owed = clock.max_steps;
    }
    clock.steps += owed;
    return static_cast<std::uint32_t>(owed);
}

std::uint32_t advance_race_step_clock(RaceStepClock& clock, TimingService& timing) {
    return advance_race_step_clock(clock, timing.now_us());
}

namespace {
bool racer_temporary_ghost(const RaceRacerTiming& r,std::int32_t now) noexcept {
    return r.ghost_start_ticks!=0 && r.ghost_start_ticks!=kRaceNoTime &&
        float(now-r.ghost_start_ticks)*.00025F<r.ghost_seconds;
}
}
bool race_racer_body_ghost(const RaceRacerTiming& r,std::int32_t now) noexcept {
    return r.permanent_ghost||racer_temporary_ghost(r,now);
}
bool race_racer_hidden(const RaceRacerTiming& r,std::int32_t now,float dt,std::uint64_t frame) noexcept {
    if(racer_temporary_ghost(r,now)&&std::abs(dt)>.001F)return (frame&1U)!=0;
    return r.permanent_ghost;
}

bool apply_lap_knockout(const RaceTimingConfig& config,RaceTimingState& state,
                        const unsigned* order,std::size_t count,std::string& error) {
    if(!order || count!=state.racers.size() || count!=config.racers || count<2 || count>4 || config.humans>count) {
        error="invalid knockout ranking";return false;
    }
    unsigned seen=0;
    for(std::size_t i=0;i<count;++i) {
        if(order[i]>=count || (seen&(1U<<order[i]))){error="invalid knockout rank permutation";return false;}
        seen|=1U<<order[i];
    }
    for(std::size_t i=0;i<count;++i) {
        const auto& trigger=state.racers[order[i]];
        if(!trigger.new_lap || trigger.highest_lap+int(i)+1!=int(count))continue;
        for(std::size_t j=i+1;j<count;++j) {
            auto& victim=state.racers[order[j]];
            if(victim.status!=RacerStatus::not_finished)continue;
            victim.status=RacerStatus::knocked_out;
            victim.position=int(j)+1;
            victim.finish_valid=false;
            victim.finish_step=state.step;
            //435740 passes hide=true only for a non-human actor;435344
            //keeps its stop action/body but sets permanent ghost and2s blink.
            if(order[j]>=config.humans) {
                victim.ghost_start_ticks=state.now_ticks;victim.ghost_seconds=2.F;
                victim.permanent_ghost=true;
            }
        }
    }
    if(config.humans && std::all_of(state.racers.begin(),state.racers.begin()+config.humans,
        [](const auto& r){return r.status!=RacerStatus::not_finished;}))state.outcome=RaceOutcome::finished;
    error.clear();return true;
}

} // namespace nfsu
