#include "nfsu/part_placement.h"

#include "nfsu/texture.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

// A fixture name no other process shares, built the way
// tests/arg_path_tests.cpp has built one since issue #87 and
// tests/track_object_tests.cpp since issue #122.
//
// test_load_file used to build its fixture under the system temporary
// directory at "nfsu_part_placement_tests", with nothing in the name to
// make it this process's own, and it opens that directory with
// fs::remove_all before creating it. CLAUDE.md says several sessions are
// routinely live on this machine, each with its own build directory, so two
// copies of this binary overlap whenever two of them run ctest at the same
// time - and the remove_all at the top of this test deleted the other
// copy's fixture out from under it. Issue #124 measured that: 48 of 360
// contended runs failed here, on "load from file", "missing file" and the
// file-limit diagnostic in turn.
std::string unique_fixture(const std::string& label) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return "nfsu_part_placement_" + label + "_" + std::to_string(pid);
}

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

void check_close(float actual, float expected, const std::string& message) {
    if (!(std::fabs(actual - expected) <= 1e-6F)) {
        std::cerr << "test failure: " << message << " (expected " << expected
                  << ", got " << actual << ")\n";
        ++g_failures;
    }
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8U * i)) & 0xFFU));
    }
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void put_f32(Bytes& bytes, std::size_t offset, float value) {
    put_u32(bytes, offset, float_bits(value));
}

// One synthetic car type, with the fields the parser decodes.
struct TestWheel {
    float longitudinal = 0.0F;
    float lateral = 0.0F;
    float vertical = 0.0F;
    float radius = 0.3F;
    float value_14 = 0.2F;
    float value_1c = 0.8F;
    float value_20 = 0.9F;
};

struct TestCar {
    std::string name = "CAR";
    std::string lookup_name = "CAR";
    std::string geometry_path = "CARS\\CAR\\GEOMETRY.BIN";
    std::string compressed_path = "CARS\\CAR\\GEOMETRY.LZC";
    std::string brand_name = "BRAND";
    std::uint32_t class_id = 0;
    std::array<TestWheel, nfsu::kCarWheelCount> wheels{};
};

TestCar default_car(const std::string& name) {
    TestCar car;
    car.name = name;
    car.lookup_name = name;
    car.geometry_path = "CARS\\" + name + "\\GEOMETRY.BIN";
    car.compressed_path = "CARS\\" + name + "\\GEOMETRY.LZC";
    // Front left, front right, rear right, rear left, as the retail table is
    // ordered. Positive lateral is the car's left.
    car.wheels[0] = TestWheel{1.25F, 0.75F, 0.10F, 0.30F, 0.20F, 0.80F, 0.90F};
    car.wheels[1] = TestWheel{1.25F, -0.75F, 0.10F, 0.30F, 0.20F, -0.80F, -0.90F};
    car.wheels[2] = TestWheel{-1.25F, -0.75F, 0.10F, 0.32F, 0.21F, -0.80F, -0.90F};
    car.wheels[3] = TestWheel{-1.25F, 0.75F, 0.10F, 0.32F, 0.21F, 0.80F, 0.90F};
    return car;
}

void write_string_field(Bytes& bytes, std::size_t offset, const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[offset + i] = static_cast<unsigned char>(text[i]);
    }
}

Bytes build_record(const TestCar& car) {
    Bytes record(nfsu::kCarTypeInfoRecordSize, 0);
    write_string_field(record, nfsu::kCarTypeNameOffset, car.name);
    write_string_field(record, nfsu::kCarTypeLookupNameOffset, car.lookup_name);
    write_string_field(record, nfsu::kCarTypeGeometryPathOffset, car.geometry_path);
    write_string_field(record, nfsu::kCarTypeCompressedPathOffset, car.compressed_path);
    write_string_field(record, nfsu::kCarTypeBrandNameOffset, car.brand_name);
    put_u32(record, nfsu::kCarTypeClassOffset, car.class_id);
    for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
        const std::size_t base =
            nfsu::kCarWheelTableOffset + w * nfsu::kCarWheelRecordSize;
        put_f32(record, base + 0x00, car.wheels[w].longitudinal);
        put_f32(record, base + 0x04, car.wheels[w].lateral);
        put_f32(record, base + 0x08, car.wheels[w].vertical);
        put_f32(record, base + 0x10, car.wheels[w].radius);
        put_f32(record, base + 0x14, car.wheels[w].value_14);
        put_u32(record, base + 0x18, static_cast<std::uint32_t>(w));
        put_f32(record, base + 0x1C, car.wheels[w].value_1c);
        put_f32(record, base + 0x20, car.wheels[w].value_20);
    }
    return record;
}

struct Layout {
    Bytes bytes;
    // Offset of the 0x00034600 chunk header, of the aligned record array and
    // of record 0, so a test can mutate one byte precisely.
    std::size_t chunk = 0;
    std::size_t records = 0;
};

// Builds a file shaped like GLOBALB.BUN: a leading padding chunk, then the
// car type chunk whose payload is 0x11 filler up to a 16-byte boundary and
// then the fixed number of records.
// The default leading padding leaves the payload 8 bytes before a 16-byte
// boundary, which is exactly the retail shape: GLOBALB.BUN has 8 filler
// bytes before its record array.
Layout build_table(const std::vector<TestCar>& cars,
                   std::size_t leading_padding = 8,
                   std::size_t record_count = nfsu::kCarTypeInfoRecordCount) {
    Layout layout;
    Bytes& out = layout.bytes;
    append_u32(out, 0);
    append_u32(out, static_cast<std::uint32_t>(leading_padding));
    out.insert(out.end(), leading_padding, 0);

    layout.chunk = out.size();
    append_u32(out, nfsu::kCarTypeInfoId);
    append_u32(out, 0);  // patched below
    const std::size_t payload = out.size();
    const std::size_t aligned =
        (payload + nfsu::kCarTypeInfoAlignment - 1) /
        nfsu::kCarTypeInfoAlignment * nfsu::kCarTypeInfoAlignment;
    out.insert(out.end(), aligned - payload, nfsu::kCarTypeInfoFillerByte);
    layout.records = out.size();
    for (std::size_t i = 0; i < record_count; ++i) {
        const TestCar& car = cars[i < cars.size() ? i : cars.size() - 1];
        const Bytes record = build_record(car);
        out.insert(out.end(), record.begin(), record.end());
    }
    put_u32(out, layout.chunk + 4, static_cast<std::uint32_t>(out.size() - payload));
    return layout;
}

std::vector<TestCar> default_cars() {
    std::vector<TestCar> cars;
    for (std::size_t i = 0; i < nfsu::kCarTypeInfoRecordCount; ++i) {
        cars.push_back(default_car("CAR" + std::to_string(i)));
    }
    return cars;
}

