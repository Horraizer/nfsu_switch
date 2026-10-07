#include "nfsu/race_events.h"

#include "nfsu/texture.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <locale>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace nfsu {
bool load_supported_race(const std::filesystem::path& front,
                         const std::filesystem::path& global,
                         std::uint32_t number, SupportedRace& race,
                         std::string& error) {
    race = {}; error.clear();
    if (number != 1 && number != 8) {
        error = "supported events are 1 (1003) and 8 (1005)"; return false;
    }
    std::vector<unsigned char> bytes;
    const auto events = load_race_event_table(front, bytes);
    const auto tracks = load_track_info_table(global, bytes);
    const auto* event = find_race_event(events, number);
    if (!events.ok() || !tracks.ok() || !event || event->races.size() != 1 ||
        event->type != static_cast<std::uint32_t>(RaceType::circuit)) {
        error = "supported event must be one valid circuit race"; return false;
    }
    const auto& slot = event->races.front();
    const auto* info = find_track_info(tracks, slot.track);
    if (!info || slot.track != (number == 1 ? 1003 : 1005) ||
        slot.laps != (number == 1 ? 2 : 3) || slot.direction != 0 ||
        info->region != "L1RA" || info->lap_skip != 0) {
        error = "supported circuit differs from its retail selection"; return false;
    }
    race.event = number; race.slot = slot; race.track = *info;
    return true;
}
namespace {

constexpr std::uint32_t kContainerBit = 0x80000000U;
constexpr std::size_t kChunkHeaderSize = 8;

std::uint16_t read_u16(const unsigned char* bytes) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<unsigned int>(bytes[0]) |
        (static_cast<unsigned int>(bytes[1]) << 8U));
}

std::int16_t read_s16(const unsigned char* bytes) noexcept {
    return static_cast<std::int16_t>(read_u16(bytes));
}

std::uint32_t read_u32(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::int8_t read_s8(const unsigned char* bytes) noexcept {
    return static_cast<std::int8_t>(bytes[0]);
}

// Locale-independent text for diagnostics, as src/chunk.cpp formats ids.
class Message {
public:
    Message() { stream_.imbue(std::locale::classic()); }

    template <typename T>
    Message& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

    std::string str() const { return stream_.str(); }

private:
    std::ostringstream stream_;
};

std::string hex_id(std::uint32_t id) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text.push_back(digits[(id >> static_cast<unsigned int>(shift)) & 0xFU]);
    }
    return text;
}

struct ChunkSpan {
    std::uint32_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_size = 0;
};

// The chunks directly inside [begin, end), which must tile it exactly.
// Returns false, with an issue, on the first chunk that does not fit.
bool list_chunks(const unsigned char* data,
                 std::uint64_t begin,
                 std::uint64_t end,
                 std::size_t max_chunks,
                 std::vector<ChunkSpan>& chunks,
                 std::vector<RaceTableIssue>& issues) {
    std::uint64_t offset = begin;
    while (offset < end) {
        if (end - offset < kChunkHeaderSize) {
            issues.push_back(RaceTableIssue{
                offset, "trailing bytes are smaller than a chunk header"});
            return false;
        }
        if (chunks.size() >= max_chunks) {
            issues.push_back(RaceTableIssue{offset, "too many chunks"});
            return false;
        }
        const std::uint32_t id = read_u32(data + offset);
        const std::uint64_t size = read_u32(data + offset + 4);
        const std::uint64_t payload = offset + kChunkHeaderSize;
        if (size > end - payload) {
            issues.push_back(RaceTableIssue{
                offset, "chunk " + hex_id(id) + " extends beyond its parent"});
            return false;
        }
        chunks.push_back(ChunkSpan{id, offset, payload, size});
        offset = payload + size;
    }
    return true;
}

