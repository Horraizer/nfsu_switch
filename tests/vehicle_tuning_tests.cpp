// The tuning tables of issue #57 (nfsu_vehicle_tuning, docs/VEHICLE_TUNING.md).
//
// Every test here is synthetic: the records it parses are built in memory
// from stock_240sx_handling() and stock_240sx_tuning(), so no test reads a
// retail file. The one retail fact the suite pins is the built-in table's
// own bytes: test_table_source encodes it and requires the 1352 bytes it
// writes, in 28 runs, to hash to kVehicle240sxTuningBytesSha256, which is
// the SHA-256 of those same bytes as record 18 of the retail GLOBALB.BUN
// holds them.

#include "nfsu/sha256.h"
#include "nfsu/vehicle.h"
#include "nfsu/vehicle_tuning.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iostream>
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

std::string num(double value) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.17g", value);
    return text;
}

std::string hex(std::size_t value) {
    char text[32];
    std::snprintf(text, sizeof(text), "0x%03zX", value);
    return text;
}

using Record = std::array<unsigned char, nfsu::kVehicleRecordSize>;

float read_f32(const unsigned char* p) {
    std::uint32_t bits = static_cast<std::uint32_t>(p[0]) |
                         (static_cast<std::uint32_t>(p[1]) << 8U) |
                         (static_cast<std::uint32_t>(p[2]) << 16U) |
                         (static_cast<std::uint32_t>(p[3]) << 24U);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t read_u32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

void write_f32(unsigned char* p, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    p[0] = static_cast<unsigned char>(bits & 0xFFU);
    p[1] = static_cast<unsigned char>((bits >> 8U) & 0xFFU);
    p[2] = static_cast<unsigned char>((bits >> 16U) & 0xFFU);
    p[3] = static_cast<unsigned char>((bits >> 24U) & 0xFFU);
}

void write_u32(unsigned char* p, std::uint32_t bits) {
    p[0] = static_cast<unsigned char>(bits & 0xFFU);
    p[1] = static_cast<unsigned char>((bits >> 8U) & 0xFFU);
    p[2] = static_cast<unsigned char>((bits >> 16U) & 0xFFU);
    p[3] = static_cast<unsigned char>((bits >> 24U) & 0xFFU);
}

// A record that carries the 240SX's handling fields and its tuning tables and
// nothing else. Every byte outside the two encoders' runs is zero.
Record stock_record() {
    Record record{};
    record.fill(0);
    nfsu::encode_vehicle_handling(nfsu::stock_240sx_handling(),
                                 record.data(), record.size());
    nfsu::encode_vehicle_tuning(nfsu::stock_240sx_tuning(),
                                record.data(), record.size());
    // encode_vehicle_handling covers the three diagonal entries of the
    // inertia matrix at +0x230 and not its element 15 at +0x26C, which the
    // retail record holds at 1.0 and which the inertia rebuild writes.
    write_f32(record.data() + 0x26C, 1.0F);
    return record;
}

// ---------------------------------------------------------------------------

void test_table_source() {
    Record zeros{};
    Record ones{};
    zeros.fill(0x00);
    ones.fill(0xFF);
    const nfsu::VehicleTuning table = nfsu::stock_240sx_tuning();
    nfsu::encode_vehicle_tuning(table, zeros.data(), zeros.size());
    nfsu::encode_vehicle_tuning(table, ones.data(), ones.size());

    std::vector<unsigned char> written;
    std::size_t runs = 0;
    std::size_t first = nfsu::kVehicleRecordSize;
    std::size_t last = 0;
    bool in_run = false;
    for (std::size_t offset = 0; offset < nfsu::kVehicleRecordSize; ++offset) {
        if (zeros[offset] == ones[offset]) {
            written.push_back(zeros[offset]);
            if (!in_run) {
                ++runs;
                in_run = true;
            }
            if (offset < first) {
                first = offset;
            }
            last = offset;
        } else {
            in_run = false;
        }
    }
    check(written.size() == nfsu::kVehicleTuningRecordBytes,
          "the tuning encoder writes " + std::to_string(written.size()) +
              " bytes, not " + std::to_string(nfsu::kVehicleTuningRecordBytes));
    check(runs == nfsu::kVehicleTuningRecordRuns,
          "the tuning encoder writes " + std::to_string(runs) + " runs, not " +
              std::to_string(nfsu::kVehicleTuningRecordRuns));
    check(first == nfsu::kVehicleTuningWeightOffset,
          "the first byte written is " + hex(first) + ", not " +
              hex(nfsu::kVehicleTuningWeightOffset));
    check(last + 1 == nfsu::kVehicleTuningEnd,
          "the last byte written is " + hex(last) + ", not " +
              hex(nfsu::kVehicleTuningEnd - 1));

    const std::string digest =
        nfsu::sha256_hex(nfsu::sha256(written.data(), written.size()));
    check(digest == nfsu::kVehicle240sxTuningBytesSha256,
          "the built-in 240SX tuning table hashes to " + digest + ", not " +
              nfsu::kVehicle240sxTuningBytesSha256);
}

void test_round_trip() {
    const Record record = stock_record();
    nfsu::VehicleTuning decoded;
    std::vector<nfsu::VehicleTuningIssue> issues;
    check(nfsu::decode_vehicle_tuning(record.data(), record.size(), decoded, issues),
          "the built-in record's tuning tables are refused");
    for (const nfsu::VehicleTuningIssue& issue : issues) {
        std::cerr << "  " << hex(issue.offset) << ": " << issue.message << '\n';
    }
    const std::size_t differs =
        nfsu::compare_vehicle_tuning(decoded, nfsu::stock_240sx_tuning());
    check(differs == nfsu::kVehicleRecordSize,
          "the round trip differs first at " + hex(differs));

    // A few values by hand, so that a silent shift of every offset by one
    // field cannot pass the round trip.
    check(decoded.weight[2].mass_scale == 0.85F,
          "level 3 weight scale is " + num(decoded.weight[2].mass_scale));
    check(decoded.gearbox[2].stored_forward_gears == 6,
          "the level 3 gearbox has " +
              std::to_string(decoded.gearbox[2].stored_forward_gears) + " gears");
    check(decoded.tyres[2].axles[0].static_friction == 2.2F &&
              decoded.tyres[2].axles[1].static_friction == 2.3F,
          "the level 3 tyre peaks are not 2.2 front and 2.3 rear");
    check(decoded.suspension[2].axles[0].spring == 110.0F &&
              decoded.suspension[2].axles[1].spring == 85.0F,
          "the level 3 spring rates are not 110 front and 85 rear");
    check(decoded.nitrous[1].capacity == 4.0F && decoded.nitrous[1].count == 4,
          "the level 2 nitrous entry is not (4, 4)");
    check(decoded.item.mass_scale_bonus == 0.015F,
          "the mass item is " + num(decoded.item.mass_scale_bonus));
    check(decoded.item.rear_drive_fraction_override == 0.801F,
          "the drive split override is " +
              num(decoded.item.rear_drive_fraction_override));
}

// ---------------------------------------------------------------------------

struct Refusal {
    const char* what;
    std::size_t offset;  // the record offset the issue must name
    void (*mutate)(Record&);
};

void test_refusals() {
    static const Refusal refusals[] = {
        {"a non-finite mass scale", 0x3C0,
         [](Record& r) { write_f32(r.data() + 0x3C0, std::nanf("")); }},
        {"a mass scale of zero", 0x3C0,
         [](Record& r) { write_f32(r.data() + 0x3C0, 0.0F); }},
        {"a mass scale above two", 0x3C0,
         [](Record& r) { write_f32(r.data() + 0x3C0, 2.5F); }},
        {"a yaw inertia scale of zero", 0x3C8,
         [](Record& r) { write_f32(r.data() + 0x3C8, 0.0F); }},
        {"a reserved weight word", 0x3CC,
         [](Record& r) { write_u32(r.data() + 0x3CC, 1); }},
        {"a final drive of zero", 0x3F8,
         [](Record& r) { write_f32(r.data() + 0x3F8, 0.0F); }},
        {"a rear drive fraction above one", 0x400,
         [](Record& r) { write_f32(r.data() + 0x400, 1.5F); }},
        {"a positive reverse ratio", 0x410,
         [](Record& r) { write_f32(r.data() + 0x410, 3.0F); }},
        {"a non-zero neutral ratio", 0x414,
         [](Record& r) { write_f32(r.data() + 0x414, 1.0F); }},
        {"forward ratios that rise", 0x418,
         [](Record& r) { write_f32(r.data() + 0x41C, 9.0F); }},
        {"a stored gear count that the ratios do not give", 0x408,
         [](Record& r) { write_u32(r.data() + 0x408, 4); }},
        {"a non-finite torque delta", 0x4C8,
         [](Record& r) { write_f32(r.data() + 0x4C8, std::nanf("")); }},
        {"a non-finite engine brake delta", 0x4E4,
         [](Record& r) { write_f32(r.data() + 0x4E4, std::nanf("")); }},
        {"a reserved induction word", 0x654,
         [](Record& r) { write_u32(r.data() + 0x654, 1); }},
        {"a non-finite induction point", 0x638,
         [](Record& r) { write_f32(r.data() + 0x638, std::nanf("")); }},
        {"a tyre slip scale of zero", 0x6C0,
         [](Record& r) { write_f32(r.data() + 0x6C0, 0.0F); }},
        {"a tyre peak friction of zero", 0x6CC,
         [](Record& r) { write_f32(r.data() + 0x6CC, 0.0F); }},
        {"a tyre sliding friction of zero", 0x6F0,
         [](Record& r) { write_f32(r.data() + 0x6F0, 0.0F); }},
        {"a reserved tyre word", 0x6D4,
         [](Record& r) { write_u32(r.data() + 0x6D4, 1); }},
        {"a nitrous capacity of zero", 0x780,
         [](Record& r) { write_f32(r.data() + 0x780, 0.0F); }},
        {"a nitrous count of zero", 0x784,
         [](Record& r) { write_u32(r.data() + 0x784, 0); }},
        {"a nitrous count that is not its capacity", 0x784,
         [](Record& r) { write_u32(r.data() + 0x784, 3); }},
        {"a nitrous rate of zero", 0x78C,
         [](Record& r) { write_f32(r.data() + 0x78C, 0.0F); }},
        {"a reserved nitrous word", 0x788,
         [](Record& r) { write_u32(r.data() + 0x788, 1); }},
        {"a non-finite downforce delta", 0x7B4,
         [](Record& r) { write_f32(r.data() + 0x7B4, std::nanf("")); }},
        {"a non-finite grip block value", 0x810,
         [](Record& r) { write_f32(r.data() + 0x810, std::nanf("")); }},
        {"a spring rate of zero", 0x844,
         [](Record& r) { write_f32(r.data() + 0x844, 0.0F); }},
        {"a bump limit of zero", 0x854,
         [](Record& r) { write_f32(r.data() + 0x854, 0.0F); }},
        {"a droop limit above zero", 0x858,
         [](Record& r) { write_f32(r.data() + 0x858, 0.1F); }},
        {"a reserved suspension word", 0x88C,
         [](Record& r) { write_u32(r.data() + 0x88C, 1); }},
        {"a negative mass item", 0x930,
         [](Record& r) { write_f32(r.data() + 0x930, -0.1F); }},
        {"a drive split override above one", 0x934,
         [](Record& r) { write_f32(r.data() + 0x934, 1.5F); }},
        {"a negative tyre friction item", 0x940,
         [](Record& r) { write_f32(r.data() + 0x940, -0.1F); }},
        {"a non-finite item torque point", 0x974,
         [](Record& r) { write_f32(r.data() + 0x974, std::nanf("")); }},
    };

    for (const Refusal& refusal : refusals) {
        Record record = stock_record();
        refusal.mutate(record);
        nfsu::VehicleTuning tuning;
        std::vector<nfsu::VehicleTuningIssue> issues;
        const bool ok =
            nfsu::decode_vehicle_tuning(record.data(), record.size(), tuning, issues);
        check(!ok, std::string("accepted ") + refusal.what);
        bool named = false;
        for (const nfsu::VehicleTuningIssue& issue : issues) {
            if (issue.offset == refusal.offset) {
                named = true;
            }
        }
        check(named, std::string(refusal.what) + " was not reported at " +
                         hex(refusal.offset));
    }

    // The two refusals that are not about a field.
    Record record = stock_record();
    nfsu::VehicleTuning tuning;
    std::vector<nfsu::VehicleTuningIssue> issues;
    check(!nfsu::decode_vehicle_tuning(record.data(), record.size() - 1, tuning, issues),
          "a short record was accepted");
    check(issues.size() == 1 && issues[0].offset == nfsu::kVehicleRecordSize,
          "a short record is not reported once, on the record");
    check(!nfsu::decode_vehicle_tuning(nullptr, record.size(), tuning, issues),
          "a null record was accepted");
    check(!nfsu::decode_vehicle_tuning(record.data(), record.size() * 2, tuning, issues),
          "an oversized record was accepted");
}

// ---------------------------------------------------------------------------

void test_box_inertia() {
    const nfsu::VehicleHandling handling = nfsu::stock_240sx_handling();
    const std::array<float, 3> inertia = nfsu::vehicle_box_inertia(
        handling.mass, handling.length, handling.width, handling.height);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        check(inertia[axis] == handling.inertia[axis],
              "box inertia axis " + std::to_string(axis) + " is " +
                  num(inertia[axis]) + ", and the record holds " +
                  num(handling.inertia[axis]));
    }
    // The rule's shape: doubling every dimension is four times the inertia,
    // and doubling the mass is twice.
    const std::array<float, 3> bigger = nfsu::vehicle_box_inertia(
        handling.mass, handling.length * 2.0F, handling.width * 2.0F,
        handling.height * 2.0F);
    const std::array<float, 3> heavier = nfsu::vehicle_box_inertia(
        handling.mass * 2.0F, handling.length, handling.width, handling.height);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        check(std::fabs(static_cast<double>(bigger[axis]) -
                        4.0 * static_cast<double>(inertia[axis])) <= 1e-6,
              "scaling the box does not scale the inertia by four");
        check(std::fabs(static_cast<double>(heavier[axis]) -
                        2.0 * static_cast<double>(inertia[axis])) <= 1e-6,
              "doubling the mass does not double the inertia");
    }
}