bool has_issue(const nfsu::CarTypeTable& table, const std::string& text) {
    for (const nfsu::CarTypeIssue& issue : table.issues) {
        if (issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void expect_rejected(const Bytes& bytes,
                     const std::string& text,
                     const std::string& what) {
    const nfsu::CarTypeTable table =
        nfsu::parse_car_type_table(bytes.data(), bytes.size());
    check(!table.valid, what + ": must not be valid");
    check(!table.issues.empty(), what + ": must report an issue");
    if (!has_issue(table, text)) {
        std::string seen;
        for (const nfsu::CarTypeIssue& issue : table.issues) {
            seen += (seen.empty() ? "" : " | ") + issue.message;
        }
        std::cerr << "test failure: " << what << ": expected an issue containing '"
                  << text << "', got: " << seen << '\n';
        ++g_failures;
    }
}

void test_marker_names() {
    // Every recovered name must reproduce its hash under the same bin_hash
    // the game uses, or the name is not the one the data refers to.
    const std::size_t count = nfsu::car_marker_name_count();
    check(count == 24, "recovered marker name count");
    std::uint32_t previous = 0;
    std::size_t ascending = 0;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t hash = 0;
        const char* name = nullptr;
        nfsu::CarMarkerRole role = nfsu::CarMarkerRole::unknown;
        if (!nfsu::car_marker_name_at(i, hash, name, role)) {
            check(false, "marker name entry " + std::to_string(i));
            continue;
        }
        check(name != nullptr && name[0] != '\0',
              "marker name " + std::to_string(i) + " is not empty");
        check(nfsu::bin_hash(name) == hash,
              std::string("bin_hash(\"") + name + "\") matches its table entry");
        check(role != nfsu::CarMarkerRole::unknown,
              std::string(name) + " has a role");
        check(nfsu::car_marker_name(hash) != nullptr &&
                  std::string(nfsu::car_marker_name(hash)) == name,
              std::string(name) + " resolves by hash");
        check(nfsu::car_marker_role(hash) == role,
              std::string(name) + " role resolves by hash");
        if (i > 0 && hash > previous) {
            ++ascending;
        }
        previous = hash;
    }
    // The first 22 entries are ascending; the two extras follow.
    check(ascending >= 21, "the main marker table is ascending by hash");

    std::uint32_t hash = 0;
    const char* name = nullptr;
    nfsu::CarMarkerRole role = nfsu::CarMarkerRole::unknown;
    check(!nfsu::car_marker_name_at(count, hash, name, role),
          "marker name index past the end");

    // 0x3AC3FFB1 occurs on 76 retail BASE meshes and its name is not
    // recovered; it must resolve to nothing rather than to a guess.
    check(nfsu::car_marker_name(0x3AC3FFB1U) == nullptr,
          "the unrecovered retail marker hash has no name");
    check(nfsu::car_marker_role(0x3AC3FFB1U) == nfsu::CarMarkerRole::unknown,
          "the unrecovered retail marker hash has no role");
    check(nfsu::car_marker_name(0U) == nullptr, "hash 0 has no name");

    // The two roles whose draw path was traced.
    check(nfsu::car_marker_role(nfsu::bin_hash("LEFT_EXHAUST")) ==
              nfsu::CarMarkerRole::exhaust,
          "LEFT_EXHAUST is an exhaust marker");
    check(nfsu::car_marker_role(nfsu::bin_hash("REAR_LICENSE_PLATE")) ==
              nfsu::CarMarkerRole::license_plate,
          "REAR_LICENSE_PLATE is a licence-plate marker");
    check(nfsu::car_marker_role(nfsu::bin_hash("MARKER_BRAKE")) ==
              nfsu::CarMarkerRole::wheel_brake,
          "MARKER_BRAKE is a wheel-brake marker");
    check(std::string(nfsu::car_marker_role_name(nfsu::CarMarkerRole::exhaust)) ==
              "exhaust",
          "exhaust role name");
    check(std::string(nfsu::car_marker_role_name(
              nfsu::CarMarkerRole::license_plate)) == "license-plate",
          "license-plate role name");
    check(std::string(nfsu::car_marker_role_name(nfsu::CarMarkerRole::unknown)) ==
              "unknown",
          "unknown role name");
}

void test_marker_transform() {
    float turn[16];
    nfsu::car_marker_quarter_turn(turn);
    // cos is 0 in elements 0 and 10, -sin in 2, +sin in 8, 1 in 5 and 15.
    const float expected_turn[16] = {0.0F, 0.0F, -1.0F, 0.0F,
                                     0.0F, 1.0F, 0.0F,  0.0F,
                                     1.0F, 0.0F, 0.0F,  0.0F,
                                     0.0F, 0.0F, 0.0F,  1.0F};
    for (std::size_t i = 0; i < 16; ++i) {
        check_close(turn[i], expected_turn[i],
                    "quarter turn element " + std::to_string(i));
    }

    // Multiplying by the identity must return the other operand unchanged.
    const float identity[16] = {1.0F, 0.0F, 0.0F, 0.0F,
                                0.0F, 1.0F, 0.0F, 0.0F,
                                0.0F, 0.0F, 1.0F, 0.0F,
                                0.0F, 0.0F, 0.0F, 1.0F};
    float product[16];
    nfsu::multiply_placement_matrix(identity, turn, product);
    for (std::size_t i = 0; i < 16; ++i) {
        check_close(product[i], turn[i], "identity times turn, element " +
                                             std::to_string(i));
    }
    nfsu::multiply_placement_matrix(turn, identity, product);
    for (std::size_t i = 0; i < 16; ++i) {
        check_close(product[i], turn[i], "turn times identity, element " +
                                             std::to_string(i));
    }

    // Aliasing the destination with either input must still be correct,
    // which is what the game's own multiply guarantees at 0x005AC6AE.
    {
        float expected[16];
        nfsu::multiply_placement_matrix(turn, turn, expected);
        float aliased_a[16];
        for (std::size_t i = 0; i < 16; ++i) {
            aliased_a[i] = turn[i];
        }
        nfsu::multiply_placement_matrix(aliased_a, turn, aliased_a);
        for (std::size_t i = 0; i < 16; ++i) {
            check_close(aliased_a[i], expected[i],
                        "out aliasing a, element " + std::to_string(i));
        }
        float aliased_b[16];
        for (std::size_t i = 0; i < 16; ++i) {
            aliased_b[i] = turn[i];
        }
        nfsu::multiply_placement_matrix(turn, aliased_b, aliased_b);
        for (std::size_t i = 0; i < 16; ++i) {
            check_close(aliased_b[i], expected[i],
                        "out aliasing b, element " + std::to_string(i));
        }
        float both[16];
        for (std::size_t i = 0; i < 16; ++i) {
            both[i] = turn[i];
        }
        nfsu::multiply_placement_matrix(both, both, both);
        for (std::size_t i = 0; i < 16; ++i) {
            check_close(both[i], expected[i],
                        "out aliasing both, element " + std::to_string(i));
        }
    }

    // Four quarter turns are the identity.
    float once[16];
    float twice[16];
    float thrice[16];
    float fourth[16];
    nfsu::multiply_placement_matrix(turn, turn, twice);
    nfsu::multiply_placement_matrix(twice, turn, thrice);
    nfsu::multiply_placement_matrix(thrice, turn, fourth);
    for (std::size_t i = 0; i < 16; ++i) {
        check_close(fourth[i], identity[i],
                    "four quarter turns, element " + std::to_string(i));
    }
    (void)once;

    // The retail LEFT_EXHAUST marker of 240SX_KIT15_REAR_BUMPER_A.
    nfsu::MeshMarker marker;
    marker.name_hash = nfsu::bin_hash("LEFT_EXHAUST");
    const float retail[16] = {-0.000001F, -0.000000F, 1.000000F, 0.0F,
                              0.000000F,  1.000000F,  0.000000F, 0.0F,
                              -1.000000F, 0.000000F,  -0.000000F, 0.0F,
                              -1.98311F,  0.48212F,   0.03986F,  1.0F};
    for (std::size_t i = 0; i < 16; ++i) {
        marker.matrix[i] = retail[i];
    }
    float placement[16];
    check(nfsu::car_marker_placement(marker, placement),
          "the retail exhaust marker produces a placement");
    // The quarter turn maps the mesh's own +longitudinal axis onto the car's
    // +longitudinal axis, so row 0 comes out as (1, 0, 0).
    check_close(placement[0], 1.0F, "exhaust placement row 0 longitudinal");
    check_close(placement[1], 0.0F, "exhaust placement row 0 lateral");
    check_close(placement[2], 0.0F, "exhaust placement row 0 vertical");
    // The turn has no translation, so row 3 is the marker's own.
    check_close(placement[12], -1.98311F, "exhaust placement translation 0");
    check_close(placement[13], 0.48212F, "exhaust placement translation 1");
    check_close(placement[14], 0.03986F, "exhaust placement translation 2");
    check_close(placement[15], 1.0F, "exhaust placement w");

    // A point at the mesh origin lands on the marker; a point 0.5 along the
    // mesh's own +longitudinal axis lands 0.5 ahead of it, which is what puts
    // the exhaust pipe under the car instead of standing it upright.
    const float tip[3] = {0.0F, 0.0F, 0.0F};
    const float along[3] = {0.5F, 0.0F, 0.0F};
    float out[3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        out[axis] = tip[0] * placement[axis] + tip[1] * placement[4 + axis] +
                    tip[2] * placement[8 + axis] + placement[12 + axis];
    }
    check_close(out[0], -1.98311F, "mesh origin lands on the marker");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        out[axis] = along[0] * placement[axis] + along[1] * placement[4 + axis] +
                    along[2] * placement[8 + axis] + placement[12 + axis];
    }
    check_close(out[0], -1.98311F + 0.5F, "the pipe runs forward from the tip");
    check_close(out[2], 0.03986F, "the pipe stays at the marker height");

    // A matrix that is not finite must be refused, leaving `out` untouched.
    for (std::size_t element : {std::size_t{0}, std::size_t{12},
                                std::size_t{15}}) {
        nfsu::MeshMarker bad = marker;
        bad.matrix[element] = std::numeric_limits<float>::quiet_NaN();
        float untouched[16];
        for (std::size_t i = 0; i < 16; ++i) {
            untouched[i] = -7.0F;
        }
        check(!nfsu::car_marker_placement(bad, untouched),
              "a non-finite marker element " + std::to_string(element) +
                  " is refused");
        check_close(untouched[0], -7.0F,
                    "a refused placement leaves the output untouched");
        bad.matrix[element] = std::numeric_limits<float>::infinity();
        check(!nfsu::car_marker_placement(bad, untouched),
              "an infinite marker element " + std::to_string(element) +
                  " is refused");
    }
}

void test_part_names() {
    check(std::string(nfsu::cullable_car_part_name(nfsu::CullableCarPart::tire_fl)) ==
              "CULLABLE_CAR_PART_TIRE_FL",
          "tire_fl name");
    check(std::string(nfsu::cullable_car_part_name(nfsu::CullableCarPart::tire_rl)) ==
              "CULLABLE_CAR_PART_TIRE_RL",
          "tire_rl name");
    check(std::string(nfsu::cullable_car_part_name(
              nfsu::CullableCarPart::tire_blurs_right)) ==
              "CULLABLE_CAR_PART_TIRE_BLURS_RIGHT",
          "tire_blurs_right name");
    check(std::string(nfsu::cullable_car_part_name(nfsu::CullableCarPart::shadow)) ==
              "CULLABLE_CAR_PART_SHADOW",
          "shadow name");
    check(nfsu::cullable_car_part_name(
              static_cast<nfsu::CullableCarPart>(nfsu::kCullableCarPartCount)) ==
              nullptr,
          "out-of-range part name is null");
    check(std::string(nfsu::placement_source_name(nfsu::PlacementSource::wheel)) ==
              "wheel",
          "wheel source name");
    check(std::string(nfsu::placement_source_name(
              nfsu::PlacementSource::plane_offset)) == "plane-offset",
          "plane_offset source name");
    // The 14 entries fill the 0x1C0-byte array the constructor clears.
    check(nfsu::kCullableCarPartCount * nfsu::kCullablePlacementEntrySize ==
              nfsu::kCullablePlacementArraySize,
          "placement array size");
    check(nfsu::kCullablePlacementArraySize == 0x1C0, "placement array is 0x1C0 bytes");
    // The four wheel records sit inside the block the game copies.
    check(nfsu::kCarWheelTableOffset - nfsu::kCarSetupBlockOffset +
                  nfsu::kCarWheelCount * nfsu::kCarWheelRecordSize <=
              nfsu::kCarSetupBlockSize,
          "wheel table fits in the copied setup block");
}

void test_valid_table() {
    const Layout layout = build_table(default_cars());
    const nfsu::CarTypeTable table =
        nfsu::parse_car_type_table(layout.bytes.data(), layout.bytes.size());
    check(table.ok(), "valid table parses");
    check(table.records.size() == nfsu::kCarTypeInfoRecordCount, "record count");
    check(table.chunk_offset == layout.chunk, "chunk offset");
    check(table.record_offset == layout.records, "record offset");
    if (table.records.empty()) {
        return;
    }
    const nfsu::CarTypeRecord& first = table.records.front();
    check(first.name == "CAR0", "first name");
    check(first.lookup_name == "CAR0", "first lookup name");
    check(first.geometry_path == "CARS\\CAR0\\GEOMETRY.BIN", "geometry path");
    check(first.compressed_path == "CARS\\CAR0\\GEOMETRY.LZC", "compressed path");
    check(first.brand_name == "BRAND", "brand name");
    check(first.class_id == 0, "class id");
    check(first.offset == layout.records, "record 0 offset");
    for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
        check(first.wheels[w].index == w, "wheel index " + std::to_string(w));
        check(first.wheels[w].offset ==
                  layout.records + nfsu::kCarWheelTableOffset +
                      w * nfsu::kCarWheelRecordSize,
              "wheel offset " + std::to_string(w));
    }
    check_close(first.wheels[0].longitudinal, 1.25F, "wheel 0 longitudinal");
    check_close(first.wheels[0].lateral, 0.75F, "wheel 0 lateral");
    check_close(first.wheels[0].vertical, 0.10F, "wheel 0 vertical");
    check_close(first.wheels[0].rolling_radius, 0.30F, "wheel 0 radius");
    check_close(first.wheels[2].rolling_radius, 0.32F, "wheel 2 radius");
    check(nfsu::find_car_type(table, "CAR0") == 0, "find first car type");
    check(nfsu::find_car_type(table, "CAR34") == 34, "find last car type");
    check(nfsu::find_car_type(table, "MISSING") == nfsu::kNoCarType,
          "missing car type");
    check(nfsu::find_car_type(table, "car0") == nfsu::kNoCarType,
          "car type lookup is case sensitive");
}

