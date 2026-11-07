#include "nfsu/arg_path.h"
#include "nfsu/race_route.h"
#include "nfsu/sha256.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

// A fixture name no other process shares (issues #122 and #124).
std::string unique_fixture(const std::string& label) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return "nfsu_race_route_" + label + "_" + std::to_string(pid);
}

void put_u16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<unsigned char>(value & 0xFFU);
    bytes[at + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

void put_u32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

void put_f32(Bytes& bytes, std::size_t at, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put_u32(bytes, at, bits);
}

void append_chunk(Bytes& image, std::uint32_t id, const Bytes& payload) {
    const std::size_t at = image.size();
    image.resize(at + 8);
    put_u32(image, at, id);
    put_u32(image, at + 4, static_cast<std::uint32_t>(payload.size()));
    image.insert(image.end(), payload.begin(), payload.end());
}

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------
// A synthetic TRACKB: a circle of 16 nodes, radius 100, counter-clockwise
// ---------------------------------------------------------------------

struct MarkerSpec {
    std::string name;
    std::uint32_t param;
    float x;
    float y;
    std::uint16_t angle;
};

struct ZoneSpec {
    std::uint32_t type;
    float direction[2];
    std::int8_t filter;
    float box_min[2];
    float box_max[2];
    std::vector<float> vertices;
};

struct TrackSpec {
    std::size_t nodes = 16;
    float radius = 100.0F;
    std::uint8_t closed = 1;
    float length = 0.0F;  // 0: measure it with the library and write that
    std::vector<MarkerSpec> markers;
    std::vector<ZoneSpec> zones;
    // When not empty, the nodes themselves, one per node, instead of the
    // sixteen of the circle.
    std::vector<std::array<float, 2>> points;
};

float circle_x(double degrees, double radius) {
    return static_cast<float>(radius * std::cos(degrees * kPi / 180.0));
}

float circle_y(double degrees, double radius) {
    return static_cast<float>(radius * std::sin(degrees * kPi / 180.0));
}

// The fixture's numbers are decimal literals, not cos and sin: a library
// cosine differs between runtimes, and a compiler folds some calls and not
// others, so a fixture computed with them differs between a Debug and a
// Release build. The literals are 100 cos and 100 sin of the angles named.
constexpr float kCircle[16][2] = {
    {100.0F, 0.0F},          {92.387953F, 38.268343F},   {70.710678F, 70.710678F},
    {38.268343F, 92.387953F}, {0.0F, 100.0F},             {-38.268343F, 92.387953F},
    {-70.710678F, 70.710678F}, {-92.387953F, 38.268343F}, {-100.0F, 0.0F},
    {-92.387953F, -38.268343F}, {-70.710678F, -70.710678F}, {-38.268343F, -92.387953F},
    {0.0F, -100.0F},          {38.268343F, -92.387953F},  {70.710678F, -70.710678F},
    {92.387953F, -38.268343F},
};

std::vector<MarkerSpec> circle_markers() {
    // 80 and 100 degrees for the two grids, 90, 180 and 270 for the lines.
    return {
        {"ForwardStartingPosition", 0, 17.364818F, 98.480775F, 0x4000},
        {"ForwardStartLine", 0, 0.0F, 100.0F, 0x0000},
        {"BackwardStartingPosition", 0, -17.364818F, 98.480775F, 0xC000},
        {"BackwardStartLine", 0, 0.0F, 100.0F, 0x8000},
        {"SplitTimeBoundary1", 0, -100.0F, 0.0F, 0x4000},
        {"SplitTimeBoundary2", 0, 0.0F, -100.0F, 0x4000},
    };
}

// The offsets every field check below is written against: the marker chunk
// first, at 0, so its payload at 8 is padded to 16; then the container.
constexpr std::size_t kMarkerChunkOffset = 0;
constexpr std::size_t kMarkerRecordOffset = 16;

Bytes marker_payload(const std::vector<MarkerSpec>& markers) {
    Bytes payload(8 + markers.size() * nfsu::kTrackMarkerRecordSize, 0x11);
    for (std::size_t m = 0; m < markers.size(); ++m) {
        const std::size_t at = 8 + m * nfsu::kTrackMarkerRecordSize;
        std::fill(payload.begin() + static_cast<std::ptrdiff_t>(at),
                  payload.begin() + static_cast<std::ptrdiff_t>(at + nfsu::kTrackMarkerRecordSize),
                  0);
        put_u32(payload, at, 11);
        put_u32(payload, at + 4, 11);
        put_u32(payload, at + 8, nfsu::track_marker_hash(markers[m].name));
        put_u32(payload, at + 12, markers[m].param);
        put_f32(payload, at + 16, markers[m].x);
        put_f32(payload, at + 20, markers[m].y);
        put_f32(payload, at + 24, 7.5F);
        put_u16(payload, at + 32, markers[m].angle);
    }
    return payload;
}

Bytes path_record(const TrackSpec& spec, float length) {
    Bytes record(nfsu::kTrackPathRecordSize, 0);
    record[1] = spec.closed;
    put_u16(record, 2, 0);
    put_u16(record, 4, static_cast<std::uint16_t>(spec.nodes));
    put_u16(record, 6, static_cast<std::uint16_t>(spec.nodes));
    put_f32(record, 8, 0.0F);
    put_f32(record, 12, length);
    put_f32(record, 16, 1.0F);
    put_f32(record, 20, spec.radius);
    put_f32(record, 24, 0.0F);
    return record;
}

// Sixteen nodes, the circle's, at spec.radius / 100 times the table.
Bytes node_payload(const TrackSpec& spec) {
    Bytes payload(spec.nodes * nfsu::kTrackPathNodeSize, 0);
    const float scale = spec.radius / 100.0F;
    for (std::size_t n = 0; n < spec.nodes; ++n) {
        const std::size_t at = n * nfsu::kTrackPathNodeSize;
        if (spec.points.empty()) {
            put_f32(payload, at, kCircle[n % 16][0] * scale);
            put_f32(payload, at + 4, kCircle[n % 16][1] * scale);
        } else {
            put_f32(payload, at, spec.points[n][0]);
            put_f32(payload, at + 4, spec.points[n][1]);
        }
        payload[at + 8] = 0;
        put_u16(payload, at + 0x0A, 0xFFFFU);
        put_u16(payload, at + 0x0C, 0xFFFFU);
        put_u16(payload, at + 0x0E, 0xFFFFU);
        // A sixteenth of the circumference each, as the exporter's are the
        // running length.
        put_f32(payload, at + 0x10, 39.269908F * scale * static_cast<float>(n));
    }
    return payload;
}

Bytes zone_payload(const std::vector<ZoneSpec>& zones) {
    Bytes payload(zones.size() * nfsu::kTrackPathZoneSize, 0);
    for (std::size_t z = 0; z < zones.size(); ++z) {
        const std::size_t at = z * nfsu::kTrackPathZoneSize;
        const ZoneSpec& zone = zones[z];
        put_u32(payload, at, zone.type);
        put_f32(payload, at + 0x04, 0.5F * (zone.box_min[0] + zone.box_max[0]));
        put_f32(payload, at + 0x08, 0.5F * (zone.box_min[1] + zone.box_max[1]));
        put_f32(payload, at + 0x0C, zone.direction[0]);
        put_f32(payload, at + 0x10, zone.direction[1]);
        payload[at + 0x18] = static_cast<unsigned char>(zone.filter);
        put_f32(payload, at + 0x28, zone.box_min[0]);
        put_f32(payload, at + 0x2C, zone.box_min[1]);
        put_f32(payload, at + 0x30, zone.box_max[0]);
        put_f32(payload, at + 0x34, zone.box_max[1]);
        put_u32(payload, at + 0x38, static_cast<std::uint32_t>(zone.vertices.size() / 2));
        for (std::size_t v = 0; v < zone.vertices.size(); ++v) {
            put_f32(payload, at + 0x3C + v * 4, zone.vertices[v]);
        }
    }
    return payload;
}

// A square zone polygon, counter-clockwise, and its box.
ZoneSpec square_zone(std::uint32_t type, float x0, float y0, float x1, float y1,
                     std::int8_t filter = -1, float dx = 0.0F, float dy = 0.0F) {
    ZoneSpec zone;
    zone.type = type;
    zone.direction[0] = dx;
    zone.direction[1] = dy;
    zone.filter = filter;
    zone.box_min[0] = x0;
    zone.box_min[1] = y0;
    zone.box_max[0] = x1;
    zone.box_max[1] = y1;
    zone.vertices = {x0, y0, x1, y0, x1, y1, x0, y1};
    return zone;
}

// The container sits after the marker chunk and an unrelated chunk.
Bytes track_image_with_length(const TrackSpec& spec, float length) {
    Bytes image;
    append_chunk(image, nfsu::kTrackMarkerChunkId, marker_payload(spec.markers));
    append_chunk(image, 0x00034250U, Bytes(24, 0));
    Bytes container;
    append_chunk(container, nfsu::kTrackPathRecordChunkId, path_record(spec, length));
    append_chunk(container, nfsu::kTrackPathNodeChunkId, node_payload(spec));
    append_chunk(container, nfsu::kTrackPathZoneChunkId, zone_payload(spec.zones));
    append_chunk(image, nfsu::kTrackPathContainerId, container);
    append_chunk(image, 0x00034130U, Bytes(16, 0));
    return image;
}

std::size_t path_record_offset(const TrackSpec& spec) {
    return 8 + 8 + spec.markers.size() * nfsu::kTrackMarkerRecordSize + 8 + 24 + 8 + 8;
}

// The length written into the path record is the spline's own measure, as
// retail's is to within 0.001 (docs/RACE_ROUTE.md), unless the spec says.
Bytes track_image(const TrackSpec& spec) {
    if (spec.length != 0.0F) {
        return track_image_with_length(spec, spec.length);
    }
    // A circle's circumference is within a fraction of a percent of its
    // Catmull-Rom loop, close enough for the loop to build, and the loop's
    // own measure is then written back.
    const Bytes probe = track_image_with_length(
        spec, static_cast<float>(2.0 * kPi * static_cast<double>(spec.radius)));
    const nfsu::RaceRouteTrack track = nfsu::parse_race_route_track(probe.data(), probe.size());
    nfsu::TrackPathSpline spline;
    std::string error;
    if (!nfsu::build_track_path_spline(track, false, spline, error)) {
        return probe;
    }
    return track_image_with_length(spec, spline.spline_length);
}

TrackSpec circle_spec() {
    TrackSpec spec;
    spec.markers = circle_markers();
    return spec;
}

void dump_issues(const std::vector<nfsu::RaceRouteIssue>& issues) {
    for (const nfsu::RaceRouteIssue& issue : issues) {
        std::cerr << "  issue at " << issue.offset << ": " << issue.message << '\n';
    }
}

bool has_issue(const nfsu::RaceRouteTrack& track, std::uint64_t offset,
               const std::string& text) {
    for (const nfsu::RaceRouteIssue& issue : track.issues) {
        if (issue.offset == offset && issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

nfsu::RaceRouteTrack parse(const Bytes& image) {
    return nfsu::parse_race_route_track(image.data(), image.size());
}

// ---------------------------------------------------------------------
// Driving
// ---------------------------------------------------------------------

// Points on the circle from `from` degrees to `to` degrees, `step` degrees
// apart, at `radius`, both ends included.
void arc(std::vector<std::array<float, 2>>& points, double from, double to, double step,
         double radius = 100.0) {
    const double span = to - from;
    const int count = static_cast<int>(std::ceil(std::fabs(span) / step));
    for (int k = 0; k <= count; ++k) {
        const double at = from + span * static_cast<double>(k) / static_cast<double>(count);
        points.push_back({circle_x(at, radius), circle_y(at, radius)});
    }
}

struct Replay {
    nfsu::RaceProgress progress;
    std::vector<nfsu::RaceProgressEvent> events;
};

Replay replay(const nfsu::RaceRoute& route, const nfsu::RaceRouteTrack& track,
              const std::vector<std::array<float, 2>>& points) {
    Replay result;
    nfsu::start_race_progress(route, track, points.front().data(), result.progress);
    for (std::size_t i = 1; i < points.size(); ++i) {
        nfsu::step_race_progress(route, track, result.progress, points[i].data(),
                                 1.0F / 60.0F, result.events);
    }
    return result;
}

std::string describe(const std::vector<nfsu::RaceProgressEvent>& events,
                     bool skip_wrong_way = false) {
    std::string text;
    for (const nfsu::RaceProgressEvent& event : events) {
        if (skip_wrong_way && (event.kind == nfsu::RaceEventKind::wrong_way_on ||
                               event.kind == nfsu::RaceEventKind::wrong_way_off)) {
            continue;
        }
        if (event.kind == nfsu::RaceEventKind::recovered) {
            continue;
        }
        text += nfsu::race_event_kind_name(event.kind);
        text += '(';
        text += std::to_string(event.index);
        text += ',';
        text += std::to_string(event.lap);
        text += ')';
        text += ' ';
    }
    return text;
}

nfsu::RaceRoute circle_route(const nfsu::RaceRouteTrack& track, int laps, int direction,
                             std::int8_t lap_skip = 0) {
    nfsu::RaceRoute route;
    std::string error;
    const bool built = nfsu::build_race_route(track, laps, direction, lap_skip, route, error);
    check(built, "the circle route builds: " + error);
    return route;
}

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

void test_parse() {
    const TrackSpec spec = circle_spec();
    const Bytes image = track_image(spec);
    const nfsu::RaceRouteTrack track = parse(image);
    dump_issues(track.issues);
    check(track.ok(), "the circle parses");
    check(track.marker_chunk_offset == kMarkerChunkOffset, "marker chunk offset");
    check(track.marker_record_offset == kMarkerRecordOffset, "marker records start 16-aligned");
    check(track.markers.size() == 6, "six markers");
    check(track.paths.size() == 1, "one path");
    check(track.nodes.size() == 16, "sixteen nodes");
    check(track.zones.empty(), "no zones");
    if (!track.ok() || track.markers.size() != 6 || track.paths.size() != 1 ||
        track.nodes.size() != 16) {
        return;
    }
    const nfsu::TrackMarker& start = track.markers[1];
    check(start.offset == kMarkerRecordOffset + nfsu::kTrackMarkerRecordSize, "marker offset");
    check(start.name_hash == nfsu::track_marker_hash("ForwardStartLine"), "marker hash");
    check(std::string(nfsu::track_marker_name(start.name_hash)) == "ForwardStartLine",
          "marker name");
    check(start.position[0] == 0.0F && start.position[1] == 100.0F &&
              start.position[2] == 7.5F,
          "marker position");
    check(track.markers[3].angle == 0x8000, "marker angle");
    const std::size_t record = path_record_offset(spec);
    check(track.paths[0].offset == record, "path record offset");
    check(track.paths[0].closed == 1 && track.paths[0].first == 0 && track.paths[0].end == 16 &&
              track.paths[0].count == 16,
          "path fields");
    check(track.paths[0].distance_scale == 1.0F, "path scale");
    check(track.nodes[4].offset == record + 40 + 8 + 4 * 20, "node offset");
    check(track.nodes[4].x == 0.0F && track.nodes[4].y == 100.0F &&
              track.nodes[1].x == 92.387953F,
          "node position");
    check(track.nodes[4].links[0] == -1 && track.nodes[4].path == 0, "node links and path");
    // The retail hashes are what the executable compares (docs/RACE_ROUTE.md).
    check(nfsu::track_marker_hash("ForwardStartLine") == 0x6A56B90AU, "bStringHash of a name");
    check(nfsu::track_marker_hash("SplitTimeBoundary1") == 0xE5FAE1CFU, "bStringHash, split 1");
    check(nfsu::track_marker_name(0x12345678U) == nullptr, "an unknown hash has no name");
}

void test_parse_refusals() {
    const TrackSpec spec = circle_spec();
    const std::size_t record = path_record_offset(spec);
    const std::size_t nodes = record + 40 + 8;
    const std::size_t zones_chunk = nodes + 16 * 20;
    {
        Bytes image = track_image(spec);
        image.push_back(0);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, image.size() - 1, "trailing bytes"),
              "a trailing byte is refused");
    }
    {
        // A node table that is not a whole number of records.
        Bytes image = track_image(spec);
        put_u32(image, nodes - 4, 16 * 20 - 4);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok(), "a short node table is refused");
    }
    {
        Bytes image = track_image(spec);
        put_u32(image, nodes - 8, 0x00034150U);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, nodes - 8, "expected 0x00034149"),
              "a child out of order is refused by its id");
    }
    {
        Bytes image = track_image(spec);
        image[nodes + 4 * 20 + 8] = 1;
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, nodes + 4 * 20 + 8, "names path 1"),
              "a node naming a path that does not exist is refused");
    }
    {
        Bytes image = track_image(spec);
        put_u16(image, nodes + 4 * 20 + 0x0C, 16);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, nodes + 4 * 20 + 0x0C, "neither -1 nor a node"),
              "a link past the node table is refused");
    }
    {
        Bytes image = track_image(spec);
        put_u16(image, record + 6, 15);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, record + 6, "must equal end - first"),
              "a control-point count other than end - first is refused");
    }
    {
        Bytes image = track_image(spec);
        put_u16(image, record + 4, 17);
        put_u16(image, record + 6, 17);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, record + 2, "not inside the 16 nodes"),
              "a node range past the table is refused");
    }
    {
        Bytes image = track_image(spec);
        put_f32(image, record + 12, 0.0F);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, record + 8, "finite and positive"),
              "a zero path length is refused (every fraction divides by it)");
    }
    {
        Bytes image = track_image(spec);
        put_f32(image, nodes + 20 + 4, std::nanf(""));
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, nodes + 20, "not finite"),
              "a node that is not finite is refused");
    }
    {
        TrackSpec zoned = spec;
        zoned.zones = {square_zone(nfsu::kZoneReset, -1, -1, 1, 1),
                       square_zone(nfsu::kZoneWrongWay, -1, -1, 1, 1)};
        const Bytes image = track_image(zoned);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, zones_chunk + 8 + 180, "follows type 2"),
              "zones out of type order are refused");
    }
    {
        TrackSpec zoned = spec;
        zoned.zones = {square_zone(11, -1, -1, 1, 1)};
        const nfsu::RaceRouteTrack track = parse(track_image(zoned));
        check(!track.ok() && has_issue(track, zones_chunk + 8, "not one of the 11"),
              "a zone type past 10 is refused");
    }
    {
        TrackSpec zoned = spec;
        zoned.zones = {square_zone(nfsu::kZoneReset, -1, -1, 1, 1)};
        Bytes image = track_image(zoned);
        put_u32(image, zones_chunk + 8 + 0x38, 16);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, zones_chunk + 8 + 0x38, "does not fit"),
              "a zone vertex count past 15 is refused");
    }
    {
        // The marker chunk removed: the route has no markers to project.
        Bytes image = track_image(spec);
        const std::size_t marker_bytes = 8 + 8 + 6 * 48;
        image.erase(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(marker_bytes));
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, 0, "expected exactly one chunk 0x00034146, found 0"),
              "a bundle with no marker chunk is refused");
    }
    {
        // The marker payload one byte short of a whole record.
        Bytes image = track_image(spec);
        put_u32(image, 4, 8 + 6 * 48 - 1);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok(), "a marker chunk that is not whole records is refused");
    }
    {
        const Bytes image = track_image(spec);
        Bytes twice = image;
        twice.insert(twice.end(), image.begin() + 8 + 8 + 6 * 48, image.end());
        const nfsu::RaceRouteTrack track = parse(twice);
        bool named = false;
        for (const nfsu::RaceRouteIssue& issue : track.issues) {
            named = named || issue.message.find("expected exactly one chunk 0x80034147, found 2") !=
                                 std::string::npos;
        }
        check(!track.ok() && named, "two path containers are refused");
    }
}

