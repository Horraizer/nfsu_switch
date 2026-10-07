#include "nfsu/car_parts.h"

#include "nfsu/chunk.h"
#include "nfsu/texture.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <system_error>
#include <utility>

namespace nfsu {
namespace {

constexpr std::uint64_t kHeaderSize = 8;

// speed.exe slot table at 0x6f0970; entry i has id i.
constexpr std::array<const char*, kCarSlotCount> kSlotNames{
    "BASE", "FRONT_BUMPER", "REAR_BUMPER", "LEFT_SIDE_MIRROR",
    "RIGHT_SIDE_MIRROR", "BODY", "WIDE_BODY", "ROOF", "HOOD", "TRUNK",
    "SKIRT", "SPOILER", "ENGINE", "HEADLIGHT", "BRAKELIGHT", "EXHAUST",
    "BRAKE", "WHEEL", "LICENSE_PLATE", "DECAL_HOOD", "DECAL_FRONT_WINDOW",
    "DECAL_REAR_WINDOW", "DECAL_LEFT_DOOR", "DECAL_RIGHT_DOOR",
    "DECAL_LEFT_QUARTER", "DECAL_RIGHT_QUARTER", "WIDEBODY_DECAL_LEFT_DOOR",
    "WIDEBODY_DECAL_RIGHT_DOOR", "WIDEBODY_DECAL_LEFT_QUARTER",
    "WIDEBODY_DECAL_RIGHT_QUARTER", "BASE_PAINT", "VINYL_LAYER0",
    "VINYL_LAYER1", "VINYL_LAYER2", "VINYL_LAYER3", "VINYL_HOOD",
    "VINYL_SPOILER", "PAINT_ENGINE", "PAINT_SPOILER", "PAINT_BRAKE",
    "PAINT_EXHAUST", "PAINT_RIM", "VINYL_COLOUR0_0", "VINYL_COLOUR0_1",
    "VINYL_COLOUR0_2", "VINYL_COLOUR0_3", "VINYL_COLOUR1_0",
    "VINYL_COLOUR1_1", "VINYL_COLOUR1_2", "VINYL_COLOUR1_3",
    "VINYL_COLOUR2_0", "VINYL_COLOUR2_1", "VINYL_COLOUR2_2",
    "VINYL_COLOUR2_3", "VINYL_COLOUR3_0", "VINYL_COLOUR3_1",
    "VINYL_COLOUR3_2", "VINYL_COLOUR3_3", "DECAL_TEX", "DECAL_HOOD_TEX0",
    "DECAL_HOOD_TEX1", "DECAL_HOOD_TEX2", "DECAL_HOOD_TEX3",
    "DECAL_HOOD_TEX4", "DECAL_HOOD_TEX5", "DECAL_HOOD_TEX6",
    "DECAL_HOOD_TEX7", "DECAL_FRONT_WINDOW_TEX0", "DECAL_FRONT_WINDOW_TEX1",
    "DECAL_FRONT_WINDOW_TEX2", "DECAL_FRONT_WINDOW_TEX3",
    "DECAL_FRONT_WINDOW_TEX4", "DECAL_FRONT_WINDOW_TEX5",
    "DECAL_FRONT_WINDOW_TEX6", "DECAL_FRONT_WINDOW_TEX7",
    "DECAL_REAR_WINDOW_TEX0", "DECAL_REAR_WINDOW_TEX1",
    "DECAL_REAR_WINDOW_TEX2", "DECAL_REAR_WINDOW_TEX3",
    "DECAL_REAR_WINDOW_TEX4", "DECAL_REAR_WINDOW_TEX5",
    "DECAL_REAR_WINDOW_TEX6", "DECAL_REAR_WINDOW_TEX7",
    "DECAL_LEFT_DOOR_TEX0", "DECAL_LEFT_DOOR_TEX1", "DECAL_LEFT_DOOR_TEX2",
    "DECAL_LEFT_DOOR_TEX3", "DECAL_LEFT_DOOR_TEX4", "DECAL_LEFT_DOOR_TEX5",
    "DECAL_LEFT_DOOR_TEX6", "DECAL_LEFT_DOOR_TEX7", "DECAL_RIGHT_DOOR_TEX0",
    "DECAL_RIGHT_DOOR_TEX1", "DECAL_RIGHT_DOOR_TEX2",
    "DECAL_RIGHT_DOOR_TEX3", "DECAL_RIGHT_DOOR_TEX4",
    "DECAL_RIGHT_DOOR_TEX5", "DECAL_RIGHT_DOOR_TEX6",
    "DECAL_RIGHT_DOOR_TEX7", "DECAL_LEFT_QUARTER_TEX0",
    "DECAL_LEFT_QUARTER_TEX1", "DECAL_LEFT_QUARTER_TEX2",
    "DECAL_LEFT_QUARTER_TEX3", "DECAL_LEFT_QUARTER_TEX4",
    "DECAL_LEFT_QUARTER_TEX5", "DECAL_LEFT_QUARTER_TEX6",
    "DECAL_LEFT_QUARTER_TEX7", "DECAL_RIGHT_QUARTER_TEX0",
    "DECAL_RIGHT_QUARTER_TEX1", "DECAL_RIGHT_QUARTER_TEX2",
    "DECAL_RIGHT_QUARTER_TEX3", "DECAL_RIGHT_QUARTER_TEX4",
    "DECAL_RIGHT_QUARTER_TEX5", "DECAL_RIGHT_QUARTER_TEX6",
    "DECAL_RIGHT_QUARTER_TEX7", "WINDOW_TINT", "NEON", "CV",
    "WHEEL_MANUFACTURER", "MISC"};

// Decal slot and the name format the stock routine passes to the
// sprintf-and-hash helper 0x4f42f0 (pointers at 0x6f88ac..0x6f88ec), in
// the routine's order. "%s" is the CarTypeInfo +0x00 name.
struct StockDecal {
    int slot;
    const char* suffix;
};
constexpr std::array<StockDecal, 7> kStockDecals{{
    {kSlotDecalFrontWindow, "_DECAL_FRONT_WINDOW_WIDE_MEDIUM"},
    {kSlotDecalRearWindow, "_DECAL_REAR_WINDOW_WIDE_MEDIUM"},
    {kSlotDecalLeftDoor, "_DECAL_LEFT_DOOR_RECT_MEDIUM"},
    {kSlotDecalRightDoor, "_DECAL_RIGHT_DOOR_RECT_MEDIUM"},
    {kSlotDecalLeftQuarter, "_DECAL_LEFT_QUARTER_RECT_MEDIUM"},
    {kSlotDecalRightQuarter, "_DECAL_RIGHT_QUARTER_RECT_MEDIUM"},
    {kSlotDecalHood, "_DECAL_HOOD_RECT_MEDIUM"},
}};

// Car type names the stock routine uses for slots 16 and 18.
constexpr const char* kBrakesCarType = "BRAKES";
constexpr const char* kPlatesCarType = "PLATES";

// CarTypeInfo name fields are 32 bytes including the terminator.
constexpr std::size_t kMaxCarTypeNameSize = 31;

std::uint32_t read_u32_le(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::int16_t read_i16_le(const unsigned char* bytes) {
    const auto bits = static_cast<std::uint16_t>(
        static_cast<unsigned int>(bytes[0]) |
        (static_cast<unsigned int>(bytes[1]) << 8U));
    return static_cast<std::int16_t>(bits);
}

bool printable_ascii(const std::string& text) {
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U || byte > 0x7EU) {
            return false;
        }
    }
    return true;
}

struct RawChunk {
    std::uint32_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_size = 0;
};

class DatabaseParser {
public:
    DatabaseParser(const unsigned char* data,
                   std::uint64_t size,
                   const CarPartLimits& limits,
                   CarPartDatabase& database)
        : data_(data), size_(size), limits_(limits), database_(database) {}

