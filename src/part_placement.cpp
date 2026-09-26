#include "nfsu/part_placement.h"

#include "nfsu/chunk.h"

#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace nfsu {
namespace {

constexpr std::uint64_t kHeaderSize = 8;

std::uint32_t read_u32_le(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

// The file stores IEEE-754 binary32 little-endian, which is what every
// platform this project targets uses for float.
float read_f32_le(const unsigned char* bytes) {
    static_assert(sizeof(float) == 4, "float must be 32 bits");
    const std::uint32_t bits = read_u32_le(bytes);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
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

// The size-limit issue for either entry point. `what` names the input the
// caller actually gave, "file" or "buffer", as parse_geometry and
// load_geometry_file distinguish them.
std::string size_limit_message(const char* what,
                               std::uintmax_t size,
                               std::uint64_t limit) {
    return std::string(what) + " is " + std::to_string(size) +
           " bytes; limit is " + std::to_string(limit);
}

struct RawChunk {
    std::uint32_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_size = 0;
};

constexpr std::array<const char*, kCullableCarPartCount> kCullableNames{
    "CULLABLE_CAR_PART_TIRE_FL",
    "CULLABLE_CAR_PART_TIRE_FR",
    "CULLABLE_CAR_PART_TIRE_RR",
    "CULLABLE_CAR_PART_TIRE_RL",
    "CULLABLE_CAR_PART_TIRE_BLURS_LEFT",
    "CULLABLE_CAR_PART_TIRE_BLURS_RIGHT",
    "CULLABLE_CAR_PART_BRAKE_FL",
    "CULLABLE_CAR_PART_BRAKE_FR",
    "CULLABLE_CAR_PART_BRAKE_RR",
    "CULLABLE_CAR_PART_BRAKE_RL",
    "CULLABLE_CAR_PART_SIDE_FRONT",
    "CULLABLE_CAR_PART_SIDE_REAR",
    "CULLABLE_CAR_PART_UNDERNEATH",
    "CULLABLE_CAR_PART_SHADOW"};

// Marker names recovered for the hashes the collector at 0x0056BDB0 tests
// and the hashes that occur in retail geometry. Every name here reproduces
// its hash under nfsu::bin_hash. Ascending by hash so a test can check the
// order and a lookup stays readable.
struct MarkerName {
    std::uint32_t hash;
    const char* name;
    CarMarkerRole role;
};

constexpr std::array<MarkerName, 22> kMarkerNames{{
    {0x02B52399U, "BRAKELIGHT_RIGHT", CarMarkerRole::brakelight},
    {0x05F63788U, "SIDE_MIRROR_LEFT", CarMarkerRole::side_mirror},
    {0x0BEA251BU, "RIGHT_SIDE_MIRROR", CarMarkerRole::side_mirror},
    {0x2F96D0FDU, "RIGHT_REAR_WINDOW", CarMarkerRole::window},
    {0x31A66786U, "LEFT_BRAKELIGHT", CarMarkerRole::brakelight},
    {0x3DF28F69U, "LEFT_FRONT_WINDOW", CarMarkerRole::window},
    {0x3F68F920U, "REAR_WINDOW", CarMarkerRole::window},
    {0x494ED280U, "LICENSE_PLATE_REAR", CarMarkerRole::license_plate},
    {0x64B0BB1CU, "RIGHT_FRONT_WINDOW", CarMarkerRole::window},
    {0x6EC7E79FU, "FRONT_WINDOW", CarMarkerRole::window},
    {0x9DB90133U, "LEFT_HEADLIGHT", CarMarkerRole::headlight},
    {0xA2A2FC7CU, "CENTRE_BRAKELIGHT", CarMarkerRole::brakelight},
    {0xBCF8A18BU, "LEFT_EXHAUST", CarMarkerRole::exhaust},
    {0xBD7CF15EU, "RIGHT_EXHAUST", CarMarkerRole::exhaust},
    {0xBF700A79U, "RIGHT_BRAKELIGHT", CarMarkerRole::brakelight},
    {0xC4EC23C8U, "LEFT_SIDE_MIRROR", CarMarkerRole::side_mirror},
    {0xC52BF01BU, "SIDE_MIRROR_RIGHT", CarMarkerRole::side_mirror},
    {0xC805F7E0U, "REAR_LICENSE_PLATE", CarMarkerRole::license_plate},
    {0xD09091C6U, "RIGHT_HEADLIGHT", CarMarkerRole::headlight},
    {0xD947F346U, "BRAKELIGHT_LEFT", CarMarkerRole::brakelight},
    {0xDF066A41U, "CENTRE_EXHAUST", CarMarkerRole::exhaust},
    {0xE898CFAAU, "LEFT_REAR_WINDOW", CarMarkerRole::window},
}};

// Two more the collector accepts that are not in the ascending table above
// because they must stay grouped with their role; kept separate so the
// table's ordering invariant is simple to test.
constexpr std::array<MarkerName, 2> kExtraMarkerNames{{
    {0xEA8DFB05U, "MARKER_BRAKE", CarMarkerRole::wheel_brake},
    {0xF21BE91EU, "EXHAUST_RIGHT", CarMarkerRole::exhaust},
}};

const MarkerName* find_marker_name(std::uint32_t hash) noexcept {
    for (const MarkerName& entry : kMarkerNames) {
        if (entry.hash == hash) {
            return &entry;
        }
    }
    for (const MarkerName& entry : kExtraMarkerNames) {
        if (entry.hash == hash) {
            return &entry;
        }
    }
    return nullptr;
}

class TableParser {
public:
    TableParser(const unsigned char* data,
                std::uint64_t size,
                const CarTypeLimits& limits,
                CarTypeTable& table)
        : data_(data), size_(size), limits_(limits), table_(table) {}

    void run() {
        std::vector<RawChunk> top;
        if (!list_top_level(top)) {
            return;
        }
        std::vector<RawChunk> roots;
        for (const RawChunk& chunk : top) {
            if (chunk.id == kCarTypeInfoId) {
                roots.push_back(chunk);
            }
        }
        if (roots.size() != 1) {
            add_issue(roots.empty() ? 0 : roots[1].offset,
                      "expected exactly one top-level " +
                          format_chunk_id(kCarTypeInfoId) +
                          " car type table, found " +
                          std::to_string(roots.size()));
            return;
        }
        table_.chunk_offset = roots.front().offset;
        if (parse_table(roots.front())) {
            table_.valid = table_.issues.empty();
        }
    }

private:
    void add_issue(std::uint64_t offset, std::string message) {
        table_.issues.push_back(CarTypeIssue{offset, std::move(message)});
    }

    bool list_top_level(std::vector<RawChunk>& out) {
        std::uint64_t offset = 0;
        while (offset < size_) {
            if (size_ - offset < kHeaderSize) {
                add_issue(offset, "top level: " + std::to_string(size_ - offset) +
                                      " trailing bytes are smaller than a chunk header");
                return false;
            }
            if (out.size() >= limits_.max_top_level_chunks) {
                add_issue(offset, "top level: chunk limit " +
                                      std::to_string(limits_.max_top_level_chunks) +
                                      " exceeded");
                return false;
            }
            RawChunk chunk;
            chunk.offset = offset;
            chunk.id = read_u32_le(data_ + offset);
            chunk.payload_size = read_u32_le(data_ + offset + 4);
            chunk.payload_offset = offset + kHeaderSize;
            if (chunk.payload_size > size_ - chunk.payload_offset) {
                add_issue(offset, "top level: chunk " + format_chunk_id(chunk.id) +
                                      " at offset " + std::to_string(chunk.offset) +
                                      " declares " + std::to_string(chunk.payload_size) +
                                      " payload bytes but only " +
                                      std::to_string(size_ - chunk.payload_offset) +
                                      " remain in the file");
                return false;
            }
            out.push_back(chunk);
            offset = chunk.payload_offset + chunk.payload_size;
        }
        return true;
    }

    bool parse_table(const RawChunk& root) {
        // 0x57c8c2 and 0x57c8c5 align the payload up to 16 bytes. The
        // alignment is relative to the start of the file, as it is for the
        // geometry chunks.
        const std::uint64_t payload_end = root.payload_offset + root.payload_size;
        const std::uint64_t aligned =
            (root.payload_offset + kCarTypeInfoAlignment - 1) /
            kCarTypeInfoAlignment * kCarTypeInfoAlignment;
        if (aligned > payload_end) {
            add_issue(root.payload_offset,
                      "car type table: the aligned record array starts at offset " +
                          std::to_string(aligned) + ", past the payload end " +
                          std::to_string(payload_end));
            return false;
        }
        for (std::uint64_t i = root.payload_offset; i < aligned; ++i) {
            if (data_[i] != kCarTypeInfoFillerByte) {
                add_issue(i, "car type table: alignment filler byte is 0x" +
                                 hex_byte(data_[i]) + ", expected 0x" +
                                 hex_byte(kCarTypeInfoFillerByte));
                return false;
            }
        }
        const std::uint64_t needed =
            static_cast<std::uint64_t>(kCarTypeInfoRecordCount) * kCarTypeInfoRecordSize;
        if (payload_end - aligned < needed) {
            add_issue(aligned,
                      "car type table: " + std::to_string(payload_end - aligned) +
                          " bytes after alignment cannot hold " +
                          std::to_string(kCarTypeInfoRecordCount) + " records of " +
                          std::to_string(kCarTypeInfoRecordSize) + " bytes");
            return false;
        }
        table_.record_offset = aligned;
        table_.records.reserve(kCarTypeInfoRecordCount);
        for (std::size_t i = 0; i < kCarTypeInfoRecordCount; ++i) {
            CarTypeRecord record;
            record.index = i;
            record.offset = aligned + static_cast<std::uint64_t>(i) * kCarTypeInfoRecordSize;
            if (!parse_record(data_ + record.offset, record)) {
                return false;
            }
            table_.records.push_back(std::move(record));
        }
        return true;
    }

    static std::string hex_byte(unsigned char value) {
        const char* digits = "0123456789ABCDEF";
        std::string out;
        out += digits[(value >> 4U) & 0xFU];
        out += digits[value & 0xFU];
        return out;
    }

    // Reads one NUL-terminated field bounded by kCarTypeStringFieldSize.
    bool read_string_field(const unsigned char* record,
                           std::uint64_t record_offset,
                           std::size_t field_offset,
                           const char* what,
                           std::string& out) {
        const unsigned char* field = record + field_offset;
        std::size_t length = 0;
        while (length < kCarTypeStringFieldSize && field[length] != 0) {
            ++length;
        }
        if (length == kCarTypeStringFieldSize) {
            add_issue(record_offset + field_offset,
                      std::string("car type record: ") + what + " is not terminated " +
                          "within " + std::to_string(kCarTypeStringFieldSize) + " bytes");
            return false;
        }
        out.assign(reinterpret_cast<const char*>(field), length);
        if (out.empty()) {
            add_issue(record_offset + field_offset,
                      std::string("car type record: ") + what + " is empty");
            return false;
        }
        if (!printable_ascii(out)) {
            add_issue(record_offset + field_offset,
                      std::string("car type record: ") + what +
                          " holds a byte outside printable ASCII");
            return false;
        }
        return true;
    }

    bool parse_record(const unsigned char* r, CarTypeRecord& record) {
        if (!read_string_field(r, record.offset, kCarTypeNameOffset, "name",
                               record.name) ||
            !read_string_field(r, record.offset, kCarTypeLookupNameOffset,
                               "lookup name", record.lookup_name) ||
            !read_string_field(r, record.offset, kCarTypeGeometryPathOffset,
                               "geometry path", record.geometry_path) ||
            !read_string_field(r, record.offset, kCarTypeCompressedPathOffset,
                               "compressed geometry path", record.compressed_path) ||
            !read_string_field(r, record.offset, kCarTypeBrandNameOffset,
                               "brand name", record.brand_name)) {
            return false;
        }
        // The decal helpers format +0x00 and the part lookup hashes +0x20
        // (docs/CAR_SCENE.md). They are equal in all 35 retail records, and
        // select_stock_parts already relies on that, so a mismatch is
        // refused rather than silently resolved one way.
        if (record.name != record.lookup_name) {
            add_issue(record.offset,
                      "car type record: name '" + record.name +
                          "' and lookup name '" + record.lookup_name +
                          "' differ; the stock-part rules assume they are equal");
            return false;
        }
        record.class_id = read_u32_le(r + kCarTypeClassOffset);
        for (std::size_t w = 0; w < kCarWheelCount; ++w) {
            const std::size_t field = kCarWheelTableOffset + w * kCarWheelRecordSize;
            if (!parse_wheel(r + field, record.offset + field, w, record.wheels[w])) {
                return false;
            }
        }
        return true;
    }

    bool parse_wheel(const unsigned char* w,
                     std::uint64_t offset,
                     std::size_t position,
                     CarWheelPlacement& wheel) {
        wheel.offset = offset;
        std::memcpy(wheel.raw.data(), w, kCarWheelRecordSize);
        wheel.longitudinal = read_f32_le(w + 0x00);
        wheel.lateral = read_f32_le(w + 0x04);
        wheel.vertical = read_f32_le(w + 0x08);
        wheel.reserved_0c = read_u32_le(w + 0x0C);
        wheel.rolling_radius = read_f32_le(w + 0x10);
        wheel.value_14 = read_f32_le(w + 0x14);
        wheel.index = read_u32_le(w + 0x18);
        wheel.value_1c = read_f32_le(w + 0x1C);
        wheel.value_20 = read_f32_le(w + 0x20);
        for (std::size_t i = 0; i < wheel.reserved_24.size(); ++i) {
            wheel.reserved_24[i] =
                read_u32_le(w + 0x24 + 4 * i);
        }
        // 0x5693d0 is called with a literal index per wheel, and the stored
        // value equals the record position in all 140 retail wheel records.
        // A mismatch would mean the array is not the one the code indexes.
        if (wheel.index != position) {
            add_issue(offset + 0x18,
                      "wheel record " + std::to_string(position) + ": stored index " +
                          std::to_string(wheel.index) + " does not equal its position");
            return false;
        }
        if (wheel.reserved_0c != 0) {
            add_issue(offset + 0x0C, "wheel record " + std::to_string(position) +
                                         ": +0x0C is not zero");
            return false;
        }
        for (std::size_t i = 0; i < wheel.reserved_24.size(); ++i) {
            if (wheel.reserved_24[i] != 0) {
                add_issue(offset + 0x24 + 4 * i,
                          "wheel record " + std::to_string(position) + ": +0x" +
                              hex_byte(static_cast<unsigned char>(0x24 + 4 * i)) +
                              " is not zero");
                return false;
            }
        }
        // All seven floats of the record have a traced reader, so all seven
        // are required to be finite: +0x00, +0x04 and +0x08 reach the
        // placement array through 0x5693d0, +0x10 the shadow offset at
        // 0x56b3f5, and +0x14, +0x1C and +0x20 the wheel draw through
        // 0x56c283, 0x56c267 and 0x56c275.
        const float values[7] = {wheel.longitudinal, wheel.lateral,
                                 wheel.vertical,     wheel.rolling_radius,
                                 wheel.value_14,     wheel.value_1c,
                                 wheel.value_20};
        for (const float value : values) {
            if (!std::isfinite(value)) {
                add_issue(offset, "wheel record " + std::to_string(position) +
                                      ": a position or size field is not finite");
                return false;
            }
        }
        return true;
    }

    const unsigned char* data_;
    std::uint64_t size_;
    CarTypeLimits limits_;
    CarTypeTable& table_;
};

void copy_wheel(const CarWheelPlacement& wheel, PartPlacement& out, std::size_t index) {
    out.position[0] = wheel.longitudinal;
    out.position[1] = wheel.lateral;
    out.position[2] = wheel.vertical;
    out.source = PlacementSource::wheel;
    out.index_space = PlacementIndexSpace::wheel;
    out.sources[0] = index;
    out.source_count = 1;
}

// 0x56b1d8 and 0x56b228 add the two positions and scale each component by
// the 0.5 at 0x6cc7dc.
void mean_of(const float a[3], const float b[3], float out[3]) {
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = (a[i] + b[i]) * 0.5F;
    }
}

void difference(const float a[3], const float b[3], float out[3]) {
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = a[i] - b[i];
    }
}

// 0x56b36a..0x56b392, component by component.
void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

} // namespace

