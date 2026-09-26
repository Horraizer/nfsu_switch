#include "nfsu/platform_fakes.h"
#include "nfsu/race_route.h"
#include "nfsu/race_timing.h"
#include "nfsu/sha256.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

using Events = std::vector<nfsu::RaceProgressEvent>;
using Timing = std::vector<nfsu::RaceTimingEvent>;

// A route made by hand: a path 1000 long, the start and finish line at 25
// percent, split 1 at 50 and split 2 at 75, as build_race_route would lay
// out a circuit's three boundaries.
nfsu::RaceRoute hand_route(int laps) {
    nfsu::RaceRoute route;
    route.laps = laps;
    route.start = 0;
    route.finish = 0;
    route.spline.path_length = 1000.0F;
    route.boundaries = {{"ForwardStartLine", -1, 25.0F},
                        {"SplitTimeBoundary1", -1, 50.0F},
                        {"SplitTimeBoundary2", -1, 75.0F}};
    return route;
}

nfsu::RaceProgressEvent progress(nfsu::RaceEventKind kind, int index, int lap, float percent) {
    nfsu::RaceProgressEvent event;
    event.kind = kind;
    event.index = index;
    event.lap = lap;
    event.percent = percent;
    return event;
}

// A crossing of boundary `index` forward, landing exactly on it (no
// backdate), and the lap_complete nfsu::step_race_progress emits after a
// finish-line crossing that raises the highest lap.
Events forward(int index, int lap, float percent) {
    return {progress(nfsu::RaceEventKind::crossed_forward, index, lap, percent)};
}

Events new_lap(int lap, float percent) {
    return {progress(nfsu::RaceEventKind::crossed_forward, 0, lap, percent),
            progress(nfsu::RaceEventKind::lap_complete, 0, lap, percent)};
}

struct Race {
    nfsu::RaceRoute route;
    nfsu::RaceTimingConfig config;
    nfsu::RaceTimingState state;
    Timing timing;
    std::string error;

    explicit Race(const nfsu::RaceRoute& the_route, std::size_t racers = 1) : route(the_route) {
        const bool made = nfsu::make_race_timing_config(60, racers, config, error);
        check(made, "the configuration is made: " + error);
        const bool started = nfsu::start_race_timing(route, config, state, error);
        check(started, "the race starts: " + error);
    }

    void countdown() {
        const bool set = nfsu::set_race_stage(state, nfsu::RaceStage::countdown, timing);
        check(set, "the countdown begins");
    }

    // One step with these events for racer 0 at this speed.
    bool step(const Events& events = {}, float speed = 0.0F) {
        std::vector<nfsu::RaceTimingInput> inputs(config.racers);
        inputs[0].events = events.data();
        inputs[0].event_count = events.size();
        inputs[0].speed = speed;
        return nfsu::step_race_timing(route, config, state, inputs.data(), timing, error);
    }

    // Steps with no events until the next step to process is `target`.
    void run_to(std::size_t target) {
        while (state.step < target) {
            const bool stepped = step();
            check(stepped, "a quiet step runs: " + error);
            if (!stepped) {
                return;
            }
        }
    }

    // Steps to `target` and processes it with these events.
    void at(std::size_t target, const Events& events, float speed = 0.0F) {
        run_to(target);
        const bool stepped = step(events, speed);
        check(stepped, "step " + std::to_string(target) + " runs: " + error);
    }

    const nfsu::RaceRacerTiming& racer(std::size_t index = 0) const {
        return state.racers[index];
    }

    std::size_t count(nfsu::RaceTimingEventKind kind) const {
        std::size_t total = 0;
        for (const nfsu::RaceTimingEvent& event : timing) {
            total += event.kind == kind ? 1U : 0U;
        }
        return total;
    }

    const nfsu::RaceTimingEvent* last(nfsu::RaceTimingEventKind kind) const {
        const nfsu::RaceTimingEvent* found = nullptr;
        for (const nfsu::RaceTimingEvent& event : timing) {
            if (event.kind == kind) {
                found = &event;
            }
        }
        return found;
    }
};

std::int32_t world(std::size_t step) {
    return 4000 + 67 * static_cast<std::int32_t>(step);
}

// ---------------------------------------------------------------------
// The clock
// ---------------------------------------------------------------------

void test_ticks_per_frame() {
    check(nfsu::kRaceTicksPerSecond == 4000, "4000 ticks a second");
    check(nfsu::kRaceWorldStartTicks == 4000, "the world timer starts at 0xFA0");
    check(nfsu::race_ticks_per_frame(60) == 67, "a 60 Hz frame adds 67 ticks");
    check(nfsu::race_ticks_per_frame(50) == 80, "a 50 Hz frame adds 80 ticks");
    check(nfsu::race_ticks_per_frame(75) == 53, "a 75 Hz frame adds 53 ticks");
    check(nfsu::race_ticks_per_frame(100) == 40, "a 100 Hz frame adds 40 ticks");
    check(nfsu::race_ticks_per_frame(120) == 33, "a 120 Hz frame adds 33 ticks");
    check(nfsu::race_ticks_per_frame(144) == 28, "a 144 Hz frame adds 28 ticks");
    check(nfsu::race_ticks_per_frame(1) == 4000, "a one-second frame adds 4000 ticks");
    // Rates whose frame, as a float, times 3600 lands just below a whole
    // number, so that only the + 0.5 of 0x00585CDD rounds it to it.
    check(nfsu::race_ticks_per_frame(13) == 308, "a 13 Hz frame adds 308 ticks");
    check(nfsu::race_ticks_per_frame(17) == 236, "a 17 Hz frame adds 236 ticks");
    check(nfsu::race_ticks_per_frame(30) == 133, "a 30 Hz frame adds 133 ticks");
    check(nfsu::race_ticks_per_frame(0) == 0, "no frame rate, no ticks");
    check(nfsu::race_ticks_per_frame(3601) == 0, "a frame shorter than a unit is refused");
    check(nfsu::race_ticks_per_frame(-60) == 0, "a negative rate is refused");
}

std::string quantize_digest(std::int32_t from, std::int32_t to, std::int32_t stride) {
    std::vector<unsigned char> bytes;
    for (std::int64_t t = from; t <= to; t += stride) {
        const auto value = static_cast<std::uint32_t>(
            nfsu::race_quantize_ticks(static_cast<std::int32_t>(t)));
        for (unsigned int b = 0; b < 4; ++b) {
            bytes.push_back(static_cast<unsigned char>((value >> (8U * b)) & 0xFFU));
        }
    }
    return nfsu::sha256_hex(nfsu::sha256(bytes.data(), bytes.size()));
}

