#include "nfsu/racing_line.h"

#include <cmath>
#include <cstring>
#include <set>
#include <sstream>

namespace nfsu {
namespace {

std::uint16_t read_u16(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned int>(bytes[0]) |
        (static_cast<unsigned int>(bytes[1]) << 8U));
}

std::int16_t read_s16(const unsigned char* bytes) {
    return static_cast<std::int16_t>(read_u16(bytes));
}

std::uint32_t read_u32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

float read_f32(const unsigned char* bytes) {
    const std::uint32_t bits = read_u32(bytes);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string hex32(std::uint32_t value) {
    std::ostringstream text;
    text << "0x" << std::uppercase << std::hex;
    text.width(8);
    text.fill('0');
    text << value;
    return text.str();
}

class Parser {
public:
    Parser(const unsigned char* payload,
           std::size_t size,
           std::uint64_t base,
           const RacingLineLimits& limits,
           RacingLineTrack& track)
        : payload_(payload), size_(size), base_(base), limits_(limits),
          track_(track) {}

    void run() {
        track_.payload_size = size_;
        if (payload_ == nullptr && size_ != 0) {
            add(0, "no payload");
            return;
        }
        if (size_ == 0) {
            // The loader's walk ends immediately on an empty payload
            // (0x00442B75..0x00442B7E), and retail 1099 ships exactly that:
            // a 0x00034121 chunk of size 0 and no routes.
            track_.valid = true;
            return;
        }
        if (size_ < kRouteHeaderSize) {
            std::ostringstream text;
            text << "payload of " << size_
                 << " bytes is shorter than one route header ("
                 << kRouteHeaderSize << ")";
            add(0, text.str());
            return;
        }
        std::size_t at = 0;
        std::size_t total_points = 0;
        while (at < size_) {
            if (size_ - at < kRouteHeaderSize) {
                std::ostringstream text;
                text << "trailing " << (size_ - at)
                     << " bytes are too few for a route header ("
                     << kRouteHeaderSize << ")";
                add(at, text.str());
                return;
            }
            if (track_.routes.size() >= limits_.max_routes) {
                std::ostringstream text;
                text << "more than " << limits_.max_routes << " routes";
                add(at, text.str());
                return;
            }
            Route route;
            if (!read_route(at, route, total_points)) {
                return;
            }
            const std::size_t span =
                kRouteHeaderSize +
                kRoutePointSize * static_cast<std::size_t>(route.point_count);
            track_.routes.push_back(std::move(route));
            at += span;
        }
        track_.valid = track_.issues.empty();
    }

private:
    void add(std::size_t at, const std::string& message) {
        track_.issues.push_back(RacingLineIssue{base_ + at, message});
    }

    // Reads the record whose header starts at `at`. Leaves `route` filled on
    // success; on failure it has already reported an issue.
    bool read_route(std::size_t at, Route& route, std::size_t& total_points) {
        const unsigned char* header = payload_ + at;
        route.offset = base_ + at;
        route.index = read_s16(header + 0x0A);
        const std::uint32_t kind = read_u32(header + 0x0C);
        route.point_count = read_s16(header + 0x10);
        route.reverse = header[0x15] != 0;
        route.progress_begin = read_f32(header + 0x18);
        route.progress_end = read_f32(header + 0x1C);
        for (unsigned k = 0; k < 4; ++k) route.bounds[k] = read_f32(header + 0x20 + 4*k);
        for (unsigned k = 0; k < 10; ++k)
            route.category_enabled[k] = header[(k+1)*0x34] != 0 || header[(k+1)*0x34+1] != 0;
        for (unsigned k = 0; k < 6; ++k) {
            if (!std::isfinite(read_f32(header + 0x18 + 4*k))) {
                add(at, "nonfinite route spatial header");
                return false;
            }
        }
        const std::int16_t count_copy = read_s16(header + 0x12);

        if (route.index < 0 || route.index > kRouteMaxIndex) {
            std::ostringstream text;
            text << "route index " << route.index << " is outside 0.."
                 << static_cast<int>(kRouteMaxIndex);
            add(at, text.str());
            return false;
        }
        if (!seen_.insert(route.index).second) {
            std::ostringstream text;
            text << "route index " << route.index
                 << " appears more than once; the manager keeps one record"
                    " per index";
            add(at, text.str());
            return false;
        }
        if (kind != static_cast<std::uint32_t>(RouteKind::racing) &&
            kind != static_cast<std::uint32_t>(RouteKind::traffic)) {
            std::ostringstream text;
            text << "route kind " << hex32(kind)
                 << " is neither 0 (racing) nor 1 (traffic)";
            add(at, text.str());
            return false;
        }
        route.kind = static_cast<RouteKind>(kind);
        if (route.point_count < 0) {
            std::ostringstream text;
            text << "route " << route.index << " declares "
                 << route.point_count << " points";
            add(at, text.str());
            return false;
        }
        if (count_copy != route.point_count) {
            std::ostringstream text;
            text << "route " << route.index << " point count " << route.point_count
                 << " at +0x10 differs from " << count_copy << " at +0x12";
            add(at, text.str());
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(route.point_count);
        if (count > limits_.max_points_per_route) {
            std::ostringstream text;
            text << "route " << route.index << " declares " << count
                 << " points, over the limit " << limits_.max_points_per_route;
            add(at, text.str());
            return false;
        }
        if (total_points + count > limits_.max_points) {
            std::ostringstream text;
            text << "more than " << limits_.max_points << " points in total";
            add(at, text.str());
            return false;
        }
        const std::size_t span = kRouteHeaderSize + kRoutePointSize * count;
        if (span > size_ - at) {
            std::ostringstream text;
            text << "route " << route.index << " needs " << span
                 << " bytes for " << count << " points but only "
                 << (size_ - at) << " remain in the payload";
            add(at, text.str());
            return false;
        }

        for (std::size_t i = 0; i < kRouteLaneFirstSlot; ++i) {
            const unsigned char value = header[kRouteLaneEnableBase + i];
            if (value != 0) {
                std::ostringstream text;
                text << "route " << route.index << " lane slot " << i
                     << " at +0x" << std::hex << std::uppercase
                     << (kRouteLaneEnableBase + i) << std::dec
                     << " is " << static_cast<unsigned>(value)
                     << "; every retail record leaves the slots below 0x"
                     << std::hex << std::uppercase << kRouteLaneFirstSlot
                     << std::dec << " zero";
                add(at, text.str());
                return false;
            }
        }
        for (std::size_t slot = 0; slot < kRouteLaneSlots; ++slot) {
            const std::size_t offset =
                kRouteLaneEnableBase + kRouteLaneFirstSlot + slot;
            const unsigned char value = header[offset];
            if (value > 1U) {
                std::ostringstream text;
                text << "route " << route.index << " lane enable at +0x"
                     << std::hex << std::uppercase << offset << std::dec
                     << " is " << static_cast<unsigned>(value)
                     << ", neither 0 nor 1";
                add(at, text.str());
                return false;
            }
            route.lane_enabled[slot] = value != 0;
        }

        route.points.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t point_at = at + kRouteHeaderSize + kRoutePointSize * i;
            RoutePoint point;
            if (!read_point(payload_ + point_at, point_at, route.index, i, point)) {
                return false;
            }
            route.points.push_back(point);
        }
        total_points += count;
        return true;
    }

    bool finite(std::size_t at, float value, const char* what,
                std::int16_t index, std::size_t i) {
        if (std::isfinite(value)) {
            return true;
        }
        std::ostringstream text;
        text << "route " << index << " point " << i << " " << what
             << " is not finite";
        add(at, text.str());
        return false;
    }

    bool read_point(const unsigned char* bytes,
                    std::size_t at,
                    std::int16_t index,
                    std::size_t i,
                    RoutePoint& point) {
        static const struct {
            std::size_t offset;
            const char* name;
        } kFloats[] = {
            {0x00, "position x"}, {0x04, "position y"}, {0x08, "height"},
            {0x0C, "forward x"}, {0x10, "forward y"},
            {0x14, "backward x"}, {0x18, "backward y"},
            {0x1C, "distance to next"}, {0x20, "distance to previous"},
            {0x24, "+0x24"}, {0x28, "+0x28"},
            {0x2C, "+0x2c"}, {0x30, "+0x30"}, {0x34, "+0x34"},
            {0x38, "path distance"},
        };
        for (const auto& field : kFloats) {
            if (!finite(at, read_f32(bytes + field.offset), field.name,
                        index, i)) {
                return false;
            }
        }
        point.position[0] = read_f32(bytes + 0x00);
        point.position[1] = read_f32(bytes + 0x04);
        point.height = read_f32(bytes + 0x08);
        point.forward[0] = read_f32(bytes + 0x0C);
        point.forward[1] = read_f32(bytes + 0x10);
        point.backward[0] = read_f32(bytes + 0x14);
        point.backward[1] = read_f32(bytes + 0x18);
        point.distance_to_next = read_f32(bytes + 0x1C);
        point.distance_to_previous = read_f32(bytes + 0x20);
        point.unknown_24 = read_f32(bytes + 0x24);
        point.unknown_28 = read_f32(bytes + 0x28);
        point.unknown_2c = read_f32(bytes + 0x2C);
        point.unknown_30 = read_f32(bytes + 0x30);
        point.unknown_34 = read_f32(bytes + 0x34);
        point.path_distance = read_f32(bytes + 0x38);
        for(std::size_t slot=0;slot<point.driving_offset.size();++slot)
            point.driving_offset[slot]=static_cast<float>(read_s16(bytes+0x3C+2*slot))*
                kRouteLaneOffsetScale;

        for (std::size_t axis = 0; axis < 2; ++axis) {
            if (std::fabs(point.position[axis]) > limits_.coordinate_limit) {
                std::ostringstream text;
                text << "route " << index << " point " << i << " coordinate "
                     << point.position[axis] << " is over the limit "
                     << limits_.coordinate_limit;
                add(at, text.str());
                return false;
            }
        }

        for (std::size_t slot = 0; slot < kRouteLaneSlots; ++slot) {
            const std::size_t offset =
                kRouteLaneOffsetBase + 2U * (kRouteLaneFirstSlot + slot);
            point.lane_offset[slot] =
                static_cast<float>(read_s16(bytes + offset)) *
                kRouteLaneOffsetScale;
        }
        for (std::size_t k = 0; k < point.speed.size(); ++k) point.speed[k] = bytes[0x6A + k];
        point.event_a = bytes[0x81];
        point.event_b = bytes[0x82];
        point.route_index = bytes[0x83];
        for (std::size_t k = 0; k < 4; ++k) {
            point.unknown_84[k] = bytes[0x84 + k];
        }
        if (point.route_index !=
            static_cast<std::uint8_t>(index & 0xFF)) {
            std::ostringstream text;
            text << "route " << index << " point " << i << " names route "
                 << static_cast<unsigned>(point.route_index) << " at +0x83";
            add(at, text.str());
            return false;
        }
        return true;
    }

    const unsigned char* payload_ = nullptr;
    std::size_t size_ = 0;
    std::uint64_t base_ = 0;
    const RacingLineLimits& limits_;
    RacingLineTrack& track_;
    std::set<std::int16_t> seen_;
};

} // namespace

std::size_t RacingLineTrack::point_count() const noexcept {
    std::size_t total = 0;
    for (const Route& route : routes) {
        total += route.points.size();
    }
    return total;
}

std::size_t RacingLineTrack::route_count(RouteKind kind) const noexcept {
    std::size_t total = 0;
    for (const Route& route : routes) {
        if (route.kind == kind) {
            ++total;
        }
    }
    return total;
}

const Route* RacingLineTrack::find(std::int16_t index) const noexcept {
    for (const Route& route : routes) {
        if (route.index == index) {
            return &route;
        }
    }
    return nullptr;
}

bool parse_racing_lines(const unsigned char* payload,
                        std::size_t payload_size,
                        std::uint64_t payload_offset,
                        const RacingLineLimits& limits,
                        RacingLineTrack& track) {
    track = RacingLineTrack{};
    Parser parser(payload, payload_size, payload_offset, limits, track);
    parser.run();
    return track.ok();
}

bool parse_racing_line_image(const unsigned char* image,
                             std::size_t image_size,
                             const RacingLineLimits& limits,
                             RacingLineTrack& track) {
    track = RacingLineTrack{};
    if (image == nullptr) {
        track.issues.push_back(RacingLineIssue{0, "no image"});
        return false;
    }
    if (static_cast<std::uint64_t>(image_size) > limits.max_image_size) {
        std::ostringstream text;
        text << "image of " << image_size << " bytes is over the limit "
             << limits.max_image_size;
        track.issues.push_back(RacingLineIssue{0, text.str()});
        return false;
    }
    std::size_t at = 0;
    std::size_t found = 0;
    std::size_t chunk_at = 0;
    std::size_t chunk_size = 0;
    while (image_size - at >= 8) {
        const std::uint32_t id = read_u32(image + at);
        const std::uint32_t size = read_u32(image + at + 4);
        if (size == 0) {
            // A zero-size chunk is padding, and the walk advances by its
            // header alone - but a zero-size route chunk is what retail 1099
            // ships, so it counts as found before the skip.
            if (id == kRouteChunkId) {
                ++found;
                chunk_at = at;
                chunk_size = 0;
            }
            at += 8;
            continue;
        }
        if (static_cast<std::uint64_t>(size) > image_size - at - 8) {
            std::ostringstream text;
            text << "chunk " << hex32(id) << " declares " << size
                 << " payload bytes but only " << (image_size - at - 8)
                 << " remain in the image";
            track.issues.push_back(
                RacingLineIssue{static_cast<std::uint64_t>(at), text.str()});
            return false;
        }
        if (id == kRouteChunkId) {
            ++found;
            chunk_at = at;
            chunk_size = size;
        }
        at += 8U + size;
    }
    if (found != 1) {
        std::ostringstream text;
        text << "image holds " << found << " " << hex32(kRouteChunkId)
             << " chunks, not one";
        track.issues.push_back(RacingLineIssue{0, text.str()});
        return false;
    }
    const bool ok = parse_racing_lines(image + chunk_at + 8, chunk_size,
                                       static_cast<std::uint64_t>(chunk_at) + 8U,
                                       limits, track);
    track.chunk_offset = static_cast<std::uint64_t>(chunk_at);
    return ok;
}

} // namespace nfsu
