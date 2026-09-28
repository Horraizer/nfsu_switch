#include "nfsu/vehicle_tuning.h"

#include <cstring>

namespace nfsu {

namespace {

// 0x006CCA6C, the float 1/12 the inertia rebuild multiplies by
// (0x00576324, 0x00576338, 0x0057634C).
constexpr float kTwelfth = 0.083333335F;

// 0x006CC7A4, the 0.0f the gear-slot count compares against (0x005763F2).
constexpr float kGearRatioStop = 0.0F;

float read_f32(const unsigned char* p) noexcept {
    std::uint32_t bits = static_cast<std::uint32_t>(p[0]) |
                         (static_cast<std::uint32_t>(p[1]) << 8U) |
                         (static_cast<std::uint32_t>(p[2]) << 16U) |
                         (static_cast<std::uint32_t>(p[3]) << 24U);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t read_u32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

void write_u32(unsigned char* p, std::uint32_t bits) noexcept {
    p[0] = static_cast<unsigned char>(bits & 0xFFU);
    p[1] = static_cast<unsigned char>((bits >> 8U) & 0xFFU);
    p[2] = static_cast<unsigned char>((bits >> 16U) & 0xFFU);
    p[3] = static_cast<unsigned char>((bits >> 24U) & 0xFFU);
}

void write_f32(unsigned char* p, float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32(p, bits);
}

bool finite32(float value) noexcept { return (value - value) == 0.0F; }

void fail(std::vector<VehicleTuningIssue>& issues,
          std::size_t offset,
          const char* message) {
    VehicleTuningIssue issue;
    issue.offset = offset;
    issue.message = message;
    issues.push_back(issue);
}

// The suspension table's entry for a level, as 0x00575A1C computes it.
std::size_t suspension_entry(int level) noexcept {
    return kVehicleTuningSuspensionBase +
           kVehicleTuningSuspensionStride * static_cast<std::size_t>(level);
}

std::size_t level_entry(std::size_t base, std::size_t stride, int level) noexcept {
    return base + stride * static_cast<std::size_t>(level - 1);
}

// Reserved words: every one of these is zero in all 35 retail records and no
// traced instruction reads any of them.
std::vector<std::size_t> reserved_offsets() {
    std::vector<std::size_t> out;
    for (int level = 1; level <= 3; ++level) {
        out.push_back(level_entry(kVehicleTuningWeightOffset,
                                  kVehicleTuningWeightStride, level) + 0x0C);
        std::size_t induction = level_entry(kVehicleTuningInductionOffset,
                                            kVehicleTuningInductionStride, level);
        out.push_back(induction + 0x24);
        out.push_back(induction + 0x28);
        out.push_back(induction + 0x2C);
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            std::size_t tyre = level_entry(kVehicleTuningTyreOffset,
                                           kVehicleTuningTyreStride, level) +
                               kVehicleAxleStride * axle;
            out.push_back(tyre + 0x14);
            out.push_back(tyre + 0x18);
            out.push_back(tyre + 0x1C);
        }
        out.push_back(level_entry(kVehicleTuningNitrousOffset,
                                  kVehicleTuningNitrousStride, level) + 0x08);
        out.push_back(level_entry(kVehicleTuningAeroAOffset,
                                  kVehicleTuningAeroStride, level) + 0x0C);
        out.push_back(level_entry(kVehicleTuningAeroBOffset,
                                  kVehicleTuningAeroStride, level) + 0x0C);
        out.push_back(level_entry(kVehicleTuningGripOffset,
                                  kVehicleTuningGripStride, level) + 0x0C);
        out.push_back(suspension_entry(level) + 0x4C);
    }
    return out;
}

} // namespace

std::array<float, 3> vehicle_box_inertia(float mass,
                                         float length,
                                         float width,
                                         float height) noexcept {
    // 0x005762FC..0x00576352. The game holds the three squares and the sums
    // on the x87 stack and rounds once, at the store; this computes the same
    // expressions in binary64 and rounds once, at the same place. The two
    // agree on all 105 diagonal entries of the 35 retail records.
    const double l2 = static_cast<double>(length) * static_cast<double>(length);
    const double w2 = static_cast<double>(width) * static_cast<double>(width);
    const double h2 = static_cast<double>(height) * static_cast<double>(height);
    const double m = static_cast<double>(mass);
    const double k = static_cast<double>(kTwelfth);
    std::array<float, 3> out{};
    out[0] = static_cast<float>((h2 + w2) * m * k);
    out[1] = static_cast<float>((h2 + l2) * m * k);
    out[2] = static_cast<float>((w2 + l2) * m * k);
    return out;
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

namespace {

void decode_tyre(const unsigned char* base, VehicleAxleTyre& out) noexcept {
    out.slip_scale = read_f32(base + 0x00);
    out.lateral_load_scale = read_f32(base + 0x04);
    out.lateral_scale = read_f32(base + 0x08);
    out.static_friction = read_f32(base + 0x0C);
    out.sliding_friction = read_f32(base + 0x10);
}

void encode_tyre(const VehicleAxleTyre& tyre, unsigned char* base) noexcept {
    write_f32(base + 0x00, tyre.slip_scale);
    write_f32(base + 0x04, tyre.lateral_load_scale);
    write_f32(base + 0x08, tyre.lateral_scale);
    write_f32(base + 0x0C, tyre.static_friction);
    write_f32(base + 0x10, tyre.sliding_friction);
}

void decode_strut(const unsigned char* base, VehicleAxleStrut& out) noexcept {
    out.progressive = read_f32(base + 0x00);
    out.spring = read_f32(base + 0x04);
    out.damping_bump = read_f32(base + 0x08);
    out.damping_rebound = read_f32(base + 0x0C);
    out.anti_roll = read_f32(base + 0x10);
    out.bump_limit = read_f32(base + 0x14);
    out.droop_limit = read_f32(base + 0x18);
}

void encode_strut(const VehicleAxleStrut& strut, unsigned char* base) noexcept {
    write_f32(base + 0x00, strut.progressive);
    write_f32(base + 0x04, strut.spring);
    write_f32(base + 0x08, strut.damping_bump);
    write_f32(base + 0x0C, strut.damping_rebound);
    write_f32(base + 0x10, strut.anti_roll);
    write_f32(base + 0x14, strut.bump_limit);
    write_f32(base + 0x18, strut.droop_limit);
}

} // namespace

std::uint32_t vehicle_derived_forward_gears(
    const std::array<float, kVehicleGearSlots>& ratios) noexcept {
    // 0x005763E8..0x0057640D: ecx starts at slot 2, the loop bound is slot
    // 8, and each turn compares the slot's ratio with the 0.0 at
    // 0x006CC7A4 and leaves on equality. fcomp reports the result in C3, C2
    // and C0; 0x005763FA masks C3 and C2 with `test ah, 0x44` and
    // 0x005763FD takes `jnp` out of the loop. A ratio above zero clears all
    // three, so the masked byte is 0x00, parity is even and the jump is not
    // taken; a ratio equal to zero sets C3 alone, so the byte is 0x40,
    // parity is odd and the jump leaves; a ratio below zero sets C0 alone,
    // which the mask drops, so the byte is 0x00 again and the slot is
    // counted. So the stop condition is equality to 0.0, not "not above
    // 0.0", and a negative ratio - or a NaN, which sets C3 and C2 together
    // for an even byte - is counted.
    std::uint32_t count = 0;
    for (std::size_t slot = static_cast<std::size_t>(kVehicleGearFirst);
         slot < kVehicleGearSlots; ++slot) {
        if (ratios[slot] == kGearRatioStop) {
            break;
        }
        ++count;
    }
    return count;
}

bool decode_vehicle_tuning(const unsigned char* record,
                           std::size_t size,
                           VehicleTuning& out,
                           std::vector<VehicleTuningIssue>& issues) {
    issues.clear();
    out = VehicleTuning{};
    if (record == nullptr) {
        fail(issues, kVehicleRecordSize, "no record");
        return false;
    }
    if (size != kVehicleRecordSize) {
        fail(issues, kVehicleRecordSize,
             "a CarTypeInfo record is exactly 0xC90 bytes");
        return false;
    }

    for (std::size_t offset : reserved_offsets()) {
        if (read_u32(record + offset) != 0) {
            fail(issues, offset,
                 "reserved word, zero in every retail record, is not zero");
        }
    }

    for (std::size_t level = 0; level < kVehicleTuningLevels; ++level) {
        const int l = static_cast<int>(level) + 1;

        const unsigned char* w = record + level_entry(kVehicleTuningWeightOffset,
                                                     kVehicleTuningWeightStride, l);
        VehicleTuningWeight& weight = out.weight[level];
        weight.mass_scale = read_f32(w + 0x00);
        weight.centre_of_mass_longitudinal_delta = read_f32(w + 0x04);
        weight.yaw_inertia_scale = read_f32(w + 0x08);

        const unsigned char* g = record + level_entry(kVehicleTuningGearboxOffset,
                                                     kVehicleTuningGearboxStride, l);
        VehicleTuningGearbox& gearbox = out.gearbox[level];
        gearbox.shift_fraction = read_f32(g + 0x00);
        gearbox.unknown_2c4 = read_f32(g + 0x04);
        gearbox.final_drive_rear = read_f32(g + 0x08);
        gearbox.final_drive_front = read_f32(g + 0x0C);
        gearbox.rear_drive_fraction = read_f32(g + 0x10);
        gearbox.unknown_2d4 = read_f32(g + 0x14);
        gearbox.stored_forward_gears = read_u32(g + 0x18);
        gearbox.unknown_2dc = read_f32(g + 0x1C);
        for (std::size_t slot = 0; slot < kVehicleGearSlots; ++slot) {
            gearbox.gear_ratios[slot] = read_f32(g + 0x20 + 4 * slot);
        }

        for (int which = 0; which < 2; ++which) {
            const std::size_t base = which == 0 ? kVehicleTuningEngineAOffset
                                               : kVehicleTuningEngineBOffset;
            const unsigned char* e =
                record + level_entry(base, kVehicleTuningEngineStride, l);
            VehicleTuningEngine& engine =
                which == 0 ? out.engine_a[level] : out.engine_b[level];
            engine.idle_rpm_delta = read_f32(e + 0x00);
            engine.redline_rpm_delta = read_f32(e + 0x04);
            engine.max_rpm_delta = read_f32(e + 0x08);
            engine.unknown_30c_delta = read_f32(e + 0x0C);
            for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
                engine.torque_delta[i] = read_f32(e + 0x10 + 4 * i);
            }
            for (std::size_t i = 0; i < kVehicleEngineBrakePoints; ++i) {
                engine.engine_brake_delta[i] = read_f32(e + 0x34 + 4 * i);
            }
        }

        const unsigned char* ind = record + level_entry(kVehicleTuningInductionOffset,
                                                       kVehicleTuningInductionStride, l);
        for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
            out.induction[level].torque[i] = read_f32(ind + 4 * i);
        }

        const unsigned char* t = record + level_entry(kVehicleTuningTyreOffset,
                                                      kVehicleTuningTyreStride, l);
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            decode_tyre(t + kVehicleAxleStride * axle, out.tyres[level].axles[axle]);
        }

