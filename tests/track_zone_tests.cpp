#include "nfsu/track_zones.h"

#include "nfsu/texture.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;

// The format's own numbers, as literals. A fixture built out of the
// header's constants would move with them, so a mutation of a constant
// would change the image and the expectation together and no assertion
// could see it. test_constants is where the two are tied, deliberately and
// in one place.
constexpr std::size_t kPolygonRecord = 140;
constexpr std::size_t kZoneRecord = 148;
constexpr std::size_t kTrackListRecord = 272;
constexpr std::size_t kExtraListRecord = 32;
constexpr std::uint32_t kContainer = 0x80034150U;
constexpr std::uint32_t kPolygonChunk = 0x00034151U;
constexpr std::uint32_t kZoneChunk = 0x00034152U;
constexpr std::uint32_t kTrackListChunk = 0x00034153U;
constexpr std::uint32_t kExtraListChunk = 0x00034154U;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

void check_eq_u64(std::uint64_t got, std::uint64_t want, const std::string& what) {
    check(got == want,
          what + ": got " + std::to_string(got) + ", want " + std::to_string(want));
}

std::string join(const std::vector<std::uint32_t>& values) {
    std::string text;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            text += ", ";
        }
        text += std::to_string(values[i]);
    }
    return "[" + text + "]";
}

void check_list(
    const std::vector<std::uint32_t>& got,
    const std::vector<std::uint32_t>& want,
    const std::string& what) {
    check(got == want, what + ": got " + join(got) + ", want " + join(want));
}

bool has_text(
    const std::vector<nfsu::TrackIssue>& issues,
    const std::string& text) {
    for (const nfsu::TrackIssue& issue : issues) {
        if (issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Every refusal below names the rule it broke rather than merely failing.
// A different rule firing first would leave the rule under test
// unexercised while the assertion still passed.
void check_refused(
    const nfsu::TrackZoneReport& report,
    const std::string& text,
    const std::string& what) {
    check(!report.valid, what + ": the image must not be valid");
    check(has_text(report.issues, text),
          what + ": expected an issue mentioning \"" + text + "\"" +
              (report.issues.empty()
                   ? std::string(", got none")
                   : ", got \"" + report.issues[0].message + "\""));
}

// ---------------------------------------------------------------------
// Byte-level fixtures
// ---------------------------------------------------------------------

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_i16(Bytes& bytes, std::int16_t value) {
    append_u16(bytes, static_cast<std::uint16_t>(value));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8U * i)) & 0xFFU));
    }
}