void test_spline() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    nfsu::TrackPathSpline spline;
    std::string error;
    check(nfsu::build_track_path_spline(track, false, spline, error), "the spline builds");
    check(spline.count == 16, "sixteen control points");
    // A Catmull-Rom spline passes through its control points.
    for (int n = 0; n < 16; ++n) {
        float point[3];
        nfsu::evaluate_track_path(spline, static_cast<float>(n), 0, point);
        check(point[0] == track.nodes[static_cast<std::size_t>(n)].x &&
                  point[1] == track.nodes[static_cast<std::size_t>(n)].y,
              "the spline passes through node " + std::to_string(n));
    }
    // The measured length is a circle's to within half a percent, and the
    // running length grows with the node.
    const double circumference = 2.0 * kPi * 100.0;
    check(std::fabs(spline.spline_length - circumference) < 0.005 * circumference,
          "the arc length is the circle's");
    for (int n = 1; n < 16; ++n) {
        check(spline.points[static_cast<std::size_t>(n) * 3 + 2] >
                  spline.points[static_cast<std::size_t>(n - 1) * 3 + 2],
              "running length increases at node " + std::to_string(n));
    }
    // The parameter wraps: n + 0.25 is 0.25.
    float a[3];
    float b[3];
    nfsu::evaluate_track_path(spline, 16.25F, 0, a);
    nfsu::evaluate_track_path(spline, 0.25F, 0, b);
    check(std::fabs(a[0] - b[0]) < 1e-3F && std::fabs(a[1] - b[1]) < 1e-3F,
          "the parameter wraps around the loop");
    // The running length is carried across the seam: just past the end of
    // the loop it is the whole length, not zero.
    float seam[3];
    nfsu::evaluate_track_path(spline, 15.999F, 0, seam);
    check(seam[2] > spline.spline_length * 0.99F, "the running length is carried over the seam");

    // An open path builds with the retail type-2 end treatment: the
    // mirrored points carry the basis past each end and the parameter is
    // never wrapped.59CEA0 extrapolates beyond the endpoint slots.
    TrackSpec open = circle_spec();
    open.closed = 0;
    open.length = 600.0F;
    const nfsu::RaceRouteTrack open_track = parse(track_image(open));
    check(open_track.ok(), "an open path parses");
    nfsu::TrackPathSpline open_spline;
    check(nfsu::build_track_path_spline(open_track, false, open_spline, error),
          "an open path's spline builds");
    check(open_spline.open, "an open path's spline is open");
    float end[3];
    float past[3];
    nfsu::evaluate_track_path(open_spline, 16.5F, 0, end);
    nfsu::evaluate_track_path(open_spline, 16.4F, 0, past);
    for(unsigned k=0;k<2;++k) {
        const float expected=(kCircle[15][k]-kCircle[14][k])*1.5F+kCircle[15][k];
        check(std::fabs(end[k]-expected)<.0001F,"open endpoint extends its authored tangent");
    }
    check(std::fabs(end[0]-past[0])+std::fabs(end[1]-past[1])>1.F,
          "distinct parameters past the open end are not clamped");
    nfsu::evaluate_track_path(open_spline, -0.5F, 0, end);
    check(std::isfinite(end[0]) && std::isfinite(end[1]),
          "an extrapolated parameter before the open start stays finite");
}