const char* cullable_car_part_name(CullableCarPart part) noexcept {
    const auto index = static_cast<std::size_t>(part);
    if (index >= kCullableNames.size()) {
        return nullptr;
    }
    return kCullableNames[index];
}

const char* placement_index_space_name(PlacementIndexSpace space) noexcept {
    switch (space) {
    case PlacementIndexSpace::wheel:
        return "wheels";
    case PlacementIndexSpace::placement:
        return "placements";
    }
    return "unknown";
}

bool placement_wheel_sources(const PartPlacement& placement,
                             std::array<std::size_t, 3>& wheels,
                             std::size_t& count) noexcept {
    if (placement.index_space != PlacementIndexSpace::wheel) {
        return false;
    }
    // The count is checked before it is used as a bound, because this takes
    // a PartPlacement by reference and a caller may hand it one it built
    // itself rather than one derive_cullable_part_placements produced.
    if (placement.source_count > placement.sources.size()) {
        return false;
    }
    // Only the first source_count entries mean anything, so only those are
    // checked and copied; the rest of the output is zeroed rather than taken
    // from the input. Copying the whole array would hand back entries this
    // never validated, and a caller that iterated the array instead of the
    // count would then be back to indexing the wheel table out of range.
    std::array<std::size_t, 3> checked{};
    for (std::size_t i = 0; i < placement.source_count; ++i) {
        if (placement.sources[i] >= kCarWheelCount) {
            // Unreachable for a placement this translation unit built; the
            // guard keeps the promise in the header true for any other.
            return false;
        }
        checked[i] = placement.sources[i];
    }
    wheels = checked;
    count = placement.source_count;
    return true;
}