void test_quantize() {
    check(nfsu::race_quantize_ticks(0) == 0, "q(0)");
    check(nfsu::race_quantize_ticks(19) == 0, "19 ticks round down");
    check(nfsu::race_quantize_ticks(20) == 40, "20 ticks, half a hundredth, round up");
    check(nfsu::race_quantize_ticks(59) == 40, "59 ticks round down to 40");
    check(nfsu::race_quantize_ticks(60) == 80, "60 ticks round up to 80");
    check(nfsu::race_quantize_ticks(14854) == 14840, "the go of event 1 rounds to 14840");
    check(nfsu::race_quantize_ticks(24100) == 24120, "6.025 s rounds up to 6.03 s");
    check(nfsu::race_quantize_ticks(442549) == 442560, "110.637 s rounds to 110.64 s");
    // The float artifact: 1619 hundredths come back as 64761 ticks, one
    // more than 40 * 1619, because 1619 * 0.01 * 4000 lands above 64760.
    check(nfsu::race_quantize_ticks(64740) == 64761, "1619 hundredths are 64761 ticks");
    check(nfsu::race_quantize_ticks(64760) == 64761, "64760 is 64761 too");
    check(nfsu::race_quantize_ticks(-20) == -39, "a negative half rounds as the float does");
    check(nfsu::race_quantize_ticks(-60) == -79, "-60 ticks give -79");
    check(nfsu::race_quantize_ticks(1 << 29) == (1 << 29), "the limit is its own hundredth");
    // The digests of a numpy float32 transcription of 0x00586690 and its
    // callers written outside the repository (docs/RACE_TIMING.md), over
    // the first minute, the eight thousand ticks around zero, and 1025
    // times spread over the whole range this model steps through.
    check(quantize_digest(0, 239999, 1) ==
              "41AD16D14A1A83B6CEDB36A5365136B30438341345156D508A5FC24ED5E370D1",
          "the first minute, tick by tick, matches the transcription");
    check(quantize_digest(-4000, 3999, 1) ==
              "7CEC9F7A2E1F218ED696B03F0BD43962110AEE6BEB74022CDF5319DD8A4A27D8",
          "the ticks around zero match the transcription");
    check(quantize_digest(-(1 << 29), 1 << 29, 1048573) ==
              "ED757A8F307BEC4E9923FD03F3DA7132A95A8F9D517A510611EC259F1F855E90",
          "the whole range, sampled, matches the transcription");
}

void test_format() {
    check(nfsu::format_race_ticks(0) == "0:00.00", "zero");
    check(nfsu::format_race_ticks(39) == "0:00.00", "39 ticks truncate");
    check(nfsu::format_race_ticks(40) == "0:00.01", "a hundredth");
    check(nfsu::format_race_ticks(4000) == "0:01.00", "a second");
    check(nfsu::format_race_ticks(240000) == "1:00.00", "a minute");
    check(nfsu::format_race_ticks(442560) == "1:50.64", "a lap");
    check(nfsu::format_race_ticks(64761) == "0:16.19", "the +1 artifact shows as its hundredth");
    check(nfsu::format_race_ticks(14400000) == "60:00.00", "an hour is 60 minutes");
    check(nfsu::format_race_ticks(-40) == "-0:00.01", "a negative time");
    check(nfsu::format_race_ticks(nfsu::kRaceNoTime) == "--:--.--", "no time");
    check(nfsu::format_race_ticks(-2147483647 - 1) == "-8947:50.91", "the most negative");
}

// ---------------------------------------------------------------------
// Stages and the countdown
// ---------------------------------------------------------------------

void test_stages() {
    Race race(hand_route(2));
    check(race.state.stage == nfsu::RaceStage::none, "a race is built at stage none");
    check(race.state.now_ticks == 4000 && race.state.step == 0, "at the world start");
    check(nfsu::race_car_held(race.state), "a car is held at stage none");
    check(nfsu::set_race_stage(race.state, nfsu::RaceStage::flyby, race.timing), "to flyby");
    check(!nfsu::set_race_stage(race.state, nfsu::RaceStage::flyby, race.timing),
          "not to flyby twice");
    check(!nfsu::set_race_stage(race.state, nfsu::RaceStage::none, race.timing),
          "not back to none");
    check(nfsu::set_race_stage(race.state, nfsu::RaceStage::prerace, race.timing), "to prerace");
    check(!nfsu::set_race_stage(race.state, nfsu::RaceStage::racing, race.timing),
          "racing only through the countdown");
    race.run_to(10);
    check(race.state.stage == nfsu::RaceStage::prerace && race.state.now_ticks == world(10),
          "steps run the world timer before the countdown");
    check(nfsu::race_countdown_value(race.config, race.state) == 0, "no countdown yet");
    check(nfsu::set_race_stage(race.state, nfsu::RaceStage::countdown, race.timing),
          "to countdown");
    check(race.state.countdown_valid && race.state.countdown_start_ticks == world(10),
          "the countdown starts at the next step's world time");
    check(!nfsu::set_race_stage(race.state, nfsu::RaceStage::countdown, race.timing),
          "not to countdown twice");
    check(!nfsu::set_race_stage(race.state, nfsu::RaceStage::prerace, race.timing),
          "not back to prerace");
    check(race.count(nfsu::RaceTimingEventKind::stage) == 3, "three stage events");
    // The countdown runs 162 steps of 67 ticks to 10854 >= 10800.
    race.run_to(10 + 163);
    check(race.state.stage == nfsu::RaceStage::racing, "racing after the countdown");
    check(race.state.go_step == 10 + 162, "go on the 163rd step of the countdown");
    check(!nfsu::set_race_stage(race.state, nfsu::RaceStage::countdown, race.timing),
          "no stage change once racing");

    Race skip(hand_route(2));
    check(nfsu::set_race_stage(skip.state, nfsu::RaceStage::countdown, skip.timing),
          "none straight to countdown");
}

// Steps one tick at a time, so that every countdown edge is met exactly.
void test_countdown_edges() {
    for (const float step_seconds : {0.9F, 1.0F}) {
        const std::string label = step_seconds == 0.9F ? "0.9 s: " : "1.0 s: ";
        nfsu::RaceRoute route = hand_route(2);
        nfsu::RaceTimingConfig config;
        config.ticks_per_step = 1;
        config.countdown_step_seconds = step_seconds;
        nfsu::RaceTimingState state;
        std::string error;
        check(nfsu::start_race_timing(route, config, state, error), label + "starts: " + error);
        Timing timing;
        nfsu::set_race_stage(state, nfsu::RaceStage::countdown, timing);
        const std::int32_t edge = step_seconds == 0.9F ? 3600 : 4000;
        std::vector<int> values;
        std::vector<std::size_t> steps;
        std::size_t held_until = 0;
        nfsu::RaceTimingInput input;
        for (std::size_t s = 0; s < static_cast<std::size_t>(5 * edge); ++s) {
            if (nfsu::race_car_held(state)) {
                held_until = s;
            }
            const std::size_t before = timing.size();
            check(nfsu::step_race_timing(route, config, state, &input, timing, error),
                  label + "a tick step runs");
            for (std::size_t e = before; e < timing.size(); ++e) {
                if (timing[e].kind == nfsu::RaceTimingEventKind::countdown) {
                    values.push_back(timing[e].value);
                    steps.push_back(timing[e].step);
                }
            }
        }
        const auto uedge = static_cast<std::size_t>(edge);
        check(values == std::vector<int>({3, 2, 1, 4, 0}),
              label + "the countdown shows 3, 2, 1, go, then nothing");
        // Go shows while it is at most T old: at 0.9 s, 3600 ticks are
        // 0.90000004 s and already more; at 1.0 s, 4000 ticks are exactly
        // 1.0 s in float and still show it.
        const std::size_t off = step_seconds == 0.9F ? 4 * uedge : 4 * uedge + 1;
        check(steps.size() == 5 && steps[0] == 0 && steps[1] == uedge &&
                  steps[2] == 2 * uedge && steps[3] == 3 * uedge && steps[4] == off,
              label + "each change on the exact tick: 0, T, 2T, 3T and go + T");
        check(state.go_step == 3 * uedge && state.go_ticks == 4000 + 3 * edge,
              label + "go on the tick that reaches 3T");
        check(held_until == 3 * uedge, label + "the car is held through the go step");
    }
    // At 67 ticks a step: 2 at 54, 1 at 108, go at 162, off at 216.
    Race race(hand_route(2));
    race.countdown();
    race.run_to(300);
    std::vector<std::size_t> steps;
    for (const nfsu::RaceTimingEvent& event : race.timing) {
        if (event.kind == nfsu::RaceTimingEventKind::countdown) {
            steps.push_back(event.step);
        }
    }
    check(steps == std::vector<std::size_t>({0, 54, 108, 162, 216}),
          "at 67 ticks a step the countdown changes at 0, 54, 108, 162 and 216");
    check(race.state.race_start_valid && race.state.race_start_ticks == 14840,
          "the race starts at 14854 rounded, 14840");
    const nfsu::RaceTimingEvent* go = race.last(nfsu::RaceTimingEventKind::go);
    check(go != nullptr && go->step == 162 && go->now == 14854 && go->ticks == 14840,
          "the go event");
    check(race.state.race_ticks == nfsu::race_quantize_ticks(world(299) - 14840),
          "the race time is the rounded time since the start");
}

