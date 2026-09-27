#include "nfsu/track_stream.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <functional>
#include <limits>
#include <locale>
#include <new>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace nfsu {
namespace {

constexpr std::size_t kChunkHeaderSize = 8;
constexpr std::uint32_t kGeometryRoot = 0x80134000U;
constexpr std::uint32_t kSceneryRoot = 0x80034100U;
constexpr std::uint32_t kTexturePackRoot = 0xB3300000U;
constexpr std::uint32_t kPaddingChunk = 0x00000000U;

std::uint16_t read_u16(const unsigned char* bytes) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<unsigned int>(bytes[0]) |
        (static_cast<unsigned int>(bytes[1]) << 8U));
}

std::uint32_t read_u32(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
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

std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b) noexcept {
    return a > std::numeric_limits<std::uint64_t>::max() - b
               ? std::numeric_limits<std::uint64_t>::max()
               : a + b;
}

struct ChunkSpan {
    std::uint32_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t payload_size = 0;
};

// The top-level chunks of [0, size), which must tile it exactly. Offsets
// are relative to `data`; `base` is added to every reported offset.
bool list_top_level(const unsigned char* data,
                    std::uint64_t size,
                    std::uint64_t base,
                    std::size_t max_chunks,
                    std::vector<ChunkSpan>& chunks,
                    std::vector<TrackIssue>& issues) {
    std::uint64_t at = 0;
    while (at < size) {
        if (chunks.size() >= max_chunks) {
            issues.push_back(TrackIssue{
                base + at, (Message() << "more than " << max_chunks
                                      << " top-level chunks")
                               .str()});
            return false;
        }
        if (size - at < kChunkHeaderSize) {
            issues.push_back(TrackIssue{
                base + at, (Message() << (size - at)
                                      << " trailing bytes are shorter than a"
                                         " chunk header")
                               .str()});
            return false;
        }
        const std::uint32_t id = read_u32(data + at);
        const std::uint64_t payload = read_u32(data + at + 4);
        if (payload > size - at - kChunkHeaderSize) {
            issues.push_back(TrackIssue{
                base + at, (Message() << "chunk " << hex_id(id) << " declares "
                                      << payload << " payload bytes and only "
                                      << (size - at - kChunkHeaderSize)
                                      << " remain")
                               .str()});
            return false;
        }
        chunks.push_back(ChunkSpan{id, at, payload});
        at += kChunkHeaderSize + payload;
    }
    return true;
}

