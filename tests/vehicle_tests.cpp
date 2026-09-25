// The vehicle model of issue #58 (nfsu_vehicle, docs/VEHICLE.md):
//
//   nfsu_vehicle_tests                             the unit tests below
//   nfsu_vehicle_tests --write-handling-fixture D  writes three synthetic
//                                                  GLOBALB.BUN files under D
//                                                  for the probe guards,
//                                                  refusing to replace any
//
// Every bound below that is not an identity is a property of the model as
// configured - the recovered 240SX handling with the default approximations
// - and docs/VEHICLE.md says what each bound stands for. Where a test
// recomputes one of the model's own expressions it does so in the same
// operation order, and compares bits: the test is built with the model's
// floating-point options for that reason.

#include "nfsu/arg_path.h"
#include "nfsu/sha256.h"
#include "nfsu/vehicle.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

std::string num(double value) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.17g", value);
    return text;
}

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

nfsu::VehicleModel stock_model() {
    return nfsu::VehicleModel(nfsu::stock_240sx_vehicle_spec());
}

// Runs a script from its own pose and returns every telemetry record.
std::vector<nfsu::VehicleTelemetry> run_script(const nfsu::VehicleModel& model,
                                               const nfsu::VehicleScript& script,
                                               std::uint64_t steps) {
    std::vector<nfsu::VehicleTelemetry> out;
    nfsu::VehiclePose pose;
    pose.speed = script.speed;
    pose.gearbox = script.gearbox;
    nfsu::VehicleState state;
    if (!model.reset(state, pose)) {
        check(false, "reset refused the script's pose");
        return out;
    }
    const nfsu::VehicleFlatGround ground(0.0);
    for (std::uint64_t i = 0; i < steps; ++i) {
        nfsu::VehicleTelemetry t;
        if (!model.step(state, script.input_at(i), ground, t)) {
            check(false, "the model stopped at step " + std::to_string(i + 1));
            return out;
        }
        out.push_back(t);
    }
    return out;
}

std::vector<nfsu::VehicleTelemetry> run_builtin(const nfsu::VehicleModel& model,
                                                const std::string& name) {
    nfsu::VehicleScript script;
    check(nfsu::builtin_vehicle_script(name, script), "built-in script " + name);
    return run_script(model, script, script.total_steps());
}

std::string telemetry_digest(const std::vector<nfsu::VehicleTelemetry>& records) {
    nfsu::VehicleTelemetryHash hash;
    for (const nfsu::VehicleTelemetry& t : records) {
        hash.add(t);
    }
    return nfsu::sha256_hex(hash.finish());
}

double speed_of(const nfsu::VehicleTelemetry& t) {
    return std::sqrt(t.velocity.x * t.velocity.x + t.velocity.y * t.velocity.y +
                     t.velocity.z * t.velocity.z);
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

void test_sqrt() {
    check(nfsu::vehicle_sqrt(0.0) == 0.0, "sqrt(0)");
    check(nfsu::vehicle_sqrt(-4.0) == 0.0, "sqrt of a negative is 0");
    check(nfsu::vehicle_sqrt(std::numeric_limits<double>::quiet_NaN()) == 0.0, "sqrt(NaN) is 0");
    check(nfsu::vehicle_sqrt(std::numeric_limits<double>::infinity()) ==
              std::numeric_limits<double>::infinity(),
          "sqrt(inf)");
    for (double x : {1.0, 4.0, 9.0, 16.0, 0.25, 1e-300, 4.9406564584124654e-324, 2.2e-308,
                     DBL_MAX, 2.0, 3.0, 10.0, 12345.678, 1e10, 7.25e-5}) {
        const double y = nfsu::vehicle_sqrt(x);
        const double reference = std::sqrt(x);
        check(std::fabs(y - reference) <= 4e-16 * reference,
              "sqrt(" + num(x) + ") = " + num(y) + " against " + num(reference));
    }
    for (int i = 0; i < 20000; ++i) {
        const double x = 1e-6 + static_cast<double>(i) * 0.731;
        const double y = nfsu::vehicle_sqrt(x);
        const double reference = std::sqrt(x);
        if (std::fabs(y - reference) > 4e-16 * reference) {
            check(false, "sqrt(" + num(x) + ") = " + num(y) + " against " + num(reference));
            break;
        }
    }
    // Perfect squares come out exact.
    for (int i = 1; i <= 1000; ++i) {
        const double root = static_cast<double>(i);
        if (nfsu::vehicle_sqrt(root * root) != root) {
            check(false, "sqrt of " + num(root * root) + " is not exact");
            break;
        }
    }
}

void test_sin_cos() {
    for (int i = -20000; i <= 20000; ++i) {
        const double x = static_cast<double>(i) * 0.000731 * 7.0;
        const double s = nfsu::vehicle_sin(x);
        const double c = nfsu::vehicle_cos(x);
        if (std::fabs(s - std::sin(x)) > 2e-15 || std::fabs(c - std::cos(x)) > 2e-15) {
            check(false, "sin/cos(" + num(x) + ") = " + num(s) + ", " + num(c));
            break;
        }
    }
    check(nfsu::vehicle_sin(0.0) == 0.0 && nfsu::vehicle_cos(0.0) == 1.0, "sin 0 and cos 0");
    check(nfsu::vehicle_sin(2e6) == 0.0 && nfsu::vehicle_cos(2e6) == 1.0,
          "outside the range sin and cos are 0 and 1");
    check(nfsu::vehicle_sin(std::numeric_limits<double>::quiet_NaN()) == 0.0, "sin(NaN) is 0");
    check(near(nfsu::vehicle_sin(1000.0), std::sin(1000.0), 1e-13), "sin(1000)");
}

// 0x00583EA0: truncation for the index, floor for the fraction, and the
// quirk between them for -1 < u < 0.
void test_uniform_curve() {
    const float v[3] = {1.0F, 2.0F, 3.0F};
    const auto f = [&](double x) { return nfsu::vehicle_uniform_curve(v, 3, 0.0, 2.0, x); };
    check(f(0.0) == 1.0, "curve at x0");
    check(f(0.5) == 1.5, "curve halfway to the second point");
    check(f(1.0) == 2.0, "curve at the second point");
    check(f(1.75) == 2.75, "curve three quarters to the last point");
    check(f(2.0) == 3.0, "curve at x1");
    check(f(9.0) == 3.0, "curve past x1 holds the last value");
    check(f(-0.5) == 1.5,
          "curve for -1 < u < 0 mixes the first two values by u + 1: " + num(f(-0.5)));
    check(f(-0.25) == 1.75, "curve at u = -0.25: " + num(f(-0.25)));
    check(f(-1.0) == 1.0 && f(-7.0) == 1.0, "curve at u <= -1 holds the first value");
    check(f(std::numeric_limits<double>::quiet_NaN()) == 1.0, "curve at NaN");
    check(nfsu::vehicle_uniform_curve(v, 1, 0.0, 2.0, 1.0) == 1.0, "one-point curve");
    check(nfsu::vehicle_uniform_curve(v, 3, 2.0, 2.0, 1.0) == 1.0, "empty range");

    const nfsu::VehicleModel model = stock_model();
    // The 240SX's torque points sit at 800 + 900 i rpm.
    const std::array<float, 9> torque = nfsu::stock_240sx_handling().torque;
    for (std::size_t i = 0; i < torque.size(); ++i) {
        const double rpm = 800.0 + 900.0 * static_cast<double>(i);
        check(model.torque_curve(rpm) == static_cast<double>(torque[i]),
              "torque point " + std::to_string(i) + " at " + num(rpm) + " rpm");
    }
    check(model.torque_curve(4850.0) ==
              (1.0 - 0.5) * static_cast<double>(0.21F) + 0.5 * static_cast<double>(0.22F),
          "torque between points 4 and 5");
    check(model.engine_brake_curve(4400.0) == static_cast<double>(0.18F),
          "engine braking at the middle point");
}

// ---------------------------------------------------------------------------
// The record
// ---------------------------------------------------------------------------

std::vector<unsigned char> stock_record() {
    std::vector<unsigned char> record(nfsu::kVehicleRecordSize, 0x11);
    nfsu::encode_vehicle_handling(nfsu::stock_240sx_handling(), record.data(), record.size());
    return record;
}

void put_u32(std::vector<unsigned char>& bytes, std::size_t at, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFFU);
    }
}