    void run() {
        std::vector<RawChunk> top;
        if (!list_children(0, size_, "top level", limits_.max_top_level_chunks,
                           top)) {
            return;
        }
        std::vector<RawChunk> roots;
        for (const RawChunk& chunk : top) {
            if (chunk.id == kCarPartDatabaseId) {
                roots.push_back(chunk);
            }
        }
        if (roots.size() != 1) {
            add_issue(roots.empty() ? 0 : roots[1].offset,
                      "expected exactly one top-level " +
                          format_chunk_id(kCarPartDatabaseId) +
                          " car part database, found " +
                          std::to_string(roots.size()));
            return;
        }
        database_.chunk_offset = roots.front().offset;
        if (parse_database(roots.front())) {
            database_.valid = database_.issues.empty();
        }
    }

private:
    void add_issue(std::uint64_t offset, std::string message) {
        database_.issues.push_back(CarPartIssue{offset, std::move(message)});
    }

    std::string chunk_label(const RawChunk& chunk) const {
        return "chunk " + format_chunk_id(chunk.id) + " at offset " +
               std::to_string(chunk.offset);
    }

    // Lists the direct children of [begin, end). Returns false after an
    // issue if the sequence is malformed or exceeds max_chunks.
    bool list_children(std::uint64_t begin,
                       std::uint64_t end,
                       const std::string& context,
                       std::size_t max_chunks,
                       std::vector<RawChunk>& out) {
        std::uint64_t offset = begin;
        while (offset < end) {
            if (end - offset < kHeaderSize) {
                add_issue(offset, context + ": " + std::to_string(end - offset) +
                                      " trailing bytes are smaller than a chunk header");
                return false;
            }
            if (out.size() >= max_chunks) {
                add_issue(offset, context + ": chunk limit " +
                                      std::to_string(max_chunks) + " exceeded");
                return false;
            }
            RawChunk chunk;
            chunk.offset = offset;
            chunk.id = read_u32_le(data_ + offset);
            chunk.payload_size = read_u32_le(data_ + offset + 4);
            chunk.payload_offset = offset + kHeaderSize;
            if (chunk.payload_size > end - chunk.payload_offset) {
                add_issue(offset, context + ": " + chunk_label(chunk) + " declares " +
                                      std::to_string(chunk.payload_size) +
                                      " payload bytes but only " +
                                      std::to_string(end - chunk.payload_offset) +
                                      " remain in its parent");
                return false;
            }
            out.push_back(chunk);
            offset = chunk.payload_offset + chunk.payload_size;
        }
        return true;
    }