const char* placement_source_name(PlacementSource source) noexcept {
    switch (source) {
    case PlacementSource::wheel:
        return "wheel";
    case PlacementSource::wheel_mean:
        return "wheel-mean";
    case PlacementSource::part_mean:
        return "part-mean";
    case PlacementSource::plane_offset:
        return "plane-offset";
    }
    return "unknown";
}

CarTypeTable parse_car_type_table(
    const unsigned char* data,
    std::size_t size,
    const CarTypeLimits& limits) {
    CarTypeTable table;
    table.file_size = size;
    if (data == nullptr || size == 0) {
        table.issues.push_back(CarTypeIssue{0, "empty input"});
        return table;
    }
    // The same bound load_car_type_table applies before it reads a file.
    // This entry point is public and takes an image the caller obtained some
    // other way, so it has to enforce the limit itself; otherwise a lowered
    // max_file_size is silently ignored for every in-memory caller.
    if (static_cast<std::uint64_t>(size) > limits.max_file_size) {
        table.issues.push_back(CarTypeIssue{
            0, size_limit_message("buffer", size, limits.max_file_size)});
        return table;
    }
    TableParser parser(data, static_cast<std::uint64_t>(size), limits, table);
    parser.run();
    return table;
}

CarTypeTable load_car_type_table(
    const std::filesystem::path& path,
    const CarTypeLimits& limits) {
    CarTypeTable table;
    std::error_code size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        table.issues.push_back(CarTypeIssue{0, "failed to read file size"});
        return table;
    }
    table.file_size = file_size;
    if (file_size > limits.max_file_size ||
        file_size > std::numeric_limits<std::size_t>::max() ||
        file_size > static_cast<std::uintmax_t>(
                        std::numeric_limits<std::streamsize>::max())) {
        table.issues.push_back(CarTypeIssue{
            0, size_limit_message("file", file_size, limits.max_file_size)});
        return table;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        table.issues.push_back(CarTypeIssue{0, "failed to open file"});
        return table;
    }
    std::vector<unsigned char> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(file_size));
    } catch (const std::exception&) {
        table.issues.push_back(CarTypeIssue{0, "out of memory while reading file"});
        return table;
    }
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        table.issues.push_back(CarTypeIssue{0, "failed to read whole file"});
        return table;
    }
    return parse_car_type_table(bytes.data(), bytes.size(), limits);
}

