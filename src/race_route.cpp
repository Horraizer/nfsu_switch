#include "nfsu/race_route.h"

#include "nfsu/texture.h"
#include "nfsu/track_zones.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>
#include <utility>

namespace nfsu {
namespace {

constexpr std::size_t kChunkHeaderSize = 8;

// 0x00443A41 `fcomp [0x6CC7C4]`: a query further than this from the last
// position is searched from every node.
constexpr float kReseedDistance = 128.0F;
// 0x00443B57 `push 0x3d4ccccd`: the nearest-point tolerance.
constexpr float kNearestTolerance = 0.05F;
// 0x0059D596 `mov dword ptr [esp + 0x2c], 0x3e6`: the iteration limit.
constexpr int kNearestIterations = 998;
// 0x004433BF `push 0x10`: chords per segment in the arc-length table.
constexpr int kArcChords = 16;
// 0x00434A38 / 0x00434A4D: a percent difference is wrapped into [-50, 50].
constexpr float kWrapHalf = 50.0F;
constexpr float kWrapWhole = 100.0F;
// 0x0043482B `push 0x40a00000` and 0x6CCD84, 0x6CCD80, 0x6CCD7C.
constexpr float kDefaultStart = 5.0F;
constexpr float kDefaultSplitA = 33.0F;
constexpr float kDefaultSplitB = 66.0F;
constexpr float kDefaultFinish = 95.0F;
// 0x004348B0 `push 0xbf800000`: no default for the third split.
constexpr float kNoDefault = -1.0F;
// 0x00430EFB..0x00430F47: 110 degrees, and 10 more while already on.
// Each is `(int)(degrees * 65536.0) / 360` in a 16-bit angle.
constexpr unsigned int kWrongWayAngle = 20024U;
constexpr unsigned int kWrongWayHysteresis = 1820U;
// 0x00430F55..0x00430F63: speed (m/s) * 2.237 against 10.
constexpr float kMilesPerHourPerMetre = 2.237F;
constexpr float kWrongWaySpeed = 10.0F;

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

std::int32_t read_s32(const unsigned char* bytes) noexcept {
    return static_cast<std::int32_t>(read_u32(bytes));
}

float read_f32(const unsigned char* bytes) noexcept {
    const std::uint32_t bits = read_u32(bytes);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

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

bool list_chunks(const unsigned char* data,
                 std::uint64_t begin,
                 std::uint64_t end,
                 std::size_t max_chunks,
                 std::vector<ChunkSpan>& chunks,
                 std::vector<RaceRouteIssue>& issues) {
    std::uint64_t offset = begin;
    while (offset < end) {
        if (end - offset < kChunkHeaderSize) {
            issues.push_back(RaceRouteIssue{
                offset, "trailing bytes are smaller than a chunk header"});
            return false;
        }
        if (chunks.size() >= max_chunks) {
            issues.push_back(RaceRouteIssue{offset, "too many chunks"});
            return false;
        }
        const std::uint32_t id = read_u32(data + offset);
        const std::uint64_t size = read_u32(data + offset + 4);
        const std::uint64_t payload = offset + kChunkHeaderSize;
        if (size > end - payload) {
            issues.push_back(RaceRouteIssue{
                offset, "chunk " + hex_id(id) + " extends beyond its parent"});
            return false;
        }
        chunks.push_back(ChunkSpan{id, offset, payload, size});
        offset = payload + size;
    }
    return true;
}

const ChunkSpan* single_chunk(const std::vector<ChunkSpan>& chunks,
                              std::uint32_t id,
                              std::vector<RaceRouteIssue>& issues) {
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
        issues.push_back(RaceRouteIssue{
            count == 0 ? 0 : found->offset,
            (Message() << "expected exactly one chunk " << hex_id(id)
                       << ", found " << count)
                .str()});
        return nullptr;
    }
    return found;
}

bool finite2(const float* values) noexcept {
    return std::isfinite(values[0]) && std::isfinite(values[1]);
}

bool read_image(const std::filesystem::path& path,
                const RaceRouteLimits& limits,
                std::vector<unsigned char>& image,
                std::uint64_t& stored_size,
                bool& jdlz,
                std::vector<RaceRouteIssue>& issues) {
    image.clear();
    stored_size = 0;
    jdlz = false;

    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        issues.push_back(
            RaceRouteIssue{0, "cannot size the file: " + code.message()});
        return false;
    }
    stored_size = static_cast<std::uint64_t>(size);
    if (stored_size > limits.max_file_size) {
        issues.push_back(RaceRouteIssue{
            0, (Message() << "file is " << stored_size
                          << " bytes, over the limit of " << limits.max_file_size)
                   .str()});
        return false;
    }

    std::vector<unsigned char> stored;
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            issues.push_back(RaceRouteIssue{0, "cannot open the file"});
            return false;
        }
        stored.resize(static_cast<std::size_t>(stored_size));
        if (stored_size != 0 &&
            !stream.read(reinterpret_cast<char*>(stored.data()),
                         static_cast<std::streamsize>(stored_size))) {
            issues.push_back(RaceRouteIssue{0, "cannot read the file"});
            return false;
        }
    } catch (const std::exception& error) {
        issues.push_back(
            RaceRouteIssue{0, std::string("cannot read the file: ") + error.what()});
        return false;
    }

    // The same sniff and the same decoder as src/race_events.cpp.
    if (stored.size() >= 4 && stored[0] == 'J' && stored[1] == 'D' &&
        stored[2] == 'L' && stored[3] == 'Z') {
        jdlz = true;
        std::size_t unused_input = 0;
        std::string error;
        const auto cap = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(limits.max_image_size, 0xFFFFFFFFULL));
        if (!jdlz_decompress(stored.data(), stored.size(), cap, image,
                             unused_input, error)) {
            issues.push_back(RaceRouteIssue{0, "JDLZ: " + error});
            return false;
        }
    } else {
        image = std::move(stored);
    }
    return true;
}