    bool parse_database(const RawChunk& root) {
        std::vector<RawChunk> children;
        if (!list_children(root.payload_offset,
                           root.payload_offset + root.payload_size,
                           chunk_label(root), kExpectedChildren.size() + 1,
                           children)) {
            return false;
        }
        bool layout_ok = children.size() == kExpectedChildren.size();
        for (std::size_t i = 0; layout_ok && i < children.size(); ++i) {
            layout_ok = children[i].id == kExpectedChildren[i];
        }
        if (!layout_ok) {
            std::string found;
            for (const RawChunk& child : children) {
                found += (found.empty() ? "" : ", ") + format_chunk_id(child.id);
            }
            add_issue(root.offset,
                      chunk_label(root) + " must hold exactly " +
                          format_chunk_id(kCarPartHeaderId) + ", " +
                          format_chunk_id(kCarPartStringTableId) + ", " +
                          format_chunk_id(kCarPartAttributeTableId) + ", " +
                          format_chunk_id(kCarPartRecordTableId) +
                          " in this order; found " +
                          (found.empty() ? std::string("none") : found));
            return false;
        }
        const RawChunk& header = children[0];
        const RawChunk& strings = children[1];
        const RawChunk& attributes = children[2];
        const RawChunk& records = children[3];

        if (header.payload_size != kCarPartHeaderSize) {
            add_issue(header.offset, chunk_label(header) + " payload is " +
                                         std::to_string(header.payload_size) +
                                         " bytes; expected " +
                                         std::to_string(kCarPartHeaderSize));
            return false;
        }
        const unsigned char* h = data_ + header.payload_offset;
        database_.version = read_u32_le(h + 8);
        const std::uint32_t record_count = read_u32_le(h + 16);
        const std::uint32_t attribute_count = read_u32_le(h + 24);
        bool ok = true;
        if (database_.version != kCarPartDatabaseVersion) {
            add_issue(header.payload_offset + 8,
                      "car part database version " +
                          std::to_string(database_.version) + " is not supported (expected " +
                          std::to_string(kCarPartDatabaseVersion) + ")");
            ok = false;
        }
        // Runtime list links and table pointers, zero in the file.
        for (const std::size_t word : {0U, 4U, 12U, 20U, 28U}) {
            if (read_u32_le(h + word) != 0) {
                add_issue(header.payload_offset + word,
                          "car part header word +" + std::to_string(word) +
                              " must be zero in the file");
                ok = false;
            }
        }
        if (record_count > limits_.max_records) {
            add_issue(header.payload_offset + 16,
                      std::to_string(record_count) + " part records; the limit is " +
                          std::to_string(limits_.max_records));
            ok = false;
        } else if (records.payload_size !=
                   std::uint64_t{record_count} * kCarPartRecordSize) {
            add_issue(records.offset, chunk_label(records) + " payload is " +
                                          std::to_string(records.payload_size) +
                                          " bytes; the header declares " +
                                          std::to_string(record_count) + " records of " +
                                          std::to_string(kCarPartRecordSize));
            ok = false;
        }
        if (attribute_count > limits_.max_attributes) {
            add_issue(header.payload_offset + 24,
                      std::to_string(attribute_count) +
                          " part attributes; the limit is " +
                          std::to_string(limits_.max_attributes));
            ok = false;
        } else if (attributes.payload_size !=
                   std::uint64_t{attribute_count} * kCarPartAttributeSize) {
            add_issue(attributes.offset,
                      chunk_label(attributes) + " payload is " +
                          std::to_string(attributes.payload_size) +
                          " bytes; the header declares " +
                          std::to_string(attribute_count) + " attributes of " +
                          std::to_string(kCarPartAttributeSize));
            ok = false;
        }
        if (!ok) {
            return false;
        }

        database_.attributes.reserve(attribute_count);
        for (std::uint32_t i = 0; i < attribute_count; ++i) {
            const unsigned char* a =
                data_ + attributes.payload_offset + std::uint64_t{i} * kCarPartAttributeSize;
            database_.attributes.push_back(
                CarPartAttribute{read_u32_le(a), read_u32_le(a + 4)});
        }
        database_.records.reserve(record_count);
        for (std::uint32_t i = 0; i < record_count; ++i) {
            parse_record(i, records.payload_offset +
                                std::uint64_t{i} * kCarPartRecordSize,
                         strings, attribute_count);
        }
        for (std::size_t i = 1; i < database_.records.size(); ++i) {
            if (database_.records[i].car_type_hash <
                database_.records[i - 1].car_type_hash) {
                add_issue(database_.records[i].offset,
                          "part record " + std::to_string(i) +
                              " breaks the ascending car type hash order");
                return false;
            }
        }
        return true;
    }