// Progress events that arrive during the countdown - a car that moved
// while it was held - time nothing, and go still comes on time.
void test_input_during_countdown() {
    Race race(hand_route(2));
    race.countdown();
    check(nfsu::race_car_held(race.state), "the car is held in the countdown");
    race.at(100, forward(0, 0, 25.5F), 30.0F);
    const nfsu::RaceTimingEvent* early = race.last(nfsu::RaceTimingEventKind::untimed);
    const std::int32_t early_crossing = nfsu::race_quantize_ticks(world(100) - 667);
    check(early != nullptr && early->value == 2 && early->crossing == early_crossing,
          "a crossing before go has no lap start");
    check(race.racer().crossing_valid[0] && race.racer().crossing_ticks[0] == early_crossing &&
              race.racer().highest_checkpoint == 0,
          "it is recorded as retail records it (0x00433A6D), in the lap start slot");
    check(race.racer().lap_ticks[1] == 0 && race.count(nfsu::RaceTimingEventKind::split) == 0,
          "and times nothing");
    // Retail then times a split from that early crossing: 0x004339EF reads
    // the slot the crossing wrote. A held car never gets here.
    race.at(130, forward(1, 0, 50.1F));
    const nfsu::RaceTimingEvent* early_split = race.last(nfsu::RaceTimingEventKind::split);
    check(early_split != nullptr &&
              early_split->ticks == nfsu::race_quantize_ticks(world(130)) - early_crossing,
          "a split after an early line crossing is timed from it");
    check(nfsu::race_car_held(race.state) && race.state.stage == nfsu::RaceStage::countdown,
          "still held, still counting down");
    race.run_to(163);
    check(race.state.go_step == 162, "go still at step 162");
    check(race.racer().highest_checkpoint == 0 && race.racer().crossing_ticks[0] == 14840,
          "go resets the highest checkpoint and the lap start");
    check(!nfsu::race_car_held(race.state), "released after go");
    race.at(300, forward(1, 0, 50.0F));
    const nfsu::RaceTimingEvent* split = race.last(nfsu::RaceTimingEventKind::split);
    check(split != nullptr && split->step == 300 && split->ticks == 24120 - 14840,
          "split 1 after go is timed from go");
}

// ---------------------------------------------------------------------
// Times
// ---------------------------------------------------------------------

// Two laps with every crossing exactly on its boundary, so that no backdate
// enters: every time is a rounded world time less the race start.
void run_two_laps(Race& race, std::size_t offset = 0) {
    race.at(offset + 170, forward(0, 0, 25.0F));
    race.at(offset + 300, forward(1, 0, 50.0F));
    race.at(offset + 400, forward(2, 0, 75.0F));
    race.at(offset + 500, new_lap(1, 25.0F));
    race.at(offset + 800, forward(1, 1, 50.0F));
    race.at(offset + 900, forward(2, 1, 75.0F));
    race.at(offset + 1000, new_lap(2, 25.0F));
}

void test_two_laps() {
    Race race(hand_route(2));
    race.countdown();
    run_two_laps(race);
    const nfsu::RaceRacerTiming& racer = race.racer();
    const nfsu::RaceTimingEvent* first = nullptr;
    for (const nfsu::RaceTimingEvent& event : race.timing) {
        if (event.kind == nfsu::RaceTimingEventKind::untimed && first == nullptr) {
            first = &event;
        }
    }
    check(first != nullptr && first->step == 170 && first->value == 1,
          "the first crossing of the line raises no checkpoint and is not timed");
    // Lap 1 runs from go, not from that crossing.
    check(racer.lap_ticks[1] == 37520 - 14840 && racer.lap_ticks[1] == 22680,
          "lap 1 is 0:05.67 from go");
    check(racer.lap_ticks[2] == 71000 - 37520 && racer.lap_ticks[2] == 33480,
          "lap 2 is 0:08.37 from the first lap's crossing");
    check(racer.lap_total_ticks[1] == 22680 && racer.lap_total_ticks[2] == 56160,
          "the totals at each lap's end");
    check(racer.lap_ticks[0] == 0 && racer.lap_ticks[3] == 0, "slots 0 and 3 are empty");
    check(racer.finish_valid && racer.finish_ticks == 56160 &&
              racer.finish_ticks == racer.lap_ticks[1] + racer.lap_ticks[2],
          "the race time is the sum of the laps");
    check(racer.split_ticks[1] == 57600 - 37520 && racer.previous_split_ticks[1] == 24120 - 14840,
          "split 1 on lap 2, and the one before");
    check(racer.best_split_ticks[1] == 9280 && racer.previous_best_split_ticks[1] == 9280,
          "the best split 1 is lap 1's");
    check(racer.best_split_ticks[0] == 22680 && race.state.fastest_lap_ticks == 22680,
          "the best lap and the fastest lap are lap 1");
    check(racer.status == nfsu::RacerStatus::normal_completion && racer.position == 1 &&
              racer.finish_step == 1000,
          "finished first at step 1000");
    check(race.state.outcome == nfsu::RaceOutcome::finished, "the race is finished");
    check(racer.crossing_ticks[0] == 71000, "the last crossing is the lap start slot");
    const nfsu::RaceTimingEvent* finished = race.last(nfsu::RaceTimingEventKind::finished);
    check(finished != nullptr && finished->value == 1 && finished->ticks == 56160 &&
              finished->step == 1000,
          "the finished event");
    check(race.count(nfsu::RaceTimingEventKind::lap) == 2 &&
              race.count(nfsu::RaceTimingEventKind::split) == 4,
          "two laps and four splits");
    // After the finish the clock runs on and the racer is not updated.
    race.at(1100, new_lap(3, 25.0F));
    check(racer.lap_ticks[3] == 0 && racer.highest_lap == 2, "a finished racer is not updated");
    check(race.state.race_ticks == nfsu::race_quantize_ticks(world(1100) - 14840),
          "the race time runs on");
    const std::vector<nfsu::RaceResultRow> rows =
        nfsu::race_timing_results(race.route, race.state);
    check(rows.size() == 1 && rows[0].position == 1 && rows[0].total_valid &&
              rows[0].total_ticks == 56160 && rows[0].best_lap_ticks == 22680 &&
              rows[0].laps == 2,
          "the result row");
}