bool printable_name(const unsigned char* bytes, std::string& name) {
    name.clear();
    std::size_t length = 0;
    while (length < kTrackStreamNameSize && bytes[length] != 0) {
        ++length;
    }
    if (length == 0 || length == kTrackStreamNameSize) {
        return false;
    }
    for (std::size_t i = 0; i < length; ++i) {
        if (bytes[i] < 0x20U || bytes[i] > 0x7EU) {
            return false;
        }
    }
    for (std::size_t i = length; i < kTrackStreamNameSize; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    name.assign(reinterpret_cast<const char*>(bytes), length);
    return true;
}

bool is_fixed_number(std::int32_t number) noexcept {
    for (const std::int16_t fixed : kTrackStreamFixedSections) {
        if (number == fixed) {
            return true;
        }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

TrackStreamTable parse_track_stream_table(
    const unsigned char* data,
    std::size_t size,
    const TrackStreamLimits& limits) {
    TrackStreamTable table;
    table.image_size = size;
    if (data == nullptr && size != 0) {
        table.issues.push_back(TrackIssue{0, "no image bytes"});
        return table;
    }
    if (static_cast<std::uint64_t>(size) > limits.max_image_bytes) {
        table.issues.push_back(TrackIssue{
            0, (Message() << "image is " << size << " bytes, over the limit of "
                          << limits.max_image_bytes)
                   .str()});
        return table;
    }

    std::vector<ChunkSpan> chunks;
    if (!list_top_level(data, size, 0, limits.max_chunks, chunks, table.issues)) {
        return table;
    }

    std::vector<ChunkSpan> tables;
    std::vector<ChunkSpan> sizes;
    for (const ChunkSpan& chunk : chunks) {
        if (chunk.id == kTrackStreamTableId) {
            tables.push_back(chunk);
        } else if (chunk.id == kTrackStreamFileSizesId) {
            sizes.push_back(chunk);
        }
    }
    if (tables.size() != 1) {
        table.issues.push_back(TrackIssue{
            tables.empty() ? 0 : tables[1].offset,
            (Message() << "expected exactly one chunk "
                       << hex_id(kTrackStreamTableId) << ", found "
                       << tables.size())
                .str()});
        return table;
    }
    if (sizes.size() > 1) {
        table.issues.push_back(TrackIssue{
            sizes[1].offset, (Message() << "expected at most one chunk "
                                        << hex_id(kTrackStreamFileSizesId)
                                        << ", found " << sizes.size())
                                 .str()});
        return table;
    }
    if (!sizes.empty()) {
        if (sizes[0].payload_size != kTrackStreamFileSizesSize) {
            table.issues.push_back(TrackIssue{
                sizes[0].offset,
                (Message() << "chunk " << hex_id(kTrackStreamFileSizesId)
                           << " holds " << sizes[0].payload_size
                           << " bytes, not " << kTrackStreamFileSizesSize)
                    .str()});
            return table;
        }
        table.has_file_sizes = true;
        table.file_sizes_offset = sizes[0].offset + kChunkHeaderSize;
        for (std::size_t i = 0; i < kTrackStreamFileCount; ++i) {
            table.file_sizes[i] =
                read_u32(data + table.file_sizes_offset + i * 4U);
        }
    }

    const ChunkSpan& chunk = tables[0];
    table.table_offset = chunk.offset + kChunkHeaderSize;
    table.table_size = chunk.payload_size;
    if (chunk.payload_size == 0 ||
        chunk.payload_size % kTrackStreamRecordSize != 0) {
        table.issues.push_back(TrackIssue{
            chunk.offset,
            (Message() << "chunk " << hex_id(kTrackStreamTableId) << " holds "
                       << chunk.payload_size
                       << " bytes, not a non-zero whole number of "
                       << kTrackStreamRecordSize << "-byte records")
                .str()});
        return table;
    }
    const std::uint64_t count = chunk.payload_size / kTrackStreamRecordSize;
    if (count > limits.max_records) {
        table.issues.push_back(TrackIssue{
            chunk.offset, (Message() << count << " records, over the limit of "
                                     << limits.max_records)
                              .str()});
        return table;
    }

    try {
        table.records.reserve(static_cast<std::size_t>(count));
        std::set<std::int16_t> seen;
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint64_t at =
                table.table_offset + i * kTrackStreamRecordSize;
            const unsigned char* record = data + at;
            TrackStreamRecord out;
            out.index = static_cast<std::size_t>(i);
            out.offset = at;
            if (!printable_name(record + kTrackStreamNameOffset, out.name)) {
                table.issues.push_back(TrackIssue{
                    at, (Message() << "record " << i
                                   << " name is not a non-empty printable"
                                      " ASCII string NUL-terminated inside its "
                                   << kTrackStreamNameSize << " bytes")
                            .str()});
                continue;
            }
            out.number = static_cast<std::int16_t>(
                read_u16(record + kTrackStreamNumberOffset));
            out.track_flag = record[kTrackStreamTrackFlagOffset];
            out.file_index = read_u32(record + kTrackStreamFileIndexOffset);
            out.data_offset = read_u32(record + kTrackStreamDataOffsetOffset);
            out.size = read_u32(record + kTrackStreamSizeOffset);
            out.resident_size = read_u32(record + kTrackStreamResidentSizeOffset);
            out.unknown = read_u32(record + kTrackStreamUnknownOffset);

            bool runtime_zero = record[kTrackStreamRequestedOffset] == 0 &&
                                read_u32(record + kTrackStreamStateOffset) == 0;
            for (std::size_t b = kTrackStreamRuntimeOffset;
                 b < kTrackStreamRecordSize; ++b) {
                runtime_zero = runtime_zero && record[b] == 0;
            }
            if (!runtime_zero) {
                table.issues.push_back(TrackIssue{
                    at, (Message() << "record " << i << " '" << out.name
                                   << "' has a non-zero byte where the loader"
                                      " writes (+11, +12..+15 or +36..+55)")
                            .str()});
                continue;
            }
            if (out.file_index >= kTrackStreamFileCount) {
                table.issues.push_back(TrackIssue{
                    at + kTrackStreamFileIndexOffset,
                    (Message() << "record " << i << " '" << out.name
                               << "' names file index " << out.file_index
                               << "; the streamer holds three file names")
                        .str()});
                continue;
            }
            if (out.resident_size > out.size) {
                table.issues.push_back(TrackIssue{
                    at + kTrackStreamResidentSizeOffset,
                    (Message() << "record " << i << " '" << out.name
                               << "' keeps " << out.resident_size
                               << " bytes of the " << out.size << " it reads")
                        .str()});
                continue;
            }
            if (table.has_file_sizes) {
                const std::uint64_t end =
                    static_cast<std::uint64_t>(out.data_offset) + out.size;
                if (end > table.file_sizes[out.file_index]) {
                    table.issues.push_back(TrackIssue{
                        at + kTrackStreamDataOffsetOffset,
                        (Message() << "record " << i << " '" << out.name
                                   << "' reaches byte " << end << " of file "
                                   << out.file_index << ", which "
                                   << hex_id(kTrackStreamFileSizesId)
                                   << " declares as "
                                   << table.file_sizes[out.file_index]
                                   << " bytes")
                            .str()});
                    continue;
                }
            }
            if (!seen.insert(out.number).second) {
                table.issues.push_back(TrackIssue{
                    at + kTrackStreamNumberOffset,
                    (Message() << "record " << i << " '" << out.name
                               << "' repeats section number " << out.number
                               << "; the lookup at 0x00444D30 would never"
                                  " reach it")
                        .str()});
                continue;
            }
            table.records.push_back(std::move(out));
        }
    } catch (const std::bad_alloc&) {
        table.records.clear();
        table.issues.push_back(TrackIssue{chunk.offset, "out of memory"});
        return table;
    }
    table.valid = table.issues.empty();
    return table;
}

std::size_t find_track_stream_record(
    const TrackStreamTable& table, std::int32_t number) noexcept {
    for (std::size_t i = 0; i < table.records.size(); ++i) {
        if (table.records[i].number == number) {
            return i;
        }
    }
    return kTrackStreamNoRecord;
}

std::uint64_t track_stream_pool_bytes(const TrackStreamTable& table) noexcept {
    std::uint64_t kept = 0;
    std::uint64_t largest = 0;
    for (const TrackStreamRecord& record : table.records) {
        if (record.track_flag == 0 && !is_fixed_number(record.number)) {
            continue;
        }
        kept = saturating_add(kept, record.resident_size);
        if (record.size != record.resident_size) {
            largest = std::max(
                largest, static_cast<std::uint64_t>(record.size) +
                             record.resident_size);
        }
    }
    return saturating_add(std::max(kept, largest), kTrackStreamPoolHeadroom);
}

// ---------------------------------------------------------------------------
// The selection
// ---------------------------------------------------------------------------

const char* track_stream_set_name(TrackStreamSet set) noexcept {
    switch (set) {
        case TrackStreamSet::permanent:
            return "permanent";
        case TrackStreamSet::zones:
            return "zones";
        case TrackStreamSet::stream:
            return "stream";
    }
    return "unknown";
}

const char* track_stream_reason_name(TrackStreamReason reason) noexcept {
    switch (reason) {
        case TrackStreamReason::fixed:
            return "fixed";
        case TrackStreamReason::extra_list:
            return "extra-list";
        case TrackStreamReason::track_flag:
            return "track-flag";
        case TrackStreamReason::zone_list:
            return "zone-list";
        case TrackStreamReason::stream_bound:
            return "stream-bound";
    }
    return "unknown";
}

namespace {

class Selector {
public:
    Selector(const TrackStreamTable& table, TrackStreamSelection& out,
             bool apply_limit)
        : table_(table), out_(out), apply_limit_(apply_limit),
          chosen_(table.records.size(), false) {}

    // Returns false once the request limit stops the walk.
    bool request(std::int32_t number, TrackStreamReason reason) {
        if (stopped_) {
            return false;
        }
        TrackStreamRequest request;
        request.number = number;
        request.reason = reason;
        request.record = find_track_stream_record(table_, number);
        if (request.record == kTrackStreamNoRecord) {
            ++out_.requests_without_record;
        } else if (chosen_[request.record]) {
            ++out_.repeated_requests;
        } else {
            chosen_[request.record] = true;
            request.selected = true;
            out_.records.push_back(request.record);
            out_.reasons.push_back(reason);
        }
        out_.requests.push_back(request);
        if (apply_limit_ && request.selected &&
            out_.records.size() >= kTrackStreamRequestLimit) {
            // 0x00444D5B checks the limit after marking a new record and
            // before listing it: once 256 are listed, the next new record is
            // marked but not listed, and the walk ends there. The records
            // selected are the same as stopping here, at the 256th. Only
            // the counters differ, and only past 256 selections: misses and
            // repeats after the 256th, which retail still walks, are not
            // counted here.
            stopped_ = true;
            out_.stopped_at_limit = true;
            return false;
        }
        return true;
    }

    bool chosen(std::size_t record) const { return chosen_[record]; }

private:
    const TrackStreamTable& table_;
    TrackStreamSelection& out_;
    bool apply_limit_;
    bool stopped_ = false;
    std::vector<bool> chosen_;
};

// The entries of a section list the loader reads: its count, bounded by
// what the record holds. parse_track_zones refuses a count outside the
// record, so the bound only guards a report built some other way.
std::size_t list_count(const TrackZoneSectionList& list) noexcept {
    if (list.count <= 0) {
        return 0;
    }
    return std::min(static_cast<std::size_t>(list.count), list.sections.size());
}

const TrackZone* zone_with_key(const TrackZoneReport& zones, std::int32_t key) {
    for (const TrackZone& zone : zones.zones) {
        if (zone.key == key) {
            return &zone;
        }
    }
    return nullptr;
}

} // namespace

bool select_track_stream_sections(
    const TrackStreamTable& table,
    const TrackZoneReport& zones,
    std::uint32_t track,
    TrackStreamSet set,
    TrackStreamSelection& selection,
    std::string& error) {
    selection = TrackStreamSelection{};
    selection.set = set;
    selection.track = track;
    if (!table.valid) {
        error = "the streaming table is not valid";
        return false;
    }
    if (!zones.valid) {
        error = "the zone report is not valid";
        return false;
    }

    try {
        Selector selector(table, selection,
                          set == TrackStreamSet::permanent);
        bool going = true;
        for (const std::int16_t number : kTrackStreamFixedSections) {
            going = going && selector.request(number, TrackStreamReason::fixed);
        }

        // 0x00444B98..0x00444BD0: the first 0x00034154 record whose key is
        // the track number, and every entry of it.
        for (std::size_t i = 0; i < zones.section_lists.size(); ++i) {
            const TrackZoneSectionList& list = zones.section_lists[i];
            if (list.chunk_id != kTrackZoneExtraListId || list.key != track) {
                continue;
            }
            selection.has_extra_list = true;
            selection.extra_list = i;
            // 0x00444BB4 copies the record's count of entries, not its
            // capacity: the slots past the count are never read.
            for (std::size_t k = 0; k < list_count(list); ++k) {
                going = going && selector.request(list.sections[k],
                                                  TrackStreamReason::extra_list);
            }
            break;
        }

        // 0x00444C94..0x00444CC6: every record whose track byte is set, in
        // table order.
        for (const TrackStreamRecord& record : table.records) {
            if (record.track_flag != 0) {
                going = going &&
                        selector.request(record.number,
                                         TrackStreamReason::track_flag);
            }
        }

        if (set == TrackStreamSet::zones) {
            for (const TrackZone& zone : zones.zones) {
                for (std::int32_t k = 0; k < zone.section_count; ++k) {
                    selector.request(zone.sections[static_cast<std::size_t>(k)],
                                     TrackStreamReason::zone_list);
                }
            }
        } else if (set == TrackStreamSet::stream) {
            for (std::size_t i = 0; i < table.records.size(); ++i) {
                const TrackStreamRecord& record = table.records[i];
                if (record.file_index != kTrackStreamFileStream &&
                    record.file_index != kTrackStreamFilePack) {
                    ++selection.excluded_records;
                    continue;
                }
                if (!selector.chosen(i)) {
                    selector.request(record.number,
                                     TrackStreamReason::stream_bound);
                }
            }
        }

        // The route check. 0x00444A28 reads the 0x00034153 record the
        // streamer keeps at +0x598, which 0x004446B0 finds by the track
        // number, and accepts a look-ahead zone only when that record names
        // its key.
        std::vector<bool> selected(table.records.size(), false);
        for (const std::size_t record : selection.records) {
            selected[record] = true;
        }
        for (const TrackZoneSectionList& list : zones.section_lists) {
            if (list.chunk_id != kTrackZoneTrackListId || list.key != track) {
                continue;
            }
            selection.has_route_list = true;
            std::set<std::int32_t> numbers;
            for (std::size_t k = 0; k < list_count(list); ++k) {
                const std::int16_t key = list.sections[k];
                ++selection.route_zones;
                const TrackZone* zone = zone_with_key(zones, key);
                if (zone == nullptr) {
                    ++selection.route_zones_missing;
                    continue;
                }
                for (std::int32_t k = 0; k < zone->section_count; ++k) {
                    numbers.insert(zone->sections[static_cast<std::size_t>(k)]);
                }
            }
            selection.route_numbers = numbers.size();
            for (const std::int32_t number : numbers) {
                const std::size_t record = find_track_stream_record(table, number);
                if (record == kTrackStreamNoRecord) {
                    continue;
                }
                ++selection.route_numbers_with_record;
                if (!selected[record]) {
                    ++selection.route_numbers_outside;
                }
            }
            break;
        }
    } catch (const std::bad_alloc&) {
        selection = TrackStreamSelection{};
        error = "out of memory while selecting sections";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool TrackStreamLoad::ok() const noexcept {
    if (!issues.empty()) {
        return false;
    }
    for (const TrackStreamSection& section : sections) {
        if (!section.issues.empty() || !section.geometry.issues.empty() ||
            !section.objects.issues.empty() || !section.packs.issues.empty()) {
            return false;
        }
        for (const TexturePack& pack : section.packs.packs) {
            if (!pack.issues.empty()) {
                return false;
            }
        }
    }
    return true;
}

namespace {

void extend_bounds(bool& has, float min[3], float max[3], const float lo[3],
                   const float hi[3]) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!has) {
            min[axis] = lo[axis];
            max[axis] = hi[axis];
        } else {
            min[axis] = std::min(min[axis], lo[axis]);
            max[axis] = std::max(max[axis], hi[axis]);
        }
    }
    has = true;
}

void extend_bounds_i32(bool& has, std::int32_t min[3], std::int32_t max[3],
                       const std::int32_t lo[3], const std::int32_t hi[3]) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!has) {
            min[axis] = lo[axis];
            max[axis] = hi[axis];
        } else {
            min[axis] = std::min(min[axis], lo[axis]);
            max[axis] = std::max(max[axis], hi[axis]);
        }
    }
    has = true;
}