    void parse_record(std::uint32_t index,
                      std::uint64_t offset,
                      const RawChunk& strings,
                      std::uint32_t attribute_count) {
        CarPartRecord record;
        record.index = index;
        record.offset = offset;
        const unsigned char* r = data_ + offset;
        std::copy(r, r + kCarPartRecordSize, record.raw.begin());
        record.name_offset = read_u32_le(r);
        record.car_type_hash = read_u32_le(r + 4);
        record.part_name_hash = read_u32_le(r + 8);
        record.hash_0c = read_u32_le(r + 12);
        record.slot = static_cast<signed char>(r[16]);
        record.upgrade_level = r[17];
        record.byte_12 = r[18];
        record.byte_13 = r[19];
        record.attribute_index = read_u32_le(r + 20);
        record.second_attribute_index = read_u32_le(r + 24);
        record.attribute_count = read_i16_le(r + 28);
        record.second_attribute_count = read_i16_le(r + 30);
        for (std::size_t lod = 0; lod < kCarPartLodCount; ++lod) {
            record.lod_mesh_hashes[lod] = read_u32_le(r + 32 + 4 * lod);
        }

        const std::string label = "part record " + std::to_string(index);
        if (record.name_offset >= strings.payload_size) {
            add_issue(offset, label + " name offset " +
                                  std::to_string(record.name_offset) +
                                  " is outside the string table");
        } else {
            const unsigned char* begin = data_ + strings.payload_offset + record.name_offset;
            const unsigned char* end = data_ + strings.payload_offset + strings.payload_size;
            const unsigned char* nul = std::find(begin, end, static_cast<unsigned char>(0));
            if (nul == end) {
                add_issue(offset, label + " name is not NUL-terminated inside the string table");
            } else {
                // Kept byte for byte; one retail name ends in a TAB.
                record.name.assign(begin, nul);
            }
        }
        if (record.slot < 0 || record.slot >= static_cast<int>(kCarSlotCount)) {
            add_issue(offset + 16, label + " slot id " + std::to_string(record.slot) +
                                       " is outside 0.." +
                                       std::to_string(kCarSlotCount - 1));
        }
        check_attributes(offset + 20, label, record.attribute_index,
                         record.attribute_count, attribute_count);
        check_attributes(offset + 24, label, record.second_attribute_index,
                         record.second_attribute_count, attribute_count);
        database_.records.push_back(std::move(record));
    }

