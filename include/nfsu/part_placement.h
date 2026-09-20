#pragma once

#include "nfsu/mesh.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// Car type table of GLOBAL/GLOBALB.BUN and the per-part placement the game
// derives from it. Every rule below was read from the retail speed.exe;
// addresses are its virtual addresses. See docs/PART_PLACEMENT.md for the
// evidence and for what is observed, inferred and still unknown.
//
// The loader is 0x57c8b0. It accepts the chunk by id alone, aligns the
// payload up to a 16-byte boundary, stores that address in the global
// 0x734588 and then walks a fixed number of fixed-size records. The
// unloader 0x57cb70 clears the same global.
constexpr std::uint32_t kCarTypeInfoId = 0x00034600U;

// 0x57c8d2: imul eax, eax, 0xc90.
constexpr std::size_t kCarTypeInfoRecordSize = 0xC90;

// 0x57c8f1: cmp esi, 0x23 / jl. The count is hard-coded, not derived from
// the payload size, so a payload that cannot hold this many records would
// make the game read past the chunk. The parser refuses that instead.
constexpr std::size_t kCarTypeInfoRecordCount = 35;

// 0x57c8c2 and 0x57c8c5: lea edi, [esi + 0x17] / and edi, 0xfffffff0, with
// esi the chunk header, so the record array starts at the first 16-byte
// boundary at or after the payload.
constexpr std::uint64_t kCarTypeInfoAlignment = 16;

// The bytes between the payload start and the aligned record array are all
// 0x11 in the retail file, the same filler the geometry chunks use
// (nfsu::kAlignmentByte). The game never inspects them; the parser checks
// them because a different filler would mean a different writer.
constexpr unsigned char kCarTypeInfoFillerByte = 0x11U;

// Fixed-size NUL-terminated ASCII fields of one record. Field +0x00 is the
// name the decal helpers format and +0x20 is the name the part lookup
// hashes; they are equal in all 35 retail records. +0x40 and +0x60 are the
// two geometry paths the car loader reads (0x58b5dd takes record + 0x40).
// +0xC0 is the brand name the front end formats with BRAND_NAME_%s
// (0x562b6e). Each field is bounded by the start of the next one.
constexpr std::size_t kCarTypeNameOffset = 0x00;
constexpr std::size_t kCarTypeLookupNameOffset = 0x20;
constexpr std::size_t kCarTypeGeometryPathOffset = 0x40;
constexpr std::size_t kCarTypeCompressedPathOffset = 0x60;
constexpr std::size_t kCarTypeBrandNameOffset = 0xC0;
constexpr std::size_t kCarTypeStringFieldSize = 0x20;

// +0xC54. 0x57c8d8 runs a post-load pass over the sub-object below only
// when this is zero; 0x56e171 sets a render flag when it is 2. It is 0 for
// the 20 racers and 2 for the 15 traffic vehicles in the retail file. No
// other value was observed and none is refused.
constexpr std::size_t kCarTypeClassOffset = 0xC54;
constexpr std::uint32_t kCarTypeClassRacer = 0;
constexpr std::uint32_t kCarTypeClassTraffic = 2;

// The sub-object at record +0x110. 0x575960 copies 0xAC dwords of it into
// RideInfo +0x60 (mov ecx, 0xac / rep movsd), so it is exactly this long
// and each car instance owns a copy that its tuning may modify.
constexpr std::size_t kCarSetupBlockOffset = 0x110;
constexpr std::size_t kCarSetupBlockSize = 0x2B0;

// The four wheel records inside that sub-object. Their stride and count are
// proven twice: by the code, which reaches them at RideInfo +0x70, +0xA0,
// +0xD0 and +0x100 (0x56b18d, 0x56b19b, 0x56b1b2, 0x56b1c8), and by the
// data, whose field +0x18 holds the record's own position in all 35 cars.
constexpr std::size_t kCarWheelTableOffset = 0x120;
constexpr std::size_t kCarWheelRecordSize = 0x30;
constexpr std::size_t kCarWheelCount = 4;