void put_f32(std::vector<unsigned char>& bytes, std::size_t at, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put_u32(bytes, at, bits);
}

std::uint32_t get_u32(const std::vector<unsigned char>& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

void test_record_round_trip() {
    const std::vector<unsigned char> record = stock_record();
    // The layout, spot-checked by the stored bits of the retail values.
    check(get_u32(record, 0x220) == 0x3F9C28F6U, "mass 1.22 at +0x220");
    check(get_u32(record, 0x2D8) == 5U, "five forward gears at +0x2D8");
    check(get_u32(record, 0x2E8) == 0x40548B44U, "first gear 3.321 at +0x2E8");
    check(get_u32(record, 0x300) == 0x44480000U, "idle 800 at +0x300");
    check(get_u32(record, 0x370) == 0x39D1B717U, "drag 0.0004 at +0x370");
    check(get_u32(record, 0x1EC) == 0x3FE66666U, "front static friction 1.8 at +0x1EC");
    check(get_u32(record, 0x298) == 0xBE051EB8U, "front droop limit -0.13 at +0x298");
    check(record[0x12C] == 0x11 && record[0x2DC] == 0x11, "bytes outside the fields are untouched");

    nfsu::VehicleHandling decoded;
    std::vector<nfsu::VehicleHandlingIssue> issues;
    check(nfsu::decode_vehicle_handling(record.data(), record.size(), decoded, issues),
          "the stock record decodes");
    check(issues.empty(), "the stock record has no issue");
    check(nfsu::compare_vehicle_handling(decoded, nfsu::stock_240sx_handling()) ==
              nfsu::kVehicleRecordSize,
          "the decoded record equals the built-in table");

    nfsu::VehicleHandling other = nfsu::stock_240sx_handling();
    other.drag = 0.0005F;
    check(nfsu::compare_vehicle_handling(other, nfsu::stock_240sx_handling()) == 0x370,
          "a changed drag is found at +0x370");
    other.tyres[1].sliding_friction = 1.4F;
    check(nfsu::compare_vehicle_handling(other, nfsu::stock_240sx_handling()) == 0x210,
          "the first differing field is reported");
    other = nfsu::stock_240sx_handling();
    other.forward_gears = 4;
    check(nfsu::compare_vehicle_handling(other, nfsu::stock_240sx_handling()) == 0x2D8,
          "a changed gear count is found at +0x2D8");
}

void test_record_refusals() {
    struct Case {
        const char* what;
        std::function<void(std::vector<unsigned char>&)> damage;
        std::size_t offset;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<Case> cases = {
        {"a NaN mass", [&](std::vector<unsigned char>& r) { put_f32(r, 0x220, nan); }, 0x220},
        {"an infinite torque point",
         [&](std::vector<unsigned char>& r) { put_f32(r, 0x320, inf); }, 0x320},
        {"a zero mass", [](std::vector<unsigned char>& r) { put_f32(r, 0x220, 0.0F); }, 0x220},
        {"a zero rolling radius",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x130, 0.0F); }, 0x130},
        {"idle at the redline",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x300, 7500.0F); }, 0x304},
        {"a maximum below the redline",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x308, 7000.0F); }, 0x308},
        {"no forward gear", [](std::vector<unsigned char>& r) { put_u32(r, 0x2D8, 0); }, 0x2D8},
        {"seven forward gears", [](std::vector<unsigned char>& r) { put_u32(r, 0x2D8, 7); }, 0x2D8},
        {"a zero second gear",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x2EC, 0.0F); }, 0x2EC},
        {"a positive reverse", [](std::vector<unsigned char>& r) { put_f32(r, 0x2E0, 3.0F); }, 0x2E0},
        {"a rear fraction above 1",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x2D0, 1.5F); }, 0x2D0},
        {"a zero rear final drive",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x2C8, 0.0F); }, 0x2C8},
        {"a zero front static friction",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x1EC, 0.0F); }, 0x1EC},
        {"a negative rear sliding friction",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x210, -1.0F); }, 0x210},
        {"a zero front spring", [](std::vector<unsigned char>& r) { put_f32(r, 0x284, 0.0F); }, 0x284},
        {"a positive droop limit",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x298, 0.1F); }, 0x298},
        {"a negative bump limit",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x2B4, -0.1F); }, 0x2B4},
        {"a negative rear damping",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x2AC, -1.0F); }, 0x2A8},
        {"a negative drag", [](std::vector<unsigned char>& r) { put_f32(r, 0x370, -0.1F); }, 0x370},
        {"a front wheel behind the origin",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x120, -0.2F); }, 0x120},
        {"unequal rear longitudinals",
         [](std::vector<unsigned char>& r) { put_f32(r, 0x1B0, -1.0F); }, 0x180},
    };
    for (const Case& c : cases) {
        std::vector<unsigned char> record = stock_record();
        c.damage(record);
        nfsu::VehicleHandling decoded = nfsu::stock_240sx_handling();
        decoded.mass = 99.0F;
        std::vector<nfsu::VehicleHandlingIssue> issues;
        const bool ok =
            nfsu::decode_vehicle_handling(record.data(), record.size(), decoded, issues);
        check(!ok, std::string("refuses ") + c.what);
        check(!issues.empty() && issues.front().offset == c.offset,
              std::string(c.what) + " is reported at its offset" +
                  (issues.empty() ? std::string(" (no issue)") : ": " + issues.front().message));
        check(decoded.mass == 99.0F, std::string(c.what) + " leaves the output untouched");
    }
    const std::vector<unsigned char> record = stock_record();
    nfsu::VehicleHandling decoded;
    std::vector<nfsu::VehicleHandlingIssue> issues;
    check(!nfsu::decode_vehicle_handling(record.data(), record.size() - 1, decoded, issues),
          "refuses a record one byte short");
    check(!nfsu::decode_vehicle_handling(nullptr, record.size(), decoded, issues),
          "refuses a null record");
}

void test_model_refusals() {
    nfsu::VehicleSpec spec = nfsu::stock_240sx_vehicle_spec();
    spec.handling.mass = 0.0F;
    const nfsu::VehicleModel zero_mass(spec);
    check(!zero_mass.valid() && zero_mass.error().find("+0x220") != std::string::npos,
          "a model with no mass is refused by offset: " + zero_mass.error());
    nfsu::VehicleState state;
    nfsu::VehicleTelemetry t;
    check(!zero_mass.reset(state, nfsu::VehiclePose{}), "an invalid model refuses reset");
    check(!zero_mass.step(state, nfsu::VehicleInput{}, nfsu::VehicleFlatGround(), t),
          "an invalid model refuses a step");

    spec = nfsu::stock_240sx_vehicle_spec();
    spec.approx.wheel_inertia = -1.0;
    check(!nfsu::VehicleModel(spec).valid(), "a negative approximation is refused");
    spec = nfsu::stock_240sx_vehicle_spec();
    spec.approx.slip_speed_floor = 0.0;
    check(!nfsu::VehicleModel(spec).valid(), "a zero slip floor is refused");
    spec = nfsu::stock_240sx_vehicle_spec();
    spec.approx.max_steer = std::numeric_limits<double>::quiet_NaN();
    check(!nfsu::VehicleModel(spec).valid(), "a NaN approximation is refused");
    spec = nfsu::stock_240sx_vehicle_spec();
    spec.handling.struts[1].spring = 1.0F;
    const nfsu::VehicleModel soft(spec);
    check(!soft.valid() && soft.error().find("static compression") != std::string::npos,
          "a spring too soft to hold the car is refused: " + soft.error());

    const nfsu::VehicleModel model = stock_model();
    check(model.valid(), "the stock model is valid: " + model.error());
    nfsu::VehiclePose pose;
    pose.heading = std::numeric_limits<double>::infinity();
    check(!model.reset(state, pose), "reset refuses a non-finite heading");
    check(model.reset(state, nfsu::VehiclePose{}), "reset accepts the origin");
    nfsu::VehicleState broken = state;
    broken.position.x = std::numeric_limits<double>::quiet_NaN();
    const nfsu::VehicleState before = broken;
    check(!model.step(broken, nfsu::VehicleInput{}, nfsu::VehicleFlatGround(), t),
          "a step refuses a state that is not finite");
    check(broken.step == before.step && broken.velocity.x == before.velocity.x &&
              broken.wheels[0].compression == before.wheels[0].compression,
          "a refused step leaves the state untouched");
    broken = state;
    broken.gear = 7;
    check(!model.step(broken, nfsu::VehicleInput{}, nfsu::VehicleFlatGround(), t),
          "a step refuses a gear slot past top gear");
}