void test_coordinate() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    nfsu::TrackPathSpline forward;
    nfsu::TrackPathSpline reverse;
    std::string error;
    check(nfsu::build_track_path_spline(track, false, forward, error), "forward spline");
    check(nfsu::build_track_path_spline(track, true, reverse, error), "reverse spline");
    for (double degrees = 10.0; degrees < 360.0; degrees += 37.0) {
        for (const double radius : {100.0, 90.0, 115.0}) {
            const float query[2] = {circle_x(degrees, radius), circle_y(degrees, radius)};
            nfsu::TrackPathCoordinate coordinate;
            nfsu::update_track_path_coordinate(forward, track, coordinate, query);
            const double expected = degrees / 360.0;
            check(std::fabs(coordinate.fraction - expected) < 0.003,
                  "fraction at " + std::to_string(degrees) + " degrees, radius " +
                      std::to_string(radius) + ": " + std::to_string(coordinate.fraction));
            check(std::fabs(coordinate.tangent[0] + std::sin(degrees * kPi / 180.0)) < 0.02 &&
                      std::fabs(coordinate.tangent[1] - std::cos(degrees * kPi / 180.0)) < 0.02,
                  "tangent at " + std::to_string(degrees) + " degrees");
            nfsu::TrackPathCoordinate backwards;
            nfsu::update_track_path_coordinate(reverse, track, backwards, query);
            check(std::fabs(backwards.fraction - (1.0 - expected)) < 0.003,
                  "a reverse race measures from the other end at " + std::to_string(degrees));
            check(backwards.tangent[0] == -coordinate.tangent[0] &&
                      backwards.tangent[1] == -coordinate.tangent[1],
                  "a reverse race turns the direction around");
        }
    }
    // The coordinate keeps its node while the car moves in small steps and
    // starts from the last parameter.
    nfsu::TrackPathCoordinate coordinate;
    const float first[2] = {circle_x(45.0, 100.0), circle_y(45.0, 100.0)};
    nfsu::update_track_path_coordinate(forward, track, coordinate, first);
    const int node = coordinate.node;
    check(node == 2, "the node nearest 45 degrees is node 2");
    const float second[2] = {circle_x(46.0, 100.0), circle_y(46.0, 100.0)};
    nfsu::update_track_path_coordinate(forward, track, coordinate, second);
    check(coordinate.node == 2 && coordinate.fraction > 45.5F / 360.0F,
          "a small step keeps the node and advances the fraction");

    // A query more than 128 from the last one is searched from every node
    // (0x00443A41): straight across the circle, the node is the one nearest
    // the query, not a neighbour of the last one.
    nfsu::TrackPathCoordinate jump;
    const float east[2] = {100.0F, 0.0F};
    nfsu::update_track_path_coordinate(forward, track, jump, east);
    check(jump.node == 0, "the node nearest 0 degrees is node 0");
    const float west[2] = {-100.0F, 0.0F};
    nfsu::update_track_path_coordinate(forward, track, jump, west);
    check(jump.node == 8 && std::fabs(jump.fraction - 0.5F) < 0.003F,
          "a jump across the circle is searched from every node");
}