        const unsigned char* n = record + level_entry(kVehicleTuningNitrousOffset,
                                                      kVehicleTuningNitrousStride, l);
        out.nitrous[level].capacity = read_f32(n + 0x00);
        out.nitrous[level].count = read_u32(n + 0x04);
        out.nitrous[level].rate = read_f32(n + 0x0C);

        const unsigned char* a = record + level_entry(kVehicleTuningAeroAOffset,
                                                      kVehicleTuningAeroStride, l);
        const unsigned char* b = record + level_entry(kVehicleTuningAeroBOffset,
                                                      kVehicleTuningAeroStride, l);
        const unsigned char* gr = record + level_entry(kVehicleTuningGripOffset,
                                                       kVehicleTuningGripStride, l);
        for (std::size_t i = 0; i < 3; ++i) {
            out.aero_a[level].downforce_delta[i] = read_f32(a + 4 * i);
            out.aero_b[level].downforce_delta[i] = read_f32(b + 4 * i);
            out.grip[level].values[i] = read_f32(gr + 4 * i);
        }

        const unsigned char* s = record + suspension_entry(l);
        VehicleTuningSuspension& susp = out.suspension[level];
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            decode_strut(s + kVehicleAxleStride * axle, susp.axles[axle]);
            susp.unknown_1c[axle] =
                read_f32(s + kVehicleAxleStride * axle + 0x1C);
        }
        susp.centre_of_mass_vertical_drop = read_f32(s + 0x40);
        susp.front_wheel_vertical_rise = read_f32(s + 0x44);
        susp.rear_wheel_vertical_rise = read_f32(s + 0x48);
    }

    const unsigned char* item = record + kVehicleTuningItemOffset;
    out.item.mass_scale_bonus = read_f32(item + 0x00);
    out.item.rear_drive_fraction_override = read_f32(item + 0x04);
    out.item.nitrous_rate_bonus = read_f32(item + 0x08);
    out.item.grip_block_bonus = read_f32(item + 0x0C);
    for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
        out.item.tyre_static_friction_bonus[axle] = read_f32(item + 0x10 + 4 * axle);
        out.item.tyre_sliding_friction_bonus[axle] = read_f32(item + 0x18 + 4 * axle);
    }
    for (std::size_t curve = 0; curve < kVehicleTuningItemCurves; ++curve) {
        const unsigned char* c = record + kVehicleTuningItemCurveOffset +
                                 kVehicleTuningItemCurveStride * curve;
        for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
            out.item_torque[curve][i] = read_f32(c + 4 * i);
        }
    }

    std::vector<VehicleTuningIssue> checks;
    if (!check_vehicle_tuning(out, checks)) {
        issues.insert(issues.end(), checks.begin(), checks.end());
    }
    return issues.empty();
}

