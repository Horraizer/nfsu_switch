// The route chunk 0x00034121 (issue #63, docs/RACING_LINE.md).
//
// Every case below builds its own bytes, so the synthetic half needs no game
// data. The retail half runs only when NFSU_RETAIL_ROOT names an install; it
// opens Tracks/TRACKB1003.lzc for reading and nothing else, and pins the
// file's own SHA-256, the route and point counts and a SHA-256 over every
// decoded value.

#include "nfsu/racing_line.h"

#include "nfsu/sha256.h"
#include "nfsu/texture.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

void put_u16(std::vector<unsigned char>& out, std::size_t at, std::uint16_t v) {
    out[at] = static_cast<unsigned char>(v & 0xFFU);
    out[at + 1] = static_cast<unsigned char>((v >> 8U) & 0xFFU);
}

void put_u32(std::vector<unsigned char>& out, std::size_t at, std::uint32_t v) {
    for (unsigned int i = 0; i < 4; ++i) {
        out[at + i] = static_cast<unsigned char>((v >> (8U * i)) & 0xFFU);
    }
}

void put_f32(std::vector<unsigned char>& out, std::size_t at, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(out, at, bits);
}

// One route record: header plus `count` points, with the invariants the
// retail records hold, so that a case can break exactly one of them.
std::vector<unsigned char> make_route(std::int16_t index,
                                     std::uint32_t kind,
                                     std::int16_t count) {
    std::vector<unsigned char> bytes(
        nfsu::kRouteHeaderSize +
            nfsu::kRoutePointSize * static_cast<std::size_t>(count),
        0);
    put_u32(bytes, 0x00, 11);
    put_u32(bytes, 0x04, 11);
    bytes[0x08] = 1;
    put_u16(bytes, 0x0A, static_cast<std::uint16_t>(index));
    put_u32(bytes, 0x0C, kind);
    put_u16(bytes, 0x10, static_cast<std::uint16_t>(count));
    put_u16(bytes, 0x12, static_cast<std::uint16_t>(count));
    put_u32(bytes, 0x14, 1);
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        const std::size_t at =
            nfsu::kRouteHeaderSize + nfsu::kRoutePointSize * i;
        put_f32(bytes, at + 0x00, static_cast<float>(10 * i));
        put_f32(bytes, at + 0x04, 0.0F);
        put_f32(bytes, at + 0x08, 1.0F);
        // Unit chord to the next point and back to the previous one, the way
        // every retail point holds them.
        put_f32(bytes, at + 0x0C, 1.0F);
        put_f32(bytes, at + 0x10, 0.0F);
        put_f32(bytes, at + 0x14, -1.0F);
        put_f32(bytes, at + 0x18, 0.0F);
        // The chord to the next point is 10 everywhere; a route end
        // carries the 4.0 every retail record carries there.
        put_f32(bytes, at + 0x1C,
                i + 1 < static_cast<std::size_t>(count) ? 10.0F : 4.0F);
        put_f32(bytes, at + 0x20, i > 0 ? 10.0F : 4.0F);
        put_f32(bytes, at + 0x38, static_cast<float>(10 * i));
        bytes[at + 0x81] = nfsu::kRouteNoEvent;
        bytes[at + 0x82] = nfsu::kRouteNoEvent;
        bytes[at + 0x83] = static_cast<unsigned char>(index & 0xFF);
    }
    return bytes;
}

std::size_t lane_offset_at(std::size_t slot) {
    return nfsu::kRouteLaneOffsetBase +
           2U * (nfsu::kRouteLaneFirstSlot + slot);
}

std::size_t lane_enable_at(std::size_t slot) {
    return nfsu::kRouteLaneEnableBase + nfsu::kRouteLaneFirstSlot + slot;
}

nfsu::RacingLineTrack parse(const std::vector<unsigned char>& payload) {
    nfsu::RacingLineTrack track;
    nfsu::RacingLineLimits limits;
    nfsu::parse_racing_lines(payload.data(), payload.size(), 0, limits, track);
    return track;
}

std::string first_issue(const nfsu::RacingLineTrack& track) {
    return track.issues.empty() ? std::string() : track.issues.front().message;
}

// -------------------------------------------------------------------------

