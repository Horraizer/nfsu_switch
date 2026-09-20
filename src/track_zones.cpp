#include "nfsu/track_zones.h"

#include "nfsu/texture.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

namespace nfsu {
namespace {

static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 float32 required");
static_assert(sizeof(float) == 4, "32-bit float required");

static_assert(kTrackZonePolygonVertexOffset +
                      kTrackZoneMaxVertices * kTrackZoneVertexSize ==
                  kTrackZonePolygonRecordSize,
              "16 vertices fill a polygon record exactly");
static_assert(kTrackZoneSectionOffset + 2 * kTrackZoneMaxSections ==
                  kTrackZoneRecordSize,
              "64 section numbers fill a zone record exactly");
static_assert(kTrackZoneListSectionOffset + 2 * kTrackZoneTrackListCapacity ==
                  kTrackZoneTrackListRecordSize,
              "the 0x00034153 array fills its record exactly");
static_assert(kTrackZoneListSectionOffset + 2 * kTrackZoneExtraListCapacity ==
                  kTrackZoneExtraListRecordSize,
              "the 0x00034154 array fills its record exactly");

constexpr std::uint64_t kHeaderSize = 8;

std::uint16_t read_u16_le(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned int>(bytes[0]) |
        (static_cast<unsigned int>(bytes[1]) << 8U));
}

std::int16_t read_i16_le(const unsigned char* bytes) {
    return static_cast<std::int16_t>(read_u16_le(bytes));
}

std::uint32_t read_u32_le(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::int32_t read_i32_le(const unsigned char* bytes) {
    return static_cast<std::int32_t>(read_u32_le(bytes));
}

float read_f32_le(const unsigned char* bytes) {
    const std::uint32_t bits = read_u32_le(bytes);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a) {
        return false;
    }
    out = a + b;
    return true;
}

// A diagnostic must not depend on the process-global locale, exactly as in
// src/track_collision.cpp: two runs over the same bytes have to produce the
// same text.
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

std::string hex32(std::uint32_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text.push_back(digits[(value >> shift) & 0x0FU]);
    }
    return text;
}

struct RawChunk {
    std::uint32_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_size = 0;
    bool present = false;
};

// The four children, in the order the loader compares them, which is also
// the order a missing one is reported in.
struct ChildSlot {
    std::uint32_t id;
    std::size_t record_size;
    const char* what;
};

constexpr ChildSlot kChildren[] = {
    {kTrackZonePolygonId, kTrackZonePolygonRecordSize, "zone polygon list"},
    {kTrackZoneId, kTrackZoneRecordSize, "zone list"},
    {kTrackZoneTrackListId, kTrackZoneTrackListRecordSize, "track section list"},
    {kTrackZoneExtraListId, kTrackZoneExtraListRecordSize, "extra section list"},
};
constexpr std::size_t kChildCount = sizeof(kChildren) / sizeof(kChildren[0]);

class ZoneParser {
public:
    ZoneParser(
        const unsigned char* data,
        std::uint64_t size,
        std::uint64_t base,
        const TrackZoneLimits& limits)
        : data_(data), size_(size), base_(base), limits_(limits) {}

    TrackZoneReport run() {
        report_.region.offset = base_;
        report_.region.size = size_;
        report_.cost.region_bytes = size_;

        if (size_ > limits_.max_region_bytes) {
            issue(base_, Message() << "zone image is " << size_
                                   << " bytes, over the limit of "
                                   << limits_.max_region_bytes);
            return std::move(report_);
        }
        if (!walk_top_level()) {
            return std::move(report_);
        }
        if (!walk_container()) {
            return std::move(report_);
        }
        if (!require_children()) {
            return std::move(report_);
        }
        if (!parse_polygons()) {
            return std::move(report_);
        }
        if (!parse_zones()) {
            return std::move(report_);
        }
        if (!parse_section_lists()) {
            return std::move(report_);
        }
        if (!resolve_polygons()) {
            return std::move(report_);
        }
        report_.valid = report_.issues.empty();
        return std::move(report_);
    }

private:
    void issue(std::uint64_t offset, const Message& text) {
        TrackIssue entry;
        entry.offset = offset;
        entry.message = text.str();
        report_.issues.push_back(std::move(entry));
    }

    const unsigned char* at(std::uint64_t absolute) const {
        return data_ + (absolute - base_);
    }

