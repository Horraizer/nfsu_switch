#pragma once

#include "nfsu/platform.h"
#include "nfsu/race_route.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// The race state machine of one race: the stages from staging through the
// countdown to racing, the race clock, split, lap and total times, the
// finish, the results, a restart and the ways out of a race that end
// without a result. docs/RACE_TIMING.md carries the disassembly every rule
// and constant below is named from, what is approximation and what is
// unknown.
//
// It is driven by fixed steps and by the per-step progress events of
// nfsu::step_race_progress (include/nfsu/race_route.h), never by a car: a
// step is one frame of the game's main loop, and its time on the race
// clock is a fixed number of ticks, so a replay gives the same answer on
// every platform. Real time enters only through RaceStepClock below, which
// turns the platform's TimingService into a whole number of steps. It
// reads no clock of its own and calls no transcendental function or square
// root; its float arithmetic is the retail order of operations, compiled
// without contraction (cmake/race_libraries.cmake).

// ---------------------------------------------------------------------
// The clock
// ---------------------------------------------------------------------

// The world timer 0x0073AD34 counts ticks of 1/4000 s: 0x00585E6C..
// 0x00585E90 add (int)(dt * [0x006CC9FC] + [0x006CC7DC]) = (int)(dt * 4000
// + 0.5) a frame, and every reader turns ticks back into seconds with
// [0x006CCA38] = 0.00025 (0x3983126F).
constexpr std::int32_t kRaceTicksPerSecond = 4000;
// 0x0041F7DA: the world a race builds (0x0041F6E0, from 0x00421FB9) sets
// the timer to 0xFA0, and the teardown a restart runs sets it there again
// (0x0041FB22, 0x00422130 -> 0x0041F9B0).
constexpr std::int32_t kRaceWorldStartTicks = 4000;
// 0x00434316..0x00434327: the countdown step at +0x0C of the race status,
// [0x006B79E0] = 0.9 (0x3F666666), or [0x006B79E4] = 1.0 when 0x0078A368 is
// set. A career race does not set it (docs/RACE_TIMING.md).
constexpr float kRaceCountdownStepSeconds = 0.9F;
constexpr float kRaceCountdownStepSecondsFlagged = 1.0F;
// The per-lap arrays of the lap record: eleven slots (0x004343BE `mov ecx,
// 0xb`, 0x00433A31 `cmp cl, 0xb`).
constexpr int kRaceLapSlots = 11;
// 0x004342F5 and 0x00433408: the value an empty best time holds.
constexpr std::int32_t kRaceNoTime = 0x7FFFFFFF;
// The HUD keeps a completed lap's time on screen for 3 s ([0x006CC7AC],
// 0x004A1A0A..0x004A1A1B) before it shows the running lap again.
constexpr float kRaceLapHoldSeconds = 3.0F;
// Not a retail rule: the largest world time this model steps to, 2^29
// ticks (37 hours). Below it every tick count converts to seconds exactly
// as the x87 does (docs/RACE_TIMING.md), and no difference or sum of two
// times can overflow 32 bits.
constexpr std::int32_t kRaceTicksLimit = 1 << 29;

// 0x00586690 and the `fmul [0x006CC9FC]`, `fadd [0x006CC7DC]`, `_ftol` its
// five callers follow it with: a time in ticks rounded to the nearest
// hundredth of a second, half up, in float as the executable computes it.
// It gives 40 * n for most n hundredths and 40 * n + 1 for a few, where the
// float product of n, 0.01 and 4000 lands just above 40 * n and 0x00438800
// rounds it up. Exact for |ticks| < 2^29.
std::int32_t race_quantize_ticks(std::int32_t ticks) noexcept;

// The ticks one frame of 1 / frames_per_second adds to the world timer:
// 0x00585CD5..0x00585CE8 turn the frame into whole 3600ths of a second,
// (int)(dt * 3600 + 0.5), 0x00585D52..0x00585D63 multiply them back by
// [0x006CC7E4] = 1/3600, and 0x00585E6C..0x00585E7E take (int)(dt * 4000 +
// 0.5) of that. 60 frames a second give 67 ticks a frame, not 66.67: the
// retail clock runs 0.5 percent fast at 60 frames a second. 0 for a rate
// below 1 or above 3600.
std::int32_t race_ticks_per_frame(int frames_per_second) noexcept;