void test_apply_nothing() {
    const Record record = stock_record();
    Record out{};
    out.fill(0xAB);
    nfsu::VehicleUpgradeLevels levels;
    std::vector<nfsu::VehicleTuningIssue> issues;
    check(nfsu::apply_vehicle_tuning(record.data(), record.size(), levels,
                                     out.data(), out.size(), issues),
          "applying nothing to the built-in record was refused");
    // The 240SX's stored inertia already is the box inertia and its +0x334
    // curve is zero, so the setup sub-object comes out byte for byte the
    // same. That is the whole point of the rebuild being unconditional.
    std::size_t differs = nfsu::kVehicleRecordSize;
    for (std::size_t offset = 0; offset < nfsu::kVehicleRecordSize; ++offset) {
        if (out[offset] != record[offset]) {
            differs = offset;
            break;
        }
    }
    check(differs == nfsu::kVehicleRecordSize,
          "applying nothing changed the record at " + hex(differs));
}

void test_apply_levels() {
    const Record record = stock_record();
    const nfsu::VehicleTuning tuning = nfsu::stock_240sx_tuning();
    const nfsu::VehicleHandling stock = nfsu::stock_240sx_handling();

    nfsu::VehicleUpgradeLevels levels;
    levels.weight = 3;
    levels.suspension = 3;
    levels.engine_a = 3;
    levels.engine_b = 3;
    levels.induction = 3;
    levels.nitrous = 3;
    levels.gearbox = 3;
    levels.tyres = 3;
    levels.grip = 3;
    levels.aero_a = 3;
    levels.aero_b = 3;
    levels.mass_item = true;
    levels.tyre_friction_item = true;
    levels.nitrous_item = true;
    levels.grip_item = true;

    Record out{};
    std::vector<nfsu::VehicleTuningIssue> issues;
    check(nfsu::apply_vehicle_tuning(record.data(), record.size(), levels,
                                     out.data(), out.size(), issues),
          "applying level 3 everything was refused");

    nfsu::VehicleHandling tuned;
    std::vector<nfsu::VehicleHandlingIssue> handling_issues;
    check(nfsu::decode_vehicle_handling(out.data(), out.size(), tuned,
                                        handling_issues),
          "the fully upgraded record is not a handling record");

    const float expected_mass = static_cast<float>(
        (static_cast<double>(tuning.weight[2].mass_scale) -
         static_cast<double>(tuning.item.mass_scale_bonus)) *
        static_cast<double>(stock.mass));
    check(tuned.mass == expected_mass,
          "the upgraded mass is " + num(tuned.mass) + ", not " + num(expected_mass));

    const std::array<float, 3> expected_inertia = nfsu::vehicle_box_inertia(
        expected_mass, stock.length, stock.width, stock.height);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        check(tuned.inertia[axis] == expected_inertia[axis],
              "the upgraded inertia axis " + std::to_string(axis) + " is " +
                  num(tuned.inertia[axis]));
    }

    check(tuned.forward_gears == 6,
          "the upgraded gearbox has " + std::to_string(tuned.forward_gears) +
              " forward gears");
    for (std::size_t slot = 0; slot < nfsu::kVehicleGearSlots; ++slot) {
        check(tuned.gear_ratios[slot] == tuning.gearbox[2].gear_ratios[slot],
              "gear slot " + std::to_string(slot) + " is not the table's");
    }
    check(read_u32(out.data() + 0x488) == 0,
          "0x005763E1 did not zero the level 3 gearbox entry's stored count");

    for (std::size_t axle = 0; axle < nfsu::kVehicleTuningAxles; ++axle) {
        const float peak = static_cast<float>(
            static_cast<double>(tuning.tyres[2].axles[axle].static_friction) +
            static_cast<double>(tuning.item.tyre_static_friction_bonus[axle]));
        const float slide = static_cast<float>(
            static_cast<double>(tuning.tyres[2].axles[axle].sliding_friction) +
            static_cast<double>(tuning.item.tyre_sliding_friction_bonus[axle]));
        check(tuned.tyres[axle].static_friction == peak,
              "axle " + std::to_string(axle) + " peak friction is " +
                  num(tuned.tyres[axle].static_friction) + ", not " + num(peak));
        check(tuned.tyres[axle].sliding_friction == slide,
              "axle " + std::to_string(axle) + " sliding friction is " +
                  num(tuned.tyres[axle].sliding_friction));
        check(tuned.struts[axle].spring == tuning.suspension[2].axles[axle].spring,
              "axle " + std::to_string(axle) + " spring rate is not the table's");
    }

    // The centre of mass moves forward by the weight table's delta and down
    // by the suspension table's drop; each wheel rises by its axle's delta.
    const float expected_com_long = static_cast<float>(
        static_cast<double>(stock.centre_of_mass[0]) +
        static_cast<double>(tuning.weight[2].centre_of_mass_longitudinal_delta));
    const float expected_com_vert = static_cast<float>(
        static_cast<double>(stock.centre_of_mass[2]) -
        static_cast<double>(tuning.suspension[2].centre_of_mass_vertical_drop));
    check(tuned.centre_of_mass[0] == expected_com_long,
          "the upgraded centre of mass is " + num(tuned.centre_of_mass[0]) +
              " forward, not " + num(expected_com_long));
    check(tuned.centre_of_mass[2] == expected_com_vert,
          "the upgraded centre of mass is " + num(tuned.centre_of_mass[2]) +
              " up, not " + num(expected_com_vert));
    for (std::size_t wheel = 0; wheel < nfsu::kVehicleWheelCount; ++wheel) {
        const float rise = wheel < 2 ? tuning.suspension[2].front_wheel_vertical_rise
                                     : tuning.suspension[2].rear_wheel_vertical_rise;
        const float expected = static_cast<float>(
            static_cast<double>(stock.wheels[wheel].vertical) +
            static_cast<double>(rise));
        check(tuned.wheels[wheel].vertical == expected,
              "wheel " + std::to_string(wheel) + " is at " +
                  num(tuned.wheels[wheel].vertical) + ", not " + num(expected));
    }

    // The engine: both tables, the induction curve and nothing else, since
    // no torque item is installed here.
    check(tuned.idle_rpm == stock.idle_rpm, "the idle rpm moved");
    const float expected_redline = static_cast<float>(
        static_cast<double>(static_cast<float>(
            static_cast<double>(stock.redline_rpm) +
            static_cast<double>(tuning.engine_a[2].redline_rpm_delta))) +
        static_cast<double>(tuning.engine_b[2].redline_rpm_delta));
    check(tuned.redline_rpm == expected_redline,
          "the upgraded redline is " + num(tuned.redline_rpm) + ", not " +
              num(expected_redline));
    for (std::size_t point = 0; point < nfsu::kVehicleTorquePoints; ++point) {
        double want = static_cast<double>(stock.torque[point]);
        want = static_cast<double>(static_cast<float>(
            want + static_cast<double>(tuning.engine_a[2].torque_delta[point])));
        want = static_cast<double>(static_cast<float>(
            want + static_cast<double>(tuning.engine_b[2].torque_delta[point])));
        want = static_cast<double>(static_cast<float>(
            want + static_cast<double>(tuning.induction[2].torque[point])));
        check(tuned.torque[point] == static_cast<float>(want),
              "torque point " + std::to_string(point) + " is " +
                  num(tuned.torque[point]) + ", not " + num(want));
    }

    // The blocks the handling struct does not carry.
    const unsigned char* sub = out.data() + nfsu::kVehicleSubObjectOffset;
    check(read_f32(sub + 0x160) == tuning.weight[2].yaw_inertia_scale,
          "the yaw inertia scale is not the weight table's");
    check(read_f32(sub + 0x274) == static_cast<float>(
              static_cast<double>(tuning.grip[2].values[0]) +
              static_cast<double>(tuning.item.grip_block_bonus)),
          "the grip block's first value is not the table's plus the item's");
    check(read_f32(sub + 0x278) == tuning.grip[2].values[1] &&
              read_f32(sub + 0x27C) == tuning.grip[2].values[2],
          "the grip block's other two values are not the table's");
    check(read_f32(sub + 0x2A0) == tuning.nitrous[2].capacity &&
              read_u32(sub + 0x2A4) == tuning.nitrous[2].count &&
              read_u32(sub + 0x2A8) == 0 &&
              read_f32(sub + 0x2AC) == static_cast<float>(
                  static_cast<double>(tuning.nitrous[2].rate) +
                  static_cast<double>(tuning.item.nitrous_rate_bonus)),
          "the nitrous block is not the table's plus the item's rate bonus");
    for (std::size_t i = 0; i < 3; ++i) {
        const float expected = static_cast<float>(
            static_cast<double>(static_cast<float>(
                static_cast<double>(read_f32(record.data() + 0x374 + 4 * i)) +
                static_cast<double>(tuning.aero_b[2].downforce_delta[i]))) +
            static_cast<double>(tuning.aero_a[2].downforce_delta[i]));
        check(read_f32(sub + 0x264 + 4 * i) == expected,
              "downforce word " + std::to_string(i) + " is " +
                  num(read_f32(sub + 0x264 + 4 * i)) + ", not " + num(expected));
    }
    for (std::size_t point = 0; point < nfsu::kVehicleTorquePoints; ++point) {
        check(read_f32(sub + 0x224 + 4 * point) == tuning.induction[2].torque[point],
              "the induction curve was not copied to +0x334");
    }
}