    // Walks `(id, payload size)` chunks over [start, start + length) and
    // appends each to out. Returns false and raises the issue itself when
    // a header is short, a payload overruns or the run does not land
    // exactly on the end.
    bool walk_chunks(
        std::uint64_t start,
        std::uint64_t length,
        const char* what,
        std::vector<RawChunk>& out) {
        std::uint64_t cursor = start;
        const std::uint64_t end = start + length;
        while (cursor < end) {
            if (end - cursor < kHeaderSize) {
                issue(cursor, Message() << "trailing " << (end - cursor)
                                        << " bytes of the " << what
                                        << " are shorter than a chunk header");
                return false;
            }
            RawChunk chunk;
            chunk.present = true;
            chunk.offset = cursor;
            chunk.id = read_u32_le(at(cursor));
            chunk.payload_size = read_u32_le(at(cursor + 4));
            chunk.payload_offset = cursor + kHeaderSize;
            std::uint64_t stop = 0;
            if (!checked_add(chunk.payload_offset, chunk.payload_size, stop) ||
                stop > end) {
                issue(chunk.offset,
                      Message() << "chunk " << hex32(chunk.id)
                                << " claims a payload of " << chunk.payload_size
                                << " bytes, which reaches past the end of the "
                                << what);
                return false;
            }
            ++report_.cost.chunks;
            if (report_.cost.chunks > limits_.max_chunks) {
                issue(chunk.offset, Message() << "image holds more than "
                                              << limits_.max_chunks
                                              << " chunks");
                return false;
            }
            out.push_back(chunk);
            cursor = stop;
        }
        return true;
    }

    bool walk_top_level() {
        std::vector<RawChunk> top;
        if (!walk_chunks(base_, size_, "image", top)) {
            return false;
        }
        for (const RawChunk& chunk : top) {
            if (chunk.id != kTrackZoneContainerId) {
                continue;
            }
            if (container_.present) {
                issue(chunk.offset,
                      Message() << "image holds more than one "
                                << hex32(kTrackZoneContainerId)
                                << " visibility-zone container");
                return false;
            }
            container_ = chunk;
        }
        if (!container_.present) {
            issue(base_, Message() << "no " << hex32(kTrackZoneContainerId)
                                   << " visibility-zone container in the image");
            return false;
        }
        return true;
    }

    bool walk_container() {
        std::vector<RawChunk> kids;
        if (!walk_chunks(container_.payload_offset, container_.payload_size,
                         "visibility-zone container", kids)) {
            return false;
        }
        report_.cost.container_children = kids.size();
        for (const RawChunk& chunk : kids) {
            std::size_t slot = kChildCount;
            for (std::size_t i = 0; i < kChildCount; ++i) {
                if (kChildren[i].id == chunk.id) {
                    slot = i;
                    break;
                }
            }
            if (slot == kChildCount) {
                issue(chunk.offset,
                      Message() << "visibility-zone container holds an "
                                   "unknown child chunk "
                                << hex32(chunk.id));
                return false;
            }
            if (child_[slot].present) {
                issue(chunk.offset, Message()
                                        << "visibility-zone container holds "
                                           "more than one "
                                        << hex32(chunk.id) << " "
                                        << kChildren[slot].what << " chunk");
                return false;
            }
            child_[slot] = chunk;
        }
        return true;
    }

    // The polygon list and the zone list are what a zone lookup needs. The
    // other two are optional: three retail tracks ship an empty
    // 0x00034153 and one an empty 0x00034154, and a chunk that is absent
    // is the same case as one whose payload is zero bytes.
    bool require_children() {
        for (std::size_t slot = 0; slot < 2; ++slot) {
            if (!child_[slot].present) {
                issue(container_.offset,
                      Message() << "no " << hex32(kChildren[slot].id) << " "
                                << kChildren[slot].what
                                << " chunk in the visibility-zone container");
                return false;
            }
        }
        return true;
    }

    // The record count of one child, with the divisibility rule applied.
    bool record_count(std::size_t slot, std::uint64_t& count) {
        const RawChunk& chunk = child_[slot];
        if (!chunk.present) {
            count = 0;
            return true;
        }
        const std::size_t size = kChildren[slot].record_size;
        if (chunk.payload_size % size != 0) {
            issue(chunk.offset, Message()
                                    << kChildren[slot].what << " payload of "
                                    << chunk.payload_size
                                    << " bytes is not a whole number of "
                                    << size << "-byte records");
            return false;
        }
        count = chunk.payload_size / size;
        return true;
    }