std::size_t find_car_type(const CarTypeTable& table,
                          const std::string& car_type_name) noexcept {
    if (!table.valid) {
        return kNoCarType;
    }
    for (const CarTypeRecord& record : table.records) {
        if (record.name == car_type_name) {
            return record.index;
        }
    }
    return kNoCarType;
}

bool derive_cullable_part_placements(
    const std::array<CarWheelPlacement, kCarWheelCount>& wheels,
    std::array<PartPlacement, kCullableCarPartCount>& placements,
    std::string& error) {
    placements = std::array<PartPlacement, kCullableCarPartCount>{};
    error.clear();
    for (std::size_t w = 0; w < kCarWheelCount; ++w) {
        const float values[4] = {wheels[w].longitudinal, wheels[w].lateral,
                                 wheels[w].vertical, wheels[w].rolling_radius};
        for (const float value : values) {
            if (!std::isfinite(value)) {
                error = "wheel " + std::to_string(w) +
                        " has a position or radius that is not finite";
                return false;
            }
        }
    }

    // 0x56b18d, 0x56b19b, 0x56b1b2, 0x56b1c8.
    copy_wheel(wheels[0], placements[0], 0);
    copy_wheel(wheels[1], placements[1], 1);
    copy_wheel(wheels[2], placements[2], 2);
    copy_wheel(wheels[3], placements[3], 3);

    // 0x56b1d8: the left pair, wheels 3 and 0 in that read order.
    PartPlacement& blurs_left = placements[4];
    mean_of(placements[3].position, placements[0].position, blurs_left.position);
    blurs_left.source = PlacementSource::wheel_mean;
    blurs_left.index_space = PlacementIndexSpace::wheel;
    blurs_left.sources[0] = 3;
    blurs_left.sources[1] = 0;
    blurs_left.source_count = 2;

    // 0x56b228: the right pair, wheels 2 and 1 in that read order.
    PartPlacement& blurs_right = placements[5];
    mean_of(placements[2].position, placements[1].position, blurs_right.position);
    blurs_right.source = PlacementSource::wheel_mean;
    blurs_right.index_space = PlacementIndexSpace::wheel;
    blurs_right.sources[0] = 2;
    blurs_right.sources[1] = 1;
    blurs_right.source_count = 2;

    // 0x56b27b, 0x56b289, 0x56b294, 0x56b2aa: the brakes reuse the wheels.
    copy_wheel(wheels[0], placements[6], 0);
    copy_wheel(wheels[1], placements[7], 1);
    copy_wheel(wheels[2], placements[8], 2);
    copy_wheel(wheels[3], placements[9], 3);

    // 0x56b2c0 and 0x56b2ce.
    copy_wheel(wheels[0], placements[10], 0);
    copy_wheel(wheels[2], placements[11], 2);

    // 0x56b2e4: the mean of the two blur positions, so the centroid of the
    // four wheels.
    PartPlacement& underneath = placements[12];
    mean_of(blurs_right.position, blurs_left.position, underneath.position);
    underneath.source = PlacementSource::part_mean;
    // 5 and 4 are placements, not wheels: this is the one entry the
    // constructor builds from its own earlier output, and there is no wheel
    // 4 or 5 to index.
    underneath.index_space = PlacementIndexSpace::placement;
    underneath.sources[0] = 5;
    underneath.sources[1] = 4;
    underneath.source_count = 2;

    // 0x56b33c..0x56b44x: the normal of the plane through wheels 0, 1 and 2,
    // normalized, scaled by wheel 0's radius and added to the centroid.
    float a[3];
    float b[3];
    difference(placements[0].position, placements[1].position, a);
    difference(placements[1].position, placements[2].position, b);
    float normal[3];
    cross(a, b, normal);
    const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                                   normal[2] * normal[2]);
    PartPlacement& shadow = placements[13];
    shadow.source = PlacementSource::plane_offset;
    // The offset is applied to placement 12, but the sources named here are
    // the three wheels whose plane gives the normal, so this stays in the
    // wheel index space.
    shadow.index_space = PlacementIndexSpace::wheel;
    shadow.sources[0] = 0;
    shadow.sources[1] = 1;
    shadow.sources[2] = 2;
    shadow.source_count = 3;
    if (length > kWheelPlaneMinimumLength) {
        const float scale = 1.0F / length;
        for (std::size_t i = 0; i < 3; ++i) {
            normal[i] *= scale;
        }
    } else {
        // 0x56b3e2 clears the third component, then 0x56b3e9 and 0x56b3ef
        // push 1.0 and 0.0. The normalizing path reaches the join at
        // 0x56b3f5 with the second component on top of the first, so the
        // 1.0 is the first component; 0x56b40a confirms it by multiplying
        // st(2), the first component, by the radius into the first output.
        shadow.degenerate_plane = true;
        for (std::size_t i = 0; i < 3; ++i) {
            normal[i] = kDegenerateWheelPlaneNormal[i];
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        shadow.position[i] = underneath.position[i] + normal[i] * wheels[0].rolling_radius;
    }

    // A finite but enormous wheel table can still overflow the differences,
    // the cross product or the means. The game would propagate the result;
    // this refuses it so no caller consumes an infinity or a NaN.
    for (std::size_t p = 0; p < placements.size(); ++p) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(placements[p].position[axis])) {
                error = "placement " + std::to_string(p) +
                        " is not finite; the wheel positions are too large";
                placements = std::array<PartPlacement, kCullableCarPartCount>{};
                return false;
            }
        }
        // Every source index must be in range for the space it was written
        // in. The indices above are literals, so this can only fire if one
        // of them is edited into the wrong space; catching it here keeps a
        // placement index from ever leaving this function as a wheel index.
        const PartPlacement& entry = placements[p];
        const std::size_t bound = entry.index_space == PlacementIndexSpace::wheel
                                      ? kCarWheelCount
                                      : kCullableCarPartCount;
        if (entry.source_count > entry.sources.size()) {
            error = "placement " + std::to_string(p) + " names " +
                    std::to_string(entry.source_count) + " sources; at most " +
                    std::to_string(entry.sources.size()) + " fit";
            placements = std::array<PartPlacement, kCullableCarPartCount>{};
            return false;
        }
        for (std::size_t i = 0; i < entry.source_count; ++i) {
            if (entry.sources[i] >= bound) {
                error = "placement " + std::to_string(p) + " source " +
                        std::to_string(i) + " is " +
                        std::to_string(entry.sources[i]) + ", outside the " +
                        std::to_string(bound) + "-entry " +
                        placement_index_space_name(entry.index_space) + " table";
                placements = std::array<PartPlacement, kCullableCarPartCount>{};
                return false;
            }
        }
    }
    return true;
}

