#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nfsu/vehicle.h"

namespace nfsu {

// ---------------------------------------------------------------------------
// The per-level tuning tables of one CarTypeInfo record
// ---------------------------------------------------------------------------
//
// docs/VEHICLE_TUNING.md. docs/VEHICLE.md decodes the stock setup sub-object
// at record +0x110 (0x2B0 bytes); the rest of the record, from +0x3C0 to
// +0x9BB, is the upgrade data that 0x00575960 folds into the per-instance
// copy of that sub-object. Every offset below is relative to the start of a
// 0xC90-byte record, and every code address is a virtual address of the
// retail speed.exe (SHA-256 C0FAD450...074B7).
//
// 0x00575960 is the whole rule: it copies 0xAC dwords from record +0x110 into
// RideInfo +0x60 (0x0057596C) and then calls, in this order,
//   0x00575A10  the suspension table
//   0x00575A90  the tyre table and the tyre-friction item
//   0x00575B30  the two downforce tables
//   inline      the grip block table and its item (0x005759A0..0x005759DB)
//   0x00575BC0  the engine tables, the induction curve, the three item
//               curves and the nitrous table
//   0x00576360  the gearbox table and the drive-split item
//   0x00576250  the weight table, the mass item and the inertia rebuild
// The callees address the record through the same pointer the copy read from,
// record + 0x110, so an offset they compute as `source + X` is record
// +0x110 + X; the constants below are record offsets.
//
// A level is gated to 1..3 by every table (`test`/`jle` then `cmp 4`/`jge`),
// so level 0 means "not installed" and the table is not read at all. An item
// is a separate boolean field of the same levels object.

constexpr std::size_t kVehicleTuningLevels = 3;
constexpr std::size_t kVehicleTuningAxles = 2;
constexpr std::size_t kVehicleTuningItemCurves = 3;

// The setup sub-object 0x00575960 copies per car instance.
constexpr std::size_t kVehicleSubObjectOffset = 0x110;
constexpr std::size_t kVehicleSubObjectSize = 0x2B0;

// Table bases and strides. Each table holds kVehicleTuningLevels entries for
// levels 1, 2 and 3; the suspension table is the one the game indexes by the
// level itself, so its entry for level L is at base + 0x50 * L and the slot
// at base + 0 is not part of it (the grip block table occupies those bytes).
constexpr std::size_t kVehicleTuningWeightOffset = 0x3C0;
constexpr std::size_t kVehicleTuningWeightStride = 0x10;
constexpr std::size_t kVehicleTuningGearboxOffset = 0x3F0;
constexpr std::size_t kVehicleTuningGearboxStride = 0x40;
constexpr std::size_t kVehicleTuningEngineAOffset = 0x4B0;
constexpr std::size_t kVehicleTuningEngineBOffset = 0x570;
constexpr std::size_t kVehicleTuningEngineStride = 0x40;
constexpr std::size_t kVehicleTuningInductionOffset = 0x630;
constexpr std::size_t kVehicleTuningInductionStride = 0x30;
constexpr std::size_t kVehicleTuningTyreOffset = 0x6C0;
constexpr std::size_t kVehicleTuningTyreStride = 0x40;
constexpr std::size_t kVehicleTuningNitrousOffset = 0x780;
constexpr std::size_t kVehicleTuningNitrousStride = 0x10;
constexpr std::size_t kVehicleTuningAeroAOffset = 0x7B0;
constexpr std::size_t kVehicleTuningAeroBOffset = 0x7E0;
constexpr std::size_t kVehicleTuningAeroStride = 0x10;
constexpr std::size_t kVehicleTuningGripOffset = 0x810;
constexpr std::size_t kVehicleTuningGripStride = 0x10;
constexpr std::size_t kVehicleTuningSuspensionBase = 0x7F0;
constexpr std::size_t kVehicleTuningSuspensionStride = 0x50;
constexpr std::size_t kVehicleTuningItemOffset = 0x930;
constexpr std::size_t kVehicleTuningItemCurveOffset = 0x950;
constexpr std::size_t kVehicleTuningItemCurveStride = 0x24;
// One past the last byte any of the tables above holds.
constexpr std::size_t kVehicleTuningEnd = 0x9BC;

// Fields of the setup sub-object the tables above write, beyond the ones
// docs/VEHICLE.md already names.
//
// +0x270 (sub +0x160) multiplies the yaw moment of the second inverse
// inertia the rigid body builds at 0x00584B42; the weight table replaces it.
constexpr std::size_t kVehicleYawInertiaScaleOffset = 0x270;
// +0x30C (sub +0x1FC): not read anywhere this project traced; both engine
// tables carry a delta for it.
constexpr std::size_t kVehicleUnknown30cOffset = 0x30C;
// +0x334 (sub +0x224): a second nine-point torque curve over the same
// [idle, max] grid as +0x310, which 0x00575E11..0x00575EAD adds into +0x310
// unconditionally. Zero in every retail record but the Eclipse, the RX-7 and
// the Skyline; the induction table replaces it.
constexpr std::size_t kVehicleInductionTorqueOffset = 0x334;
// +0x374, +0x378, +0x37C (sub +0x264..+0x26C): the block 0x00456969 averages
// the first two into a downforce-like term and the strut preload's total
// scales by the third (docs/VEHICLE.md). Both aero tables add to all three.
constexpr std::size_t kVehicleDownforceOffset = 0x374;
// +0x384, +0x388, +0x38C (sub +0x274..+0x27C): three floats no traced
// instruction reads. The grip table replaces them and one item adds to the
// first. Stock 240SX (0.4, 0.5, 0.5).
constexpr std::size_t kVehicleGripBlockOffset = 0x384;
// +0x3B0 (sub +0x2A0): four words, zero in every retail record, which the
// nitrous table writes: a float, a count, a reserved zero word and a rate.
constexpr std::size_t kVehicleNitrousBlockOffset = 0x3B0;

// +0x3C0 + 0x10 * (level - 1), read at 0x00576250 (sub +0x2A0 + 0x10 * level).
struct VehicleTuningWeight {
    // +0x00: multiplies the mass at +0x220 (0x0057628B). The item at
    // levels +0x2C subtracts VehicleTuningItems::mass_scale_bonus from it
    // first (0x00576285).
    float mass_scale = 1.0F;
    // +0x04: added to the centre of mass's longitudinal component, +0x110
    // (0x00576270..0x00576275).
    float centre_of_mass_longitudinal_delta = 0.0F;
    // +0x08: replaces +0x270 (0x00576277..0x0057627A).
    float yaw_inertia_scale = 0.0F;
    // +0x0C is zero in all 35 retail records and is not read.
};

// +0x3F0 + 0x40 * (level - 1), read at 0x00576360. The entry has the layout
// of the stock block at +0x2C0..+0x2FF and replaces it word for word, with
// one exception named on stored_forward_gears.
struct VehicleTuningGearbox {
    float shift_fraction = 0.0F;   // +0x00 -> +0x2C0
    float unknown_2c4 = 0.0F;      // +0x04 -> +0x2C4, 500 in every entry
    float final_drive_rear = 0.0F; // +0x08 -> +0x2C8
    float final_drive_front = 0.0F;// +0x0C -> +0x2CC
    float rear_drive_fraction = 0.0F; // +0x10 -> +0x2D0
    float unknown_2d4 = 0.0F;      // +0x14 -> +0x2D4
    // +0x18. The game does not copy it: 0x005763E1 writes zero over it in
    // the loaded record and 0x005763F0..0x0057640D counts the gear slots 2
    // to 7 whose ratio is above 0.0 (0x006CC7A4) into +0x2D8 instead. In
    // every retail entry the stored value equals that count.
    std::uint32_t stored_forward_gears = 0;
    float unknown_2dc = 0.0F;      // +0x1C -> +0x2DC
    // +0x20 + 4 * slot -> +0x2E0: reverse, neutral, 1st .. 6th.
    std::array<float, kVehicleGearSlots> gear_ratios{};
};

// +0x4B0 and +0x570, each + 0x40 * (level - 1), read at 0x00575BDE and
// 0x00575CF0. Every field is added to its target, so two installed engine
// tables stack.
struct VehicleTuningEngine {
    float idle_rpm_delta = 0.0F;     // +0x00 -> +0x300
    float redline_rpm_delta = 0.0F;  // +0x04 -> +0x304
    float max_rpm_delta = 0.0F;      // +0x08 -> +0x308
    float unknown_30c_delta = 0.0F;  // +0x0C -> +0x30C
    // +0x10 -> +0x310, kN m at the same nine grid points.
    std::array<float, kVehicleTorquePoints> torque_delta{};
    // +0x34 -> +0x358, kN m.
    std::array<float, kVehicleEngineBrakePoints> engine_brake_delta{};
};

// +0x630 + 0x30 * (level - 1), read at 0x00575DFD: nine floats copied over
// +0x334, which is then added into the torque curve. +0x24, +0x28 and +0x2C
// of the entry are zero in all 35 retail records and are not read.
struct VehicleTuningInduction {
    std::array<float, kVehicleTorquePoints> torque{};
};

// +0x6C0 + 0x40 * (level - 1), read at 0x00575AAB: two 0x20-byte blocks with
// the layout of the stock tyre blocks at +0x1E0 and +0x200, front then rear,
// each replacing its block. The three words after each block's +0x10 are
// zero in all 35 retail records.
struct VehicleTuningTyres {
    std::array<VehicleAxleTyre, kVehicleTuningAxles> axles{};
};

// +0x780 + 0x10 * (level - 1), read at 0x0057620A: four words copied over
// +0x3B0. Identical in all 35 retail records: (2, 2, 0.5), (4, 4, 0.5) and
// (6, 6, 0.5). The item at levels +0x3C adds
// VehicleTuningItems::nitrous_rate_bonus to `rate` (0x00576233).
struct VehicleTuningNitrous {
    float capacity = 0.0F;         // +0x00
    std::uint32_t count = 0;       // +0x04
    // +0x08 is zero in all 35 retail records and is not read.
    float rate = 0.0F;             // +0x0C
};

// +0x7B0 and +0x7E0, each + 0x10 * (level - 1), read at 0x00575B83 and
// 0x00575B42: three floats added to +0x374, +0x378 and +0x37C. +0x0C of each
// entry is zero in all 35 retail records. The game applies the +0x7E0 table
// first.
struct VehicleTuningAero {
    std::array<float, 3> downforce_delta{};
};

// +0x810 + 0x10 * (level - 1), read at 0x005759AB: three floats copied over
// +0x384, +0x388 and +0x38C. +0x0C of each entry is zero in all 35 retail
// records. The item at levels +0x4C adds
// VehicleTuningItems::grip_block_bonus to the first (0x005759CF).
struct VehicleTuningGrip {
    std::array<float, 3> values{};
};

// +0x7F0 + 0x50 * level, read at 0x00575A1C. The two 0x20-byte blocks have
// the layout of the stock strut blocks at +0x280 and +0x2A0 and replace them;
// the three deltas after them lower the car. +0x4C of each entry is zero in
// all 35 retail records.
struct VehicleTuningSuspension {
    // Each block's eighth float, +0x1C, is not a field of
    // VehicleAxleStrut; it equals bump_limit in every stock block and in
    // every entry of this table, and no reader of it was traced.
    std::array<VehicleAxleStrut, kVehicleTuningAxles> axles{};
    std::array<float, kVehicleTuningAxles> bump_limit_copy{};
    // +0x40: subtracted from the centre of mass's vertical component,
    // +0x118 (0x00575A76..0x00575A7C).
    float centre_of_mass_vertical_drop = 0.0F;
    // +0x44: added to the vertical of wheels 0 and 1 (0x00575A4A,
    // 0x00575A53), that is +0x128 and +0x158.
    float front_wheel_vertical_rise = 0.0F;
    // +0x48: the same for wheels 2 and 3 (0x00575A5C, 0x00575A67).
    float rear_wheel_vertical_rise = 0.0F;
};

// +0x930: eight floats the boolean items use. Every one of them is the same
// in all 35 retail records except rear_drive_fraction_override and
// grip_block_bonus, which runs from 0.04 to 0.08 over the 20 racers.
struct VehicleTuningItems {
    float mass_scale_bonus = 0.0F;            // +0x930, 0.015 in every record
    // +0x934. Only three values appear in the retail records, and each sits
    // just outside a threshold of the drive-type classifier at 0x0047517E,
    // which splits at 0.2 and 0.8: 0.199 (front), 0.6 (four) and 0.801
    // (rear). Nineteen of the twenty racers get the value of the class their
    // stock +0x2D0 already puts them in; the Peugeot is front-wheel drive in
    // stock, with +0x2D0 zero, and holds 0.801 here. Every traffic vehicle
    // holds 0.199 where its stock +0x2D0 is 0.5, except the bus, whose stock
    // is 0.4.
    float rear_drive_fraction_override = 0.0F;
    float nitrous_rate_bonus = 0.0F;          // +0x938, 0.125 in every record
    float grip_block_bonus = 0.0F;            // +0x93C
    // +0x940 front, +0x944 rear: added to each tyre block's +0x0C.
    std::array<float, kVehicleTuningAxles> tyre_static_friction_bonus{};
    // +0x948 front, +0x94C rear: added to each tyre block's +0x10.
    std::array<float, kVehicleTuningAxles> tyre_sliding_friction_bonus{};
};

// One 0xC90-byte record's tuning tables.
struct VehicleTuning {
    std::array<VehicleTuningWeight, kVehicleTuningLevels> weight{};
    std::array<VehicleTuningGearbox, kVehicleTuningLevels> gearbox{};
    std::array<VehicleTuningEngine, kVehicleTuningLevels> engine_a{};
    std::array<VehicleTuningEngine, kVehicleTuningLevels> engine_b{};
    std::array<VehicleTuningInduction, kVehicleTuningLevels> induction{};
    std::array<VehicleTuningTyres, kVehicleTuningLevels> tyres{};
    std::array<VehicleTuningNitrous, kVehicleTuningLevels> nitrous{};
    std::array<VehicleTuningAero, kVehicleTuningLevels> aero_a{};
    std::array<VehicleTuningAero, kVehicleTuningLevels> aero_b{};
    std::array<VehicleTuningGrip, kVehicleTuningLevels> grip{};
    std::array<VehicleTuningSuspension, kVehicleTuningLevels> suspension{};
    VehicleTuningItems item{};
    // +0x950, +0x974, +0x998: one nine-point torque curve per boolean item,
    // in the order the three items are applied (levels +0x34, +0x40, +0x38).
    std::array<std::array<float, kVehicleTorquePoints>, kVehicleTuningItemCurves>
        item_torque{};
};

// The installed upgrades, as the levels object 0x00575960 is given holds
// them. The field names are this project's; the offsets are the object's own
// and are what the code reads. A level of 0 means nothing is installed, and
// the game itself ignores any value outside 1..3; decode and apply below
// refuse one instead of ignoring it, because a tool that is handed 7 has
// been handed nonsense.
struct VehicleUpgradeLevels {
    int weight = 0;      // +0x00, the weight table
    int suspension = 0;  // +0x04
    int engine_a = 0;    // +0x08
    int induction = 0;   // +0x0C
    int nitrous = 0;     // +0x10
    int engine_b = 0;    // +0x14
    int gearbox = 0;     // +0x18
    int tyres = 0;       // +0x1C
    int grip = 0;        // +0x20
    int aero_a = 0;      // +0x24, the +0x7B0 table
    int aero_b = 0;      // +0x28, the +0x7E0 table
    bool mass_item = false;         // +0x2C
    bool torque_item_a = false;     // +0x34, item curve 0
    bool torque_item_c = false;     // +0x38, item curve 2
    bool nitrous_item = false;      // +0x3C
    bool torque_item_b = false;     // +0x40, item curve 1
    bool drive_split_item = false;  // +0x44
    bool tyre_friction_item = false;// +0x48
    bool grip_item = false;         // +0x4C
};

struct VehicleTuningIssue {
    // Offset of the offending field inside the record, or
    // kVehicleRecordSize when the issue is about the whole record or about
    // a level rather than a field.
    std::size_t offset = 0;
    std::string message;
};

// Decodes the tables above out of one CarTypeInfo record, little-endian.
// `size` must be exactly kVehicleRecordSize. Never throws; returns false and
// fills `issues` on any refusal. What it refuses: a size other than the
// record size, a null record, any non-finite float, a word the retail
// records keep at zero that is not zero, a mass scale outside (0, 2], a
// non-positive yaw inertia scale, a gear table whose neutral slot is not
// zero or whose reverse ratio is not negative or whose forward ratios are
// not non-negative and non-increasing from first gear, a stored gear count
// that does not equal the count the game derives, a tyre block with a
// non-positive slip scale or friction coefficient, a strut block with a
// non-positive spring rate or with droop_limit >= 0 >= bump_limit, a
// nitrous entry with a non-positive capacity or a count outside 1..64 or a
// count that does not equal its capacity, and a friction or mass item
// scalar that is negative.
bool decode_vehicle_tuning(const unsigned char* record,
                           std::size_t size,
                           VehicleTuning& out,
                           std::vector<VehicleTuningIssue>& issues);

// The same checks on a table built in memory.
bool check_vehicle_tuning(const VehicleTuning& tuning,
                          std::vector<VehicleTuningIssue>& issues);

// Writes the tables above into a record image at their offsets and leaves
// every other byte untouched: the inverse of decode_vehicle_tuning, for
// fixtures. The bytes it writes are kVehicleTuningRecordBytes of them, in
// kVehicleTuningRecordRuns runs from +0x3C0 to +0x9BB.
void encode_vehicle_tuning(const VehicleTuning& tuning,
                           unsigned char* record,
                           std::size_t size) noexcept;

// Field-by-field comparison of the stored bits. Returns the record offset of
// the first differing field, or kVehicleRecordSize when they are equal.
std::size_t compare_vehicle_tuning(const VehicleTuning& a,
                                   const VehicleTuning& b) noexcept;

// Reproduces 0x00575960 over one record: copies the whole record into
// `out_record` and rewrites its setup sub-object, +0x110..+0x3BF, exactly as
// the game rewrites the per-instance copy, in the order and with the
// arithmetic of the seven callees named at the top of this header. The
// inertia rebuild at 0x005762FC runs whether or not anything is installed,
// as it does in the game, so applying an all-zero VehicleUpgradeLevels is
// not the identity: it replaces the inertia matrix at +0x230 with the box
// inertia of the mass, length, width and height and adds +0x334 into the
// torque curve. `size` and `out_size` must both be kVehicleRecordSize.
// Never throws; returns false and fills `issues` on a refusal, and then
// leaves `out_record` untouched.
bool apply_vehicle_tuning(const unsigned char* record,
                          std::size_t size,
                          const VehicleUpgradeLevels& levels,
                          unsigned char* out_record,
                          std::size_t out_size,
                          std::vector<VehicleTuningIssue>& issues);

// The box inertia rule of 0x005762FC..0x00576352, in the record's units
// (tonnes, metres, tonnes m^2), with the constant 1/12 at 0x006CCA6C as the
// float the game holds there:
//   roll  = mass * (width^2 + height^2) / 12
//   pitch = mass * (height^2 + length^2) / 12
//   yaw   = mass * (width^2 + length^2) / 12
// The game runs it on every car instance, which is why the diagonal of every
// retail record's +0x230 matrix already is this (docs/VEHICLE.md measured
// that and could not say why).
std::array<float, 3> vehicle_box_inertia(float mass,
                                         float length,
                                         float width,
                                         float height) noexcept;

// The 240SX's tuning tables, record 18 of chunk 0x00034600 in the retail
// Global/GLOBALB.BUN (SHA-256 39DF38FA...8DB9) at absolute file offset
// 1736752. A small numeric table from a retail file, kept in the source
// under the owner's rule of PR #169 comment 3860, as stock_240sx_handling()
// is: it states its source and the SHA-256 of the bytes it reproduces, and
// nfsu_vehicle_tuning_tests rebuilds those bytes from it. It reproduces
// kVehicleTuningRecordBytes of the record's 3216 bytes and holds nothing
// else; the whole record hashes to
// 212F69AD8E739250F95E04C54FB6613A39A7C76014CB402DF966F64B5D9FFF9B.
VehicleTuning stock_240sx_tuning();
constexpr std::size_t kVehicleTuningRecordBytes = 1352;
constexpr std::size_t kVehicleTuningRecordRuns = 28;
constexpr const char* kVehicle240sxTuningBytesSha256 =
    "0FB2DD15F45173378F4917E02541B97AADC84DEFF62E4DD4F3601618A18C810B";

} // namespace nfsu