// The one chunk with `id` among `chunks`, or nullptr with an issue.
const ChunkSpan* single_chunk(const std::vector<ChunkSpan>& chunks,
                              std::uint32_t id,
                              std::uint64_t where,
                              std::vector<RaceTableIssue>& issues) {
    const ChunkSpan* found = nullptr;
    std::size_t count = 0;
    for (const ChunkSpan& chunk : chunks) {
        if (chunk.id == id) {
            if (found == nullptr) {
                found = &chunk;
            }
            ++count;
        }
    }
    if (count != 1) {
        issues.push_back(RaceTableIssue{
            count == 0 ? where : found->offset,
            (Message() << "expected exactly one chunk " << hex_id(id)
                       << ", found " << count)
                .str()});
        return nullptr;
    }
    return found;
}

bool printable_ascii(const std::string& text) noexcept {
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U || byte > 0x7EU) {
            return false;
        }
    }
    return true;
}

// Reads the file whole, decompressing it when it opens with a JDLZ header.
// Returns false with an issue when the file cannot be read or decoded.
bool read_image(const std::filesystem::path& path,
                const RaceTableLimits& limits,
                std::vector<unsigned char>& image,
                std::uint64_t& stored_size,
                bool& jdlz,
                std::vector<RaceTableIssue>& issues) {
    image.clear();
    stored_size = 0;
    jdlz = false;

    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        issues.push_back(
            RaceTableIssue{0, "cannot size the file: " + code.message()});
        return false;
    }
    stored_size = static_cast<std::uint64_t>(size);
    if (stored_size > limits.max_file_size) {
        issues.push_back(RaceTableIssue{
            0, (Message() << "file is " << stored_size
                          << " bytes, over the limit of " << limits.max_file_size)
                   .str()});
        return false;
    }

    std::vector<unsigned char> stored;
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            issues.push_back(RaceTableIssue{0, "cannot open the file"});
            return false;
        }
        stored.resize(static_cast<std::size_t>(stored_size));
        if (stored_size != 0 &&
            !stream.read(reinterpret_cast<char*>(stored.data()),
                         static_cast<std::streamsize>(stored_size))) {
            issues.push_back(RaceTableIssue{0, "cannot read the file"});
            return false;
        }
    } catch (const std::exception& error) {
        issues.push_back(
            RaceTableIssue{0, std::string("cannot read the file: ") + error.what()});
        return false;
    }

    // The same sniff src/track_zones.cpp and src/track_collision.cpp do, and
    // the same decoder: this file adds no decompressor of its own.
    if (stored.size() >= 4 && stored[0] == 'J' && stored[1] == 'D' &&
        stored[2] == 'L' && stored[3] == 'Z') {
        jdlz = true;
        std::size_t unused_input = 0;
        std::string error;
        const auto cap = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(limits.max_image_size, 0xFFFFFFFFULL));
        if (!jdlz_decompress(stored.data(), stored.size(), cap, image,
                             unused_input, error)) {
            issues.push_back(RaceTableIssue{0, "JDLZ: " + error});
            return false;
        }
    } else {
        image = std::move(stored);
    }
    return true;
}

// The prefix every parser shares: the size limit and the top-level tiling.
bool top_level(const unsigned char* data,
               std::size_t size,
               const RaceTableLimits& limits,
               std::vector<ChunkSpan>& chunks,
               std::vector<RaceTableIssue>& issues) {
    if (data == nullptr) {
        issues.push_back(RaceTableIssue{0, "no image bytes"});
        return false;
    }
    if (static_cast<std::uint64_t>(size) > limits.max_image_size) {
        issues.push_back(RaceTableIssue{
            0, (Message() << "image is " << size << " bytes, over the limit of "
                          << limits.max_image_size)
                   .str()});
        return false;
    }
    return list_chunks(data, 0, size, limits.max_top_level_chunks, chunks, issues);
}

} // namespace

bool load_race_image(const std::filesystem::path& path,
                     std::vector<unsigned char>& image,
                     std::vector<RaceTableIssue>& issues,
                     const RaceTableLimits& limits) {
    std::uint64_t stored_size = 0;
    bool jdlz = false;
    if (!read_image(path, limits, image, stored_size, jdlz, issues)) return false;
    if (image.size() > limits.max_image_size) {
        issues.push_back({0, "image exceeds setup limit"});
        image.clear();
        return false;
    }
    return true;
}