// ---------------------------------------------------------------------------
// The gearbox tables (0x0046D1C0)
// ---------------------------------------------------------------------------

void test_shift_tables() {
    const nfsu::VehicleModel model = stock_model();
    const auto& up = model.upshift_rpm();
    const auto& down = model.downshift_rpm();
    check(up[1] == (7500.0 - 800.0) * static_cast<double>(0.8F) + 800.0,
          "the neutral slot is 0.8 of the way from idle to redline: " + num(up[1]));
    // Found by the 50 rpm search from 4150: the first speed at which the
    // next gear's wheel torque exceeds this one's, else redline - 100.
    check(up[2] == 7400.0, "first to second at 7400 rpm: " + num(up[2]));
    check(up[3] == 7400.0, "second to third at 7400 rpm: " + num(up[3]));
    check(up[4] == 7150.0, "third to fourth at 7150 rpm: " + num(up[4]));
    check(up[5] == 6750.0, "fourth to fifth at 6750 rpm: " + num(up[5]));
    check(up[6] == 7500.0, "top gear holds to the redline: " + num(up[6]));
    const std::array<float, 8> r = nfsu::stock_240sx_handling().gear_ratios;
    for (std::size_t g = 3; g <= 6; ++g) {
        const double expected =
            static_cast<double>(r[g]) / static_cast<double>(r[g - 1]) * up[g - 1];
        check(down[g] == expected, "down threshold of slot " + std::to_string(g) + ": " +
                                       num(down[g]) + " against " + num(expected));
    }
    check(near(down[3], 4238.12, 0.01) && near(down[6], 6075.0, 0.01),
          "down thresholds 4238.12 and 6075: " + num(down[3]) + " " + num(down[6]));

    // The crossing itself: at the chosen speed the next gear pulls harder,
    // one search step earlier it did not.
    for (std::size_t g = 4; g <= 5; ++g) {
        const double rc = static_cast<double>(r[g]);
        const double rn = static_cast<double>(r[g + 1]);
        const auto next = [&](double rpm) {
            return model.torque_curve(rpm * (1.0 / rc) * rn) * (1.0 / rc) * rn;
        };
        check(next(up[g]) > model.torque_curve(up[g]),
              "slot " + std::to_string(g) + " crosses at its threshold");
        check(!(next(up[g] - 50.0) > model.torque_curve(up[g] - 50.0)),
              "slot " + std::to_string(g) + " did not cross one step earlier");
    }
}

// The same search over synthetic cars, against a restatement of 0x0046D1C0
// written here: the 240SX's own crossings happen to fall on a 100 rpm grid
// from the 4150 rpm start, so a search with the wrong step would pass the
// test above; these curves put crossings between the grid's points.
void test_shift_search() {
    std::uint32_t seed = 12345U;
    const auto next_unit = [&]() {
        seed = seed * 1664525U + 1013904223U;
        return static_cast<float>((seed >> 8) & 0xFFFFU) / 65536.0F;
    };
    int off_grid = 0;
    for (int car = 0; car < 40; ++car) {
        nfsu::VehicleSpec spec = nfsu::stock_240sx_vehicle_spec();
        nfsu::VehicleHandling& h = spec.handling;
        for (float& point : h.torque) {
            point = 0.1F + 0.2F * next_unit();
        }
        float ratio = 3.0F + next_unit();
        for (std::size_t g = 2; g < 7; ++g) {
            h.gear_ratios[g] = ratio;
            ratio = ratio * (0.6F + 0.25F * next_unit());
        }
        const nfsu::VehicleModel model(spec);
        if (!model.valid()) {
            check(false, "synthetic car " + std::to_string(car) + ": " + model.error());
            continue;
        }
        const double idle = static_cast<double>(h.idle_rpm);
        const double redline = static_cast<double>(h.redline_rpm);
        for (std::size_t g = 2; g < 6; ++g) {
            const double current = static_cast<double>(h.gear_ratios[g]);
            const double next = static_cast<double>(h.gear_ratios[g + 1]);
            double rpm = (redline + idle) * 0.5;
            double expected = redline - 100.0;
            for (; rpm < redline; rpm = 50.0 + rpm) {
                const double inverse = 1.0 / current;
                if (model.torque_curve(rpm * inverse * next) * inverse * next >
                    model.torque_curve(rpm)) {
                    expected = rpm;
                    break;
                }
            }
            check(model.upshift_rpm()[g] == expected,
                  "synthetic car " + std::to_string(car) + " slot " + std::to_string(g) + ": " +
                      num(model.upshift_rpm()[g]) + " against " + num(expected));
            if (expected < redline - 100.0 &&
                static_cast<long long>(expected - 4150.0) % 100 == 50) {
                ++off_grid;
            }
        }
    }
    check(off_grid > 0, "some synthetic crossings fall between the 100 rpm grid's points");
}

// ---------------------------------------------------------------------------
// Behaviour
// ---------------------------------------------------------------------------

void test_rest() {
    const nfsu::VehicleModel model = stock_model();
    const std::vector<nfsu::VehicleTelemetry> run = run_builtin(model, "rest");
    check(run.size() == 240, "rest runs 240 steps");
    const double weight = static_cast<double>(1.22F) * 1000.0 * static_cast<double>(9.8F);
    double worst_move = 0.0;
    double worst_speed = 0.0;
    bool idle = true;
    for (const nfsu::VehicleTelemetry& t : run) {
        worst_move = std::max(worst_move, std::fabs(t.position.x) + std::fabs(t.position.y));
        worst_speed = std::max(worst_speed, speed_of(t));
        idle = idle && t.engine_rpm == 800.0 && t.engine_torque == 0.0 &&
               t.gear == nfsu::kVehicleGearFirst;
    }
    check(idle, "at rest the engine idles in first with no torque");
    // reset() levels the car on the lever rule, which leaves the contact
    // points a few millimetres off the lever arms once the body is pitched;
    // the struts take that up in micrometres.
    check(worst_move < 1e-5, "the car stays put: " + num(worst_move));
    check(worst_speed < 1e-4, "the car stays still: " + num(worst_speed));
    check(speed_of(run.back()) < 1e-6, "and settles: " + num(speed_of(run.back())));
    const nfsu::VehicleTelemetry& last = run.back();
    const double loads =
        last.wheels[0].load + last.wheels[1].load + last.wheels[2].load + last.wheels[3].load;
    check(near(loads, weight, weight * 1e-9), "the struts carry the weight: " + num(loads));
    // The lever rule with the centre of mass at the body origin.
    const double front_share = 1.04878 / (1.39 + 1.04878);
    check(near(last.wheels[0].load, weight * front_share * 0.5, 1.0) &&
              last.wheels[0].load == last.wheels[1].load &&
              last.wheels[2].load == last.wheels[3].load,
          "front and rear loads by the lever rule: " + num(last.wheels[0].load) + " " +
              num(last.wheels[2].load));
    // The static compression of the recovered progressive spring:
    // (1 + 5.4 x) * 35000 x = load.
    const double x = last.wheels[0].compression;
    check(near((1.0 + 5.4 * x) * 35000.0 * x, last.wheels[0].load, 1e-3),
          "the front strut is at its static compression: " + num(x));
    check(x > 0.0 && x < 0.125, "the static compression is inside the strut's range");
}