// The three children of 0x80034147, in the order the loader takes them.
bool parse_path_container(const unsigned char* data,
                          const ChunkSpan& container,
                          const RaceRouteLimits& limits,
                          RaceRouteTrack& track) {
    std::vector<ChunkSpan> children;
    if (!list_chunks(data, container.payload_offset,
                     container.payload_offset + container.payload_size, 16,
                     children, track.issues)) {
        return false;
    }
    static constexpr std::uint32_t kIds[3] = {kTrackPathRecordChunkId,
                                              kTrackPathNodeChunkId,
                                              kTrackPathZoneChunkId};
    static constexpr std::size_t kSizes[3] = {
        kTrackPathRecordSize, kTrackPathNodeSize, kTrackPathZoneSize};
    if (children.size() != 3) {
        track.issues.push_back(RaceRouteIssue{
            container.offset,
            (Message() << "container " << hex_id(kTrackPathContainerId)
                       << " holds " << children.size()
                       << " chunks; the loader (0x004434D0) takes exactly three")
                .str()});
        return false;
    }
    for (std::size_t c = 0; c < 3; ++c) {
        if (children[c].id != kIds[c]) {
            track.issues.push_back(RaceRouteIssue{
                children[c].offset,
                (Message() << "child " << c << " of "
                           << hex_id(kTrackPathContainerId) << " is "
                           << hex_id(children[c].id) << ", expected "
                           << hex_id(kIds[c]))
                    .str()});
            return false;
        }
        if (children[c].payload_size % kSizes[c] != 0) {
            track.issues.push_back(RaceRouteIssue{
                children[c].offset,
                (Message() << "chunk " << hex_id(kIds[c]) << " holds "
                           << children[c].payload_size
                           << " bytes, not a whole number of " << kSizes[c]
                           << "-byte records")
                    .str()});
            return false;
        }
    }
    track.path_chunk_offset = children[0].offset;
    track.node_chunk_offset = children[1].offset;
    track.zone_chunk_offset = children[2].offset;

    const std::size_t path_count =
        static_cast<std::size_t>(children[0].payload_size / kTrackPathRecordSize);
    const std::size_t node_count =
        static_cast<std::size_t>(children[1].payload_size / kTrackPathNodeSize);
    const std::size_t zone_count =
        static_cast<std::size_t>(children[2].payload_size / kTrackPathZoneSize);
    if (path_count == 0) {
        track.issues.push_back(
            RaceRouteIssue{children[0].offset, "the path table is empty"});
        return false;
    }
    if (node_count > limits.max_nodes || zone_count > limits.max_zones) {
        track.issues.push_back(RaceRouteIssue{
            container.offset, (Message() << node_count << " nodes and "
                                         << zone_count
                                         << " zones exceed the limits")
                                  .str()});
        return false;
    }

    bool ok = true;
    for (std::size_t n = 0; n < node_count; ++n) {
        const std::uint64_t at = children[1].payload_offset + n * kTrackPathNodeSize;
        const unsigned char* bytes = data + at;
        TrackPathNode node;
        node.offset = at;
        node.x = read_f32(bytes);
        node.y = read_f32(bytes + 4);
        node.path = static_cast<std::int8_t>(bytes[8]);
        node.byte9 = bytes[9];
        node.links[0] = read_s16(bytes + 0x0A);
        node.links[1] = read_s16(bytes + 0x0C);
        node.links[2] = read_s16(bytes + 0x0E);
        node.distance = read_f32(bytes + 0x10);
        if (!std::isfinite(node.x) || !std::isfinite(node.y) ||
            !std::isfinite(node.distance)) {
            track.issues.push_back(RaceRouteIssue{at, "node holds a value that is not finite"});
            ok = false;
        }
        if (node.path < 0 || static_cast<std::size_t>(node.path) >= path_count) {
            track.issues.push_back(RaceRouteIssue{
                at + 8, (Message() << "node names path " << static_cast<int>(node.path)
                                   << "; the table holds " << path_count
                                   << " (0x00443B02 indexes it unchecked)")
                            .str()});
            ok = false;
        }
        for (int k = 0; k < 3; ++k) {
            if (node.links[k] < -1 ||
                (node.links[k] >= 0 &&
                 static_cast<std::size_t>(node.links[k]) >= node_count)) {
                track.issues.push_back(RaceRouteIssue{
                    at + 0x0A + static_cast<std::uint64_t>(k) * 2,
                    (Message() << "node link " << node.links[k]
                               << " is neither -1 nor a node")
                        .str()});
                ok = false;
            }
        }
        track.nodes.push_back(node);
    }

    for (std::size_t p = 0; p < path_count; ++p) {
        const std::uint64_t at = children[0].payload_offset + p * kTrackPathRecordSize;
        const unsigned char* bytes = data + at;
        TrackPathRecord record;
        record.offset = at;
        record.byte0 = bytes[0];
        record.closed = bytes[1];
        record.first = read_s16(bytes + 2);
        record.end = read_s16(bytes + 4);
        record.count = read_s16(bytes + 6);
        record.distance_offset = read_f32(bytes + 0x08);
        record.length = read_f32(bytes + 0x0C);
        record.distance_scale = read_f32(bytes + 0x10);
        record.start_point[0] = read_f32(bytes + 0x14);
        record.start_point[1] = read_f32(bytes + 0x18);
        record.end_point[0] = read_f32(bytes + 0x1C);
        record.end_point[1] = read_f32(bytes + 0x20);
        if (record.first < 0 || record.end < record.first ||
            static_cast<std::size_t>(record.end) > node_count) {
            track.issues.push_back(RaceRouteIssue{
                at + 2, (Message() << "node range " << record.first << ".."
                                   << record.end << " is not inside the "
                                   << node_count << " nodes")
                            .str()});
            ok = false;
        } else if (record.count != record.end - record.first || record.count < 3) {
            track.issues.push_back(RaceRouteIssue{
                at + 6, (Message() << "control-point count " << record.count
                                   << " must equal end - first ("
                                   << record.end - record.first
                                   << ") and be at least 3")
                            .str()});
            ok = false;
        }
        if (!std::isfinite(record.length) || !(record.length > 0.0F) ||
            !std::isfinite(record.distance_scale) ||
            !std::isfinite(record.distance_offset)) {
            track.issues.push_back(RaceRouteIssue{
                at + 8, "path length must be finite and positive, and its "
                        "scale and offset finite"});
            ok = false;
        }
        track.paths.push_back(record);
    }

    std::uint32_t previous_type = 0;
    for (std::size_t z = 0; z < zone_count; ++z) {
        const std::uint64_t at = children[2].payload_offset + z * kTrackPathZoneSize;
        const unsigned char* bytes = data + at;
        TrackPathZone zone;
        zone.offset = at;
        zone.type = read_u32(bytes);
        zone.point[0] = read_f32(bytes + 0x04);
        zone.point[1] = read_f32(bytes + 0x08);
        zone.direction[0] = read_f32(bytes + 0x0C);
        zone.direction[1] = read_f32(bytes + 0x10);
        zone.height = read_f32(bytes + 0x14);
        zone.direction_filter = static_cast<std::int8_t>(bytes[0x18]);
        zone.distances[0] = read_f32(bytes + 0x1C);
        zone.distances[1] = read_f32(bytes + 0x20);
        zone.distances[2] = read_f32(bytes + 0x24);
        zone.box_min[0] = read_f32(bytes + 0x28);
        zone.box_min[1] = read_f32(bytes + 0x2C);
        zone.box_max[0] = read_f32(bytes + 0x30);
        zone.box_max[1] = read_f32(bytes + 0x34);
        zone.vertex_count = read_s32(bytes + 0x38);
        if (zone.type >= kTrackPathZoneTypes) {
            track.issues.push_back(RaceRouteIssue{
                at, (Message() << "zone type " << zone.type
                               << " is not one of the 11 the loader indexes")
                        .str()});
            ok = false;
        } else if (zone.type < previous_type) {
            track.issues.push_back(RaceRouteIssue{
                at, (Message() << "zone type " << zone.type << " follows type "
                               << previous_type
                               << "; 0x00443450 indexes a table sorted by type")
                        .str()});
            ok = false;
        } else {
            previous_type = zone.type;
        }
        if (zone.vertex_count < 0 ||
            static_cast<std::size_t>(zone.vertex_count) > kTrackPathZoneMaxVertices) {
            track.issues.push_back(RaceRouteIssue{
                at + 0x38, (Message() << "zone vertex count " << zone.vertex_count
                                      << " does not fit the record")
                               .str()});
            ok = false;
            zone.vertex_count = 0;
        }
        for (std::int32_t v = 0; v < zone.vertex_count; ++v) {
            zone.vertices.push_back(read_f32(bytes + 0x3C + v * 8));
            zone.vertices.push_back(read_f32(bytes + 0x3C + v * 8 + 4));
        }
        bool finite = finite2(zone.point) && finite2(zone.direction) &&
                      finite2(zone.box_min) && finite2(zone.box_max) &&
                      std::isfinite(zone.height);
        for (const float value : zone.vertices) {
            finite = finite && std::isfinite(value);
        }
        if (!finite) {
            track.issues.push_back(RaceRouteIssue{at, "zone holds a value that is not finite"});
            ok = false;
        }
        track.zones.push_back(std::move(zone));
    }
    return ok;
}