void test_apply_items_only() {
    const Record record = stock_record();
    const nfsu::VehicleTuning tuning = nfsu::stock_240sx_tuning();
    const nfsu::VehicleHandling stock = nfsu::stock_240sx_handling();

    nfsu::VehicleUpgradeLevels levels;
    levels.mass_item = true;
    levels.drive_split_item = true;
    levels.torque_item_a = true;
    levels.torque_item_b = true;
    levels.torque_item_c = true;

    Record out{};
    std::vector<nfsu::VehicleTuningIssue> issues;
    check(nfsu::apply_vehicle_tuning(record.data(), record.size(), levels,
                                     out.data(), out.size(), issues),
          "applying the items alone was refused");
    nfsu::VehicleHandling tuned;
    std::vector<nfsu::VehicleHandlingIssue> handling_issues;
    check(nfsu::decode_vehicle_handling(out.data(), out.size(), tuned,
                                        handling_issues),
          "the item-only record is not a handling record");

    // With no weight table the scale starts at the 1.0 of 0x006CC7BC.
    const float expected_mass = static_cast<float>(
        (1.0 - static_cast<double>(tuning.item.mass_scale_bonus)) *
        static_cast<double>(stock.mass));
    check(tuned.mass == expected_mass,
          "the item-only mass is " + num(tuned.mass) + ", not " + num(expected_mass));
    check(tuned.rear_drive_fraction == tuning.item.rear_drive_fraction_override,
          "the drive split item did not set the rear fraction");

    // The three item curves are added in the order A, B, C at each point,
    // and curve C also lands in +0x334.
    const unsigned char* sub = out.data() + nfsu::kVehicleSubObjectOffset;
    for (std::size_t point = 0; point < nfsu::kVehicleTorquePoints; ++point) {
        double want = static_cast<double>(stock.torque[point]);
        for (std::size_t curve = 0; curve < nfsu::kVehicleTuningItemCurves; ++curve) {
            want = static_cast<double>(static_cast<float>(
                want + static_cast<double>(tuning.item_torque[curve][point])));
        }
        // +0x334 is zero for this car, so the unconditional add of it is a
        // no-op before the item curves and adds curve C's point after.
        want = static_cast<double>(static_cast<float>(want));
        check(tuned.torque[point] == static_cast<float>(want),
              "the item-only torque point " + std::to_string(point) + " is " +
                  num(tuned.torque[point]) + ", not " + num(want));
        check(read_f32(sub + 0x224 + 4 * point) == tuning.item_torque[2][point],
              "item curve C did not land in +0x334 at point " +
                  std::to_string(point));
    }
}