bool check_vehicle_tuning(const VehicleTuning& tuning,
                          std::vector<VehicleTuningIssue>& issues) {
    issues.clear();

    auto check_finite = [&issues](float value, std::size_t offset, const char* what) {
        if (!finite32(value)) {
            fail(issues, offset, what);
        }
    };

    for (std::size_t level = 0; level < kVehicleTuningLevels; ++level) {
        const int l = static_cast<int>(level) + 1;

        const std::size_t w = level_entry(kVehicleTuningWeightOffset,
                                          kVehicleTuningWeightStride, l);
        const VehicleTuningWeight& weight = tuning.weight[level];
        check_finite(weight.mass_scale, w + 0x00, "mass scale is not finite");
        check_finite(weight.centre_of_mass_longitudinal_delta, w + 0x04,
                     "centre of mass delta is not finite");
        check_finite(weight.yaw_inertia_scale, w + 0x08,
                     "yaw inertia scale is not finite");
        if (!(weight.mass_scale > 0.0F) || !(weight.mass_scale <= 2.0F)) {
            fail(issues, w + 0x00, "mass scale outside (0, 2]");
        }
        if (!(weight.yaw_inertia_scale > 0.0F)) {
            fail(issues, w + 0x08, "yaw inertia scale is not above zero");
        }

        const std::size_t g = level_entry(kVehicleTuningGearboxOffset,
                                          kVehicleTuningGearboxStride, l);
        const VehicleTuningGearbox& gearbox = tuning.gearbox[level];
        check_finite(gearbox.shift_fraction, g + 0x00, "shift fraction is not finite");
        check_finite(gearbox.unknown_2c4, g + 0x04, "+0x2C4 is not finite");
        check_finite(gearbox.final_drive_rear, g + 0x08, "rear final drive is not finite");
        check_finite(gearbox.final_drive_front, g + 0x0C, "front final drive is not finite");
        check_finite(gearbox.rear_drive_fraction, g + 0x10,
                     "rear drive fraction is not finite");
        check_finite(gearbox.unknown_2d4, g + 0x14, "+0x2D4 is not finite");
        check_finite(gearbox.unknown_2dc, g + 0x1C, "+0x2DC is not finite");
        if (!(gearbox.final_drive_rear > 0.0F) || !(gearbox.final_drive_front > 0.0F)) {
            fail(issues, g + 0x08, "a final drive is not above zero");
        }
        if (!(gearbox.rear_drive_fraction >= 0.0F) ||
            !(gearbox.rear_drive_fraction <= 1.0F)) {
            fail(issues, g + 0x10, "rear drive fraction outside [0, 1]");
        }
        for (std::size_t slot = 0; slot < kVehicleGearSlots; ++slot) {
            check_finite(gearbox.gear_ratios[slot], g + 0x20 + 4 * slot,
                         "a gear ratio is not finite");
        }
        if (!(gearbox.gear_ratios[kVehicleGearReverse] < 0.0F)) {
            fail(issues, g + 0x20, "the reverse ratio is not negative");
        }
        if (gearbox.gear_ratios[kVehicleGearNeutral] != 0.0F) {
            fail(issues, g + 0x24, "the neutral ratio is not zero");
        }
        for (std::size_t slot = kVehicleGearFirst; slot < kVehicleGearSlots; ++slot) {
            if (!(gearbox.gear_ratios[slot] >= 0.0F)) {
                fail(issues, g + 0x20 + 4 * slot, "a forward ratio is negative");
            }
            if (slot + 1 < kVehicleGearSlots &&
                gearbox.gear_ratios[slot] < gearbox.gear_ratios[slot + 1]) {
                fail(issues, g + 0x20 + 4 * slot,
                     "the forward ratios do not fall from first gear up");
            }
        }
        const std::uint32_t derived =
            vehicle_derived_forward_gears(gearbox.gear_ratios);
        if (derived == 0 || derived > 6) {
            fail(issues, g + 0x20 + 4 * kVehicleGearFirst,
                 "the ratios give no gear, or more than six");
        }
        if (gearbox.stored_forward_gears != derived) {
            fail(issues, g + 0x18,
                 "the stored gear count is not the one the ratios give");
        }

        for (int which = 0; which < 2; ++which) {
            const std::size_t base = which == 0 ? kVehicleTuningEngineAOffset
                                               : kVehicleTuningEngineBOffset;
            const std::size_t e = level_entry(base, kVehicleTuningEngineStride, l);
            const VehicleTuningEngine& engine =
                which == 0 ? tuning.engine_a[level] : tuning.engine_b[level];
            check_finite(engine.idle_rpm_delta, e + 0x00, "idle rpm delta is not finite");
            check_finite(engine.redline_rpm_delta, e + 0x04,
                         "redline rpm delta is not finite");
            check_finite(engine.max_rpm_delta, e + 0x08, "max rpm delta is not finite");
            check_finite(engine.unknown_30c_delta, e + 0x0C, "+0x30C delta is not finite");
            for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
                check_finite(engine.torque_delta[i], e + 0x10 + 4 * i,
                             "a torque delta is not finite");
            }
            for (std::size_t i = 0; i < kVehicleEngineBrakePoints; ++i) {
                check_finite(engine.engine_brake_delta[i], e + 0x34 + 4 * i,
                             "an engine brake delta is not finite");
            }
        }

        const std::size_t ind = level_entry(kVehicleTuningInductionOffset,
                                            kVehicleTuningInductionStride, l);
        for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
            check_finite(tuning.induction[level].torque[i], ind + 4 * i,
                         "an induction torque point is not finite");
        }

        const std::size_t t = level_entry(kVehicleTuningTyreOffset,
                                          kVehicleTuningTyreStride, l);
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            const std::size_t o = t + kVehicleAxleStride * axle;
            const VehicleAxleTyre& tyre = tuning.tyres[level].axles[axle];
            check_finite(tyre.slip_scale, o + 0x00, "tyre slip scale is not finite");
            check_finite(tyre.lateral_load_scale, o + 0x04,
                         "tyre load scale is not finite");
            check_finite(tyre.lateral_scale, o + 0x08,
                         "tyre lateral scale is not finite");
            check_finite(tyre.static_friction, o + 0x0C,
                         "tyre peak friction is not finite");
            check_finite(tyre.sliding_friction, o + 0x10,
                         "tyre sliding friction is not finite");
            if (!(tyre.slip_scale > 0.0F)) {
                fail(issues, o + 0x00, "tyre slip scale is not above zero");
            }
            if (!(tyre.static_friction > 0.0F)) {
                fail(issues, o + 0x0C, "tyre peak friction is not above zero");
            }
            if (!(tyre.sliding_friction > 0.0F)) {
                fail(issues, o + 0x10, "tyre sliding friction is not above zero");
            }
        }

        const std::size_t n = level_entry(kVehicleTuningNitrousOffset,
                                          kVehicleTuningNitrousStride, l);
        const VehicleTuningNitrous& nitrous = tuning.nitrous[level];
        check_finite(nitrous.capacity, n + 0x00, "nitrous capacity is not finite");
        check_finite(nitrous.rate, n + 0x0C, "nitrous rate is not finite");
        if (!(nitrous.capacity > 0.0F)) {
            fail(issues, n + 0x00, "nitrous capacity is not above zero");
        }
        if (nitrous.count == 0 || nitrous.count > 64) {
            fail(issues, n + 0x04, "nitrous count outside 1..64");
        } else if (static_cast<float>(nitrous.count) != nitrous.capacity) {
            fail(issues, n + 0x04,
                 "the nitrous count and its capacity differ, and they are equal "
                 "in every retail record");
        }
        if (!(nitrous.rate > 0.0F)) {
            fail(issues, n + 0x0C, "nitrous rate is not above zero");
        }

        const std::size_t a = level_entry(kVehicleTuningAeroAOffset,
                                          kVehicleTuningAeroStride, l);
        const std::size_t b = level_entry(kVehicleTuningAeroBOffset,
                                          kVehicleTuningAeroStride, l);
        const std::size_t gr = level_entry(kVehicleTuningGripOffset,
                                           kVehicleTuningGripStride, l);
        for (std::size_t i = 0; i < 3; ++i) {
            check_finite(tuning.aero_a[level].downforce_delta[i], a + 4 * i,
                         "a downforce delta is not finite");
            check_finite(tuning.aero_b[level].downforce_delta[i], b + 4 * i,
                         "a downforce delta is not finite");
            check_finite(tuning.grip[level].values[i], gr + 4 * i,
                         "a grip block value is not finite");
        }

        const std::size_t s = suspension_entry(l);
        const VehicleTuningSuspension& susp = tuning.suspension[level];
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            const std::size_t o = s + kVehicleAxleStride * axle;
            const VehicleAxleStrut& strut = susp.axles[axle];
            check_finite(strut.progressive, o + 0x00, "strut progressive is not finite");
            check_finite(strut.spring, o + 0x04, "strut spring rate is not finite");
            check_finite(strut.damping_bump, o + 0x08, "strut bump damping is not finite");
            check_finite(strut.damping_rebound, o + 0x0C,
                         "strut rebound damping is not finite");
            check_finite(strut.anti_roll, o + 0x10, "strut anti-roll is not finite");
            check_finite(strut.bump_limit, o + 0x14, "strut bump limit is not finite");
            check_finite(strut.droop_limit, o + 0x18, "strut droop limit is not finite");
            check_finite(susp.unknown_1c[axle], o + 0x1C,
                         "strut +0x1C is not finite");
            if (!(strut.spring > 0.0F)) {
                fail(issues, o + 0x04, "strut spring rate is not above zero");
            }
            if (!(strut.bump_limit > 0.0F)) {
                fail(issues, o + 0x14, "strut bump limit is not above zero");
            }
            if (!(strut.droop_limit < 0.0F)) {
                fail(issues, o + 0x18, "strut droop limit is not below zero");
            }
        }
        check_finite(susp.centre_of_mass_vertical_drop, s + 0x40,
                     "centre of mass drop is not finite");
        check_finite(susp.front_wheel_vertical_rise, s + 0x44,
                     "front wheel rise is not finite");
        check_finite(susp.rear_wheel_vertical_rise, s + 0x48,
                     "rear wheel rise is not finite");
    }

    const std::size_t item = kVehicleTuningItemOffset;
    check_finite(tuning.item.mass_scale_bonus, item + 0x00,
                 "mass scale bonus is not finite");
    check_finite(tuning.item.rear_drive_fraction_override, item + 0x04,
                 "rear drive fraction override is not finite");
    check_finite(tuning.item.nitrous_rate_bonus, item + 0x08,
                 "nitrous rate bonus is not finite");
    check_finite(tuning.item.grip_block_bonus, item + 0x0C,
                 "grip block bonus is not finite");
    if (!(tuning.item.mass_scale_bonus >= 0.0F)) {
        fail(issues, item + 0x00, "mass scale bonus is negative");
    }
    if (!(tuning.item.rear_drive_fraction_override >= 0.0F) ||
        !(tuning.item.rear_drive_fraction_override <= 1.0F)) {
        fail(issues, item + 0x04, "rear drive fraction override outside [0, 1]");
    }
    for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
        check_finite(tuning.item.tyre_static_friction_bonus[axle], item + 0x10 + 4 * axle,
                     "a tyre peak friction bonus is not finite");
        check_finite(tuning.item.tyre_sliding_friction_bonus[axle], item + 0x18 + 4 * axle,
                     "a tyre sliding friction bonus is not finite");
        if (!(tuning.item.tyre_static_friction_bonus[axle] >= 0.0F)) {
            fail(issues, item + 0x10 + 4 * axle,
                 "a tyre peak friction bonus is negative");
        }
        if (!(tuning.item.tyre_sliding_friction_bonus[axle] >= 0.0F)) {
            fail(issues, item + 0x18 + 4 * axle,
                 "a tyre sliding friction bonus is negative");
        }
    }
    for (std::size_t curve = 0; curve < kVehicleTuningItemCurves; ++curve) {
        const std::size_t c = kVehicleTuningItemCurveOffset +
                              kVehicleTuningItemCurveStride * curve;
        for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
            check_finite(tuning.item_torque[curve][i], c + 4 * i,
                         "an item torque point is not finite");
        }
    }

    return issues.empty();
}