    bool parse_polygons() {
        std::uint64_t count = 0;
        if (!record_count(0, count)) {
            return false;
        }
        if (count > limits_.max_polygons) {
            issue(child_[0].offset, Message()
                                        << "zone polygon list holds " << count
                                        << " records, over the limit of "
                                        << limits_.max_polygons);
            return false;
        }
        report_.cost.polygons = count;
        report_.polygons.resize(static_cast<std::size_t>(count));
        bool have_box = false;
        for (std::uint64_t i = 0; i < count; ++i) {
            TrackZonePolygon& polygon =
                report_.polygons[static_cast<std::size_t>(i)];
            const std::uint64_t offset =
                child_[0].payload_offset + i * kTrackZonePolygonRecordSize;
            const unsigned char* record = at(offset);
            polygon.index = static_cast<std::size_t>(i);
            polygon.offset = offset;
            polygon.stored_link[0] =
                read_u32_le(record + kTrackZoneLinkNextOffset);
            polygon.stored_link[1] =
                read_u32_le(record + kTrackZoneLinkPrevOffset);
            polygon.key = read_i16_le(record + kTrackZonePolygonKeyOffset);
            polygon.vertex_count =
                read_i16_le(record + kTrackZonePolygonVertexCountOffset);
            if (polygon.vertex_count < 1 ||
                static_cast<std::size_t>(polygon.vertex_count) >
                    kTrackZoneMaxVertices) {
                issue(offset + kTrackZonePolygonVertexCountOffset,
                      Message() << "zone polygon " << i << " claims "
                                << polygon.vertex_count
                                << " vertices; a record holds 1 to "
                                << kTrackZoneMaxVertices);
                return false;
            }
            for (std::size_t v = 0; v < kTrackZoneMaxVertices; ++v) {
                const unsigned char* slot =
                    record + kTrackZonePolygonVertexOffset +
                    v * kTrackZoneVertexSize;
                polygon.vertices[v][0] = read_f32_le(slot);
                polygon.vertices[v][1] = read_f32_le(slot + 4);
                if (v >= static_cast<std::size_t>(polygon.vertex_count)) {
                    continue;
                }
                ++report_.cost.polygon_vertices;
                for (std::size_t axis = 0; axis < 2; ++axis) {
                    const float value = polygon.vertices[v][axis];
                    if (!have_box) {
                        report_.cost.vertex_min[axis] = value;
                        report_.cost.vertex_max[axis] = value;
                    } else {
                        report_.cost.vertex_min[axis] =
                            std::min(report_.cost.vertex_min[axis], value);
                        report_.cost.vertex_max[axis] =
                            std::max(report_.cost.vertex_max[axis], value);
                    }
                }
                have_box = true;
            }
        }
        return check_unique_polygon_keys();
    }

    bool check_unique_polygon_keys() {
        std::vector<std::pair<std::int16_t, std::size_t>> order;
        order.reserve(report_.polygons.size());
        for (const TrackZonePolygon& polygon : report_.polygons) {
            order.emplace_back(polygon.key, polygon.index);
        }
        std::sort(order.begin(), order.end());
        for (std::size_t i = 1; i < order.size(); ++i) {
            if (order[i].first != order[i - 1].first) {
                continue;
            }
            const TrackZonePolygon& polygon = report_.polygons[order[i].second];
            issue(polygon.offset + kTrackZonePolygonKeyOffset,
                  Message() << "zone polygon " << polygon.index << " repeats "
                            << "the key " << polygon.key << " of polygon "
                            << order[i - 1].second
                            << "; the lookup at 0x00446EB9 would be ambiguous");
            return false;
        }
        return true;
    }