void test_fields() {
    std::vector<unsigned char> route = make_route(21, 0, 3);
    // Two lanes enabled, in the two groups of six, and offsets in 1/256.
    route[lane_enable_at(0)] = 1;
    route[lane_enable_at(6)] = 1;
    const std::size_t point1 =
        nfsu::kRouteHeaderSize + nfsu::kRoutePointSize;
    put_u16(route, point1 + lane_offset_at(0),
            static_cast<std::uint16_t>(static_cast<std::int16_t>(-1280)));
    put_u16(route, point1 + lane_offset_at(6),
            static_cast<std::uint16_t>(static_cast<std::int16_t>(896)));
    route[point1 + 0x81] = 4;
    route[point1 + 0x82] = 9;

    const nfsu::RacingLineTrack track = parse(route);
    check(track.ok(), "a well formed route parses: " + first_issue(track));
    check(track.routes.size() == 1 && track.point_count() == 3,
          "one route of three points");
    const nfsu::Route& r = track.routes.front();
    check(r.index == 21, "index at +0x0A");
    check(r.kind == nfsu::RouteKind::racing, "kind 0 is the racing route");
    check(r.point_count == 3, "count at +0x10");
    check(r.lane_enabled[0] && r.lane_enabled[6], "the two enabled slots");
    check(!r.lane_enabled[1] && !r.lane_enabled[5] && !r.lane_enabled[11],
          "the other ten slots stay off");
    check(r.lane_group_enabled(0, 0) && r.lane_group_enabled(1, 0),
          "slot 0 of each group of six");
    check(!r.lane_group_enabled(0, 1), "slot 1 of the first group");

    const nfsu::RoutePoint& p = r.points[1];
    // -1280 / 256 and 896 / 256, derived from the stored unit rather than
    // read back from the implementation.
    check(p.lane_offset[0] == -5.0F, "lane offset scale on a negative value");
    check(p.lane_offset[6] == 3.5F, "lane offset scale on a positive value");
    check(p.lane_offset[1] == 0.0F, "an unwritten slot decodes to zero");
    check(p.position[0] == 10.0F && p.position[1] == 0.0F, "position");
    check(p.height == 1.0F, "height at +0x08");
    check(p.forward[0] == 1.0F && p.forward[1] == 0.0F, "forward at +0x0C");
    check(p.backward[0] == -1.0F && p.backward[1] == 0.0F,
          "backward at +0x14");
    check(p.distance_to_next == 10.0F && p.distance_to_previous == 10.0F,
          "the two chord lengths");
    check(r.points[0].distance_to_previous == 4.0F &&
              r.points[2].distance_to_next == 4.0F,
          "the 4.0 a route end carries");
    check(p.path_distance == 10.0F, "path distance at +0x38");
    check(p.event_a == 4 && p.event_b == 9, "the two event indices");
    check(p.route_index == 21, "route index at +0x83");
    check(r.points[0].event_a == nfsu::kRouteNoEvent,
          "0xFF is no event");
    check(track.route_count(nfsu::RouteKind::racing) == 1 &&
          track.route_count(nfsu::RouteKind::traffic) == 0,
          "route counts by kind");
    check(track.find(21) == &r && track.find(22) == nullptr,
          "lookup by index");
}

void test_two_records_tile_the_payload() {
    std::vector<unsigned char> payload = make_route(1, 0, 2);
    const std::vector<unsigned char> second = make_route(100, 1, 4);
    payload.insert(payload.end(), second.begin(), second.end());
    const nfsu::RacingLineTrack track = parse(payload);
    check(track.ok(), "two records tile the payload: " + first_issue(track));
    check(track.routes.size() == 2, "two routes");
    check(track.routes[1].kind == nfsu::RouteKind::traffic,
          "kind 1 is the traffic route");
    check(track.routes[1].offset ==
              nfsu::kRouteHeaderSize + 2U * nfsu::kRoutePointSize,
          "the second record starts where the first ends");
    check(track.point_count() == 6, "six points in total");
}