bool parse_markers(const unsigned char* data,
                   const ChunkSpan& chunk,
                   const RaceRouteLimits& limits,
                   RaceRouteTrack& track) {
    const std::uint64_t first =
        (chunk.payload_offset + (kTrackMarkerAlignment - 1)) &
        ~static_cast<std::uint64_t>(kTrackMarkerAlignment - 1);
    const std::uint64_t pad = first - chunk.payload_offset;
    if (pad > chunk.payload_size ||
        (chunk.payload_size - pad) % kTrackMarkerRecordSize != 0) {
        track.issues.push_back(RaceRouteIssue{
            chunk.offset,
            (Message() << "chunk " << hex_id(kTrackMarkerChunkId) << " holds "
                       << chunk.payload_size << " bytes, which after " << pad
                       << " bytes of alignment is not a whole number of "
                       << kTrackMarkerRecordSize << "-byte records")
                .str()});
        return false;
    }
    const std::size_t count =
        static_cast<std::size_t>((chunk.payload_size - pad) / kTrackMarkerRecordSize);
    if (count > limits.max_markers) {
        track.issues.push_back(RaceRouteIssue{chunk.offset, "too many markers"});
        return false;
    }
    track.marker_chunk_offset = chunk.offset;
    track.marker_record_offset = first;
    bool ok = true;
    for (std::size_t m = 0; m < count; ++m) {
        const std::uint64_t at = first + m * kTrackMarkerRecordSize;
        const unsigned char* bytes = data + at;
        TrackMarker marker;
        marker.offset = at;
        marker.name_hash = read_u32(bytes + kTrackMarkerHashOffset);
        marker.param = read_u32(bytes + kTrackMarkerParamOffset);
        for (int k = 0; k < 3; ++k) {
            marker.position[k] = read_f32(bytes + kTrackMarkerPositionOffset +
                                          static_cast<std::size_t>(k) * 4);
        }
        marker.angle = read_u16(bytes + kTrackMarkerAngleOffset);
        if (!std::isfinite(marker.position[0]) || !std::isfinite(marker.position[1]) ||
            !std::isfinite(marker.position[2])) {
            track.issues.push_back(RaceRouteIssue{
                at + kTrackMarkerPositionOffset, "marker position is not finite"});
            ok = false;
        }
        track.markers.push_back(marker);
    }
    return ok;
}

// ---------------------------------------------------------------------
// The spline
// ---------------------------------------------------------------------

// 0x0059C85D..0x0059CA3A: the basis rows (the weight of control point k as
// 1, u, u^2 and u^3 coefficients) for the position and its derivative. The
// executable stores them in this order and transposes them once
// (0x0059CA44 call 0x00565230); the weights below are the same numbers.
constexpr float kBasis[2][4][4] = {
    {{0.0F, -0.5F, 1.0F, -0.5F},
     {1.0F, 0.0F, -2.5F, 1.5F},
     {0.0F, 0.5F, 2.0F, -1.5F},
     {0.0F, 0.0F, -0.5F, 0.5F}},
    {{-0.5F, 2.0F, -1.5F, 0.0F},
     {0.0F, -5.0F, 4.5F, 0.0F},
     {0.5F, 4.0F, -4.5F, 0.0F},
     {0.0F, -1.0F, 1.5F, 0.0F}},
};

// 0x0059CE10 for a closed spline: t / n, less its floor, times n.
float wrap_parameter(float t, std::size_t count) noexcept {
    const float n = static_cast<float>(count);
    const float q = t / n;
    const float f = std::floor(q);
    return (q - f) * n;
}

// Control point j of a closed spline, with the running length carried
// around the loop as 0x0059CD8D..0x0059CDA5 do for the three copies at
// -1, n and n + 1.
void control_point(const TrackPathSpline& spline,
                   std::ptrdiff_t j,
                   float length,
                   float out[3]) noexcept {
    const auto n = static_cast<std::ptrdiff_t>(spline.count);
    float shift = 0.0F;
    if (j < 0) {
        j += n;
        shift = -length;
    } else if (j >= n) {
        j -= n;
        shift = length;
    }
    const float* point = &spline.points[static_cast<std::size_t>(j) * 3];
    out[0] = point[0];
    out[1] = point[1];
    // 0x0059CD8D `fld [ecx - 4]; fsub [edi + 0x14]` below zero and
    // 0x0059CD99 / 0x0059CD9F `fadd` above the end.
    out[2] = shift == 0.0F ? point[2] : (shift < 0.0F ? point[2] - length : length + point[2]);
}