const char* car_marker_role_name(CarMarkerRole role) noexcept {
    switch (role) {
    case CarMarkerRole::exhaust:
        return "exhaust";
    case CarMarkerRole::license_plate:
        return "license-plate";
    case CarMarkerRole::brakelight:
        return "brakelight";
    case CarMarkerRole::headlight:
        return "headlight";
    case CarMarkerRole::window:
        return "window";
    case CarMarkerRole::side_mirror:
        return "side-mirror";
    case CarMarkerRole::wheel_brake:
        return "wheel-brake";
    case CarMarkerRole::unknown:
        return "unknown";
    }
    return "unknown";
}

const char* car_marker_name(std::uint32_t hash) noexcept {
    const MarkerName* entry = find_marker_name(hash);
    return entry == nullptr ? nullptr : entry->name;
}

CarMarkerRole car_marker_role(std::uint32_t hash) noexcept {
    const MarkerName* entry = find_marker_name(hash);
    return entry == nullptr ? CarMarkerRole::unknown : entry->role;
}

std::size_t car_marker_name_count() noexcept {
    return kMarkerNames.size() + kExtraMarkerNames.size();
}

bool car_marker_name_at(std::size_t index,
                        std::uint32_t& hash,
                        const char*& name,
                        CarMarkerRole& role) noexcept {
    const MarkerName* entry = nullptr;
    if (index < kMarkerNames.size()) {
        entry = &kMarkerNames[index];
    } else if (index < car_marker_name_count()) {
        entry = &kExtraMarkerNames[index - kMarkerNames.size()];
    } else {
        return false;
    }
    hash = entry->hash;
    name = entry->name;
    role = entry->role;
    return true;
}