void test_truncated() {
    // An empty payload is what 1099 ships, and it is not an error.
    nfsu::RacingLineTrack empty = parse({});
    check(empty.ok() && empty.routes.empty(),
          "an empty payload parses to zero routes");

    // One byte short of the header.
    std::vector<unsigned char> payload = make_route(1, 0, 0);
    payload.pop_back();
    nfsu::RacingLineTrack track = parse(payload);
    check(!track.ok() && first_issue(track).find("shorter than one route") !=
                             std::string::npos,
          "a payload under one header is refused");

    // A record whose points run past the payload.
    payload = make_route(1, 0, 4);
    payload.resize(nfsu::kRouteHeaderSize + 2U * nfsu::kRoutePointSize);
    track = parse(payload);
    check(!track.ok() &&
              first_issue(track).find("remain in the payload") !=
                  std::string::npos,
          "a truncated record is refused: " + first_issue(track));

    // A trailing run too short to be a header.
    payload = make_route(1, 0, 1);
    payload.resize(payload.size() + 16);
    track = parse(payload);
    check(!track.ok() &&
              first_issue(track).find("too few for a route header") !=
                  std::string::npos,
          "a trailing run is refused: " + first_issue(track));
}

void test_oversized() {
    const std::vector<unsigned char> payload = make_route(1, 0, 4);
    nfsu::RacingLineLimits limits;
    limits.max_points_per_route = 3;
    nfsu::RacingLineTrack track;
    nfsu::parse_racing_lines(payload.data(), payload.size(), 0, limits, track);
    check(!track.ok() && first_issue(track).find("over the limit") !=
                             std::string::npos,
          "a record over max_points_per_route is refused");

    limits = nfsu::RacingLineLimits{};
    limits.max_points = 2;
    track = nfsu::RacingLineTrack{};
    nfsu::parse_racing_lines(payload.data(), payload.size(), 0, limits, track);
    check(!track.ok() && first_issue(track).find("in total") !=
                             std::string::npos,
          "a payload over max_points is refused");

    std::vector<unsigned char> two = make_route(1, 0, 0);
    const std::vector<unsigned char> second = make_route(2, 0, 0);
    two.insert(two.end(), second.begin(), second.end());
    limits = nfsu::RacingLineLimits{};
    limits.max_routes = 1;
    track = nfsu::RacingLineTrack{};
    nfsu::parse_racing_lines(two.data(), two.size(), 0, limits, track);
    check(!track.ok() && first_issue(track).find("more than 1 routes") !=
                             std::string::npos,
          "a payload over max_routes is refused: " + first_issue(track));

    limits = nfsu::RacingLineLimits{};
    limits.max_image_size = 8;
    track = nfsu::RacingLineTrack{};
    nfsu::parse_racing_line_image(payload.data(), payload.size(), limits,
                                  track);
    check(!track.ok() && first_issue(track).find("over the limit") !=
                             std::string::npos,
          "an image over max_image_size is refused");
}

void test_malformed() {
    struct Case {
        const char* what;
        const char* expect;
        void (*bend)(std::vector<unsigned char>&);
    };
    const Case cases[] = {
        {"a negative point count", "declares -1 points",
         [](std::vector<unsigned char>& b) { put_u16(b, 0x10, 0xFFFFU); }},
        {"a disagreeing count copy", "at +0x12",
         [](std::vector<unsigned char>& b) { put_u16(b, 0x12, 5); }},
        {"a negative route index", "is outside 0..127",
         [](std::vector<unsigned char>& b) { put_u16(b, 0x0A, 0xFFFFU); }},
        {"an index over 127", "is outside 0..127",
         [](std::vector<unsigned char>& b) { put_u16(b, 0x0A, 128); }},
        {"an unknown kind", "neither 0 (racing) nor 1 (traffic)",
         [](std::vector<unsigned char>& b) { put_u32(b, 0x0C, 2); }},
        {"a lane enable of 2", "neither 0 nor 1",
         [](std::vector<unsigned char>& b) { b[lane_enable_at(3)] = 2; }},
        {"a nonzero slot below 0x0B", "leaves the slots below",
         [](std::vector<unsigned char>& b) {
             b[nfsu::kRouteLaneEnableBase + 4] = 1;
         }},
        {"a non-finite coordinate", "is not finite",
         [](std::vector<unsigned char>& b) {
             put_u32(b, nfsu::kRouteHeaderSize + 0x00, 0x7F800000U);
         }},
        {"an enormous coordinate", "over the limit",
         [](std::vector<unsigned char>& b) {
             put_f32(b, nfsu::kRouteHeaderSize + 0x04, 2.0e6F);
         }},
        {"a point naming another route", "names route 7 at +0x83",
         [](std::vector<unsigned char>& b) {
             b[nfsu::kRouteHeaderSize + 0x83] = 7;
         }},
    };
    for (const Case& c : cases) {
        std::vector<unsigned char> payload = make_route(21, 0, 2);
        c.bend(payload);
        const nfsu::RacingLineTrack track = parse(payload);
        check(!track.ok(), std::string(c.what) + " must be refused");
        check(first_issue(track).find(c.expect) != std::string::npos,
              std::string(c.what) + " diagnostic: " + first_issue(track));
    }

    // A repeated index: the manager keeps one record per slot.
    std::vector<unsigned char> twice = make_route(21, 0, 0);
    const std::vector<unsigned char> again = make_route(21, 1, 0);
    twice.insert(twice.end(), again.begin(), again.end());
    const nfsu::RacingLineTrack track = parse(twice);
    check(!track.ok() &&
              first_issue(track).find("more than once") != std::string::npos,
          "a repeated index is refused: " + first_issue(track));

    nfsu::RacingLineTrack none;
    nfsu::RacingLineLimits limits;
    check(!nfsu::parse_racing_lines(nullptr, 100, 0, limits, none),
          "a null payload is refused");
}

