#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace nfsu {

// Car part database of GLOBAL/GLOBALB.BUN. The layout and every rule below
// were read from the retail speed.exe; addresses are its virtual addresses.
// See docs/FORMATS.md and docs/CAR_SCENE.md.
//
// 0x80034602 holds exactly four children in this order. The game's loader
// (0x57c927) locates them by size alone and never checks their ids.
constexpr std::uint32_t kCarPartDatabaseId = 0x80034602U;
constexpr std::uint32_t kCarPartHeaderId = 0x00034603U;
constexpr std::uint32_t kCarPartStringTableId = 0x00034606U;
constexpr std::uint32_t kCarPartAttributeTableId = 0x00034605U;
constexpr std::uint32_t kCarPartRecordTableId = 0x00034604U;

constexpr std::size_t kCarPartHeaderSize = 32;
constexpr std::size_t kCarPartAttributeSize = 8;
constexpr std::size_t kCarPartRecordSize = 48;
// Header +8. The only value observed; other versions are refused.
constexpr std::uint32_t kCarPartDatabaseVersion = 5;

// RideInfo holds one installed part per slot. Slot ids and names come from
// the 120-entry {id, name} table at 0x6f0970; the id equals the position.
constexpr std::size_t kCarSlotCount = 120;
constexpr int kSlotFrontBumper = 1;
constexpr int kSlotRearBumper = 2;
constexpr int kSlotBody = 5;
constexpr int kSlotWideBody = 6;
constexpr int kSlotSkirt = 10;
constexpr int kSlotEngine = 12;
constexpr int kSlotHeadlight = 13;
constexpr int kSlotBrakelight = 14;
constexpr int kSlotBrake = 16;
constexpr int kSlotWheel = 17;
constexpr int kSlotLicensePlate = 18;
constexpr int kSlotDecalHood = 19;
constexpr int kSlotDecalFrontWindow = 20;
constexpr int kSlotDecalRearWindow = 21;
constexpr int kSlotDecalLeftDoor = 22;
constexpr int kSlotDecalRightDoor = 23;
constexpr int kSlotDecalLeftQuarter = 24;
constexpr int kSlotDecalRightQuarter = 25;
constexpr int kSlotWideBodyDecalLeftDoor = 26;
constexpr int kSlotWideBodyDecalRightQuarter = 29;
constexpr int kSlotVinylLayer0 = 31;
constexpr int kSlotVinylLayer3 = 34;

// Name of a slot id, or nullptr outside 0..kCarSlotCount-1.
const char* car_slot_name(int slot) noexcept;

// Per-LOD model hashes, record +0x20 +0x24 +0x28 +0x2C. Index 0 is the
// mesh name suffix _A, 3 is _D. A zero hash means the record names no mesh
// for that LOD.
constexpr std::size_t kCarPartLodCount = 4;

// Attribute key whose value is the hash of a slot name; 0x57ea80 disables
// that slot while the part is installed. The key's own string is unknown.
constexpr std::uint32_t kDisableSlotAttributeKey = 0xE80A3B62U;

struct CarPartAttribute {
    std::uint32_t key = 0;
    std::uint32_t value = 0;
};

// One 48-byte record of 0x00034604. Field names describe how speed.exe uses
// a field; fields it was not seen to use keep neutral names.
struct CarPartRecord {
    std::size_t index = 0;
    // Absolute file offset of the record.
    std::uint64_t offset = 0;
    // +0x00: offset into 0x00034606 and the NUL-terminated string there,
    // the part's display name, byte for byte. No lookup uses it.
    std::uint32_t name_offset = 0;
    std::string name;
    // +0x04: bin_hash of the car type name; lookups binary-search on it.
    std::uint32_t car_type_hash = 0;
    // +0x08: bin_hash of the part name, e.g. of "240SX_KIT00_FRONT_BUMPER".
    std::uint32_t part_name_hash = 0;
    // +0x0C: meaning not decoded (0x57c9c1 groups paint parts by it).
    std::uint32_t hash_0c = 0;
    // +0x10: slot id, compared as a signed byte.
    int slot = 0;
    // +0x11: upgrade level, compared as an unsigned byte. The stock rule
    // picks level 0.
    std::uint8_t upgrade_level = 0;
    // +0x12 and +0x13: meanings not decoded.
    std::uint8_t byte_12 = 0;
    std::uint8_t byte_13 = 0;
    // +0x14 attribute index with the signed 16-bit count at +0x1C, and
    // +0x18 with its count at +0x1E. An index is only used when its count
    // is nonzero.
    std::uint32_t attribute_index = 0;
    std::uint32_t second_attribute_index = 0;
    std::int16_t attribute_count = 0;
    std::int16_t second_attribute_count = 0;
    std::array<std::uint32_t, kCarPartLodCount> lod_mesh_hashes{};
    std::array<unsigned char, kCarPartRecordSize> raw{};
};