// Parses one section's bytes in place. `section.bytes` is already filled.
void parse_section(TrackStreamSection& section,
                   const TrackStreamLoadLimits& limits) {
    std::vector<ChunkSpan> chunks;
    if (!list_top_level(section.bytes.data(), section.bytes.size(),
                        section.offset, limits.geometry.max_chunks, chunks,
                        section.issues)) {
        return;
    }
    section.chunks = chunks.size();
    for (const ChunkSpan& chunk : chunks) {
        if (chunk.id == kGeometryRoot) {
            ++section.geometry_roots;
        } else if (chunk.id == kSceneryRoot) {
            ++section.scenery_sections;
        } else if (chunk.id == kTexturePackRoot) {
            ++section.texture_packs;
        } else if (chunk.id == kPaddingChunk) {
            ++section.padding_chunks;
        } else {
            ++section.other_chunks;
        }
    }
    section.loaded = true;
    if (section.geometry_roots > 0) {
        section.geometry = parse_track_region(
            section.bytes.data(), section.bytes.size(), section.offset,
            limits.geometry);
    }
    if (limits.poll) limits.poll();
    if (section.scenery_sections > 0) {
        section.objects = parse_track_objects(
            section.bytes.data(), section.bytes.size(), section.offset,
            limits.objects);
    }
    if (limits.poll) limits.poll();
    if (section.texture_packs > 0) {
        section.packs = parse_texture_pack_set(
            section.bytes.data(), section.bytes.size(), limits.textures);
    }
}