// A crossing that lands past the line is dated back by the distance past
// it over the speed (0x00433650).
void test_backdate() {
    Race race(hand_route(2));
    race.countdown();
    race.at(170, forward(0, 0, 25.0F));
    // 0.5 percent of 1000 is 5 units; at 30 a second, 0.1667 s, 667 ticks.
    race.at(500, new_lap(1, 25.5F), 30.0F);
    const nfsu::RaceTimingEvent* lap = race.last(nfsu::RaceTimingEventKind::lap);
    check(lap != nullptr && lap->value == 667, "the backdate is 667 ticks");
    check(lap != nullptr && lap->crossing == nfsu::race_quantize_ticks(world(500) - 667) &&
              lap->crossing == 36840,
          "the crossing is the rounded time less the backdate");
    check(race.racer().lap_ticks[1] == 36840 - 14840, "the lap time uses the crossing");
    // No backdate at a speed of 1 or less (0x00433761..0x00433772), and one
    // just above 1 divides.
    Race slow(hand_route(2));
    slow.countdown();
    slow.at(500, forward(0, 0, 25.5F), 1.0F);
    const nfsu::RaceTimingEvent* at_one = slow.last(nfsu::RaceTimingEventKind::untimed);
    check(at_one != nullptr && at_one->value == 1, "a first crossing is untimed at any speed");
    Race crawl(hand_route(2));
    crawl.countdown();
    crawl.at(200, forward(0, 0, 25.0F));
    crawl.at(1000, new_lap(1, 25.5F), 1.0F);
    check(crawl.last(nfsu::RaceTimingEventKind::lap) != nullptr &&
              crawl.last(nfsu::RaceTimingEventKind::lap)->value == 0,
          "at a speed of 1 the crossing is not dated back");
    Race creep(hand_route(2));
    creep.countdown();
    creep.at(200, forward(0, 0, 25.0F));
    creep.at(1000, new_lap(1, 25.5F), 1.0000001F);
    check(creep.last(nfsu::RaceTimingEventKind::lap) != nullptr &&
              creep.last(nfsu::RaceTimingEventKind::lap)->value == 20000,
          "just above 1 it is: 5 units over 1.0000001, 20000 ticks");
    // Event 1's first crossing, as the retail replay makes it: 0.011348724
    // percent past 39.34353 on a path 3318.2698 long at 30 a second.
    nfsu::RaceRoute retail = hand_route(2);
    retail.spline.path_length = 3318.2698F;
    retail.boundaries[0].percent = 39.34353F;
    retail.boundaries[1].percent = 83.33021F;
    retail.boundaries[2].percent = 12.659794F;
    Race event1(retail);
    event1.countdown();
    event1.at(300, forward(1, 0, 83.4F));
    event1.at(400, forward(2, 0, 12.7F));
    event1.at(500, new_lap(1, 39.354877F), 30.0F);
    check(event1.last(nfsu::RaceTimingEventKind::lap) != nullptr &&
              event1.last(nfsu::RaceTimingEventKind::lap)->value == 50,
          "event 1's line crossed 0.3766 units back at 30 a second: 50 ticks");
    // A line near the path's seam: 99.9 percent, crossed to 0.1, is 0.2
    // past it once the difference is wrapped (0x00433675..0x00433697), 2
    // units, 267 ticks at 30 a second.
    nfsu::RaceRoute seam = hand_route(2);
    seam.boundaries[0].percent = 99.9F;
    Race wrap(seam);
    wrap.countdown();
    wrap.at(200, forward(0, 0, 99.9F));
    wrap.at(1000, new_lap(1, 0.1F), 30.0F);
    check(wrap.last(nfsu::RaceTimingEventKind::lap) != nullptr &&
              wrap.last(nfsu::RaceTimingEventKind::lap)->value == 267,
          "a crossing over the seam is dated back by the wrapped difference");
    // An absurd distance saturates rather than overflowing.
    nfsu::RaceRoute huge = hand_route(2);
    huge.spline.path_length = 1.0e30F;
    Race far(huge);
    far.countdown();
    far.at(200, forward(0, 0, 25.0F));
    far.at(1000, new_lap(1, 25.5F), 2.0F);
    check(far.last(nfsu::RaceTimingEventKind::lap) != nullptr &&
              far.last(nfsu::RaceTimingEventKind::lap)->value == nfsu::kRaceTicksLimit,
          "a backdate beyond the limit saturates at 2^29");
}

// Backing over the line takes a lap back; crossing it again gives it back
// without a new checkpoint, so nothing is timed twice.
void test_backward_line() {
    Race race(hand_route(2));
    race.countdown();
    race.at(170, forward(0, 0, 25.0F));
    race.at(200, {progress(nfsu::RaceEventKind::crossed_backward, 0, -1, 24.9F)});
    check(race.racer().lap == -1, "the lap counter follows the route back to -1");
    race.at(230, forward(0, 0, 25.0F));
    race.at(500, new_lap(1, 25.0F));
    const std::int32_t lap1 = race.racer().lap_ticks[1];
    race.at(520, {progress(nfsu::RaceEventKind::crossed_backward, 0, 0, 24.9F)});
    race.at(540, forward(0, 1, 25.0F));
    check(race.racer().lap_ticks[1] == lap1 && race.racer().highest_checkpoint == 5,
          "re-crossing the line forward times nothing");
    const nfsu::RaceTimingEvent* again = race.last(nfsu::RaceTimingEventKind::untimed);
    check(again != nullptr && again->step == 540 && again->value == 1,
          "the re-crossing is untimed");
    check(race.count(nfsu::RaceTimingEventKind::lap) == 1, "one lap");
}

// A split passed beyond the window is no crossing: no split time, and the
// lap still counts at the line.
void test_missed_split() {
    Race race(hand_route(2));
    race.countdown();
    race.at(170, forward(0, 0, 25.0F));
    race.at(300, {progress(nfsu::RaceEventKind::missed_forward, 1, 0, 80.0F)});
    race.at(400, forward(2, 0, 80.0F));
    race.at(500, new_lap(1, 25.0F));
    check(race.racer().split_ticks[1] == 0 &&
              race.racer().best_split_ticks[1] == nfsu::kRaceNoTime,
          "the missed split has no time");
    check(race.racer().lap_ticks[1] == 22680, "the lap still counts");
}

// ---------------------------------------------------------------------
// Restart, quit and the conditions that end nothing
// ---------------------------------------------------------------------

bool same_racer(const nfsu::RaceRacerTiming& a, const nfsu::RaceRacerTiming& b) {
    bool same = a.status == b.status && a.lap == b.lap && a.highest_lap == b.highest_lap &&
                a.checkpoint == b.checkpoint && a.highest_checkpoint == b.highest_checkpoint &&
                a.position == b.position && a.finish_valid == b.finish_valid &&
                a.finish_ticks == b.finish_ticks && a.finish_step == b.finish_step;
    for (std::size_t i = 0; i < nfsu::kRaceBoundaryLimit; ++i) {
        same = same && a.crossing_valid[i] == b.crossing_valid[i] &&
               a.crossing_ticks[i] == b.crossing_ticks[i] && a.split_ticks[i] == b.split_ticks[i] &&
               a.best_split_ticks[i] == b.best_split_ticks[i];
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(nfsu::kRaceLapSlots); ++i) {
        same = same && a.lap_ticks[i] == b.lap_ticks[i] &&
               a.lap_total_ticks[i] == b.lap_total_ticks[i];
    }
    return same;
}