    void check_attributes(std::uint64_t offset,
                          const std::string& label,
                          std::uint32_t first,
                          std::int16_t count,
                          std::uint32_t total) {
        if (count < 0) {
            add_issue(offset, label + " has a negative attribute count");
            return;
        }
        if (count > 0 && (first > total ||
                          static_cast<std::uint32_t>(count) > total - first)) {
            add_issue(offset, label + " attributes " + std::to_string(first) + "+" +
                                  std::to_string(count) +
                                  " are outside the attribute table");
        }
    }

    static constexpr std::array<std::uint32_t, 4> kExpectedChildren{
        kCarPartHeaderId, kCarPartStringTableId, kCarPartAttributeTableId,
        kCarPartRecordTableId};

    const unsigned char* data_;
    std::uint64_t size_;
    const CarPartLimits& limits_;
    CarPartDatabase& database_;
};

// The enable bytes 0x57ea80 derives from a stock part table when no garage
// preview part is set (RideInfo +0x71C is zero after 0x57d160). Slots 6
// and 12 are empty: with a wide body the routine would also install parts.
void derive_enabled(const CarPartDatabase& database,
                    std::array<StockPartSlot, kCarSlotCount>& slots,
                    bool native_installed = false) {
    const auto at = [&](int slot) -> StockPartSlot& {
        return slots[static_cast<std::size_t>(slot)];
    };
    const auto installed = [&](int slot) { return at(slot).record != kNoCarPart; };
    std::array<std::uint32_t, kCarSlotCount> slot_hashes{};
    for (std::size_t i = 0; i < kCarSlotCount; ++i) {
        slot_hashes[i] = bin_hash(kSlotNames[i]);
        slots[i].enabled = true;
    }
    for (int s = 0; s < static_cast<int>(kCarSlotCount); ++s) {
        if (installed(s)) {
            const CarPartRecord& record = database.records[at(s).record];
            for (std::int16_t a = 0; a < record.attribute_count; ++a) {
                const CarPartAttribute& attribute =
                    database.attributes[record.attribute_index +
                                        static_cast<std::uint32_t>(a)];
                if (attribute.key != kDisableSlotAttributeKey) {
                    continue;
                }
                const auto found = std::find(slot_hashes.begin(), slot_hashes.end(),
                                             attribute.value);
                if (found != slot_hashes.end()) {
                    slots[static_cast<std::size_t>(found - slot_hashes.begin())]
                        .enabled = false;
                }
            }
        }
        // The hood rule at 0x57ec29 only changes slot 12, which the stock
        // routine leaves empty, and the wheel rule at 0x57ebbb needs a
        // preview part; neither has an effect here. The wide-body branch
        // of 0x57ed2e is not reproduced: it also installs parts, and the
        // stock routine never fills slot 6.
        if (native_installed && s == 8 && installed(kSlotEngine)) {
            // 0x57EC29 with the native preset and no garage preview part:
            // expose engine only when the hood is absent or disabled.
            at(kSlotEngine).enabled = !installed(8) || !at(8).enabled;
        }
        if (native_installed && s == kSlotWideBody && installed(kSlotWideBody)) {
            // 0x57ED2E..0x57EF7A. Native preset resolution supplies the four
            // original widebody decal records before this enable consumer.
            for (const int slot : {kSlotFrontBumper, kSlotRearBumper, kSlotSkirt, kSlotBody})
                at(slot).enabled = false;
            at(kSlotWideBody).enabled = true;
            for (int i = 0; i < 4; ++i) {
                at(kSlotDecalLeftDoor + i).enabled = false;
                at(kSlotWideBodyDecalLeftDoor + i).enabled = true;
            }
        }
        if (s == kSlotWideBody && !installed(kSlotWideBody)) {
            // 0x57ef81, the branch without a wide body part.
            for (const int slot : {kSlotFrontBumper, kSlotRearBumper, kSlotSkirt,
                                   kSlotBody}) {
                at(slot).enabled = true;
            }
            at(kSlotWideBody).enabled = false;
            for (int i = 0; i < 4; ++i) {
                at(kSlotDecalLeftDoor + i).enabled = true;
                at(kSlotWideBodyDecalLeftDoor + i).enabled = false;
            }
        }
    }
}

} // namespace