void merge_sections(TrackStreamLoad& load, const TrackStreamLoadLimits& limits) {
    TrackRegionReport& geometry = load.geometry;
    TrackObjectReport& objects = load.objects;
    geometry = TrackRegionReport{};
    objects = TrackObjectReport{};
    geometry.file_size = load.stream_file_size;
    objects.file_size = load.stream_file_size;
    // Counts come from already allocated parser arrays, not file claims.
    // Reserve once so moving the selected set never grows a second header array.
    std::size_t meshes = 0, scenery = 0;
    for (const auto& section : load.sections) {
        if (!section.loaded) continue;
        meshes += section.geometry.meshes.size();
        scenery += section.objects.sections.size();
    }
    geometry.meshes.reserve(meshes);
    objects.sections.reserve(scenery);
    bool all_tables = true;
    bool any_geometry = false;
    for (TrackStreamSection& section : load.sections) {
        if (limits.poll) limits.poll();
        section.first_mesh = geometry.meshes.size();
        section.first_scenery = objects.sections.size();
        section.mesh_count = 0;
        section.scenery_count = 0;
        if (!section.loaded) {
            continue;
        }
        geometry.region.size += section.size;
        objects.region.size += section.size;
        if (section.geometry_roots > 0) {
            any_geometry = true;
            all_tables = all_tables && section.geometry.mesh_table_valid;
            const TrackRegionCost& part = section.geometry.cost;
            TrackRegionCost& total = geometry.cost;
            total.region_bytes += part.region_bytes;
            total.chunks += part.chunks;
            total.geometry_roots += part.geometry_roots;
            total.meshes += part.meshes;
            total.valid_meshes += part.valid_meshes;
            total.empty_meshes += part.empty_meshes;
            total.vertices += part.vertices;
            total.triangles += part.triangles;
            total.materials += part.materials;
            total.source_vertex_bytes += part.source_vertex_bytes;
            total.source_index_bytes += part.source_index_bytes;
            total.decoded_vertex_bytes += part.decoded_vertex_bytes;
            total.decoded_index_bytes += part.decoded_index_bytes;
            total.largest_mesh_vertices =
                std::max(total.largest_mesh_vertices, part.largest_mesh_vertices);
            total.largest_mesh_triangles = std::max(
                total.largest_mesh_triangles, part.largest_mesh_triangles);
            if (part.has_bounds) {
                extend_bounds(total.has_bounds, total.bounds_min,
                              total.bounds_max, part.bounds_min,
                              part.bounds_max);
            }
            geometry.issues.insert(geometry.issues.end(),
                                   section.geometry.issues.begin(),
                                   section.geometry.issues.end());
            for (TrackMesh& mesh : section.geometry.meshes) {
                mesh.index = geometry.meshes.size();
                geometry.meshes.push_back(std::move(mesh));
            }
            section.mesh_count = geometry.meshes.size() - section.first_mesh;
            section.geometry.meshes.clear();
            section.geometry.meshes.shrink_to_fit();
        }
        if (section.scenery_sections > 0) {
            const TrackObjectCost& part = section.objects.cost;
            TrackObjectCost& total = objects.cost;
            total.region_bytes += part.region_bytes;
            total.chunks += part.chunks;
            total.sections += part.sections;
            total.valid_sections += part.valid_sections;
            total.objects += part.objects;
            total.instances += part.instances;
            total.bound_nodes += part.bound_nodes;
            total.source_object_bytes += part.source_object_bytes;
            total.source_instance_bytes += part.source_instance_bytes;
            total.source_bound_node_bytes += part.source_bound_node_bytes;
            total.mesh_slots_used += part.mesh_slots_used;
            total.largest_section_instances = std::max(
                total.largest_section_instances, part.largest_section_instances);
            total.deepest_tree = std::max(total.deepest_tree, part.deepest_tree);
            if (part.has_bounds) {
                extend_bounds_i32(total.has_bounds, total.bounds_min,
                                  total.bounds_max, part.bounds_min,
                                  part.bounds_max);
            }
            objects.issues.insert(objects.issues.end(),
                                  section.objects.issues.begin(),
                                  section.objects.issues.end());
            for (TrackSection& scenery : section.objects.sections) {
                scenery.index = objects.sections.size();
                objects.sections.push_back(std::move(scenery));
            }
            section.scenery_count =
                objects.sections.size() - section.first_scenery;
            section.objects.sections.clear();
            section.objects.sections.shrink_to_fit();
        }
    }
    geometry.mesh_table_valid = any_geometry && all_tables;
    geometry.cost.distinct_texture_hashes =
        track_region_texture_hashes(geometry).size();
    objects.cost.distinct_mesh_hashes = track_object_mesh_hashes(objects).size();
}

} // namespace