void test_acceleration() {
    const nfsu::VehicleModel model = stock_model();
    const std::vector<nfsu::VehicleTelemetry> run = run_builtin(model, "accelerate");
    check(run.size() == 3660, "accelerate runs 3660 steps");
    const double target = 100.0 / 3.6;
    std::uint64_t reached = 0;
    int previous_gear = nfsu::kVehicleGearFirst;
    double previous_wheel_rpm = run.front().wheel_rpm;
    int shifts = 0;
    bool in_range = true;
    bool monotonic = true;
    bool straight = true;
    const std::array<float, 8> r = nfsu::stock_240sx_handling().gear_ratios;
    for (const nfsu::VehicleTelemetry& t : run) {
        if (reached == 0 && t.forward_speed >= target) {
            reached = t.step;
        }
        in_range = in_range && t.engine_rpm >= 800.0 && t.engine_rpm <= 8000.0;
        monotonic = monotonic && t.gear >= previous_gear;
        if (t.gear == previous_gear + 1) {
            // The recovered rule: up when the wheel-derived speed in the old
            // gear passes the threshold.
            const double old_gear_rpm =
                t.wheel_rpm / static_cast<double>(r[static_cast<std::size_t>(t.gear)]) *
                static_cast<double>(r[static_cast<std::size_t>(previous_gear)]);
            const double threshold = model.upshift_rpm()[static_cast<std::size_t>(previous_gear)];
            check(old_gear_rpm > threshold - 1e-6 && previous_wheel_rpm <= threshold + 1e-6,
                  "the shift at step " + std::to_string(t.step) + " crosses " + num(threshold) +
                      ": before " + num(previous_wheel_rpm) + " after " + num(old_gear_rpm));
            ++shifts;
        }
        straight = straight && std::fabs(t.position.y) < 1e-9 &&
                   std::fabs(t.angular_velocity.z) < 1e-9;
        previous_gear = t.gear;
        previous_wheel_rpm = t.wheel_rpm;
    }
    check(in_range, "the engine speed stays between idle and the maximum");
    check(monotonic, "full throttle never shifts down");
    check(straight, "a straight run stays straight");
    // 60 steps at rest before the throttle opens.
    const double seconds = static_cast<double>(reached - 60) / 120.0;
    check(reached != 0 && seconds >= 5.0 && seconds <= 7.5,
          "0-100 km/h in " + num(seconds) + " s, expected 5.0 to 7.5");
    check(shifts == 4 && run.back().gear == 6,
          "four upshifts to fifth: " + std::to_string(shifts));
}

void test_braking() {
    const nfsu::VehicleModel model = stock_model();
    const std::vector<nfsu::VehicleTelemetry> run = run_builtin(model, "brake");
    std::uint64_t stopped = 0;
    bool slid = false;
    bool straight = true;
    for (const nfsu::VehicleTelemetry& t : run) {
        if (stopped == 0 && t.forward_speed <= 0.01) {
            stopped = t.step;
        }
        for (const nfsu::VehicleWheelTelemetry& w : t.wheels) {
            slid = slid || w.sliding;
        }
        straight = straight && std::fabs(t.position.y) < 1e-9;
    }
    check(straight, "braking in a straight line");
    check(stopped != 0, "the car stops");
    if (stopped == 0) {
        return;
    }
    const double distance = run[stopped - 1].position.x;
    const double seconds = static_cast<double>(stopped) / 120.0;
    // Friction alone cannot stop 100 km/h in less than v^2 / (2 mu g) with
    // the highest recovered coefficient, 1.8.
    const double v = 100.0 / 3.6;
    const double floor = v * v / (2.0 * 1.8 * 9.8);
    check(distance > floor, "no shorter than friction allows: " + num(distance) + " > " + num(floor));
    check(distance >= 25.0 && distance <= 35.0,
          "100-0 km/h in " + num(distance) + " m, expected 25 to 35");
    check(seconds >= 1.6 && seconds <= 2.6, "100-0 km/h in " + num(seconds) + " s");
    check(!slid, "full braking locks no wheel");
    bool held = true;
    for (std::size_t i = static_cast<std::size_t>(stopped) + 120; i < run.size(); ++i) {
        held = held && std::fabs(run[i].forward_speed) <= 0.01;
    }
    check(held, "the held brake keeps the car stopped");
}

// Holds a speed with the throttle and a steer constant, from that speed.
std::vector<nfsu::VehicleTelemetry> run_steady(const nfsu::VehicleModel& model, double speed,
                                               double steer, std::uint64_t steps,
                                               bool handbrake_late = false) {
    std::vector<nfsu::VehicleTelemetry> out;
    nfsu::VehiclePose pose;
    pose.speed = speed;
    nfsu::VehicleState state;
    check(model.reset(state, pose), "steady reset");
    const nfsu::VehicleFlatGround ground(0.0);
    double v = speed;
    for (std::uint64_t i = 0; i < steps; ++i) {
        nfsu::VehicleInput input;
        input.steer = steer;
        const double throttle = 0.6 + 0.8 * (speed - v);
        input.throttle = throttle < 0.0 ? 0.0 : (throttle > 1.0 ? 1.0 : throttle);
        input.handbrake = handbrake_late && i >= steps / 2;
        nfsu::VehicleTelemetry t;
        if (!model.step(state, input, ground, t)) {
            check(false, "steady step");
            break;
        }
        v = t.forward_speed;
        out.push_back(t);
    }
    return out;
}

double lateral_acceleration(const nfsu::VehicleTelemetry& a, const nfsu::VehicleTelemetry& b) {
    // The component of the change in velocity across the direction of travel.
    const double dvx = (b.velocity.x - a.velocity.x) * 120.0;
    const double dvy = (b.velocity.y - a.velocity.y) * 120.0;
    const double s = std::sqrt(b.velocity.x * b.velocity.x + b.velocity.y * b.velocity.y);
    return (b.velocity.x * dvy - b.velocity.y * dvx) / s;
}

void test_cornering() {
    const nfsu::VehicleModel model = stock_model();
    const double v = 15.0;
    const std::vector<nfsu::VehicleTelemetry> left = run_steady(model, v, 0.3, 960);
    const std::vector<nfsu::VehicleTelemetry> right = run_steady(model, v, -0.3, 960);
    const nfsu::VehicleTelemetry& end = left.back();
    const double yaw = end.angular_velocity.z;
    check(yaw > 0.0, "a left steer turns left: " + num(yaw));
    check(near(yaw, -right.back().angular_velocity.z, 1e-9),
          "left and right are mirror images: " + num(yaw) + " " +
              num(right.back().angular_velocity.z));
    check(end.position.y > 0.0 && right.back().position.y < 0.0,
          "the car goes to the side it steers");
    // Steady state: the last second changes the yaw rate by under 0.5%.
    check(std::fabs(left[839].angular_velocity.z - yaw) < 0.005 * yaw,
          "steady yaw rate: " + num(left[839].angular_velocity.z) + " -> " + num(yaw));
    const double speed =
        std::sqrt(end.velocity.x * end.velocity.x + end.velocity.y * end.velocity.y);
    const double ay = lateral_acceleration(left[958], left[959]);
    check(near(ay, speed * yaw, 0.02 * speed * yaw),
          "lateral acceleration is speed times yaw rate: " + num(ay) + " " + num(speed * yaw));
    // Below the limit the car understeers: it turns less than the wheels
    // point, v * delta / L.
    const double kinematic = speed * end.steer_angle / (1.39 + 1.04878);
    check(yaw < kinematic && yaw > 0.5 * kinematic,
          "an understeering response below the limit: " + num(yaw) + " of " + num(kinematic));
    check(ay < 0.9 * 9.8, "well below the grip limit: " + num(ay));
    bool slid = false;
    for (const nfsu::VehicleTelemetry& t : left) {
        for (const nfsu::VehicleWheelTelemetry& w : t.wheels) {
            slid = slid || w.sliding;
        }
    }
    check(!slid, "no tyre slides below the limit");
    // Load moves to the outside: the right wheels, 1 and 2, in a left turn.
    check(end.wheels[1].load > end.wheels[0].load && end.wheels[2].load > end.wheels[3].load,
          "load transfer to the outside");

    // Steering response: the road wheels slew at the rate limit to the
    // speed-dependent lock, and the yaw rate reaches 90% of its steady
    // value within a second.
    const double lock = 0.55 / (1.0 + (v / 12.0) * (v / 12.0));
    for (std::size_t i = 0; i < 20; ++i) {
        const double expected = std::min(static_cast<double>(i + 1) * 3.0 / 120.0, 0.3 * lock);
        if (!near(left[i].steer_angle, expected, 2e-3)) {
            check(false, "steer slew at step " + std::to_string(i + 1) + ": " +
                             num(left[i].steer_angle) + " against " + num(expected));
            break;
        }
    }
    std::size_t ninety = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].angular_velocity.z >= 0.9 * yaw) {
            ninety = i + 1;
            break;
        }
    }
    check(ninety != 0 && ninety <= 120,
          "90% of the yaw rate after " + std::to_string(ninety) + " steps");
}