void test_image_walk() {
    const std::vector<unsigned char> route = make_route(21, 0, 2);
    std::vector<unsigned char> image;
    const auto append_chunk = [&](std::uint32_t id,
                                  const std::vector<unsigned char>& body) {
        const std::size_t at = image.size();
        image.resize(at + 8 + body.size());
        put_u32(image, at, id);
        put_u32(image, at + 4, static_cast<std::uint32_t>(body.size()));
        std::memcpy(image.data() + at + 8, body.data(), body.size());
    };
    // A zero-size chunk in front, the way a retail image pads.
    append_chunk(0, {});
    append_chunk(0x00034146U, std::vector<unsigned char>(48, 0));
    const std::size_t expected_offset = image.size();
    append_chunk(nfsu::kRouteChunkId, route);
    append_chunk(0x00034122U, std::vector<unsigned char>(32, 0));

    nfsu::RacingLineTrack track;
    nfsu::RacingLineLimits limits;
    check(nfsu::parse_racing_line_image(image.data(), image.size(), limits,
                                        track),
          "the image walk finds the route chunk: " + first_issue(track));
    check(track.chunk_offset == expected_offset, "the chunk offset");
    check(track.routes.size() == 1 && track.routes[0].offset ==
                                          expected_offset + 8,
          "the record offset is absolute in the image");

    // No route chunk.
    std::vector<unsigned char> bare;
    image.swap(bare);
    append_chunk(0x00034146U, std::vector<unsigned char>(48, 0));
    track = nfsu::RacingLineTrack{};
    check(!nfsu::parse_racing_line_image(image.data(), image.size(), limits,
                                        track),
          "an image with no route chunk is refused");
    check(first_issue(track).find("holds 0") != std::string::npos,
          "the count is named: " + first_issue(track));

    // Two of them.
    append_chunk(nfsu::kRouteChunkId, route);
    append_chunk(nfsu::kRouteChunkId, route);
    track = nfsu::RacingLineTrack{};
    check(!nfsu::parse_racing_line_image(image.data(), image.size(), limits,
                                        track) &&
              first_issue(track).find("holds 2") != std::string::npos,
          "two route chunks are refused: " + first_issue(track));

    // A zero-size route chunk, the way 1099 ships it, is found and parses
    // to zero routes.
    std::vector<unsigned char> just_header;
    image.swap(just_header);
    append_chunk(nfsu::kRouteChunkId, {});
    track = nfsu::RacingLineTrack{};
    check(nfsu::parse_racing_line_image(image.data(), image.size(), limits,
                                        track),
          "an empty route chunk is accepted: " + first_issue(track));
    check(track.routes.empty() && track.payload_size == 0,
          "and holds no route");

    // A chunk whose size overruns the image.
    std::vector<unsigned char> overrun(16, 0);
    put_u32(overrun, 0, nfsu::kRouteChunkId);
    put_u32(overrun, 4, 1000);
    track = nfsu::RacingLineTrack{};
    check(!nfsu::parse_racing_line_image(overrun.data(), overrun.size(),
                                        limits, track) &&
              first_issue(track).find("remain in the image") !=
                  std::string::npos,
          "an overrunning chunk is refused: " + first_issue(track));
}