// 0x0059CDB0: (1, u, u^2, u^3) through the basis, then over the four
// control points from `segment - 1`. The running length is evaluated like
// a coordinate. `length` is the loop length the copies are shifted by; it
// is zero while 0x0059D9F0 is still measuring it.
void evaluate_segment(const TrackPathSpline& spline,
                      std::ptrdiff_t segment,
                      float u,
                      int derivative,
                      float length,
                      float out[3]) noexcept {
    const float u2 = u * u;
    const float u3 = u2 * u;
    const float powers[4] = {1.0F, u, u2, u3};
    const auto& basis = kBasis[derivative == 0 ? 0 : 1];
    float weights[4];
    for (int k = 0; k < 4; ++k) {
        // 0x005AC449: the terms are added as u^3, u, u^2, then 1.
        weights[k] = ((powers[3] * basis[k][3] + powers[1] * basis[k][1]) +
                      powers[2] * basis[k][2]) +
                     powers[0] * basis[k][0];
    }
    float points[4][3];
    for (int k = 0; k < 4; ++k) {
        control_point(spline, segment - 1 + k, length, points[k]);
    }
    for (int c = 0; c < 3; ++c) {
        out[c] = ((points[3][c] * weights[3] + points[1][c] * weights[1]) +
                  points[2][c] * weights[2]) +
                 points[0][c] * weights[0];
    }
}

// 0x0059D350: wrap, split into a segment and a fraction, evaluate.
void evaluate_at(const TrackPathSpline& spline,
                 float t,
                 int derivative,
                 float length,
                 float out[3]) noexcept {
    const float wrapped = wrap_parameter(t, spline.count);
    float whole = std::floor(wrapped);
    float u = wrapped - whole;
    // A parameter a hair below zero wraps to exactly the count in float,
    // which would read one control point past the copies 0x0059CB90 keeps.
    // It is the same place as parameter 0, and is evaluated there; retail
    // has no such guard.
    if (!(whole < static_cast<float>(spline.count))) {
        whole = 0.0F;
        u = 0.0F;
    }
    evaluate_segment(spline, static_cast<std::ptrdiff_t>(whole), u, derivative,
                     length, out);
}

// 0x0059D540: the secant iteration toward the parameter whose point is
// nearest `query`, from `start`. Returns the parameter; `iterations` says
// how many evaluations it took.
float nearest_parameter(const TrackPathSpline& spline,
                        const float query[2],
                        float start,
                        int& iterations) noexcept {
    float t = start;
    float step = 1.0F;
    float previous_move = 0.0F;
    float previous_along = 0.0F;
    iterations = 0;
    for (int left = kNearestIterations; left > 0; --left) {
        ++iterations;
        float point[3];
        float slope[3];
        evaluate_at(spline, t, 0, spline.spline_length, point);
        evaluate_at(spline, t, 1, spline.spline_length, slope);
        // 0x0059D5E6..0x0059D5F6: (z^2 + y^2) + x^2 of the derivative. The
        // spline is flat, so its z terms are zero.
        const float slope_length =
            std::sqrt(0.0F * 0.0F + slope[1] * slope[1] + slope[0] * slope[0]);
        if (slope_length == 0.0F) {
            return t;
        }
        // 0x0059D60D..0x0059D62F: how far along the tangent the query lies.
        const float ex = query[0] - point[0];
        const float ey = query[1] - point[1];
        const float along = ((0.0F + ey * slope[1]) + ex * slope[0]) / slope_length;
        if (std::fabs(along) < kNearestTolerance) {
            return t;
        }
        float rate = slope_length;
        if (previous_move != 0.0F) {
            // 0x0059D668..0x0059D674: the secant of the last two steps.
            rate = -((along - previous_along) / previous_move);
            if (rate == 0.0F) {
                return t;
            }
        }
        float move = along / rate;
        // 0x0059D68B..0x0059D6BF: clamped to [-step, step].
        const float low = -step;
        if (!(move > low)) {
            move = low;
        }
        if (!(step >= move)) {
            move = step;
        }
        t = wrap_parameter(move + t, spline.count);
        // 0x0059D6DA..0x0059D6F9: a change of direction halves the step.
        if (move * previous_move < 0.0F) {
            step = step * 0.5F;
        }
        previous_move = move;
        previous_along = along;
    }
    return t;
}

float distance2(float ax, float ay, float bx, float by) noexcept {
    const float dx = ax - bx;
    const float dy = ay - by;
    return std::sqrt(dy * dy + dx * dx);
}

// 0x00443630: the node nearest `query` over every path's node range.
int nearest_node(const RaceRouteTrack& track, const float query[2]) noexcept {
    int best = -1;
    float best_distance = 0.0F;
    for (const TrackPathRecord& path : track.paths) {
        for (int n = path.first; n < path.end; ++n) {
            const TrackPathNode& node = track.nodes[static_cast<std::size_t>(n)];
            const float d = distance2(node.x, node.y, query[0], query[1]);
            if (best < 0 || d < best_distance) {
                best = n;
                best_distance = d;
            }
        }
    }
    return best;
}

// 0x00443920: the node, the one before it and the one after it, wrapping
// on a closed path; the nearest of them, the first on a tie.
int nearer_neighbour(const RaceRouteTrack& track, int node, const float query[2]) noexcept {
    const TrackPathNode& at = track.nodes[static_cast<std::size_t>(node)];
    const TrackPathRecord& path = track.paths[static_cast<std::size_t>(at.path)];
    int candidates[3] = {node, -1, -1};
    int count = 1;
    if (node != path.first) {
        candidates[count++] = node - 1;
    } else if (path.closed != 0) {
        candidates[count++] = path.end - 1;
    }
    if (node != path.end - 1) {
        candidates[count++] = node + 1;
    } else if (path.closed != 0) {
        candidates[count++] = path.first;
    }
    int best = -1;
    float best_distance = 0.0F;
    for (int k = 0; k < count; ++k) {
        const TrackPathNode& candidate =
            track.nodes[static_cast<std::size_t>(candidates[k])];
        const float d = distance2(query[0], query[1], candidate.x, candidate.y);
        if (best < 0 || d < best_distance) {
            best = candidates[k];
            best_distance = d;
        }
    }
    // 0x00443AB3..0x00443AFD: then each linked node, when nearer still.
    const TrackPathNode& chosen = track.nodes[static_cast<std::size_t>(best)];
    for (int k = 0; k < 3; ++k) {
        const int link = chosen.links[k];
        if (link < 0) {
            continue;
        }
        const TrackPathNode& linked = track.nodes[static_cast<std::size_t>(link)];
        const float d = distance2(query[0], query[1], linked.x, linked.y);
        if (d < best_distance) {
            best = link;
            best_distance = d;
        }
    }
    return best;
}

// A percent difference wrapped into [-50, 50] on a closed path.
float wrap_percent(float difference) noexcept {
    if (difference < -kWrapHalf) {
        difference += kWrapWhole;
    } else if (difference > kWrapHalf) {
        difference -= kWrapWhole;
    }
    return difference;
}