// A time in ticks as M:SS.hh, split as 0x005864F0 splits it: whole seconds
// ticks / 4000, milliseconds (ticks % 4000) * 1000 / 4000, hundredths
// milliseconds / 10, all truncated; minutes include the hours. A negative
// time is its magnitude with a leading '-'. kRaceNoTime is "--:--.--",
// which is what 0x00586580 prints for it.
std::string format_race_ticks(std::int32_t ticks);

// ---------------------------------------------------------------------
// Stages and states
// ---------------------------------------------------------------------

// The stage of the race status (+0x00), in the executable's own order and
// names (the table at 0x00700A88: RACE_STAGE_NONE .. RACE_STAGE_RACING).
enum class RaceStage : std::uint8_t {
    none = 0,
    flyby = 1,
    prerace = 2,
    countdown = 3,
    racing = 4
};

const char* race_stage_name(RaceStage stage) noexcept;

// A racer's status (+0x10 of its lap record). Retail has more, named by the
// table at 0x00700A5C and reached by knockouts, time limits and the online
// game; one race of the first lap needs these two.
enum class RacerStatus : std::uint8_t {
    not_finished = 0,
    normal_completion = 1
};

const char* racer_status_name(RacerStatus status) noexcept;

// How the race stands. `finished` is every human racer finished;
// `quit` is RC_QUIT_RACE (0x00422BE8), which leaves without a result;
// `clock_limit` is this project's stop at kRaceTicksLimit.
enum class RaceOutcome : std::uint8_t {
    running,
    finished,
    quit,
    clock_limit
};

const char* race_outcome_name(RaceOutcome outcome) noexcept;

struct RaceTimingConfig {
    // Steps a second, and the ticks each adds to the world timer.
    int steps_per_second = 60;
    std::int32_t ticks_per_step = 67;
    float countdown_step_seconds = kRaceCountdownStepSeconds;
    // The world timer when the race is built.
    std::int32_t start_ticks = kRaceWorldStartTicks;
    // Racers 0 .. humans - 1 are the players; the rest are opponents.
    std::size_t racers = 1;
    std::size_t humans = 1;
};

// The configuration of a race run at steps_per_second steps a second, with
// race_ticks_per_frame of that rate. False, with a reason, for a rate that
// gives no ticks.
bool make_race_timing_config(int steps_per_second, std::size_t racers,
                             RaceTimingConfig& config, std::string& error);