void test_boundaries() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    const nfsu::RaceRoute route = circle_route(track, 2, 0);
    check(route.boundaries.size() == 3, "start line and two splits");
    if (route.boundaries.size() != 3) {
        return;
    }
    check(route.start == 0 && route.finish == 0, "a circuit finishes at its start line");
    check(route.boundaries[0].name == "ForwardStartLine" && route.boundaries[0].marker == 1,
          "the start line is projected from its marker");
    check(std::fabs(route.boundaries[0].percent - 25.0F) < 0.3F, "start line at 25 percent");
    check(std::fabs(route.boundaries[1].percent - 50.0F) < 0.3F, "split 1 at 50 percent");
    check(std::fabs(route.boundaries[2].percent - 75.0F) < 0.3F, "split 2 at 75 percent");
    check(route.laps == 2, "two laps");

    const nfsu::RaceRoute backwards = circle_route(track, 3, 1);
    check(backwards.boundaries.size() == 3 &&
              backwards.boundaries[0].name == "BackwardStartLine" &&
              backwards.boundaries[1].name == "SplitTimeBoundary2" &&
              backwards.boundaries[2].name == "SplitTimeBoundary1",
          "a reverse race lists the splits the other way round");
    check(backwards.boundaries.size() == 3 &&
              std::fabs(backwards.boundaries[0].percent - 75.0F) < 0.3F &&
              std::fabs(backwards.boundaries[1].percent - 25.0F) < 0.3F &&
              std::fabs(backwards.boundaries[2].percent - 50.0F) < 0.3F,
          "reverse percentages");

    // No markers at all: the defaults of 0x00434810.
    TrackSpec bare = circle_spec();
    bare.markers = {{"Unrelated", 0, 0.0F, 0.0F, 0}};
    const nfsu::RaceRouteTrack bare_track = parse(track_image(bare));
    const nfsu::RaceRoute defaults = circle_route(bare_track, 2, 0);
    check(defaults.boundaries.size() == 3 && defaults.boundaries[0].marker == -1 &&
              defaults.boundaries[0].percent == 5.0F && defaults.boundaries[1].percent == 38.0F &&
              defaults.boundaries[2].percent == 71.0F,
          "defaults: 5, then 33 and 66 past it, no third split");
    // Point to point: one lap, and a finish line 95 past the start, less 100.
    const nfsu::RaceRoute sprint = circle_route(bare_track, 2, 0, 1);
    check(sprint.laps == 1 && sprint.point_to_point, "a lap-skip track runs one lap");
    check(sprint.boundaries.size() == 4 && sprint.finish == 3 &&
              sprint.boundaries[3].percent == 100.0F,
          "the finish default is start + 95 = 100 (not above 100, so not wrapped)");
    check(nfsu::race_laps(2, 0) == 2 && nfsu::race_laps(2, 1) == 1 &&
              nfsu::race_laps(1, 1) == 1 && nfsu::race_laps(0, 1) == 0,
          "the laps rule of 0x004342A4..0x004342D6");
    nfsu::RaceRoute refused;
    std::string error;
    check(!nfsu::build_race_route(track, 2, 2, 0, refused, error), "direction 2 is refused");
}

// The whole race: from the grid behind the line, two laps counter-clockwise.
void test_laps() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    const nfsu::RaceRoute route = circle_route(track, 2, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 80.0, 80.0 + 720.0 + 20.0, 0.25);
    const Replay run = replay(route, track, points);
    check(describe(run.events) ==
              "crossed-forward(0,0) crossed-forward(1,0) crossed-forward(2,0) "
              "crossed-forward(0,1) lap-complete(0,1) crossed-forward(1,1) "
              "crossed-forward(2,1) crossed-forward(0,2) lap-complete(0,2) finished(0,2) ",
          "two laps: " + describe(run.events));
    check(run.progress.finished && run.progress.max_lap == 2 &&
              nfsu::race_laps_completed(run.progress) == 2,
          "finished after two laps");
    check(run.progress.checkpoint == 10 && run.progress.max_checkpoint == 10,
          "checkpoint numbering is 5 * lap + boundary");
    // A finished car is not updated again (0x00435603).
    const std::size_t finished_at = run.progress.finish_step;
    check(finished_at < points.size() - 1, "the race finished before the path ended");
    check(run.progress.step == finished_at, "no step after the finish");

    // Replaying the same path gives the same events.
    const Replay again = replay(route, track, points);
    check(describe(again.events) == describe(run.events) &&
              again.progress.finish_step == run.progress.finish_step,
          "the replay is deterministic");
}

// A lap does not count while the car has not crossed the line forward: a
// car that starts ahead of the line has a lap to drive before its first.
void test_start_finish_crossing() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    const nfsu::RaceRoute route = circle_route(track, 1, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 100.0, 100.0 + 360.0, 0.25);
    const Replay run = replay(route, track, points);
    check(describe(run.events) ==
              "crossed-forward(1,-1) crossed-forward(2,-1) crossed-forward(0,0) ",
          "starting past the line, one loop is the start of lap 1: " + describe(run.events));
    check(!run.progress.finished && nfsu::race_laps_completed(run.progress) == 0,
          "no lap yet");
}

// Touching the finish percentage exactly counts as crossing it (0x00433722
// takes the car as past when the difference is zero), and backing off again
// is a reverse crossing that takes the lap back.
void test_graze() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    nfsu::RaceRoute route = circle_route(track, 2, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 80.0, 89.0, 0.25);
    const std::array<float, 2> touch = {circle_x(89.5, 100.0), circle_y(89.5, 100.0)};
    // Put the boundary exactly where the step to `touch` will measure the
    // car: the same coordinate state and the same query give the same float.
    nfsu::RaceProgress probe;
    nfsu::start_race_progress(route, track, points.front().data(), probe);
    std::vector<nfsu::RaceProgressEvent> ignored;
    for (std::size_t i = 1; i < points.size(); ++i) {
        nfsu::step_race_progress(route, track, probe, points[i].data(), 1.0F / 60.0F, ignored);
    }
    nfsu::TrackPathCoordinate at = probe.coordinate;
    nfsu::update_track_path_coordinate(route.spline, track, at, touch.data());
    route.boundaries[0].percent = at.fraction * 100.0F;
    route.boundaries[1].percent = 60.0F;
    route.boundaries[2].percent = 80.0F;

    std::vector<std::array<float, 2>> graze = points;
    graze.push_back(touch);
    arc(graze, 89.0, 85.0, 0.25);
    const Replay run = replay(route, track, graze);
    check(describe(run.events, true) == "crossed-forward(0,0) crossed-backward(0,-1) ",
          "touching the line and backing off: " + describe(run.events, true));
    check(run.progress.lap == -1, "the lap is taken back");

    // Stopping one float short of the line counts nothing.
    nfsu::RaceRoute short_route = route;
    short_route.boundaries[0].percent = std::nextafter(at.fraction * 100.0F, 200.0F);
    const Replay near_miss = replay(short_route, track, graze);
    check(describe(near_miss.events, true).empty(),
          "one float short of the line is no crossing: " + describe(near_miss.events, true));
}

// The window: a step that lands exactly 25 percent past a boundary counts,
// and one that lands further does not - it is missed.
void test_window_edge() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    nfsu::RaceRoute route = circle_route(track, 2, 0);
    route.boundaries.resize(1);
    std::vector<std::array<float, 2>> points;
    arc(points, 110.0, 120.0, 0.5);
    // Jump from 120 to 230 degrees, 30.6 percent: the boundary goes 25
    // percent behind the landing, which is 5.6 percent ahead of the take-off.
    const std::array<float, 2> landing = {circle_x(230.0, 100.0), circle_y(230.0, 100.0)};
    nfsu::RaceProgress probe;
    nfsu::start_race_progress(route, track, points.front().data(), probe);
    std::vector<nfsu::RaceProgressEvent> ignored;
    for (std::size_t i = 1; i < points.size(); ++i) {
        nfsu::step_race_progress(route, track, probe, points[i].data(), 1.0F / 60.0F, ignored);
    }
    nfsu::TrackPathCoordinate at = probe.coordinate;
    nfsu::update_track_path_coordinate(route.spline, track, at, landing.data());
    const float land = at.fraction * 100.0F;
    const float edge = land - 25.0F;
    check(land - edge == 25.0F, "the window edge is exact in float");
    route.boundaries[0].percent = edge;
    std::vector<std::array<float, 2>> jump = points;
    jump.push_back(landing);
    const Replay on_edge = replay(route, track, jump);
    check(describe(on_edge.events, true) == "crossed-forward(0,0) ",
          "landing exactly 25 past counts: " + describe(on_edge.events, true));

    route.boundaries[0].percent = std::nextafter(edge, 0.0F);
    const Replay beyond = replay(route, track, jump);
    check(describe(beyond.events, true) == "missed-forward(0,-1) ",
          "landing past the window is missed: " + describe(beyond.events, true));
    check(beyond.progress.lap == -1, "a missed finish line counts no lap");
}

