#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// ---------------------------------------------------------------------------
// The shipped racing line: the route chunk 0x00034121 of a TRACKB image
// ---------------------------------------------------------------------------
//
// docs/RACING_LINE.md. Need for Speed: Underground ships its opponent line
// as data rather than computing one: all 31 TRACKB files carry a 0x00034121
// chunk of route records, 1099's being empty, and 0x007009D0 names the two
// record kinds `Racing route` (0) and `Traffic route` (1).
//
// The loader is 0x00442B70, reached from the chunk dispatcher 0x00442DD0
// with the manager at 0x0077AF20 in edx. It walks the payload from
// chunk + 8 to chunk + 8 + payload size, and for each record reads
//   +0x0A  s16  the route index, stored at 0x0077AF30 + 4 * index
//                (0x00442B80..0x00442B84)
//   +0x0C  u32  the kind, 0 and 1 going on two separate lists
//                (0x00442B88..0x00442BA3)
//   +0x10  s16  the point count, and the record is
//                0x27C + 0x88 * count bytes (0x00442BB2..0x00442BC5)
// so the record stride is data-driven and a record's own count is the only
// thing that finds the next one.
//
// Every code address below is a virtual address of the retail speed.exe
// (SHA-256 C0FAD450...074B7); `file_offset = VA - 0x400000`.

constexpr std::uint32_t kRouteChunkId = 0x00034121U;

// 0x00442BBC `lea eax, [eax + ecx + 0x27c]` after `imul ecx, ecx, 0x88`.
constexpr std::size_t kRouteHeaderSize = 0x27C;
constexpr std::size_t kRoutePointSize = 0x88;

// The lane table. 0x00456E90 reads a lane offset at point + 0x3C + 2 * i and
// gates it on the route header byte at + 0x265 + i (0x00456F45,
// 0x00456F54). It runs i over 0x0B..0x10 in one mode and 0x11..0x16 in the
// other (0x00456EE6 `mov ecx, 0xb`, 0x00456F01 `mov ecx, 0x11`, both with
// `lea edi, [ecx + 6]`), so twelve slots in two groups of six. In all 504
// retail route records the eleven header bytes + 0x265..+0x26F are zero, so
// no slot below 0x0B is ever enabled and this header exposes the twelve.
constexpr std::size_t kRouteLaneSlots = 12;
constexpr std::size_t kRouteLaneGroupSize = 6;
constexpr std::size_t kRouteLaneFirstSlot = 0x0B;
constexpr std::size_t kRouteLaneOffsetBase = 0x3C;
constexpr std::size_t kRouteLaneEnableBase = 0x265;
// 0x00456F6D shifts the s16 left by 8 and 0x00456F78 multiplies by
// 0x006CC83C = 1.52587890625e-05 = 1/65536, so the stored unit is 1/256.
constexpr float kRouteLaneOffsetScale = 1.0F / 256.0F;
// 0xFF in point + 0x81 or + 0x82 means "no record" (0x00456EF5,
// 0x00456F17).
constexpr std::uint8_t kRouteNoEvent = 0xFFU;

enum class RouteKind : std::uint32_t {
    // 0x006CA258, table 0x007009D0 entry 0.
    racing = 0,
    // 0x006CA248, table 0x007009D0 entry 1.
    traffic = 1,
};

// One 0x88-byte point. Offsets are into the point record.
struct RoutePoint {
    // +0x00, +0x04: the two horizontal slots, the same pair
    // docs/RACE_ROUTE.md uses for a path node. 0x00456EC8..0x00456ED0
    // subtracts them from a car position.
    float position[2] = {0.0F, 0.0F};
    // +0x08: height. No traced instruction reads it.
    float height = 0.0F;
    // +0x0C, +0x10: a unit vector. 0x00456EDC..0x00456EF1 crosses it with
    // the car offset to get a signed lateral distance. Measured over all
    // 31956 consecutive pairs of the 504 retail route records (the shortest
    // chord is 4.0028), its dot product with the unit chord to the next
    // point is within 1e-7 of 1.0, so it is the direction to the next point.
    float forward[2] = {0.0F, 0.0F};
    // +0x14, +0x18: the same, taken instead at 0x00456EFB..0x00456F0C in
    // the other mode. Over the same 31956 pairs its dot product with the
    // unit chord to the previous point is approximately 1.0.
    float backward[2] = {0.0F, 0.0F};
    // +0x1C, +0x20: the lengths of the two chords whose directions +0x0C and
    // +0x14 hold. Over all 31956 consecutive pairs of the 504 retail records
    // +0x1C of the earlier point and +0x20 of the later one both equal the
    // chord between them to within 1.4e-6. The last point of every record
    // carries 4.0 at +0x1C and the first 4.0 at +0x20, with no neighbour on
    // that side.
    float distance_to_next = 0.0F;
    float distance_to_previous = 0.0F;
    // +0x24, +0x28: 0 to 33.589 and 0 to 33.2152, zero at 16 and 19 of the
    // 32460 retail points. Unknown. They are not chord lengths, and they do
    // not bound the lane offsets in either order: 2890 of the 86024 enabled
    // lane offsets fall outside [-(+0x24), +0x28] and 2832 outside
    // [-(+0x28), +0x24]. The debug events
    // JOY_EVENT_TRACKROUTE_EDITOR_EDIT_LEFT_WIDTH and _EDIT_RIGHT_WIDTH
    // (0x006BADA8, 0x006BADD4) say an editor edits a left and a right width
    // somewhere in the record; which offsets they write is unknown.
    float unknown_24 = 0.0F;
    float unknown_28 = 0.0F;
    // +0x2C, +0x30, +0x34: unknown. +0x30 and +0x34 are a mirrored pair
    // (-2.70805..3.16066 and -3.16066..2.70805 over all retail points) and
    // neither is the heading of +0x0C or +0x14.
    float unknown_2c = 0.0F;
    float unknown_30 = 0.0F;
    float unknown_34 = 0.0F;
    // +0x38: the distance along the track path spline of
    // docs/RACE_ROUTE.md, the measure race progress uses. On each of the
    // eight circuit tracks every point's value lies inside [0, path
    // length]; on 1201 and 1210 the smallest is negative (-43.935 and
    // -53.834), which an open path allows.
    float path_distance = 0.0F;
    // +0x3C + 2 * (0x0B + slot), s16 in 1/256. Only a slot the route
    // header enables is read.
    std::array<float, kRouteLaneSlots> lane_offset{};
    // +0x81, +0x82: indices into the 0x00034122 table after 0x004430B0
    // sorts it; kRouteNoEvent for none. 0x004430B0 rewrites both through
    // the sort permutation (0x004431B0..0x004431D2).
    std::uint8_t event_a = kRouteNoEvent;
    std::uint8_t event_b = kRouteNoEvent;
    // +0x83: the index of the route this point belongs to, used unchecked
    // as an index into 0x0077AF30 at 0x00456EA8 and 0x004552DC. It equals
    // the containing record's own +0x0A for all 32460 retail points.
    std::uint8_t route_index = 0;
    // +0x84..+0x87: four bytes no traced instruction reads.
    std::array<std::uint8_t, 4> unknown_84{};
};