// One racer's lap record, the 0x13C bytes 0x004333C0 initialises, cut to
// what the timing reads and writes. The retail offsets are named.
struct RaceRacerTiming {
    RacerStatus status = RacerStatus::not_finished;
    // +0x08 and +0x09: the lap counter and the highest lap, as the progress
    // events report them.
    int lap = -1;
    int highest_lap = 0;
    // +0x04 and +0x06: 5 * lap + boundary of the last forward crossing and
    // the highest of those, signed 16-bit; the highest is -1 until go sets
    // it to 0 (0x00434C33).
    int checkpoint = -1;
    int highest_checkpoint = -1;
    // +0x0D and +0x0E: a new highest lap and a new highest checkpoint on
    // the last step.
    bool new_lap = false;
    bool new_checkpoint = false;
    // +0x24: the time of the last timed crossing of each boundary. Slot 0,
    // the start line's, is the lap start: go sets it (0x00434C0A) and every
    // timed crossing of the finish line moves it (0x00433A6D). Retail marks
    // an empty slot 0; this model keeps a flag.
    bool crossing_valid[kRaceBoundaryLimit] = {false, false, false, false, false};
    std::int32_t crossing_ticks[kRaceBoundaryLimit] = {0, 0, 0, 0, 0};
    // +0x38 and +0x4C: the time from the lap start to each boundary on this
    // lap and the one before (0x004339FE..0x00433A0B).
    std::int32_t split_ticks[kRaceBoundaryLimit] = {0, 0, 0, 0, 0};
    std::int32_t previous_split_ticks[kRaceBoundaryLimit] = {0, 0, 0, 0, 0};
    // +0x60 and +0x74: the best split at each boundary and the best before
    // it (0x00433A0E..0x00433A1C); at the finish line, the best lap.
    std::int32_t best_split_ticks[kRaceBoundaryLimit] = {
        kRaceNoTime, kRaceNoTime, kRaceNoTime, kRaceNoTime, kRaceNoTime};
    std::int32_t previous_best_split_ticks[kRaceBoundaryLimit] = {
        kRaceNoTime, kRaceNoTime, kRaceNoTime, kRaceNoTime, kRaceNoTime};
    // +0x88 and +0xB4: each lap's time and the race time at its end, by the
    // lap counter after the crossing (1 for the first lap); slot 0 is never
    // written (0x00433A2A..0x00433A66).
    std::int32_t lap_ticks[kRaceLapSlots] = {};
    std::int32_t lap_total_ticks[kRaceLapSlots] = {};
    // +0x0C: the finishing place, 0 until finished (0x00435BD0).
    int position = 0;
    // +0x110: the race time at the finish, the last finish-line crossing
    // less the race start (0x0043538C..0x004353B5).
    bool finish_valid = false;
    std::int32_t finish_ticks = 0;
    std::size_t finish_step = 0;
    // What the HUD's lap timer holds after a completed lap (0x004A19D7..
    // 0x004A19F2): the world time the lap completed and its time.
    bool lap_shown_valid = false;
    std::int32_t lap_shown_since = 0;
    std::int32_t lap_shown_ticks = 0;
    // This project's record of the two conditions that end nothing in a
    // single-player race: the wrong-way flag and since when, the steps it
    // has been on in total, and the reset zones entered.
    bool wrong_way = false;
    std::int32_t wrong_way_since = 0;
    std::size_t wrong_way_steps = 0;
    std::size_t resets = 0;
};

// The race status (0x00434270, kept at 0x0073619C), cut the same way.
struct RaceTimingState {
    RaceStage stage = RaceStage::none;
    RaceOutcome outcome = RaceOutcome::running;
    // Steps processed since the race was built, the world timer of the next
    // step, and that of the last one processed.
    std::size_t step = 0;
    std::int32_t now_ticks = kRaceWorldStartTicks;
    std::int32_t step_ticks = kRaceWorldStartTicks;
    // +0x08: when the countdown began; +0x04: when the race went.
    bool countdown_valid = false;
    std::int32_t countdown_start_ticks = 0;
    bool go_valid = false;
    std::int32_t go_ticks = 0;
    std::size_t go_step = 0;
    // +0x10, the race start rounded to 1/100 s (0x00434C7A..0x00434CA6),
    // and +0x14, the race time (0x004355C2..0x004355EF).
    bool race_start_valid = false;
    std::int32_t race_start_ticks = 0;
    std::int32_t race_ticks = 0;
    // +0x18: the best lap any human racer has driven (0x0043560F..
    // 0x00435630).
    std::int32_t fastest_lap_ticks = kRaceNoTime;
    // The countdown value last reported, and how many restarts.
    int countdown_shown = 0;
    std::size_t restarts = 0;
    std::vector<RaceRacerTiming> racers;
};

// ---------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------

enum class RaceTimingEventKind : std::uint8_t {
    // The stage changed (value is the new RaceStage).
    stage,
    // The countdown the HUD shows changed: 3, 2, 1, 4 for go, 0 for none
    // (0x00434D10).
    countdown,
    // The race went: ticks is the race start.
    go,
    // A timed forward crossing of a boundary that is not the finish line:
    // ticks is the split, crossing the crossing time, value the backdate.
    split,
    // A timed forward crossing of the finish line: ticks is the lap time,
    // value the backdate; lap is the lap counter after it.
    lap,
    // A forward crossing that timed nothing: value 1 when it raised no
    // checkpoint (the first crossing of the line after go is one), 2 when
    // there was no lap start yet (before go).
    untimed,
    // The racer finished: value is its position, ticks its race time.
    finished,
    wrong_way_on,
    wrong_way_off,
    // A reset zone entered (value is the zone): the car is put back and
    // the clock runs on.
    reset_zone,
    // The coordinate went beyond 5000 of its node: the car is flagged for a
    // reset (+0x87C), and the clock runs on.
    off_path,
    restarted,
    quit,
    clock_limit
};