// One 0x30-byte wheel record. Only the fields that a traced instruction
// reads are named; the rest keep neutral names and their raw bytes.
struct CarWheelPlacement {
    // Absolute file offset of the record.
    std::uint64_t offset = 0;
    // +0x00, +0x04, +0x08. 0x5693d0 copies exactly these three floats into
    // the placement entry, with no rotation, scale or offset. The frame is
    // the same right-handed frame the geometry file uses in raw slot order:
    // longitudinal is positive forward, lateral is positive toward the
    // car's left, vertical is positive up. docs/PART_PLACEMENT.md gives the
    // evidence for each of the three signs.
    float longitudinal = 0.0F;
    float lateral = 0.0F;
    float vertical = 0.0F;
    // +0x0C. Zero in all 140 retail wheel records.
    std::uint32_t reserved_0c = 0;
    // +0x10. The distance the shadow is offset from the plane of the four
    // wheel centres (0x56b3f5), and the term 0x4b17f5 combines as
    // (+0x10) - (+0x08) + 0.01, so it is the height of a wheel centre above
    // the ground: a rolling radius. It is NOT the visual wheel mesh's
    // radius. 240SX stores 0.288 front and 0.3135 rear while
    // 240SX_KIT00_WHEEL_A measures 0.311064 in both non-axle axes, and the
    // aftermarket rims in Cars/WHEELS quantize to exact inch diameters that
    // neither value matches. Equal within an axle in every retail record.
    float rolling_radius = 0.0F;
    // +0x14. Read by the wheel draw at 0x56c283 into CarRenderInfo +0x530,
    // where 0x56f9c6 uses it as a lateral scale and 0x56fa52 as half of a
    // lateral offset. Equal within an axle in every retail record. The exact
    // expression was not reproduced here, so no name beyond the offset is
    // claimed.
    float value_14 = 0.0F;
    // +0x18. The record's own index. 0x5693d0 is called with a matching
    // literal, and the stored value equals the position in all 140 records.
    std::uint32_t index = 0;
    // +0x1C and +0x20. Read negated by 0x56c267 and 0x56c275 into
    // CarRenderInfo +0x510 and +0x514, from which 0x56f980 selects one by a
    // RideInfo field. Both mirror the sign of `lateral` in every retail
    // record. Which of the two applies, and to what, was not reproduced
    // here, so neither is given a name.
    float value_1c = 0.0F;
    float value_20 = 0.0F;
    // +0x24, +0x28, +0x2C. Zero in all 140 retail wheel records.
    std::array<std::uint32_t, 3> reserved_24{};
    std::array<unsigned char, kCarWheelRecordSize> raw{};
};

// The 14 entries of the table at 0x704670. Each entry there carries its own
// index, a name, a flag and one to three direction vectors; the names below
// are byte for byte the retail strings. CarRenderInfo clears a 0x1C0-byte
// array at +0x1C00 (0x56a2c3: lea edi, [esi + 0x1c00]; 0x56a2cc: mov ecx,
// 0x70; rep stosd) and fills one 0x20-byte entry per part, so the array
// holds exactly these 14 placements.
enum class CullableCarPart {
    tire_fl = 0,
    tire_fr,
    tire_rr,
    tire_rl,
    tire_blurs_left,
    tire_blurs_right,
    brake_fl,
    brake_fr,
    brake_rr,
    brake_rl,
    side_front,
    side_rear,
    underneath,
    shadow
};

constexpr std::size_t kCullableCarPartCount = 14;
constexpr std::size_t kCullablePlacementEntrySize = 0x20;
constexpr std::size_t kCullablePlacementArraySize =
    kCullableCarPartCount * kCullablePlacementEntrySize;