struct CarPartIssue {
    std::uint64_t offset = 0;
    std::string message;
};

struct CarPartLimits {
    std::uint64_t max_file_size = 256ULL * 1024ULL * 1024ULL;
    std::size_t max_top_level_chunks = 1U << 20;
    std::size_t max_records = 65536;
    std::size_t max_attributes = 1U << 20;
};

struct CarPartDatabase {
    std::uint64_t file_size = 0;
    // Absolute offset of the 0x80034602 chunk header.
    std::uint64_t chunk_offset = 0;
    std::uint32_t version = 0;
    std::vector<CarPartRecord> records;
    std::vector<CarPartAttribute> attributes;
    std::vector<CarPartIssue> issues;
    // True only when the whole database was read and every check passed.
    // The lookup functions refuse a database that is not valid.
    bool valid = false;

    bool ok() const noexcept { return valid && issues.empty(); }
};

// Parses the car part database of an in-memory file such as GLOBALB.BUN.
// Never throws for malformed input. The file must be a sequence of
// top-level chunks with exactly one 0x80034602 among them. Checks: the four
// children and their order, header size, version and zero pointer words,
// part and attribute counts against their tables, name offsets and
// NUL-terminated names, attribute ranges, slot ids 0..119, and
// records sorted by car type hash (unsigned, ascending), which the game's
// binary search at 0x53e400 requires.
CarPartDatabase parse_car_part_database(
    const unsigned char* data,
    std::size_t size,
    const CarPartLimits& limits = CarPartLimits{});

// Reads the file read-only and parses it.
CarPartDatabase load_car_part_database(
    const std::filesystem::path& path,
    const CarPartLimits& limits = CarPartLimits{});

constexpr std::size_t kNoCarPart = std::numeric_limits<std::size_t>::max();
constexpr std::uint32_t kAnyCarType = 0;
constexpr int kAnySlot = -1;
constexpr std::uint32_t kAnyPartName = 0;
constexpr int kAnyUpgradeLevel = -1;

// The game's part lookup (0x57ce10) on one database. Returns the index of
// the first record after `after` (kNoCarPart: from the start) that matches
// every given key, or kNoCarPart. With a car type hash the search starts at
// the first record of that car type (or after `after`) and ends at the
// first record of another car type. Returns kNoCarPart for a database that
// is not valid or an `after` outside it.
std::size_t find_car_part(
    const CarPartDatabase& database,
    std::uint32_t car_type_hash,
    int slot,
    std::uint32_t part_name_hash,
    std::size_t after,
    int upgrade_level) noexcept;

// How select_stock_parts filled a slot.
enum class StockPartRule {
    none,             // no record: the slot stays empty
    skipped,          // slots 6, 12 and 31..34 are not filled
    upgrade_level_0,  // first record of the car type, slot and level 0
    brakes,           // slot 16: first record of car type BRAKES and level 0
    plates_fallback,  // slot 18 without a level-0 record: first record of
                      // car type PLATES and slot 18, any level
    decal_name        // slots 19..25: record with a car-specific name
};

const char* stock_part_rule_name(StockPartRule rule) noexcept;

struct StockPartSlot {
    std::size_t record = kNoCarPart;
    StockPartRule rule = StockPartRule::none;
    bool overridden = false;
    // The per-slot enable byte after the stock parts are installed, as
    // computed by 0x57ea80 with no garage preview part.
    bool enabled = true;
};

struct StockParts {
    std::string car_type_name;
    std::uint32_t car_type_hash = 0;
    std::array<StockPartSlot, kCarSlotCount> slots{};
};