void encode_vehicle_tuning(const VehicleTuning& tuning,
                           unsigned char* record,
                           std::size_t size) noexcept {
    if (record == nullptr || size != kVehicleRecordSize) {
        return;
    }
    for (std::size_t level = 0; level < kVehicleTuningLevels; ++level) {
        const int l = static_cast<int>(level) + 1;

        unsigned char* w = record + level_entry(kVehicleTuningWeightOffset,
                                                kVehicleTuningWeightStride, l);
        write_f32(w + 0x00, tuning.weight[level].mass_scale);
        write_f32(w + 0x04, tuning.weight[level].centre_of_mass_longitudinal_delta);
        write_f32(w + 0x08, tuning.weight[level].yaw_inertia_scale);

        unsigned char* g = record + level_entry(kVehicleTuningGearboxOffset,
                                                kVehicleTuningGearboxStride, l);
        const VehicleTuningGearbox& gearbox = tuning.gearbox[level];
        write_f32(g + 0x00, gearbox.shift_fraction);
        write_f32(g + 0x04, gearbox.unknown_2c4);
        write_f32(g + 0x08, gearbox.final_drive_rear);
        write_f32(g + 0x0C, gearbox.final_drive_front);
        write_f32(g + 0x10, gearbox.rear_drive_fraction);
        write_f32(g + 0x14, gearbox.unknown_2d4);
        write_u32(g + 0x18, gearbox.stored_forward_gears);
        write_f32(g + 0x1C, gearbox.unknown_2dc);
        for (std::size_t slot = 0; slot < kVehicleGearSlots; ++slot) {
            write_f32(g + 0x20 + 4 * slot, gearbox.gear_ratios[slot]);
        }

        for (int which = 0; which < 2; ++which) {
            const std::size_t base = which == 0 ? kVehicleTuningEngineAOffset
                                               : kVehicleTuningEngineBOffset;
            unsigned char* e = record + level_entry(base, kVehicleTuningEngineStride, l);
            const VehicleTuningEngine& engine =
                which == 0 ? tuning.engine_a[level] : tuning.engine_b[level];
            write_f32(e + 0x00, engine.idle_rpm_delta);
            write_f32(e + 0x04, engine.redline_rpm_delta);
            write_f32(e + 0x08, engine.max_rpm_delta);
            write_f32(e + 0x0C, engine.unknown_30c_delta);
            for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
                write_f32(e + 0x10 + 4 * i, engine.torque_delta[i]);
            }
            for (std::size_t i = 0; i < kVehicleEngineBrakePoints; ++i) {
                write_f32(e + 0x34 + 4 * i, engine.engine_brake_delta[i]);
            }
        }

        unsigned char* ind = record + level_entry(kVehicleTuningInductionOffset,
                                                  kVehicleTuningInductionStride, l);
        for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
            write_f32(ind + 4 * i, tuning.induction[level].torque[i]);
        }

        unsigned char* t = record + level_entry(kVehicleTuningTyreOffset,
                                                kVehicleTuningTyreStride, l);
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            encode_tyre(tuning.tyres[level].axles[axle], t + kVehicleAxleStride * axle);
        }

        unsigned char* n = record + level_entry(kVehicleTuningNitrousOffset,
                                                kVehicleTuningNitrousStride, l);
        write_f32(n + 0x00, tuning.nitrous[level].capacity);
        write_u32(n + 0x04, tuning.nitrous[level].count);
        write_f32(n + 0x0C, tuning.nitrous[level].rate);

        unsigned char* a = record + level_entry(kVehicleTuningAeroAOffset,
                                                kVehicleTuningAeroStride, l);
        unsigned char* b = record + level_entry(kVehicleTuningAeroBOffset,
                                                kVehicleTuningAeroStride, l);
        unsigned char* gr = record + level_entry(kVehicleTuningGripOffset,
                                                 kVehicleTuningGripStride, l);
        for (std::size_t i = 0; i < 3; ++i) {
            write_f32(a + 4 * i, tuning.aero_a[level].downforce_delta[i]);
            write_f32(b + 4 * i, tuning.aero_b[level].downforce_delta[i]);
            write_f32(gr + 4 * i, tuning.grip[level].values[i]);
        }

        unsigned char* s = record + suspension_entry(l);
        const VehicleTuningSuspension& susp = tuning.suspension[level];
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            encode_strut(susp.axles[axle], s + kVehicleAxleStride * axle);
            write_f32(s + kVehicleAxleStride * axle + 0x1C, susp.unknown_1c[axle]);
        }
        write_f32(s + 0x40, susp.centre_of_mass_vertical_drop);
        write_f32(s + 0x44, susp.front_wheel_vertical_rise);
        write_f32(s + 0x48, susp.rear_wheel_vertical_rise);
    }

    unsigned char* item = record + kVehicleTuningItemOffset;
    write_f32(item + 0x00, tuning.item.mass_scale_bonus);
    write_f32(item + 0x04, tuning.item.rear_drive_fraction_override);
    write_f32(item + 0x08, tuning.item.nitrous_rate_bonus);
    write_f32(item + 0x0C, tuning.item.grip_block_bonus);
    for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
        write_f32(item + 0x10 + 4 * axle, tuning.item.tyre_static_friction_bonus[axle]);
        write_f32(item + 0x18 + 4 * axle, tuning.item.tyre_sliding_friction_bonus[axle]);
    }
    for (std::size_t curve = 0; curve < kVehicleTuningItemCurves; ++curve) {
        unsigned char* c = record + kVehicleTuningItemCurveOffset +
                           kVehicleTuningItemCurveStride * curve;
        for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
            write_f32(c + 4 * i, tuning.item_torque[curve][i]);
        }
    }
}