const char* race_timing_event_kind_name(RaceTimingEventKind kind) noexcept;

struct RaceTimingEvent {
    RaceTimingEventKind kind = RaceTimingEventKind::stage;
    // The step it happened on and the world timer of that step.
    std::size_t step = 0;
    std::int32_t now = 0;
    int racer = -1;
    int boundary = -1;
    int lap = 0;
    std::int32_t value = 0;
    std::int32_t ticks = 0;
    std::int32_t crossing = 0;
};

// ---------------------------------------------------------------------
// The machine
// ---------------------------------------------------------------------

// What one racer brings to a step: the progress events nfsu::
// step_race_progress appended for its position this step, in order, and
// the car's speed in units a second (+0x3D0, which 0x00433761 reads to
// backdate a crossing).
struct RaceTimingInput {
    const RaceProgressEvent* events = nullptr;
    std::size_t event_count = 0;
    float speed = 0.0F;
};

// 0x00434270 and 0x00434590: a race built, at stage none, with every
// racer's lap record fresh and the world timer at config.start_ticks.
// False, with a reason, and the state untouched, for a configuration or a
// route this model does not run: no racer, more than 64, more humans than
// racers, a step rate below 1, ticks a step below 1 or above 4000, a
// countdown step that is not finite and positive, a world start outside 1
// .. kRaceTicksLimit, a route without boundaries or with more than
// kRaceBoundaryLimit, a finish line that is not one of them, or a
// point-to-point route (docs/RACE_TIMING.md).
bool start_race_timing(const RaceRoute& route, const RaceTimingConfig& config,
                       RaceTimingState& state, std::string& error);

// 0x00434B50 for a stage before racing: none to flyby to prerace to
// countdown, forwards only, and a stage may be skipped. The countdown
// takes the world timer of the next step as its start (0x00434C6A). False
// for any other change, and for a race that is not running; racing is
// reached only by the countdown.
bool set_race_stage(RaceTimingState& state, RaceStage stage,
                    std::vector<RaceTimingEvent>& events);

// 0x00434D10 at the world time of the last step processed (or the next,
// before the first): 3, 2 and 1 in the three countdown steps, 4 from the
// end of the third until the race goes and while the race is at most one
// countdown step old, 0 otherwise.
int race_countdown_value(const RaceTimingConfig& config, const RaceTimingState& state) noexcept;

// Whether a racer's car is held where it stands for the next step: every
// stage but racing holds it (0x00434BB0..0x00434C00 call the driver's hold
// method, vtable +0x1C, for them and its release, +0x20, for racing), and
// go comes in the race status update after the frame's physics
// (0x00421137..0x00421155), so a car first moves on the step after go.
bool race_car_held(const RaceTimingState& state) noexcept;

// One frame, in the order 0x00435570 runs it after the frame's physics:
// the countdown's go (0x00435570..0x004355B5), the race time
// (0x004355BA..0x004355EF), each unfinished racer's events (0x004338C0,
// 0x00435600..0x00435630), the finish (0x00435A17..0x00435A5D); then the
// world timer advances (0x00585E00). `inputs` holds config.racers entries.
// False, with a reason and nothing changed, for a race that has ended
// (quit, clock limit), for inputs that do not describe this route - a
// boundary index outside it, a lap outside -128..127, a percent that is not
// finite, a speed that is not finite or is negative - and for a step that
// would take the world timer past kRaceTicksLimit, which ends the race
// with outcome clock_limit instead.
bool step_race_timing(const RaceRoute& route, const RaceTimingConfig& config,
                      RaceTimingState& state, const RaceTimingInput* inputs,
                      std::vector<RaceTimingEvent>& events, std::string& error);