void test_restart() {
    Race fresh(hand_route(2));
    fresh.countdown();
    run_two_laps(fresh);

    Race race(hand_route(2));
    race.countdown();
    race.at(170, forward(0, 0, 25.0F));
    race.at(300, forward(1, 0, 50.0F));
    race.at(500, new_lap(1, 25.0F));
    race.run_to(640);
    check(nfsu::restart_race_timing(race.route, race.config, race.state, race.timing, race.error),
          "a restart mid-race: " + race.error);
    const nfsu::RaceTimingEvent* restarted = race.last(nfsu::RaceTimingEventKind::restarted);
    check(restarted != nullptr && restarted->step == 640 && restarted->value == 1,
          "the restart event names the step it came on");
    check(race.state.stage == nfsu::RaceStage::none && race.state.step == 0 &&
              race.state.now_ticks == 4000 && race.state.restarts == 1 &&
              !race.state.race_start_valid && race.state.race_ticks == 0,
          "the race is built again, the world timer at 0xFA0");
    check(race.racer().lap == -1 && race.racer().lap_ticks[1] == 0 &&
              !race.racer().crossing_valid[0] && race.racer().highest_checkpoint == -1,
          "the lap record is fresh");
    race.countdown();
    run_two_laps(race);
    check(same_racer(race.racer(), fresh.racer()),
          "a restarted race times exactly as a fresh one");
    check(race.state.race_start_ticks == fresh.state.race_start_ticks &&
              race.state.fastest_lap_ticks == fresh.state.fastest_lap_ticks &&
              race.state.outcome == nfsu::RaceOutcome::finished,
          "and ends the same");
    // A finished race can be restarted too.
    check(nfsu::restart_race_timing(race.route, race.config, race.state, race.timing, race.error),
          "a restart after the finish");
    check(race.state.outcome == nfsu::RaceOutcome::running && race.state.restarts == 2,
          "running again");
}

void test_quit() {
    Race race(hand_route(2));
    race.countdown();
    race.at(170, forward(0, 0, 25.0F));
    race.run_to(400);
    check(nfsu::quit_race_timing(race.state, race.timing), "quit");
    check(race.state.outcome == nfsu::RaceOutcome::quit &&
              race.count(nfsu::RaceTimingEventKind::quit) == 1,
          "the race was left");
    check(!race.step(), "no step after a quit");
    check(race.error.find("quit") != std::string::npos, "the refusal says so: " + race.error);
    check(!nfsu::quit_race_timing(race.state, race.timing), "no second quit");
    check(!nfsu::restart_race_timing(race.route, race.config, race.state, race.timing,
                                     race.error),
          "no restart after a quit");
    check(!nfsu::set_race_stage(race.state, nfsu::RaceStage::countdown, race.timing),
          "no stage change after a quit");
    const std::vector<nfsu::RaceResultRow> rows =
        nfsu::race_timing_results(race.route, race.state);
    check(rows.size() == 1 && rows[0].status == nfsu::RacerStatus::not_finished &&
              rows[0].position == 0 && !rows[0].total_valid,
          "no result");
}

// The wrong way and a reset end nothing in a single-player race: the flag
// is kept with its duration, and the clock runs on.
void test_wrong_way_and_reset() {
    Race race(hand_route(2));
    race.countdown();
    race.at(170, forward(0, 0, 25.0F));
    race.at(300, {progress(nfsu::RaceEventKind::wrong_way_on, -1, 0, 30.0F)});
    check(race.racer().wrong_way && race.racer().wrong_way_since == world(300), "wrong way on");
    race.at(360, {progress(nfsu::RaceEventKind::wrong_way_off, -1, 0, 30.0F)});
    const nfsu::RaceTimingEvent* off = race.last(nfsu::RaceTimingEventKind::wrong_way_off);
    check(off != nullptr && off->ticks == 60 * 67, "wrong way for 60 steps, 4020 ticks");
    check(race.racer().wrong_way_steps == 60, "counted in steps");
    race.at(400, {progress(nfsu::RaceEventKind::reset_zone, 13, 0, 94.3F)});
    race.at(401, {progress(nfsu::RaceEventKind::off_path, 7, 0, 94.3F)});
    check(race.racer().resets == 1 && race.count(nfsu::RaceTimingEventKind::reset_zone) == 1 &&
              race.last(nfsu::RaceTimingEventKind::reset_zone)->value == 13 &&
              race.count(nfsu::RaceTimingEventKind::off_path) == 1,
          "the reset zone and the off-path flag are reported");
    race.at(500, new_lap(1, 25.0F));
    check(race.racer().lap_ticks[1] == 22680 && race.state.outcome == nfsu::RaceOutcome::running,
          "and the lap is timed as if nothing happened");
    // A wrong way that never ends ends nothing either.
    race.at(600, {progress(nfsu::RaceEventKind::wrong_way_on, -1, 1, 20.0F)});
    race.run_to(5000);
    check(race.state.outcome == nfsu::RaceOutcome::running && race.racer().wrong_way &&
              race.racer().wrong_way_steps == 60 + 4400,
          "4400 steps the wrong way, still racing");
}

void test_clock_limit() {
    nfsu::RaceRoute route = hand_route(2);
    nfsu::RaceTimingConfig config;
    config.start_ticks = nfsu::kRaceTicksLimit - 100;
    nfsu::RaceTimingState state;
    std::string error;
    check(nfsu::start_race_timing(route, config, state, error), "a race near the limit starts");
    Timing timing;
    nfsu::RaceTimingInput input;
    check(nfsu::step_race_timing(route, config, state, &input, timing, error),
          "one step fits below the limit");
    check(!nfsu::step_race_timing(route, config, state, &input, timing, error),
          "the next would pass it");
    check(state.outcome == nfsu::RaceOutcome::clock_limit && !timing.empty() &&
              timing.back().kind == nfsu::RaceTimingEventKind::clock_limit,
          "the race ends at the clock limit");
    check(!nfsu::step_race_timing(route, config, state, &input, timing, error),
          "and steps no further");
    check(nfsu::restart_race_timing(route, config, state, timing, error),
          "a restart builds it again");
    check(state.outcome == nfsu::RaceOutcome::running && state.now_ticks == config.start_ticks,
          "running again");
}

// ---------------------------------------------------------------------
// Opponents, the HUD and refusals
// ---------------------------------------------------------------------