void test_grip_limit() {
    // The tyres' limit, apart from the steering aid: with the lock held at
    // 0.55 rad at every speed, 20 m/s asks up to 90 m/s^2 of the tyres.
    nfsu::VehicleSpec spec = nfsu::stock_240sx_vehicle_spec();
    spec.approx.steer_speed_reference = 1.0e6;
    const nfsu::VehicleModel fixed(spec);
    const double v = 20.0;
    const std::vector<nfsu::VehicleTelemetry> full = run_steady(fixed, v, 1.0, 600);
    const std::vector<nfsu::VehicleTelemetry> half = run_steady(fixed, v, 0.5, 600);
    const std::vector<nfsu::VehicleTelemetry> small = run_steady(fixed, v, 0.1, 600);
    const std::vector<nfsu::VehicleTelemetry> light = run_steady(fixed, v, 0.05, 600);
    const double a_full = lateral_acceleration(full[598], full[599]);
    const double a_half = lateral_acceleration(half[598], half[599]);
    const double a_small = lateral_acceleration(small[598], small[599]);
    const double a_light = lateral_acceleration(light[598], light[599]);
    std::cout << "lateral acceleration at 20 m/s by steer 0.05 0.1 0.5 1: " << num(a_light)
              << ' ' << num(a_small) << ' ' << num(a_half) << ' ' << num(a_full) << '\n';
    bool front_slid = false;
    for (std::size_t i = 300; i < full.size(); ++i) {
        front_slid = front_slid || (full[i].wheels[0].sliding && full[i].wheels[1].sliding);
    }
    check(front_slid, "full lock at 20 m/s slides both front tyres");
    // The recovered coefficients bound the lateral acceleration.
    check(a_full > 0.0 && a_full < 1.8 * 9.8, "lateral acceleration under mu g: " + num(a_full));
    // Below the limit the response is linear in the input; past it, five
    // times the input gains nothing.
    check(a_small > 1.8 * a_light, "linear below the limit: " + num(a_light) + " -> " + num(a_small));
    // Past the limit more lock gains nothing: the front tyres slide further
    // and fall from the static toward the sliding coefficient.
    check(a_full <= a_half && a_full > 0.6 * a_half,
          "saturated past the limit: " + num(a_half) + " -> " + num(a_full));

    // The handbrake locks the rear wheels, which then slide, and the car
    // yaws faster than it would on the same steer without it.
    const nfsu::VehicleModel model = stock_model();
    const std::vector<nfsu::VehicleTelemetry> plain = run_steady(model, 20.0, 0.4, 480);
    const std::vector<nfsu::VehicleTelemetry> pulled = run_steady(model, 20.0, 0.4, 480, true);
    bool rear_slid = false;
    for (std::size_t i = 240; i < 300; ++i) {
        rear_slid = rear_slid || (pulled[i].wheels[2].sliding && pulled[i].wheels[3].sliding);
    }
    check(rear_slid, "the handbrake slides the rear wheels");
    check(pulled[300].wheels[2].spin == 0.0 && pulled[300].wheels[3].spin == 0.0,
          "the handbrake holds the rear wheels still");
    check(pulled[300].angular_velocity.z > plain[300].angular_velocity.z * 1.2,
          "the handbrake turns the car in: " + num(pulled[300].angular_velocity.z) +
              " against " + num(plain[300].angular_velocity.z));
}

void test_top_speed() {
    const nfsu::VehicleModel model = stock_model();
    const std::vector<nfsu::VehicleTelemetry> run = run_builtin(model, "top-speed");
    const nfsu::VehicleTelemetry& end = run.back();
    const double v = end.forward_speed;
    check(v >= 60.0 && v <= 70.0, "top speed " + num(v * 3.6) + " km/h, expected 216 to 252");
    check(end.gear == 6 && end.engine_rpm < 7500.0, "in fifth, below the redline");
    const double gain = v - run[run.size() - 1201].forward_speed;
    check(gain >= 0.0 && gain < 0.1, "the speed has settled: " + num(gain) + " m/s in 10 s");
    // Drag-limited: the tyres' drive balances the recovered drag, 0.0004 kN
    // per (m/s)^2, and the rolling resistance.
    double drive = 0.0;
    double load = 0.0;
    for (const nfsu::VehicleWheelTelemetry& w : end.wheels) {
        drive += w.force_long;
        load += w.load;
    }
    const double speed = speed_of(end);
    const double resist = 0.4 * speed * speed + 0.015 * load;
    check(near(drive, resist, 0.01 * resist),
          "drive " + num(drive) + " N balances drag and rolling " + num(resist) + " N");

    // Coasting from there slows the car on engine braking.
    nfsu::VehiclePose pose;
    pose.speed = 60.0;
    nfsu::VehicleState state;
    check(model.reset(state, pose), "coast reset");
    nfsu::VehicleTelemetry t;
    check(model.step(state, nfsu::VehicleInput{}, nfsu::VehicleFlatGround(), t), "coast step");
    check(t.forward_speed < 60.0 && t.engine_torque < 0.0,
          "coasting slows the car on engine braking");
}

void test_drivetrain() {
    const nfsu::VehicleModel model = stock_model();
    const nfsu::VehicleHandling h = nfsu::stock_240sx_handling();
    const std::vector<nfsu::VehicleTelemetry> run = run_builtin(model, "mixed");
    double previous_rpm = 800.0;
    double previous_wheel_rpm = 0.0;
    int previous_gear = nfsu::kVehicleGearFirst;
    bool rpm_ok = true;
    bool clamp_ok = true;
    bool torque_ok = true;
    bool rear_ok = true;
    for (const nfsu::VehicleTelemetry& t : run) {
        // 0x004757D0, the rear-drive branch, recomputed in its own order.
        const double term = (t.wheels[3].spin + t.wheels[2].spin) *
                            static_cast<double>(h.final_drive_front) * 0.5 *
                            static_cast<double>(9.549304F);
        const double rpm =
            term * static_cast<double>(h.gear_ratios[static_cast<std::size_t>(t.gear)]);
        rpm_ok = rpm_ok && t.wheel_rpm == rpm;
        const double clamped = rpm < 800.0 ? 800.0 : (rpm > 8000.0 ? 8000.0 : rpm);
        clamp_ok = clamp_ok && t.engine_rpm == clamped;
        // The crank torque of a step comes from the previous step's speed;
        // at full throttle it is the torque curve alone, in N m.
        if (t.throttle == 1.0 && previous_wheel_rpm <= 8000.0) {
            torque_ok = torque_ok && t.engine_torque == model.torque_curve(previous_rpm) * 1000.0;
        }
        // Rear drive (+0x2D0 is 1.0): the front wheels get nothing, the
        // rear pair halves the rest (0x0047562C..0x00475661), in the gear
        // the step started in.
        const double share =
            static_cast<double>(h.final_drive_rear) *
            static_cast<double>(h.gear_ratios[static_cast<std::size_t>(previous_gear)]) *
            t.engine_torque * 1.0 * 0.5;
        rear_ok = rear_ok && t.wheels[0].drive_torque == 0.0 && t.wheels[1].drive_torque == 0.0 &&
                  t.wheels[2].drive_torque == share && t.wheels[3].drive_torque == share;
        previous_rpm = t.engine_rpm;
        previous_wheel_rpm = t.wheel_rpm;
        previous_gear = t.gear;
    }
    check(rpm_ok, "the wheel rpm is the recovered expression over the rear wheels");
    check(clamp_ok, "the engine rpm is the wheel rpm clamped to idle and maximum");
    check(torque_ok, "full-throttle crank torque is the torque curve");
    check(rear_ok, "the 240SX drives its rear wheels only, half each");

    // The manual gearbox: first into the limiter, then up, up and down.
    const std::vector<nfsu::VehicleTelemetry> manual = run_builtin(model, "manual");
    const double limiter_speed = 8000.0 / static_cast<double>(9.549304F) /
                                 (static_cast<double>(4.083F) * static_cast<double>(3.321F)) *
                                 static_cast<double>(0.3135F);
    check(manual[599].gear == 2 && near(manual[599].forward_speed, limiter_speed, 0.1),
          "first gear holds at the limiter, " + num(manual[599].forward_speed) + " m/s");
    bool limited = true;
    for (std::size_t i = 300; i < 600; ++i) {
        limited = limited && manual[i].engine_rpm <= 8000.0;
    }
    check(limited, "the limiter holds the engine speed");
    check(manual[600].gear == 3 && manual[1080].gear == 4 && manual[1560].gear == 3,
          "manual up, up, down on the steps asked for");
    check(manual[599].gear == 2 && manual[1079].gear == 3 && manual[1559].gear == 4,
          "and not a step early");

    // Neutral and reverse through the manual gearbox, from rest.
    nfsu::VehicleScript script;
    script.gearbox = nfsu::VehicleGearbox::manual;
    nfsu::VehicleScriptSegment s;
    s.steps = 240;
    s.input.throttle = 1.0;
    s.input.shift = nfsu::VehicleShift::down;
    script.segments.push_back(s);
    const std::vector<nfsu::VehicleTelemetry> neutral = run_script(model, script, 240);
    check(neutral.back().gear == nfsu::kVehicleGearNeutral, "down from first is neutral");
    check(std::fabs(neutral.back().forward_speed) < 1e-4 &&
              neutral.back().wheels[2].drive_torque == 0.0,
          "neutral drives nothing at full throttle");
    script.segments.front().steps = 1;
    script.segments.push_back(s);
    const std::vector<nfsu::VehicleTelemetry> reverse = run_script(model, script, 241);
    check(reverse.back().gear == nfsu::kVehicleGearReverse, "down again is reverse");
    check(reverse.back().forward_speed < -1.0 && reverse.back().engine_rpm > 800.0,
          "reverse drives backwards: " + num(reverse.back().forward_speed));

    // The automatic gearbox's reverse: down at a standstill, up again.
    nfsu::VehicleScript automatic;
    nfsu::VehicleScriptSegment a;
    a.steps = 240;
    a.input.throttle = 0.8;
    a.input.shift = nfsu::VehicleShift::down;
    automatic.segments.push_back(a);
    a.input.throttle = 0.0;
    a.input.brake = 1.0;
    a.input.shift = nfsu::VehicleShift::up;
    automatic.segments.push_back(a);
    const std::vector<nfsu::VehicleTelemetry> backwards = run_script(model, automatic, 480);
    check(backwards[239].gear == nfsu::kVehicleGearReverse && backwards[239].forward_speed < -1.0,
          "automatic reverse from a standstill: " + num(backwards[239].forward_speed));
    check(backwards[240].gear == nfsu::kVehicleGearFirst, "up from reverse is first");
    check(std::fabs(backwards.back().forward_speed) < 0.01, "and the brake stops it");

    // A down request while moving does nothing in the automatic gearbox.
    nfsu::VehicleScript moving;
    moving.speed = 10.0;
    nfsu::VehicleScriptSegment m;
    m.steps = 10;
    m.input.throttle = 0.6;
    m.input.shift = nfsu::VehicleShift::down;
    moving.segments.push_back(m);
    check(run_script(model, moving, 10).back().gear == nfsu::kVehicleGearFirst,
          "no reverse while moving");
}