std::size_t compare_vehicle_tuning(const VehicleTuning& a,
                                  const VehicleTuning& b) noexcept {
    std::array<unsigned char, kVehicleRecordSize> left{};
    std::array<unsigned char, kVehicleRecordSize> right{};
    encode_vehicle_tuning(a, left.data(), left.size());
    encode_vehicle_tuning(b, right.data(), right.size());
    for (std::size_t offset = 0; offset < kVehicleRecordSize; ++offset) {
        if (left[offset] != right[offset]) {
            return offset & ~static_cast<std::size_t>(3);
        }
    }
    return kVehicleRecordSize;
}

// ---------------------------------------------------------------------------
// Applying the upgrades, as 0x00575960 does
// ---------------------------------------------------------------------------

namespace {

bool level_ok(int level) noexcept { return level >= 0 && level <= 3; }

// The sub-object's own offsets, to keep the reproduction readable: a field
// the game writes at RideInfo +0x60 + X is record +0x110 + X.
unsigned char* sub(unsigned char* record, std::size_t sub_offset) noexcept {
    return record + kVehicleSubObjectOffset + sub_offset;
}

void add_f32(unsigned char* target, float addend) noexcept {
    write_f32(target, static_cast<float>(static_cast<double>(read_f32(target)) +
                                         static_cast<double>(addend)));
}

} // namespace