namespace {

// Where a section's bytes come from. `size` is the size of the file the
// index names, and `present` says whether this load was given it at all.
struct RangeSource {
    bool present = false;
    std::uint64_t size = 0;
};

using RangeReader = std::function<bool(std::uint32_t file,
                                       std::uint64_t offset,
                                       std::uint64_t size,
                                       std::vector<unsigned char>& out,
                                       std::string& problem)>;

bool load_with_reader(const TrackStreamTable& table,
                      const TrackStreamSelection& selection,
                      const RangeSource (&files)[kTrackStreamFileCount],
                      const RangeReader& read_range,
                      const TrackStreamLoadLimits& limits,
                      TrackStreamLoad& load,
                      std::string& error) {
    load.sections.clear();
    load.bytes_read = 0;
    if (selection.records.size() != selection.reasons.size()) {
        error = "the selection's records and reasons differ in length";
        return false;
    }
    try {
        load.sections.reserve(selection.records.size());
        for (std::size_t k = 0; k < selection.records.size(); ++k) {
            if (limits.poll) limits.poll();
            const std::size_t index = selection.records[k];
            if (index >= table.records.size()) {
                error = (Message() << "the selection names record " << index
                                   << " of a table of "
                                   << table.records.size())
                            .str();
                load.sections.clear();
                return false;
            }
            const TrackStreamRecord& record = table.records[index];
            load.sections.emplace_back();
            TrackStreamSection& section = load.sections.back();
            section.record = index;
            section.reason = selection.reasons[k];
            section.name = record.name;
            section.number = record.number;
            section.file_index = record.file_index;
            section.offset = record.data_offset;
            section.size = record.size;
            section.resident_size = record.resident_size;

            if (section.file_index >= kTrackStreamFileCount ||
                !files[section.file_index].present) {
                section.issues.push_back(TrackIssue{
                    section.offset,
                    (Message() << "section '" << section.name
                               << "' lives in file index "
                               << section.file_index
                               << ", which this load was not given")
                        .str()});
                continue;
            }
            const std::uint64_t file_size = files[section.file_index].size;
            if (section.offset % kTrackStreamSectionAlignment != 0) {
                section.issues.push_back(TrackIssue{
                    section.offset,
                    (Message() << "section '" << section.name
                               << "' starts at " << section.offset
                               << ", not a multiple of "
                               << kTrackStreamSectionAlignment)
                        .str()});
                continue;
            }
            if (section.offset > file_size ||
                section.size > file_size - section.offset) {
                section.issues.push_back(TrackIssue{
                    section.offset,
                    (Message() << "section '" << section.name << "' reaches "
                               << (section.offset + section.size)
                               << " in a file of " << file_size << " bytes")
                        .str()});
                continue;
            }
            if (section.size > limits.max_total_bytes - load.bytes_read) {
                error = (Message() << "the selected sections hold more than "
                                   << limits.max_total_bytes << " bytes")
                            .str();
                load.sections.clear();
                return false;
            }
            std::string problem;
            if (!read_range(section.file_index, section.offset, section.size,
                      section.bytes, problem)) {
                section.bytes.clear();
                section.issues.push_back(TrackIssue{
                    section.offset, (Message() << "section '" << section.name
                                               << "': " << problem)
                                        .str()});
                continue;
            }
            load.bytes_read += section.size;
            if (limits.poll) limits.poll();
            parse_section(section, limits);
            if (limits.release_non_texture_bytes && section.packs.packs.empty()) {
                std::vector<unsigned char>().swap(section.bytes);
            }
            if (limits.poll) limits.poll();
        }
        merge_sections(load, limits);
        if (limits.poll) limits.poll();
    } catch (const std::bad_alloc&) {
        load.sections.clear();
        error = "out of memory while loading sections";
        return false;
    }
    return true;
}

} // namespace