void test_opponents() {
    Race race(hand_route(1), 3);
    race.countdown();
    const auto step_all = [&race](std::size_t target, const Events& zero, const Events& one) {
        race.run_to(target);
        std::vector<nfsu::RaceTimingInput> inputs(3);
        inputs[0].events = zero.data();
        inputs[0].event_count = zero.size();
        inputs[1].events = one.data();
        inputs[1].event_count = one.size();
        check(nfsu::step_race_timing(race.route, race.config, race.state, inputs.data(),
                                     race.timing, race.error),
              "a step of three racers");
    };
    step_all(170, forward(0, 0, 25.0F), forward(0, 0, 25.0F));
    step_all(400, {}, new_lap(1, 25.0F));
    check(race.state.racers[1].position == 1 && race.state.outcome == nfsu::RaceOutcome::running,
          "an opponent first; the race runs on until the player finishes");
    check(race.state.fastest_lap_ticks == nfsu::kRaceNoTime,
          "an opponent's lap is not the fastest lap, which is the players'");
    step_all(500, new_lap(1, 25.0F), {});
    check(race.state.racers[0].position == 2 && race.state.outcome == nfsu::RaceOutcome::finished,
          "the player second, and the race is finished");
    check(race.state.racers[2].position == 0 &&
              race.state.racers[2].status == nfsu::RacerStatus::not_finished,
          "the third racer has not finished");
    const std::vector<nfsu::RaceResultRow> rows =
        nfsu::race_timing_results(race.route, race.state);
    check(rows.size() == 3 && rows[0].racer == 1 && rows[1].racer == 0 && rows[2].racer == 2,
          "results by place, the unfinished last");
    check(rows[0].total_ticks < rows[1].total_ticks && !rows[2].total_valid,
          "the winner's time is the lower");
}

void test_hud() {
    Race race(hand_route(2));
    nfsu::RaceHud hud;
    check(nfsu::race_timing_hud(race.route, race.config, race.state, 0, hud),
          "a HUD before anything");
    check(hud.countdown == 0 && !hud.race_time_valid && !hud.lap_time_valid &&
              !hud.best_lap_valid && hud.lap == 0 && hud.laps == 2,
          "nothing to show before the countdown");
    check(!nfsu::race_timing_hud(race.route, race.config, race.state, 1, hud),
          "no second racer");
    race.countdown();
    race.run_to(1);
    nfsu::race_timing_hud(race.route, race.config, race.state, 0, hud);
    check(hud.countdown == 3, "3 on the first step");
    race.run_to(163);
    nfsu::race_timing_hud(race.route, race.config, race.state, 0, hud);
    check(hud.countdown == 4 && hud.race_time_valid && hud.race_ticks == 0 &&
              hud.lap_time_valid && hud.lap_ticks == 14854 - 14840,
          "go: the race time 0, the lap timer from the rounded start");
    race.at(170, forward(0, 0, 25.0F));
    nfsu::race_timing_hud(race.route, race.config, race.state, 0, hud);
    check(hud.lap_ticks == world(170) - 14840 && !hud.lap_time_held,
          "the lap timer is the unrounded world time since the lap start");
    race.at(500, new_lap(1, 25.0F));
    nfsu::race_timing_hud(race.route, race.config, race.state, 0, hud);
    check(hud.lap_time_held && hud.lap_ticks == 22680 && hud.best_lap_valid &&
              hud.best_lap_ticks == 22680 && hud.lap == 1,
          "a completed lap is held on the lap timer");
    race.run_to(680);
    nfsu::race_timing_hud(race.route, race.config, race.state, 0, hud);
    check(hud.lap_time_held && hud.lap_ticks == 22680, "still held 179 steps on, 2.998 s");
    race.run_to(681);
    nfsu::race_timing_hud(race.route, race.config, race.state, 0, hud);
    check(!hud.lap_time_held && hud.lap_ticks == world(680) - 37520,
          "180 steps on, 3.015 s, the lap timer runs again");
    race.at(700, {progress(nfsu::RaceEventKind::wrong_way_on, -1, 1, 20.0F)});
    nfsu::race_timing_hud(race.route, race.config, race.state, 0, hud);
    check(hud.wrong_way && !hud.finished, "the wrong-way flag");
}

void test_refusals() {
    nfsu::RaceTimingConfig config;
    std::string error;
    check(!nfsu::make_race_timing_config(0, 1, config, error), "a rate of 0 is refused");
    check(!nfsu::make_race_timing_config(3601, 1, config, error), "a rate of 3601 is refused");
    check(nfsu::make_race_timing_config(60, 1, config, error) && config.ticks_per_step == 67 &&
              config.humans == 1,
          "60 steps a second");

    const nfsu::RaceRoute good = hand_route(2);
    const auto refused = [&](const nfsu::RaceRoute& route, const nfsu::RaceTimingConfig& given,
                             const std::string& label) {
        nfsu::RaceTimingState state;
        state.step = 99;
        std::string reason;
        check(!nfsu::start_race_timing(route, given, state, reason) && !reason.empty() &&
                  state.step == 99,
              label + " is refused and leaves the state alone");
    };
    nfsu::RaceTimingConfig bad;
    bad.racers = 0;
    refused(good, bad, "no racer");
    bad = nfsu::RaceTimingConfig{};
    bad.racers = 65;
    refused(good, bad, "65 racers");
    bad = nfsu::RaceTimingConfig{};
    bad.humans = 2;
    refused(good, bad, "more humans than racers");
    bad = nfsu::RaceTimingConfig{};
    bad.steps_per_second = 0;
    refused(good, bad, "a step rate of 0");
    bad = nfsu::RaceTimingConfig{};
    bad.ticks_per_step = 0;
    refused(good, bad, "0 ticks a step");
    bad.ticks_per_step = 4001;
    refused(good, bad, "4001 ticks a step");
    bad = nfsu::RaceTimingConfig{};
    bad.countdown_step_seconds = std::numeric_limits<float>::quiet_NaN();
    refused(good, bad, "a countdown step of NaN");
    bad.countdown_step_seconds = 0.0F;
    refused(good, bad, "a countdown step of 0");
    bad = nfsu::RaceTimingConfig{};
    bad.start_ticks = 0;
    refused(good, bad, "a world start of 0");
    bad.start_ticks = nfsu::kRaceTicksLimit + 1;
    refused(good, bad, "a world start past the limit");
    const nfsu::RaceTimingConfig fine{};
    nfsu::RaceRoute route = good;
    route.boundaries.clear();
    refused(route, fine, "a route without boundaries");
    route = good;
    route.boundaries.resize(6, {"SplitTimeBoundary3", -1, 90.0F});
    refused(route, fine, "six boundaries");
    route = good;
    route.start = 1;
    refused(route, fine, "a start line that is not boundary 0");
    route = good;
    route.finish = 3;
    refused(route, fine, "a finish line outside the boundaries");
    route = good;
    route.point_to_point = true;
    refused(route, fine, "a point-to-point race");
    route = good;
    route.spline.path_length = 0.0F;
    refused(route, fine, "a path length of 0");
    route.spline.path_length = std::numeric_limits<float>::infinity();
    refused(route, fine, "an infinite path length");
    route = good;
    route.boundaries[1].percent = std::numeric_limits<float>::quiet_NaN();
    refused(route, fine, "a boundary at NaN");

    // Malformed step inputs change nothing.
    Race race(hand_route(2));
    race.countdown();
    race.run_to(170);
    const std::int32_t now = race.state.now_ticks;
    const auto step_refused = [&](const Events& events, float speed, const std::string& label) {
        const std::size_t before = race.timing.size();
        check(!race.step(events, speed) && !race.error.empty(), label + " is refused");
        check(race.state.step == 170 && race.state.now_ticks == now &&
                  race.timing.size() == before,
              label + " changes nothing");
    };
    step_refused(forward(3, 0, 25.0F), 0.0F, "boundary 3 of three");
    step_refused(forward(-1, 0, 25.0F), 0.0F, "boundary -1");
    step_refused({progress(nfsu::RaceEventKind::missed_forward, 5, 0, 25.0F)}, 0.0F,
                 "a missed boundary 5");
    step_refused(forward(0, 128, 25.0F), 0.0F, "lap 128");
    step_refused(forward(0, -129, 25.0F), 0.0F, "lap -129");
    step_refused(forward(0, 0, std::numeric_limits<float>::quiet_NaN()), 0.0F,
                 "a percent of NaN");
    step_refused({}, std::numeric_limits<float>::quiet_NaN(), "a speed of NaN");
    step_refused({}, -1.0F, "a negative speed");
    step_refused({}, std::numeric_limits<float>::infinity(), "an infinite speed");
    step_refused({progress(nfsu::RaceEventKind::reset_zone, -1, 0, 25.0F)}, 0.0F,
                 "a reset zone without a zone");
    {
        nfsu::RaceTimingInput counted;
        counted.event_count = 1;
        check(!nfsu::step_race_timing(race.route, race.config, race.state, &counted, race.timing,
                                      race.error),
              "events counted and not given are refused");
        check(!nfsu::step_race_timing(race.route, race.config, race.state, nullptr, race.timing,
                                      race.error),
              "no input is refused");
        nfsu::RaceTimingConfig two = race.config;
        two.racers = 2;
        std::vector<nfsu::RaceTimingInput> inputs(2);
        check(!nfsu::step_race_timing(race.route, two, race.state, inputs.data(), race.timing,
                                      race.error),
              "a configuration the race was not started with is refused");
    }
    check(race.step(), "a good step still runs after the refusals");
}