// The 16-bit angle of a direction. 0x00564DB0 computes it and is not
// traced; atan2 stands in, and only differences of two angles are used.
unsigned int angle16(float x, float y) noexcept {
    const double radians = std::atan2(static_cast<double>(y), static_cast<double>(x));
    const double units = radians * (65536.0 / (2.0 * 3.14159265358979323846));
    const long whole = std::lround(units);
    return static_cast<unsigned int>(static_cast<unsigned long>(whole) & 0xFFFFUL);
}

// The smaller way round between two 16-bit angles, as 0x00430EE2..0x00430EF1
// take it: the difference, negated when above 0x8000.
unsigned int angle_between(unsigned int a, unsigned int b) noexcept {
    unsigned int difference = (a - b) & 0xFFFFU;
    if (difference > 0x8000U) {
        difference = (0U - difference) & 0xFFFFU;
    }
    return difference;
}

// 0x004436D0: the first zone of `type`, in table order, whose direction
// filter admits `direction`, whose box holds the point and whose polygon
// holds it by 0x005679B0.
int find_zone(const RaceRouteTrack& track,
              std::uint32_t type,
              int direction,
              const float point[2]) {
    for (std::size_t z = 0; z < track.zones.size(); ++z) {
        const TrackPathZone& zone = track.zones[z];
        if (zone.type != type) {
            continue;
        }
        if (zone.direction_filter != -1 && zone.direction_filter != direction) {
            continue;
        }
        if (point[0] < zone.box_min[0] || point[0] > zone.box_max[0] ||
            point[1] < zone.box_min[1] || point[1] > zone.box_max[1]) {
            continue;
        }
        if (point_in_track_zone_polygon(point, zone.vertices.data(),
                                        static_cast<std::size_t>(zone.vertex_count))) {
            return static_cast<int>(z);
        }
    }
    return -1;
}

} // namespace

// ---------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------

RaceRouteTrack parse_race_route_track(const unsigned char* data,
                                      std::size_t size,
                                      const RaceRouteLimits& limits) {
    RaceRouteTrack track;
    track.image_size = size;
    if (data == nullptr && size != 0) {
        track.issues.push_back(RaceRouteIssue{0, "no image"});
        return track;
    }
    if (size > limits.max_image_size) {
        track.issues.push_back(RaceRouteIssue{
            0, (Message() << "image is " << size << " bytes, over the limit of "
                          << limits.max_image_size)
                   .str()});
        return track;
    }
    std::vector<ChunkSpan> chunks;
    if (!list_chunks(data, 0, size, limits.max_top_level_chunks, chunks,
                     track.issues)) {
        return track;
    }
    const ChunkSpan* container = single_chunk(chunks, kTrackPathContainerId, track.issues);
    const ChunkSpan* markers = single_chunk(chunks, kTrackMarkerChunkId, track.issues);
    if (container == nullptr || markers == nullptr) {
        return track;
    }
    track.container_offset = container->offset;
    const bool path_ok = parse_path_container(data, *container, limits, track);
    const bool marker_ok = parse_markers(data, *markers, limits, track);
    track.valid = path_ok && marker_ok && track.issues.empty();
    return track;
}

RaceRouteTrack load_race_route_track(const std::filesystem::path& path,
                                     std::vector<unsigned char>& image,
                                     const RaceRouteLimits& limits) {
    std::vector<RaceRouteIssue> issues;
    std::uint64_t stored_size = 0;
    bool jdlz = false;
    RaceRouteTrack track;
    if (read_image(path, limits, image, stored_size, jdlz, issues)) {
        track = parse_race_route_track(image.data(), image.size(), limits);
    } else {
        track.issues = std::move(issues);
    }
    track.stored_size = stored_size;
    track.jdlz = jdlz;
    return track;
}

std::uint32_t track_marker_hash(std::string_view name) noexcept {
    std::uint32_t hash = 0xFFFFFFFFU;
    for (const char ch : name) {
        hash = hash * 33U + static_cast<unsigned char>(ch);
    }
    return hash;
}