void test_malformed_structure() {
    const std::vector<TestCar> cars = default_cars();

    {
        // No car type chunk at all.
        Bytes bytes;
        append_u32(bytes, 0);
        append_u32(bytes, 16);
        bytes.insert(bytes.end(), 16, 0);
        expect_rejected(bytes, "found 0", "missing car type chunk");
    }
    {
        // Two of them. Appending the whole second image really does add a
        // second 0x00034600 header; appending from a byte offset instead
        // would only add a payload and the duplicate branch would never
        // run.
        Layout a = build_table(cars);
        const Layout b = build_table(cars);
        a.bytes.insert(a.bytes.end(), b.bytes.begin(), b.bytes.end());
        expect_rejected(a.bytes, "found 2", "two car type chunks");
    }
    {
        // Trailing bytes smaller than a chunk header.
        Layout layout = build_table(cars);
        layout.bytes.push_back(0);
        expect_rejected(layout.bytes, "smaller than a chunk header",
                        "trailing partial header");
    }
    {
        // A payload larger than the file.
        Layout layout = build_table(cars);
        put_u32(layout.bytes, layout.chunk + 4, 0x7FFFFFFFU);
        expect_rejected(layout.bytes, "remain in the file", "payload past end of file");
    }
    {
        // Empty input.
        const nfsu::CarTypeTable table = nfsu::parse_car_type_table(nullptr, 0);
        check(!table.valid && !table.issues.empty(), "null input rejected");
        const Bytes empty;
        const nfsu::CarTypeTable table2 = nfsu::parse_car_type_table(empty.data(), 0);
        check(!table2.valid && !table2.issues.empty(), "empty input rejected");
    }
}