bool load_track_stream_sections_from_memory(
    const TrackStreamTable& table,
    const TrackStreamSelection& selection,
    const std::vector<unsigned char>* const images[kTrackStreamFileCount],
    const TrackStreamLoadLimits& limits,
    TrackStreamLoad& load,
    std::string& error) {
    load = TrackStreamLoad{};
    RangeSource files[kTrackStreamFileCount];
    for (std::size_t f = 0; f < kTrackStreamFileCount; ++f) {
        files[f].present = images[f] != nullptr;
        files[f].size = images[f] != nullptr ? images[f]->size() : 0;
    }
    load.stream_file_size = files[kTrackStreamFileStream].size;
    load.pack_file_size = files[kTrackStreamFilePack].size;
    const RangeReader read_range = [&images](std::uint32_t file, std::uint64_t offset,
                                       std::uint64_t size,
                                       std::vector<unsigned char>& out,
                                       std::string&) {
        const auto begin =
            images[file]->begin() + static_cast<std::ptrdiff_t>(offset);
        out.assign(begin, begin + static_cast<std::ptrdiff_t>(size));
        return true;
    };
    if (!load_with_reader(table, selection, files, read_range, limits, load, error)) {
        return false;
    }
    load.geometry.file_size = load.stream_file_size;
    load.objects.file_size = load.stream_file_size;
    return true;
}