// A car that skips a split boundary by more than the window still completes
// its lap at the finish line: nothing in 0x004338C0 requires the splits.
// Skipping the finish line itself costs the lap.
void test_missed_checkpoint() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    const nfsu::RaceRoute route = circle_route(track, 1, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 80.0, 170.0, 0.25);
    arc(points, 290.0, 80.0 + 360.0 + 20.0, 0.25);
    const Replay skip = replay(route, track, points);
    check(describe(skip.events, true) ==
              "crossed-forward(0,0) missed-forward(1,0) crossed-forward(2,0) "
              "crossed-forward(0,1) lap-complete(0,1) finished(0,1) ",
          "a skipped split is reported and the lap still counts: " +
              describe(skip.events, true));
    check(skip.progress.finished, "finished despite the skipped split");

    // Round once, then from 60 degrees straight to 190: 27.8 percent past
    // the line at 90 and 2.8 past split 1 at 180. The same jump lands 22.2
    // percent short of split 2 at 270 from 41.7 past it, which 0x00433650
    // reads as a backward crossing of split 2: the wrapped differences are
    // all it sees.
    std::vector<std::array<float, 2>> finish_cut;
    arc(finish_cut, 80.0, 360.0 + 60.0, 0.25);
    arc(finish_cut, 360.0 + 190.0, 720.0 + 180.0, 0.25);
    const Replay lost = replay(route, track, finish_cut);
    check(describe(lost.events, true) ==
              "crossed-forward(0,0) crossed-forward(1,0) crossed-forward(2,0) "
              "missed-forward(0,0) crossed-forward(1,0) crossed-backward(2,0) "
              "crossed-forward(2,0) crossed-forward(0,1) lap-complete(0,1) finished(0,1) ",
          "a skipped finish line costs the lap: " + describe(lost.events, true));
}

// Driving the wrong way: the splits and the line are crossed backward, the
// lap counter goes below -1, and the wrong-way flag comes on once the car is
// moving at speed and goes off when it turns round.
void test_reverse_crossing_and_wrong_way() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    const nfsu::RaceRoute route = circle_route(track, 2, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 80.0, 80.0 - 370.0, 0.25);
    arc(points, 80.0 - 370.0, 80.0 - 370.0 + 40.0, 0.25);
    const Replay run = replay(route, track, points);
    check(describe(run.events) ==
              "wrong-way-on(-1,-1) crossed-backward(2,-1) crossed-backward(1,-1) "
              "crossed-backward(0,-2) wrong-way-off(-1,-2) crossed-forward(0,-1) ",
          "backward round the loop and forward over the line: " + describe(run.events));
    check(run.progress.lap == -1 && run.progress.max_lap == 0 && !run.progress.finished,
          "the lap counter is back where it started");

    // Too slow to decide: a car creeping backward is not flagged.
    std::vector<std::array<float, 2>> creep;
    arc(creep, 80.0, 75.0, 0.001);
    const Replay slow = replay(route, track, creep);
    check(describe(slow.events).empty(), "a creeping car is not flagged: " + describe(slow.events));
}

void test_wrong_way_zones() {
    // A NO_WRONG_WAY zone over the whole circle: never flagged.
    TrackSpec quiet = circle_spec();
    quiet.zones = {square_zone(nfsu::kZoneNoWrongWay, -200, -200, 200, 200)};
    const nfsu::RaceRouteTrack quiet_track = parse(track_image(quiet));
    check(quiet_track.ok(), "the zoned circle parses");
    const nfsu::RaceRoute quiet_route = circle_route(quiet_track, 2, 0);
    std::vector<std::array<float, 2>> backward;
    arc(backward, 80.0, 20.0, 0.25);
    const Replay quiet_run = replay(quiet_route, quiet_track, backward);
    check(describe(quiet_run.events).find("wrong-way") == std::string::npos,
          "NO_WRONG_WAY suppresses the flag: " + describe(quiet_run.events));

    // The same zone for the reverse direction only: the forward race is
    // flagged again (0x00443710..0x0044371E).
    TrackSpec other = circle_spec();
    other.zones = {square_zone(nfsu::kZoneNoWrongWay, -200, -200, 200, 200, 1)};
    const nfsu::RaceRouteTrack other_track = parse(track_image(other));
    const Replay other_run = replay(circle_route(other_track, 2, 0), other_track, backward);
    check(describe(other_run.events).find("wrong-way-on") != std::string::npos,
          "a zone for the other direction does not apply");

    // A WRONG_WAY zone pointing the other way round: driving forward is
    // wrong inside it.
    TrackSpec forced = circle_spec();
    forced.zones = {square_zone(nfsu::kZoneWrongWay, -200, -200, 200, 200, -1, 0.0F, 1.0F)};
    const nfsu::RaceRouteTrack forced_track = parse(track_image(forced));
    std::vector<std::array<float, 2>> forward;
    arc(forward, 170.0, 190.0, 0.25);
    const Replay forced_run = replay(circle_route(forced_track, 2, 0), forced_track, forward);
    check(describe(forced_run.events).rfind("wrong-way-on", 0) == 0,
          "WRONG_WAY measures against its own direction: " + describe(forced_run.events));

    // A RESET zone is reported once on entry.
    TrackSpec reset = circle_spec();
    reset.zones = {square_zone(nfsu::kZoneReset, -10, 90, 10, 110)};
    const nfsu::RaceRouteTrack reset_track = parse(track_image(reset));
    std::vector<std::array<float, 2>> through;
    arc(through, 80.0, 100.0, 0.25);
    const Replay reset_run = replay(circle_route(reset_track, 2, 0), reset_track, through);
    check(describe(reset_run.events) == "reset-zone(0,-1) crossed-forward(0,0) ",
          "a reset zone is reported once: " + describe(reset_run.events));
}

// A coordinate left behind by a car that cut across the middle is found
// again on a step that is a multiple of 60 (0x00426BE2).
void test_recovery() {
    // A circle of radius 1000, so that the middle is further than 300 from
    // every node.
    TrackSpec wide = circle_spec();
    wide.radius = 1000.0F;
    for (MarkerSpec& marker : wide.markers) {
        marker.x *= 10.0F;
        marker.y *= 10.0F;
    }
    const nfsu::RaceRouteTrack track = parse(track_image(wide));
    const nfsu::RaceRoute route = circle_route(track, 2, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 80.0, 100.0, 0.025, 1000.0);
    // Straight across the middle in half-metre steps: the tracked node is
    // left behind, and the middle is 1000 from every node.
    const std::array<float, 2> from = points.back();
    const std::array<float, 2> to = {circle_x(280.0, 1000.0), circle_y(280.0, 1000.0)};
    for (int k = 1; k <= 4000; ++k) {
        const float t = static_cast<float>(k) / 4000.0F;
        points.push_back({from[0] + (to[0] - from[0]) * t, from[1] + (to[1] - from[1]) * t});
    }
    const Replay run = replay(route, track, points);
    bool recovered = false;
    for (const nfsu::RaceProgressEvent& event : run.events) {
        if (event.kind == nfsu::RaceEventKind::recovered) {
            recovered = true;
            check(event.step % 60 == 0, "recovery happens on a multiple of 60");
        }
    }
    check(recovered, "the coordinate is searched again after the cut");
    check(std::fabs(run.progress.previous_percent - 280.0F / 3.6F) < 1.0F,
          "after the cut the car is measured where it is");
}