void append_f32(Bytes& bytes, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(bytes, bits);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

Bytes chunk(std::uint32_t id, const Bytes& payload) {
    Bytes out;
    append_u32(out, id);
    append_u32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

void append(Bytes& into, const Bytes& more) {
    into.insert(into.end(), more.begin(), more.end());
}

struct PolygonSpec {
    std::int16_t key = 0;
    std::int16_t vertex_count = -1; // -1 means "as many as vertices holds"
    std::vector<std::pair<float, float>> vertices;
    std::uint32_t link0 = 11;
    std::uint32_t link1 = 11;
    // Slots at and past the count, which retail leaves zero.
    float tail_value = 0.0F;
};

Bytes polygon_record(const PolygonSpec& spec) {
    Bytes out;
    append_u32(out, spec.link0);
    append_u32(out, spec.link1);
    append_i16(out, spec.key);
    const std::int16_t count =
        spec.vertex_count >= 0
            ? spec.vertex_count
            : static_cast<std::int16_t>(spec.vertices.size());
    append_i16(out, count);
    for (std::size_t v = 0; v < 16; ++v) {
        if (v < spec.vertices.size()) {
            append_f32(out, spec.vertices[v].first);
            append_f32(out, spec.vertices[v].second);
        } else {
            append_f32(out, spec.tail_value);
            append_f32(out, spec.tail_value);
        }
    }
    return out;
}

struct ZoneSpec {
    std::int16_t key = 0;
    std::int32_t section_count = -1; // -1 means "as many as sections holds"
    std::vector<std::int16_t> sections;
    std::uint32_t link0 = 11;
    std::uint32_t link1 = 11;
    std::int16_t unknown = 0;
    std::uint32_t polygon_link = 0;
    std::int16_t tail_value = 0;
};

Bytes zone_record(const ZoneSpec& spec) {
    Bytes out;
    append_u32(out, spec.link0);
    append_u32(out, spec.link1);
    append_i16(out, spec.key);
    append_i16(out, spec.unknown);
    append_u32(out, spec.polygon_link);
    append_u32(out, static_cast<std::uint32_t>(
                        spec.section_count >= 0
                            ? spec.section_count
                            : static_cast<std::int32_t>(spec.sections.size())));
    for (std::size_t s = 0; s < 64; ++s) {
        append_i16(out, s < spec.sections.size() ? spec.sections[s]
                                                 : spec.tail_value);
    }
    return out;
}

Bytes section_list_record(
    std::size_t record_size,
    std::uint32_t key,
    std::int32_t count,
    const std::vector<std::int16_t>& sections) {
    Bytes out;
    append_u32(out, 11);
    append_u32(out, 11);
    append_u32(out, key);
    append_u32(out, static_cast<std::uint32_t>(count));
    const std::size_t capacity = (record_size - 16) / 2;
    for (std::size_t s = 0; s < capacity; ++s) {
        append_i16(out, s < sections.size() ? sections[s] : 0);
    }
    return out;
}

// A square from (0, 0) to (100, 100), in the winding all 2110 retail
// polygons use: their signed area is positive on every one.
PolygonSpec square(std::int16_t key, float x0, float y0, float x1, float y1) {
    PolygonSpec spec;
    spec.key = key;
    spec.vertices = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    return spec;
}

struct ImageSpec {
    std::vector<PolygonSpec> polygons;
    std::vector<ZoneSpec> zones;
    std::vector<std::vector<std::int16_t>> track_lists;
    std::vector<std::vector<std::int16_t>> extra_lists;
    bool omit_polygon_chunk = false;
    bool omit_zone_chunk = false;
    bool write_polygon_chunk_empty = false;
};

Bytes build_image(const ImageSpec& spec) {
    Bytes children;
    if (!spec.omit_polygon_chunk) {
        Bytes payload;
        if (!spec.write_polygon_chunk_empty) {
            for (const PolygonSpec& polygon : spec.polygons) {
                append(payload, polygon_record(polygon));
            }
        }
        append(children, chunk(kPolygonChunk, payload));
    }
    if (!spec.omit_zone_chunk) {
        Bytes payload;
        for (const ZoneSpec& zone : spec.zones) {
            append(payload, zone_record(zone));
        }
        append(children, chunk(kZoneChunk, payload));
    }
    if (!spec.track_lists.empty()) {
        Bytes payload;
        for (const std::vector<std::int16_t>& list : spec.track_lists) {
            append(payload,
                   section_list_record(kTrackListRecord, 1301,
                                       static_cast<std::int32_t>(list.size()),
                                       list));
        }
        append(children, chunk(kTrackListChunk, payload));
    }
    if (!spec.extra_lists.empty()) {
        Bytes payload;
        for (const std::vector<std::int16_t>& list : spec.extra_lists) {
            append(payload,
                   section_list_record(kExtraListRecord, 1301,
                                       static_cast<std::int32_t>(list.size()),
                                       list));
        }
        append(children, chunk(kExtraListChunk, payload));
    }
    return chunk(kContainer, children);
}

nfsu::TrackZoneReport parse(const Bytes& image) {
    return nfsu::parse_track_zones(image.data(), image.size(), 0);
}

// Two zones side by side, keyed 101 and 103, with a section list each. The
// section numbers follow the retail shape: every zone list holds its own
// key and the key plus ten.
ImageSpec two_zone_spec() {
    ImageSpec spec;
    spec.polygons.push_back(square(101, 0.0F, 0.0F, 100.0F, 100.0F));
    spec.polygons.push_back(square(103, 100.0F, 0.0F, 200.0F, 100.0F));
    ZoneSpec left;
    left.key = 101;
    left.sections = {100, 101, 110, 111};
    ZoneSpec right;
    right.key = 103;
    right.sections = {102, 103, 112, 113};
    spec.zones = {left, right};
    return spec;
}

// ---------------------------------------------------------------------
// The constants
// ---------------------------------------------------------------------

void test_constants() {
    check_eq_u64(nfsu::kTrackZoneContainerId, kContainer, "container id");
    check_eq_u64(nfsu::kTrackZonePolygonId, kPolygonChunk, "polygon chunk id");
    check_eq_u64(nfsu::kTrackZoneId, kZoneChunk, "zone chunk id");
    check_eq_u64(nfsu::kTrackZoneTrackListId, kTrackListChunk,
                 "track list chunk id");
    check_eq_u64(nfsu::kTrackZoneExtraListId, kExtraListChunk,
                 "extra list chunk id");
    check_eq_u64(nfsu::kTrackZonePolygonRecordSize, kPolygonRecord,
                 "polygon record size");
    check_eq_u64(nfsu::kTrackZoneRecordSize, kZoneRecord, "zone record size");
    check_eq_u64(nfsu::kTrackZoneTrackListRecordSize, kTrackListRecord,
                 "track list record size");
    check_eq_u64(nfsu::kTrackZoneExtraListRecordSize, kExtraListRecord,
                 "extra list record size");
    // The capacities are what the record sizes leave room for. Stating
    // both and comparing them is what makes a changed stride visible.
    check_eq_u64(nfsu::kTrackZoneMaxVertices,
                 (kPolygonRecord - nfsu::kTrackZonePolygonVertexOffset) /
                     nfsu::kTrackZoneVertexSize,
                 "vertex capacity");
    check_eq_u64(nfsu::kTrackZoneMaxVertices, 16, "vertex capacity literal");
    check_eq_u64(nfsu::kTrackZoneMaxSections,
                 (kZoneRecord - nfsu::kTrackZoneSectionOffset) / 2,
                 "section capacity");
    check_eq_u64(nfsu::kTrackZoneMaxSections, 64, "section capacity literal");
    check_eq_u64(nfsu::kTrackZoneTrackListCapacity,
                 (kTrackListRecord - nfsu::kTrackZoneListSectionOffset) / 2,
                 "track list capacity");
    check_eq_u64(nfsu::kTrackZoneExtraListCapacity,
                 (kExtraListRecord - nfsu::kTrackZoneListSectionOffset) / 2,
                 "extra list capacity");
    check_eq_u64(nfsu::kTrackZoneDrawBufferLimit, 128, "draw buffer limit");
    check_eq_u64(nfsu::kTrackZoneStoredLinkValue, 11, "stored link value");
    check_eq_u64(nfsu::kTrackZonePolygonKeyOffset, 8, "polygon key offset");
    check_eq_u64(nfsu::kTrackZonePolygonVertexCountOffset, 10,
                 "polygon vertex count offset");
    check_eq_u64(nfsu::kTrackZonePolygonVertexOffset, 12,
                 "polygon vertex offset");
    check_eq_u64(nfsu::kTrackZoneKeyOffset, 8, "zone key offset");
    check_eq_u64(nfsu::kTrackZoneUnknownOffset, 10, "zone unknown offset");
    check_eq_u64(nfsu::kTrackZonePolygonLinkOffset, 12, "zone polygon link");
    check_eq_u64(nfsu::kTrackZoneSectionCountOffset, 16, "zone count offset");
    check_eq_u64(nfsu::kTrackZoneSectionOffset, 20, "zone section offset");
    check_eq_u64(nfsu::kTrackZoneListKeyOffset, 8, "list key offset");
    check_eq_u64(nfsu::kTrackZoneListCountOffset, 12, "list count offset");
    check_eq_u64(nfsu::kTrackZoneListSectionOffset, 16, "list section offset");
}

// ---------------------------------------------------------------------
// The polygon test: 0x005679B0
// ---------------------------------------------------------------------

bool hit(const std::vector<std::pair<float, float>>& poly, float x, float y) {
    std::vector<float> flat;
    for (const std::pair<float, float>& v : poly) {
        flat.push_back(v.first);
        flat.push_back(v.second);
    }
    const float point[2] = {x, y};
    return nfsu::point_in_track_zone_polygon(point, flat.data(), poly.size());
}

void check_hit(
    const std::vector<std::pair<float, float>>& poly,
    float x,
    float y,
    bool want,
    const std::string& what) {
    const bool got = hit(poly, x, y);
    check(got == want, what + ": (" + std::to_string(x) + ", " +
                           std::to_string(y) + ") got " +
                           (got ? "inside" : "outside") + ", want " +
                           (want ? "inside" : "outside"));
}

void test_polygon_basic() {
    const std::vector<std::pair<float, float>> unit = {
        {0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
    check_hit(unit, 5.0F, 5.0F, true, "square centre");
    check_hit(unit, -1.0F, 5.0F, false, "square, left of the left edge");
    check_hit(unit, 11.0F, 5.0F, false, "square, right of the right edge");
    check_hit(unit, 5.0F, -1.0F, false, "square, below");
    check_hit(unit, 5.0F, 11.0F, false, "square, above");
    // The parity is what bounds the answer on the side the scan runs
    // towards: a point left of the square crosses both vertical edges and
    // toggles twice, so it is outside however far left it goes. A count
    // that stopped at the first crossing, or that dropped the parity for a
    // single flag, would report every one of these inside.
    check_hit(unit, -1000.0F, 5.0F, false, "square, far to the left");
}

void test_polygon_boundaries() {
    const std::vector<std::pair<float, float>> unit = {
        {0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
    // The two vertical edges cross at slot0 = 0 and slot0 = 10. The toggle
    // is strict, so a point resting exactly on the left edge sees one
    // crossing (the right edge, 10 > 0) and a point resting exactly on the
    // right edge sees none (neither 0 > 10 nor 10 > 10).
    check_hit(unit, 0.0F, 5.0F, true, "exactly on the left edge");
    check_hit(unit, 10.0F, 5.0F, false, "exactly on the right edge");

    // The vertical bands are half open: the lower endpoint is in, the
    // upper endpoint is out. A point level with slot1 = 0 is inside and one
    // level with slot1 = 10 is outside.
    check_hit(unit, 5.0F, 0.0F, true, "level with the bottom edge");
    check_hit(unit, 5.0F, 10.0F, false, "level with the top edge");
    check_hit(unit, 0.0F, 0.0F, true, "exactly on the bottom-left vertex");
    check_hit(unit, 10.0F, 10.0F, false, "exactly on the top-right vertex");

    // A horizontal edge is never taken at all: both its endpoints share a
    // slot1 and neither half-open band can hold the point. That is what
    // keeps the division from dividing by zero, and it is why a point level
    // with the bottom edge is decided by the two vertical edges alone.
    const std::vector<std::pair<float, float>> flat_top = {
        {0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 4.0F}, {0.0F, 4.0F}};
    check_hit(flat_top, 5.0F, 4.0F, false, "level with a flat top");
    check_hit(flat_top, 5.0F, 0.0F, true, "level with a flat bottom");

    // The two bands must be attacked separately, and a square cannot do it.
    // At a scanline through a vertex, the edge on one side is decided by
    // the rising clause and the edge on the other by the falling one, so a
    // shape symmetric about that scanline lets one clause cover for the
    // other. These two do not.
    //
    // A diamond, sampled at the level of its left vertex and to the LEFT of
    // everything. Both edges at that vertex cross the scanline, so the
    // correct answer is two toggles and outside. Opening the rising band's
    // closed end drops one of them and the point becomes inside.
    const std::vector<std::pair<float, float>> diamond = {
        {0.0F, 0.0F}, {10.0F, -5.0F}, {20.0F, 0.0F}, {10.0F, 5.0F}};
    check_hit(diamond, 10.0F, 0.0F, true, "diamond centre");
    check_hit(diamond, -5.0F, 0.0F, false,
              "diamond, left of a vertex-level scanline");
    check_hit(diamond, 25.0F, 0.0F, false,
              "diamond, right of a vertex-level scanline");

    // A triangle sampled exactly at its apex, where both neighbours lie
    // below. The correct answer takes neither edge, so the apex is outside.
    // Closing the rising band's open end takes one of them and the point
    // becomes inside.
    const std::vector<std::pair<float, float>> peak = {
        {0.0F, 0.0F}, {20.0F, 0.0F}, {10.0F, 10.0F}};
    check_hit(peak, 0.0F, 10.0F, false, "left of a local maximum vertex");
    check_hit(peak, 10.0F, 10.0F, false, "exactly on a local maximum vertex");
    check_hit(peak, 10.0F, 0.0F, true, "inside the triangle at its base");
    check_hit(peak, 0.0F, 5.0F, false, "left of the triangle at half height");
}

void test_polygon_winding_and_shape() {
    const std::vector<std::pair<float, float>> ccw = {
        {0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
    const std::vector<std::pair<float, float>> cw = {
        {0.0F, 10.0F}, {10.0F, 10.0F}, {10.0F, 0.0F}, {0.0F, 0.0F}};
    check_hit(cw, 5.0F, 5.0F, true, "reversed winding, centre");
    check_hit(cw, 5.0F, 11.0F, false, "reversed winding, above");
    check(hit(ccw, 3.0F, 7.0F) == hit(cw, 3.0F, 7.0F),
          "winding must not change a crossing count");

    // An L shape, so that the notch is a point the convex hull holds and
    // the polygon does not. A test that only ever saw convex squares could
    // not tell a crossing count from a half-plane intersection.
    const std::vector<std::pair<float, float>> ell = {
        {0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 4.0F},
        {4.0F, 4.0F}, {4.0F, 10.0F}, {0.0F, 10.0F}};
    check_hit(ell, 2.0F, 2.0F, true, "L, in the corner");
    check_hit(ell, 8.0F, 2.0F, true, "L, in the arm");
    check_hit(ell, 2.0F, 8.0F, true, "L, in the leg");
    check_hit(ell, 8.0F, 8.0F, false, "L, in the notch");
}

void test_polygon_counts() {
    // The retail loop is unrolled four ways and the remainder runs through
    // a tail loop, so the same shape has to answer the same at counts on
    // either side of every multiple of four. A triangle never reaches the
    // unrolled body at all.
    const std::vector<std::pair<float, float>> triangle = {
        {0.0F, 0.0F}, {12.0F, 0.0F}, {0.0F, 12.0F}};
    check_hit(triangle, 2.0F, 2.0F, true, "triangle, inside");
    check_hit(triangle, 9.0F, 9.0F, false, "triangle, past the hypotenuse");
    check_hit(triangle, 5.0F, 13.0F, false, "triangle, above");

    // A regular polygon on a circle of radius 10, at every count from 3 to
    // 16. A point at the centre is inside every one and a point well
    // outside the circle is outside every one; at a radius of 12 on the
    // +slot0 side the point is past every edge.
    for (std::size_t n = 3; n <= 16; ++n) {
        std::vector<std::pair<float, float>> poly;
        for (std::size_t i = 0; i < n; ++i) {
            const double angle =
                6.283185307179586 * static_cast<double>(i) /
                static_cast<double>(n);
            // An offset keeps every vertex off the axes, so no vertex sits
            // exactly on the sampled scanline and the half-open rule is not
            // what the count is measuring here.
            poly.emplace_back(
                static_cast<float>(10.0 * std::cos(angle + 0.1)),
                static_cast<float>(10.0 * std::sin(angle + 0.1)));
        }
        check_hit(poly, 0.0F, 0.0F, true,
                  "regular " + std::to_string(n) + "-gon, centre");
        check_hit(poly, 12.0F, 0.0F, false,
                  "regular " + std::to_string(n) + "-gon, outside on +slot0");
    }

    // Degenerate counts. A count of zero reads no vertex and reports
    // outside; a single vertex and a pair of vertices have no edge that can
    // bound anything, so both do too.
    const std::vector<std::pair<float, float>> one = {{0.0F, 0.0F}};
    const std::vector<std::pair<float, float>> two = {{0.0F, 0.0F},
                                                      {10.0F, 10.0F}};
    check(!nfsu::point_in_track_zone_polygon(nullptr, nullptr, 0),
          "a null point is outside");
    const float origin[2] = {0.0F, 0.0F};
    check(!nfsu::point_in_track_zone_polygon(origin, nullptr, 4),
          "a null vertex array is outside");
    std::vector<float> flat = {0.0F, 0.0F};
    check(!nfsu::point_in_track_zone_polygon(origin, flat.data(), 0),
          "a count of zero is outside");
    check_hit(one, 0.0F, 0.0F, false, "one vertex");
    check_hit(two, 5.0F, 5.0F, false, "two vertices: one crossing each way");
    check_hit(two, -5.0F, 5.0F, false, "two vertices, from the left");
}

void test_polygon_uses_only_two_floats() {
    // The retail caller hands 0x005679B0 a three-float world point and the
    // function reads +0 and +4. A test that passed exactly two floats could
    // not tell that from a function that read three.
    const float point[3] = {5.0F, 5.0F, 999999.0F};
    const float vertices[] = {0.0F, 0.0F, 10.0F, 0.0F, 10.0F, 10.0F, 0.0F, 10.0F};
    check(nfsu::point_in_track_zone_polygon(point, vertices, 4),
          "a third float must not reach the test");
}

// ---------------------------------------------------------------------
// The parser
// ---------------------------------------------------------------------

void test_parse_valid() {
    ImageSpec spec = two_zone_spec();
    spec.track_lists.push_back({704, 705, 706});
    spec.extra_lists.push_back({2601});
    const Bytes image = build_image(spec);
    const nfsu::TrackZoneReport report = parse(image);

    check(report.valid, "the two-zone image must parse");
    check(report.ok(), "the two-zone image must raise no issue");
    check_eq_u64(report.cost.polygons, 2, "polygon count");
    check_eq_u64(report.cost.zones, 2, "zone count");
    check_eq_u64(report.cost.container_children, 4, "container children");
    check_eq_u64(report.cost.polygon_vertices, 8, "counted vertices");
    check_eq_u64(report.cost.zone_sections, 8, "counted zone sections");
    check_eq_u64(report.section_lists.size(), 2, "section list records");

    check(report.polygons[0].key == 101, "polygon 0 key");
    check(report.polygons[0].vertex_count == 4, "polygon 0 vertex count");
    check(report.polygons[0].vertices[0][0] == 0.0F &&
              report.polygons[0].vertices[0][1] == 0.0F,
          "polygon 0 vertex 0");
    check(report.polygons[0].vertices[2][0] == 100.0F &&
              report.polygons[0].vertices[2][1] == 100.0F,
          "polygon 0 vertex 2");
    check(report.polygons[0].stored_link[0] == 11 &&
              report.polygons[0].stored_link[1] == 11,
          "polygon 0 keeps its stored link words");
    check(report.polygons[1].offset == report.polygons[0].offset + kPolygonRecord,
          "polygon records are 140 bytes apart");

    check(report.zones[0].key == 101, "zone 0 key");
    check(report.zones[0].section_count == 4, "zone 0 section count");
    check(report.zones[0].sections[0] == 100 && report.zones[0].sections[3] == 111,
          "zone 0 section numbers");
    check(report.zones[0].polygon_index == 0, "zone 0 resolves to polygon 0");
    check(report.zones[1].polygon_index == 1, "zone 1 resolves to polygon 1");
    check(report.zones[1].offset == report.zones[0].offset + kZoneRecord,
          "zone records are 148 bytes apart");

    check(report.section_lists[0].chunk_id == kTrackListChunk,
          "the 0x00034153 record is reported first");
    check(report.section_lists[0].key == 1301, "track list key");
    check(report.section_lists[0].count == 3, "track list count");
    check(report.section_lists[0].sections.size() == 128,
          "the track list keeps all 128 slots");
    check(report.section_lists[0].sections[0] == 704, "track list entry 0");
    check(report.section_lists[1].chunk_id == kExtraListChunk,
          "the 0x00034154 record is reported second");
    check(report.section_lists[1].sections.size() == 8,
          "the extra list keeps all 8 slots");
    check(report.section_lists[1].sections[0] == 2601, "extra list entry 0");

    check(report.cost.vertex_min[0] == 0.0F && report.cost.vertex_max[0] == 200.0F,
          "vertex bounds on slot 0");
    check(report.cost.vertex_min[1] == 0.0F && report.cost.vertex_max[1] == 100.0F,
          "vertex bounds on slot 1");
}

void test_parse_keeps_raw_fields() {
    // Nothing the file holds in a field the runtime overwrites, or in a
    // slot past a count, may be dropped: a decoder that zeroed them would
    // be reporting what retail does rather than what the file says.
    ImageSpec spec;
    PolygonSpec polygon = square(101, 0.0F, 0.0F, 10.0F, 10.0F);
    polygon.link0 = 0xDEADBEEFU;
    polygon.link1 = 0x12345678U;
    polygon.tail_value = 7.5F;
    spec.polygons.push_back(polygon);
    ZoneSpec zone;
    zone.key = 101;
    zone.sections = {100};
    zone.link0 = 0xCAFEBABEU;
    zone.unknown = -3;
    zone.polygon_link = 0x99887766U;
    zone.tail_value = -9;
    spec.zones.push_back(zone);
    const nfsu::TrackZoneReport report = parse(build_image(spec));

    check(report.valid, "the raw-field image must parse");
    check(report.polygons[0].stored_link[0] == 0xDEADBEEFU &&
              report.polygons[0].stored_link[1] == 0x12345678U,
          "the polygon's stored link words are kept");
    check(report.polygons[0].vertices[4][0] == 7.5F &&
              report.polygons[0].vertices[15][1] == 7.5F,
          "the vertex slots past the count are kept");
    check(report.zones[0].stored_link[0] == 0xCAFEBABEU,
          "the zone's stored link word is kept");
    check(report.zones[0].unknown == -3, "the zone's +0xa is kept");
    check(report.zones[0].stored_polygon_link == 0x99887766U,
          "the zone's stored polygon pointer is kept");
    check(report.zones[0].sections[1] == -9 && report.zones[0].sections[63] == -9,
          "the section slots past the count are kept");
}

void test_parse_refusals() {
    {
        Bytes image;
        append(image, chunk(0x00030000U, Bytes(16, 0U)));
        check_refused(parse(image), "no 0x80034150", "an image with no container");
    }
    {
        Bytes image = build_image(two_zone_spec());
        append(image, build_image(two_zone_spec()));
        check_refused(parse(image), "more than one 0x80034150",
                      "two containers");
    }
    {
        ImageSpec spec = two_zone_spec();
        Bytes children;
        append(children, chunk(kPolygonChunk, Bytes()));
        append(children, chunk(kZoneChunk, Bytes()));
        append(children, chunk(0x00034155U, Bytes(8, 0U)));
        check_refused(parse(chunk(kContainer, children)),
                      "unknown child chunk 0x00034155", "an unknown child");
    }
    {
        Bytes children;
        append(children, chunk(kPolygonChunk, Bytes()));
        append(children, chunk(kZoneChunk, Bytes()));
        append(children, chunk(kZoneChunk, Bytes()));
        check_refused(parse(chunk(kContainer, children)),
                      "more than one 0x00034152", "a repeated child");
    }
    {
        ImageSpec spec = two_zone_spec();
        spec.omit_polygon_chunk = true;
        check_refused(parse(build_image(spec)), "no 0x00034151",
                      "a missing polygon chunk");
    }
    {
        ImageSpec spec = two_zone_spec();
        spec.omit_zone_chunk = true;
        check_refused(parse(build_image(spec)), "no 0x00034152",
                      "a missing zone chunk");
    }
    {
        Bytes children;
        append(children, chunk(kPolygonChunk, Bytes(kPolygonRecord + 1, 0U)));
        append(children, chunk(kZoneChunk, Bytes()));
        check_refused(parse(chunk(kContainer, children)),
                      "not a whole number of 140-byte records",
                      "a short polygon payload");
    }
    {
        Bytes children;
        append(children, chunk(kPolygonChunk, Bytes()));
        append(children, chunk(kZoneChunk, Bytes(kZoneRecord - 1, 0U)));
        check_refused(parse(chunk(kContainer, children)),
                      "not a whole number of 148-byte records",
                      "a short zone payload");
    }
    {
        ImageSpec spec;
        PolygonSpec polygon = square(101, 0.0F, 0.0F, 10.0F, 10.0F);
        polygon.vertex_count = 0;
        spec.polygons.push_back(polygon);
        check_refused(parse(build_image(spec)), "claims 0 vertices",
                      "a zero vertex count");
    }
    {
        ImageSpec spec;
        PolygonSpec polygon = square(101, 0.0F, 0.0F, 10.0F, 10.0F);
        polygon.vertex_count = 17;
        spec.polygons.push_back(polygon);
        check_refused(parse(build_image(spec)), "claims 17 vertices",
                      "a vertex count past the record");
    }
    {
        ImageSpec spec;
        PolygonSpec polygon = square(101, 0.0F, 0.0F, 10.0F, 10.0F);
        polygon.vertex_count = -1;
        polygon.vertices.clear();
        // vertex_count -1 means "derive" in the spec, so write it directly.
        Bytes record = polygon_record(polygon);
        record[nfsu::kTrackZonePolygonVertexCountOffset] = 0xFFU;
        record[nfsu::kTrackZonePolygonVertexCountOffset + 1] = 0xFFU;
        Bytes children;
        append(children, chunk(kPolygonChunk, record));
        append(children, chunk(kZoneChunk, Bytes()));
        check_refused(parse(chunk(kContainer, children)), "claims -1 vertices",
                      "a negative vertex count");
    }
    {
        ImageSpec spec;
        spec.polygons.push_back(square(101, 0.0F, 0.0F, 10.0F, 10.0F));
        ZoneSpec zone;
        zone.key = 101;
        zone.section_count = 65;
        spec.zones.push_back(zone);
        check_refused(parse(build_image(spec)), "claims 65 section numbers",
                      "a section count past the record");
    }
    {
        ImageSpec spec;
        spec.polygons.push_back(square(101, 0.0F, 0.0F, 10.0F, 10.0F));
        ZoneSpec zone;
        zone.key = 101;
        spec.zones.push_back(zone);
        Bytes children;
        Bytes record = zone_record(zone);
        put_u32(record, nfsu::kTrackZoneSectionCountOffset, 0xFFFFFFFFU);
        append(children,
               chunk(kPolygonChunk, polygon_record(spec.polygons[0])));
        append(children, chunk(kZoneChunk, record));
        check_refused(parse(chunk(kContainer, children)),
                      "claims -1 section numbers", "a negative section count");
    }
    {
        ImageSpec spec;
        spec.polygons.push_back(square(101, 0.0F, 0.0F, 10.0F, 10.0F));
        spec.polygons.push_back(square(101, 10.0F, 0.0F, 20.0F, 10.0F));
        check_refused(parse(build_image(spec)), "repeats the key 101",
                      "two polygons with one key");
    }
    {
        ImageSpec spec;
        spec.polygons.push_back(square(101, 0.0F, 0.0F, 10.0F, 10.0F));
        ZoneSpec a;
        a.key = 101;
        ZoneSpec b;
        b.key = 101;
        spec.zones = {a, b};
        check_refused(parse(build_image(spec)), "zone 1 repeats the key 101",
                      "two zones with one key");
    }
    {
        ImageSpec spec;
        spec.polygons.push_back(square(101, 0.0F, 0.0F, 10.0F, 10.0F));
        ZoneSpec zone;
        zone.key = 999;
        spec.zones.push_back(zone);
        check_refused(parse(build_image(spec)), "names no 0x00034151",
                      "a zone whose key has no polygon");
    }
    {
        Bytes image = build_image(two_zone_spec());
        image.push_back(0U);
        image.push_back(0U);
        check_refused(parse(image), "shorter than a chunk header",
                      "a trailing fragment");
    }
    {
        Bytes image = build_image(two_zone_spec());
        put_u32(image, 4, 0xFFFFU);
        check_refused(parse(image), "reaches past the end",
                      "a payload that overruns the image");
    }
    {
        const nfsu::TrackZoneReport report = nfsu::parse_track_zones(nullptr, 0, 0);
        check(!report.valid, "a null image must not be valid");
        check(has_text(report.issues, "no zone image bytes"),
              "a null image names itself");
    }
}

void test_empty_optional_lists() {
    // Three retail tracks ship an empty 0x00034153 and one an empty
    // 0x00034154. An empty payload is not an error and contributes no
    // record.
    ImageSpec spec = two_zone_spec();
    Bytes children;
    append(children, chunk(kPolygonChunk,
                           [&] {
                               Bytes payload;
                               for (const PolygonSpec& p : spec.polygons) {
                                   append(payload, polygon_record(p));
                               }
                               return payload;
                           }()));
    append(children, chunk(kZoneChunk,
                           [&] {
                               Bytes payload;
                               for (const ZoneSpec& z : spec.zones) {
                                   append(payload, zone_record(z));
                               }
                               return payload;
                           }()));
    append(children, chunk(kTrackListChunk, Bytes()));
    append(children, chunk(kExtraListChunk, Bytes()));
    const nfsu::TrackZoneReport report = parse(chunk(kContainer, children));
    check(report.valid, "empty optional chunks must parse");
    check_eq_u64(report.section_lists.size(), 0, "no section list records");
    check_eq_u64(report.cost.container_children, 4,
                 "empty children are still counted");
}

// ---------------------------------------------------------------------
// load_track_zones: the file path, the JDLZ branch and the branch that
// parses an image that was never compressed.
// ---------------------------------------------------------------------

// The smallest legal JDLZ stream for a payload: every step a literal. The
// decoder reads a flags1 byte before every eighth step and a flags2 byte
// only once, because flags2 is shifted on a match and there are none.
Bytes jdlz_all_literals(const Bytes& payload) {
    Bytes body;
    for (std::size_t i = 0; i < payload.size(); ++i) {
        if (i % 8 == 0) {
            body.push_back(0x00U); // flags1: eight literals
            if (i == 0) {
                body.push_back(0x00U); // flags2, read once
            }
        }
        body.push_back(payload[i]);
    }
    Bytes out;
    out.push_back('J');
    out.push_back('D');
    out.push_back('L');
    out.push_back('Z');
    out.push_back(0x02U);
    out.push_back(0x10U);
    out.push_back(0x00U);
    out.push_back(0x00U);
    append_u32(out, static_cast<std::uint32_t>(payload.size()));
    append_u32(out, static_cast<std::uint32_t>(out.size() + 4 + body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bool write_file(const std::string& path, const Bytes& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

void test_load() {
    const Bytes image = build_image(two_zone_spec());
    // The encoder is a fixture, so prove it against the decoder the loader
    // uses before anything is concluded from a file that went through it.
    {
        std::vector<unsigned char> round;
        std::size_t unused = 0;
        std::string error;
        const Bytes packed = jdlz_all_literals(image);
        const bool ok = nfsu::jdlz_decompress(packed.data(), packed.size(),
                                              1U << 20, round, unused, error);
        check(ok, "the test's JDLZ encoder round trips: " + error);
        check(round == image, "the round trip returns the same bytes");
    }

    std::error_code code;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path(code) / "nfsu_track_zone_tests";
    if (code) {
        check(false, "cannot name a temporary directory: " + code.message());
        return;
    }
    std::filesystem::create_directories(dir, code);
    if (code) {
        check(false, "cannot create a temporary directory: " + code.message());
        return;
    }
    const std::filesystem::path packed_path = dir / "packed.lzc";
    const std::filesystem::path raw_path = dir / "raw.lzc";
    const std::filesystem::path broken_path = dir / "broken.lzc";
    const std::filesystem::path missing_path = dir / "absent.lzc";

    check(write_file(packed_path.string(), jdlz_all_literals(image)),
          "the packed fixture is written");
    check(write_file(raw_path.string(), image), "the raw fixture is written");
    Bytes broken = jdlz_all_literals(image);
    broken[12] = static_cast<unsigned char>(broken[12] + 1U); // the packed size
    check(write_file(broken_path.string(), broken),
          "the broken fixture is written");

    {
        std::vector<unsigned char> bytes;
        const nfsu::TrackZoneReport report =
            nfsu::load_track_zones(packed_path, bytes);
        check(report.valid, "a JDLZ file loads");
        check(bytes == image, "the decompressed image is the one written");
        check_eq_u64(report.cost.zones, 2, "the JDLZ file holds two zones");
        check(report.file_size == jdlz_all_literals(image).size(),
              "the file size is the packed size");
        check(report.cost.packed_bytes == report.file_size,
              "packed bytes and file size agree");
    }
    {
        std::vector<unsigned char> bytes;
        const nfsu::TrackZoneReport report =
            nfsu::load_track_zones(raw_path, bytes);
        check(report.valid, "a file with no JDLZ header parses as it stands");
        check_eq_u64(report.cost.unused_packed_bytes, 0,
                     "an uncompressed file leaves no packed bytes unread");
    }
    {
        std::vector<unsigned char> bytes;
        const nfsu::TrackZoneReport report =
            nfsu::load_track_zones(broken_path, bytes);
        check(!report.valid, "a broken JDLZ header is refused");
        check(has_text(report.issues, "JDLZ"),
              "the refusal names JDLZ rather than a record rule");
    }
    {
        std::vector<unsigned char> bytes;
        const nfsu::TrackZoneReport report =
            nfsu::load_track_zones(missing_path, bytes);
        check(!report.valid, "a missing file is refused");
        check(has_text(report.issues, "cannot size the file"),
              "the refusal names the file, not a record rule");
    }
    {
        nfsu::TrackZoneLimits limits;
        limits.max_packed_bytes = 8;
        std::vector<unsigned char> bytes;
        const nfsu::TrackZoneReport report =
            nfsu::load_track_zones(packed_path, bytes, limits);
        check(!report.valid, "the packed-size limit refuses a larger file");
        check(has_text(report.issues, "over the limit of 8"),
              "the refusal names the limit it broke");
    }

    // Only the files this test created, by exact name.
    std::filesystem::remove(packed_path, code);
    std::filesystem::remove(raw_path, code);
    std::filesystem::remove(broken_path, code);
}

void test_determinism() {
    const Bytes image = build_image(two_zone_spec());
    const nfsu::TrackZoneReport a = parse(image);
    const nfsu::TrackZoneReport b = parse(image);
    check(a.valid && b.valid, "both parses are valid");
    check(a.zones.size() == b.zones.size(), "same zone count");
    check(a.zones[0].polygon_index == b.zones[0].polygon_index,
          "same resolution");
    check(a.cost.zone_sections == b.cost.zone_sections, "same cost");
}

// ---------------------------------------------------------------------
// Picking a zone
// ---------------------------------------------------------------------

void test_find_zone() {
    const nfsu::TrackZoneReport report = parse(build_image(two_zone_spec()));
    check(report.valid, "the two-zone image must parse");

    const float left[2] = {50.0F, 50.0F};
    const float right[2] = {150.0F, 50.0F};
    const float above[2] = {50.0F, 500.0F};
    check_eq_u64(nfsu::find_track_zone(report, left), 0, "the left zone");
    check_eq_u64(nfsu::find_track_zone(report, right), 1, "the right zone");
    check(nfsu::find_track_zone(report, above) == nfsu::kTrackZoneNone,
          "a point in no polygon picks no zone");
    check(nfsu::find_track_zone(report, nullptr) == nfsu::kTrackZoneNone,
          "a null point picks no zone");

    // The shared edge at slot0 = 100 belongs to the right square by the
    // strict toggle: for the left square its two crossings are 0 and 100
    // and neither is greater than 100, so the left square reports outside.
    const float seam[2] = {100.0F, 50.0F};
    check_eq_u64(nfsu::find_track_zone(report, seam), 1, "the seam");

    ImageSpec bad;
    bad.polygons.push_back(square(101, 0.0F, 0.0F, 10.0F, 10.0F));
    ZoneSpec zone;
    zone.key = 999;
    bad.zones.push_back(zone);
    const nfsu::TrackZoneReport refused = parse(build_image(bad));
    check(nfsu::find_track_zone(refused, left) == nfsu::kTrackZoneNone,
          "an invalid report picks no zone");
}

void test_find_zone_takes_the_first_match() {
    // Retail scans a list and stops at the first hit. Two polygons over one
    // point is not something the retail data does, and the order this
    // decoder scans in is record order; the assertion exists so that a
    // change to "the last match" or "the smallest key" is visible.
    ImageSpec spec;
    spec.polygons.push_back(square(201, 0.0F, 0.0F, 100.0F, 100.0F));
    spec.polygons.push_back(square(101, 0.0F, 0.0F, 100.0F, 100.0F));
    ZoneSpec first;
    first.key = 201;
    first.sections = {200};
    ZoneSpec second;
    second.key = 101;
    second.sections = {100};
    spec.zones = {first, second};
    const nfsu::TrackZoneReport report = parse(build_image(spec));
    check(report.valid, "the overlapping image must parse");
    const float point[2] = {50.0F, 50.0F};
    check_eq_u64(nfsu::find_track_zone(report, point), 0,
                 "the first record in order wins");
}

// ---------------------------------------------------------------------
// Residency and the draw list
// ---------------------------------------------------------------------

void test_zone_residency() {
    ImageSpec spec = two_zone_spec();
    spec.zones[0].sections = {100, -7, 101, 110, 111};
    const nfsu::TrackZoneReport report = parse(build_image(spec));
    check(report.valid, "the residency image must parse");

    nfsu::TrackResidency residency;
    check(nfsu::track_zone_residency(report, 0, residency), "zone 0 residency");
    check(residency.rule == nfsu::TrackResidencyRule::zone_list,
          "the rule is zone_list");
    check_list(residency.zone_sections, {100, 101, 110, 111},
               "the negative entry is dropped and the order is kept");

    check(!nfsu::track_zone_residency(report, 99, residency),
          "an index past the end is refused");
    nfsu::TrackZoneReport empty;
    check(!nfsu::track_zone_residency(empty, 0, residency),
          "an empty report is refused");

    // The validity guard needs a report that is not valid and that still
    // holds a resolvable zone. The parser cannot produce one - every step
    // that raises an issue returns before the zones resolve - so the guard
    // is a contract rather than a state the parser reaches, and the only
    // way to test a contract is to build the state by hand.
    nfsu::TrackZoneReport hand;
    hand.valid = false;
    nfsu::TrackZonePolygon polygon;
    polygon.index = 0;
    polygon.key = 101;
    polygon.vertex_count = 4;
    polygon.vertices[0][0] = 0.0F;
    polygon.vertices[0][1] = 0.0F;
    polygon.vertices[1][0] = 100.0F;
    polygon.vertices[1][1] = 0.0F;
    polygon.vertices[2][0] = 100.0F;
    polygon.vertices[2][1] = 100.0F;
    polygon.vertices[3][0] = 0.0F;
    polygon.vertices[3][1] = 100.0F;
    hand.polygons.push_back(polygon);
    nfsu::TrackZone zone;
    zone.index = 0;
    zone.key = 101;
    zone.section_count = 1;
    zone.sections[0] = 100;
    zone.polygon_index = 0;
    hand.zones.push_back(zone);

    check(!nfsu::track_zone_residency(hand, 0, residency),
          "a report that is not valid yields no residency, zone or no zone");
    const float inside_hand[2] = {50.0F, 50.0F};
    check(nfsu::find_track_zone(hand, inside_hand) == nfsu::kTrackZoneNone,
          "a report that is not valid picks no zone, even for a point in one");
    check(nfsu::track_zone_draw_list(hand, 0, {100}).empty(),
          "a report that is not valid draws nothing");

    // The same report, marked valid, must answer - otherwise the three
    // assertions above would hold for the wrong reason.
    hand.valid = true;
    check(nfsu::find_track_zone(hand, inside_hand) == 0,
          "the same report, marked valid, picks the zone");
    check(nfsu::track_zone_residency(hand, 0, residency),
          "the same report, marked valid, yields a residency");
    check_list(residency.zone_sections, {100}, "and it is the zone's list");
}

void test_zone_draw_list() {
    ImageSpec spec = two_zone_spec();
    spec.zones[0].sections = {100, -7, 101, 110, 111, 999};
    const nfsu::TrackZoneReport report = parse(build_image(spec));
    check(report.valid, "the draw-list image must parse");

    // 999 is not resident, -7 is negative, 2601 and 2699 are in the band
    // and 2700 is not.
    const std::vector<std::uint32_t> resident = {100,  101,  110,  111,
                                                 2601, 2650, 2699, 2700};
    check_list(nfsu::track_zone_draw_list(report, 0, resident),
               {2601, 2650, 2699, 100, 101, 110, 111},
               "the band comes first, ascending, then the zone's own order");

    // A band number the global table does not hold is skipped, exactly as
    // 0x00446489 skips a null table entry.
    check_list(nfsu::track_zone_draw_list(report, 0, {100, 2650}), {2650, 100},
               "only resident numbers reach the buffer");

    // The cap is checked before every append, band entries included.
    check_list(nfsu::track_zone_draw_list(report, 0, resident, 4),
               {2601, 2650, 2699, 100}, "the cap stops the append");
    check_list(nfsu::track_zone_draw_list(report, 0, resident, 2),
               {2601, 2650}, "the cap can stop inside the band");
    check_list(nfsu::track_zone_draw_list(report, 0, resident, 0), {},
               "a cap of zero draws nothing");

    // Retail does not check for a repeat, so a zone list naming a band
    // number puts it in the buffer twice. No retail zone list does; the
    // behaviour is reproduced rather than tidied.
    ImageSpec repeat = two_zone_spec();
    repeat.zones[0].sections = {2650, 100};
    const nfsu::TrackZoneReport repeated = parse(build_image(repeat));
    check_list(nfsu::track_zone_draw_list(repeated, 0, {100, 2650}),
               {2650, 2650, 100},
               "a band number in a zone list is appended twice");

    check_list(nfsu::track_zone_draw_list(report, 99, resident), {},
               "an index past the end draws nothing");

    // The negative-entry skip has to be tested against a resident number,
    // or it cannot fail: a dropped skip reinterprets -7 as the unsigned
    // 65529, which a residency that does not hold 65529 drops anyway. The
    // skip is then doing no work the residency filter was not already
    // doing, and only this list can tell the two apart.
    std::vector<std::uint32_t> with_wrapped = resident;
    with_wrapped.push_back(65529); // (std::uint16_t)(-7)
    check_list(nfsu::track_zone_draw_list(report, 0, with_wrapped),
               {2601, 2650, 2699, 100, 101, 110, 111},
               "a negative entry is skipped before residency, not by it");
}

void test_no_zone_draw_list() {
    // 0x00446459 `cmp edx, 0xa`: the sub-index below ten, and the loaded
    // order kept as the list holds it.
    const std::vector<std::uint32_t> loaded = {303, 310, 309, 2601, 2610,
                                               100, 199, 0,   9,    10};
    check_list(nfsu::track_no_zone_draw_list(loaded),
               {303, 309, 2601, 100, 0, 9},
               "modulo 100 below ten, in the loaded order");
    check_list(nfsu::track_no_zone_draw_list(loaded, 3), {303, 309, 2601},
               "the cap stops the append");
    check_list(nfsu::track_no_zone_draw_list({}), {}, "an empty list");
}

void test_section_number_arithmetic() {
    // The boundaries of the two rules, stated as numbers rather than as
    // the constants the implementation reads.
    check(nfsu::track_section_resident_without_zone(0), "0 is resident");
    check(nfsu::track_section_resident_without_zone(9), "9 is resident");
    check(!nfsu::track_section_resident_without_zone(10), "10 is not");
    check(!nfsu::track_section_resident_without_zone(99), "99 is not");
    check(nfsu::track_section_resident_without_zone(100), "100 is resident");
    check(nfsu::track_section_resident_without_zone(109), "109 is resident");
    check(!nfsu::track_section_resident_without_zone(110), "110 is not");
    check(nfsu::track_section_resident_without_zone(2609), "2609 is resident");
    check(!nfsu::track_section_resident_without_zone(2610), "2610 is not");

    check(!nfsu::track_section_always_resident(2599), "2599 is not in the band");
    check(nfsu::track_section_always_resident(2600), "2600 opens the band");
    check(nfsu::track_section_always_resident(2699), "2699 closes the band");
    check(!nfsu::track_section_always_resident(2700), "2700 is past the band");

    // 0x40 + number / 100: the letter the two runtime list operations bound
    // to 'A'..'T'. The band is 'Z' and is outside that range, which is why
    // the range is not a filter on what is drawn.
    char letter = '\0';
    check(nfsu::track_section_letter(100, letter) && letter == 'A',
          "100 is letter A");
    check(nfsu::track_section_letter(2000, letter) && letter == 'T',
          "2000 is letter T");
    check(nfsu::track_section_letter(2600, letter) && letter == 'Z',
          "2600 is letter Z");
    check_eq_u64(nfsu::kTrackZoneEditLetterFirst, 'A', "the edit range opens at A");
    check_eq_u64(nfsu::kTrackZoneEditLetterLast, 'T', "the edit range closes at T");
}

} // namespace

// The region fixture and the cross-check against the recovered culler live
// in a second translation unit's worth of code below, because they need the
// scenery chunk writer rather than the zone one.
#include "nfsu/track_objects.h"
#include "nfsu/track_visibility.h"

#include <cmath>

namespace {

class Writer {
public:
    explicit Writer(std::uint64_t base) : base_(base) {}

    std::uint64_t at() const { return base_ + bytes_.size(); }
    const Bytes& bytes() const { return bytes_; }

    void begin(std::uint32_t id) {
        open_.push_back(bytes_.size());
        append_u32(bytes_, id);
        append_u32(bytes_, 0);
    }

    void end() {
        const std::size_t header = open_.back();
        open_.pop_back();
        put_u32(bytes_, header + 4,
                static_cast<std::uint32_t>(bytes_.size() - header - 8));
    }

    void append_bytes(const Bytes& payload) {
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    void leaf(std::uint32_t id, const Bytes& payload) {
        begin(id);
        append_bytes(payload);
        end();
    }

    void pad_to(std::uint64_t alignment) {
        while (at() % alignment != 0) {
            bytes_.push_back(0x11U);
        }
    }

private:
    std::uint64_t base_;
    Bytes bytes_;
    std::vector<std::size_t> open_;
};

// One section of `count` cubes in a row along raw slot 0, at 100, 200, ...,
// each of half extent 5, with a flat tree: node 0 names every instance.
void write_row_section(Writer& writer, std::uint32_t number, int count) {
    writer.begin(nfsu::kScenerySectionId);

    writer.begin(nfsu::kSceneryHeaderId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes header(nfsu::kSceneryHeaderRecordSize, 0U);
    put_u32(header, 12, number);
    writer.append_bytes(header);
    writer.end();

    writer.begin(nfsu::kSceneryInstanceListId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes instances;
    for (int i = 0; i < count; ++i) {
        const int centre = 100 * (i + 1);
        const std::int16_t lo[3] = {static_cast<std::int16_t>(centre - 5), -5, -5};
        const std::int16_t hi[3] = {static_cast<std::int16_t>(centre + 5), 5, 5};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(instances, lo[axis]);
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(instances, hi[axis]);
        }
        append_u16(instances, 0);
        append_u16(instances, 0);
        append_f32(instances, static_cast<float>(centre));
        append_f32(instances, 0.0F);
        append_f32(instances, 0.0F);
        const std::int16_t identity[9] = {8192, 0, 0, 0, 8192, 0, 0, 0, 8192};
        for (std::size_t word = 0; word < 9; ++word) {
            append_i16(instances, identity[word]);
        }
        append_i16(instances, 0);
    }
    writer.append_bytes(instances);
    writer.end();

    Bytes objects;
    for (std::size_t slot = 0; slot < nfsu::kSceneryObjectMeshSlots; ++slot) {
        append_u32(objects, slot == 0 ? 0xAAAA0001U : 0U);
    }
    objects.resize(68, 0U);
    append_f32(objects, 5.0F);
    objects.resize(nfsu::kSceneryObjectRecordSize, 0U);
    writer.leaf(nfsu::kSceneryObjectListId, objects);

    Bytes tree;
    const std::int16_t lo[3] = {95, -5, -5};
    const std::int16_t hi[3] = {static_cast<std::int16_t>(100 * count + 5), 5, 5};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        append_i16(tree, lo[axis]);
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        append_i16(tree, hi[axis]);
    }
    append_u16(tree, static_cast<std::uint16_t>(count));
    for (std::size_t slot = 0; slot < nfsu::kSceneryBoundNodeEntries; ++slot) {
        append_i16(tree, slot < static_cast<std::size_t>(count)
                             ? static_cast<std::int16_t>(slot)
                             : 0);
    }
    writer.leaf(nfsu::kSceneryBoundTreeId, tree);

    writer.end();
}

constexpr std::uint64_t kRegionBase = 23011344;

// Three sections: 100 (three instances), 110 (two) and 2601 (one). The
// numbers are chosen so that the three rules disagree about all three:
// 100 and 2601 pass the no-zone rule and 110 does not, and only 100 is in
// the zone list built beside it.
nfsu::TrackObjectReport region() {
    Writer writer(kRegionBase);
    write_row_section(writer, 100, 3);
    write_row_section(writer, 110, 2);
    write_row_section(writer, 2601, 1);
    const Bytes& bytes = writer.bytes();
    return nfsu::parse_track_objects(bytes.data(), bytes.size(), kRegionBase);
}

nfsu::TrackFrustum wide_frustum() {
    nfsu::TrackCamera camera;
    camera.eye[0] = 0.0F;
    camera.eye[1] = 0.0F;
    camera.eye[2] = 0.0F;
    camera.target[0] = 1.0F;
    camera.target[1] = 0.0F;
    camera.target[2] = 0.0F;
    camera.far_plane = 5000.0F;
    camera.vertical_fov = 2.0F;
    camera.aspect = 2.0F;
    nfsu::TrackFrustum frustum;
    if (!nfsu::track_frustum_look_at(camera, frustum)) {
        std::cerr << "test failure: the reference camera builds no frustum\n";
        ++g_failures;
    }
    return frustum;
}

std::vector<std::uint32_t> resident_numbers(const nfsu::TrackCullReport& report) {
    std::vector<std::uint32_t> out;
    for (const nfsu::TrackSectionCull& section : report.sections) {
        if (section.resident) {
            out.push_back(section.number);
        }
    }
    return out;
}

void test_residency_drives_the_culler() {
    const nfsu::TrackObjectReport objects = region();
    check(objects.ok(), "the three-section region must parse");
    check_eq_u64(objects.sections.size(), 3, "three sections");
    const nfsu::TrackFrustum frustum = wide_frustum();

    {
        nfsu::TrackCullOptions options;
        options.residency.rule = nfsu::TrackResidencyRule::every_section;
        const nfsu::TrackCullReport report =
            nfsu::cull_track_region(objects, frustum, options);
        check_list(resident_numbers(report), {100, 110, 2601},
                   "every_section keeps all three");
        check_eq_u64(report.stats.instances_visible, 6,
                     "every_section sees every instance");
    }
    {
        nfsu::TrackCullOptions options;
        options.residency.rule = nfsu::TrackResidencyRule::no_zone;
        const nfsu::TrackCullReport report =
            nfsu::cull_track_region(objects, frustum, options);
        check_list(resident_numbers(report), {100, 2601},
                   "no_zone drops 110 and keeps 2601");
        check_eq_u64(report.stats.instances_visible, 4,
                     "no_zone sees four instances");
    }

    ImageSpec spec;
    spec.polygons.push_back(square(101, 0.0F, 0.0F, 100.0F, 100.0F));
    ZoneSpec zone;
    zone.key = 101;
    zone.sections = {100};
    spec.zones.push_back(zone);
    const nfsu::TrackZoneReport zones = parse(build_image(spec));
    check(zones.valid, "the culler's zone image must parse");

    const float inside_point[2] = {50.0F, 50.0F};
    const std::size_t picked = nfsu::find_track_zone(zones, inside_point);
    check_eq_u64(picked, 0, "the camera point picks zone 0");

    nfsu::TrackCullOptions options;
    check(nfsu::track_zone_residency(zones, picked, options.residency),
          "the zone fills the residency");
    const nfsu::TrackCullReport report =
        nfsu::cull_track_region(objects, frustum, options);
    check(report.ok(), "a filled zone list raises no issue");
    check_list(resident_numbers(report), {100, 2601},
               "the zone list plus the 2600 band");
    check_eq_u64(report.stats.instances_visible, 4,
                 "the zone sees four instances");

    // The cross-check the recovered culler makes available: under the same
    // residency, the tree walk and the flat instance test must select the
    // same instances, and the explicit fallback must select every instance
    // of the same resident sections.
    nfsu::TrackCullOptions flat = options;
    flat.policy = nfsu::TrackCullPolicy::flat_instance_test;
    const nfsu::TrackCullReport flat_report =
        nfsu::cull_track_region(objects, frustum, flat);
    check(report.visible.size() == flat_report.visible.size(),
          "the tree and the flat test agree on the count");
    bool same = report.visible.size() == flat_report.visible.size();
    for (std::size_t i = 0; same && i < report.visible.size(); ++i) {
        same = report.visible[i].section_index ==
                   flat_report.visible[i].section_index &&
               report.visible[i].instance_index ==
                   flat_report.visible[i].instance_index;
    }
    check(same, "the tree and the flat test agree instance for instance");

    nfsu::TrackCullOptions all = options;
    all.policy = nfsu::TrackCullPolicy::draw_everything;
    const nfsu::TrackCullReport all_report =
        nfsu::cull_track_region(objects, frustum, all);
    check_list(resident_numbers(all_report), {100, 2601},
               "the fallback respects the residency it was given");
    check_eq_u64(all_report.stats.instances_visible, 4,
                 "the fallback draws every instance of the resident sections");

    // The fallback is still reachable, and it still says so: a
    // draw_everything run names itself on every section it culled.
    for (const nfsu::TrackSectionCull& section : all_report.sections) {
        if (!section.resident) {
            continue;
        }
        check(section.policy_used == nfsu::TrackCullPolicy::draw_everything,
              "a fallback section names the fallback");
    }
}

void test_empty_zone_list_still_names_the_chunk() {
    const nfsu::TrackObjectReport objects = region();
    const nfsu::TrackFrustum frustum = wide_frustum();
    nfsu::TrackCullOptions options;
    options.residency.rule = nfsu::TrackResidencyRule::zone_list;
    const nfsu::TrackCullReport report =
        nfsu::cull_track_region(objects, frustum, options);
    check(!report.ok(), "an empty zone list raises an issue");
    check(has_text(report.issues, "0x80034150"),
          "the issue names the visibility-zone chunk");
    check(has_text(report.issues, "track_zone_residency"),
          "the issue names the call that fills the list");
    check_list(resident_numbers(report), {2601},
               "an empty list leaves only the band");
}

} // namespace

int main() {
    test_constants();
    test_polygon_basic();
    test_polygon_boundaries();
    test_polygon_winding_and_shape();
    test_polygon_counts();
    test_polygon_uses_only_two_floats();
    test_parse_valid();
    test_parse_keeps_raw_fields();
    test_parse_refusals();
    test_empty_optional_lists();
    test_load();
    test_determinism();
    test_find_zone();
    test_find_zone_takes_the_first_match();
    test_zone_residency();
    test_zone_draw_list();
    test_no_zone_draw_list();
    test_section_number_arithmetic();
    test_residency_drives_the_culler();
    test_empty_zone_list_still_names_the_chunk();

    std::cout << "track zone tests: " << g_checks << " checks, " << g_failures
              << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