// -------------------------------------------------------------------------

void test_retail_1003() {
    const char* root = std::getenv("NFSU_RETAIL_ROOT");
    if (root == nullptr || *root == '\0') {
        std::cout << "racing line: retail check skipped, NFSU_RETAIL_ROOT "
                     "unset\n";
        return;
    }
    const std::filesystem::path file =
        std::filesystem::path(root) / "Tracks" / "TRACKB1003.lzc";
    std::ifstream input(file, std::ios::binary);
    check(bool(input), "retail Tracks/TRACKB1003.lzc opens for reading");
    const std::vector<unsigned char> stored(
        std::istreambuf_iterator<char>(input), {});
    check(stored.size() == 433352, "retail TRACKB1003.lzc is 433352 bytes");
    check(nfsu::sha256_hex(nfsu::sha256(stored.data(), stored.size())) ==
              "ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA",
          "retail TRACKB1003.lzc SHA-256");

    std::vector<unsigned char> image;
    std::size_t unused = 0;
    std::string error;
    check(nfsu::jdlz_decompress(stored.data(), stored.size(),
                                64U * 1024U * 1024U, image, unused, error),
          "the JDLZ image opens: " + error);
    check(image.size() == 961328, "the image is 961328 bytes");

    nfsu::RacingLineTrack track;
    nfsu::RacingLineLimits limits;
    check(nfsu::parse_racing_line_image(image.data(), image.size(), limits,
                                        track),
          "retail 1003 routes parse: " +
              (track.issues.empty() ? std::string() : track.issues[0].message));
    check(track.chunk_offset == 274916, "the route chunk is at 274916");
    check(track.payload_size == 160276, "its payload is 160276 bytes");
    check(track.routes.size() == 17, "1003 holds 17 routes");
    check(track.route_count(nfsu::RouteKind::racing) == 14 &&
              track.route_count(nfsu::RouteKind::traffic) == 3,
          "14 racing routes and 3 traffic routes");
    check(track.point_count() == 1099, "1099 points");

    // A digest over every decoded value, so that any change to any field of
    // any point shows here rather than in a document.
    nfsu::Sha256 digest;
    for (const nfsu::Route& route : track.routes) {
        const auto feed = [&](const void* data, std::size_t size) {
            digest.update(static_cast<const unsigned char*>(data), size);
        };
        const std::int16_t index = route.index;
        const std::uint32_t kind = static_cast<std::uint32_t>(route.kind);
        const std::int16_t count = route.point_count;
        feed(&index, sizeof(index));
        feed(&kind, sizeof(kind));
        feed(&count, sizeof(count));
        for (bool enabled : route.lane_enabled) {
            const unsigned char byte = enabled ? 1U : 0U;
            feed(&byte, 1);
        }
        for (const nfsu::RoutePoint& point : route.points) {
            feed(point.position, sizeof(point.position));
            feed(&point.height, sizeof(point.height));
            feed(point.forward, sizeof(point.forward));
            feed(point.backward, sizeof(point.backward));
            feed(&point.distance_to_next, sizeof(point.distance_to_next));
            feed(&point.distance_to_previous,
                 sizeof(point.distance_to_previous));
            feed(&point.unknown_24, sizeof(point.unknown_24));
            feed(&point.unknown_28, sizeof(point.unknown_28));
            feed(&point.path_distance, sizeof(point.path_distance));
            feed(point.lane_offset.data(),
                 point.lane_offset.size() * sizeof(float));
            feed(&point.event_a, 1);
            feed(&point.event_b, 1);
            feed(&point.route_index, 1);
        }
    }
    const std::string pin = nfsu::sha256_hex(digest.finish());
    check(pin == "14904BD5A8134F8C19B40EB559D212F0EEFB09C7C55A110CA286F3C9A0443DC3",
          "retail 1003 decoded route digest is " + pin);
    std::cout << "racing line: retail 1003 pinned\n";
}

} // namespace

int main() {
    try {
        test_fields();
        test_two_records_tile_the_payload();
        test_truncated();
        test_oversized();
        test_malformed();
        test_image_walk();
        test_retail_1003();
        std::cout << "racing line tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "racing line tests: " << e.what() << '\n';
        return 1;
    }
}