// ---------------------------------------------------------------------
// The step clock, driven by the platform's fake timing service
// ---------------------------------------------------------------------

void test_step_clock() {
    nfsu::RaceStepClock clock;
    std::string error;
    check(!nfsu::make_race_step_clock(0, 4, clock, error), "a rate of 0 is refused");
    check(!nfsu::make_race_step_clock(1001, 4, clock, error), "a rate of 1001 is refused");
    check(!nfsu::make_race_step_clock(60, 0, clock, error), "a cap of 0 is refused");
    check(nfsu::make_race_step_clock(60, 4, clock, error), "60 a second, 4 at most");

    // A frame of 16667 us: one step a frame, the first handed out at once.
    nfsu::FakeTiming timing(1000000, 16667);
    std::uint64_t handed = 0;
    for (int frame = 0; frame < 600; ++frame) {
        const std::uint32_t steps = nfsu::advance_race_step_clock(clock, timing);
        check(steps == 1, "one step a 16667 us frame, frame " + std::to_string(frame));
        handed += steps;
    }
    check(handed == 600 && clock.steps == 600 && clock.dropped == 0, "600 frames, 600 steps");
    // A stall of 100 ms: six steps are due, four are handed out, two dropped.
    const std::uint64_t stalled = 1000000ULL + 599ULL * 16667ULL + 100000ULL;
    timing.push_time(stalled);
    check(nfsu::advance_race_step_clock(clock, timing) == 4 && clock.dropped == 2,
          "a 100 ms stall hands out 4 and drops 2");
    // The fake resumes stepping from the queued time.
    check(nfsu::advance_race_step_clock(clock, timing) == 1, "then one a frame again");
    // A reading earlier than the last is taken as the last.
    const std::uint64_t last = clock.last_us;
    check(nfsu::advance_race_step_clock(clock, last - 50000U) == 0 && clock.last_us == last,
          "a clock going back hands out nothing");
    // A frame of 8333 us: a step every other frame, never two.
    nfsu::RaceStepClock fast;
    nfsu::make_race_step_clock(60, 4, fast, error);
    nfsu::FakeTiming quick(0, 8333);
    std::uint64_t total = 0;
    for (int frame = 0; frame < 1200; ++frame) {
        const std::uint32_t steps = nfsu::advance_race_step_clock(fast, quick);
        check(steps <= 1, "at most one step a 120 Hz frame");
        total += steps;
    }
    // 1199 frames of 8333 us after the first read are 9991267 us: 599 steps
    // due after the first.
    check(total == 600, "600 steps in 1200 frames of 8333 us");
    // Exactly one second: step 60 is due at 1000000 us.
    nfsu::RaceStepClock exact;
    nfsu::make_race_step_clock(60, 100, exact, error);
    check(nfsu::advance_race_step_clock(exact, 5U) == 1, "step 0 at once");
    check(nfsu::advance_race_step_clock(exact, 5U + 999999U) == 59, "59 more before 1 s");
    check(nfsu::advance_race_step_clock(exact, 5U + 1000000U) == 1, "step 60 at 1 s");
    // An unmade clock hands out nothing.
    nfsu::RaceStepClock unmade;
    unmade.max_steps = 0;
    check(nfsu::advance_race_step_clock(unmade, 12345U) == 0, "a clock with no cap");
}

// ---------------------------------------------------------------------
// End to end: a synthetic circle driven through nfsu_race_route
// ---------------------------------------------------------------------

using Bytes = std::vector<unsigned char>;