void test_malformed_counts_and_offsets() {
    const std::vector<TestCar> cars = default_cars();
    {
        // One record short: the game would read past the chunk, because
        // 0x57c8f1 hard-codes the count.
        const Layout layout =
            build_table(cars, 16, nfsu::kCarTypeInfoRecordCount - 1);
        expect_rejected(layout.bytes, "cannot hold", "payload one record short");
    }
    {
        // A single byte short of the last record.
        Layout layout = build_table(cars);
        layout.bytes.pop_back();
        put_u32(layout.bytes, layout.chunk + 4,
                static_cast<std::uint32_t>(layout.bytes.size() - layout.chunk - 8));
        expect_rejected(layout.bytes, "cannot hold", "payload one byte short");
    }
    {
        // No records at all.
        const Layout layout = build_table(cars, 16, 0);
        expect_rejected(layout.bytes, "cannot hold", "payload with no records");
    }
    {
        // A filler byte that is not 0x11.
        Layout layout = build_table(cars);
        const std::size_t payload = layout.chunk + 8;
        check(layout.records > payload, "the synthetic table has filler bytes");
        layout.bytes[payload] = 0x22;
        expect_rejected(layout.bytes, "alignment filler byte", "bad filler byte");
    }
    {
        // Shifting the whole file changes where the 16-byte boundary falls,
        // so the filler length must follow it. With this padding the payload
        // is already aligned and there is no filler at all.
        const Layout layout = build_table(cars, 16);
        const nfsu::CarTypeTable table =
            nfsu::parse_car_type_table(layout.bytes.data(), layout.bytes.size());
        check(table.ok(), "a different leading padding still parses");
        check(table.record_offset % nfsu::kCarTypeInfoAlignment == 0,
              "records stay 16-byte aligned");
        check(table.record_offset == layout.chunk + 8,
              "an aligned payload needs no filler");
    }
}

void test_malformed_fields() {
    const std::vector<TestCar> cars = default_cars();
    {
        // A name field with no terminator.
        Layout layout = build_table(cars);
        for (std::size_t i = 0; i < nfsu::kCarTypeStringFieldSize; ++i) {
            layout.bytes[layout.records + nfsu::kCarTypeNameOffset + i] = 'A';
        }
        expect_rejected(layout.bytes, "is not terminated", "unterminated name");
    }
    {
        // An empty name.
        Layout layout = build_table(cars);
        layout.bytes[layout.records + nfsu::kCarTypeNameOffset] = 0;
        expect_rejected(layout.bytes, "is empty", "empty name");
    }
    {
        // A non-printable byte in a name.
        Layout layout = build_table(cars);
        layout.bytes[layout.records + nfsu::kCarTypeNameOffset + 1] = 0x01;
        expect_rejected(layout.bytes, "outside printable ASCII", "non-printable name");
    }
    {
        // An empty geometry path.
        Layout layout = build_table(cars);
        layout.bytes[layout.records + nfsu::kCarTypeGeometryPathOffset] = 0;
        expect_rejected(layout.bytes, "geometry path", "empty geometry path");
    }
    {
        // An empty brand name.
        Layout layout = build_table(cars);
        layout.bytes[layout.records + nfsu::kCarTypeBrandNameOffset] = 0;
        expect_rejected(layout.bytes, "brand name", "empty brand name");
    }
    {
        // The two name fields disagreeing. The stock-part rules assume they
        // are equal, so the parser refuses instead of picking one.
        Layout layout = build_table(cars);
        layout.bytes[layout.records + nfsu::kCarTypeLookupNameOffset] = 'X';
        expect_rejected(layout.bytes, "differ", "name and lookup name differ");
    }
}