// Round 2 of the review, finding F1. The strut block's eighth float at
// +0x1C is not a copy of the bump limit at +0x14: over the retail file the
// two differ in 71 of the 210 table blocks and in 14 of the 35 records'
// stock blocks, and the 240SX - the only car this suite's fixture carries -
// is one of the records where they agree, which is why nothing here caught
// the claim that they are always equal. This gives the two different values
// in every block of every level and requires the whole path to carry +0x1C
// on its own: the decoder, the round trip, the encoder's byte runs and the
// strut block apply writes at sub +0x18C and +0x1AC.
void test_strut_unknown_1c() {
    nfsu::VehicleTuning tuning = nfsu::stock_240sx_tuning();
    for (std::size_t level = 0; level < nfsu::kVehicleTuningLevels; ++level) {
        for (std::size_t axle = 0; axle < nfsu::kVehicleTuningAxles; ++axle) {
            // Distinct from the bump limit, from the droop limit and from
            // every other block's value, and positive so no bound refuses it.
            tuning.suspension[level].unknown_1c[axle] =
                0.0625F + 0.0078125F * static_cast<float>(2 * level + axle);
            check(tuning.suspension[level].unknown_1c[axle] !=
                      tuning.suspension[level].axles[axle].bump_limit,
                  "the fixture's +0x1C equals the bump limit, so it tests nothing");
        }
    }

    Record record = stock_record();
    nfsu::encode_vehicle_tuning(tuning, record.data(), record.size());
    nfsu::VehicleTuning decoded;
    std::vector<nfsu::VehicleTuningIssue> issues;
    check(nfsu::decode_vehicle_tuning(record.data(), record.size(), decoded, issues),
          "a record whose strut +0x1C differs from its bump limit was refused");
    check(nfsu::compare_vehicle_tuning(decoded, tuning) == nfsu::kVehicleRecordSize,
          "the round trip does not carry strut +0x1C independently");
    for (std::size_t level = 0; level < nfsu::kVehicleTuningLevels; ++level) {
        const std::size_t entry =
            nfsu::kVehicleTuningSuspensionBase +
            nfsu::kVehicleTuningSuspensionStride * (level + 1);
        for (std::size_t axle = 0; axle < nfsu::kVehicleTuningAxles; ++axle) {
            const std::size_t block = entry + nfsu::kVehicleAxleStride * axle;
            check(read_f32(record.data() + block + 0x1C) ==
                      tuning.suspension[level].unknown_1c[axle],
                  "the encoder did not write strut +0x1C at level " +
                      std::to_string(level + 1));
            check(read_f32(record.data() + block + 0x1C) !=
                      read_f32(record.data() + block + 0x14),
                  "the encoded +0x1C and +0x14 are equal at level " +
                      std::to_string(level + 1));
        }
    }

    // And the apply path copies the whole 0x20-byte block, +0x1C included,
    // over the stock strut blocks at sub +0x170 and +0x190.
    for (int level = 1; level <= 3; ++level) {
        nfsu::VehicleUpgradeLevels levels;
        levels.suspension = level;
        Record out{};
        check(nfsu::apply_vehicle_tuning(record.data(), record.size(), levels,
                                         out.data(), out.size(), issues),
              "a suspension level of " + std::to_string(level) + " was refused");
        const unsigned char* sub = out.data() + nfsu::kVehicleSubObjectOffset;
        for (std::size_t axle = 0; axle < nfsu::kVehicleTuningAxles; ++axle) {
            const unsigned char* block = sub + 0x170 + nfsu::kVehicleAxleStride * axle;
            check(read_f32(block + 0x1C) ==
                      tuning.suspension[static_cast<std::size_t>(level) - 1]
                          .unknown_1c[axle],
                  "apply did not carry strut +0x1C at level " +
                      std::to_string(level));
            check(read_f32(block + 0x1C) != read_f32(block + 0x14),
                  "apply made +0x1C equal to the bump limit at level " +
                      std::to_string(level));
        }
    }
}

