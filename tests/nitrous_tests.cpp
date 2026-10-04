#include "nfsu/nitrous.h"
#include "nfsu/nitrous_controls.h"
#include "nfsu/sha256.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "test failure: " << message << '\n'; ++failures; }
}
// Fresh stdin/stdout transport for tools/nitrous314_compare.py. Every
// command prints a state/result; malformed input is a failed comparison.
int oracle() {
    nfsu::NitrousResource state;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string op;
        input >> op;
        bool result = false;
        if (op == "load") input >> state.capacity >> state.step >> state.remaining >> state.started;
        else if (op == "initialize") {
            float seconds = 0; std::int32_t count = 0;
            input >> seconds >> count; result = state.initialize(seconds, count);
        } else if (op == "available") result = state.available();
        else if (op == "start" || op == "active" || op == "stop" || op == "snapshot") {
            std::uint32_t clock = 0; input >> clock;
            if (op == "start") { bool held = false; input >> held; result = state.start(clock, held); }
            else if (op == "active") result = state.active(clock);
            else if (op == "stop") state.stop(clock);
            else {
                const auto snapshot = nfsu::nitrous_snapshot(state, clock);
                std::cout << snapshot.capacity << ' ' << snapshot.remaining << ' '
                          << snapshot.active << ' ' << snapshot.available << '\n';
                continue;
            }
        } else if (op == "dispatch") {
            std::uint32_t clock = 0; bool pressed = false;
            nfsu::NitrousDispatchContext context;
            input >> clock >> pressed >> context.flag_78a345 >> context.override_7361a4
                  >> context.flag_78a346 >> context.override_7361a0 >> context.held_mode;
            nfsu::nitrous_dispatch(state, clock, pressed, context);
        } else if (op == "torque") {
            float base = 0, angular = 0, rate = 0; bool active = false;
            nfsu::NitrousTorqueContext context;
            input >> base >> angular >> rate >> active >> context.rpm_origin >> context.rpm_scale
                  >> context.activation_seconds >> context.clock >> context.flag_78a344 >> context.override_7361a8;
            if (!input) { std::cerr << "test failure: torque input\n"; return 1; }
            std::cout << std::setprecision(9) << nfsu::nitrous_engine_torque(base, angular, rate, active, context) << '\n';
            continue;
        } else { std::cerr << "test failure: unknown command\n"; return 1; }
        if (!input) { std::cerr << "test failure: malformed input\n"; return 1; }
        std::cout << state.capacity << ' ' << state.step << ' ' << state.remaining << ' ' << state.started << ' ' << result << '\n';
    }
    return 0;
}
void resource_tests() {
    nfsu::NitrousResource state;
    check(state.initialize(2, 2) && state.capacity == 8000 && state.step == 4000 && state.remaining == 8000,
          "fresh resource capacity and count");
    nfsu::NitrousDispatchContext context;
    check(nfsu::nitrous_dispatch(state, 10000, true, context) == nfsu::NitrousTransition::started,
          "press transition");
    const auto before = state;
    const auto snapshot = nfsu::nitrous_snapshot(state, 11000);
    check(snapshot.active && snapshot.remaining == 7000 && !snapshot.available,
          "read-only current charge");
    check(state.started == before.started && state.remaining == before.remaining, "snapshot does not commit charge");
    check(nfsu::nitrous_dispatch(state, 11000, true, context) == nfsu::NitrousTransition::none,
          "duplicate press has no repeated event");
    context.override_7361a4 = true;
    nfsu::nitrous_dispatch(state, 11000, false, context);
    check(state.started == 10000, "original predicate suppresses release too");
    context.override_7361a4 = false;
    check(nfsu::nitrous_dispatch(state, 11000, false, context) == nfsu::NitrousTransition::stopped && state.remaining == 7000,
          "release commits consumption");
    check(state.initialize(6, 6) && state.capacity == 8000 && state.remaining == 7000, "car reset preserves charge");
    state.start(12000, true);
    check(state.active(19000), "exact exhaustion inclusive");
    check(!state.active(19001) && state.remaining == 0 && state.started == 0, "past exhaustion stops");
    check(!state.start(20000, true), "empty cannot activate");
    check(state.initialize(2, 2) && state.remaining == 0, "reset does not refill depletion");
    state = {};
    check(state.initialize(2, 2) && state.remaining == 8000, "fresh retry resource initializes full");
    const auto valid = state;
    nfsu::NitrousResource invalid;
    check(!invalid.initialize(std::numeric_limits<float>::infinity(), 2) && invalid.capacity == 0,
          "invalid tuning is refused without mutation");
    state.start(30000, true);
    state.stop(30000);
    check(state.remaining == valid.remaining, "frozen world clock consumes no resource");
    state.start(31000, true);
    state.stop(32000); // Original finish consumer's resource operation.
    const auto finished = state;
    state.stop(35000);
    check(state.remaining == finished.remaining && state.started == 0, "finish stop is idempotent");
}
void controls_tests() {
    nfsu::NitrousControls controls;
    std::string error;
    const auto config = nfsu::default_drive_controls_config();
    check(controls.configure(config, true, error), "enabled controls configuration");
    nfsu::GamepadFrame frame;
    frame.pads[0].connected = true;
    frame.pads[0].buttons = nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_left);
    controls.update(frame);
    check(!controls.held(), "connect with L held stays latched");
    frame.pads[0].buttons = 0; controls.update(frame);
    frame.pads[0].buttons = nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_left);
    controls.update(frame);
    check(controls.held() && controls.pressed() && !controls.driving().frame_state().shift_down,
          "enabled L cannot also shift down");
    controls.update(frame);
    check(controls.held() && !controls.pressed() && !controls.released(), "nitrous request is held across frames");
    frame.pads[0].connected = false; controls.update(frame);
    check(!controls.held() && controls.released(), "disconnect releases request");
    check(controls.configure(config, false, error), "disabled controls configuration");
    frame.pads[0].connected = true; frame.pads[0].buttons = 0; controls.update(frame);
    frame.pads[0].buttons = nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_left); controls.update(frame);
    check(!controls.held() && controls.driving().frame_state().shift_down, "disabled diagnostic L retains shift down");
    check(controls.configure(config, true, error), "re-enable controls");
    frame = {}; frame.pads[0].connected = true; frame.pads[1].connected = true; controls.update(frame);
    frame.pads[0].buttons = nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_left); controls.update(frame);
    frame.pads[1].buttons = frame.pads[0].buttons; frame.pads[0].connected = false; controls.update(frame);
    check(controls.held(), "another connected slot keeps request held");
    frame.pads[1].buttons = 0; controls.update(frame);
    check(!controls.held(), "last slot releases request");
}
void torque_tests() {
    std::array<std::uint8_t, 36> encoded{};
    for (std::size_t i = 0; i < nfsu::kNitrousTorqueCurve.size(); ++i) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &nfsu::kNitrousTorqueCurve[i], sizeof(bits));
        for (unsigned byte = 0; byte < 4; ++byte) encoded[i * 4 + byte] = static_cast<std::uint8_t>(bits >> (8 * byte));
    }
    check(nfsu::sha256_hex(nfsu::sha256(encoded.data(), encoded.size())) == nfsu::kNitrousTorqueCurveSha256,
          "original numeric curve byte identity");
    nfsu::NitrousTorqueContext context;
    context.rpm_origin = 1000; context.rpm_scale = 0.001F;
    context.activation_seconds = 10; context.clock = 40000;
    const float angular = 5000.0F / 9.54930400848388671875F;
    check(nfsu::nitrous_engine_torque(2, angular, 0.5F, false, context) == 2, "inactive baseline torque unchanged");
    const float boosted = nfsu::nitrous_engine_torque(2, angular, 0.5F, true, context);
    check(std::abs(boosted - 2.275F) < 0.000002F, "original additive rate term");
    context.override_7361a8 = true;
    check(std::abs(nfsu::nitrous_engine_torque(2, angular, 0.5F, true, context) - boosted * 1.5F) < 0.000002F,
          "original explicit time and mode multiplier");
    context.clock = 80000;
    check(nfsu::nitrous_engine_torque(2, angular, 0.5F, true, context) == boosted, "time window ends modifier");
}
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--oracle") return oracle();
    if (argc != 1) return 1;
    resource_tests(); controls_tests(); torque_tests();
    if (failures) { std::cerr << "FAIL: " << failures << " nitrous tests\n"; return 1; }
    std::cout << "PASS: nitrous resource, controls and torque tests\n";
    return 0;
}