    bool parse_zones() {
        std::uint64_t count = 0;
        if (!record_count(1, count)) {
            return false;
        }
        if (count > limits_.max_zones) {
            issue(child_[1].offset, Message() << "zone list holds " << count
                                              << " records, over the limit of "
                                              << limits_.max_zones);
            return false;
        }
        report_.cost.zones = count;
        report_.zones.resize(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            TrackZone& zone = report_.zones[static_cast<std::size_t>(i)];
            const std::uint64_t offset =
                child_[1].payload_offset + i * kTrackZoneRecordSize;
            const unsigned char* record = at(offset);
            zone.index = static_cast<std::size_t>(i);
            zone.offset = offset;
            zone.stored_link[0] = read_u32_le(record + kTrackZoneLinkNextOffset);
            zone.stored_link[1] = read_u32_le(record + kTrackZoneLinkPrevOffset);
            zone.key = read_i16_le(record + kTrackZoneKeyOffset);
            zone.unknown = read_i16_le(record + kTrackZoneUnknownOffset);
            zone.stored_polygon_link =
                read_u32_le(record + kTrackZonePolygonLinkOffset);
            zone.section_count =
                read_i32_le(record + kTrackZoneSectionCountOffset);
            if (zone.section_count < 0 ||
                static_cast<std::size_t>(zone.section_count) >
                    kTrackZoneMaxSections) {
                issue(offset + kTrackZoneSectionCountOffset,
                      Message() << "zone " << i << " claims "
                                << zone.section_count
                                << " section numbers; a record holds 0 to "
                                << kTrackZoneMaxSections);
                return false;
            }
            for (std::size_t s = 0; s < kTrackZoneMaxSections; ++s) {
                zone.sections[s] =
                    read_i16_le(record + kTrackZoneSectionOffset + 2 * s);
            }
            report_.cost.zone_sections +=
                static_cast<std::uint64_t>(zone.section_count);
        }
        return check_unique_zone_keys();
    }

    bool check_unique_zone_keys() {
        std::vector<std::pair<std::int16_t, std::size_t>> order;
        order.reserve(report_.zones.size());
        for (const TrackZone& zone : report_.zones) {
            order.emplace_back(zone.key, zone.index);
        }
        std::sort(order.begin(), order.end());
        for (std::size_t i = 1; i < order.size(); ++i) {
            if (order[i].first != order[i - 1].first) {
                continue;
            }
            const TrackZone& zone = report_.zones[order[i].second];
            issue(zone.offset + kTrackZoneKeyOffset,
                  Message() << "zone " << zone.index << " repeats the key "
                            << zone.key << " of zone " << order[i - 1].second);
            return false;
        }
        return true;
    }

    bool parse_section_lists() {
        for (std::size_t slot = 2; slot < kChildCount; ++slot) {
            std::uint64_t count = 0;
            if (!record_count(slot, count)) {
                return false;
            }
            if (count > limits_.max_section_lists) {
                issue(child_[slot].offset,
                      Message() << kChildren[slot].what << " holds " << count
                                << " records, over the limit of "
                                << limits_.max_section_lists);
                return false;
            }
            const std::size_t capacity =
                (kChildren[slot].record_size - kTrackZoneListSectionOffset) / 2;
            for (std::uint64_t i = 0; i < count; ++i) {
                const std::uint64_t offset = child_[slot].payload_offset +
                                             i * kChildren[slot].record_size;
                const unsigned char* record = at(offset);
                TrackZoneSectionList list;
                list.chunk_id = kChildren[slot].id;
                list.index = static_cast<std::size_t>(i);
                list.offset = offset;
                list.stored_link[0] =
                    read_u32_le(record + kTrackZoneLinkNextOffset);
                list.stored_link[1] =
                    read_u32_le(record + kTrackZoneLinkPrevOffset);
                list.key = read_u32_le(record + kTrackZoneListKeyOffset);
                list.count = read_i32_le(record + kTrackZoneListCountOffset);
                if (list.count < 0 ||
                    static_cast<std::size_t>(list.count) > capacity) {
                    issue(offset + kTrackZoneListCountOffset,
                          Message()
                              << kChildren[slot].what << " record " << i
                              << " claims " << list.count
                              << " section numbers; a record holds 0 to "
                              << capacity);
                    return false;
                }
                list.sections.resize(capacity);
                for (std::size_t s = 0; s < capacity; ++s) {
                    list.sections[s] =
                        read_i16_le(record + kTrackZoneListSectionOffset + 2 * s);
                }
                report_.section_lists.push_back(std::move(list));
            }
        }
        return true;
    }

    // 0x00446EB9 walks the polygon list looking for the record whose +8
    // equals this zone's +8. Every key is unique by now, so the search is
    // an ordinary lookup and the first match is the only match.
    bool resolve_polygons() {
        for (TrackZone& zone : report_.zones) {
            zone.polygon_index = kTrackZoneNone;
            for (const TrackZonePolygon& polygon : report_.polygons) {
                if (polygon.key == zone.key) {
                    zone.polygon_index = polygon.index;
                    break;
                }
            }
            if (zone.polygon_index == kTrackZoneNone) {
                issue(zone.offset + kTrackZoneKeyOffset,
                      Message() << "zone " << zone.index << " key " << zone.key
                                << " names no " << hex32(kTrackZonePolygonId)
                                << " polygon; retail would read the vertex "
                                   "count through a null pointer at "
                                   "0x00446FE6");
                return false;
            }
        }
        return true;
    }