void test_malformed_wheels() {
    const std::vector<TestCar> cars = default_cars();
    const std::size_t wheel2 =
        nfsu::kCarWheelTableOffset + 2 * nfsu::kCarWheelRecordSize;
    {
        // A wheel whose stored index is not its position: the array is then
        // not the one 0x5693d0 indexes with a literal.
        Layout layout = build_table(cars);
        put_u32(layout.bytes, layout.records + wheel2 + 0x18, 3);
        expect_rejected(layout.bytes, "does not equal its position",
                        "wheel index mismatch");
    }
    {
        // Every wheel index zeroed, which is what a table of a different
        // shape would look like.
        Layout layout = build_table(cars);
        for (std::size_t w = 1; w < nfsu::kCarWheelCount; ++w) {
            put_u32(layout.bytes,
                    layout.records + nfsu::kCarWheelTableOffset +
                        w * nfsu::kCarWheelRecordSize + 0x18,
                    0);
        }
        expect_rejected(layout.bytes, "does not equal its position",
                        "all wheel indexes zero");
    }
    {
        // Reserved +0x0C not zero.
        Layout layout = build_table(cars);
        put_u32(layout.bytes, layout.records + wheel2 + 0x0C, 1);
        expect_rejected(layout.bytes, "+0x0C is not zero", "reserved 0x0C set");
    }
    {
        // Reserved +0x24 not zero.
        Layout layout = build_table(cars);
        put_u32(layout.bytes, layout.records + wheel2 + 0x24, 0x12345678U);
        expect_rejected(layout.bytes, "is not zero", "reserved 0x24 set");
    }
    {
        // Reserved +0x2C not zero.
        Layout layout = build_table(cars);
        put_u32(layout.bytes, layout.records + wheel2 + 0x2C, 1);
        expect_rejected(layout.bytes, "is not zero", "reserved 0x2C set");
    }
    {
        // A transform value that is not a number.
        Layout layout = build_table(cars);
        put_f32(layout.bytes, layout.records + wheel2 + 0x00,
                std::numeric_limits<float>::quiet_NaN());
        expect_rejected(layout.bytes, "not finite", "NaN longitudinal");
    }
    {
        // An infinite lateral.
        Layout layout = build_table(cars);
        put_f32(layout.bytes, layout.records + wheel2 + 0x04,
                std::numeric_limits<float>::infinity());
        expect_rejected(layout.bytes, "not finite", "infinite lateral");
    }
    {
        // An infinite radius, which would place the shadow at infinity.
        Layout layout = build_table(cars);
        put_f32(layout.bytes, layout.records + wheel2 + 0x10,
                -std::numeric_limits<float>::infinity());
        expect_rejected(layout.bytes, "not finite", "infinite radius");
    }
    {
        // All seven floats are checked, not just the three positions and
        // the radius: +0x14, +0x1C and +0x20 have traced readers too.
        Layout layout = build_table(cars);
        put_f32(layout.bytes, layout.records + wheel2 + 0x14,
                std::numeric_limits<float>::quiet_NaN());
        expect_rejected(layout.bytes, "not finite", "NaN in +0x14");
    }
    {
        Layout layout = build_table(cars);
        put_f32(layout.bytes, layout.records + wheel2 + 0x1C,
                std::numeric_limits<float>::quiet_NaN());
        expect_rejected(layout.bytes, "not finite", "NaN in +0x1C");
    }
    {
        Layout layout = build_table(cars);
        put_f32(layout.bytes, layout.records + wheel2 + 0x20,
                -std::numeric_limits<float>::infinity());
        expect_rejected(layout.bytes, "not finite", "infinite +0x20");
    }
}

std::array<nfsu::CarWheelPlacement, nfsu::kCarWheelCount> wheels_of(
    const TestCar& car) {
    std::array<nfsu::CarWheelPlacement, nfsu::kCarWheelCount> wheels{};
    for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
        wheels[w].longitudinal = car.wheels[w].longitudinal;
        wheels[w].lateral = car.wheels[w].lateral;
        wheels[w].vertical = car.wheels[w].vertical;
        wheels[w].rolling_radius = car.wheels[w].radius;
        wheels[w].value_14 = car.wheels[w].value_14;
        wheels[w].index = static_cast<std::uint32_t>(w);
    }
    return wheels;
}

