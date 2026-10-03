#include "nfsu/race_hud.h"
#include <cstring>
#include <iostream>
#include <iomanip>
#include <limits>

namespace {
using namespace nfsu;
int failures = 0;
void check(bool value, const char* label) {
    if (!value) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
bool text(const RaceLapHudPanel& p, std::size_t i, const char* expected) {
    return std::strcmp(p.fields[i].text.data(), expected) == 0;
}
void test_records() {
    RaceRoute route; route.finish = 4; route.laps = 2;
    RaceTimingState state; state.racers.resize(2); state.step_ticks = 4000;
    auto& r = state.racers[1];
    RaceLapHudPanel p;
    const auto build = [&] { build_race_lap_hud_panel(race_lap_hud_snapshot(route,state,1),
                                                    RaceLapHudStyle::native5,p); };
    build();
    check(text(p,5," 1") && text(p,4,"/ 2"), "start uses highest lap and configured total");
    check(!p.current_valid && p.fields[0].localized_hash == 0x61C9B7A2 &&
          text(p,0,"") && text(p,2," 0:00.00"), "start localizes current and formats sentinel best");
    r.crossing_valid[0] = true; r.crossing_ticks[0] = 4000; build();
    check(text(p,0," 0:00.00") && !p.current_held,"GO supplied start is valid zero elapsed time");
    state.step_ticks = 64321; build();
    check(text(p,0," 0:15.08"),"running truncates hundredths");
    r.lap = 0; r.highest_lap = 1; r.crossing_ticks[0] = 244000;
    state.step_ticks = 244000; r.lap_shown_valid = true;
    r.lap_shown_since = 244000; r.lap_shown_ticks = 240000; r.best_split_ticks[4] = 240000;
    build();
    check(text(p,5," 2") && text(p,0," 1:00.00") && p.current_held &&
          text(p,2," 1:00.00"),"lap crossing consumes authoritative hold; highest differs from lap");
    state.step_ticks = 255999; build();
    check(p.current_held && text(p,0," 1:00.00"),"native holds immediately before three seconds");
    state.step_ticks = 256000; build();
    check(!p.current_held && text(p,0," 0:03.00"),"native tick constant releases at exact 12000 ticks");
    state.step_ticks = 256001; build();
    check(!p.current_held && text(p,0," 0:03.00"),"next lap clock continues under hold");
    const auto paused = race_lap_hud_snapshot(route,state,1);
    state.step_ticks += 4000; r.highest_lap = 2;
    RaceLapHudPanel repeat;
    build_race_lap_hud_panel(paused,RaceLapHudStyle::native5,repeat);
    check(repeat.fields[0].text == p.fields[0].text && repeat.fields[5].text == p.fields[5].text,
          "paused copy is immutable after source advances");
    r.status = RacerStatus::normal_completion; build();
    check(p.snapshot.finished && text(p,5," 3") && !p.presentation_ready,
          "isolated finish has no invented clamp or visibility");
    r.best_split_ticks[4] = 0; build();
    check(text(p,2,"--:--:--"),"zero best is different from sentinel");
    r.crossing_ticks[0] = kRaceNoTime; build();
    check(!p.current_valid && p.fields[0].localized_hash == 0x61C9B7A2,"sentinel current invalid");
    r.crossing_ticks[0] = 0; build();
    check(!p.current_valid,"zero current invalid despite port validity flag");
    state = RaceTimingState{}; state.restarts = 1; state.racers.resize(2); build();
    check(p.snapshot.generation == 1 && !p.current_held && text(p,5," 1"),"retry clears hold and lap");
    build_race_lap_hud_panel(race_lap_hud_snapshot(route,state,2),RaceLapHudStyle::native5,p);
    check(!p.snapshot.valid && text(p,5,"") && !p.presentation_ready,"missing actor clears old panel");
    route.finish = 5;
    check(!race_lap_hud_snapshot(route,state,0).valid,"out-of-range boundary refused");
    RaceLapHudText formatted;
    check(!format_lap_hud_ticks(4000,RaceLapHudStyle::unknown,formatted) && formatted[0] == 0,
          "unknown format does not guess profile option");
    check(!format_lap_hud_ticks(std::numeric_limits<std::int32_t>::min(),
          RaceLapHudStyle::native5,formatted),"native NEG overflow unsupported explicitly");
    check(format_lap_hud_ticks(-3599999,RaceLapHudStyle::native5,formatted) &&
          std::strcmp(formatted.data(),"-14:59.9") == 0,"native negative two-digit minute truncation");
    check(format_lap_hud_ticks(29536000,RaceLapHudStyle::native5,formatted) &&
          std::strcmp(formatted.data()," 2h:03:04") == 0,"hours use whole seconds and minute remainder");
    check(format_lap_hud_ticks(2147483646,RaceLapHudStyle::native5,formatted) &&
          std::strcmp(formatted.data(),"49h:07:50") == 0,"native hour field retains only two digits");
    check(format_lap_hud_ticks(-2147483646,RaceLapHudStyle::native5,formatted) &&
          std::strcmp(formatted.data(),"-49h:07:5") == 0,"negative long hour template preserves native truncation");
    check(format_lap_hud_ticks(1440000000,RaceLapHudStyle::native5,formatted) &&
          std::strcmp(formatted.data(),"00h:00:00") == 0,"wrapped hour padding uses original quotient");
}

void test_session() {
    VehicleTrackCourse course; std::string error;
    check(build_vehicle_proving_ground(course,error),"synthetic course builds");
    check(build_race_route(course.track,2,0,0,course.route,error),"synthetic two-lap route builds");
    RaceSession read(course), control(course); RaceRouteDriver driver;
    check(read.start(error) && control.start(error),"paired sessions start");
    bool lap = false; RaceHudFrame frame;
    while (read.timing().outcome == RaceOutcome::running && read.timing().step < 12000) {
        const auto pad = driver.sample(read);
        if (!read.advance(pad,error) || !control.advance(pad,error)) {
            check(false,"paired sessions advance"); break;
        }
        const auto record = read.record(); const auto step = read.timing().step;
        build_race_hud_frame(read,1280,720,frame);
        build_race_hud_frame(read,1280,720,frame);
        check(read.record() == record && read.record() == control.record() && read.timing().step == step,
              "duplicate rendering preserves physics, timing and replay");
        check(frame.lap.snapshot.step == step && !frame.lap.presentation_ready &&
              !frame.lap.formatting_valid,"production frame consumes current snapshot with unresolved gates closed");
        if (frame.lap.snapshot.highest_lap == 1) lap = true;
    }
    check(lap && read.timing().outcome == RaceOutcome::finished,"paired run crosses lap and finishes");
    const auto held = read.record();
    build_race_hud_frame(read,1280,720,frame);
    check(frame.lap.snapshot.finished && read.record() == held,"finish read preserves latched state");
    // A vehicle reset is a physics event, not a retry of race timing.
    RaceSession reset(course); check(reset.start(error),"reset session starts");
    while (race_car_held(reset.timing()) && reset.timing().step < 1000) {
        if (!reset.advance({},error)) { check(false,"reset session reaches GO"); break; }
    }
    check(!race_car_held(reset.timing()),"reset session leaves countdown");
    const auto start = reset.timing().racers[0].crossing_ticks[0];
    const auto generation = reset.timing().restarts;
    check(reset.advance(race_input_frame({1,gamepad_button_bit(GamepadButton::select),0},
          reset.timing().step),error),"manual car reset advances");
    build_race_hud_frame(reset,1280,720,frame);
    check(frame.lap.snapshot.start_ticks == start && frame.lap.snapshot.generation == generation,
          "car reset preserves timing start and generation");
    build_race_hud_frame_for_car(reset.car(),8000,7500,1280,720,frame);
    check(!frame.lap.snapshot.valid,"car-only frame clears a prior session panel");
}

int oracle_stream() {
    char command;
    while (std::cin >> command) {
        if (command == 'f') {
            std::int32_t ticks; int style;
            if (!(std::cin >> ticks >> style) || style < 1 || style > 7) return 2;
            RaceLapHudText value;
            if (!format_lap_hud_ticks(ticks,static_cast<RaceLapHudStyle>(style),value)) return 2;
            std::cout << value.data() << '\n';
        } else if (command == 'p') {
            RaceLapHudSnapshot s; s.valid = true; s.start_valid = true; int held, style;
            if (!(std::cin >> s.world_ticks >> s.start_ticks >> s.highest_lap >> s.laps >> s.best_ticks
                  >> held >> s.held_since >> s.held_ticks >> style)) return 2;
            s.hold_valid = held != 0;
            RaceLapHudPanel p; build_race_lap_hud_panel(s,static_cast<RaceLapHudStyle>(style),p);
            for (auto i : {5U,4U,2U,0U}) {
                if (p.fields[i].localized_hash) std::cout << '#' << p.fields[i].localized_hash;
                else std::cout << p.fields[i].text.data();
                std::cout << (i == 0 ? '\n' : '\t');
            }
        } else return 2;
    }
    return std::cin.eof() ? 0 : 2;
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1],"--oracle-stream") == 0) return oracle_stream();
    if (argc == 2 && std::strcmp(argv[1],"--bindings") == 0) {
        std::cout << std::setprecision(9);
        for (const auto& b : race_lap_hud_bindings()) {
            std::cout << b.object << '\t' << b.default_resource << '\t' << b.resource_index << '\t'
                      << b.x << '\t' << b.y << '\t' << b.depth << '\t' << b.scale << '\t'
                      << b.justification << '\t' << b.label_hash;
            for (const auto c : b.rgba) std::cout << '\t' << unsigned(c);
            std::cout << '\n';
        }
        return 0;
    }
    if (argc != 1) return 2;
    test_records(); test_session();
    std::cout << "race lap HUD: " << (failures ? "FAIL" : "PASS") << '\n';
    return failures ? 1 : 0;
}