void car_marker_quarter_turn(float out[16]) noexcept {
    // 0x0040BA71 and 0x0040BA7D store the cosine into elements 0 and 10,
    // 0x0040BA69 stores -sin into element 2 and 0x0040BA75 stores +sin into
    // element 8. At a quarter turn cos is 0 and sin is 1 exactly.
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = 0.0F;
    }
    out[0] = 0.0F;
    out[2] = -1.0F;
    out[5] = 1.0F;
    out[8] = 1.0F;
    out[10] = 0.0F;
    out[15] = 1.0F;
}

void multiply_placement_matrix(const float a[16],
                               const float b[16],
                               float out[16]) noexcept {
    // 0x005AC6AE..0x005AC6BC compares the destination against both sources
    // and accumulates into a local when it aliases either one, so the game's
    // own multiply is alias-safe. This one always does, which costs one
    // 64-byte copy and removes the footgun.
    float result[16];
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += a[row * 4 + k] * b[k * 4 + column];
            }
            result[row * 4 + column] = sum;
        }
    }
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = result[i];
    }
}

bool car_marker_placement(const MeshMarker& marker, float out[16]) noexcept {
    for (std::size_t i = 0; i < 16; ++i) {
        if (!std::isfinite(marker.matrix[i])) {
            return false;
        }
    }
    float turn[16];
    car_marker_quarter_turn(turn);
    multiply_placement_matrix(turn, marker.matrix, out);
    return true;
}