void test_derive_placements() {
    const TestCar car = default_car("CAR");
    const auto wheels = wheels_of(car);
    std::array<nfsu::PartPlacement, nfsu::kCullableCarPartCount> placements;
    std::string error;
    check(nfsu::derive_cullable_part_placements(wheels, placements, error),
          "derivation succeeds");
    check(error.empty(), "derivation reports no error");

    // 0..3 are the wheels verbatim.
    for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
        check_close(placements[w].position[0], wheels[w].longitudinal,
                    "tire " + std::to_string(w) + " longitudinal");
        check_close(placements[w].position[1], wheels[w].lateral,
                    "tire " + std::to_string(w) + " lateral");
        check_close(placements[w].position[2], wheels[w].vertical,
                    "tire " + std::to_string(w) + " vertical");
        check(placements[w].source == nfsu::PlacementSource::wheel,
              "tire " + std::to_string(w) + " source");
        check(placements[w].source_count == 1 && placements[w].sources[0] == w &&
                  placements[w].index_space == nfsu::PlacementIndexSpace::wheel,
              "tire " + std::to_string(w) + " provenance");
    }

    // 4 and 5 are the axle midpoints of the left and right pairs. The left
    // pair is wheels 3 and 0, so its lateral is the positive one.
    check_close(placements[4].position[0], 0.0F, "blurs left longitudinal");
    check_close(placements[4].position[1], 0.75F, "blurs left lateral");
    check(placements[4].source == nfsu::PlacementSource::wheel_mean,
          "blurs left source");
    check(placements[4].source_count == 2 && placements[4].sources[0] == 3 &&
              placements[4].sources[1] == 0 &&
              placements[4].index_space == nfsu::PlacementIndexSpace::wheel,
          "blurs left provenance is wheels 3 and 0");
    check_close(placements[5].position[1], -0.75F, "blurs right lateral");
    check(placements[5].source_count == 2 && placements[5].sources[0] == 2 &&
              placements[5].sources[1] == 1 &&
              placements[5].index_space == nfsu::PlacementIndexSpace::wheel,
          "blurs right provenance is wheels 2 and 1");

    // 6..9 repeat the wheels for the brakes.
    for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
        check_close(placements[6 + w].position[0], wheels[w].longitudinal,
                    "brake " + std::to_string(w) + " longitudinal");
        check_close(placements[6 + w].position[1], wheels[w].lateral,
                    "brake " + std::to_string(w) + " lateral");
        check(placements[6 + w].sources[0] == w &&
                  placements[6 + w].index_space == nfsu::PlacementIndexSpace::wheel,
              "brake " + std::to_string(w) + " provenance");
    }

    // 10 is wheel 0 and 11 is wheel 2.
    check(placements[10].sources[0] == 0, "side front is wheel 0");
    check(placements[11].sources[0] == 2, "side rear is wheel 2");

    // 12 is the centroid of the four wheels.
    check_close(placements[12].position[0], 0.0F, "underneath longitudinal");
    check_close(placements[12].position[1], 0.0F, "underneath lateral");
    check_close(placements[12].position[2], 0.10F, "underneath vertical");
    check(placements[12].source == nfsu::PlacementSource::part_mean,
          "underneath source");
    // The one entry built from earlier placements. Its 5 and 4 are placement
    // indices; reading them as wheel indices would run off the 4-entry wheel
    // table, so the index space must say which table they belong to.
    check(placements[12].index_space == nfsu::PlacementIndexSpace::placement,
          "underneath sources are placement indices");
    check(placements[12].source_count == 2 && placements[12].sources[0] == 5 &&
              placements[12].sources[1] == 4,
          "underneath provenance is placements 5 and 4");

    // 13 is the centroid offset by wheel 0's radius along the wheel-plane
    // normal, which for a level car is straight down.
    check_close(placements[13].position[0], 0.0F, "shadow longitudinal");
    check_close(placements[13].position[1], 0.0F, "shadow lateral");
    check_close(placements[13].position[2], 0.10F - 0.30F, "shadow vertical");
    check(placements[13].source == nfsu::PlacementSource::plane_offset,
          "shadow source");
    check(!placements[13].degenerate_plane, "level car has a valid wheel plane");
    check(placements[13].source_count == 3, "shadow uses three wheels");
}

void test_derive_tilted_and_asymmetric() {
    // Raising the front axle tilts the wheel plane, so the shadow must move
    // off the vertical axis but stay one radius from the plane.
    TestCar car = default_car("CAR");
    car.wheels[0].vertical = 0.30F;
    car.wheels[1].vertical = 0.30F;
    const auto wheels = wheels_of(car);
    std::array<nfsu::PartPlacement, nfsu::kCullableCarPartCount> placements;
    std::string error;
    check(nfsu::derive_cullable_part_placements(wheels, placements, error),
          "tilted derivation succeeds");
    const nfsu::PartPlacement& centroid = placements[12];
    const nfsu::PartPlacement& shadow = placements[13];
    float delta[3];
    for (std::size_t i = 0; i < 3; ++i) {
        delta[i] = shadow.position[i] - centroid.position[i];
    }
    const float length =
        std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    check_close(length, wheels[0].rolling_radius, "tilted shadow is one radius from the plane");
    check(delta[0] > 0.0F, "a raised front axle tilts the shadow forward");
    check(delta[2] < 0.0F, "the shadow still sits below the plane");
    check(!shadow.degenerate_plane, "a tilted plane is not degenerate");
}

void test_derive_degenerate_plane() {
    // 0x56b3af compares the wheel-plane normal's length against the 0.0 at
    // 0x6cc7a4 and 0x56b3b7 takes the fallback only when it is exactly
    // zero, so any non-zero plane, however small, is normalized.
    check(nfsu::kWheelPlaneMinimumLength == 0.0F,
          "the degenerate threshold is zero");

    // Four coincident wheels give a zero-length normal.
    TestCar car = default_car("CAR");
    for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
        car.wheels[w].longitudinal = 0.0F;
        car.wheels[w].lateral = 0.0F;
        car.wheels[w].vertical = 0.0F;
    }
    const auto wheels = wheels_of(car);
    std::array<nfsu::PartPlacement, nfsu::kCullableCarPartCount> placements;
    std::string error;
    check(nfsu::derive_cullable_part_placements(wheels, placements, error),
          "degenerate derivation still succeeds");
    check(placements[13].degenerate_plane, "degenerate plane is reported");
    // 0x56b3e2 leaves 1.0 in the first component and 0.0 in the other two,
    // so the shadow moves along the longitudinal axis by wheel 0's radius.
    check_close(placements[13].position[0],
                placements[12].position[0] + wheels[0].rolling_radius,
                "degenerate shadow moves by the radius along the first axis");
    check_close(placements[13].position[1], placements[12].position[1],
                "degenerate shadow keeps the centroid lateral");
    check_close(placements[13].position[2], placements[12].position[2],
                "degenerate shadow keeps the centroid vertical");

    // Collinear wheels are degenerate too: both differences are parallel.
    TestCar collinear = default_car("CAR");
    for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
        collinear.wheels[w].lateral = 0.0F;
        collinear.wheels[w].vertical = 0.0F;
    }
    std::array<nfsu::PartPlacement, nfsu::kCullableCarPartCount> collinear_out;
    check(nfsu::derive_cullable_part_placements(wheels_of(collinear), collinear_out,
                                                error),
          "collinear derivation succeeds");
    check(collinear_out[13].degenerate_plane, "collinear wheels are degenerate");

    // A plane far smaller than the old 0.01 guess must still be normalized,
    // because the executable only rejects an exactly zero length. For a
    // rectangle |(w0-w1) x (w1-w2)| is 0.2 * lateral with these longitudes,
    // so lateral 0.04 gives 0.008 and lateral 0.0004 gives 0.00008.
    for (const float lateral : {0.04F, 0.0004F}) {
        TestCar tiny = default_car("CAR");
        tiny.wheels[0] = TestWheel{0.05F, lateral, 0.0F, 0.3F, 0.2F, 0.0F, 0.0F};
        tiny.wheels[1] = TestWheel{0.05F, -lateral, 0.0F, 0.3F, 0.2F, 0.0F, 0.0F};
        tiny.wheels[2] = TestWheel{-0.05F, -lateral, 0.0F, 0.3F, 0.2F, 0.0F, 0.0F};
        tiny.wheels[3] = TestWheel{-0.05F, lateral, 0.0F, 0.3F, 0.2F, 0.0F, 0.0F};
        std::array<nfsu::PartPlacement, nfsu::kCullableCarPartCount> tiny_out;
        check(nfsu::derive_cullable_part_placements(wheels_of(tiny), tiny_out, error),
              "small-plane derivation succeeds");
        check(!tiny_out[13].degenerate_plane,
              "a non-zero wheel plane is normalized, not treated as degenerate");
        // However small the plane, the normal is a unit vector, so the
        // shadow stays exactly one radius from the centroid.
        float delta[3];
        for (std::size_t i = 0; i < 3; ++i) {
            delta[i] = tiny_out[13].position[i] - tiny_out[12].position[i];
        }
        const float length = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] +
                                       delta[2] * delta[2]);
        check_close(length, 0.3F, "a tiny plane still gives a unit normal");
        check(delta[2] < 0.0F, "the shadow still sits below the plane");
    }
}