bool load_track_stream_sections(
    const TrackStreamTable& table,
    const TrackStreamSelection& selection,
    const TrackStreamFiles& files,
    const TrackStreamLoadLimits& limits,
    TrackStreamLoad& load,
    std::string& error) {
    load = TrackStreamLoad{};
    // Each selected record's range is read straight into its section, one
    // seek and one read per record. Nothing else of either file is read,
    // and neither file is ever opened for writing.
    std::filesystem::path paths[kTrackStreamFileCount];
    paths[kTrackStreamFileStream] = files.stream;
    paths[kTrackStreamFilePack] = files.pack;
    const bool given[kTrackStreamFileCount] = {false, true, files.has_pack};
    RangeSource sources[kTrackStreamFileCount];
    std::ifstream streams[kTrackStreamFileCount];
    std::vector<TrackIssue> file_issues;
    try {
        for (std::size_t f = 0; f < kTrackStreamFileCount; ++f) {
            if (!given[f]) {
                continue;
            }
            std::error_code code;
            const std::uintmax_t size = std::filesystem::file_size(paths[f], code);
            if (code) {
                file_issues.push_back(TrackIssue{
                    0, (Message() << "file index " << f
                                  << ": cannot size the file: "
                                  << code.message())
                           .str()});
                continue;
            }
            streams[f].open(paths[f], std::ios::binary);
            if (!streams[f]) {
                file_issues.push_back(TrackIssue{
                    0, (Message() << "file index " << f
                                  << ": cannot open the file")
                           .str()});
                continue;
            }
            sources[f].present = true;
            sources[f].size = static_cast<std::uint64_t>(size);
        }
    } catch (const std::exception& caught) {
        error = std::string("cannot open the stream: ") + caught.what();
        return false;
    }

    const RangeReader read_range = [&streams](std::uint32_t file, std::uint64_t offset,
                                        std::uint64_t size,
                                        std::vector<unsigned char>& out,
                                        std::string& problem) {
        std::ifstream& stream = streams[file];
        out.assign(static_cast<std::size_t>(size), 0);
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset));
        if (!stream ||
            !stream.read(reinterpret_cast<char*>(out.data()),
                         static_cast<std::streamsize>(size))) {
            problem = (Message() << "cannot read " << size << " bytes at "
                                 << offset)
                          .str();
            stream.clear();
            return false;
        }
        return true;
    };
    load.stream_file_size = sources[kTrackStreamFileStream].size;
    load.pack_file_size = sources[kTrackStreamFilePack].size;
    if (!load_with_reader(table, selection, sources, read_range, limits, load, error)) {
        return false;
    }
    load.issues = std::move(file_issues);
    load.geometry.file_size = load.stream_file_size;
    load.objects.file_size = load.stream_file_size;
    return true;
}

void track_stream_texture_sources(
    const TrackStreamLoad& load,
    const std::vector<std::uint32_t>* wanted,
    std::vector<RenderTextureSource>& sources,
    std::vector<std::size_t>& owners) {
    sources.clear();
    owners.clear();
    for (std::size_t s = 0; s < load.sections.size(); ++s) {
        const TrackStreamSection& section = load.sections[s];
        if (!section.loaded || section.packs.packs.empty()) {
            continue;
        }
        RenderTextureSource source;
        source.bytes = &section.bytes;
        for (const TexturePack& pack : section.packs.packs) {
            source.packs.push_back(&pack);
        }
        source.complete = section.packs.complete;
        source.wanted = wanted;
        sources.push_back(std::move(source));
        owners.push_back(s);
    }
}

// ---------------------------------------------------------------------------
// Costs and budgets
// ---------------------------------------------------------------------------