// ---------------------------------------------------------------------
// Round 2: the retail angle, the wrong-way thresholds, resets, the
// off-path rule, the lap counter's width, absurd data and the retail
// summation order
// ---------------------------------------------------------------------

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// 0x00564DB0 against a model of it - a numpy float32 transcription of the
// disassembly this project's worker wrote beside the code, not in the
// repository; the round-2 reviewer of PR #168 reproduced all three digests
// and the table's with a model of their own - and the table against the
// executable's bytes.
void test_track_angle() {
    struct Case {
        float x;
        float y;
        std::uint16_t angle;
    };
    const Case cases[] = {
        {1.0F, 0.0F, 0},          {0.0F, 1.0F, 16384},       {-1.0F, 0.0F, 32768},
        {0.0F, -1.0F, 49152},     {1.0F, 1.0F, 8192},        {-1.0F, 1.0F, 24576},
        {-1.0F, -1.0F, 40960},    {1.0F, -1.0F, 57344},      {3.0F, 4.0F, 9673},
        // No length at all: the steep branch with a ratio of 0.
        {0.0F, 0.0F, 16384},
        // The two directions the review found atan2 splitting between
        // MinGW and newlib: the retail routine gives both 20023.
        {-3.9831483F, 10.948146F, 20023}, {-3.9831486F, 10.948146F, 20023},
        // The wrong-way thresholds below, as dyadic steps.
        {-5960.0F / 16384.0F, 1.0F, 20023}, {-5962.0F / 16384.0F, 1.0F, 20024},
        {-2888.0F / 16384.0F, 1.0F, 18203}, {-2889.0F / 16384.0F, 1.0F, 18204},
        // x * x + y * y exactly the minimum, 1e-4 in float (0x38D1B717):
        // not above it, so a ratio of 0 on either branch. Taking the
        // minimum itself would give 1044 and 15340.
        {0.009949874F, 0.001F, 0}, {0.001F, 0.009949874F, 16384},
    };
    for (const Case& c : cases) {
        const std::uint16_t got = nfsu::track_angle(c.x, c.y);
        check(got == c.angle, "track_angle(" + std::to_string(c.x) + ", " +
                                  std::to_string(c.y) + ") = " + std::to_string(got) +
                                  ", expected " + std::to_string(c.angle));
    }
    // 4096 directions on two grids, hashed as little-endian 16-bit angles;
    // the digests are the model's.
    unsigned char grid[4096 * 2];
    unsigned char grid2[4096 * 2];
    for (int i = 0; i < 4096; ++i) {
        const float x = static_cast<float>(i % 64) - 31.5F;
        const float y = static_cast<float>(i / 64) - 31.5F;
        const std::uint16_t a = nfsu::track_angle(x, y);
        grid[i * 2] = static_cast<unsigned char>(a & 0xFFU);
        grid[i * 2 + 1] = static_cast<unsigned char>(a >> 8U);
        const float x2 = static_cast<float>(((i * 37) % 4096) - 2048) / 256.0F;
        const float y2 = static_cast<float>(((i * 101) % 4096) - 2047) / 1024.0F;
        const std::uint16_t b = nfsu::track_angle(x2, y2);
        grid2[i * 2] = static_cast<unsigned char>(b & 0xFFU);
        grid2[i * 2 + 1] = static_cast<unsigned char>(b >> 8U);
    }
    check(nfsu::sha256_hex(nfsu::sha256(grid, sizeof(grid))) ==
              "85ACB1D0BC1095DB5AA97E8E9360DE674D06DB83F1090DDD52D9CFCCB26F413E",
          "track_angle over the first grid matches the model");
    check(nfsu::sha256_hex(nfsu::sha256(grid2, sizeof(grid2))) ==
              "09C286ECB5B1E405977B47680A5F0FB6C5A99EBD7AFF9A967B4141B280D59751",
          "track_angle over the second grid matches the model");
    // 0x00564CD0 over its whole range, which track_angle never leaves the
    // first two of its twelve intervals of: 4096 sines from -1.08 to 1.08
    // and 64 either side of 1 in steps of 2^-16, hashed as little-endian
    // 32-bit angles against the model's digest.
    std::vector<unsigned char> sines;
    const auto put_angle = [&](std::uint32_t a) {
        for (unsigned int b = 0; b < 4; ++b) {
            sines.push_back(static_cast<unsigned char>((a >> (8U * b)) & 0xFFU));
        }
    };
    for (int i = 0; i < 4096; ++i) {
        put_angle(nfsu::track_arcsine_angle(static_cast<float>(i - 2048) / 1900.0F));
    }
    for (int k = 0; k < 64; ++k) {
        const float s = 1.0F - static_cast<float>(k) / 65536.0F;
        put_angle(nfsu::track_arcsine_angle(s));
        put_angle(nfsu::track_arcsine_angle(-s));
    }
    check(nfsu::sha256_hex(nfsu::sha256(sines.data(), sines.size())) ==
              "546A16A352903AAFA9AFAB8EBF943FE4F884316ACD6269BD2BDB38E5306BD1A8",
          "the arcsine over its whole range matches the model");
    unsigned char table[nfsu::kTrackAngleTableBytes];
    nfsu::track_angle_table_bytes(table);
    check(nfsu::sha256_hex(nfsu::sha256(table, sizeof(table))) ==
              "BA4B65CEDA6B5BE5C8F7A7FC2D52B0A16D03D9507D602590BACB78DC98E71A87",
          "the arcsine table is the executable's 1664 bytes at 0x006F2280");
}

// A circle whose wrong-way reference is +x everywhere: a WRONG_WAY zone of
// direction (1, 0) over the whole track, so the step's own angle decides.
// Every step is (-t, 1) with t a multiple of 1/16384, which every position
// holds exactly, so each step's angle is the table's above.
Replay wrong_way_run(const std::vector<int>& steps) {
    TrackSpec spec = circle_spec();
    spec.zones = {square_zone(nfsu::kZoneWrongWay, -1000, -1000, 1000, 1000, -1, 1.0F, 0.0F)};
    const nfsu::RaceRouteTrack track = parse(track_image(spec));
    check(track.ok(), "the wrong-way circle parses");
    const nfsu::RaceRoute route = circle_route(track, 2, 0);
    std::vector<std::array<float, 2>> points;
    float x = 100.0F;
    float y = 0.0F;
    points.push_back({x, y});
    for (const int t : steps) {
        x = x - static_cast<float>(t) / 16384.0F;
        y = y + 1.0F;
        points.push_back({x, y});
    }
    return replay(route, track, points);
}

std::string wrong_way_events(const Replay& run) {
    std::string text;
    for (const nfsu::RaceProgressEvent& event : run.events) {
        if (event.kind == nfsu::RaceEventKind::wrong_way_on ||
            event.kind == nfsu::RaceEventKind::wrong_way_off) {
            text += nfsu::race_event_kind_name(event.kind);
            text += '@';
            text += std::to_string(event.step);
            text += ' ';
        }
    }
    return text;
}

void test_wrong_way_thresholds() {
    // Off: 20023 units is not the wrong way, 20024 is (0x00430F49 `jb`).
    check(wrong_way_events(wrong_way_run({5960, 5960, 5962})) == "wrong-way-on@3 ",
          "20023 stays off and 20024 turns on: " +
              wrong_way_events(wrong_way_run({5960, 5960, 5962})));
    // On: 1820 is added (0x00430EF7..0x00430F21), so 18204 stays on and
    // 18203 turns it off.
    check(wrong_way_events(wrong_way_run({5962, 2889, 2889, 2888})) ==
              "wrong-way-on@1 wrong-way-off@4 ",
          "18204 keeps it on and 18203 turns it off: " +
              wrong_way_events(wrong_way_run({5962, 2889, 2889, 2888})));
    // The 1820 is only added while the flag is on: 18204 from off stays off.
    check(wrong_way_events(wrong_way_run({2889, 2890, 2889})).empty(),
          "18204 from off stays off: " + wrong_way_events(wrong_way_run({2889, 2890, 2889})));
}

// A reset zone answers no (0x00430EDA..0x00430EDC): driving the wrong way
// into one turns the flag off, and leaving it turns it on again.
void test_reset_answers_no() {
    for (const float height : {0.0F, 48.0F}) {
        TrackSpec spec = circle_spec();
        ZoneSpec zone = square_zone(nfsu::kZoneReset, -60, 60, -20, 110);
        spec.zones = {zone};
        Bytes image = track_image(spec);
        // +0x14 of the one zone record: the height 0x00427ED9 tests.
        const std::size_t zone_record = path_record_offset(spec) + 40 + 8 + 16 * 20 + 8;
        put_f32(image, zone_record + 0x14, height);
        const nfsu::RaceRouteTrack track = parse(image);
        check(track.ok() && track.zones.size() == 1 && track.zones[0].height == height,
              "the reset circle parses with its height");
        const nfsu::RaceRoute route = circle_route(track, 2, 0);
        std::vector<std::array<float, 2>> points;
        arc(points, 170.0, 60.0, 0.25);
        const Replay run = replay(route, track, points);
        std::string text;
        for (const nfsu::RaceProgressEvent& event : run.events) {
            if (event.kind == nfsu::RaceEventKind::wrong_way_on ||
                event.kind == nfsu::RaceEventKind::wrong_way_off ||
                event.kind == nfsu::RaceEventKind::reset_zone) {
                text += nfsu::race_event_kind_name(event.kind);
                text += ' ';
            }
        }
        check(text == "wrong-way-on reset-zone wrong-way-off wrong-way-on ",
              "a reset zone of height " + std::to_string(height) +
                  " answers no while the car is in it: " + text);
    }
}

// More than 5000 from the tracked node: +0x87C, and no search
// (0x00426B8C..0x00426BAC), however many steps pass.
void test_off_path() {
    TrackSpec wide = circle_spec();
    wide.radius = 1000.0F;
    for (MarkerSpec& marker : wide.markers) {
        marker.x *= 10.0F;
        marker.y *= 10.0F;
    }
    const nfsu::RaceRouteTrack track = parse(track_image(wide));
    const nfsu::RaceRoute route = circle_route(track, 2, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 0.0, 5.0, 0.05, 1000.0);
    const std::array<float, 2> away = {8000.0F, 0.0F};
    const std::array<float, 2> back = points.back();
    for (int round = 0; round < 2; ++round) {
        for (int k = 0; k < 130; ++k) {
            points.push_back(away);
        }
        for (int k = 0; k < 10; ++k) {
            points.push_back(back);
        }
    }
    const Replay run = replay(route, track, points);
    int off_path = 0;
    int recovered = 0;
    for (const nfsu::RaceProgressEvent& event : run.events) {
        off_path += event.kind == nfsu::RaceEventKind::off_path ? 1 : 0;
        recovered += event.kind == nfsu::RaceEventKind::recovered ? 1 : 0;
    }
    check(off_path == 2, "off-path is reported once each time the car goes beyond 5000: " +
                             std::to_string(off_path));
    check(recovered == 0, "a coordinate beyond 5000 is not searched again: " +
                              std::to_string(recovered));
}