bool find_race_chunk(const unsigned char* data, std::size_t size,
                     std::uint32_t id, RaceChunkPayload& payload,
                     std::vector<RaceTableIssue>& issues,
                     const RaceTableLimits& limits) {
    std::vector<ChunkSpan> roots;
    if (!top_level(data, size, limits, roots, issues)) return false;
    struct Pending { ChunkSpan span; unsigned depth; };
    std::vector<Pending> pending;
    for (const auto& root : roots) pending.push_back({root, 0});
    std::size_t count = roots.size(), matches = 0;
    RaceChunkPayload found;
    while (!pending.empty()) {
        const auto next = pending.back(); pending.pop_back();
        const auto& span = next.span;
        if (span.id == id) {
            ++matches; found = {span.payload_offset, span.payload_offset + span.payload_size};
        }
        if ((span.id & kContainerBit) == 0) continue;
        if (next.depth >= 16) { issues.push_back({span.offset, "chunk nesting exceeds 16"}); return false; }
        std::vector<ChunkSpan> children;
        if (!list_chunks(data, span.payload_offset, span.payload_offset + span.payload_size,
                         limits.max_top_level_chunks - count, children, issues)) return false;
        count += children.size();
        for (const auto& child : children) pending.push_back({child, next.depth + 1});
    }
    if (matches != 1) { issues.push_back({0, "expected exactly one requested setup chunk"}); return false; }
    payload = found;
    return true;
}

const char* race_type_name(std::uint32_t type) noexcept {
    switch (type) {
    case 0:
        return "circuit";
    case 1:
        return "drag";
    case 2:
        return "sprint";
    case 3:
        return "drift";
    case 4:
        return "time-trial";
    case 5:
        return "lap-knockout";
    default:
        return "unknown";
    }
}

const char* race_direction_name(std::int8_t direction) noexcept {
    switch (direction) {
    case 0:
        return "forward";
    case 1:
        return "reverse";
    default:
        return "unknown";
    }
}

TrackInfoTable parse_track_info_table(
    const unsigned char* data,
    std::size_t size,
    const RaceTableLimits& limits) {
    TrackInfoTable table;
    table.image_size = static_cast<std::uint64_t>(size);

    std::vector<ChunkSpan> chunks;
    if (!top_level(data, size, limits, chunks, table.issues)) {
        return table;
    }
    const ChunkSpan* chunk =
        single_chunk(chunks, kTrackInfoChunkId, 0, table.issues);
    if (chunk == nullptr) {
        return table;
    }
    table.chunk_offset = chunk->offset;
    table.record_offset = chunk->payload_offset;
    if (chunk->payload_size == 0 ||
        chunk->payload_size % kTrackInfoRecordSize != 0) {
        table.issues.push_back(RaceTableIssue{
            chunk->offset,
            (Message() << "track table payload of " << chunk->payload_size
                       << " bytes is not a non-zero whole number of "
                       << kTrackInfoRecordSize << "-byte records")
                .str()});
        return table;
    }

    const std::size_t count =
        static_cast<std::size_t>(chunk->payload_size / kTrackInfoRecordSize);
    std::set<std::int16_t> numbers;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint64_t offset = chunk->payload_offset + i * kTrackInfoRecordSize;
        const unsigned char* record = data + offset;
        TrackInfoRecord decoded;
        decoded.index = i;
        decoded.offset = offset;

        const unsigned char* region = record + kTrackInfoRegionOffset;
        const auto* end = std::find(region, region + kTrackInfoRegionSize,
                                    static_cast<unsigned char>(0));
        if (end == region + kTrackInfoRegionSize) {
            table.issues.push_back(RaceTableIssue{
                offset + kTrackInfoRegionOffset,
                "track region is not NUL-terminated inside its 8 bytes"});
        } else {
            decoded.region.assign(reinterpret_cast<const char*>(region),
                                  static_cast<std::size_t>(end - region));
            if (decoded.region.empty() || !printable_ascii(decoded.region)) {
                table.issues.push_back(RaceTableIssue{
                    offset + kTrackInfoRegionOffset,
                    "track region is empty or not printable ASCII"});
            }
        }

        decoded.lap_skip = read_s8(record + kTrackInfoLapSkipOffset);
        decoded.number = read_s16(record + kTrackInfoNumberOffset);
        decoded.number_copy = read_s16(record + kTrackInfoNumberCopyOffset);
        decoded.mode_mask = read_u32(record + kTrackInfoModeMaskOffset);

        if (decoded.number <= 0) {
            table.issues.push_back(RaceTableIssue{
                offset + kTrackInfoNumberOffset,
                (Message() << "track number " << decoded.number
                           << " is not positive")
                    .str()});
        } else if (!numbers.insert(decoded.number).second) {
            table.issues.push_back(RaceTableIssue{
                offset + kTrackInfoNumberOffset,
                (Message() << "track number " << decoded.number
                           << " is shared with an earlier record, which the "
                              "game's lookup would always return instead")
                    .str()});
        }
        table.records.push_back(std::move(decoded));
    }
    table.valid = table.issues.empty();
    return table;
}