// Round 2 of the review, finding F2. The recovered stop condition of
// 0x005763E8..0x0057640D is equality to the 0.0 at 0x006CC7A4, not "not
// above 0.0": 0x005763FA masks C3 and C2 out of the compare and 0x005763FD
// jumps on no parity, so a negative ratio is counted and the walk continues
// past it. This pins vehicle_derived_forward_gears directly, including that
// case. The case is unreachable through decode_vehicle_tuning and
// apply_vehicle_tuning, because check_vehicle_tuning refuses a negative
// forward ratio first - which test_refusals pins at 0x41C - so what is
// pinned here is the rule, not any answer the library will return.
void test_derived_gear_rule() {
    using Ratios = std::array<float, nfsu::kVehicleGearSlots>;
    const auto ratios = [](std::initializer_list<float> forward) {
        Ratios r{};
        r[nfsu::kVehicleGearReverse] = -3.5F;
        r[nfsu::kVehicleGearNeutral] = 0.0F;
        std::size_t slot = static_cast<std::size_t>(nfsu::kVehicleGearFirst);
        for (float value : forward) {
            r[slot++] = value;
        }
        return r;
    };

    struct Case {
        const char* what;
        Ratios r;
        std::uint32_t gears;
    };
    const Case cases[] = {
        {"one gear", ratios({3.0F}), 1},
        {"five gears", ratios({3.3F, 1.9F, 1.3F, 1.0F, 0.9F}), 5},
        {"six gears", ratios({3.3F, 1.9F, 1.3F, 1.1F, 0.9F, 0.8F}), 6},
        // The equality stop: the zero ends the walk and the ratios after it
        // are not counted, even a positive one.
        {"a zero before a positive ratio", ratios({3.0F, 2.0F, 0.0F, 1.0F}), 2},
        // The masked C0: a negative ratio is counted and the walk goes on,
        // which a "greater than zero" reading would get wrong.
        {"a negative ratio in the middle", ratios({3.0F, -1.0F, 2.0F}), 3},
        {"a negative ratio first", ratios({-1.0F, 2.0F}), 2},
        // Unordered sets C3 and C2 together, an even byte, so a NaN is
        // counted too.
        {"a NaN ratio", ratios({3.0F, std::nanf(""), 2.0F}), 3},
        // No forward ratio at all: the first slot is the stop.
        {"no forward ratio", ratios({}), 0},
        // Every slot filled: the walk ends at the loop bound, slot 8.
        {"all six slots filled", ratios({6.0F, 5.0F, 4.0F, 3.0F, 2.0F, 1.0F}), 6},
    };
    for (const Case& c : cases) {
        const std::uint32_t got = nfsu::vehicle_derived_forward_gears(c.r);
        check(got == c.gears,
              std::string("the derived gear count for ") + c.what + " is " +
                  std::to_string(got) + ", not " + std::to_string(c.gears));
    }

    // A "greater than zero" reading gives a different answer on the two
    // negative cases, so this test would fail if the rule were restated that
    // way; that is what it exists to stop.
    const Ratios negative = ratios({3.0F, -1.0F, 2.0F});
    std::uint32_t above_zero = 0;
    for (std::size_t slot = static_cast<std::size_t>(nfsu::kVehicleGearFirst);
         slot < nfsu::kVehicleGearSlots; ++slot) {
        if (!(negative[slot] > 0.0F)) {
            break;
        }
        ++above_zero;
    }
    check(above_zero == 1 && nfsu::vehicle_derived_forward_gears(negative) == 3,
          "the two readings of the stop condition no longer disagree, so this "
          "test pins nothing");
}