const char* car_slot_name(int slot) noexcept {
    if (slot < 0 || slot >= static_cast<int>(kCarSlotCount)) {
        return nullptr;
    }
    return kSlotNames[static_cast<std::size_t>(slot)];
}

const char* stock_part_rule_name(StockPartRule rule) noexcept {
    switch (rule) {
    case StockPartRule::none:
        return "none";
    case StockPartRule::skipped:
        return "skipped";
    case StockPartRule::upgrade_level_0:
        return "level-0";
    case StockPartRule::brakes:
        return "brakes";
    case StockPartRule::plates_fallback:
        return "plates-fallback";
    case StockPartRule::decal_name:
        return "decal-name";
    }
    return "unknown";
}

const char* part_model_source_name(PartModelSource source) noexcept {
    switch (source) {
    case PartModelSource::none:
        return "none";
    case PartModelSource::requested:
        return "requested";
    case PartModelSource::fallback:
        return "fallback";
    }
    return "unknown";
}

std::size_t car_lod_for_pixel_size(float pixel_size, std::size_t minimum_lod) noexcept {
    std::size_t lod = std::min(minimum_lod, kCarPartLodCount - 1);
    while (lod + 1 < kCarPartLodCount && pixel_size < kCarLodPixelSizes[lod]) {
        ++lod;
    }
    // The game's loop at 0x56e0d4 has no bound and a separate clamp at
    // 0x56e0f3 after it. Both agree here: its last threshold is 0.0 and
    // 0x56dc8a already dropped every size below 4, so it can never walk
    // past the last index.
    return lod;
}

bool resolve_part_models(
    const CarPartRecord& record,
    const std::vector<std::uint32_t>& loaded_hashes,
    std::array<PartModel, kCarPartLodCount>& models,
    std::string& error) {
    models = std::array<PartModel, kCarPartLodCount>{};
    for (std::size_t i = 1; i < loaded_hashes.size(); ++i) {
        if (loaded_hashes[i] <= loaded_hashes[i - 1]) {
            error = "loaded mesh hashes must be ascending without duplicates";
            return false;
        }
    }
    const auto loaded = [&loaded_hashes](std::uint32_t hash) {
        return std::binary_search(loaded_hashes.begin(), loaded_hashes.end(), hash);
    };
    // 0x56a95d: the fallback starts at the LOD B hash, even for LOD A.
    std::uint32_t fallback = record.lod_mesh_hashes[1];
    for (std::size_t lod = 0; lod < kCarPartLodCount; ++lod) {
        PartModel& model = models[lod];
        model.requested_hash = record.lod_mesh_hashes[lod];
        if (model.requested_hash == 0) {
            continue;
        }
        if (loaded(model.requested_hash)) {
            model.hash = model.requested_hash;
            model.source = PartModelSource::requested;
            model.resolved = true;
            fallback = model.requested_hash;
            continue;
        }
        if (fallback == 0) {
            continue;
        }
        model.hash = fallback;
        model.source = PartModelSource::fallback;
        model.resolved = loaded(fallback);
    }
    return true;
}