RaceEventTable parse_race_event_table(
    const unsigned char* data,
    std::size_t size,
    const RaceTableLimits& limits) {
    RaceEventTable table;
    table.image_size = static_cast<std::uint64_t>(size);

    std::vector<ChunkSpan> chunks;
    if (!top_level(data, size, limits, chunks, table.issues)) {
        return table;
    }
    const ChunkSpan* container =
        single_chunk(chunks, kRaceEventContainerId, 0, table.issues);
    if (container == nullptr) {
        return table;
    }
    table.container_offset = container->offset;

    std::vector<ChunkSpan> children;
    if (!list_chunks(data, container->payload_offset,
                     container->payload_offset + container->payload_size,
                     limits.max_top_level_chunks, children, table.issues)) {
        return table;
    }
    const ChunkSpan* chunk = single_chunk(children, kRaceEventChunkId,
                                          container->offset, table.issues);
    if (chunk == nullptr) {
        return table;
    }
    table.chunk_offset = chunk->offset;
    table.record_offset = chunk->payload_offset;
    if (chunk->payload_size == 0 ||
        chunk->payload_size % kRaceEventRecordSize != 0) {
        table.issues.push_back(RaceTableIssue{
            chunk->offset,
            (Message() << "event table payload of " << chunk->payload_size
                       << " bytes is not a non-zero whole number of "
                       << kRaceEventRecordSize << "-byte records")
                .str()});
        return table;
    }

    const std::size_t count =
        static_cast<std::size_t>(chunk->payload_size / kRaceEventRecordSize);
    std::set<std::uint32_t> numbers;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint64_t offset = chunk->payload_offset + i * kRaceEventRecordSize;
        const unsigned char* record = data + offset;
        RaceEventRecord decoded;
        decoded.position = i;
        decoded.offset = offset;
        decoded.number = read_u32(record + kRaceEventNumberOffset);
        decoded.kind = read_u32(record + kRaceEventKindOffset);
        decoded.type = read_u32(record + kRaceEventTypeOffset);
        decoded.race_count = read_u32(record + kRaceEventRaceCountOffset);
        decoded.prerequisite_mode = read_u16(record + kRaceEventPrerequisiteModeOffset);
        decoded.prerequisite_count =
            read_s16(record + kRaceEventPrerequisiteCountOffset);

        if (decoded.number >= kRaceEventNumberLimit) {
            table.issues.push_back(RaceTableIssue{
                offset + kRaceEventNumberOffset,
                (Message() << "event number " << decoded.number
                           << " is not below " << kRaceEventNumberLimit
                           << ", so the game never enters it")
                    .str()});
        } else if (!numbers.insert(decoded.number).second) {
            table.issues.push_back(RaceTableIssue{
                offset + kRaceEventNumberOffset,
                (Message() << "event number " << decoded.number
                           << " is shared with an earlier record, which the "
                              "game's pointer table would lose")
                    .str()});
        }

        if (decoded.race_count < 1 || decoded.race_count > kRaceEventRaceSlots) {
            table.issues.push_back(RaceTableIssue{
                offset + kRaceEventRaceCountOffset,
                (Message() << "event " << decoded.number << " uses "
                           << decoded.race_count << " race slots, not 1.."
                           << kRaceEventRaceSlots)
                    .str()});
        } else {
            for (std::uint32_t r = 0; r < decoded.race_count; ++r) {
                const std::uint64_t slot =
                    offset + kRaceEventRaceOffset + r * kRaceEventRaceStride;
                RaceSlot race;
                race.offset = slot;
                race.track = read_s16(data + slot + kRaceSlotTrackOffset);
                race.laps = read_s8(data + slot + kRaceSlotLapsOffset);
                race.direction = read_s8(data + slot + kRaceSlotDirectionOffset);
                if (race.track <= 0) {
                    table.issues.push_back(RaceTableIssue{
                        slot + kRaceSlotTrackOffset,
                        (Message() << "event " << decoded.number << " race " << r
                                   << " names track " << race.track)
                            .str()});
                }
                if (race.laps < 1) {
                    table.issues.push_back(RaceTableIssue{
                        slot + kRaceSlotLapsOffset,
                        (Message() << "event " << decoded.number << " race " << r
                                   << " has " << static_cast<int>(race.laps)
                                   << " laps")
                            .str()});
                }
                decoded.races.push_back(race);
            }
        }

        if (decoded.prerequisite_mode != kPrerequisitesAllOf &&
            decoded.prerequisite_mode != kPrerequisitesAnyOf) {
            table.issues.push_back(RaceTableIssue{
                offset + kRaceEventPrerequisiteModeOffset,
                (Message() << "event " << decoded.number << " prerequisite mode "
                           << decoded.prerequisite_mode << " is neither 0 nor 1")
                    .str()});
        }
        if (decoded.prerequisite_count < 0 ||
            decoded.prerequisite_count >
                static_cast<std::int16_t>(kRaceEventPrerequisiteSlots)) {
            table.issues.push_back(RaceTableIssue{
                offset + kRaceEventPrerequisiteCountOffset,
                (Message() << "event " << decoded.number << " lists "
                           << decoded.prerequisite_count
                           << " prerequisites, not 0.."
                           << kRaceEventPrerequisiteSlots)
                    .str()});
        } else {
            for (std::int16_t p = 0; p < decoded.prerequisite_count; ++p) {
                decoded.prerequisites.push_back(read_s16(
                    record + kRaceEventPrerequisiteOffset +
                    static_cast<std::size_t>(p) * 2U));
            }
        }
        table.records.push_back(std::move(decoded));
    }

    // A prerequisite must name a record of this table: the availability test
    // reads the listed event's record and status with no check that either
    // exists (0x005A1044..0x005A1060).
    for (const RaceEventRecord& event : table.records) {
        for (std::size_t p = 0; p < event.prerequisites.size(); ++p) {
            const std::int16_t wanted = event.prerequisites[p];
            if (wanted < 0 ||
                numbers.find(static_cast<std::uint32_t>(wanted)) == numbers.end()) {
                table.issues.push_back(RaceTableIssue{
                    event.offset + kRaceEventPrerequisiteOffset + p * 2U,
                    (Message() << "event " << event.number << " prerequisite "
                               << wanted << " is not an event of this table")
                        .str()});
            }
        }
    }

    table.valid = table.issues.empty();
    return table;
}