// +0x08 is a signed byte: 129 backward crossings from -1 wrap it to 126,
// and the next forward crossing is a new highest lap, 127. The race is not
// finished: 0x00435A43 finishes on equality with the lap count.
// The other way: a race of no laps driven forward 130 times over the line.
// The counter climbs to 127, the 129th crossing wraps it to -128, which is
// no new highest lap, and the 130th makes it -127: 127 laps complete.
void test_lap_counter_width_forward() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    const nfsu::RaceRoute route = circle_route(track, 0, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 80.0, 80.0 + 130.0 * 360.0, 5.0);
    const Replay run = replay(route, track, points);
    int laps = 0;
    int forward = 0;
    for (const nfsu::RaceProgressEvent& event : run.events) {
        laps += event.kind == nfsu::RaceEventKind::lap_complete ? 1 : 0;
        forward += event.kind == nfsu::RaceEventKind::crossed_forward && event.index == 0 ? 1 : 0;
    }
    check(forward == 130 && laps == 127 && run.progress.lap == -127 &&
              run.progress.max_lap == 127,
          "the lap byte wraps upward: " + std::to_string(forward) + " crossings, " +
              std::to_string(laps) + " laps, counter " + std::to_string(run.progress.lap) +
              ", highest " + std::to_string(run.progress.max_lap));
}

void test_lap_counter_width() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    const nfsu::RaceRoute route = circle_route(track, 2, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 80.0, 80.0 - 129.0 * 360.0, 5.0);
    arc(points, 80.0 - 129.0 * 360.0, 80.0 - 129.0 * 360.0 + 30.0, 5.0);
    const Replay run = replay(route, track, points);
    int backward = 0;
    for (const nfsu::RaceProgressEvent& event : run.events) {
        backward += event.kind == nfsu::RaceEventKind::crossed_backward && event.index == 0 ? 1 : 0;
    }
    check(backward == 129, "129 backward crossings of the line: " + std::to_string(backward));
    check(run.progress.lap == 127 && run.progress.max_lap == 127 && !run.progress.finished,
          "the lap byte wraps to 126 and the next crossing is lap 127, not finished: lap " +
              std::to_string(run.progress.lap) + " highest " +
              std::to_string(run.progress.max_lap));
    check(!run.events.empty() && run.events.back().kind == nfsu::RaceEventKind::lap_complete &&
              run.events.back().lap == 127,
          "the last event is lap 127 complete");
}

// A race of no laps is never finished (0x00435A1D `test eax, eax`).
void test_no_laps() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    const nfsu::RaceRoute route = circle_route(track, 0, 0);
    std::vector<std::array<float, 2>> points;
    arc(points, 80.0, 80.0 + 3.0 * 360.0 + 20.0, 1.0);
    const Replay run = replay(route, track, points);
    check(run.progress.max_lap == 3 && !run.progress.finished, "a race of 0 laps never finishes");
}

// The defaults of a reverse race, the wrap of a default above 100, and the
// first of two markers of one name.
void test_boundary_details() {
    TrackSpec bare = circle_spec();
    bare.markers = {{"Unrelated", 0, 0.0F, 0.0F, 0}};
    const nfsu::RaceRouteTrack bare_track = parse(track_image(bare));
    const nfsu::RaceRoute reverse = circle_route(bare_track, 2, 1);
    check(reverse.boundaries.size() == 3 && reverse.boundaries[0].name == "BackwardStartLine" &&
              reverse.boundaries[0].percent == 5.0F &&
              reverse.boundaries[1].name == "SplitTimeBoundary2" &&
              reverse.boundaries[1].percent == 38.0F &&
              reverse.boundaries[2].name == "SplitTimeBoundary1" &&
              reverse.boundaries[2].percent == 71.0F,
          "reverse defaults: 5, then split 2 at start + 33 and split 1 at start + 66");

    // A start line at 330 degrees, 91.7 percent: the splits' defaults pass
    // 100 and have 100 taken off (0x004347BA..).
    TrackSpec late = circle_spec();
    late.markers = {{"ForwardStartLine", 0, 86.60254F, -50.0F, 0}};
    const nfsu::RaceRouteTrack late_track = parse(track_image(late));
    const nfsu::RaceRoute wrapped = circle_route(late_track, 2, 0);
    const float start = wrapped.boundaries.empty() ? 0.0F : wrapped.boundaries[0].percent;
    check(wrapped.boundaries.size() == 3 && start > 91.0F &&
              wrapped.boundaries[1].percent == (start + 33.0F) - 100.0F &&
              wrapped.boundaries[2].percent == (start + 66.0F) - 100.0F,
          "defaults past 100 wrap below it");

    // Two ForwardStartLine markers: GetMarker(hash, 0) takes the first.
    TrackSpec twice = circle_spec();
    twice.markers.push_back({"ForwardStartLine", 0, -100.0F, 0.0F, 0});
    const nfsu::RaceRouteTrack twice_track = parse(track_image(twice));
    const nfsu::RaceRoute first = circle_route(twice_track, 2, 0);
    check(!first.boundaries.empty() && first.boundaries[0].marker == 1 &&
              std::fabs(first.boundaries[0].percent - 25.0F) < 0.3F,
          "the first marker of a name is the one projected");
}

// Three control points are enough, two are not.
void test_count_bound() {
    TrackSpec three = circle_spec();
    three.nodes = 3;
    three.length = 250.0F;
    check(parse(track_image(three)).ok(), "a path of three control points parses");
    TrackSpec two = circle_spec();
    two.nodes = 2;
    two.length = 250.0F;
    const Bytes image = track_image(two);
    const nfsu::RaceRouteTrack track = parse(image);
    check(!track.ok() && has_issue(track, path_record_offset(two) + 6, "at least 3"),
          "a path of two control points is refused");
}

// On a closed path, node 0's neighbour before it is the last node
// (0x0044395F..0x00443977).
void test_closed_neighbour() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    nfsu::TrackPathSpline spline;
    std::string error;
    check(nfsu::build_track_path_spline(track, false, spline, error), "the circle builds");
    nfsu::TrackPathCoordinate coordinate;
    const float at_node0[2] = {100.0F, 0.0F};
    nfsu::update_track_path_coordinate(spline, track, coordinate, at_node0);
    check(coordinate.node == 0, "the coordinate starts on node 0");
    const float at_node15[2] = {92.387953F, -38.268343F};
    nfsu::update_track_path_coordinate(spline, track, coordinate, at_node15);
    check(coordinate.node == 15, "node 0's neighbour before it is node 15: " +
                                     std::to_string(coordinate.node));
}

// 0x005AC449's own order of additions, pinned on the circle: w, y, x, z for
// the running length. The first round's order (w, y, z, x everywhere)
// measures 628.12054 (0x441D07B7); the retail order 628.1205 (0x441D07B6).
void test_summation_order() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    nfsu::TrackPathSpline spline;
    std::string error;
    check(nfsu::build_track_path_spline(track, false, spline, error), "the circle builds");
    check(float_bits(spline.spline_length) == 0x441D07B6U,
          "the circle's loop measures 0x441D07B6 in the retail order: " +
              std::to_string(float_bits(spline.spline_length)));
    check(float_bits(spline.points[4 * 3 + 2]) == 0x431D07B4U,
          "node 4's running length in the retail order");
}

// 0x0059D6DA..0x0059D6F9 halve the step whenever it changes direction. Near
// the circle's centre the search swings; the halving is what lands it on
// this parameter in nine evaluations (without it: seven, and 0.51778).
void test_secant_halving() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    nfsu::TrackPathSpline spline;
    std::string error;
    check(nfsu::build_track_path_spline(track, false, spline, error), "the circle builds");
    nfsu::TrackPathCoordinate coordinate;
    const float query[2] = {-9.75F, -0.75F};
    nfsu::update_track_path_coordinate(spline, track, coordinate, query);
    check(coordinate.iterations == 9 && std::fabs(coordinate.fraction - 0.517994761F) < 1e-6F,
          "the swinging search near the centre: " + std::to_string(coordinate.iterations) +
              " evaluations, fraction " + std::to_string(coordinate.fraction));
}

// 0x0059D9F0 evaluates the first point of each segment through 0x0059D350
// at (float)i, so through the wrap of 0x0059CE10, which with 41 control
// points gives 1.9999999 for i = 2 and ten other near misses. A 41-gon made
// by a float rotation recurrence (exact on every platform) has node 2's
// running length 0x41F531E4 that way; evaluated directly on segment i at
// u = 0 it would be 0x41F531E2. The model gives both.
void test_arc_start_through_wrap() {
    const auto bits_float = [](std::uint32_t bits) {
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };
    const float c = bits_float(0x3F7CFFF2U);  // cos(2 pi / 41) in float
    const float s = bits_float(0x3E1C5018U);  // sin(2 pi / 41) in float
    TrackSpec spec = circle_spec();
    spec.nodes = 41;
    float x = 100.0F;
    float y = 0.0F;
    for (int k = 0; k < 41; ++k) {
        spec.points.push_back({x, y});
        const float nx = x * c - y * s;
        const float ny = x * s + y * c;
        x = nx;
        y = ny;
    }
    const nfsu::RaceRouteTrack track = parse(track_image(spec));
    nfsu::TrackPathSpline spline;
    std::string error;
    check(track.ok() && nfsu::build_track_path_spline(track, false, spline, error),
          "the 41-gon builds: " + error);
    if (spline.count != 41) {
        return;
    }
    check(float_bits(spline.points[2 * 3 + 2]) == 0x41F531E4U,
          "node 2's running length through the wrap: " +
              std::to_string(float_bits(spline.points[2 * 3 + 2])));
    check(float_bits(spline.spline_length) == 0x441D13FAU, "the 41-gon's loop length");
}