// Round 2 of the review, note N4. The 240SX figures docs/VEHICLE_TUNING.md
// used to quote for the engine tables belong in a test rather than in the
// document, because stock_240sx_tuning() and stock_240sx_handling() hold the
// values they are computed from. What the document keeps is the shape: the
// ratio is this car's, not the table's.
void test_engine_table_ratios() {
    const nfsu::VehicleTuning tuning = nfsu::stock_240sx_tuning();
    const nfsu::VehicleHandling stock = nfsu::stock_240sx_handling();
    const double fractions[nfsu::kVehicleTuningLevels] = {0.085, 0.17, 0.25};
    for (std::size_t level = 0; level < nfsu::kVehicleTuningLevels; ++level) {
        for (std::size_t point = 0; point < nfsu::kVehicleTorquePoints; ++point) {
            const double ratio =
                static_cast<double>(tuning.engine_a[level].torque_delta[point]) /
                static_cast<double>(stock.torque[point]);
            check(std::fabs(ratio - fractions[level]) <= 1e-7,
                  "the 240SX engine table level " + std::to_string(level + 1) +
                      " torque delta at point " + std::to_string(point) +
                      " is " + num(ratio) + " of stock, not " +
                      num(fractions[level]));
            check(tuning.engine_b[level].torque_delta[point] ==
                      tuning.engine_a[level].torque_delta[point],
                  "the two engine tables' torque deltas differ at level " +
                      std::to_string(level + 1));
        }
    }
    // The engine braking deltas do not follow that ratio even on this car,
    // which is why the document does not claim they do.
    const double brake[nfsu::kVehicleEngineBrakePoints] = {0.25, 2.5 / 9.0, 0.25 / 1.3};
    bool all_quarter = true;
    for (std::size_t point = 0; point < nfsu::kVehicleEngineBrakePoints; ++point) {
        const double ratio =
            static_cast<double>(tuning.engine_a[2].engine_brake_delta[point]) /
            static_cast<double>(stock.engine_brake[point]);
        check(std::fabs(ratio - brake[point]) <= 1e-6,
              "the 240SX level 3 engine braking delta at point " +
                  std::to_string(point) + " is " + num(ratio) + " of stock");
        if (std::fabs(ratio - 0.25) > 1e-6) {
            all_quarter = false;
        }
    }
    check(!all_quarter,
          "the engine braking deltas are all a quarter of stock after all, so "
          "the document's distinction between them and the torque deltas is empty");
    // Table B raises the rev limits and table A does not, on this car.
    check(tuning.engine_a[2].redline_rpm_delta == 0.0F &&
              tuning.engine_a[2].max_rpm_delta == 0.0F,
          "engine table A raises the rev limits");
    check(tuning.engine_b[2].redline_rpm_delta > 0.0F &&
              tuning.engine_b[2].max_rpm_delta > 0.0F,
          "engine table B does not raise the rev limits");
}