void put_u16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<unsigned char>(value & 0xFFU);
    bytes[at + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

void put_u32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

void put_f32(Bytes& bytes, std::size_t at, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put_u32(bytes, at, bits);
}

void append_chunk(Bytes& image, std::uint32_t id, const Bytes& payload) {
    const std::size_t at = image.size();
    image.resize(at + 8);
    put_u32(image, at, id);
    put_u32(image, at + 4, static_cast<std::uint32_t>(payload.size()));
    image.insert(image.end(), payload.begin(), payload.end());
}

// The circle of tests/race_route_tests.cpp: 16 nodes, radius 100, counter
// clockwise, the grid at 80 degrees, the line at 90, split 1 at 180 and
// split 2 at 270. Decimal literals, so a Debug and a Release build write
// the same bytes.
constexpr float kCircle[16][2] = {
    {100.0F, 0.0F},          {92.387953F, 38.268343F},   {70.710678F, 70.710678F},
    {38.268343F, 92.387953F}, {0.0F, 100.0F},             {-38.268343F, 92.387953F},
    {-70.710678F, 70.710678F}, {-92.387953F, 38.268343F}, {-100.0F, 0.0F},
    {-92.387953F, -38.268343F}, {-70.710678F, -70.710678F}, {-38.268343F, -92.387953F},
    {0.0F, -100.0F},          {38.268343F, -92.387953F},  {70.710678F, -70.710678F},
    {92.387953F, -38.268343F},
};

Bytes circle_image(float length) {
    struct Marker {
        const char* name;
        float x;
        float y;
    };
    const Marker markers[] = {{"ForwardStartingPosition", 17.364818F, 98.480775F},
                              {"ForwardStartLine", 0.0F, 100.0F},
                              {"SplitTimeBoundary1", -100.0F, 0.0F},
                              {"SplitTimeBoundary2", 0.0F, -100.0F}};
    Bytes marker_payload(8 + 4 * nfsu::kTrackMarkerRecordSize, 0);
    for (std::size_t m = 0; m < 4; ++m) {
        const std::size_t at = 8 + m * nfsu::kTrackMarkerRecordSize;
        put_u32(marker_payload, at + 8, nfsu::track_marker_hash(markers[m].name));
        put_f32(marker_payload, at + 16, markers[m].x);
        put_f32(marker_payload, at + 20, markers[m].y);
    }
    Bytes record(nfsu::kTrackPathRecordSize, 0);
    record[1] = 1;
    put_u16(record, 4, 16);
    put_u16(record, 6, 16);
    put_f32(record, 12, length);
    put_f32(record, 16, 1.0F);
    Bytes nodes(16 * nfsu::kTrackPathNodeSize, 0);
    for (std::size_t n = 0; n < 16; ++n) {
        const std::size_t at = n * nfsu::kTrackPathNodeSize;
        put_f32(nodes, at, kCircle[n][0]);
        put_f32(nodes, at + 4, kCircle[n][1]);
        put_u16(nodes, at + 0x0A, 0xFFFFU);
        put_u16(nodes, at + 0x0C, 0xFFFFU);
        put_u16(nodes, at + 0x0E, 0xFFFFU);
        put_f32(nodes, at + 0x10, 39.269908F * static_cast<float>(n));
    }
    Bytes image;
    append_chunk(image, nfsu::kTrackMarkerChunkId, marker_payload);
    Bytes container;
    append_chunk(container, nfsu::kTrackPathRecordChunkId, record);
    append_chunk(container, nfsu::kTrackPathNodeChunkId, nodes);
    append_chunk(container, nfsu::kTrackPathZoneChunkId, Bytes());
    append_chunk(image, nfsu::kTrackPathContainerId, container);
    return image;
}

struct Drive {
    nfsu::RaceTimingState state;
    Timing timing;
    std::size_t positions = 0;
    bool built = false;
};

// The replay race-timing runs: held at the drive's first position until
// the step after go, one position a step after that.
Drive drive_circle(bool backward, int laps) {
    Drive result;
    const Bytes probe = circle_image(628.31853F);
    nfsu::RaceRouteTrack track = nfsu::parse_race_route_track(probe.data(), probe.size());
    nfsu::TrackPathSpline spline;
    std::string error;
    if (!track.ok() || !nfsu::build_track_path_spline(track, false, spline, error)) {
        check(false, "the circle parses and builds: " + error);
        return result;
    }
    const Bytes image = circle_image(spline.spline_length);
    track = nfsu::parse_race_route_track(image.data(), image.size());
    nfsu::RaceRoute route;
    nfsu::RaceDrive drive;
    nfsu::RaceTimingConfig config;
    if (!nfsu::build_race_route(track, laps, 0, 0, route, error) ||
        !nfsu::make_race_drive(route, track, backward, -1, -1, 0.5F, drive, error) ||
        !nfsu::make_race_timing_config(60, 1, config, error) ||
        !nfsu::start_race_timing(route, config, result.state, error)) {
        check(false, "the circle race is set up: " + error);
        return result;
    }
    result.built = true;
    result.positions = drive.points.size() / 2;
    nfsu::set_race_stage(result.state, nfsu::RaceStage::countdown, result.timing);
    nfsu::RaceProgress progress;
    nfsu::start_race_progress(route, track, drive.points.data(), progress);
    std::size_t next = 1;
    float at[2] = {drive.points[0], drive.points[1]};
    Events moved;
    while (result.state.outcome == nfsu::RaceOutcome::running) {
        float speed = 0.0F;
        if (!nfsu::race_car_held(result.state)) {
            if (next >= result.positions) {
                break;
            }
            const float dx = drive.points[next * 2] - at[0];
            const float dy = drive.points[next * 2 + 1] - at[1];
            speed = std::sqrt(dy * dy + dx * dx) / (1.0F / 60.0F);
            at[0] = drive.points[next * 2];
            at[1] = drive.points[next * 2 + 1];
            ++next;
        }
        moved.clear();
        if (!progress.finished) {
            nfsu::step_race_progress(route, track, progress, at, 1.0F / 60.0F, moved);
        }
        nfsu::RaceTimingInput input;
        input.events = moved.data();
        input.event_count = moved.size();
        input.speed = speed;
        if (!nfsu::step_race_timing(route, config, result.state, &input, result.timing,
                                    error)) {
            check(false, "a circle step runs: " + error);
            break;
        }
    }
    return result;
}

void test_circle() {
    const Drive ahead = drive_circle(false, 2);
    if (!ahead.built) {
        return;
    }
    const nfsu::RaceRacerTiming& racer = ahead.state.racers.front();
    check(ahead.state.outcome == nfsu::RaceOutcome::finished && racer.position == 1,
          "the forward drive finishes first");
    check(ahead.state.go_step == 162, "go at step 162");
    // The route replay crosses the line at its step 36, lap 1 at 1300 and
    // finishes at 2564 (tests/race_route_guards.cmake); held until go, the
    // race sees them 162 steps later.
    check(racer.finish_step == 162 + 2564, "finished at step 2726");
    const nfsu::RaceTimingEvent* first = nullptr;
    for (const nfsu::RaceTimingEvent& event : ahead.timing) {
        if (event.kind == nfsu::RaceTimingEventKind::untimed && first == nullptr) {
            first = &event;
        }
    }
    check(first != nullptr && first->step == 162 + 36 && first->value == 1,
          "the first crossing of the line, untimed, at step 198");
    check(racer.lap_ticks[1] > 0 && racer.lap_ticks[2] > 0 &&
              racer.finish_ticks == racer.lap_ticks[1] + racer.lap_ticks[2] &&
              racer.lap_total_ticks[2] == racer.finish_ticks,
          "the two laps add up to the race time");
    // 1300 - 36 + 36 steps from go to the end of lap 1, 1264 for lap 2, at
    // 67 ticks a step and backdated by less than a step.
    check(racer.lap_ticks[1] > 1300 * 67 - 67 && racer.lap_ticks[1] < 1300 * 67 + 67,
          "lap 1 is about 1300 steps");
    check(racer.lap_ticks[2] > 1264 * 67 - 67 && racer.lap_ticks[2] < 1264 * 67 + 67,
          "lap 2 is about 1264 steps");
    std::cout << "circle: lap 1 " << racer.lap_ticks[1] << " lap 2 " << racer.lap_ticks[2]
              << " total " << racer.finish_ticks << '\n';
    // Pinned from this implementation's own run (not an independent
    // value): 1300 steps of 67 ticks from go, less the rounded start's 14
    // and a backdate, and 1264 steps.
    check(racer.lap_ticks[1] == 87040 && racer.lap_ticks[2] == 84680 &&
              racer.finish_ticks == 171720,
          "the circle's times, pinned");

    const Drive back = drive_circle(true, 2);
    if (!back.built) {
        return;
    }
    const nfsu::RaceRacerTiming& reverse = back.state.racers.front();
    check(back.state.outcome == nfsu::RaceOutcome::running &&
              reverse.status == nfsu::RacerStatus::not_finished && reverse.wrong_way,
          "the backward drive never finishes and ends the wrong way");
    check(reverse.lap_ticks[1] == 0 && reverse.lap < -1, "no lap, the lap counter below -1");
    check(back.state.step == 162 + back.positions, "it ran until the positions ran out");
}

} // namespace

int main() {
    test_ticks_per_frame();
    test_quantize();
    test_format();
    test_stages();
    test_countdown_edges();
    test_input_during_countdown();
    test_two_laps();
    test_backdate();
    test_backward_line();
    test_missed_split();
    test_restart();
    test_quit();
    test_wrong_way_and_reset();
    test_clock_limit();
    test_opponents();
    test_hud();
    test_refusals();
    test_step_clock();
    test_circle();
    if (g_failures != 0) {
        std::cerr << g_failures << " race timing test(s) failed\n";
        return 1;
    }
    std::cout << "race timing tests passed\n";
    return 0;
}