// The derivative's half of 0x005AC449's order: the weight of control point
// 0 summed w, y, z, x. Its derivative basis row is (-0.5, 2, -1.5, 0), so
// the order moves the tangent; at t = 0.07 on the circle the retail order
// gives (0xBF91B380, 0x421A888A), the w, y, x, z order (0xBF91B3A0,
// 0x421A888B). The model gives both.
void test_derivative_order() {
    const nfsu::RaceRouteTrack track = parse(track_image(circle_spec()));
    nfsu::TrackPathSpline spline;
    std::string error;
    check(nfsu::build_track_path_spline(track, false, spline, error), "the circle builds");
    float slope[3];
    nfsu::evaluate_track_path(spline, 0.07F, 1, slope);
    check(float_bits(slope[0]) == 0xBF91B380U && float_bits(slope[1]) == 0x421A888AU &&
              float_bits(slope[2]) == 0x421D07C4U,
          "the derivative at 0.07 in the retail order: " + std::to_string(float_bits(slope[0])) +
              " " + std::to_string(float_bits(slope[1])));
}

// Data the parser once let through that made the replay loop forever.
void test_absurd_data() {
    const TrackSpec spec = circle_spec();
    const std::size_t record = path_record_offset(spec);
    const std::size_t nodes = record + 40 + 8;
    {
        for (const float distant : {1.0e30F, -1.0e30F, 3.0e38F}) {
            Bytes image = track_image(spec);
            put_f32(image, nodes + 7 * 20, distant);
            const nfsu::RaceRouteTrack track = parse(image);
            check(!track.ok() && has_issue(track, nodes + 7 * 20, "further than"),
                  "a node at " + std::to_string(distant) + " is refused");
        }
        Bytes image = track_image(spec);
        put_f32(image, kMarkerRecordOffset + 48 + 16, 2.0e6F);
        const nfsu::RaceRouteTrack track = parse(image);
        check(!track.ok() && has_issue(track, kMarkerRecordOffset + 48 + 16, "further than"),
              "a marker at 2e6 is refused");
    }
    struct Field {
        std::size_t at;
        float value;
        const char* what;
    };
    const Field fields[] = {
        {record + 12, 1.0e-30F, "a length of 1e-30"},
        {record + 16, 1.0e12F, "a scale of 1e12"},
        {record + 16, 3.0e38F, "a scale of 3e38"},
        {record + 8, 50.0F, "an offset of 50"},
        {record + 12, 700.0F, "a length 11 percent long"},
    };
    {
        // An offset of 5 percent with a scale of 0.95: the loop still ends
        // at 100 percent, so only the start's bound refuses it.
        Bytes image = track_image(spec);
        put_f32(image, record + 8, 31.40602F);
        put_f32(image, record + 16, 0.95F);
        const nfsu::RaceRouteTrack track = parse(image);
        nfsu::RaceRoute route;
        std::string error;
        check(track.ok() && !nfsu::build_race_route(track, 2, 0, 0, route, error) &&
                  error.find("measure its own loop") != std::string::npos &&
                  error.find("to 100") != std::string::npos,
              "a loop that starts at 5 percent and ends at 100 is refused: " + error);
    }
    for (const Field& field : fields) {
        Bytes image = track_image(spec);
        put_f32(image, field.at, field.value);
        const nfsu::RaceRouteTrack track = parse(image);
        check(track.ok(), std::string(field.what) + " parses");
        nfsu::RaceRoute route;
        std::string error;
        check(!nfsu::build_race_route(track, 2, 0, 0, route, error) &&
                  error.find("measure its own loop") != std::string::npos,
              std::string(field.what) + " is refused by the route: " + error);
    }

    // A route whose percents are absurd anyway - built by hand - still
    // replays in bounded time: every wrap here is at most two turns.
    const nfsu::RaceRouteTrack track = parse(track_image(spec));
    nfsu::RaceRoute route = circle_route(track, 2, 0);
    route.boundaries[1].percent = 1.0e20F;
    route.boundaries[2].percent = std::numeric_limits<float>::infinity();
    std::vector<std::array<float, 2>> points;
    arc(points, 80.0, 100.0, 1.0);
    const Replay run = replay(route, track, points);
    check(run.progress.step == points.size() - 1, "a route of absurd percents replays");
    // An absurd percent reads as a whole lap ahead, never as the nearest.
    check(run.progress.next_boundary == 0,
          "an absurd boundary is never the next: " + std::to_string(run.progress.next_boundary));
    nfsu::RaceDrive drive;
    std::string error;
    route.boundaries[0].percent = -1.0e20F;
    check(nfsu::make_race_drive(route, track, false, -1, -1, 0.5F, drive, error),
          "a drive over absurd percents is built: " + error);
}

// --write-fixture <trackb> [variant]: the circle with a reset zone at its
// centre, for tests/race_route_guards.cmake, or one of three variants the
// first round's parser let through and its replay never returned from:
// tiny-length (the path length 1e-30), huge-scale (the scale 3e38) and
// far-node (node 7 at x = 1e30). Never replaces a file.
bool write_fixture(const fs::path& path, const std::string& variant) {
    std::error_code code;
    if (fs::exists(path, code)) {
        std::cerr << "error: --write-fixture " << path.u8string()
                  << " already exists; this command never replaces a file\n";
        return false;
    }
    TrackSpec spec = circle_spec();
    spec.zones = {square_zone(nfsu::kZoneReset, -10, -10, 10, 10)};
    Bytes image = track_image(spec);
    const std::size_t record = path_record_offset(spec);
    if (variant == "tiny-length") {
        put_f32(image, record + 12, 1.0e-30F);
    } else if (variant == "huge-scale") {
        put_f32(image, record + 16, 3.0e38F);
    } else if (variant == "far-node") {
        put_f32(image, record + 40 + 8 + 7 * 20, 1.0e30F);
    } else if (variant != "circle") {
        std::cerr << "error: unknown fixture variant " << variant << '\n';
        return false;
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream.write(reinterpret_cast<const char*>(image.data()),
                      static_cast<std::streamsize>(image.size()))) {
        std::cerr << "failed to write the fixture\n";
        return false;
    }
    return true;
}

void test_load_from_file() {
    const fs::path path = fs::temp_directory_path() / (unique_fixture("trackb") + ".lzc");
    std::error_code code;
    fs::remove(path, code);
    check(write_fixture(path, "circle"), "the fixture is written");
    std::vector<unsigned char> image;
    const nfsu::RaceRouteTrack track = nfsu::load_race_route_track(path, image);
    check(track.ok() && track.stored_size == image.size() && !track.jdlz,
          "the fixture loads from a file");
    check(!write_fixture(path, "circle"),
          "the fixture writer never replaces a file");
    fs::remove(path, code);
    const nfsu::RaceRouteTrack missing = nfsu::load_race_route_track(path, image);
    check(!missing.ok() && !missing.issues.empty() &&
              missing.issues.front().message.find("cannot size the file") == 0,
          "a missing file is refused");
}

} // namespace

int main(int argc, char** argv) {
    std::string argument_error;
    if (!nfsu::check_arguments(argc, argv, 1, argument_error)) {
        std::cerr << "error: " << argument_error << '\n';
        return 1;
    }
    if ((argc == 3 || argc == 4) && std::string(argv[1]) == "--write-fixture") {
        return write_fixture(nfsu::argument_path(argv[2]), argc == 4 ? argv[3] : "circle") ? 0 : 1;
    }
    if (argc != 1) {
        std::cerr << "usage: nfsu_race_route_tests [--write-fixture <trackb> "
                     "[circle|tiny-length|huge-scale|far-node]]\n";
        return 1;
    }
    test_parse();
    test_parse_refusals();
    test_spline();
    test_coordinate();
    test_boundaries();
    test_laps();
    test_start_finish_crossing();
    test_graze();
    test_window_edge();
    test_missed_checkpoint();
    test_reverse_crossing_and_wrong_way();
    test_wrong_way_zones();
    test_recovery();
    test_track_angle();
    test_wrong_way_thresholds();
    test_reset_answers_no();
    test_off_path();
    test_lap_counter_width();
    test_lap_counter_width_forward();
    test_arc_start_through_wrap();
    test_derivative_order();
    test_no_laps();
    test_boundary_details();
    test_count_bound();
    test_closed_neighbour();
    test_summation_order();
    test_absurd_data();
    test_secant_halving();
    test_load_from_file();
    if (g_failures != 0) {
        std::cerr << g_failures << " race route test(s) failed\n";
        return 1;
    }
    std::cout << "race route tests passed\n";
    return 0;
}