TrackInfoTable load_track_info_table(
    const std::filesystem::path& path,
    std::vector<unsigned char>& image,
    const RaceTableLimits& limits) {
    std::vector<RaceTableIssue> issues;
    std::uint64_t stored_size = 0;
    bool jdlz = false;
    TrackInfoTable table;
    if (read_image(path, limits, image, stored_size, jdlz, issues)) {
        table = parse_track_info_table(image.data(), image.size(), limits);
    } else {
        table.issues = std::move(issues);
    }
    table.stored_size = stored_size;
    table.jdlz = jdlz;
    return table;
}

RaceEventTable load_race_event_table(
    const std::filesystem::path& path,
    std::vector<unsigned char>& image,
    const RaceTableLimits& limits) {
    std::vector<RaceTableIssue> issues;
    std::uint64_t stored_size = 0;
    bool jdlz = false;
    RaceEventTable table;
    if (read_image(path, limits, image, stored_size, jdlz, issues)) {
        table = parse_race_event_table(image.data(), image.size(), limits);
    } else {
        table.issues = std::move(issues);
    }
    table.stored_size = stored_size;
    table.jdlz = jdlz;
    return table;
}

const TrackInfoRecord* find_track_info(const TrackInfoTable& table,
                                       std::int32_t number) noexcept {
    if (!table.ok()) {
        return nullptr;
    }
    for (const TrackInfoRecord& record : table.records) {
        if (record.number == number) {
            return &record;
        }
    }
    return nullptr;
}