void test_level_gate() {
    const Record record = stock_record();
    Record out{};
    std::vector<nfsu::VehicleTuningIssue> issues;
    for (int level = 0; level <= 3; ++level) {
        nfsu::VehicleUpgradeLevels levels;
        levels.tyres = level;
        check(nfsu::apply_vehicle_tuning(record.data(), record.size(), levels,
                                         out.data(), out.size(), issues),
              "a tyre level of " + std::to_string(level) + " was refused");
    }
    for (int level : {-1, 4, 7}) {
        nfsu::VehicleUpgradeLevels levels;
        levels.tyres = level;
        Record untouched{};
        untouched.fill(0x5A);
        Record target = untouched;
        check(!nfsu::apply_vehicle_tuning(record.data(), record.size(), levels,
                                          target.data(), target.size(), issues),
              "a tyre level of " + std::to_string(level) + " was accepted");
        check(target == untouched,
              "a refused apply wrote to the output record");
    }
    nfsu::VehicleUpgradeLevels levels;
    check(!nfsu::apply_vehicle_tuning(record.data(), record.size() - 1, levels,
                                      out.data(), out.size(), issues),
          "a short source record was accepted by apply");
    check(!nfsu::apply_vehicle_tuning(record.data(), record.size(), levels,
                                      out.data(), out.size() - 1, issues),
          "a short output record was accepted by apply");
    check(!nfsu::apply_vehicle_tuning(nullptr, record.size(), levels,
                                      out.data(), out.size(), issues),
          "a null source record was accepted by apply");

    // A record whose tuning tables are malformed is refused by apply too,
    // with the decoder's own issue.
    Record broken = record;
    write_f32(broken.data() + 0x6CC, 0.0F);
    check(!nfsu::apply_vehicle_tuning(broken.data(), broken.size(), levels,
                                      out.data(), out.size(), issues),
          "apply accepted a record with a zero tyre peak friction");
    bool named = false;
    for (const nfsu::VehicleTuningIssue& issue : issues) {
        if (issue.offset == 0x6CC) {
            named = true;
        }
    }
    check(named, "apply did not report the malformed tyre block at 0x6CC");
}