// Caller-provided installed parts. An absent entry keeps the stock part.
// This input is a viewer construct, not a decoded garage or save record.
struct InstalledPartSelection {
    std::array<std::optional<std::string>, kCarSlotCount> names{};
};

// CarRenderInfo (0x56a130) builds models only for slots 0..29, so a part
// in a higher slot, such as the collision volume in slot 119, never gets
// one. Whether a model is drawn is decided per slot; see docs/CAR_SCENE.md.
constexpr std::size_t kCarModelSlotCount = 30;

// Pixel sizes at 0x6b5dec. CarRenderInfo::Render (0x56dc30) starts at a
// minimum LOD index and raises it while the car's pixel size is below the
// entry for that index, clamped to the last one. Below kCarMinimumPixelSize
// the car is not drawn at all. The pixel size itself is
// eView::GetPixelSize (0x545180) for radius 3.0, which needs the view.
constexpr std::array<float, kCarPartLodCount> kCarLodPixelSizes{
    120.0F, 45.0F, 20.0F, 0.0F};
constexpr int kCarMinimumPixelSize = 4;

// The LOD index for a pixel size, starting at minimum_lod (clamped into
// 0..kCarPartLodCount-1). The game compares an integer size, so it never
// sees a NaN; here a NaN keeps minimum_lod.
std::size_t car_lod_for_pixel_size(float pixel_size, std::size_t minimum_lod) noexcept;

// Where the mesh hash of one model comes from.
enum class PartModelSource {
    none,       // the model stays empty and draws nothing
    requested,  // the record's hash for this LOD, whose solid was found
    fallback    // the record's hash was missing; another LOD's hash is used
};

const char* part_model_source_name(PartModelSource source) noexcept;

struct PartModel {
    // The record's hash for this LOD; 0 when it names no mesh.
    std::uint32_t requested_hash = 0;
    // The hash the model ends up with; 0 when it stays empty.
    std::uint32_t hash = 0;
    PartModelSource source = PartModelSource::none;
    // True when a solid for `hash` is loaded. A fallback hash can be
    // missing too, and then nothing is drawn at this LOD.
    bool resolved = false;
};

// Reproduces how CarRenderInfo's constructor fills the four models of one
// installed part (0x56a95d..0x56aae9). It keeps a fallback hash, which
// starts at the record's LOD B hash. For LOD A, B, C, D in turn: a zero
// hash leaves the model empty; a hash whose solid is loaded becomes the
// model and the new fallback; a hash whose solid is missing is replaced by
// the current fallback and looked up again, which may also find nothing.
// loaded_hashes lists the mesh hashes the game would find, ascending and
// without duplicates, as its lookup (0x540630) binary-searches the
// 0x00134003 table of every loaded geometry file. Fails, with models
// cleared, if that order is broken.
bool resolve_part_models(
    const CarPartRecord& record,
    const std::vector<std::uint32_t>& loaded_hashes,
    std::array<PartModel, kCarPartLodCount>& models,
    std::string& error);

// Fills a RideInfo part table the way speed.exe's stock-part routine does
// (0x57d340 with 0x57d280 and 0x504630): every slot gets the first record
// of the car type, that slot and upgrade level 0, except slots 6, 12 and
// 31..34 (skipped), slot 16 (car type BRAKES), slot 18 (falls back to car
// type PLATES) and the decal slots 19..25, which are overwritten with the
// record named "<car>_DECAL_..." (see docs/CAR_SCENE.md). The enable bytes
// are then derived as 0x57ea80 does. The game hashes CarTypeInfo +0x20 for
// the car type and formats decal names with CarTypeInfo +0x00; both are
// car_type_name here, which holds for every retail car type. Fails for an
// invalid database, an empty or non-printable name, or an unknown car type.
bool select_stock_parts(
    const CarPartDatabase& database,
    const std::string& car_type_name,
    StockParts& parts,
    std::string& error);

// Resolves overrides by the retail part-name hash in the named slot and car
// type, with shared WHEELS, BRAKES and PLATES records in their matching slots,
// then recomputes enable bytes. Unknown or incompatible names are errors.
bool select_installed_parts(
    const CarPartDatabase& database,
    const std::string& car_type_name,
    const InstalledPartSelection& selection,
    StockParts& parts,
    std::string& error);

} // namespace nfsu