bool track_stream_section_costs(
    const TrackStreamLoad& load,
    const PreparedTrackScene& scene,
    const std::vector<std::size_t>& source_owner,
    std::vector<TrackStreamSectionCost>& costs,
    std::string& error) {
    costs.clear();
    try {
        costs.assign(load.sections.size(), TrackStreamSectionCost{});
        // Which stream section each merged scenery section came from.
        std::vector<std::size_t> scenery_owner(load.objects.sections.size(),
                                               kTrackStreamNoRecord);
        for (std::size_t s = 0; s < load.sections.size(); ++s) {
            const TrackStreamSection& section = load.sections[s];
            TrackStreamSectionCost& cost = costs[s];
            if (section.first_mesh > load.geometry.meshes.size() ||
                section.mesh_count >
                    load.geometry.meshes.size() - section.first_mesh ||
                section.first_scenery > load.objects.sections.size() ||
                section.scenery_count >
                    load.objects.sections.size() - section.first_scenery) {
                error = (Message() << "section " << s
                                   << " names meshes or scenery outside the"
                                      " merged reports")
                            .str();
                costs.clear();
                return false;
            }
            for (std::size_t m = 0; m < section.mesh_count; ++m) {
                const TrackMesh& mesh = load.geometry.meshes[section.first_mesh + m];
                ++cost.meshes;
                if (mesh.valid) {
                    ++cost.valid_meshes;
                    cost.vertices += mesh.vertices.size();
                    cost.triangles += mesh.triangles.size();
                } else if (mesh.empty) {
                    ++cost.empty_meshes;
                } else {
                    ++cost.refused_meshes;
                }
            }
            for (std::size_t k = 0; k < section.scenery_count; ++k) {
                const std::size_t merged = section.first_scenery + k;
                scenery_owner[merged] = s;
                ++cost.scenery_sections;
                cost.instances += load.objects.sections[merged].instances.size();
            }
            for (const TexturePack& pack : section.packs.packs) {
                cost.pack_records += pack.textures.size();
            }
        }
        for (const PreparedTrackInstance& instance : scene.instances) {
            if (instance.section_index >= scenery_owner.size() ||
                scenery_owner[instance.section_index] == kTrackStreamNoRecord) {
                error = (Message() << "a placed instance names scenery section "
                                   << instance.section_index
                                   << ", which no stream section supplied")
                            .str();
                costs.clear();
                return false;
            }
            ++costs[scenery_owner[instance.section_index]].placed_instances;
        }
        for (const RenderTexture& texture : scene.region.textures) {
            if (texture.source < source_owner.size() &&
                source_owner[texture.source] != kTrackStreamNoRecord &&
                source_owner[texture.source] < costs.size()) {
                TrackStreamSectionCost& cost = costs[source_owner[texture.source]];
                ++cost.retained_textures;
                cost.retained_texture_bytes += texture.payload().size();
            }
        }
    } catch (const std::bad_alloc&) {
        costs.clear();
        error = "out of memory while costing sections";
        return false;
    }
    return true;
}

const char* track_whole_platform_name(TrackWholePlatform platform) noexcept {
    switch (platform) {
        case TrackWholePlatform::desktop:
            return "desktop";
        case TrackWholePlatform::switch_plan:
            return "switch";
    }
    return "unknown";
}

void track_whole_budget_rows(
    const PreparedTrackScene& scene,
    std::uint64_t decoded_texture_bytes,
    std::uint64_t resident_bytes,
    const SceneMemoryRequirement& gpu,
    std::vector<TrackWholeBudgetRow>& rows) {
    rows.clear();
    const auto add = [&rows](TrackWholePlatform platform, const char* name,
                             std::uint64_t value, std::uint64_t ceiling) {
        TrackWholeBudgetRow row;
        row.platform = platform;
        row.name = name;
        row.value = value;
        row.ceiling = ceiling;
        row.within = value <= ceiling;
        rows.push_back(std::move(row));
    };
    const TrackDrawCost& cost = scene.region.cost;
    const TrackSceneLimits region_limits;
    const RenderTextureLimits texture_limits;
    const TrackDrawListLimits list_limits;
    const std::uint64_t upload = saturating_add(
        saturating_add(cost.vertex_buffer_bytes, cost.index_buffer_bytes),
        cost.texture_bytes);
    const std::uint64_t frame = track_scene_frame_ceiling(scene);

    add(TrackWholePlatform::desktop, "region.upload", upload,
        track_region_upload_ceiling(region_limits));
    add(TrackWholePlatform::desktop, "region.vertices", cost.vertices,
        region_limits.max_vertices);
    add(TrackWholePlatform::desktop, "region.triangles", cost.triangles,
        region_limits.max_triangles);
    add(TrackWholePlatform::desktop, "region.draws", cost.draws,
        region_limits.max_draws);
    add(TrackWholePlatform::desktop, "region.drawn_triangles",
        cost.drawn_triangles, region_limits.max_draw_triangles);
    add(TrackWholePlatform::desktop, "region.texture_bytes", cost.texture_bytes,
        region_limits.max_texture_bytes);
    add(TrackWholePlatform::desktop, "textures.decoded_bytes",
        decoded_texture_bytes, texture_limits.max_total_bytes);
    add(TrackWholePlatform::desktop, "frame.draw_list_bytes", frame,
        track_draw_list_frame_ceiling(list_limits));

    const MemoryBudget budget = switch_memory_budget();
    const auto capacity = [&budget](MemoryCategory category) {
        return budget.capacity[static_cast<std::size_t>(category)];
    };
    add(TrackWholePlatform::switch_plan, "streaming.region", resident_bytes,
        kStreamingRegionBudgetPlaceholder);
    add(TrackWholePlatform::switch_plan, "streaming.arena", resident_bytes,
        capacity(MemoryCategory::streaming));
    add(TrackWholePlatform::switch_plan, "gpu.arena",
        saturating_add(saturating_add(gpu.buffer_extent, gpu.image_extent),
                       gpu.staging_largest_bytes),
        capacity(MemoryCategory::gpu));
    add(TrackWholePlatform::switch_plan, "transient.arena", frame,
        capacity(MemoryCategory::transient));
}

} // namespace nfsu