const char* track_marker_name(std::uint32_t hash) noexcept {
    // The names the executable hashes: 0x006CA4E8..0x006CA560 and
    // 0x006CAE04 / 0x006CAE20.
    static constexpr const char* kNames[] = {
        "ForwardStartLine",      "ForwardFinishLine",
        "BackwardStartLine",     "BackwardFinishLine",
        "SplitTimeBoundary1",    "SplitTimeBoundary2",
        "SplitTimeBoundary3",    "ForwardStartingPosition",
        "BackwardStartingPosition",
    };
    for (const char* name : kNames) {
        if (track_marker_hash(name) == hash) {
            return name;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------
// The spline and the coordinate
// ---------------------------------------------------------------------

bool build_track_path_spline(const RaceRouteTrack& track,
                             bool reverse,
                             TrackPathSpline& spline,
                             std::string& error) {
    spline = TrackPathSpline{};
    if (!track.ok()) {
        error = "the track did not parse";
        return false;
    }
    if (track.paths.size() != 1) {
        error = (Message() << "the track holds " << track.paths.size()
                           << " paths; this layer follows exactly one")
                    .str();
        return false;
    }
    for (const TrackPathNode& node : track.nodes) {
        if (node.links[0] != -1 || node.links[1] != -1 || node.links[2] != -1) {
            error = (Message() << "the node at " << node.offset
                               << " links another node; junctions are not "
                                  "followed by this layer")
                        .str();
            return false;
        }
    }
    const TrackPathRecord& path = track.paths.front();
    if (path.closed == 0) {
        error = "the path is open (a point-to-point track); 0x0059CEA0 "
                "extrapolates past the ends of an open spline and is not "
                "implemented here";
        return false;
    }
    spline.count = static_cast<std::size_t>(path.count);
    spline.first_node = static_cast<std::size_t>(path.first);
    spline.path_length = path.length;
    spline.distance_scale = path.distance_scale;
    spline.distance_offset = path.distance_offset;
    spline.reverse = reverse;
    spline.points.assign(spline.count * 3, 0.0F);
    for (std::size_t i = 0; i < spline.count; ++i) {
        const TrackPathNode& node = track.nodes[spline.first_node + i];
        spline.points[i * 3] = node.x;
        spline.points[i * 3 + 1] = node.y;
    }
    // 0x0059D9F0: each control point's running length is the length so
    // far; each segment adds 16 chords, measured between points evaluated
    // at i + k / 16 while the loop length is still zero.
    float running = 0.0F;
    for (std::size_t i = 0; i < spline.count; ++i) {
        spline.points[i * 3 + 2] = running;
        float previous[3];
        evaluate_segment(spline, static_cast<std::ptrdiff_t>(i), 0.0F, 0, 0.0F, previous);
        for (int k = 1; k <= kArcChords; ++k) {
            const float t = static_cast<float>(i) +
                            static_cast<float>(k) / static_cast<float>(kArcChords);
            float point[3];
            evaluate_at(spline, t, 0, 0.0F, point);
            const float dx = point[0] - previous[0];
            const float dy = point[1] - previous[1];
            running = running + std::sqrt(0.0F * 0.0F + dy * dy + dx * dx);
            previous[0] = point[0];
            previous[1] = point[1];
            previous[2] = point[2];
        }
    }
    spline.spline_length = running;
    return true;
}

void evaluate_track_path(const TrackPathSpline& spline,
                         float parameter,
                         int derivative,
                         float out[3]) noexcept {
    evaluate_at(spline, parameter, derivative, spline.spline_length, out);
}

void update_track_path_coordinate(const TrackPathSpline& spline,
                                  const RaceRouteTrack& track,
                                  TrackPathCoordinate& coordinate,
                                  const float query[2]) {
    int node = coordinate.node;
    if (node >= 0 &&
        !(distance2(query[0], query[1], coordinate.position[0],
                    coordinate.position[1]) <= kReseedDistance)) {
        node = -1;
    }
    if (node < 0) {
        node = nearest_node(track, query);
    }
    if (node < 0) {
        // 0x00443A5F..0x00443A7B: no node at all leaves the coordinate at
        // the query with nothing measured.
        coordinate = TrackPathCoordinate{};
        coordinate.position[0] = query[0];
        coordinate.position[1] = query[1];
        return;
    }
    node = nearer_neighbour(track, node, query);
    float start = coordinate.parameter;
    if (node != coordinate.node) {
        start = static_cast<float>(node - static_cast<int>(spline.first_node));
        coordinate.node = node;
    }
    const float parameter =
        nearest_parameter(spline, query, start, coordinate.iterations);
    float point[3];
    float slope[3];
    evaluate_track_path(spline, parameter, 0, point);
    evaluate_track_path(spline, parameter, 1, slope);
    float tangent[2] = {1.0F, 0.0F};
    const float slope_length = std::sqrt(slope[1] * slope[1] + slope[0] * slope[0]);
    if (slope_length > 0.0F) {
        const float inverse = 1.0F / slope_length;
        tangent[0] = inverse * slope[0];
        tangent[1] = inverse * slope[1];
    }
    // 0x00443C1F..0x00443C31, and 0x00443C37..0x00443C51 for a reverse race.
    float distance = point[2] * spline.distance_scale + spline.distance_offset;
    if (spline.reverse) {
        distance = spline.path_length - distance;
        tangent[0] = -tangent[0];
        tangent[1] = -tangent[1];
    }
    coordinate.parameter = parameter;
    coordinate.position[0] = query[0];
    coordinate.position[1] = query[1];
    coordinate.tangent[0] = tangent[0];
    coordinate.tangent[1] = tangent[1];
    coordinate.distance = distance;
    coordinate.fraction = distance / spline.path_length;
}

// ---------------------------------------------------------------------
// The boundaries
// ---------------------------------------------------------------------

int race_laps(int event_laps, std::int8_t lap_skip) noexcept {
    if (event_laps != 0 && lap_skip != 0 && event_laps != 1) {
        return 1;
    }
    return event_laps;
}

bool build_race_route(const RaceRouteTrack& track,
                      int laps,
                      int direction,
                      std::int8_t lap_skip,
                      RaceRoute& route,
                      std::string& error) {
    route = RaceRoute{};
    if (direction != 0 && direction != 1) {
        error = (Message() << "direction " << direction << " is neither 0 nor 1").str();
        return false;
    }
    route.reverse = direction == 1;
    route.point_to_point = lap_skip != 0;
    route.laps = race_laps(laps, lap_skip);
    if (!build_track_path_spline(track, route.reverse, route.spline, error)) {
        return false;
    }

    // 0x00434640: the marker's projection, or the default.
    const auto add = [&](const char* name, float fallback) -> int {
        if (fallback > kWrapWhole) {
            fallback -= kWrapWhole;
        }
        const std::uint32_t hash = track_marker_hash(name);
        int found = -1;
        for (std::size_t m = 0; m < track.markers.size(); ++m) {
            // GetMarker(hash, 0), 0x0043FEF0: the first in file order. The
            // parameter the lookup could match is 0 for every call this
            // layer makes, and 0 skips the search (0x004346A6).
            if (track.markers[m].name_hash == hash) {
                found = static_cast<int>(m);
                break;
            }
        }
        RaceBoundary boundary;
        boundary.name = name;
        boundary.marker = found;
        if (found >= 0) {
            TrackPathCoordinate coordinate;
            const float point[2] = {track.markers[static_cast<std::size_t>(found)].position[0],
                                    track.markers[static_cast<std::size_t>(found)].position[1]};
            update_track_path_coordinate(route.spline, track, coordinate, point);
            boundary.percent = coordinate.fraction * kWrapWhole;
        } else if (fallback >= 0.0F) {
            boundary.percent = fallback;
        } else {
            return -1;
        }
        route.boundaries.push_back(boundary);
        return static_cast<int>(route.boundaries.size()) - 1;
    };

    if (!route.reverse) {
        route.start = add("ForwardStartLine", kDefaultStart);
        const float start = route.boundaries[static_cast<std::size_t>(route.start)].percent;
        add("SplitTimeBoundary1", start + kDefaultSplitA);
        add("SplitTimeBoundary2", start + kDefaultSplitB);
        add("SplitTimeBoundary3", kNoDefault);
        route.finish = route.point_to_point
                           ? add("ForwardFinishLine", start + kDefaultFinish)
                           : route.start;
    } else {
        route.start = add("BackwardStartLine", kDefaultStart);
        const float start = route.boundaries[static_cast<std::size_t>(route.start)].percent;
        add("SplitTimeBoundary3", kNoDefault);
        add("SplitTimeBoundary2", start + kDefaultSplitA);
        add("SplitTimeBoundary1", start + kDefaultSplitB);
        route.finish = route.point_to_point
                           ? add("BackwardFinishLine", start + kDefaultFinish)
                           : route.start;
    }
    return true;
}

// ---------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------

const char* race_event_kind_name(RaceEventKind kind) noexcept {
    switch (kind) {
    case RaceEventKind::crossed_forward:
        return "crossed-forward";
    case RaceEventKind::crossed_backward:
        return "crossed-backward";
    case RaceEventKind::missed_forward:
        return "missed-forward";
    case RaceEventKind::missed_backward:
        return "missed-backward";
    case RaceEventKind::lap_complete:
        return "lap-complete";
    case RaceEventKind::finished:
        return "finished";
    case RaceEventKind::wrong_way_on:
        return "wrong-way-on";
    case RaceEventKind::wrong_way_off:
        return "wrong-way-off";
    case RaceEventKind::reset_zone:
        return "reset-zone";
    case RaceEventKind::recovered:
        return "recovered";
    }
    return "unknown";
}

namespace {

int next_boundary_ahead(const RaceRoute& route, float percent) {
    int best = -1;
    float best_ahead = 0.0F;
    for (std::size_t i = 0; i < route.boundaries.size(); ++i) {
        float ahead = route.boundaries[i].percent - percent;
        while (ahead <= 0.0F) {
            ahead += kWrapWhole;
        }
        while (ahead > kWrapWhole) {
            ahead -= kWrapWhole;
        }
        if (best < 0 || ahead < best_ahead) {
            best = static_cast<int>(i);
            best_ahead = ahead;
        }
    }
    return best;
}

} // namespace

void start_race_progress(const RaceRoute& route,
                         const RaceRouteTrack& track,
                         const float position[2],
                         RaceProgress& progress) {
    progress = RaceProgress{};
    update_track_path_coordinate(route.spline, track, progress.coordinate, position);
    progress.previous_percent = progress.coordinate.fraction * kWrapWhole;
    progress.previous_position[0] = position[0];
    progress.previous_position[1] = position[1];
    progress.next_boundary = next_boundary_ahead(route, progress.previous_percent);
}

void step_race_progress(const RaceRoute& route,
                        const RaceRouteTrack& track,
                        RaceProgress& progress,
                        const float position[2],
                        float step_seconds,
                        std::vector<RaceProgressEvent>& events) {
    if (progress.finished) {
        return;
    }
    ++progress.step;
    const std::size_t step = progress.step;
    const auto emit = [&](RaceEventKind kind, int index, float percent) {
        RaceProgressEvent event;
        event.kind = kind;
        event.step = step;
        event.index = index;
        event.lap = progress.lap;
        event.percent = percent;
        events.push_back(event);
    };

    // 0x00426AFE: the coordinate follows the car.
    update_track_path_coordinate(route.spline, track, progress.coordinate, position);
    // 0x00426B63..0x00426BFD: more than 300 from the tracked node, on a
    // frame whose counter is a multiple of 60, searched again from every
    // node. The step counter stands in for the frame counter.
    if (progress.coordinate.node >= 0 && step % kRaceRecoverPeriod == 0) {
        const TrackPathNode& node =
            track.nodes[static_cast<std::size_t>(progress.coordinate.node)];
        if (distance2(progress.coordinate.position[0], progress.coordinate.position[1],
                      node.x, node.y) > kRaceRecoverDistance) {
            progress.coordinate.node = -1;
            update_track_path_coordinate(route.spline, track, progress.coordinate,
                                         position);
            emit(RaceEventKind::recovered, progress.coordinate.node,
                 progress.coordinate.fraction * kWrapWhole);
        }
    }
    const float percent = progress.coordinate.fraction * kWrapWhole;

    // 0x00430E00, with the step as heading and velocity.
    const float dx = position[0] - progress.previous_position[0];
    const float dy = position[1] - progress.previous_position[1];
    const float travelled = std::sqrt(dy * dy + dx * dx);
    const int direction = route.reverse ? 1 : 0;
    bool wrong_way = false;
    unsigned int reference =
        angle16(progress.coordinate.tangent[0], progress.coordinate.tangent[1]);
    bool decided = false;
    int reset = -1;
    const float* where = progress.coordinate.position;
    // 0x00430E55..0x00430E91: the four types in this order, the first zone
    // found deciding.
    if (find_zone(track, kZoneNoWrongWay, direction, where) >= 0) {
        decided = true;
    } else {
        const int forced = find_zone(track, kZoneWrongWay, direction, where);
        if (forced >= 0) {
            const TrackPathZone& zone = track.zones[static_cast<std::size_t>(forced)];
            reference = angle16(zone.direction[0], zone.direction[1]);
        } else {
            reset = find_zone(track, kZoneReset, direction, where);
            if (reset < 0) {
                reset = find_zone(track, kZoneResetToPoint, direction, where);
            }
            if (reset >= 0 && reset != progress.zone) {
                emit(RaceEventKind::reset_zone, reset, percent);
            }
        }
    }
    progress.zone = reset;
    if (!decided) {
        if (travelled > 0.0F) {
            const unsigned int heading = angle16(dx, dy);
            const unsigned int hysteresis = progress.wrong_way ? kWrongWayHysteresis : 0U;
            const unsigned int off = angle_between(reference, heading) + hysteresis;
            if (off >= kWrongWayAngle) {
                const float speed = travelled / step_seconds;
                if (!(std::fabs(speed * kMilesPerHourPerMetre) > kWrongWaySpeed)) {
                    // 0x00430FF4: too slow to decide; the flag stays.
                    wrong_way = progress.wrong_way;
                } else {
                    // The velocity test repeats the heading test for a
                    // point that has no heading of its own, and the
                    // wheel-contact gate is taken as met.
                    wrong_way = true;
                }
            }
        } else {
            wrong_way = progress.wrong_way;
        }
    }
    if (wrong_way != progress.wrong_way) {
        emit(wrong_way ? RaceEventKind::wrong_way_on : RaceEventKind::wrong_way_off, -1,
             percent);
        progress.wrong_way = wrong_way;
    }

    // 0x004338C0: every boundary in table order.
    const float previous = progress.previous_percent;
    const float travel = wrap_percent(percent - previous);
    for (std::size_t i = 0; i < route.boundaries.size(); ++i) {
        const float boundary = route.boundaries[i].percent;
        const float now = wrap_percent(percent - boundary);
        const float before = wrap_percent(previous - boundary);
        const int index = static_cast<int>(i);
        const bool in_window = std::fabs(now) <= kRaceCrossingWindow;
        const bool forward = in_window && before < 0.0F && now >= 0.0F;
        const bool backward = in_window && before >= 0.0F && now < 0.0F;
        if (forward) {
            if (index == route.finish) {
                ++progress.lap;
                if (progress.lap > progress.max_lap) {
                    progress.max_lap = progress.lap;
                    emit(RaceEventKind::crossed_forward, index, percent);
                    emit(RaceEventKind::lap_complete, index, percent);
                } else {
                    emit(RaceEventKind::crossed_forward, index, percent);
                }
            } else {
                emit(RaceEventKind::crossed_forward, index, percent);
            }
            progress.checkpoint = 5 * progress.lap + index;
            progress.max_checkpoint = std::max(progress.max_checkpoint, progress.checkpoint);
        } else if (backward) {
            if (index == route.finish) {
                --progress.lap;
            }
            emit(RaceEventKind::crossed_backward, index, percent);
        } else if (travel > 0.0F && before < 0.0F && before + travel >= 0.0F) {
            // Passed forward, landing beyond the window: the game counts
            // nothing, and this layer says so.
            emit(RaceEventKind::missed_forward, index, percent);
        } else if (travel < 0.0F && before >= 0.0F && before + travel < 0.0F) {
            emit(RaceEventKind::missed_backward, index, percent);
        }
    }
    progress.previous_percent = percent;
    progress.previous_position[0] = position[0];
    progress.previous_position[1] = position[1];
    progress.next_boundary = next_boundary_ahead(route, percent);

    // 0x00435A2A..0x00435A4F: the highest lap reaching the race's laps.
    if (route.laps > 0 && progress.max_lap >= route.laps) {
        progress.finished = true;
        progress.finish_step = step;
        emit(RaceEventKind::finished, route.finish, percent);
    }
}

int race_laps_completed(const RaceProgress& progress) noexcept {
    return progress.lap < 0 ? 0 : progress.lap;
}

float race_node_percent(const RaceRoute& route,
                        const RaceRouteTrack& track,
                        std::size_t node) {
    TrackPathCoordinate coordinate;
    const float point[2] = {track.nodes[node].x, track.nodes[node].y};
    update_track_path_coordinate(route.spline, track, coordinate, point);
    return coordinate.fraction * kWrapWhole;
}

bool make_race_drive(const RaceRoute& route,
                     const RaceRouteTrack& track,
                     bool backward,
                     int skip_from,
                     int skip_to,
                     float spacing,
                     RaceDrive& drive,
                     std::string& error) {
    drive = RaceDrive{};
    if (route.spline.count == 0) {
        error = "the route has no path";
        return false;
    }
    if (!(spacing > 0.0F) || !std::isfinite(spacing)) {
        error = "the drive spacing must be positive";
        return false;
    }
    const char* grid_name =
        route.reverse ? "BackwardStartingPosition" : "ForwardStartingPosition";
    const std::uint32_t grid_hash = track_marker_hash(grid_name);
    for (std::size_t m = 0; m < track.markers.size(); ++m) {
        if (track.markers[m].name_hash == grid_hash) {
            drive.grid_marker = static_cast<int>(m);
            break;
        }
    }
    if (drive.grid_marker < 0) {
        error = std::string("the bundle has no ") + grid_name + " marker to start from";
        return false;
    }
    const auto count = static_cast<int>(route.spline.count);
    const int first = static_cast<int>(route.spline.first_node);
    const bool has_skip = skip_from >= 0 || skip_to >= 0;
    if (has_skip && (skip_from < first || skip_from >= first + count || skip_to < first ||
                     skip_to >= first + count || skip_from == skip_to)) {
        error = (Message() << "a skip needs two different nodes of the path, " << first
                           << " to " << first + count - 1)
                    .str();
        return false;
    }

    const TrackMarker& grid = track.markers[static_cast<std::size_t>(drive.grid_marker)];
    TrackPathCoordinate at;
    const float start[2] = {grid.position[0], grid.position[1]};
    update_track_path_coordinate(route.spline, track, at, start);
    const float grid_percent = at.fraction * kWrapWhole;

    // How far ahead of the grid, in race direction, a percent is: in
    // (0, 100].
    const auto ahead_of_grid = [&](float percent) {
        float distance = percent - grid_percent;
        while (distance <= 0.0F) {
            distance += kWrapWhole;
        }
        while (distance > kWrapWhole) {
            distance -= kWrapWhole;
        }
        return distance;
    };
    // Race order is node order forward and the reverse of it backward.
    const int race_step = route.reverse ? -1 : 1;
    int ahead = -1;
    float ahead_distance = 0.0F;
    for (int n = 0; n < count; ++n) {
        const float distance =
            ahead_of_grid(race_node_percent(route, track, static_cast<std::size_t>(first + n)));
        if (ahead < 0 || distance < ahead_distance) {
            ahead = n;
            ahead_distance = distance;
        }
    }
    // The first node past the finish line, counted in race order from the
    // node ahead of the grid: the drive ends there after its last lap. A
    // node exactly on the line is not past it, because a position there is
    // measured within the search tolerance of the line, on either side.
    const float finish =
        ahead_of_grid(route.boundaries[static_cast<std::size_t>(route.finish)].percent);
    int past_finish = 0;
    for (int k = 0; k < count; ++k) {
        const int n = (((ahead + k * race_step) % count) + count) % count;
        if (ahead_of_grid(race_node_percent(route, track, static_cast<std::size_t>(first + n))) >
            finish) {
            past_finish = k;
            break;
        }
    }
    const int direction = backward ? -race_step : race_step;
    int node = backward ? ahead - race_step : ahead;
    const int total = route.laps * count + past_finish + 1;
    for (int k = 0; k < total; ++k) {
        node = ((node % count) + count) % count;
        drive.nodes.push_back(first + node);
        node += direction;
    }
    std::vector<bool> jump(drive.nodes.size(), false);
    if (has_skip) {
        std::size_t from = drive.nodes.size();
        for (std::size_t k = 0; k < drive.nodes.size(); ++k) {
            if (drive.nodes[k] == skip_from) {
                from = k;
                break;
            }
        }
        std::size_t to = drive.nodes.size();
        for (std::size_t k = from + 1; k < drive.nodes.size(); ++k) {
            if (drive.nodes[k] == skip_to) {
                to = k;
                break;
            }
        }
        if (from == drive.nodes.size() || to == drive.nodes.size()) {
            error = (Message() << "node " << skip_to << " does not follow node "
                               << skip_from << " in the drive")
                        .str();
            return false;
        }
        drive.skipped = to - from - 1;
        drive.nodes.erase(drive.nodes.begin() + static_cast<std::ptrdiff_t>(from + 1),
                          drive.nodes.begin() + static_cast<std::ptrdiff_t>(to));
        jump.assign(drive.nodes.size(), false);
        jump[from + 1] = true;
    }

    float previous[2] = {grid.position[0], grid.position[1]};
    drive.points.push_back(previous[0]);
    drive.points.push_back(previous[1]);
    for (std::size_t k = 0; k < drive.nodes.size(); ++k) {
        const TrackPathNode& target = track.nodes[static_cast<std::size_t>(drive.nodes[k])];
        const float dx = target.x - previous[0];
        const float dy = target.y - previous[1];
        const float length = std::sqrt(dy * dy + dx * dx);
        const int steps =
            jump[k] ? 1 : std::max(1, static_cast<int>(std::ceil(length / spacing)));
        for (int j = 1; j <= steps; ++j) {
            const float t = static_cast<float>(j) / static_cast<float>(steps);
            drive.points.push_back(previous[0] + dx * t);
            drive.points.push_back(previous[1] + dy * t);
        }
        previous[0] = target.x;
        previous[1] = target.y;
    }
    return true;
}

} // namespace nfsu