// One route record.
struct Route {
    std::uint64_t offset = 0;
    // +0x0A. The manager's table is 0x80 entries wide (the clear loop at
    // 0x00442BD7 counts 0x80 dwords), so an index is 0..127.
    std::int16_t index = 0;
    // +0x0C.
    RouteKind kind = RouteKind::racing;
    // +0x10, and the same value again at +0x12 in all 504 retail records.
    std::int16_t point_count = 0;
    // +0x265 + 0x0B + slot, 0 or 1 in every retail record.
    std::array<bool, kRouteLaneSlots> lane_enabled{};
    std::vector<RoutePoint> points;

    bool lane_group_enabled(std::size_t group, std::size_t slot) const {
        const std::size_t index = group * kRouteLaneGroupSize + slot;
        return index < kRouteLaneSlots && lane_enabled[index];
    }
};

struct RacingLineIssue {
    std::uint64_t offset = 0;
    std::string message;
};

// Not retail rules. The manager's table decides the index bound; the rest
// bound a file this project is willing to walk.
constexpr std::int16_t kRouteMaxIndex = 0x7F;

struct RacingLineLimits {
    std::uint64_t max_image_size = 64ULL * 1024ULL * 1024ULL;
    std::size_t max_routes = 1U << 10;
    std::size_t max_points_per_route = 1U << 14;
    std::size_t max_points = 1U << 17;
    // The largest magnitude a point coordinate may have on either
    // horizontal axis. Retail points stay within 2392.56.
    float coordinate_limit = 1.0e6F;
};

// What one 0x00034121 chunk holds.
struct RacingLineTrack {
    std::uint64_t chunk_offset = 0;
    std::uint64_t payload_size = 0;
    std::vector<Route> routes;
    std::vector<RacingLineIssue> issues;
    bool valid = false;

    bool ok() const noexcept { return valid && issues.empty(); }
    std::size_t point_count() const noexcept;
    std::size_t route_count(RouteKind kind) const noexcept;
    // The first record with that +0x0A index, or nullptr.
    const Route* find(std::int16_t index) const noexcept;
};

// Parses the payload of one 0x00034121 chunk: `payload` is the chunk's
// payload, `payload_offset` only labels issues. Never throws.
//
// An empty payload is not an error: the loader's walk ends at once on one,
// and retail 1099 ships exactly that. It parses to zero routes.
//
// Refusals, each an issue rather than a crash:
//   - a non-empty payload shorter than one header, or a trailing run too
//     short for a header;
//   - a record whose 0x27C + 0x88 * count bytes overrun the payload
//     (truncated input);
//   - a payload that does not end exactly on a record boundary;
//   - a negative point count, or one over limits.max_points_per_route;
//   - a route index outside 0..kRouteMaxIndex, or a repeated index;
//   - a kind other than 0 or 1;
//   - a point count copy at +0x12 that disagrees with +0x10;
//   - a lane enable byte other than 0 or 1, or a nonzero byte in the
//     eleven slots below 0x0B;
//   - a non-finite float, or a coordinate over limits.coordinate_limit;
//   - a point whose +0x83 is not the record's own index;
//   - more routes or points than the limits allow.
bool parse_racing_lines(const unsigned char* payload,
                        std::size_t payload_size,
                        std::uint64_t payload_offset,
                        const RacingLineLimits& limits,
                        RacingLineTrack& track);

// Finds the single 0x00034121 chunk of an already decompressed TRACKB image
// and parses it. Top-level chunks are walked the way docs/TRACK_ZONES.md
// walks them: a header is 8 bytes, a zero size advances by 8. Refuses an
// image over limits.max_image_size, one with no 0x00034121 and one with
// more than one.
bool parse_racing_line_image(const unsigned char* image,
                             std::size_t image_size,
                             const RacingLineLimits& limits,
                             RacingLineTrack& track);

} // namespace nfsu