void test_derive_rejects_huge_positions() {
    // Finite but enormous wheel positions overflow the differences and the
    // cross product. The result must be refused rather than handed back.
    TestCar car = default_car("CAR");
    const float huge = 3.0e38F;
    car.wheels[0] = TestWheel{huge, huge, 0.0F, 0.3F, 0.2F, 0.0F, 0.0F};
    car.wheels[1] = TestWheel{huge, -huge, 0.0F, 0.3F, 0.2F, 0.0F, 0.0F};
    car.wheels[2] = TestWheel{-huge, -huge, 0.0F, 0.3F, 0.2F, 0.0F, 0.0F};
    car.wheels[3] = TestWheel{-huge, huge, 0.0F, 0.3F, 0.2F, 0.0F, 0.0F};
    std::array<nfsu::PartPlacement, nfsu::kCullableCarPartCount> placements;
    std::string error;
    check(!nfsu::derive_cullable_part_placements(wheels_of(car), placements, error),
          "an overflowing wheel table is refused");
    check(!error.empty(), "the overflow reports an error");
    for (const nfsu::PartPlacement& placement : placements) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            check(placement.position[axis] == 0.0F,
                  "a refused derivation clears its output");
        }
    }
}

void test_derive_rejects_non_finite() {
    for (std::size_t field = 0; field < 4; ++field) {
        TestCar car = default_car("CAR");
        const float bad = field % 2 == 0 ? std::numeric_limits<float>::quiet_NaN()
                                         : std::numeric_limits<float>::infinity();
        switch (field) {
        case 0:
            car.wheels[1].longitudinal = bad;
            break;
        case 1:
            car.wheels[1].lateral = bad;
            break;
        case 2:
            car.wheels[1].vertical = bad;
            break;
        default:
            car.wheels[0].radius = bad;
            break;
        }
        std::array<nfsu::PartPlacement, nfsu::kCullableCarPartCount> placements;
        std::string error;
        check(!nfsu::derive_cullable_part_placements(wheels_of(car), placements, error),
              "non-finite field " + std::to_string(field) + " is refused");
        check(!error.empty(),
              "non-finite field " + std::to_string(field) + " reports an error");
    }
}

// Every placement must name sources that are valid in the table its index
// space points at, and placement indices must never be reachable as wheel
// indices. Before this was modelled, CullableCarPart::underneath stored 5
// and 4 in a field documented as wheel indices, so a consumer following the
// header would have indexed the 4-entry wheel table with 5.
void test_placement_index_space() {
    const TestCar car = default_car("CAR");
    const auto wheels = wheels_of(car);
    std::array<nfsu::PartPlacement, nfsu::kCullableCarPartCount> placements;
    std::string error;
    check(nfsu::derive_cullable_part_placements(wheels, placements, error),
          "index space derivation succeeds");

    std::size_t wheel_space = 0;
    std::size_t placement_space = 0;
    for (std::size_t p = 0; p < placements.size(); ++p) {
        const nfsu::PartPlacement& placement = placements[p];
        const std::string what = "placement " + std::to_string(p);
        check(placement.source_count != 0 &&
                  placement.source_count <= placement.sources.size(),
              what + " names at least one source");
        // part_mean is the only source that reads earlier placements, so it
        // is exactly the set of entries in the placement index space.
        const bool expect_placement_space =
            placement.source == nfsu::PlacementSource::part_mean;
        check((placement.index_space == nfsu::PlacementIndexSpace::placement) ==
                  expect_placement_space,
              what + " index space matches its source kind");
        const std::size_t bound =
            placement.index_space == nfsu::PlacementIndexSpace::wheel
                ? nfsu::kCarWheelCount
                : nfsu::kCullableCarPartCount;
        for (std::size_t i = 0; i < placement.source_count; ++i) {
            check(placement.sources[i] < bound,
                  what + " source " + std::to_string(i) + " is in range");
        }

        std::array<std::size_t, 3> from_wheels{};
        std::size_t count = 12345;
        const bool is_wheel_space =
            placement.index_space == nfsu::PlacementIndexSpace::wheel;
        if (is_wheel_space) {
            ++wheel_space;
            check(nfsu::placement_wheel_sources(placement, from_wheels, count),
                  what + " exposes its wheel sources");
            check(count == placement.source_count,
                  what + " wheel source count");
            for (std::size_t i = 0; i < count; ++i) {
                check(from_wheels[i] == placement.sources[i],
                      what + " wheel source " + std::to_string(i));
            }
            for (std::size_t i = count; i < from_wheels.size(); ++i) {
                check(from_wheels[i] == 0,
                      what + " zeroes output element " + std::to_string(i));
            }
        } else {
            ++placement_space;
            // The accessor is the only safe way to reach the wheel table, so
            // it must refuse an entry whose indices are not wheels and leave
            // the caller's outputs alone.
            check(!nfsu::placement_wheel_sources(placement, from_wheels, count),
                  what + " refuses to report placement indices as wheels");
            check(count == 12345, what + " leaves the count untouched");
            for (const std::size_t value : from_wheels) {
                check(value == 0, what + " leaves the wheel array untouched");
            }
        }
    }
    check(placement_space == 1,
          "exactly one placement is built from earlier placements");
    check(wheel_space == nfsu::kCullableCarPartCount - 1,
          "the other thirteen are built from wheel records");

    check(std::string(nfsu::placement_index_space_name(
              nfsu::PlacementIndexSpace::wheel)) == "wheels",
          "wheel index space name");
    check(std::string(nfsu::placement_index_space_name(
              nfsu::PlacementIndexSpace::placement)) == "placements",
          "placement index space name");

    // A hand-built placement that lies about its space must not be able to
    // hand a caller an out-of-range wheel index either.
    nfsu::PartPlacement forged;
    forged.index_space = nfsu::PlacementIndexSpace::wheel;
    forged.sources[0] = nfsu::kCarWheelCount;
    forged.source_count = 1;
    std::array<std::size_t, 3> forged_out{};
    std::size_t forged_count = 7;
    check(!nfsu::placement_wheel_sources(forged, forged_out, forged_count),
          "an out-of-range wheel index is refused");
    check(forged_count == 7, "a refused forgery leaves the count untouched");

    // A count larger than the array must be refused before it is used as a
    // loop bound. This one pins the contract rather than catching a
    // regression: without the guard the accessor reads past the end of
    // `sources`, which is undefined and need not fail the check.
    nfsu::PartPlacement overlong;
    overlong.index_space = nfsu::PlacementIndexSpace::wheel;
    overlong.sources = {{0, 1, 2}};
    overlong.source_count = overlong.sources.size() + 1;
    check(!nfsu::placement_wheel_sources(overlong, forged_out, forged_count),
          "a source count past the array is refused");
    check(forged_count == 7, "a refused overlong count leaves the count untouched");

    // Entries past source_count mean nothing, so a junk tail must neither
    // refuse the placement nor be copied out: a caller that iterates the
    // returned array instead of applying the count would otherwise index the
    // wheel table with them.
    nfsu::PartPlacement junk_tail;
    junk_tail.index_space = nfsu::PlacementIndexSpace::wheel;
    junk_tail.sources = {{2, 9, 12345}};
    junk_tail.source_count = 1;
    std::array<std::size_t, 3> tail_out{};
    std::size_t tail_count = 7;
    check(nfsu::placement_wheel_sources(junk_tail, tail_out, tail_count),
          "a placement with a junk tail past its count is still usable");
    check(tail_count == 1, "the junk-tail placement names one source");
    check(tail_out[0] == 2, "the one named source is copied");
    for (const std::size_t value : tail_out) {
        check(value < nfsu::kCarWheelCount,
              "every element written is a wheel index");
    }
}