CarPartDatabase parse_car_part_database(
    const unsigned char* data,
    std::size_t size,
    const CarPartLimits& limits) {
    CarPartDatabase database;
    database.file_size = size;
    if (data == nullptr && size != 0) {
        database.issues.push_back(CarPartIssue{0, "no input data"});
        return database;
    }
    if (size > limits.max_file_size) {
        database.issues.push_back(CarPartIssue{
            0, "file is " + std::to_string(size) + " bytes; limit is " +
                   std::to_string(limits.max_file_size)});
        return database;
    }
    try {
        DatabaseParser parser(data, size, limits, database);
        parser.run();
    } catch (const std::exception&) {
        database.records.clear();
        database.attributes.clear();
        database.valid = false;
        database.issues.push_back(CarPartIssue{0, "out of memory while parsing"});
    }
    return database;
}

CarPartDatabase load_car_part_database(
    const std::filesystem::path& path,
    const CarPartLimits& limits) {
    CarPartDatabase database;
    std::error_code size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        database.issues.push_back(CarPartIssue{0, "failed to read file size"});
        return database;
    }
    database.file_size = file_size;
    if (file_size > limits.max_file_size ||
        file_size > std::numeric_limits<std::size_t>::max() ||
        file_size > static_cast<std::uintmax_t>(
                        std::numeric_limits<std::streamsize>::max())) {
        database.issues.push_back(CarPartIssue{
            0, "file is " + std::to_string(file_size) + " bytes; limit is " +
                   std::to_string(limits.max_file_size)});
        return database;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        database.issues.push_back(CarPartIssue{0, "failed to open file"});
        return database;
    }
    std::vector<unsigned char> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(file_size));
    } catch (const std::exception&) {
        database.issues.push_back(CarPartIssue{0, "out of memory while reading file"});
        return database;
    }
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        database.issues.push_back(CarPartIssue{0, "failed to read whole file"});
        return database;
    }
    return parse_car_part_database(bytes.data(), bytes.size(), limits);
}

std::size_t find_car_part(
    const CarPartDatabase& database,
    std::uint32_t car_type_hash,
    int slot,
    std::uint32_t part_name_hash,
    std::size_t after,
    int upgrade_level) noexcept {
    if (!database.ok()) {
        return kNoCarPart;
    }
    const std::vector<CarPartRecord>& records = database.records;
    std::size_t i = 0;
    if (after != kNoCarPart) {
        if (after >= records.size()) {
            return kNoCarPart;
        }
        i = after + 1;
    } else if (car_type_hash != kAnyCarType) {
        // 0x53e400 returns the first record of the equal-key run.
        const auto first = std::lower_bound(
            records.begin(), records.end(), car_type_hash,
            [](const CarPartRecord& record, std::uint32_t hash) {
                return record.car_type_hash < hash;
            });
        if (first == records.end() || first->car_type_hash != car_type_hash) {
            return kNoCarPart;
        }
        i = static_cast<std::size_t>(first - records.begin());
    }
    for (; i < records.size(); ++i) {
        const CarPartRecord& record = records[i];
        if (car_type_hash != kAnyCarType && record.car_type_hash != car_type_hash) {
            return kNoCarPart;
        }
        if (slot != kAnySlot && record.slot != slot) {
            continue;
        }
        if (part_name_hash != kAnyPartName && record.part_name_hash != part_name_hash) {
            continue;
        }
        if (upgrade_level != kAnyUpgradeLevel &&
            static_cast<int>(record.upgrade_level) != upgrade_level) {
            continue;
        }
        return i;
    }
    return kNoCarPart;
}