bool car_wheel_turned_by_draw(std::size_t wheel) noexcept {
    // The wheel draw multiplies the half turn in for the wheels whose
    // visibility bytes it read from CarRenderInfo +0x1C18 and +0x1C78
    // (0x0056FEA9, 0x00570138), and the brake draw the reflection for the
    // brakes of +0x1CD8 and +0x1D38 (0x00570446, 0x0057069E): culling
    // entries 0 and 3, and 6 and 9, which are wheel positions 0 and 3.
    return wheel == 0 || wheel == 3;
}

void car_wheel_half_turn(float out[16]) noexcept {
    // 0x0040B947 stores +sin into element 1 and 0x0040B954 -sin into
    // element 4; 0x0040B957 and 0x0040B95B store the cosine into elements
    // 0 and 5; 0x0040B975 and 0x0040B984 store 1 into elements 10 and 15,
    // and the stores between them zero the rest. At a half turn cos is -1
    // and sin is 0.
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = 0.0F;
    }
    out[0] = -1.0F;
    out[5] = -1.0F;
    out[10] = 1.0F;
    out[15] = 1.0F;
}

void car_brake_reflection(float out[16]) noexcept {
    // 0x00570431, 0x00570435 and 0x00570439 store the floats of 0x006B5DDC,
    // 0x006B5DE0 and 0x006B5DE4 into elements 0, 5 and 10; 0x00570426
    // stores 1.0 into element 15, and the twelve stores before it zero the
    // rest.
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = 0.0F;
    }
    out[0] = 1.0F;
    out[5] = -1.0F;
    out[10] = 1.0F;
    out[15] = 1.0F;
}

} // namespace nfsu