    const unsigned char* data_;
    std::uint64_t size_;
    std::uint64_t base_;
    TrackZoneLimits limits_;
    TrackZoneReport report_;
    RawChunk container_;
    RawChunk child_[kChildCount];
};

} // namespace

bool point_in_track_zone_polygon(
    const float point[2],
    const float* vertices,
    std::size_t vertex_count) noexcept {
    if (point == nullptr || vertices == nullptr || vertex_count == 0) {
        return false;
    }
    // 0x005679B3 and 0x005679B5: the two floats of the point the test uses.
    const float ps0 = point[0];
    const float ps1 = point[1];
    bool inside = false;
    for (std::size_t i = 0; i < vertex_count; ++i) {
        // 0x005679E3 seeds the predecessor with the last vertex and every
        // later step carries the previous one, so j is i - 1 modulo the
        // count.
        const std::size_t j = (i == 0) ? vertex_count - 1 : i - 1;
        const float is0 = vertices[2 * i];
        const float is1 = vertices[2 * i + 1];
        const float js0 = vertices[2 * j];
        const float js1 = vertices[2 * j + 1];
        // 0x005679FD to 0x00567A39. Two half-open bands, one per direction.
        // Written as the retail branches test them: the `jp` after
        // `test ah, 0x41` leaves the first arm when the loaded value is
        // greater than the point or the pair is unordered, which is exactly
        // `!(a <= b)`, and the `jnp` after `test ah, 5` takes it when the
        // point is strictly below, which is `a < b`.
        const bool rising = (is1 <= ps1) && (ps1 < js1);
        const bool falling = (js1 <= ps1) && (ps1 < is1);
        if (!rising && !falling) {
            continue;
        }
        // 0x00567A3B to 0x00567A4F, in that association: the difference of
        // the point and vertex i, times the run of the edge, over the rise
        // of the edge, plus vertex i. The band test above cannot be true
        // for js1 == is1, so this never divides by zero.
        const float crossing =
            is0 + (ps1 - is1) * (js0 - is0) / (js1 - is1);
        // 0x00567A52 `fcomp` then 0x00567A5B `jne`: the parity flips only
        // when the crossing is strictly greater than the point.
        if (crossing > ps0) {
            inside = !inside;
        }
    }
    return inside;
}

TrackZoneReport parse_track_zones(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t base_offset,
    const TrackZoneLimits& limits) {
    if (data == nullptr) {
        TrackZoneReport report;
        report.region.offset = base_offset;
        report.issues.push_back(TrackIssue{base_offset, "no zone image bytes"});
        return report;
    }
    ZoneParser parser(data, size, base_offset, limits);
    return parser.run();
}

TrackZoneReport load_track_zones(
    const std::filesystem::path& path,
    std::vector<unsigned char>& bytes,
    const TrackZoneLimits& limits) {
    bytes.clear();
    TrackZoneReport report;

    std::error_code code;
    const std::uintmax_t file_size = std::filesystem::file_size(path, code);
    if (code) {
        report.issues.push_back(
            TrackIssue{0, "cannot size the file: " + code.message()});
        return report;
    }
    if (static_cast<std::uint64_t>(file_size) > limits.max_packed_bytes) {
        report.issues.push_back(TrackIssue{
            0, (Message() << "file is " << static_cast<std::uint64_t>(file_size)
                          << " bytes, over the limit of "
                          << limits.max_packed_bytes)
                   .str()});
        return report;
    }

    std::vector<unsigned char> packed;
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            report.issues.push_back(TrackIssue{0, "cannot open the file"});
            return report;
        }
        packed.resize(static_cast<std::size_t>(file_size));
        if (file_size != 0 &&
            !stream.read(reinterpret_cast<char*>(packed.data()),
                         static_cast<std::streamsize>(file_size))) {
            report.issues.push_back(TrackIssue{0, "cannot read the file"});
            return report;
        }
    } catch (const std::exception& error) {
        report.issues.push_back(
            TrackIssue{0, std::string("cannot read the file: ") + error.what()});
        return report;
    }

    // The same sniff src/track_collision.cpp does, and the same decoder.
    // This file adds no decompressor of its own.
    std::uint64_t unused = 0;
    if (packed.size() >= 4 && packed[0] == 'J' && packed[1] == 'D' &&
        packed[2] == 'L' && packed[3] == 'Z') {
        std::size_t unused_input = 0;
        std::string error;
        const std::uint32_t cap = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(limits.max_region_bytes, 0xFFFFFFFFULL));
        if (!jdlz_decompress(packed.data(), packed.size(), cap, bytes,
                             unused_input, error)) {
            report.issues.push_back(TrackIssue{0, "JDLZ: " + error});
            return report;
        }
        unused = static_cast<std::uint64_t>(unused_input);
    } else {
        bytes = packed;
    }

    report = parse_track_zones(bytes.data(), bytes.size(), 0, limits);
    report.file_size = static_cast<std::uint64_t>(file_size);
    report.cost.packed_bytes = static_cast<std::uint64_t>(packed.size());
    report.cost.unused_packed_bytes = unused;
    return report;
}