// The retail name of a part, or nullptr outside the enumeration.
const char* cullable_car_part_name(CullableCarPart part) noexcept;

// How the constructor produced one placement.
enum class PlacementSource {
    wheel,       // copied verbatim from one wheel record
    wheel_mean,  // the mean of two wheel positions
    part_mean,   // the mean of two placements produced earlier
    plane_offset // a placement offset along the wheel-plane normal
};

const char* placement_source_name(PlacementSource source) noexcept;

struct PartPlacement {
    float position[3] = {0.0F, 0.0F, 0.0F};
    PlacementSource source = PlacementSource::wheel;
    // The wheel indexes the position was built from, in the order the
    // constructor reads them. Two entries for a mean, one for a copy, three
    // for the shadow's plane.
    std::array<std::size_t, 3> wheels{};
    std::size_t wheel_count = 0;
    // Set only for CullableCarPart::shadow, when the wheel plane is
    // degenerate and 0x56b3e2 takes the fallback branch instead of
    // normalizing. See docs/PART_PLACEMENT.md.
    bool degenerate_plane = false;
};

// 0x56b3af compares the wheel-plane normal's length against this before
// normalizing it; at or below it, 0x56b3e2 replaces the normal instead.
constexpr float kWheelPlaneMinimumLength = 0.01F;

// ---------------------------------------------------------------------
// Attachment markers (geometry chunk 0x0013401A)
// ---------------------------------------------------------------------
//
// The wheels are placed from the car type table above. Every other part
// that is not authored in car space is placed from a named 80-byte marker
// record stored on a mesh. The parser is in nfsu::parse_geometry; this
// layer names the hashes and reproduces the transform the draw paths apply.

// What the collector at 0x0056BDB0..0x0056C0DC does with a marker. The
// exhaust and licence-plate lists are the two whose consumers were traced
// to a draw call (0x0056F711 and 0x0056F880); the rest are collected into
// their own lists whose consumers were not traced, so they are reported but
// no transform is claimed for them.
enum class CarMarkerRole {
    exhaust,        // CarRenderInfo +0x67C, drawn at 0x0056F799
    license_plate,  // CarRenderInfo +0x704, drawn at 0x0056F92B
    brakelight,
    headlight,
    window,
    side_mirror,
    wheel_brake,    // read directly at 0x0056B8F8, not through a list
    unknown         // the hash is not one of the recovered names
};

const char* car_marker_role_name(CarMarkerRole role) noexcept;

// The name whose bin_hash is `hash`, or nullptr. Every name below was
// confirmed by recomputing bin_hash over it. One hash that occurs in retail
// data, 0x3AC3FFB1, has no recovered name and resolves to nullptr with role
// `unknown`.
const char* car_marker_name(std::uint32_t hash) noexcept;
CarMarkerRole car_marker_role(std::uint32_t hash) noexcept;

// Number of entries in the recovered-name table, for tests and listings.
std::size_t car_marker_name_count() noexcept;
// Entry `index` of that table, ascending by hash. Returns false out of range.
bool car_marker_name_at(std::size_t index,
                        std::uint32_t& hash,
                        const char*& name,
                        CarMarkerRole& role) noexcept;

// Both draw paths build an identity, turn it by 0x4000 of a full turn
// through 0x0040BA40, and premultiply the marker matrix by the result
// (0x0056F62E and 0x0056F6D1 for the exhaust, 0x0056F7CE..0x0056F87B for
// the plate). 0x0040BA40 writes cos into elements 0 and 10, -sin into 2 and
// +sin into 8, so the turn is a quarter turn in the longitudinal-vertical
// plane, about the lateral axis. At 0x4000 the sine is exactly 1 and the
// cosine exactly 0, so the matrix is exact and no trigonometry is needed.
void car_marker_quarter_turn(float out[16]) noexcept;

// out = a * b, row-major, the row-vector convention the game uses
// (0x005AC69C). `out` may alias neither input.
void multiply_placement_matrix(const float a[16],
                               const float b[16],
                               float out[16]) noexcept;