struct NoGround final : nfsu::VehicleGround {
    nfsu::VehicleGroundHit cast(const nfsu::VehicleVec3&, const nfsu::VehicleVec3&,
                                double) const override {
        return nfsu::VehicleGroundHit{};
    }
};

// The flat ground at 0, with its answer altered one way.
struct AlteredGround final : nfsu::VehicleGround {
    enum class Change { long_normal, downward_normal, negative_distance, past_the_end };
    explicit AlteredGround(Change c) : change(c) {}
    nfsu::VehicleGroundHit cast(const nfsu::VehicleVec3& from, const nfsu::VehicleVec3& direction,
                                double length) const override {
        nfsu::VehicleGroundHit hit = flat.cast(from, direction, length);
        switch (change) {
        case Change::long_normal:
            hit.normal = nfsu::VehicleVec3{0.0, 0.0, 5.0};
            break;
        case Change::downward_normal:
            hit.normal = nfsu::VehicleVec3{0.0, 0.0, -1.0};
            break;
        case Change::negative_distance:
            hit.distance = -0.25;
            break;
        case Change::past_the_end:
            hit.distance = length + 0.25;
            break;
        }
        return hit;
    }
    nfsu::VehicleFlatGround flat{0.0};
    Change change;
};

std::string ground_digest(const nfsu::VehicleModel& model, const nfsu::VehicleGround& ground,
                          bool& contact) {
    nfsu::VehicleScript script;
    nfsu::builtin_vehicle_script("mixed", script);
    nfsu::VehicleState state;
    model.reset(state, nfsu::VehiclePose{});
    nfsu::VehicleTelemetryHash hash;
    nfsu::VehicleTelemetry t;
    contact = false;
    for (std::uint64_t i = 0; i < 600; ++i) {
        if (!model.step(state, script.input_at(i), ground, t)) {
            return "stopped";
        }
        contact = contact || t.wheels[0].contact;
        hash.add(t);
    }
    return nfsu::sha256_hex(hash.finish());
}

// What the model does with a ground's answer it cannot use as given.
void test_ground_answers() {
    const nfsu::VehicleModel model = stock_model();
    bool contact = false;
    const std::string flat = ground_digest(model, nfsu::VehicleFlatGround(0.0), contact);
    check(contact, "the flat ground is touched");
    check(ground_digest(model, AlteredGround(AlteredGround::Change::long_normal), contact) == flat,
          "a normal five units long is made unit length");
    const auto missed = [&](AlteredGround::Change change, const char* what) {
        bool touched = true;
        const std::string digest = ground_digest(model, AlteredGround(change), touched);
        check(digest != flat && !touched, std::string(what) + " is a miss");
    };
    missed(AlteredGround::Change::downward_normal, "a normal facing away from the car");
    missed(AlteredGround::Change::negative_distance, "a hit before the segment's start");
    missed(AlteredGround::Change::past_the_end, "a hit past the segment's end");

    nfsu::VehicleState state;
    check(model.reset(state, nfsu::VehiclePose{}), "reset");
    state.wheels[3].spin = std::numeric_limits<double>::infinity();
    nfsu::VehicleTelemetry t;
    check(!model.step(state, nfsu::VehicleInput{}, nfsu::VehicleFlatGround(), t),
          "a step refuses a wheel spin that is not finite");
}

void test_input_sanitising() {
    const nfsu::VehicleModel model = stock_model();
    nfsu::VehicleState state;
    check(model.reset(state, nfsu::VehiclePose{}), "reset");
    nfsu::VehicleInput input;
    input.throttle = std::numeric_limits<double>::quiet_NaN();
    input.brake = -3.0;
    input.steer = 5.0;
    nfsu::VehicleTelemetry t;
    check(model.step(state, input, nfsu::VehicleFlatGround(), t), "a wild input still steps");
    check(t.throttle == 0.0 && t.brake == 0.0 && t.steer == 1.0,
          "NaN reads as 0 and out of range is clamped");
    bool stepped = true;
    for (int i = 0; i < 600; ++i) {
        input.throttle = (i % 2) == 0 ? std::numeric_limits<double>::infinity() : -1.0;
        stepped = stepped && model.step(state, input, nfsu::VehicleFlatGround(), t);
    }
    check(stepped, "keeps stepping");
    check(std::isfinite(t.position.x) && std::isfinite(t.velocity.x), "the state stays finite");

    // A ground that answers nothing: the car falls with gravity alone.
    check(model.reset(state, nfsu::VehiclePose{}), "reset");
    const double z = state.position.z;
    for (int i = 0; i < 12; ++i) {
        check(model.step(state, nfsu::VehicleInput{}, NoGround(), t), "falling step");
    }
    check(!t.wheels[0].contact && t.wheels[0].load == 0.0, "no ground, no contact, no load");
    const double fall = z - t.position.z;
    const double expected = 0.5 * static_cast<double>(9.8F) * 0.01;  // 0.1 s
    check(fall > 0.8 * expected && fall < 1.2 * expected, "free fall: " + num(fall));
}