bool apply_vehicle_tuning(const unsigned char* record,
                          std::size_t size,
                          const VehicleUpgradeLevels& levels,
                          unsigned char* out_record,
                          std::size_t out_size,
                          std::vector<VehicleTuningIssue>& issues) {
    issues.clear();
    if (record == nullptr || out_record == nullptr) {
        fail(issues, kVehicleRecordSize, "no record");
        return false;
    }
    if (size != kVehicleRecordSize || out_size != kVehicleRecordSize) {
        fail(issues, kVehicleRecordSize,
             "a CarTypeInfo record is exactly 0xC90 bytes");
        return false;
    }
    const int level_values[] = {levels.weight,  levels.suspension, levels.engine_a,
                                levels.induction, levels.nitrous,  levels.engine_b,
                                levels.gearbox, levels.tyres,      levels.grip,
                                levels.aero_a,  levels.aero_b};
    for (int value : level_values) {
        if (!level_ok(value)) {
            fail(issues, kVehicleRecordSize,
                 "an upgrade level is outside 0..3");
            return false;
        }
    }

    VehicleTuning tuning;
    std::vector<VehicleTuningIssue> tuning_issues;
    if (!decode_vehicle_tuning(record, size, tuning, tuning_issues)) {
        issues = tuning_issues;
        return false;
    }
    VehicleHandling handling;
    std::vector<VehicleHandlingIssue> handling_issues;
    if (!decode_vehicle_handling(record, size, handling, handling_issues)) {
        for (const VehicleHandlingIssue& issue : handling_issues) {
            fail(issues, issue.offset, issue.message.c_str());
        }
        return false;
    }

    // 0x0057596C: the per-instance copy of the setup sub-object. Everything
    // below rewrites that copy; the tables are read from the record.
    std::memcpy(out_record, record, kVehicleRecordSize);

    // 0x00575A10, the suspension table.
    if (levels.suspension >= 1) {
        const VehicleTuningSuspension& susp =
            tuning.suspension[static_cast<std::size_t>(levels.suspension) - 1];
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            unsigned char* block = sub(out_record, 0x170 + kVehicleAxleStride * axle);
            encode_strut(susp.axles[axle], block);
            write_f32(block + 0x1C, susp.unknown_1c[axle]);
        }
        // 0x00575A4A..0x00575A70: wheels 0 and 1 take the front rise, 2 and
        // 3 the rear one, each on the wheel record's +0x08.
        for (std::size_t wheel = 0; wheel < kVehicleWheelCount; ++wheel) {
            const float rise = wheel < 2 ? susp.front_wheel_vertical_rise
                                         : susp.rear_wheel_vertical_rise;
            add_f32(sub(out_record, 0x10 + kVehicleWheelStride * wheel + 0x08), rise);
        }
        // 0x00575A76: the centre of mass's vertical, sub +0x08.
        add_f32(sub(out_record, 0x08), -susp.centre_of_mass_vertical_drop);
    }

    // 0x00575A90, the tyre table and the friction item.
    if (levels.tyres >= 1) {
        const VehicleTuningTyres& tyres =
            tuning.tyres[static_cast<std::size_t>(levels.tyres) - 1];
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            encode_tyre(tyres.axles[axle],
                        sub(out_record, 0xD0 + kVehicleAxleStride * axle));
        }
    }
    if (levels.tyre_friction_item) {
        for (std::size_t axle = 0; axle < kVehicleTuningAxles; ++axle) {
            unsigned char* block = sub(out_record, 0xD0 + kVehicleAxleStride * axle);
            add_f32(block + 0x0C, tuning.item.tyre_static_friction_bonus[axle]);
            add_f32(block + 0x10, tuning.item.tyre_sliding_friction_bonus[axle]);
        }
    }

    // 0x00575B30, the two downforce tables: the +0x7E0 one first.
    for (int which = 0; which < 2; ++which) {
        const int level = which == 0 ? levels.aero_b : levels.aero_a;
        if (level < 1) {
            continue;
        }
        const VehicleTuningAero& aero =
            which == 0 ? tuning.aero_b[static_cast<std::size_t>(level) - 1]
                       : tuning.aero_a[static_cast<std::size_t>(level) - 1];
        for (std::size_t i = 0; i < 3; ++i) {
            add_f32(sub(out_record, 0x264 + 4 * i), aero.downforce_delta[i]);
        }
    }

    // 0x005759A0..0x005759DB, the grip block and its item. The three words
    // are written in the order +0x00, +0x08, +0x04.
    if (levels.grip >= 1) {
        const VehicleTuningGrip& grip =
            tuning.grip[static_cast<std::size_t>(levels.grip) - 1];
        write_f32(sub(out_record, 0x274), grip.values[0]);
        write_f32(sub(out_record, 0x27C), grip.values[2]);
        write_f32(sub(out_record, 0x278), grip.values[1]);
    }
    if (levels.grip_item) {
        add_f32(sub(out_record, 0x274), tuning.item.grip_block_bonus);
    }

    // 0x00575BC0, the engine tables. Both are additive and stack.
    for (int which = 0; which < 2; ++which) {
        const int level = which == 0 ? levels.engine_a : levels.engine_b;
        if (level < 1) {
            continue;
        }
        const VehicleTuningEngine& engine =
            which == 0 ? tuning.engine_a[static_cast<std::size_t>(level) - 1]
                       : tuning.engine_b[static_cast<std::size_t>(level) - 1];
        add_f32(sub(out_record, 0x1F0), engine.idle_rpm_delta);
        add_f32(sub(out_record, 0x1F4), engine.redline_rpm_delta);
        add_f32(sub(out_record, 0x1F8), engine.max_rpm_delta);
        add_f32(sub(out_record, 0x1FC), engine.unknown_30c_delta);
        for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
            add_f32(sub(out_record, 0x200 + 4 * i), engine.torque_delta[i]);
        }
        for (std::size_t i = 0; i < kVehicleEngineBrakePoints; ++i) {
            add_f32(sub(out_record, 0x248 + 4 * i), engine.engine_brake_delta[i]);
        }
    }

    // 0x00575DEB: the induction curve replaces +0x334 when installed, and
    // 0x00575E11 adds +0x334 into the torque curve whether it was replaced
    // or not.
    if (levels.induction >= 1) {
        const VehicleTuningInduction& induction =
            tuning.induction[static_cast<std::size_t>(levels.induction) - 1];
        for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
            write_f32(sub(out_record, 0x224 + 4 * i), induction.torque[i]);
        }
    }
    for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
        add_f32(sub(out_record, 0x200 + 4 * i),
                read_f32(sub(out_record, 0x224 + 4 * i)));
    }

    // 0x00575EB3..0x005761F2: the three item curves, per torque point, in
    // the order curve 0 (levels +0x34), curve 1 (+0x40), curve 2 (+0x38);
    // curve 2 is also added into +0x334.
    const bool item_on[kVehicleTuningItemCurves] = {
        levels.torque_item_a, levels.torque_item_b, levels.torque_item_c};
    for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
        for (std::size_t curve = 0; curve < kVehicleTuningItemCurves; ++curve) {
            if (!item_on[curve]) {
                continue;
            }
            const float point = tuning.item_torque[curve][i];
            add_f32(sub(out_record, 0x200 + 4 * i), point);
            if (curve == 2) {
                add_f32(sub(out_record, 0x224 + 4 * i), point);
            }
        }
    }

    // 0x005761F8, the nitrous table, and 0x00576233, its item.
    if (levels.nitrous >= 1) {
        const VehicleTuningNitrous& nitrous =
            tuning.nitrous[static_cast<std::size_t>(levels.nitrous) - 1];
        write_f32(sub(out_record, 0x2A0), nitrous.capacity);
        write_u32(sub(out_record, 0x2A4), nitrous.count);
        write_u32(sub(out_record, 0x2A8), 0);
        write_f32(sub(out_record, 0x2AC), nitrous.rate);
    }
    if (levels.nitrous_item) {
        add_f32(sub(out_record, 0x2AC), tuning.item.nitrous_rate_bonus);
    }

    // 0x00576360, the gearbox table and the drive-split item.
    if (levels.gearbox >= 1) {
        const VehicleTuningGearbox& gearbox =
            tuning.gearbox[static_cast<std::size_t>(levels.gearbox) - 1];
        write_f32(sub(out_record, 0x1B0), gearbox.shift_fraction);
        write_f32(sub(out_record, 0x1B4), gearbox.unknown_2c4);
        write_f32(sub(out_record, 0x1B8), gearbox.final_drive_rear);
        write_f32(sub(out_record, 0x1BC), gearbox.final_drive_front);
        write_f32(sub(out_record, 0x1C0), gearbox.rear_drive_fraction);
        write_f32(sub(out_record, 0x1C4), gearbox.unknown_2d4);
        write_f32(sub(out_record, 0x1CC), gearbox.unknown_2dc);
        for (std::size_t slot = 0; slot < kVehicleGearSlots; ++slot) {
            write_f32(sub(out_record, 0x1D0 + 4 * slot), gearbox.gear_ratios[slot]);
        }
        // 0x005763E1 writes zero over the table's own stored count in the
        // loaded record; this reproduction writes it into out_record's copy
        // of the table rather than into the caller's bytes.
        write_u32(out_record + level_entry(kVehicleTuningGearboxOffset,
                                           kVehicleTuningGearboxStride,
                                           levels.gearbox) + 0x18, 0);
        write_u32(sub(out_record, 0x1C8),
                  vehicle_derived_forward_gears(gearbox.gear_ratios));
    }
    if (levels.drive_split_item) {
        write_f32(sub(out_record, 0x1C0), tuning.item.rear_drive_fraction_override);
    }

    // 0x00576250, the weight table, its item and the inertia rebuild. The
    // mass scale starts at 1.0 (0x006CC7BC) when no weight table is
    // installed, so the item alone still takes 0.015 off the mass.
    float mass_scale = 1.0F;
    if (levels.weight >= 1) {
        const VehicleTuningWeight& weight =
            tuning.weight[static_cast<std::size_t>(levels.weight) - 1];
        mass_scale = weight.mass_scale;
        add_f32(sub(out_record, 0x00), weight.centre_of_mass_longitudinal_delta);
        write_f32(sub(out_record, 0x160), weight.yaw_inertia_scale);
    }
    double scale = static_cast<double>(mass_scale);
    if (levels.mass_item) {
        scale -= static_cast<double>(tuning.item.mass_scale_bonus);
    }
    const float mass = static_cast<float>(
        scale * static_cast<double>(read_f32(sub(out_record, 0x110))));
    write_f32(sub(out_record, 0x110), mass);

    // 0x0057628B..0x005762F6: the 4x4 matrix at sub +0x120 is set to the
    // identity, then 0x005762FC overwrites its three leading diagonal
    // entries with the box inertia.
    for (std::size_t i = 0; i < 16; ++i) {
        const bool diagonal = (i % 5) == 0;
        write_f32(sub(out_record, 0x120 + 4 * i), diagonal ? 1.0F : 0.0F);
    }
    const std::array<float, 3> inertia =
        vehicle_box_inertia(mass,
                            read_f32(sub(out_record, 0x114)),
                            read_f32(sub(out_record, 0x118)),
                            read_f32(sub(out_record, 0x11C)));
    write_f32(sub(out_record, 0x120), inertia[0]);
    write_f32(sub(out_record, 0x134), inertia[1]);
    write_f32(sub(out_record, 0x148), inertia[2]);

    return true;
}

// ---------------------------------------------------------------------------
// The 240SX's tables. Generated from record 18 of the retail
// Global/GLOBALB.BUN; see the header for the rule this is kept under.
// ---------------------------------------------------------------------------

#include "vehicle_tuning_240sx.inc"

} // namespace nfsu