// The local transform a marker gives its part, before the car's own world
// matrix: car_marker_quarter_turn() * marker.matrix. Returns false, leaving
// `out` untouched, if any element of the marker matrix is not finite.
bool car_marker_placement(const MeshMarker& marker, float out[16]) noexcept;

// One decoded car type record. Only the fields a traced instruction reads
// are decoded; everything else stays in `raw`.
struct CarTypeRecord {
    std::size_t index = 0;
    std::uint64_t offset = 0;
    std::string name;
    std::string lookup_name;
    std::string geometry_path;
    std::string compressed_path;
    std::string brand_name;
    std::uint32_t class_id = 0;
    std::array<CarWheelPlacement, kCarWheelCount> wheels{};
};

struct CarTypeIssue {
    std::uint64_t offset = 0;
    std::string message;
};

struct CarTypeLimits {
    std::uint64_t max_file_size = 256ULL * 1024ULL * 1024ULL;
    std::size_t max_top_level_chunks = 1U << 20;
};

struct CarTypeTable {
    std::uint64_t file_size = 0;
    // Absolute offset of the 0x00034600 chunk header.
    std::uint64_t chunk_offset = 0;
    // Absolute offset of the aligned record array.
    std::uint64_t record_offset = 0;
    std::vector<CarTypeRecord> records;
    std::vector<CarTypeIssue> issues;
    // True only when the whole table was read and every check passed. The
    // lookup and derivation functions refuse a table that is not valid.
    bool valid = false;

    bool ok() const noexcept { return valid && issues.empty(); }
};

// Parses the car type table of an in-memory file such as GLOBALB.BUN. Never
// throws for malformed input. The file must be a sequence of top-level
// chunks with exactly one 0x00034600 among them, whose payload holds the
// filler bytes and then all kCarTypeInfoRecordCount records. Checks: the
// chunk count and payload bounds, the filler bytes, NUL-terminated printable
// names in every string field, the two name fields being equal, every wheel
// record's stored index, its four reserved words, and every wheel float
// being finite.
CarTypeTable parse_car_type_table(
    const unsigned char* data,
    std::size_t size,
    const CarTypeLimits& limits = CarTypeLimits{});

// Reads the file read-only and parses it.
CarTypeTable load_car_type_table(
    const std::filesystem::path& path,
    const CarTypeLimits& limits = CarTypeLimits{});

constexpr std::size_t kNoCarType = static_cast<std::size_t>(-1);

// Index of the record whose +0x00 name equals `car_type_name` byte for
// byte, or kNoCarType. The game indexes this array by number (RideInfo
// +0x00); the name search is this project's convenience and matches the
// name the part database is keyed by.
std::size_t find_car_type(const CarTypeTable& table,
                          const std::string& car_type_name) noexcept;

// Reproduces the 14 placements the CarRenderInfo constructor writes into
// its +0x1C00 array, from the four wheel records of one car
// (0x56b18d..0x56b44x). The order and the source of every entry are fixed
// in the executable:
//   0..3   the four wheel positions, copied verbatim
//   4      the mean of wheels 3 and 0 (the left pair)
//   5      the mean of wheels 2 and 1 (the right pair)
//   6..9   the four wheel positions again
//   10     wheel 0, 11 wheel 2
//   12     the mean of entries 5 and 4, so the centroid of the four wheels
//   13     entry 12 offset along the normalized wheel-plane normal by
//          wheel 0's radius
// Fails only for a wheel float that is not finite; a degenerate wheel plane
// is reported in the shadow entry rather than refused, because the game
// takes a fallback branch for it.
bool derive_cullable_part_placements(
    const std::array<CarWheelPlacement, kCarWheelCount>& wheels,
    std::array<PartPlacement, kCullableCarPartCount>& placements,
    std::string& error);

} // namespace nfsu