// The same run from another place and heading, over ground at another
// height, is the same motion turned and moved: #60 puts the car anywhere on
// a track.
void test_pose_invariance() {
    const nfsu::VehicleModel model = stock_model();
    nfsu::VehicleScript script;
    check(nfsu::builtin_vehicle_script("mixed", script), "mixed");
    const std::vector<nfsu::VehicleTelemetry> home = run_script(model, script, 2400);
    nfsu::VehiclePose pose;
    pose.ground = nfsu::VehicleVec3{1000.0, -250.0, 37.5};
    pose.heading = 1.5707963267948966;  // +x of the body along world +y
    nfsu::VehicleState state;
    check(model.reset(state, pose), "reset away from the origin");
    const nfsu::VehicleFlatGround ground(37.5);
    nfsu::VehicleTelemetry t;
    double worst = 0.0;
    for (std::uint64_t i = 0; i < 2400; ++i) {
        check(model.step(state, script.input_at(i), ground, t), "turned step");
        const nfsu::VehicleTelemetry& h = home[i];
        // Turned by +90 degrees: (x, y) -> (-y, x).
        const double dx = (t.position.x - 1000.0) - (-h.position.y);
        const double dy = (t.position.y + 250.0) - h.position.x;
        const double dz = (t.position.z - 37.5) - h.position.z;
        worst = std::max(worst, std::fabs(dx) + std::fabs(dy) + std::fabs(dz));
        if (t.gear != h.gear) {
            check(false, "the turned run shifts differently at step " + std::to_string(i + 1));
            break;
        }
    }
    check(worst < 1e-6, "the turned run follows the same path: " + num(worst));
    check(near(t.forward.y, home.back().forward.x, 1e-9), "the turned car points the turned way");
}

void test_flat_ground() {
    const nfsu::VehicleFlatGround ground(2.0);
    const nfsu::VehicleGroundHit hit =
        ground.cast(nfsu::VehicleVec3{0.0, 0.0, 3.5}, nfsu::VehicleVec3{0.0, 0.0, -1.0}, 2.0);
    check(hit.hit && hit.distance == 1.5 && hit.normal.z == 1.0, "a hit 1.5 below");
    check(!ground.cast(nfsu::VehicleVec3{0.0, 0.0, 3.5}, nfsu::VehicleVec3{0.0, 0.0, -1.0}, 1.0).hit,
          "too short a segment");
    check(!ground.cast(nfsu::VehicleVec3{0.0, 0.0, 1.5}, nfsu::VehicleVec3{0.0, 0.0, -1.0}, 5.0).hit,
          "starting under the ground");
    check(!ground.cast(nfsu::VehicleVec3{0.0, 0.0, 3.5}, nfsu::VehicleVec3{0.0, 0.0, 1.0}, 5.0).hit,
          "going up");
    const double s = std::sqrt(0.5);
    const nfsu::VehicleGroundHit slanted =
        ground.cast(nfsu::VehicleVec3{0.0, 0.0, 3.0}, nfsu::VehicleVec3{s, 0.0, -s}, 5.0);
    check(slanted.hit && near(slanted.distance, 1.0 / s, 1e-12), "a slanted segment");
}

// ---------------------------------------------------------------------------
// Scripts
// ---------------------------------------------------------------------------

void test_decimals() {
    double v = 0.0;
    check(nfsu::parse_vehicle_decimal("0.1", v) && v == 1.0 / 10.0, "0.1 is 1 / 10");
    check(nfsu::parse_vehicle_decimal("-0.25", v) && v == -0.25, "-0.25");
    check(nfsu::parse_vehicle_decimal("27.777778", v) && v == 27777778.0 / 1000000.0,
          "27.777778");
    check(nfsu::parse_vehicle_decimal("123456789.123456", v) &&
              v == 123456789123456.0 / 1000000.0,
          "fifteen digits");
    check(nfsu::parse_vehicle_decimal("1", v) && v == 1.0, "an integer");
    for (const char* bad : {"", "-", ".5", "1.", "1.2.3", "1e3", "0x10", "+1", "1234567890",
                            "0.1234567", " 1", "1 ", "--1", "nan", "inf"}) {
        check(!nfsu::parse_vehicle_decimal(bad, v), std::string("refuses '") + bad + "'");
    }
}

void test_script_parser() {
    const std::string text =
        "# a comment\r\n"
        "speed 12.5\r\n"
        "gearbox manual\n"
        "\n"
        "   # indented comment\n"
        "10 1 0 0 0 up\n"
        "5\t0.5\t0.25\t-1\t1\tnone\n"
        "3 0 1 0.125 0 down";
    nfsu::VehicleScript script;
    std::string error;
    check(nfsu::parse_vehicle_script(text, script, error), "parses a valid script: " + error);
    check(script.speed == 12.5 && script.gearbox == nfsu::VehicleGearbox::manual,
          "the header lines");
    check(script.segments.size() == 3 && script.total_steps() == 18, "three segments, 18 steps");
    check(script.segments[1].input.brake == 0.25 && script.segments[1].input.steer == -1.0 &&
              script.segments[1].input.handbrake,
          "the second segment's fields");
    check(script.input_at(0).shift == nfsu::VehicleShift::up &&
              script.input_at(1).shift == nfsu::VehicleShift::none,
          "a shift acts on the segment's first step only");
    check(script.input_at(15).shift == nfsu::VehicleShift::down && script.input_at(15).brake == 1.0,
          "the third segment starts at step 15");
    check(script.input_at(18).throttle == 0.0 && script.input_at(18).brake == 0.0 &&
              script.input_at(1000).shift == nfsu::VehicleShift::none,
          "past the end there is no input");

    struct Case {
        std::string text;
        std::string needle;
    };
    const std::vector<Case> cases = {
        {"", "no segment"},
        {"# only a comment\n", "no segment"},
        {"10 1 0 0 0\n", "line 1: a segment is"},
        {"10 1 0 0 0 none extra\n", "line 1: a segment is"},
        {"0 1 0 0 0 none\n", "line 1: the step count"},
        {"1e3 1 0 0 0 none\n", "line 1: the step count"},
        {"-5 1 0 0 0 none\n", "line 1: the step count"},
        {"1234567890 1 0 0 0 none\n", "line 1: the step count"},
        {"10 1.5 0 0 0 none\n", "line 1: the throttle"},
        {"10 0.5.5 0 0 0 none\n", "line 1: the throttle"},
        {"10 1 -0.1 0 0 none\n", "line 1: the brake"},
        {"10 1 0 -1.2 0 none\n", "line 1: the steer"},
        {"10 1 0 0.1234567 0 none\n", "line 1: the steer"},
        {"10 1 0 0 2 none\n", "line 1: the handbrake"},
        {"10 1 0 0 0 left\n", "line 1: the shift"},
        {"10 1 0 0 0 none\nspeed 3\n", "line 2: 'speed' must come before"},
        {"speed 3\nspeed 4\n10 1 0 0 0 none\n", "line 2: 'speed' is given twice"},
        {"gearbox manual\ngearbox manual\n", "line 2: 'gearbox' is given twice"},
        {"speed 150\n", "line 1: the speed"},
        {"speed\n", "line 1: 'speed' takes exactly one value"},
        {"gearbox sequential\n", "line 1: the gearbox"},
        {"# ok\n10 1 0 0 0 none\n10 1 0 0 0 n\xC3\xA9\n", "line 3: a byte outside printable ASCII"},
        {"10 1 0 0 0 none\x01\n", "line 1: a byte outside printable ASCII"},
    };
    for (const Case& c : cases) {
        nfsu::VehicleScript out;
        out.name = "untouched";
        std::string message;
        const bool ok = nfsu::parse_vehicle_script(c.text, out, message);
        check(!ok && message.find(c.needle) != std::string::npos,
              "refuses '" + c.needle + "': got '" + message + "'");
        check(out.name == "untouched", "a refusal leaves the output untouched");
    }

    nfsu::VehicleScriptLimits limits;
    limits.max_bytes = 20;
    std::string message;
    nfsu::VehicleScript out;
    check(!nfsu::parse_vehicle_script("10 1 0 0 0 none\n10 1 0 0 0 none\n", out, message, limits) &&
              message.find("bytes") != std::string::npos,
          "refuses a script over the byte limit: " + message);
    limits = nfsu::VehicleScriptLimits{};
    limits.max_segments = 1;
    check(!nfsu::parse_vehicle_script("10 1 0 0 0 none\n10 1 0 0 0 none\n", out, message, limits) &&
              message.find("line 2: more than 1 segments") != std::string::npos,
          "refuses too many segments: " + message);
    limits = nfsu::VehicleScriptLimits{};
    limits.max_total_steps = 15;
    check(!nfsu::parse_vehicle_script("10 1 0 0 0 none\n10 1 0 0 0 none\n", out, message, limits) &&
              message.find("line 2: more than 15 steps") != std::string::npos,
          "refuses too many steps: " + message);

    for (const std::string& name : nfsu::builtin_vehicle_script_names()) {
        nfsu::VehicleScript builtin;
        check(nfsu::builtin_vehicle_script(name, builtin) && builtin.total_steps() > 0 &&
                  builtin.name == name,
              "built-in script " + name);
    }
    nfsu::VehicleScript mixed;
    check(nfsu::builtin_vehicle_script("mixed", mixed) && mixed.total_steps() == 2400,
          "mixed is 2400 steps");
    check(!nfsu::builtin_vehicle_script("drift", mixed), "no script named drift");
}