const RaceEventRecord* find_race_event(const RaceEventTable& table,
                                       std::uint32_t number) noexcept {
    if (!table.ok()) {
        return nullptr;
    }
    for (const RaceEventRecord& record : table.records) {
        if (record.number == number) {
            return &record;
        }
    }
    return nullptr;
}

std::vector<RaceTableIssue> check_race_tracks(const RaceEventTable& events,
                                              const TrackInfoTable& tracks) {
    std::vector<RaceTableIssue> issues;
    if (!events.ok() || !tracks.ok()) {
        return issues;
    }
    for (const RaceEventRecord& event : events.records) {
        for (std::size_t r = 0; r < event.races.size(); ++r) {
            const RaceSlot& race = event.races[r];
            if (find_track_info(tracks, race.track) == nullptr) {
                issues.push_back(RaceTableIssue{
                    race.offset + kRaceSlotTrackOffset,
                    (Message() << "event " << event.number << " race " << r
                               << " names track " << race.track
                               << ", which is not in the track table")
                        .str()});
            }
        }
    }
    return issues;
}

std::string track_stream_file_name(const TrackInfoRecord& record) {
    return "STREAM" + record.region + ".BUN";
}

std::string track_stream_pack_file_name(const TrackInfoRecord& record) {
    return (Message() << "STREAM" << record.region << '_' << record.number << ".BUN")
        .str();
}

std::string track_texture_file_name(std::int32_t number) {
    return (Message() << "TEX" << number << "TRACK.BIN").str();
}

std::string track_bundle_file_name(std::int32_t number) {
    return (Message() << "TRACKB" << number << ".LZC").str();
}

bool race_event_available(const RaceEventTable& table,
                          const RaceEventRecord& event,
                          const std::vector<std::uint8_t>& status) noexcept {
    if (!table.ok() || status.size() < kRaceEventNumberLimit ||
        event.number == 0 || event.number >= kRaceEventNumberLimit) {
        return false;
    }
    const std::uint8_t own = status[event.number];
    if (own == 2 || own == 4 || event.kind == kRaceEventKindNeverAvailable) {
        return false;
    }
    const auto satisfied = [&](std::int16_t wanted) {
        const RaceEventRecord* prerequisite =
            find_race_event(table, static_cast<std::uint32_t>(wanted));
        if (prerequisite == nullptr) {
            return false;
        }
        const std::uint8_t state = status[prerequisite->number];
        if (state == 2) {
            return true;
        }
        return (state == 4 || state == 5) &&
               (prerequisite->kind == 2 || prerequisite->kind == 3);
    };
    if (event.prerequisite_mode == kPrerequisitesAllOf) {
        return std::all_of(event.prerequisites.begin(), event.prerequisites.end(),
                           satisfied);
    }
    if (event.prerequisite_mode == kPrerequisitesAnyOf) {
        return std::any_of(event.prerequisites.begin(), event.prerequisites.end(),
                           satisfied);
    }
    return false;
}

} // namespace nfsu