bool select_stock_parts(
    const CarPartDatabase& database,
    const std::string& car_type_name,
    StockParts& parts,
    std::string& error) {
    parts = StockParts{};
    if (!database.ok()) {
        error = "car part database is not valid";
        return false;
    }
    if (car_type_name.empty() || car_type_name.size() > kMaxCarTypeNameSize ||
        !printable_ascii(car_type_name)) {
        error = "car type name must be 1.." + std::to_string(kMaxCarTypeNameSize) +
                " printable ASCII bytes";
        return false;
    }
    const std::uint32_t car = bin_hash(car_type_name);
    if (find_car_part(database, car, kAnySlot, kAnyPartName, kNoCarPart,
                      kAnyUpgradeLevel) == kNoCarPart) {
        error = "no part record has car type '" + car_type_name + "'";
        return false;
    }

    StockParts result;
    result.car_type_name = car_type_name;
    result.car_type_hash = car;
    for (int slot = 0; slot < static_cast<int>(kCarSlotCount); ++slot) {
        StockPartSlot& entry = result.slots[static_cast<std::size_t>(slot)];
        if ((slot >= kSlotVinylLayer0 && slot <= kSlotVinylLayer3) ||
            slot == kSlotWideBody || slot == kSlotEngine) {
            entry.rule = StockPartRule::skipped;
            continue;
        }
        if (slot == kSlotBrake) {
            entry.record = find_car_part(database, bin_hash(kBrakesCarType), slot,
                                         kAnyPartName, kNoCarPart, 0);
            entry.rule = entry.record == kNoCarPart ? StockPartRule::none
                                                    : StockPartRule::brakes;
            continue;
        }
        entry.record = find_car_part(database, car, slot, kAnyPartName, kNoCarPart, 0);
        if (entry.record != kNoCarPart) {
            entry.rule = StockPartRule::upgrade_level_0;
        } else if (slot == kSlotLicensePlate) {
            entry.record = find_car_part(database, bin_hash(kPlatesCarType), slot,
                                         kAnyPartName, kNoCarPart, kAnyUpgradeLevel);
            if (entry.record != kNoCarPart) {
                entry.rule = StockPartRule::plates_fallback;
            }
        }
    }
    for (const StockDecal& decal : kStockDecals) {
        StockPartSlot& entry = result.slots[static_cast<std::size_t>(decal.slot)];
        entry.record = find_car_part(database, car, decal.slot,
                                     bin_hash(car_type_name + decal.suffix),
                                     kNoCarPart, kAnyUpgradeLevel);
        entry.rule = entry.record == kNoCarPart ? StockPartRule::none
                                                : StockPartRule::decal_name;
    }
    derive_enabled(database, result.slots);
    parts = std::move(result);
    return true;
}

bool select_installed_parts(
    const CarPartDatabase& database,
    const std::string& car_type_name,
    const InstalledPartSelection& selection,
    StockParts& parts,
    std::string& error) {
    if (!select_stock_parts(database, car_type_name, parts, error)) {
        return false;
    }
    for (std::size_t slot = 0; slot < kCarSlotCount; ++slot) {
        if (selection.records[slot]) {
            const auto record = *selection.records[slot];
            if (record != kNoCarPart && record >= database.records.size()) {
                error = "native installed part outside database";
                parts = StockParts{}; return false;
            }
            parts.slots[slot].record = record;
            parts.slots[slot].overridden = true;
        }
        const auto& name = selection.names[slot];
        if (!name) {
            continue;
        }
        const std::string label = std::to_string(slot) + " " + car_slot_name(static_cast<int>(slot));
        if (name->empty() || !printable_ascii(*name)) {
            error = "part override for slot " + label + " must be printable ASCII";
            parts = StockParts{};
            return false;
        }
        const std::uint32_t hash = bin_hash(*name);
        const auto find_for = [&](const char* type) {
            return find_car_part(database, bin_hash(type), static_cast<int>(slot), hash,
                                 kNoCarPart, kAnyUpgradeLevel);
        };
        std::size_t record = hash == kAnyPartName ? kNoCarPart
            : find_for(car_type_name.c_str());
        if (record == kNoCarPart && slot == kSlotWheel) {
            record = find_for("WHEELS");
        } else if (record == kNoCarPart && slot == kSlotBrake) {
            record = find_for(kBrakesCarType);
        } else if (record == kNoCarPart && slot == kSlotLicensePlate) {
            record = find_for(kPlatesCarType);
        }
        if (record == kNoCarPart) {
            const bool other_type = hash != kAnyPartName &&
                find_car_part(database, kAnyCarType, static_cast<int>(slot), hash,
                              kNoCarPart, kAnyUpgradeLevel) != kNoCarPart;
            error = "part override for slot " + label + " names " +
                    (other_type ? "part for another car type '" : "unknown part '") +
                    *name + "'";
            parts = StockParts{};
            return false;
        }
        StockPartSlot& entry = parts.slots[slot];
        entry.record = record;
        entry.overridden = true;
    }
    const bool native_installed = std::any_of(selection.records.begin(),selection.records.end(),
        [](const auto& record) { return record.has_value(); });
    derive_enabled(database, parts.slots, native_installed);
    return true;
}

} // namespace nfsu