// The public in-memory parser must apply limits.max_file_size itself. The
// file entry point checked it before reading, so a caller that already had
// the bytes could parse an image of any size with the limit lowered.
void test_in_memory_size_limit() {
    const Layout layout = build_table(default_cars());

    nfsu::CarTypeLimits exact;
    exact.max_file_size = layout.bytes.size();
    const nfsu::CarTypeTable at_limit =
        nfsu::parse_car_type_table(layout.bytes.data(), layout.bytes.size(), exact);
    check(at_limit.ok(), "an image exactly at the limit is accepted");

    nfsu::CarTypeLimits one_short;
    one_short.max_file_size = layout.bytes.size() - 1;
    const nfsu::CarTypeTable over =
        nfsu::parse_car_type_table(layout.bytes.data(), layout.bytes.size(),
                                   one_short);
    check(!over.valid, "an image one byte over the limit is refused");
    check(over.records.empty(), "a refused image decodes no records");
    check(over.file_size == layout.bytes.size(),
          "a refused image still reports its size");
    // The in-memory entry point says "buffer", as parse_geometry does; only
    // load_car_type_table, which was handed a path, says "file".
    check(has_issue(over, "buffer is " + std::to_string(layout.bytes.size()) +
                              " bytes; limit is " +
                              std::to_string(layout.bytes.size() - 1)),
          "the in-memory limit issue names both sizes");
    check(!has_issue(over, "file is "),
          "the in-memory limit issue does not call the buffer a file");
    check(nfsu::find_car_type(over, "CAR0") == nfsu::kNoCarType,
          "lookup on an over-size table returns kNoCarType");

    // A deliberately tiny limit, the shape a caller would use to bound an
    // untrusted image, and the same limit the file entry point reports.
    nfsu::CarTypeLimits tiny;
    tiny.max_file_size = 8;
    const nfsu::CarTypeTable refused =
        nfsu::parse_car_type_table(layout.bytes.data(), layout.bytes.size(), tiny);
    check(!refused.valid && refused.issues.size() == 1U,
          "a tiny limit refuses with one issue");
    check(has_issue(refused, "limit is 8"), "the tiny limit is reported");

    // The limit is a bound on the image, not a reason to reject a small one.
    Bytes empty_chunk;
    append_u32(empty_chunk, 0);
    append_u32(empty_chunk, 0);
    const nfsu::CarTypeTable small =
        nfsu::parse_car_type_table(empty_chunk.data(), empty_chunk.size(), tiny);
    check(!has_issue(small, "limit is 8"),
          "an image inside the limit is not refused for its size");
}

void test_find_on_invalid_table() {
    // A table that failed to parse must not answer lookups.
    Bytes bytes;
    append_u32(bytes, 0);
    append_u32(bytes, 16);
    bytes.insert(bytes.end(), 16, 0);
    const nfsu::CarTypeTable table =
        nfsu::parse_car_type_table(bytes.data(), bytes.size());
    check(!table.valid, "table without the chunk is invalid");
    check(nfsu::find_car_type(table, "CAR0") == nfsu::kNoCarType,
          "lookup on an invalid table returns kNoCarType");
}

void test_load_file() {
    std::error_code ignored;
    const fs::path dir =
        fs::temp_directory_path(ignored) / unique_fixture("tests");
    fs::remove_all(dir, ignored);
    fs::create_directories(dir, ignored);
    const fs::path path = dir / "table.bin";

    const Layout layout = build_table(default_cars());
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(layout.bytes.data()),
                  static_cast<std::streamsize>(layout.bytes.size()));
    }
    const nfsu::CarTypeTable table = nfsu::load_car_type_table(path);
    check(table.ok(), "load from file");
    check(table.file_size == layout.bytes.size(), "file size");
    check(table.records.size() == nfsu::kCarTypeInfoRecordCount,
          "record count from file");

    nfsu::CarTypeLimits small;
    small.max_file_size = 8;
    const nfsu::CarTypeTable too_big = nfsu::load_car_type_table(path, small);
    check(!too_big.valid && !too_big.issues.empty(), "file over the size limit");
    check(has_issue(too_big, "file is " + std::to_string(layout.bytes.size()) +
                                 " bytes; limit is 8"),
          "the file limit issue names the file and both sizes");

    fs::remove(path, ignored);
    const nfsu::CarTypeTable missing = nfsu::load_car_type_table(path);
    check(!missing.valid && !missing.issues.empty(), "missing file");
    fs::remove_all(dir, ignored);
}

} // namespace

int main() {
    test_part_names();
    test_marker_names();
    test_marker_transform();
    test_valid_table();
    test_malformed_structure();
    test_malformed_counts_and_offsets();
    test_malformed_fields();
    test_malformed_wheels();
    test_derive_placements();
    test_derive_tilted_and_asymmetric();
    test_derive_degenerate_plane();
    test_derive_rejects_huge_positions();
    test_derive_rejects_non_finite();
    test_placement_index_space();
    test_in_memory_size_limit();
    test_find_on_invalid_table();
    test_load_file();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "part placement tests passed\n";
    return 0;
}