// RC_RESTART_RACE (0x004243C1 in the pause state, then 0x00422BD4): the
// race torn down and built again - a fresh race status and lap records at
// stage none, and the world timer back at config.start_ticks, as the
// teardown's world reset leaves it. The caller starts each racer's route
// progress again from the grid. False for a race that was quit.
bool restart_race_timing(const RaceRoute& route, const RaceTimingConfig& config,
                         RaceTimingState& state, std::vector<RaceTimingEvent>& events,
                         std::string& error);

// RC_QUIT_RACE (0x00422BE8): the race left without a result. False when it
// already was.
bool quit_race_timing(RaceTimingState& state, std::vector<RaceTimingEvent>& events);

// ---------------------------------------------------------------------
// Results and the HUD
// ---------------------------------------------------------------------

struct RaceResultRow {
    std::size_t racer = 0;
    RacerStatus status = RacerStatus::not_finished;
    int position = 0;
    bool total_valid = false;
    std::int32_t total_ticks = 0;
    // The best lap, kRaceNoTime before the first.
    std::int32_t best_lap_ticks = kRaceNoTime;
    // Laps completed: the highest lap, never below 0.
    int laps = 0;
};

// Every racer: the finished by place, then the rest by racer number.
std::vector<RaceResultRow> race_timing_results(const RaceRoute& route,
                                               const RaceTimingState& state);

// What the HUD of one racer reads on the last step processed.
struct RaceHud {
    // 0x00434D10, as race_countdown_value.
    int countdown = 0;
    // The race time, +0x14 of the race status, once the race has gone.
    bool race_time_valid = false;
    std::int32_t race_ticks = 0;
    // The lap timer (0x004A19B1..0x004A1A38): the world time since the lap
    // start, unrounded, or for kRaceLapHoldSeconds after a completed lap
    // that lap's time; invalid before the race goes.
    bool lap_time_valid = false;
    bool lap_time_held = false;
    std::int32_t lap_ticks = 0;
    // The best lap at the finish line (0x004A1A91..0x004A1AE0).
    bool best_lap_valid = false;
    std::int32_t best_lap_ticks = 0;
    // Laps completed (the lap counter, never below 0) and the race's.
    int lap = 0;
    int laps = 0;
    bool wrong_way = false;
    bool finished = false;
    int position = 0;
};

// False, with the HUD untouched, for a racer the race does not hold.
bool race_timing_hud(const RaceRoute& route, const RaceTimingConfig& config,
                     const RaceTimingState& state, std::size_t racer, RaceHud& hud);

// ---------------------------------------------------------------------
// Real time
// ---------------------------------------------------------------------

// Not a retail rule: the fixed-step clock a port runs the machine by. Each
// call takes the platform clock's microseconds and hands out the steps now
// due - step k is due k / steps_per_second after the first call, which
// hands out step 0 - never more than max_steps at once; a backlog beyond
// that (a stall, a suspended console) is dropped rather than replayed, and
// a reading earlier than the last is taken as the last. Integer
// arithmetic only, so a fake clock gives the same steps everywhere.
struct RaceStepClock {
    int steps_per_second = 60;
    std::uint32_t max_steps = 4;
    bool started = false;
    std::uint64_t origin_us = 0;
    std::uint64_t last_us = 0;
    // Steps handed out, and steps dropped by the cap.
    std::uint64_t steps = 0;
    std::uint64_t dropped = 0;
};

// False, with a reason, for a rate outside 1 .. 1000 or a cap of 0.
bool make_race_step_clock(int steps_per_second, std::uint32_t max_steps,
                          RaceStepClock& clock, std::string& error);

std::uint32_t advance_race_step_clock(RaceStepClock& clock, std::uint64_t now_us) noexcept;

// The same, reading timing.now_us() once.
std::uint32_t advance_race_step_clock(RaceStepClock& clock, TimingService& timing);

} // namespace nfsu