// ---------------------------------------------------------------------------
// Telemetry and determinism
// ---------------------------------------------------------------------------

void test_telemetry_encoding() {
    nfsu::VehicleTelemetry t;
    t.step = 0x0102030405060708ULL;
    t.position.x = 1.0;
    t.gear = 6;
    t.handbrake = true;
    t.wheels[3].sliding = true;
    std::array<std::uint8_t, nfsu::kVehicleTelemetryRecordBytes> bytes{};
    nfsu::encode_vehicle_telemetry(t, bytes);
    check(bytes[0] == 0x08 && bytes[7] == 0x01, "the step is little-endian at 0");
    // 1.0 is 0x3FF0000000000000.
    check(bytes[8] == 0x00 && bytes[14] == 0xF0 && bytes[15] == 0x3F, "position.x at 8");
    // The step, four vectors and five doubles: the gear at 8 + 96 + 40.
    check(bytes[144] == 6 && bytes[145] == 0, "the gear at 144");
    // Then three doubles: the handbrake at 172.
    check(bytes[172] == 1, "the handbrake at 172");
    // Then the steer angle to 184 and four wheels of 76 bytes, the last
    // wheel's sliding flag in the record's last four bytes.
    check(bytes[484] == 1 && bytes[487] == 0 && 184 + 4 * 76 == nfsu::kVehicleTelemetryRecordBytes,
          "the last wheel's sliding flag ends the record");

    nfsu::VehicleTelemetryHash a;
    nfsu::VehicleTelemetryHash b;
    a.add(t);
    t.wheels[2].force_lat = 1e-300;
    b.add(t);
    check(a.records() == 1, "one record");
    check(nfsu::sha256_hex(a.finish()) != nfsu::sha256_hex(b.finish()), "any field moves the hash");
}

void test_determinism() {
    const nfsu::VehicleModel model = stock_model();
    const std::vector<nfsu::VehicleTelemetry> first = run_builtin(model, "mixed");
    const std::vector<nfsu::VehicleTelemetry> second = run_builtin(model, "mixed");
    const std::string a = telemetry_digest(first);
    check(a == telemetry_digest(second), "the same script twice gives the same telemetry");
    check(first.size() == 2400 && first.back().step == 2400, "2400 records");

    // Stopping halfway, copying the state into a fresh object, and going on
    // with a second model built from the same spec gives the same records.
    nfsu::VehicleScript script;
    check(nfsu::builtin_vehicle_script("mixed", script), "mixed");
    nfsu::VehicleState state;
    check(model.reset(state, nfsu::VehiclePose{}), "reset");
    nfsu::VehicleTelemetryHash hash;
    const nfsu::VehicleFlatGround ground(0.0);
    nfsu::VehicleTelemetry t;
    for (std::uint64_t i = 0; i < 1200; ++i) {
        model.step(state, script.input_at(i), ground, t);
        hash.add(t);
    }
    const nfsu::VehicleModel other = stock_model();
    nfsu::VehicleState copy = state;
    for (std::uint64_t i = 1200; i < 2400; ++i) {
        other.step(copy, script.input_at(i), ground, t);
        hash.add(t);
    }
    check(nfsu::sha256_hex(hash.finish()) == a, "a resumed run gives the same telemetry");

    // nfsu::kVehiclePinnedMixedSha256 is required of the Debug and the
    // Release build alike, because both run this test.
    check(a == nfsu::kVehiclePinnedMixedSha256, "the pinned telemetry hash of 'mixed': " + a);
    std::cout << "mixed telemetry sha256 " << a << '\n';
}

// ---------------------------------------------------------------------------
// The fixture the probe guards read
// ---------------------------------------------------------------------------

// A GLOBALB.BUN holding nothing but a valid 0x00034600 car type table: 35
// records named CAR00..CAR34 except record 18, which is 240SX, every wheel
// record valid, and `handling` written into every record.
std::vector<unsigned char> handling_fixture(const nfsu::VehicleHandling& handling) {
    const std::size_t records = 35;
    const std::size_t payload = 8 + records * nfsu::kVehicleRecordSize;
    std::vector<unsigned char> file(8 + payload, 0);
    put_u32(file, 0, 0x00034600U);
    put_u32(file, 4, static_cast<std::uint32_t>(payload));
    for (std::size_t i = 8; i < 16; ++i) {
        file[i] = 0x11;
    }
    for (std::size_t r = 0; r < records; ++r) {
        std::vector<unsigned char> record(nfsu::kVehicleRecordSize, 0);
        const std::string name =
            r == 18 ? "240SX" : "CAR" + std::string(r < 10 ? "0" : "") + std::to_string(r);
        const std::string fields[5] = {name, name, "CARS\\" + name + "\\GEOMETRY.BIN",
                                       "CARS\\" + name + "\\GEOMETRY.LZC", "BRAND"};
        const std::size_t offsets[5] = {0x00, 0x20, 0x40, 0x60, 0xC0};
        for (std::size_t f = 0; f < 5; ++f) {
            std::memcpy(record.data() + offsets[f], fields[f].data(), fields[f].size());
        }
        nfsu::encode_vehicle_handling(handling, record.data(), record.size());
        for (std::size_t k = 0; k < 4; ++k) {
            put_u32(record, 0x120 + k * 0x30 + 0x18, static_cast<std::uint32_t>(k));
        }
        std::memcpy(file.data() + 16 + r * nfsu::kVehicleRecordSize, record.data(), record.size());
    }
    return file;
}

int write_handling_fixture(const fs::path& directory) {
    nfsu::VehicleHandling other = nfsu::stock_240sx_handling();
    other.drag = 0.0005F;
    nfsu::VehicleHandling broken = nfsu::stock_240sx_handling();
    broken.idle_rpm = 9000.0F;
    const struct {
        const char* name;
        nfsu::VehicleHandling handling;
    } files[] = {
        {"vehicle_globalb.bun", nfsu::stock_240sx_handling()},
        {"vehicle_globalb_other.bun", other},
        {"vehicle_globalb_broken.bun", broken},
    };
    std::error_code ec;
    // Every target is checked before any is written, so a refusal leaves
    // nothing behind.
    for (const auto& f : files) {
        const fs::path path = directory / f.name;
        if (fs::exists(path, ec)) {
            std::cerr << "error: " << path.string()
                      << " already exists; this writer never replaces a file\n";
            return 1;
        }
    }
    fs::create_directories(directory, ec);
    for (const auto& f : files) {
        const fs::path path = directory / f.name;
        const std::vector<unsigned char> bytes = handling_fixture(f.handling);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            std::cerr << "error: cannot write " << path.string() << '\n';
            return 1;
        }
        std::cout << "wrote " << f.name << ' ' << bytes.size() << " bytes\n";
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-handling-fixture") {
        return write_handling_fixture(nfsu::argument_path(argv[2]));
    }
    if (argc != 1) {
        std::cerr << "usage: nfsu_vehicle_tests [--write-handling-fixture DIR]\n";
        return 1;
    }
    test_sqrt();
    test_sin_cos();
    test_uniform_curve();
    test_record_round_trip();
    test_record_refusals();
    test_model_refusals();
    test_shift_tables();
    test_shift_search();
    test_rest();
    test_acceleration();
    test_braking();
    test_cornering();
    test_grip_limit();
    test_top_speed();
    test_drivetrain();
    test_input_sanitising();
    test_ground_answers();
    test_pose_invariance();
    test_flat_ground();
    test_decimals();
    test_script_parser();
    test_telemetry_encoding();
    test_determinism();
    if (g_failures != 0) {
        std::cerr << g_failures << " vehicle test failure(s)\n";
        return 1;
    }
    std::cout << "vehicle tests passed\n";
    return 0;
}