std::size_t find_track_zone(
    const TrackZoneReport& report,
    const float point[2]) noexcept {
    if (!report.valid || point == nullptr) {
        return kTrackZoneNone;
    }
    for (const TrackZone& zone : report.zones) {
        if (zone.polygon_index >= report.polygons.size()) {
            continue;
        }
        const TrackZonePolygon& polygon = report.polygons[zone.polygon_index];
        if (polygon.vertex_count < 1) {
            continue;
        }
        if (point_in_track_zone_polygon(
                point, &polygon.vertices[0][0],
                static_cast<std::size_t>(polygon.vertex_count))) {
            return zone.index;
        }
    }
    return kTrackZoneNone;
}

bool track_zone_residency(
    const TrackZoneReport& report,
    std::size_t zone_index,
    TrackResidency& out) {
    if (!report.valid || zone_index >= report.zones.size()) {
        return false;
    }
    const TrackZone& zone = report.zones[zone_index];
    TrackResidency residency;
    residency.rule = TrackResidencyRule::zone_list;
    for (std::int32_t i = 0; i < zone.section_count; ++i) {
        const std::int16_t number = zone.sections[static_cast<std::size_t>(i)];
        // 0x004464B3 `test eax, eax` / `jl`: a negative entry is skipped.
        if (number < 0) {
            continue;
        }
        residency.zone_sections.push_back(
            static_cast<std::uint32_t>(static_cast<std::uint16_t>(number)));
    }
    out = std::move(residency);
    return true;
}

std::vector<std::uint32_t> track_zone_draw_list(
    const TrackZoneReport& report,
    std::size_t zone_index,
    const std::vector<std::uint32_t>& resident_numbers,
    std::size_t cap) {
    std::vector<std::uint32_t> out;
    if (!report.valid || zone_index >= report.zones.size()) {
        return out;
    }
    const auto resident = [&resident_numbers](std::uint32_t number) {
        for (std::uint32_t candidate : resident_numbers) {
            if (candidate == number) {
                return true;
            }
        }
        return false;
    };
    // 0x00446473 to 0x0044649C: the band, ascending, each entry appended
    // only when the global table holds it.
    for (std::uint32_t number = kTrackAlwaysResidentFirst;
         number <= kTrackAlwaysResidentLast; ++number) {
        if (out.size() >= cap) {
            return out;
        }
        if (resident(number)) {
            out.push_back(number);
        }
    }
    // 0x004464B0 to 0x004464D6: the zone's own numbers, in its own order.
    const TrackZone& zone = report.zones[zone_index];
    for (std::int32_t i = 0; i < zone.section_count; ++i) {
        if (out.size() >= cap) {
            return out;
        }
        const std::int16_t number = zone.sections[static_cast<std::size_t>(i)];
        if (number < 0) {
            continue;
        }
        const std::uint32_t value =
            static_cast<std::uint32_t>(static_cast<std::uint16_t>(number));
        if (resident(value)) {
            out.push_back(value);
        }
    }
    return out;
}

std::vector<std::uint32_t> track_no_zone_draw_list(
    const std::vector<std::uint32_t>& loaded_numbers,
    std::size_t cap) {
    std::vector<std::uint32_t> out;
    // 0x00446448 to 0x0044646F, including the cap check at 0x0044645E that
    // the zone arm also makes.
    for (std::uint32_t number : loaded_numbers) {
        if (out.size() >= cap) {
            return out;
        }
        if (track_section_resident_without_zone(number)) {
            out.push_back(number);
        }
    }
    return out;
}

} // namespace nfsu