void test_gear_count_rule() {
    // 0x005763F0: the count is the run of forward slots whose ratio is above
    // zero, and the loop stops at the first that is not.
    nfsu::VehicleTuning tuning = nfsu::stock_240sx_tuning();
    Record record = stock_record();
    for (std::uint32_t gears = 1; gears <= 6; ++gears) {
        for (std::size_t slot = nfsu::kVehicleGearFirst;
             slot < nfsu::kVehicleGearSlots; ++slot) {
            const std::size_t gear = slot - nfsu::kVehicleGearFirst;
            tuning.gearbox[0].gear_ratios[slot] =
                gear < gears ? 4.0F - 0.5F * static_cast<float>(gear) : 0.0F;
        }
        tuning.gearbox[0].stored_forward_gears = gears;
        nfsu::encode_vehicle_tuning(tuning, record.data(), record.size());
        nfsu::VehicleUpgradeLevels levels;
        levels.gearbox = 1;
        Record out{};
        std::vector<nfsu::VehicleTuningIssue> issues;
        check(nfsu::apply_vehicle_tuning(record.data(), record.size(), levels,
                                         out.data(), out.size(), issues),
              "a synthetic " + std::to_string(gears) + "-speed gearbox was refused");
        nfsu::VehicleHandling tuned;
        std::vector<nfsu::VehicleHandlingIssue> handling_issues;
        check(nfsu::decode_vehicle_handling(out.data(), out.size(), tuned,
                                            handling_issues),
              "a synthetic " + std::to_string(gears) +
                  "-speed gearbox is not a handling record");
        check(tuned.forward_gears == gears,
              "a synthetic " + std::to_string(gears) + "-speed gearbox gave " +
                  std::to_string(tuned.forward_gears) + " gears");
    }
}

} // namespace

int main() {
    test_table_source();
    test_round_trip();
    test_refusals();
    test_box_inertia();
    test_apply_nothing();
    test_apply_levels();
    test_apply_items_only();
    test_strut_unknown_1c();
    test_derived_gear_rule();
    test_engine_table_ratios();
    test_level_gate();
    test_gear_count_rule();
    if (g_failures != 0) {
        std::cerr << g_failures << " vehicle tuning test failure(s)\n";
        return 1;
    }
    std::cout << "vehicle tuning tests passed\n";
    return 0;
}
