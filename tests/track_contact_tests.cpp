#include "nfsu/arg_path.h"
#include "nfsu/memory_arena.h"
#include "nfsu/track_collision.h"
#include "nfsu/track_contact.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
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
#undef near
#undef far
#else
#include <unistd.h>
#endif

// Issue #158: the whole-track collision and its contact queries, over
// synthetic images that hold several separate sections of track, the way
// one retail TRACKB image holds every section of a circuit. Nothing here
// reads a retail file.

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

std::string text(float value) {
    std::ostringstream out;
    out.precision(9);
    out << value;
    return out.str();
}

// A fixture name no other process shares; see tests/track_collision_tests.cpp
// for why a name without the process id cost this repository two issues.
std::string unique_fixture(const std::string& label) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return "nfsu_track_contact_" + label + "_" + std::to_string(pid);
}

// ---------------------------------------------------------------------
// A synthetic collision image, written from the format's own numbers as
// literals so that a constant drifting in a header cannot move the fixture
// with it.
// ---------------------------------------------------------------------

constexpr std::uint32_t kTreeChunk = 0x00034130U;
constexpr std::uint32_t kCellChunk = 0x00034131U;
constexpr std::uint32_t kPolygonChunk = 0x00034132U;
constexpr std::uint32_t kIndexChunk = 0x00034133U;
constexpr std::uint32_t kBoundsChunk = 0x00034134U;
constexpr std::size_t kAlignment = 16;
constexpr unsigned char kFiller = 0x11;
constexpr std::int16_t kSaturation = 32512;
constexpr float kRootHalf = 16384.0F;
constexpr std::uint32_t kDepth = 10;

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFFU));
    }
}

void append_f32(Bytes& bytes, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(bytes, bits);
}

void write_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[offset + static_cast<std::size_t>(i)] =
            static_cast<unsigned char>((value >> (8 * i)) & 0xFFU);
    }
}

void pad_to_alignment(Bytes& bytes) {
    while (bytes.size() % kAlignment != 0) {
        bytes.push_back(kFiller);
    }
}

struct Corner {
    float px;
    float pz;
    float py;
};

struct Polygon {
    std::int16_t base = 0;
    std::int16_t slot0[4] = {0, 0, 0, 0};
    std::int16_t slot1[4] = {0, 0, 0, 0};
    std::int16_t slot2[4] = {0, 0, 0, 0};
};

// Four corners in perimeter order. The vertical is stored as a whole-unit
// base plus 256ths, as speed.exe 0x0043FD20 reads it back, so every fixture
// here exercises the base.
Polygon polygon(const Corner (&corners)[4], std::int16_t base) {
    Polygon out;
    out.base = base;
    for (std::size_t i = 0; i < 4; ++i) {
        out.slot0[i] = static_cast<std::int16_t>(std::lround(corners[i].px * 8.0F));
        out.slot1[i] = static_cast<std::int16_t>(std::lround(corners[i].pz * 8.0F));
        out.slot2[i] = static_cast<std::int16_t>(
            std::lround((corners[i].py - static_cast<float>(base)) * 256.0F));
    }
    return out;
}

// A flat quad at height py. Its corner order makes (c1 - c0) x (c2 - c0)
// point up.
Polygon flat(float x0, float z0, float x1, float z1, float py) {
    const Corner corners[4] = {{x0, z0, py}, {x1, z0, py}, {x1, z1, py}, {x0, z1, py}};
    return polygon(corners, static_cast<std::int16_t>(std::floor(py)));
}

// The same flat quad wound the other way, so (c1 - c0) x (c2 - c0) points
// down. Retail road is wound both ways; the review of issue #158 counted
// the downward-wound road triangles of TRACKB1210.
Polygon flat_clockwise(float x0, float z0, float x1, float z1, float py) {
    const Corner corners[4] = {{x0, z0, py}, {x0, z1, py}, {x1, z1, py}, {x1, z0, py}};
    return polygon(corners, static_cast<std::int16_t>(std::floor(py)));
}

// A flat quad through four given horizontal corners, in perimeter order.
Polygon flat_quad(const float (&xz)[4][2], float py) {
    const Corner corners[4] = {{xz[0][0], xz[0][1], py},
                               {xz[1][0], xz[1][1], py},
                               {xz[2][0], xz[2][1], py},
                               {xz[3][0], xz[3][1], py}};
    return polygon(corners, static_cast<std::int16_t>(std::floor(py)));
}

// A plane rising along px from h0 at x0 to h1 at x1.
Polygon ramp(float x0, float z0, float x1, float z1, float h0, float h1) {
    const Corner corners[4] = {{x0, z0, h0}, {x1, z0, h1}, {x1, z1, h1}, {x0, z1, h0}};
    return polygon(corners, static_cast<std::int16_t>(std::floor(h0)));
}

// A retail barrier: a line in the horizontal plane, saturated vertically.
Polygon barrier(float x0, float z0, float x1, float z1) {
    Polygon out;
    const float xs[4] = {x0, x0, x1, x1};
    const float zs[4] = {z0, z0, z1, z1};
    const std::int16_t ys[4] = {-kSaturation, kSaturation, kSaturation, -kSaturation};
    for (std::size_t i = 0; i < 4; ++i) {
        out.slot0[i] = static_cast<std::int16_t>(std::lround(xs[i] * 8.0F));
        out.slot1[i] = static_cast<std::int16_t>(std::lround(zs[i] * 8.0F));
        out.slot2[i] = ys[i];
    }
    return out;
}

// A vertical wall of finite height, from py0 up to py1.
Polygon pier(float x0, float z0, float x1, float z1, float py0, float py1) {
    const Corner corners[4] = {{x0, z0, py0}, {x0, z0, py1}, {x1, z1, py1}, {x1, z1, py0}};
    return polygon(corners, static_cast<std::int16_t>(std::floor(py0)));
}

// A triangle stored in the quad slot by repeating its last corner, which is
// what 85710 retail records are: triangle (0,2,3) of it is degenerate.
Polygon sliver(float x0, float z0, float x1, float z1, float py) {
    const Corner corners[4] = {{x0, z0, py}, {x1, z0, py}, {x1, z1, py}, {x1, z1, py}};
    return polygon(corners, static_cast<std::int16_t>(std::floor(py)));
}

struct Cell {
    float center[2] = {0.0F, 0.0F};
    std::vector<std::uint16_t> polygons;
};

struct Image {
    std::vector<Polygon> polygons;
    // Cell 0 is the sentinel and is added by build().
    std::vector<Cell> cells;
};

std::vector<std::size_t> cell_path(float x, float z) {
    std::vector<std::size_t> path;
    float cx = 0.0F;
    float cz = 0.0F;
    float half = kRootHalf;
    for (std::uint32_t depth = 0; depth < kDepth; ++depth) {
        const float quarter = half * 0.5F;
        std::size_t slot = 0;
        if (x > cx) {
            slot |= 1U;
            cx += quarter;
        } else {
            cx -= quarter;
        }
        if (z > cz) {
            slot |= 2U;
            cz += quarter;
        } else {
            cz -= quarter;
        }
        path.push_back(slot);
        half = quarter;
    }
    return path;
}

Bytes build(const Image& source) {
    std::vector<Cell> cells = source.cells;
    Cell sentinel;
    sentinel.center[0] = 999999.0F;
    sentinel.center[1] = 999999.0F;
    cells.insert(cells.begin(), sentinel);

    struct Node {
        std::uint16_t entries[4] = {0, 0, 0, 0};
    };
    std::vector<Node> nodes(1);
    for (std::size_t i = 1; i < cells.size(); ++i) {
        const std::vector<std::size_t> path =
            cell_path(cells[i].center[0], cells[i].center[1]);
        std::size_t node = 0;
        for (std::size_t d = 0; d + 1 < path.size(); ++d) {
            std::uint16_t next = nodes[node].entries[path[d]];
            if (next == 0) {
                nodes.push_back(Node{});
                next = static_cast<std::uint16_t>(nodes.size() - 1);
                nodes[node].entries[path[d]] = next;
            }
            node = next;
        }
        nodes[node].entries[path.back()] = static_cast<std::uint16_t>(i);
    }

    Bytes bytes;
    // One chunk nobody claims, as a retail image holds about forty.
    append_u32(bytes, 0x00000000U);
    append_u32(bytes, 8);
    for (int i = 0; i < 8; ++i) {
        bytes.push_back(0);
    }

    std::size_t header = bytes.size();
    append_u32(bytes, kTreeChunk);
    append_u32(bytes, 0);
    pad_to_alignment(bytes);
    for (const Node& node : nodes) {
        for (std::size_t e = 0; e < 4; ++e) {
            append_u16(bytes, node.entries[e]);
        }
    }
    write_u32(bytes, header + 4, static_cast<std::uint32_t>(bytes.size() - header - 8));

    std::vector<std::uint16_t> indices;
    header = bytes.size();
    append_u32(bytes, kCellChunk);
    append_u32(bytes, 0);
    pad_to_alignment(bytes);
    for (const Cell& cell : cells) {
        append_f32(bytes, cell.center[0]);
        append_f32(bytes, cell.center[1]);
        append_u32(bytes, static_cast<std::uint32_t>(indices.size()));
        append_u32(bytes, static_cast<std::uint32_t>(cell.polygons.size()));
        indices.insert(indices.end(), cell.polygons.begin(), cell.polygons.end());
    }
    write_u32(bytes, header + 4, static_cast<std::uint32_t>(bytes.size() - header - 8));

    append_u32(bytes, kPolygonChunk);
    append_u32(bytes, static_cast<std::uint32_t>(source.polygons.size() * 32U));
    for (const Polygon& p : source.polygons) {
        for (int i = 0; i < 4; ++i) {
            bytes.push_back(0);
        }
        append_u16(bytes, static_cast<std::uint16_t>(p.base));
        append_u16(bytes, 0xFFFFU);
        for (std::size_t i = 0; i < 4; ++i) {
            append_u16(bytes, static_cast<std::uint16_t>(p.slot0[i]));
        }
        for (std::size_t i = 0; i < 4; ++i) {
            append_u16(bytes, static_cast<std::uint16_t>(p.slot1[i]));
        }
        for (std::size_t i = 0; i < 4; ++i) {
            append_u16(bytes, static_cast<std::uint16_t>(p.slot2[i]));
        }
    }

    header = bytes.size();
    append_u32(bytes, kIndexChunk);
    append_u32(bytes, 0);
    for (std::uint16_t index : indices) {
        append_u16(bytes, index);
    }
    if (indices.size() % 2 != 0) {
        append_u16(bytes, 0);
    }
    write_u32(bytes, header + 4, static_cast<std::uint32_t>(bytes.size() - header - 8));

    float low[3] = {0.0F, 0.0F, 0.0F};
    float high[3] = {0.0F, 0.0F, 0.0F};
    bool first = true;
    for (const Polygon& p : source.polygons) {
        for (std::size_t c = 0; c < 4; ++c) {
            const float point[3] = {static_cast<float>(p.slot0[c]) / 8.0F,
                                    static_cast<float>(p.slot1[c]) / 8.0F,
                                    static_cast<float>(p.base) +
                                        static_cast<float>(p.slot2[c]) / 256.0F};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                low[axis] = first ? point[axis] : std::min(low[axis], point[axis]);
                high[axis] = first ? point[axis] : std::max(high[axis], point[axis]);
            }
            first = false;
        }
    }
    header = bytes.size();
    append_u32(bytes, kBoundsChunk);
    append_u32(bytes, 0);
    pad_to_alignment(bytes);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        append_f32(bytes, low[axis]);
    }
    append_f32(bytes, 0.0F);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        append_f32(bytes, high[axis]);
    }
    append_f32(bytes, 0.0F);
    write_u32(bytes, header + 4, static_cast<std::uint32_t>(bytes.size() - header - 8));
    return bytes;
}

// ---------------------------------------------------------------------
// Four sections of track, far apart, each in its own part of the
// quadtree, and each with the kind of thing a vehicle meets.
// ---------------------------------------------------------------------

struct Section {
    const char* name;
    std::vector<Polygon> polygons;
    // Cells with the polygons they name, as indices into `polygons`.
    std::vector<Cell> cells;
    // The horizontal square the section's cells cover.
    float min0;
    float min1;
    float max0;
    float max1;
};

Cell cell(float x, float z, std::vector<std::uint16_t> polygons) {
    Cell out;
    out.center[0] = x;
    out.center[1] = z;
    out.polygons = std::move(polygons);
    return out;
}

// A street between two barriers, with a gap in the road surface between
// its two slabs and a triangle stored in a quad slot above the first one.
// Cells centred at (976, -400) and (1008, -400): the (+px, -pz) quadrant.
Section street() {
    Section s{"street", {}, {}, 960.0F, -416.0F, 1024.0F, -384.0F};
    s.polygons.push_back(flat(962.0F, -410.0F, 990.0F, -390.0F, 50.25F)); // 0
    s.polygons.push_back(flat(994.0F, -410.0F, 1022.0F, -390.0F, 50.25F)); // 1
    s.polygons.push_back(barrier(961.0F, -411.0F, 1023.0F, -411.0F)); // 2
    s.polygons.push_back(barrier(961.0F, -389.0F, 1023.0F, -389.0F)); // 3
    s.polygons.push_back(sliver(966.0F, -406.0F, 974.0F, -398.0F, 50.5F)); // 4
    s.cells.push_back(cell(976.0F, -400.0F, {0, 2, 3, 4}));
    s.cells.push_back(cell(1008.0F, -400.0F, {1, 2, 3}));
    return s;
}

// A 45-degree ramp, a face steeper than 60 degrees and a flat top.
// Cells centred at (1648, 720) and (1680, 720): the (+px, +pz) quadrant.
Section hill() {
    Section s{"hill", {}, {}, 1632.0F, 704.0F, 1696.0F, 736.0F};
    s.polygons.push_back(ramp(1634.0F, 706.0F, 1650.0F, 734.0F, 60.0F, 76.0F)); // 0
    s.polygons.push_back(flat(1651.0F, 706.0F, 1663.0F, 734.0F, 76.0F)); // 1
    s.polygons.push_back(ramp(1666.0F, 706.0F, 1682.0F, 734.0F, 60.0F, 104.0F)); // 2
    s.cells.push_back(cell(1648.0F, 720.0F, {0, 1}));
    s.cells.push_back(cell(1680.0F, 720.0F, {2}));
    return s;
}

// A deck over a road, and a pier under the deck's far end.
// Cells centred at (-816, 304) and (-784, 304): the (-px, +pz) quadrant.
Section bridge() {
    Section s{"bridge", {}, {}, -832.0F, 288.0F, -768.0F, 320.0F};
    s.polygons.push_back(flat(-830.0F, 290.0F, -770.0F, 318.0F, -20.0F)); // 0
    s.polygons.push_back(flat(-826.0F, 292.0F, -802.0F, 316.0F, -10.0F)); // 1
    s.polygons.push_back(pier(-798.0F, 300.0F, -790.0F, 300.0F, -20.0F, -10.0F)); // 2
    s.cells.push_back(cell(-816.0F, 304.0F, {0, 1}));
    s.cells.push_back(cell(-784.0F, 304.0F, {0, 2}));
    return s;
}

// A slab across the world origin, which every one of the root's four
// quadrants shares, with a barrier through it.
Section origin() {
    Section s{"origin", {}, {}, -32.0F, -32.0F, 32.0F, 32.0F};
    s.polygons.push_back(flat(-30.0F, -30.0F, 30.0F, 30.0F, 0.5F)); // 0
    s.polygons.push_back(barrier(8.0F, -30.0F, 8.0F, 30.0F)); // 1
    s.cells.push_back(cell(-16.0F, -16.0F, {0}));
    s.cells.push_back(cell(16.0F, -16.0F, {0, 1}));
    s.cells.push_back(cell(-16.0F, 16.0F, {0}));
    s.cells.push_back(cell(16.0F, 16.0F, {0, 1}));
    return s;
}

std::vector<Section> sections() {
    return {street(), hill(), bridge(), origin()};
}

// The whole circuit: every section in one image, polygons and cells in
// section order. `base` receives each section's first polygon index.
Image whole_image(const std::vector<Section>& all, std::vector<std::size_t>& base) {
    Image image;
    base.clear();
    for (const Section& s : all) {
        base.push_back(image.polygons.size());
        const std::uint16_t offset = static_cast<std::uint16_t>(image.polygons.size());
        image.polygons.insert(image.polygons.end(), s.polygons.begin(), s.polygons.end());
        for (Cell c : s.cells) {
            for (std::uint16_t& p : c.polygons) {
                p = static_cast<std::uint16_t>(p + offset);
            }
            image.cells.push_back(c);
        }
    }
    return image;
}

Image region_image(const Section& s) {
    Image image;
    image.polygons = s.polygons;
    image.cells = s.cells;
    return image;
}

nfsu::TrackContactWorld world_of(const Bytes& bytes) {
    return nfsu::build_track_contact_world(
        nfsu::parse_track_collision(bytes.data(), bytes.size(), 0));
}

bool near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// A query's answer for a failure message: its status, and a hit's polygon,
// distance and height only when it hit, since a query that does not hit
// leaves the contact as the previous query left it.
std::string answer(nfsu::TrackContactStatus status, const nfsu::TrackContact& contact) {
    std::string out = nfsu::track_contact_status_name(status);
    if (status == nfsu::TrackContactStatus::hit) {
        out += " polygon " + std::to_string(contact.polygon) + " at distance " +
               text(contact.distance) + " height " + text(contact.point[2]);
    }
    return out;
}

nfsu::TrackContactStatus column(const nfsu::TrackContactWorld& world, float px,
                                float pz, float py, float rise, float drop,
                                nfsu::TrackContact& contact) {
    const float point[3] = {px, pz, py};
    return nfsu::query_track_contact_point(world, point, rise, drop, contact);
}

nfsu::TrackContactStatus segment(const nfsu::TrackContactWorld& world,
                                 const float from[3], const float to[3],
                                 nfsu::TrackContact& contact) {
    return nfsu::query_track_contact_segment(world, from, to, contact);
}

// ---------------------------------------------------------------------
// The world: triangles, kinds, counts and bytes
// ---------------------------------------------------------------------

// Would catch: a normal from the wrong corners or not normalised, the road
// test inverted or on the wrong component, a degenerate triangle counted as
// a surface, or a resident figure that forgets an array.
void test_world_build() {
    std::vector<std::size_t> base;
    const Bytes bytes = build(whole_image(sections(), base));
    const nfsu::TrackContactWorld world = world_of(bytes);
    check(world.valid, "the four-section image builds a valid world");
    if (!world.valid) {
        return;
    }
    const nfsu::TrackCollisionReport& report = world.collision;
    check(report.polygons.size() == 13, "thirteen polygons in four sections");
    check(world.triangles.size() == 26, "two triangles per polygon");

    // Triangle kinds over the whole image, counted by hand: the six flat
    // slabs and the 45-degree ramp are road on both triangles (7 polygons,
    // 14 triangles) plus the sliver's one real triangle; the three barriers,
    // the pier and the 70-degree face are wall (5 polygons, 10 triangles);
    // the sliver's (0,2,3) is degenerate.
    check(world.counts.road_triangles == 15,
          "road triangles: got " + std::to_string(world.counts.road_triangles) +
              ", want 15");
    check(world.counts.wall_triangles == 10,
          "wall triangles: got " + std::to_string(world.counts.wall_triangles) +
              ", want 10");
    check(world.counts.degenerate_triangles == 1,
          "degenerate triangles: got " +
              std::to_string(world.counts.degenerate_triangles) + ", want 1");

    // A flat slab: normal straight up, unit length.
    const nfsu::TrackContactTriangle& slab = world.triangles[base[0] * 2];
    check(!slab.degenerate && slab.kind == nfsu::TrackContactKind::road,
          "a flat slab is road");
    check(slab.normal[0] == 0.0F && slab.normal[1] == 0.0F && slab.normal[2] == 1.0F,
          "a flat slab's normal is (0, 0, 1) in raw slot order: got (" +
              text(slab.normal[0]) + ", " + text(slab.normal[1]) + ", " +
              text(slab.normal[2]) + ")");
    check(slab.up == 1.0F, "a flat slab has up 1");

    // The 45-degree ramp rises along px: its normal leans toward -px.
    const nfsu::TrackContactTriangle& ramp45 = world.triangles[base[1] * 2 + 1];
    const float r = std::sqrt(0.5F);
    check(ramp45.kind == nfsu::TrackContactKind::road,
          "a 45-degree ramp is road");
    check(near(ramp45.normal[0], -r, 1e-6F) && ramp45.normal[1] == 0.0F &&
              near(ramp45.normal[2], r, 1e-6F),
          "a 45-degree ramp's normal is (-0.7071, 0, 0.7071): got (" +
              text(ramp45.normal[0]) + ", " + text(ramp45.normal[1]) + ", " +
              text(ramp45.normal[2]) + ")");
    // Rising 44 over 16 is 70 degrees: up = 16 / sqrt(16^2 + 44^2).
    const nfsu::TrackContactTriangle& steep = world.triangles[(base[1] + 2) * 2];
    check(steep.kind == nfsu::TrackContactKind::wall,
          "a face steeper than 60 degrees is wall, up " + text(steep.up));
    check(near(steep.up, 16.0F / std::sqrt(16.0F * 16.0F + 44.0F * 44.0F), 1e-6F),
          "the steep face's up is the cosine of its slope: got " + text(steep.up));
    // A barrier is vertical: up 0 and wall.
    const nfsu::TrackContactTriangle& wall = world.triangles[(base[0] + 2) * 2];
    check(wall.kind == nfsu::TrackContactKind::wall && wall.up == 0.0F,
          "a saturated barrier is wall with up 0");
    for (const nfsu::TrackContactTriangle& t : world.triangles) {
        if (t.degenerate) {
            check(t.normal[0] == 0.0F && t.normal[1] == 0.0F && t.normal[2] == 0.0F,
                  "a degenerate triangle carries no normal");
            continue;
        }
        const float length = std::sqrt(t.normal[0] * t.normal[0] +
                                       t.normal[1] * t.normal[1] +
                                       t.normal[2] * t.normal[2]);
        check(near(length, 1.0F, 1e-6F), "every normal is unit length: " + text(length));
    }
    check(world.triangles[(base[0] + 4) * 2 + 1].degenerate,
          "the sliver's triangle (0,2,3) is the degenerate one");

    // Bytes: every array's size times its element, and the stored form.
    const nfsu::TrackContactMemory& m = world.memory;
    check(m.polygon_bytes == 13 * sizeof(nfsu::TrackCollisionPolygon), "polygon bytes");
    check(m.cell_bytes == 11 * sizeof(nfsu::TrackCollisionCell),
          "cell bytes cover ten cells and the sentinel");
    check(m.index_bytes == report.indices.size() * sizeof(std::uint32_t), "index bytes");
    check(m.node_bytes == report.nodes.size() * sizeof(nfsu::TrackCollisionTreeNode),
          "node bytes");
    check(m.triangle_bytes == 26 * sizeof(nfsu::TrackContactTriangle), "triangle bytes");
    check(m.resident_bytes == m.polygon_bytes + m.cell_bytes + m.index_bytes +
                                  m.node_bytes + m.triangle_bytes,
          "resident bytes are the five arrays");
    check(m.resident_allocations == 5, "five resident arrays");
    // 13 records of 32, 11 of 16, 20 indices of 2 - an even count, so no
    // padding - and the nodes at 8 each.
    check(report.indices.size() == 20, "20 cell/polygon pairs: got " +
                                           std::to_string(report.indices.size()));
    check(m.stored_bytes == 13 * 32 + 11 * 16 + 20 * 2 + report.nodes.size() * 8,
          "stored bytes are the arrays as the file holds them: got " +
              std::to_string(m.stored_bytes));
    check(m.packed_bytes == 0 && m.image_bytes == 0 &&
              m.load_peak_bytes == m.resident_bytes,
          "a world built in memory had no load buffers");

    // A report that is not valid gives a world that answers nothing.
    Bytes broken = bytes;
    broken.resize(broken.size() - 3);
    const nfsu::TrackContactWorld refused = world_of(broken);
    check(!refused.valid && refused.triangles.empty(),
          "an image that fails its own checks builds no world");
}

// ---------------------------------------------------------------------
// Point queries: the column a wheel probes
// ---------------------------------------------------------------------

// Would catch: the column started at the wrong end, a normal that does not
// face the query, a bridge answered from underneath, the airborne and the
// out-of-bounds answers confused, and the distance reported as a fraction.
void test_point_queries() {
    std::vector<std::size_t> base;
    const nfsu::TrackContactWorld world = world_of(build(whole_image(sections(), base)));
    nfsu::TrackContact c;

    // Straight down onto the street, from 2 units above the road.
    check(column(world, 980.0F, -395.0F, 52.25F, 0.0F, 10.0F, c) ==
              nfsu::TrackContactStatus::hit,
          "a wheel over the street finds the road");
    check(c.polygon == base[0] && c.kind == nfsu::TrackContactKind::road,
          "the street slab is road, polygon " + std::to_string(c.polygon));
    check(c.distance == 2.0F, "two units down: got " + text(c.distance));
    check(c.point[0] == 980.0F && c.point[1] == -395.0F && c.point[2] == 50.25F,
          "the contact is on the slab: got (" + text(c.point[0]) + ", " +
              text(c.point[1]) + ", " + text(c.point[2]) + ")");
    check(c.normal[0] == 0.0F && c.normal[1] == 0.0F && c.normal[2] == 1.0F,
          "met from above, the normal points up");
    check(c.up == 1.0F, "the contact's up is 1 on a flat slab");

    // The rise: a wheel already sunk below the road looks up for it.
    check(column(world, 980.0F, -395.0F, 49.25F, 2.0F, 2.0F, c) ==
              nfsu::TrackContactStatus::hit,
          "a wheel sunk 1 unit finds the road above it");
    check(c.distance == 1.0F && c.point[2] == 50.25F,
          "found at 1 unit from the top of the column: got " + text(c.distance));

    // The sliver sits a quarter unit above the slab over its real triangle.
    check(column(world, 973.0F, -404.0F, 60.0F, 0.0F, 20.0F, c) ==
              nfsu::TrackContactStatus::hit,
          "the sliver is met over its real triangle");
    check(c.polygon == base[0] + 4 && c.triangle == 0 && c.point[2] == 50.5F,
          "the higher surface first: got polygon " + std::to_string(c.polygon) +
              " triangle " + std::to_string(c.triangle));
    // Over the sliver's degenerate half the slab below is met instead.
    check(column(world, 967.0F, -399.0F, 60.0F, 0.0F, 20.0F, c) ==
              nfsu::TrackContactStatus::hit &&
              c.polygon == base[0] && c.point[2] == 50.25F,
          "the degenerate triangle is never met; the slab under it is");

    // The gap between the two slabs is inside a cell: airborne, not out of
    // bounds. So is a column that stops short of the road.
    check(column(world, 992.0F, -400.0F, 60.0F, 0.0F, 20.0F, c) ==
              nfsu::TrackContactStatus::clear,
          "the gap in the road surface is clear");
    check(column(world, 980.0F, -395.0F, 60.0F, 0.0F, 9.0F, c) ==
              nfsu::TrackContactStatus::clear,
          "a column that ends above the road is clear");
    // Outside every section: no cell at all.
    check(column(world, 500.0F, 500.0F, 0.0F, 100.0F, 100.0F, c) ==
              nfsu::TrackContactStatus::outside,
          "a column no cell covers is outside");
    check(column(world, 1025.0F, -400.0F, 0.0F, 100.0F, 100.0F, c) ==
              nfsu::TrackContactStatus::outside,
          "one unit past the street's last cell is outside");

    // The bridge: from above, the deck; from between, the road beneath.
    check(column(world, -810.0F, 300.0F, 0.0F, 0.0F, 40.0F, c) ==
                  nfsu::TrackContactStatus::hit &&
              c.polygon == base[2] + 1 && c.point[2] == -10.0F,
          "from above the bridge the deck is met first");
    check(column(world, -810.0F, 300.0F, -15.0F, 0.0F, 40.0F, c) ==
                  nfsu::TrackContactStatus::hit &&
              c.polygon == base[2] && c.point[2] == -20.0F,
          "from under the deck the road beneath is met");
    check(c.normal[2] == 1.0F, "the road beneath faces up");

    // The ramps: the 45-degree one is road and its normal faces the query;
    // the steep face is met, as wall.
    check(column(world, 1642.0F, 720.0F, 100.0F, 0.0F, 100.0F, c) ==
              nfsu::TrackContactStatus::hit,
          "the 45-degree ramp is met");
    check(c.kind == nfsu::TrackContactKind::road && near(c.point[2], 68.0F, 1e-4F),
          "the ramp is road at height 68 halfway up: got " + text(c.point[2]));
    check(c.normal[2] > 0.0F && c.normal[0] < 0.0F,
          "met from above, the ramp's normal leans back and up");
    check(column(world, 1674.0F, 720.0F, 120.0F, 0.0F, 100.0F, c) ==
                  nfsu::TrackContactStatus::hit &&
              c.kind == nfsu::TrackContactKind::wall && near(c.point[2], 82.0F, 1e-4F),
          "the steep face is met as wall at height 82");

    // The origin slab, in all four quadrants of the root square.
    const float corners[4][2] = {{-20.0F, -20.0F}, {20.0F, -20.0F},
                                 {-20.0F, 20.0F}, {20.0F, 20.0F}};
    for (const auto& xz : corners) {
        check(column(world, xz[0], xz[1], 5.0F, 0.0F, 10.0F, c) ==
                      nfsu::TrackContactStatus::hit &&
                  c.polygon == base[3] && c.point[2] == 0.5F,
              "the origin slab is met at (" + text(xz[0]) + ", " + text(xz[1]) + ")");
    }
}

// ---------------------------------------------------------------------
// Segment queries: the sweep a body makes
// ---------------------------------------------------------------------

// Would catch: the normal left on the stored side instead of the side hit,
// the nearest of two contacts not chosen, a segment in a wall's own plane
// counted as a hit, the distance not in world units, and a tie broken by
// visiting order.
void test_segment_queries() {
    std::vector<std::size_t> base;
    const nfsu::TrackContactWorld world = world_of(build(whole_image(sections(), base)));
    nfsu::TrackContact c;

    // Across the street toward -pz at bumper height: the barrier at -411.
    const float a[3] = {1000.0F, -400.0F, 51.0F};
    const float b[3] = {1000.0F, -420.0F, 51.0F};
    check(segment(world, a, b, c) == nfsu::TrackContactStatus::hit,
          "a sweep across the street meets a barrier");
    check(c.polygon == base[0] + 2 && c.kind == nfsu::TrackContactKind::wall,
          "the near barrier, as wall: got polygon " + std::to_string(c.polygon));
    check(c.distance == 11.0F && c.point[1] == -411.0F,
          "11 units from the start, on the barrier line: got " + text(c.distance));
    check(c.normal[0] == 0.0F && c.normal[1] == 1.0F && c.normal[2] == 0.0F,
          "the barrier's normal faces back toward +pz: got (" + text(c.normal[0]) +
              ", " + text(c.normal[1]) + ", " + text(c.normal[2]) + ")");
    // The same barrier from the other side: two-sided, normal turned.
    const float a2[3] = {1000.0F, -415.0F, 51.0F};
    const float b2[3] = {1000.0F, -405.0F, 51.0F};
    check(segment(world, a2, b2, c) == nfsu::TrackContactStatus::hit &&
              c.polygon == base[0] + 2 && c.distance == 4.0F,
          "the barrier is met from outside the street too");
    check(c.normal[1] == -1.0F, "from outside, the normal faces -pz");
    // A long sweep through both barriers stops at the nearer one.
    const float a3[3] = {1000.0F, -380.0F, 60.0F};
    const float b3[3] = {1000.0F, -420.0F, 60.0F};
    check(segment(world, a3, b3, c) == nfsu::TrackContactStatus::hit &&
              c.polygon == base[0] + 3 && c.distance == 9.0F,
          "of two barriers, the nearer: got polygon " + std::to_string(c.polygon) +
              " at " + text(c.distance));
    // Along the barrier, in its own plane: never a contact.
    const float a4[3] = {965.0F, -411.0F, 51.0F};
    const float b4[3] = {1020.0F, -411.0F, 51.0F};
    check(segment(world, a4, b4, c) == nfsu::TrackContactStatus::clear,
          "a sweep in a barrier's own plane is clear");
    // Along the street between the barriers and above the road: clear.
    const float a5[3] = {965.0F, -400.0F, 52.0F};
    const float b5[3] = {1020.0F, -400.0F, 52.0F};
    check(segment(world, a5, b5, c) == nfsu::TrackContactStatus::clear,
          "a sweep down the street above the road is clear");
    // Into the pier under the bridge, and the pier's finite height.
    const float a6[3] = {-794.0F, 296.0F, -15.0F};
    const float b6[3] = {-794.0F, 304.0F, -15.0F};
    check(segment(world, a6, b6, c) == nfsu::TrackContactStatus::hit &&
              c.polygon == base[2] + 2 && c.distance == 4.0F &&
              c.kind == nfsu::TrackContactKind::wall,
          "the pier stops a sweep under the deck");
    const float a7[3] = {-794.0F, 296.0F, -5.0F};
    const float b7[3] = {-794.0F, 304.0F, -5.0F};
    check(segment(world, a7, b7, c) == nfsu::TrackContactStatus::clear,
          "above the pier's top the sweep is clear");
    // A diagonal sweep into the barrier at the origin, whose distance is a
    // length and not a fraction of the segment.
    const float a8[3] = {0.0F, 0.0F, 3.0F};
    const float b8[3] = {16.0F, 12.0F, 3.0F};
    check(segment(world, a8, b8, c) == nfsu::TrackContactStatus::hit &&
              c.polygon == base[3] + 1 && near(c.distance, 10.0F, 1e-5F) &&
              near(c.point[0], 8.0F, 1e-5F) && near(c.point[1], 6.0F, 1e-5F),
          "a 3-4-5 sweep meets the barrier 10 units along: got " + text(c.distance));
    check(c.normal[0] == -1.0F, "the barrier faces the sweep's start");
    // A contact exactly at the segment's end is a contact, and so is one
    // exactly at its start: the second street slab, polygon 1, is met one
    // unit down from py 51.25 at the very end of a one-unit segment. The
    // first contact any query can find has to be accepted whatever it ties
    // with, or this one is lost to the untouched starting best.
    const float e0[3] = {1000.0F, -400.0F, 51.25F};
    const float e1[3] = {1000.0F, -400.0F, 50.25F};
    check(segment(world, e0, e1, c) == nfsu::TrackContactStatus::hit &&
              c.polygon == base[0] + 1 && c.distance == 1.0F && c.point[2] == 50.25F,
          "a contact exactly at the segment's end is met: got polygon " +
              std::to_string(c.polygon) + " at " + text(c.distance));
    check(column(world, 1000.0F, -400.0F, 52.25F, 0.0F, 2.0F, c) ==
                  nfsu::TrackContactStatus::hit &&
              c.polygon == base[0] + 1 && c.distance == 2.0F,
          "a column whose drop ends exactly on the road meets it");
    const float e2[3] = {1000.0F, -400.0F, 49.25F};
    check(segment(world, e1, e2, c) == nfsu::TrackContactStatus::hit &&
              c.polygon == base[0] + 1 && c.distance == 0.0F,
          "a contact exactly at the segment's start is met at distance 0");

    // A sweep that leaves every cell entirely.
    const float a9[3] = {3000.0F, 3000.0F, 0.0F};
    const float b9[3] = {3010.0F, 3000.0F, 0.0F};
    check(segment(world, a9, b9, c) == nfsu::TrackContactStatus::outside,
          "a sweep far from every section is outside");

    // Two identical slabs: the lower polygon index wins, whichever cell is
    // visited first. The second copy is named first in both cells.
    Image twins;
    twins.polygons.push_back(flat(2.0F, 2.0F, 60.0F, 30.0F, 5.0F));
    twins.polygons.push_back(flat(2.0F, 2.0F, 60.0F, 30.0F, 5.0F));
    twins.cells.push_back(cell(16.0F, 16.0F, {1, 0}));
    twins.cells.push_back(cell(48.0F, 16.0F, {1, 0}));
    const nfsu::TrackContactWorld pair = world_of(build(twins));
    for (float x = 3.0F; x < 60.0F; x += 7.0F) {
        check(column(pair, x, 10.0F, 10.0F, 0.0F, 10.0F, c) ==
                      nfsu::TrackContactStatus::hit &&
                  c.polygon == 0,
              "a tie goes to the lower polygon index at px " + text(x));
    }
}

// ---------------------------------------------------------------------
// Region against whole
// ---------------------------------------------------------------------

bool same(const nfsu::TrackContact& region, std::size_t base,
          const nfsu::TrackContact& whole) {
    return region.polygon + base == whole.polygon && region.triangle == whole.triangle &&
           region.kind == whole.kind && region.distance == whole.distance &&
           region.up == whole.up && region.point[0] == whole.point[0] &&
           region.point[1] == whole.point[1] && region.point[2] == whole.point[2] &&
           region.normal[0] == whole.normal[0] && region.normal[1] == whole.normal[1] &&
           region.normal[2] == whole.normal[2];
}

// Issue #158's second acceptance bullet: a query inside one part of the
// track answers exactly as the same query over an image holding that part
// alone - bit for bit, with only the polygon index offset by where the part
// sits in the whole. Would catch a walk that reaches past the query's own
// cells, a cap that counts the whole image, and a tie broken by an index
// that is not the polygon's own order.
//
// It also holds every column of the lattice to the ray query of issue #39
// over the whole image, whatever either answers: wherever the ray meets
// something the column is that hit bit for bit, and where the ray meets
// nothing so does the column, because no column of this lattice lies on a
// triangle edge the #39 arithmetic misses (test_edges_the_ray_misses has
// those). A query that loses contacts cannot pass that, where comparing
// only the columns both sides hit let it through.
void test_region_equals_whole() {
    const std::vector<Section> all = sections();
    std::vector<std::size_t> base;
    const nfsu::TrackContactWorld whole = world_of(build(whole_image(all, base)));
    check(whole.valid, "the whole image is valid");
    std::size_t compared = 0;
    std::size_t hits = 0;
    std::size_t rays = 0;
    std::size_t ray_hits = 0;
    for (std::size_t k = 0; k < all.size(); ++k) {
        const Section& s = all[k];
        const nfsu::TrackContactWorld region = world_of(build(region_image(s)));
        check(region.valid, std::string("the ") + s.name + " region alone is valid");
        if (!region.valid || !whole.valid) {
            continue;
        }
        // A lattice of columns over the section's cells, off the cell edges
        // and on them, from above everything and from between layers.
        for (float x = s.min0 - 0.5F; x <= s.max0 + 0.5F; x += 1.75F) {
            for (float z = s.min1 - 0.5F; z <= s.max1 + 0.5F; z += 1.75F) {
                for (float top : {150.0F, -15.0F, 55.0F}) {
                    nfsu::TrackContact rc;
                    nfsu::TrackContact wc;
                    const auto rs = column(region, x, z, top, 0.0F, 300.0F, rc);
                    const auto ws = column(whole, x, z, top, 0.0F, 300.0F, wc);
                    ++compared;
                    check(rs == ws, std::string(s.name) + ": the same status at (" +
                                        text(x) + ", " + text(z) + ", " + text(top) + ")");
                    if (rs == nfsu::TrackContactStatus::hit &&
                        ws == nfsu::TrackContactStatus::hit) {
                        ++hits;
                        check(same(rc, base[k], wc),
                              std::string(s.name) + ": the same contact at (" + text(x) +
                                  ", " + text(z) + ", " + text(top) + ")");
                    }
                    // Issue #39's ray over the whole image, on every column.
                    const float origin[3] = {x, z, top};
                    const float down[3] = {0.0F, 0.0F, -1.0F};
                    nfsu::TrackCollisionHit ray;
                    const bool got = nfsu::query_track_collision_ray(
                        whole.collision, origin, down, 300.0F, ray);
                    ++rays;
                    if (got) {
                        ++ray_hits;
                        check(ws == nfsu::TrackContactStatus::hit &&
                                  ray.polygon == wc.polygon &&
                                  ray.triangle == wc.triangle &&
                                  ray.distance == wc.distance &&
                                  ray.point[0] == wc.point[0] &&
                                  ray.point[1] == wc.point[1] &&
                                  ray.point[2] == wc.point[2],
                              std::string(s.name) +
                                  ": the ray query of issue #39 agrees at (" + text(x) +
                                  ", " + text(z) + ", " + text(top) + ")");
                    } else {
                        check(ws != nfsu::TrackContactStatus::hit,
                              std::string(s.name) +
                                  ": where the ray of issue #39 meets nothing the column "
                                  "meets nothing at (" +
                                  text(x) + ", " + text(z) + ", " + text(top) + ")");
                    }
                }
            }
        }
        // Sweeps across the section in both horizontal directions and on
        // the diagonals, at several heights.
        const float mid0 = 0.5F * (s.min0 + s.max0);
        const float mid1 = 0.5F * (s.min1 + s.max1);
        for (float h : {-15.0F, 0.0F, 3.0F, 51.0F, 65.0F, 90.0F}) {
            for (float offset = -14.0F; offset <= 14.0F; offset += 3.5F) {
                const float sweeps[4][6] = {
                    {s.min0 - 1.0F, mid1 + offset, h, s.max0 + 1.0F, mid1 + offset, h},
                    {s.max0 + 1.0F, mid1 + offset, h, s.min0 - 1.0F, mid1 + offset, h},
                    {mid0 + offset, s.min1 - 1.0F, h, mid0 + offset, s.max1 + 1.0F, h},
                    {s.min0, s.min1, h + offset, s.max0, s.max1, h - offset},
                };
                for (const auto& sw : sweeps) {
                    nfsu::TrackContact rc;
                    nfsu::TrackContact wc;
                    const auto rs = segment(region, sw, sw + 3, rc);
                    const auto ws = segment(whole, sw, sw + 3, wc);
                    ++compared;
                    check(rs == ws, std::string(s.name) + ": the same sweep status");
                    if (rs == nfsu::TrackContactStatus::hit &&
                        ws == nfsu::TrackContactStatus::hit) {
                        ++hits;
                        check(same(rc, base[k], wc),
                              std::string(s.name) + ": the same sweep contact");
                    }
                }
            }
        }
    }
    // The lattice has to have measured something: a comparison of two
    // queries that were both refused, or both missed everywhere, would pass
    // this test while proving nothing.
    check(compared > 3000, "region against whole compared " + std::to_string(compared) +
                               " queries");
    check(hits > 1000, "region against whole compared " + std::to_string(hits) +
                           " contacts");
    check(rays > 1500 && ray_hits > 500 && ray_hits < rays,
          "the ray of issue #39 was compared on " + std::to_string(rays) +
              " columns, " + std::to_string(ray_hits) + " of them hits");
}

// ---------------------------------------------------------------------
// Footprints: which cells a segment meets
// ---------------------------------------------------------------------

// Would catch (review F1): a segment's cells chosen by its bounding box,
// which answers clear - airborne - for a diagonal sweep that passes outside
// every cell, where the contract says outside. And (review N5): a cap that
// counts the cells of the box rather than the cells the segment meets,
// across the root's quadrants and inside one node of the tree.
void test_footprints() {
    std::vector<std::size_t> base;
    const nfsu::TrackContactWorld world = world_of(build(whole_image(sections(), base)));
    nfsu::TrackContact c;

    // The origin section's four cells end at (32, 32) and nothing lies
    // beyond that corner. This sweep crosses the corner's diagonal 3 units
    // outside it: its bounding box reaches into cell (16, 16), the segment
    // itself meets no cell, and neither end nor the midpoint has one.
    const float a[3] = {30.0F, 40.0F, 3.0F};
    const float b[3] = {40.0F, 30.0F, 3.0F};
    check(segment(world, a, b, c) == nfsu::TrackContactStatus::outside,
          "a sweep past the corner of every cell is outside, not clear");
    for (const float xz : {30.0F, 35.0F, 40.0F}) {
        check(column(world, xz, 70.0F - xz, 3.0F, 10.0F, 10.0F, c) ==
                  nfsu::TrackContactStatus::outside,
              "a column on that sweep at px " + text(xz) + " is outside");
    }
    // Through the corner itself: the closed square is met and nothing on it
    // is, so the index covers the sweep and it is clear. A quarter unit
    // further out it is outside again.
    const float t0[3] = {28.0F, 36.0F, 3.0F};
    const float t1[3] = {36.0F, 28.0F, 3.0F};
    check(segment(world, t0, t1, c) == nfsu::TrackContactStatus::clear,
          "a sweep through a cell's corner meets the cell and is clear");
    const float u0[3] = {28.0F, 36.25F, 3.0F};
    const float u1[3] = {36.25F, 28.0F, 3.0F};
    check(segment(world, u0, u1, c) == nfsu::TrackContactStatus::outside,
          "a quarter unit past the corner the sweep is outside");

    // A sweep down the origin slab's anti-diagonal side: its bounding box
    // covers all four origin cells, the segment meets three, since every
    // point of it has px + pz = -20 and cell (16, 16) starts at 0. The cap
    // counts the three.
    const float d0[3] = {-30.0F, 10.0F, 3.0F};
    const float d1[3] = {10.0F, -30.0F, -2.0F};
    nfsu::TrackContactQueryLimits cap;
    cap.max_cells = 3;
    check(nfsu::query_track_contact_segment(world, d0, d1, c, cap) ==
                  nfsu::TrackContactStatus::hit &&
              c.polygon == base[3] && near(c.point[2], 0.5F, 1e-5F),
          "three cells met under a cap of three run, though the box covers four");
    cap.max_cells = 2;
    check(nfsu::query_track_contact_segment(world, d0, d1, c, cap) ==
              nfsu::TrackContactStatus::refused,
          "three cells met over a cap of two refuse");

    // The same inside one node of the tree, whose four cells the walk
    // reaches together: a slab over cells (16, 16), (48, 16), (16, 48) and
    // (48, 48), and a sweep with px + pz = 44 throughout. Its bounding box
    // reaches cell (48, 48), which starts at px + pz = 64; the segment meets
    // the other three.
    Image block;
    block.polygons.push_back(flat(2.0F, 2.0F, 62.0F, 62.0F, 1.0F));
    block.cells.push_back(cell(16.0F, 16.0F, {0}));
    block.cells.push_back(cell(48.0F, 16.0F, {0}));
    block.cells.push_back(cell(16.0F, 48.0F, {0}));
    block.cells.push_back(cell(48.0F, 48.0F, {0}));
    const nfsu::TrackContactWorld four = world_of(build(block));
    const float f0[3] = {40.0F, 4.0F, 3.0F};
    const float f1[3] = {4.0F, 40.0F, -1.0F};
    cap.max_cells = 3;
    check(four.valid &&
              nfsu::query_track_contact_segment(four, f0, f1, c, cap) ==
                  nfsu::TrackContactStatus::hit &&
              c.polygon == 0 && near(c.point[2], 1.0F, 1e-5F),
          "inside one node, three cells met under a cap of three run");
    cap.max_cells = 2;
    check(nfsu::query_track_contact_segment(four, f0, f1, c, cap) ==
              nfsu::TrackContactStatus::refused,
          "inside one node, three cells met over a cap of two refuse");
}

// ---------------------------------------------------------------------
// Windings and edges
// ---------------------------------------------------------------------

// Would catch (review F3, M2): the road test reading the signed vertical of
// the normal rather than its magnitude, which turns road wound the other
// way into wall. (Review N2, M1): the threshold made exclusive, which turns
// a slope of exactly 60 degrees into wall. And (review F3, M11): the #39
// arithmetic's closed bound on a triangle's outer edge made open, which
// drops a deck's own edge and answers with the road beneath it.
void test_windings_and_edges() {
    Image image;
    image.polygons.push_back(flat(2.0F, 2.0F, 30.0F, 30.0F, 0.0F)); // 0
    // 16 units on a side, so every step of the arithmetic is exact.
    image.polygons.push_back(flat(8.0F, 8.0F, 24.0F, 24.0F, 10.0F)); // 1
    image.polygons.push_back(flat_clockwise(-30.0F, 2.0F, -2.0F, 30.0F, 3.0F)); // 2
    image.cells.push_back(cell(16.0F, 16.0F, {0, 1}));
    image.cells.push_back(cell(-16.0F, 16.0F, {2}));
    const nfsu::TrackContactWorld world = world_of(build(image));
    check(world.valid, "the winding and edge image is valid");
    if (!world.valid) {
        return;
    }
    check(world.counts.road_triangles == 6 && world.counts.wall_triangles == 0,
          "both windings are road: got road " +
              std::to_string(world.counts.road_triangles) + " wall " +
              std::to_string(world.counts.wall_triangles));
    const nfsu::TrackContactTriangle& down = world.triangles[2 * 2];
    check(down.normal[2] == -1.0F,
          "the clockwise quad's own normal points down: got " + text(down.normal[2]));
    check(down.kind == nfsu::TrackContactKind::road && down.up == 1.0F,
          "a road quad wound the other way is road with up 1, got up " + text(down.up));
    nfsu::TrackContact c;
    check(column(world, -16.0F, 16.0F, 10.0F, 0.0F, 20.0F, c) ==
                  nfsu::TrackContactStatus::hit &&
              c.polygon == 2 && c.kind == nfsu::TrackContactKind::road &&
              c.up == 1.0F && c.normal[2] == 1.0F && c.distance == 7.0F,
          "met from above, the clockwise road is road facing up");

    // A slope of exactly 60 degrees in float: rising 94.61328125 over
    // 54.625, 0.375 deep, both triangles' normals have |py| of exactly 0.5,
    // found by a search over the format's grid. The threshold is inclusive,
    // so both are road.
    Image steep;
    steep.polygons.push_back(ramp(2.0F, 2.0F, 56.625F, 2.375F, 10.0F, 104.61328125F));
    steep.cells.push_back(cell(16.0F, 16.0F, {0}));
    steep.cells.push_back(cell(48.0F, 16.0F, {0}));
    const nfsu::TrackContactWorld sixty = world_of(build(steep));
    check(sixty.valid && sixty.triangles.size() == 2 && sixty.triangles[0].up == 0.5F &&
              sixty.triangles[1].up == 0.5F,
          "the 60-degree ramp's up is exactly 0.5");
    check(sixty.valid && sixty.counts.road_triangles == 2,
          "a slope exactly at the threshold is road: got road " +
              std::to_string(sixty.counts.road_triangles) + " wall " +
              std::to_string(sixty.counts.wall_triangles));

    // The deck's edge px = 24 is triangle 0's (c1, c2) edge, the one the
    // #39 arithmetic bounds by u + v <= 1, and at pz 12 u + v is exactly 1.
    const float top[3] = {24.0F, 12.0F, 20.0F};
    const float fall[3] = {0.0F, 0.0F, -1.0F};
    nfsu::TrackCollisionHit ray;
    check(nfsu::query_track_collision_ray(world.collision, top, fall, 40.0F, ray) &&
              ray.polygon == 1 && ray.triangle == 0 && ray.distance == 10.0F,
          "the ray of issue #39 meets the deck on its own edge");
    check(column(world, 24.0F, 12.0F, 20.0F, 0.0F, 40.0F, c) ==
                  nfsu::TrackContactStatus::hit &&
              c.polygon == 1 && c.triangle == 0 && c.distance == 10.0F &&
              c.point[2] == 10.0F,
          "a column on the deck's own edge meets the deck, not the road beneath: "
          "got polygon " + std::to_string(c.polygon) + " at " + text(c.distance));
}

// Would catch (review F2): a column exactly on the seam between two road
// polygons answered clear, which issue #60 would read as airborne, and a
// column on a polygon's own outer edge answered clear. Both are points the
// #39 arithmetic misses: it rounds each barycentric on its own, so a point
// exactly on an edge can round outside every triangle that has it. The
// geometry below was found by a search over quads on the format's own
// 1/8-unit grid, and the first checks prove the #39 ray really misses it;
// if a compiler change ever lets the ray meet it, those checks say so
// rather than the rest passing for the wrong reason.
//
// Would also catch (review G1) the fallback's distance and its closed ends
// going wrong: a contact exactly at a segment's start dropped (W2, t <= 0),
// one exactly at its end dropped (W3, along >= max), and the distance
// weighting the wrong corner's height (W6). On a flat quad every corner's
// sheared height is the same, so no weighting can move the answer; the
// sloped seam at the end of this test is where it shows. And (review 3482,
// G2) the fallback's weight on corner a - the u term of its distance (U1)
// and u's share of its determinant (U2) - going wrong: that weight is 0 on
// every (b,c) edge, so only a contact on an (a,b) or (c,a) edge, such as a
// wall's own diagonal, can show it.
void test_edges_the_ray_misses() {
    const float py = 5.08984375F;
    const float first[4][2] = {{39.875F, 16.25F}, {60.75F, 9.125F},
                               {55.75F, 14.875F}, {43.375F, 23.0F}};
    const float second[4][2] = {{59.125F, 17.0F}, {56.625F, 22.25F},
                                {55.75F, 14.875F}, {60.75F, 9.125F}};
    Image seam;
    seam.polygons.push_back(flat_quad(first, py)); // 0
    seam.polygons.push_back(flat_quad(second, py)); // 1
    seam.cells.push_back(cell(48.0F, 16.0F, {0, 1}));
    const nfsu::TrackContactWorld world = world_of(build(seam));
    check(world.valid && world.counts.road_triangles == 4, "the seam image is valid");
    if (!world.valid) {
        return;
    }
    const float fall[3] = {0.0F, 0.0F, -1.0F};
    nfsu::TrackCollisionHit ray;
    nfsu::TrackContact c;

    // (60.203125, 9.75390625) is exactly 7/64 of the way along the shared
    // edge from (60.75, 9.125) to (55.75, 14.875): triangle 0's (c1, c2)
    // edge in the first quad and triangle 1's (c2, c3) edge in the second.
    const float on_seam[3] = {60.203125F, 9.75390625F, 1000.0F};
    check(!nfsu::query_track_collision_ray(world.collision, on_seam, fall, 2000.0F, ray),
          "the ray of issue #39 misses this seam from both sides");
    check(column(world, on_seam[0], on_seam[1], 1000.0F, 0.0F, 2000.0F, c) ==
                  nfsu::TrackContactStatus::hit &&
              c.kind == nfsu::TrackContactKind::road && c.normal[0] == 0.0F &&
              c.normal[1] == 0.0F && c.normal[2] == 1.0F &&
              near(c.point[2], py, 1e-4F) && near(c.distance, 1000.0F - py, 1e-3F),
          "a column exactly on the seam meets the road");
    // A short sweep through the seam, from above and from below: the normal
    // faces each start, which is the watertight test's determinant sign.
    const float above[3] = {on_seam[0], on_seam[1], py + 0.5F};
    const float below[3] = {on_seam[0], on_seam[1], py - 0.5F};
    check(segment(world, above, below, c) == nfsu::TrackContactStatus::hit &&
              c.normal[2] == 1.0F && near(c.distance, 0.5F, 1e-5F),
          "a sweep down through the seam meets it facing up");
    check(segment(world, below, above, c) == nfsu::TrackContactStatus::hit &&
              c.normal[2] == -1.0F && near(c.distance, 0.5F, 1e-5F),
          "a sweep up through the seam meets it facing down");
    // A sweep that starts exactly on the seam - a wheel resting on the road
    // - and one whose drop ends exactly on it. Both ends are closed, so both
    // meet it: the first at distance 0, the second at its full length. The
    // #39 ray misses both along the same line, so these are the fallback's.
    const float on[3] = {on_seam[0], on_seam[1], py};
    const float under[3] = {on_seam[0], on_seam[1], py - 1.0F};
    const float over[3] = {on_seam[0], on_seam[1], py + 1.0F};
    check(!nfsu::query_track_collision_ray(world.collision, on, fall, 1.0F, ray) &&
              !nfsu::query_track_collision_ray(world.collision, over, fall, 1.0F, ray),
          "the ray of issue #39 misses the seam from its surface and from 1 above");
    // Each query runs before its check, so that the message reports this
    // query's answer and not the previous one's.
    const nfsu::TrackContactStatus starts = segment(world, on, under, c);
    check(starts == nfsu::TrackContactStatus::hit &&
              c.kind == nfsu::TrackContactKind::road && c.distance == 0.0F &&
              c.point[2] == py && c.normal[2] == 1.0F,
          "a sweep that starts exactly on the seam meets it at distance 0: got " +
              answer(starts, c));
    const nfsu::TrackContactStatus ends = segment(world, over, on, c);
    check(ends == nfsu::TrackContactStatus::hit &&
              c.kind == nfsu::TrackContactKind::road && c.distance == 1.0F &&
              c.point[2] == py && c.normal[2] == 1.0F,
          "a sweep that ends exactly on the seam meets it at its end: got " +
              answer(ends, c));
    // Off the seam by a hundredth, each side is its own polygon, as the ray
    // agrees: the seam is closed without either polygon growing.
    check(column(world, 60.2F, 9.74F, 1000.0F, 0.0F, 2000.0F, c) ==
                  nfsu::TrackContactStatus::hit && c.polygon == 0,
          "just off the seam toward the first quad, the first quad: got polygon " +
              std::to_string(c.polygon));
    check(column(world, 60.2F, 9.77F, 1000.0F, 0.0F, 2000.0F, c) ==
                  nfsu::TrackContactStatus::hit && c.polygon == 1,
          "just off the seam toward the second quad, the second quad: got polygon " +
              std::to_string(c.polygon));

    // A polygon's outer edge with nothing beyond it: the first quad alone,
    // and a point 3/64 of the way along its (c1, c2) edge from c1.
    const float outer[4][2] = {{52.625F, 21.25F}, {44.375F, 19.25F},
                               {35.25F, 16.125F}, {61.25F, 4.375F}};
    Image alone;
    alone.polygons.push_back(flat_quad(outer, py));
    alone.cells.push_back(cell(48.0F, 16.0F, {0}));
    const nfsu::TrackContactWorld edge = world_of(build(alone));
    check(edge.valid, "the outer-edge image is valid");
    if (!edge.valid) {
        return;
    }
    const float on_edge[3] = {43.947265625F, 19.103515625F, 1000.0F};
    check(!nfsu::query_track_collision_ray(edge.collision, on_edge, fall, 2000.0F, ray),
          "the ray of issue #39 misses this outer edge");
    check(column(edge, on_edge[0], on_edge[1], 1000.0F, 0.0F, 2000.0F, c) ==
                  nfsu::TrackContactStatus::hit &&
              c.polygon == 0 && c.kind == nfsu::TrackContactKind::road,
          "a column exactly on a polygon's outer edge meets it");
    check(column(edge, 43.9F, 19.3F, 1000.0F, 0.0F, 2000.0F, c) ==
              nfsu::TrackContactStatus::clear,
          "just beyond that edge the column is clear");

    // A seam between two SLOPED quads, both on one plane rising about 0.2
    // across and falling 0.1 along, snapped to the format's 1/256: every
    // corner at its own height, so the fallback's distance has to weight
    // each corner's height by the right edge function. The shared edge runs
    // from (50.75, 29.75, 7.12109375) to (55.625, 20, 8.7578125), triangle
    // 0's (c1, c2) edge in the first quad and triangle 1's (c2, c3) edge in
    // the second, and (51.283203125, 28.68359375) is exactly 7/64 of the way
    // along it, so the road there is at exactly
    // 7.12109375 + 7/64 * 1.63671875 = 7.30010986328125.
    const Corner sloped_first[4] = {{60.75F, 31.0F, 4.421875F},
                                    {50.75F, 29.75F, 7.12109375F},
                                    {55.625F, 20.0F, 8.7578125F},
                                    {60.625F, 20.0F, 7.5859375F}};
    const Corner sloped_second[4] = {{39.25F, 27.25F, 10.53125F},
                                     {34.125F, 5.375F, 17.96875F},
                                     {55.625F, 20.0F, 8.7578125F},
                                     {50.75F, 29.75F, 7.12109375F}};
    Image slope;
    slope.polygons.push_back(polygon(sloped_first, 4)); // 0
    slope.polygons.push_back(polygon(sloped_second, 7)); // 1
    slope.cells.push_back(cell(48.0F, 16.0F, {0, 1}));
    const nfsu::TrackContactWorld sloped = world_of(build(slope));
    check(sloped.valid && sloped.counts.road_triangles == 4, "the sloped seam image is valid");
    if (!sloped.valid) {
        return;
    }
    const float road_height = 7.30010986328125F;
    const float on_slope[3] = {51.283203125F, 28.68359375F, 40.0F};
    check(!nfsu::query_track_collision_ray(sloped.collision, on_slope, fall, 80.0F, ray),
          "the ray of issue #39 misses this sloped seam from both sides");
    const nfsu::TrackContactStatus down_slope =
        column(sloped, on_slope[0], on_slope[1], on_slope[2], 0.0F, 80.0F, c);
    check(down_slope == nfsu::TrackContactStatus::hit && c.polygon <= 1 &&
              c.kind == nfsu::TrackContactKind::road &&
              near(c.point[2], road_height, 1e-4F) &&
              near(c.distance, 40.0F - road_height, 1e-4F) && c.normal[2] > 0.9F,
          "a column exactly on a sloped seam meets the road at its own height: got " +
              answer(down_slope, c) + ", want height " + text(road_height));
    // The same point by a sweep that ends exactly on it: the height is the
    // end of the segment, and the distance its whole length.
    const float slope_top[3] = {on_slope[0], on_slope[1], road_height + 2.0F};
    const float slope_foot[3] = {on_slope[0], on_slope[1], road_height};
    const nfsu::TrackContactStatus onto_slope = segment(sloped, slope_top, slope_foot, c);
    check(onto_slope == nfsu::TrackContactStatus::hit && near(c.distance, 2.0F, 1e-4F) &&
              near(c.point[2], road_height, 1e-4F),
          "a sweep ending exactly on the sloped seam meets it at its end: got " +
              answer(onto_slope, c));

    // A wall's own (0,2) diagonal, met by an oblique sweep (review 3482,
    // G2). The seams above put the fallback's contact on a triangle's (b,c)
    // edge, where the edge function u is exactly 0, so the fallback's weight
    // on corner a - the u term of its distance and u's share of its
    // determinant - never counted. The diagonal is triangle 0's (c,a) edge
    // and triangle 1's (a,b) edge, where u is not 0, and on a wall the
    // diagonal runs from a bottom corner to a top one. Each sweep
    // below passes through a point exactly on the diagonal at the middle of
    // its length, so the contact is exactly half its length along. Both were
    // found by a search over walls on the format's grid, and each first
    // proves the #39 ray misses it along the same line, as the contact
    // query computes that line.
    struct DiagonalSweep {
        const char* name;
        float x0, z0, x1, z1, py0, py1;
        float point[3];
        float half[3];
    };
    const DiagonalSweep diagonal_sweeps[] = {
        // 60/64 along the diagonal from (60.5, 23.375, 14.87890625) to
        // (37.375, 19.75, 18.14453125).
        {"a sweep rising through a wall", 60.5F, 23.375F, 37.375F, 19.75F, 14.87890625F, 18.14453125F,
         {38.8203125F, 19.9765625F, 17.9404296875F}, {-1.0F, 1.375F, 1.75F}},
        // 20/64 along the diagonal from (35.125, 15.375, 13.9453125) to
        // (55.375, 23.375, 21.6484375).
        {"a sweep nearly level through a wall", 35.125F, 15.375F, 55.375F, 23.375F, 13.9453125F,
         21.6484375F, {41.453125F, 17.875F, 16.3525390625F}, {1.25F, -3.375F, 0.25F}},
    };
    for (const DiagonalSweep& sweep : diagonal_sweeps) {
        Image wall_image;
        wall_image.polygons.push_back(pier(sweep.x0, sweep.z0, sweep.x1, sweep.z1, sweep.py0,
                                           sweep.py1));
        wall_image.cells.push_back(cell(48.0F, 16.0F, {0}));
        const nfsu::TrackContactWorld wall = world_of(build(wall_image));
        check(wall.valid && wall.counts.wall_triangles == 2,
              std::string(sweep.name) + ": the wall image is valid");
        if (!wall.valid) {
            continue;
        }
        const float from[3] = {sweep.point[0] - sweep.half[0], sweep.point[1] - sweep.half[1],
                               sweep.point[2] - sweep.half[2]};
        const float to[3] = {sweep.point[0] + sweep.half[0], sweep.point[1] + sweep.half[1],
                             sweep.point[2] + sweep.half[2]};
        const float delta[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
        const float length =
            std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
        const float line[3] = {delta[0] / length, delta[1] / length, delta[2] / length};
        check(!nfsu::query_track_collision_ray(wall.collision, from, line, length, ray),
              std::string(sweep.name) +
                  ": the ray of issue #39 misses the wall's diagonal from both triangles");
        const double half_length = std::sqrt(
            static_cast<double>(sweep.half[0]) * sweep.half[0] +
            static_cast<double>(sweep.half[1]) * sweep.half[1] +
            static_cast<double>(sweep.half[2]) * sweep.half[2]);
        const nfsu::TrackContactStatus through = segment(wall, from, to, c);
        check(through == nfsu::TrackContactStatus::hit && c.polygon == 0 &&
                  c.kind == nfsu::TrackContactKind::wall &&
                  near(c.distance, static_cast<float>(half_length), 1e-4F) &&
                  near(c.point[0], sweep.point[0], 1e-4F) &&
                  near(c.point[1], sweep.point[1], 1e-4F) &&
                  near(c.point[2], sweep.point[2], 1e-4F) &&
                  c.normal[0] * line[0] + c.normal[1] * line[1] + c.normal[2] * line[2] < 0.0F,
              std::string(sweep.name) + ": a sweep through the wall's diagonal meets it " +
                  text(static_cast<float>(half_length)) + " along, facing it: got " +
                  answer(through, c));
    }
}

// ---------------------------------------------------------------------
// The nearest surface, whichever test finds it (issue #167)
// ---------------------------------------------------------------------

// Would catch (issue #167) the first contact layer's rule, which kept every
// hit of the #39 arithmetic and tested the watertight way only when that
// arithmetic met nothing: a sweep through a wall quad's own (0,2) diagonal,
// which the #39 arithmetic misses, passed through the wall to a barrier
// behind it; a column exactly on a raised polygon's outer edge answered the
// road beneath; an oblique sweep exactly through an outer edge answered
// clear, because the float shear rounded the edge away; and where the #39
// arithmetic accepted a triangle with two equal corners - which no contact
// ever meets - the sweep answered clear although it meets the same
// polygon's other triangle exactly on its outer edge. Each case first checks
// its premise against the ray of issue #39, so that a compiler change that
// moves the premise says so rather than the rest passing for the wrong
// reason. The geometry is on the format's own grid, found by a search, and
// every segment below has both ends exact in float with its midpoint exactly
// on the edge it is aimed at, so the contact is exactly half its length
// along.
void test_nearest_surface_wins() {
    const float fall[3] = {0.0F, 0.0F, -1.0F};
    nfsu::TrackCollisionHit ray;
    nfsu::TrackContact c;

    // The line the contact query computes, for the premise checks.
    const auto line_of = [](const float from[3], const float to[3], float line[3]) {
        const float delta[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
        const float length =
            std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            line[axis] = delta[axis] / length;
        }
        return length;
    };
    const auto half_length = [](const float half[3]) {
        return static_cast<float>(std::sqrt(static_cast<double>(half[0]) * half[0] +
                                            static_cast<double>(half[1]) * half[1] +
                                            static_cast<double>(half[2]) * half[2]));
    };

    // A wall quad's own (0,2) diagonal in front of a barrier: the two walls
    // and sweeps of test_edges_the_ray_misses, each with a barrier across
    // the second half of the sweep. The #39 arithmetic misses the wall and
    // meets the barrier, which the first contact layer reported: the sweep
    // passed through the wall, as on retail TRACKB1107, 1210 and 1003.
    struct WallAndBarrier {
        const char* name;
        float x0, z0, x1, z1, py0, py1;
        float point[3];
        float half[3];
        float barrier[4];
    };
    const WallAndBarrier walls[] = {
        {"a sweep rising through a wall", 60.5F, 23.375F, 37.375F, 19.75F, 14.87890625F, 18.14453125F,
         {38.8203125F, 19.9765625F, 17.9404296875F}, {-1.0F, 1.375F, 1.75F},
         {35.625F, 18.625F, 41.0F, 22.625F}},
        {"a sweep nearly level through a wall", 35.125F, 15.375F, 55.375F, 23.375F, 13.9453125F,
         21.6484375F, {41.453125F, 17.875F, 16.3525390625F}, {1.25F, -3.375F, 0.25F},
         {38.75F, 14.875F, 45.5F, 17.375F}},
    };
    for (const WallAndBarrier& wall : walls) {
        Image image;
        image.polygons.push_back(pier(wall.x0, wall.z0, wall.x1, wall.z1, wall.py0, wall.py1)); // 0
        image.polygons.push_back(
            barrier(wall.barrier[0], wall.barrier[1], wall.barrier[2], wall.barrier[3])); // 1
        image.cells.push_back(cell(48.0F, 16.0F, {0, 1}));
        const nfsu::TrackContactWorld world = world_of(build(image));
        check(world.valid && world.counts.wall_triangles == 4,
              std::string(wall.name) + ": the wall and barrier image is valid");
        if (!world.valid) {
            continue;
        }
        const float from[3] = {wall.point[0] - wall.half[0], wall.point[1] - wall.half[1],
                               wall.point[2] - wall.half[2]};
        const float to[3] = {wall.point[0] + wall.half[0], wall.point[1] + wall.half[1],
                             wall.point[2] + wall.half[2]};
        float line[3];
        const float length = line_of(from, to, line);
        const float half = half_length(wall.half);
        check(nfsu::query_track_collision_ray(world.collision, from, line, length, ray) &&
                  ray.polygon == 1 && ray.distance > half + 0.25F,
              std::string(wall.name) +
                  ": the ray of issue #39 misses the wall and meets the barrier behind it");
        const nfsu::TrackContactStatus through = segment(world, from, to, c);
        check(through == nfsu::TrackContactStatus::hit && c.polygon == 0 &&
                  c.kind == nfsu::TrackContactKind::wall && near(c.distance, half, 1e-4F) &&
                  near(c.point[0], wall.point[0], 1e-4F) &&
                  near(c.point[1], wall.point[1], 1e-4F) &&
                  near(c.point[2], wall.point[2], 1e-4F) &&
                  c.normal[0] * line[0] + c.normal[1] * line[1] + c.normal[2] * line[2] < 0.0F,
              std::string(wall.name) + ": the wall is met " + text(half) +
                  " along, not the barrier behind it: got " + answer(through, c));
    }

    // A column exactly on a raised polygon's outer edge, a quarter unit
    // above a road: the outer-edge quad of test_edges_the_ray_misses and the
    // same point 3/64 along its (c1, c2) edge, where the #39 arithmetic
    // misses the quad and meets the road beneath. The first contact layer
    // answered the road, as retail columns at (1690, 486) and (979.5, 281.5)
    // of TRACKB1003 did.
    const float py = 5.08984375F;
    const float outer[4][2] = {{52.625F, 21.25F}, {44.375F, 19.25F},
                               {35.25F, 16.125F}, {61.25F, 4.375F}};
    Image step;
    step.polygons.push_back(flat_quad(outer, py));                             // 0
    step.polygons.push_back(flat(34.0F, 2.0F, 62.0F, 30.0F, py - 0.25F));      // 1
    step.cells.push_back(cell(48.0F, 16.0F, {0, 1}));
    const nfsu::TrackContactWorld stepped = world_of(build(step));
    check(stepped.valid && stepped.counts.road_triangles == 4, "the step image is valid");
    if (stepped.valid) {
        const float on_edge[3] = {43.947265625F, 19.103515625F, 1000.0F};
        check(nfsu::query_track_collision_ray(stepped.collision, on_edge, fall, 2000.0F, ray) &&
                  ray.polygon == 1,
              "the ray of issue #39 misses the raised edge and meets the road beneath");
        const nfsu::TrackContactStatus onto =
            column(stepped, on_edge[0], on_edge[1], 1000.0F, 0.0F, 2000.0F, c);
        check(onto == nfsu::TrackContactStatus::hit && c.polygon == 0 &&
                  near(c.point[2], py, 1e-4F) && near(c.distance, 1000.0F - py, 1e-3F) &&
                  c.normal[2] == 1.0F,
              "a column exactly on a raised polygon's outer edge meets it, not the road a "
              "quarter unit beneath: got " +
                  answer(onto, c));
        check(column(stepped, 43.9F, 19.3F, 1000.0F, 0.0F, 2000.0F, c) ==
                      nfsu::TrackContactStatus::hit &&
                  c.polygon == 1,
              "just beyond that edge the column meets the road: got polygon " +
                  std::to_string(c.polygon));
    }

    // An oblique sweep exactly through a sloped quad's outer edge: 3/4 of
    // the way along its (c0, c1) edge, from (46.625, 20.875, 12.45703125) to
    // (51.125, 20.875, 10.53515625), which is (50, 20.875, 11.015625). The
    // #39 arithmetic misses it, and the first contact layer's watertight
    // test, which sheared the corners in float, rounded the edge away.
    const Corner sloped[4] = {{46.625F, 20.875F, 12.45703125F},
                              {51.125F, 20.875F, 10.53515625F},
                              {53.375F, 5.75F, 7.0390625F},
                              {49.5F, 9.625F, 9.74609375F}};
    Image tilted;
    tilted.polygons.push_back(polygon(sloped, 7)); // 0
    tilted.cells.push_back(cell(48.0F, 16.0F, {0}));
    const nfsu::TrackContactWorld edge = world_of(build(tilted));
    check(edge.valid && edge.counts.road_triangles == 2, "the sloped outer-edge image is valid");
    if (edge.valid) {
        const float point[3] = {50.0F, 20.875F, 11.015625F};
        const float half_vector[3] = {-2.0F, -1.25F, -0.828125F};
        const float from[3] = {point[0] - half_vector[0], point[1] - half_vector[1],
                               point[2] - half_vector[2]};
        const float to[3] = {point[0] + half_vector[0], point[1] + half_vector[1],
                             point[2] + half_vector[2]};
        float line[3];
        const float length = line_of(from, to, line);
        const float half = half_length(half_vector);
        check(!nfsu::query_track_collision_ray(edge.collision, from, line, length, ray),
              "the ray of issue #39 misses the sloped quad's outer edge");
        const nfsu::TrackContactStatus through = segment(edge, from, to, c);
        check(through == nfsu::TrackContactStatus::hit && c.polygon == 0 && c.triangle == 0 &&
                  near(c.distance, half, 1e-4F) && near(c.point[0], point[0], 1e-4F) &&
                  near(c.point[1], point[1], 1e-4F) && near(c.point[2], point[2], 1e-4F) &&
                  c.normal[0] * line[0] + c.normal[1] * line[1] + c.normal[2] * line[2] < 0.0F,
              "an oblique sweep exactly through a sloped quad's outer edge meets it " +
                  text(half) + " along: got " + answer(through, c));
    }

    // A triangle stored in a quad slot, (c0, c1, c2, c2), so that triangle 1
    // is degenerate and lies along triangle 0's outer edge (c2, c0). The sweep
    // passes exactly through that edge 25/64 of the way from c2, at
    // (42.5, 20.40625, 6.5). The #39 arithmetic accepts the degenerate
    // triangle, whose determinant is zero only in exact arithmetic, and
    // misses triangle 0; the first contact layer answered clear, as retail
    // TRACKB1003 polygon 536 did.
    const Corner corners[4] = {{53.75F, 29.625F, 5.54296875F},
                               {51.0F, 23.0F, 6.83203125F},
                               {35.75F, 14.875F, 7.07421875F},
                               {35.75F, 14.875F, 7.07421875F}};
    Image slivered;
    slivered.polygons.push_back(polygon(corners, 5)); // 0
    slivered.cells.push_back(cell(48.0F, 16.0F, {0}));
    const nfsu::TrackContactWorld one = world_of(build(slivered));
    check(one.valid && one.counts.road_triangles == 1 && one.counts.degenerate_triangles == 1,
          "the sliver image is valid");
    if (one.valid) {
        const float point[3] = {42.5F, 20.40625F, 6.5F};
        const float half_vector[3] = {1.59375F, -0.796875F, -2.25F};
        const float from[3] = {point[0] - half_vector[0], point[1] - half_vector[1],
                               point[2] - half_vector[2]};
        const float to[3] = {point[0] + half_vector[0], point[1] + half_vector[1],
                             point[2] + half_vector[2]};
        float line[3];
        const float length = line_of(from, to, line);
        const float half = half_length(half_vector);
        check(nfsu::query_track_collision_ray(one.collision, from, line, length, ray) &&
                  ray.polygon == 0 && ray.triangle == 1,
              "the ray of issue #39 meets the sliver's degenerate triangle");
        const nfsu::TrackContactStatus through = segment(one, from, to, c);
        check(through == nfsu::TrackContactStatus::hit && c.polygon == 0 && c.triangle == 0 &&
                  c.kind == nfsu::TrackContactKind::road && near(c.distance, half, 1e-4F) &&
                  near(c.point[0], point[0], 1e-4F) && near(c.point[1], point[1], 1e-4F) &&
                  near(c.point[2], point[2], 1e-4F) &&
                  c.normal[0] * line[0] + c.normal[1] * line[1] + c.normal[2] * line[2] < 0.0F,
              "a sweep exactly through a sliver's outer edge meets its triangle 0 " + text(half) +
                  " along, where the #39 ray met the degenerate one: got " + answer(through, c));
    }
}

// Would catch the breaks of the conservative test and of the rule that
// sweeps over the retail tracks show and the cases above do not (issue
// #167's mutation sweep, in docs/TRACK_COLLISION.md): the plane of a
// triangle met behind the segment's start, or beyond its end, taken as a
// contact at the start or the end; a tie between two triangles the
// conservative test meets at one point broken by the order the cell names
// them in; a surface nearer by less than the margin replacing the #39
// answer; a sign the rounding could hide taken as certain, which loses an
// exact contact on an edge; the segment taken as from plus its float
// difference; and the #39 arithmetic's closed bound on a triangle's (b,c)
// edge made open, which the conservative test would then answer in its own
// bits. The geometry of the last four was found by a search over the
// format's grid that compared each break with the code, and each checks
// its premise against the ray of issue #39 first.
void test_conservative_bounds() {
    const float fall[3] = {0.0F, 0.0F, -1.0F};
    nfsu::TrackCollisionHit ray;
    nfsu::TrackContact c;
    const auto line_of = [](const float from[3], const float to[3], float line[3]) {
        const float delta[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
        const float length =
            std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            line[axis] = delta[axis] / length;
        }
        return length;
    };

    // Level sweeps at py 70 inside the bounding box of the hill's 45-degree
    // ramp, which rises from py 60 at px 1634 to 76 at px 1650: one starts
    // under the ramp and moves away from it, so the ramp's plane lies 2
    // units behind its start; the other stays above the ramp and stops 4
    // units short of where its line would cross it. Neither meets anything.
    std::vector<std::size_t> base;
    const nfsu::TrackContactWorld world = world_of(build(whole_image(sections(), base)));
    const float behind0[3] = {1646.0F, 720.0F, 70.0F};
    const float behind1[3] = {1649.0F, 720.0F, 70.0F};
    const nfsu::TrackContactStatus behind = segment(world, behind0, behind1, c);
    check(behind == nfsu::TrackContactStatus::clear,
          "a sweep moving away from a ramp behind its start is clear: got " + answer(behind, c));
    const float short0[3] = {1636.0F, 720.0F, 70.0F};
    const float short1[3] = {1640.0F, 720.0F, 70.0F};
    const nfsu::TrackContactStatus stops = segment(world, short0, short1, c);
    check(stops == nfsu::TrackContactStatus::clear,
          "a sweep that stops short of a ramp is clear: got " + answer(stops, c));

    // The flat seam of test_edges_the_ray_misses, with the cell naming the
    // second quad first: the #39 arithmetic misses both, the conservative
    // test meets both at the same distance, and the lower polygon wins.
    const float py = 5.08984375F;
    const float first[4][2] = {{39.875F, 16.25F}, {60.75F, 9.125F},
                               {55.75F, 14.875F}, {43.375F, 23.0F}};
    const float second[4][2] = {{59.125F, 17.0F}, {56.625F, 22.25F},
                                {55.75F, 14.875F}, {60.75F, 9.125F}};
    Image reversed;
    reversed.polygons.push_back(flat_quad(first, py));  // 0
    reversed.polygons.push_back(flat_quad(second, py)); // 1
    reversed.cells.push_back(cell(48.0F, 16.0F, {1, 0}));
    const nfsu::TrackContactWorld seam = world_of(build(reversed));
    const float on_seam[3] = {60.203125F, 9.75390625F, 1000.0F};
    check(seam.valid, "the reversed seam image is valid");
    if (seam.valid) {
        check(!nfsu::query_track_collision_ray(seam.collision, on_seam, fall, 2000.0F, ray),
              "the ray of issue #39 misses the seam, the cell naming the second quad first");
        const nfsu::TrackContactStatus tie =
            column(seam, on_seam[0], on_seam[1], 1000.0F, 0.0F, 2000.0F, c);
        check(tie == nfsu::TrackContactStatus::hit && c.polygon == 0,
              "a tie on the seam goes to the lower polygon, whichever the cell names first: "
              "got " + answer(tie, c));
    }

    // A column down 2000 units exactly through the outer edge of a sloped
    // quad, 39/64 of the way from (45.75, 9.625, 12.4375) to (52, 9.875,
    // 12.02734375), where the edge is at 12.18756103515625: 1/16384 above a
    // road at 12.1875. That is less than 2^-23 of the column's length, a
    // difference its float distance cannot express, so the #39 answer, the
    // road, stands bit for bit.
    const Corner raised[4] = {{45.75F, 9.625F, 12.4375F},
                              {52.0F, 9.875F, 12.02734375F},
                              {52.0F, 25.375F, 5.73046875F},
                              {45.75F, 25.375F, 9.2265625F}};
    Image tie_image;
    tie_image.polygons.push_back(polygon(raised, 5));                        // 0
    tie_image.polygons.push_back(flat(36.0F, 2.0F, 62.0F, 30.0F, 12.1875F)); // 1
    tie_image.cells.push_back(cell(48.0F, 16.0F, {0, 1}));
    const nfsu::TrackContactWorld near_tie = world_of(build(tie_image));
    check(near_tie.valid, "the near-tie image is valid");
    if (near_tie.valid) {
        const float top[3] = {49.55859375F, 9.77734375F, 1000.0F};
        const bool ray_met =
            nfsu::query_track_collision_ray(near_tie.collision, top, fall, 2000.0F, ray);
        check(ray_met && ray.polygon == 1,
              "the ray of issue #39 misses the raised edge and meets the road 1/16384 below");
        const nfsu::TrackContactStatus kept =
            column(near_tie, top[0], top[1], 1000.0F, 0.0F, 2000.0F, c);
        check(ray_met && kept == nfsu::TrackContactStatus::hit && c.polygon == ray.polygon &&
                  c.triangle == ray.triangle && c.distance == ray.distance &&
                  c.point[2] == ray.point[2],
              "a surface nearer by less than a rounding of the column's length leaves the "
              "#39 answer: got " + answer(kept, c));
    }

    // A sweep near the origin, where a float carries bits down to 2^-22,
    // exactly through a sloped quad's outer edge 3/64 of the way from
    // (1.625, 1.625, 1.609375) to (3.375, 1.875, 2.73046875): both ends are
    // exact floats, written in hex, and the midpoint (437/256, 419/256,
    // 27229/16384) is on the edge. In double the edge's own determinant
    // comes out a rounding away from 0, and only its rounding bound keeps
    // the contact.
    const Corner origin_corners[4] = {{1.625F, 1.625F, 1.609375F},
                                      {3.375F, 1.875F, 2.73046875F},
                                      {3.25F, 4.375F, 1.921875F},
                                      {1.75F, 4.375F, 1.32421875F}};
    Image origin_image;
    origin_image.polygons.push_back(polygon(origin_corners, 5));
    origin_image.cells.push_back(cell(16.0F, 16.0F, {0}));
    const nfsu::TrackContactWorld dense = world_of(build(origin_image));
    check(dense.valid, "the origin image is valid");
    if (dense.valid) {
        const float from[3] = {0x1.4b282ep+1F, 0x1.3849dep+1F, 0x1.58364p-1F};
        const float to[3] = {0x1.a75f48p-1F, 0x1.aad888p-1F, 0x1.53667p+1F};
        const float mid[3] = {1.70703125F, 1.63671875F, 1.66192626953125F};
        float line[3];
        const float length = line_of(from, to, line);
        check(!nfsu::query_track_collision_ray(dense.collision, from, line, length, ray),
              "the ray of issue #39 misses the edge near the origin");
        const nfsu::TrackContactStatus met = segment(dense, from, to, c);
        check(met == nfsu::TrackContactStatus::hit && c.polygon == 0 && c.triangle == 0 &&
                  near(c.distance, 0.5F * length, 1e-5F) && near(c.point[0], mid[0], 1e-5F) &&
                  near(c.point[1], mid[1], 1e-5F) && near(c.point[2], mid[2], 1e-5F),
              "a sweep with many bits exactly through an outer edge meets it at its midpoint: "
              "got " + answer(met, c));
    }

    // The case above, mirrored through px = 0 (review comment 3802 on issue
    // #173, F1): every corner, both sweep ends and the cell centre negate
    // px. In conservative_triangle every double product and sum that px
    // enters is then negated exactly, so u, v, w, start, det and end all
    // change sign bit for bit; the edge's rounding residual was negative
    // with det > 0 above and is now positive with det < 0, the branch
    // mutant B5 (certain_sign's positive branch ignoring its bound,
    // src/track_contact.cpp:243) changes. Issue #39's Moller-Trumbore sees
    // its own intermediates negated in pairs - its det and inv change sign,
    // its u, v and t come out bit-identical - so the ray still misses, and
    // the query still depends on the conservative test alone.
    const Corner mirrored_corners[4] = {{-1.625F, 1.625F, 1.609375F},
                                        {-3.375F, 1.875F, 2.73046875F},
                                        {-3.25F, 4.375F, 1.921875F},
                                        {-1.75F, 4.375F, 1.32421875F}};
    Image mirrored_image;
    mirrored_image.polygons.push_back(polygon(mirrored_corners, 5));
    mirrored_image.cells.push_back(cell(-16.0F, 16.0F, {0}));
    const nfsu::TrackContactWorld mirrored = world_of(build(mirrored_image));
    check(mirrored.valid, "the mirrored origin image is valid");
    if (mirrored.valid) {
        const float from[3] = {-0x1.4b282ep+1F, 0x1.3849dep+1F, 0x1.58364p-1F};
        const float to[3] = {-0x1.a75f48p-1F, 0x1.aad888p-1F, 0x1.53667p+1F};
        const float mid[3] = {-1.70703125F, 1.63671875F, 1.66192626953125F};
        float line[3];
        const float length = line_of(from, to, line);
        check(!nfsu::query_track_collision_ray(mirrored.collision, from, line, length, ray),
              "the ray of issue #39 misses the mirrored edge near the origin too");
        const nfsu::TrackContactStatus met = segment(mirrored, from, to, c);
        check(met == nfsu::TrackContactStatus::hit && c.polygon == 0 && c.triangle == 0 &&
                  near(c.distance, 0.5F * length, 1e-5F) && near(c.point[0], mid[0], 1e-5F) &&
                  near(c.point[1], mid[1], 1e-5F) && near(c.point[2], mid[2], 1e-5F),
              "the mirror of a sweep with many bits exactly through an outer edge still "
              "meets it at its midpoint: got " + answer(met, c));
    }

    // A sweep between two rounded floats, from 4 units before a point 51/64
    // of the way along a sloped quad's outer edge to 4 units beyond it:
    // to - from is not a float, and the segment between the two ends meets
    // the edge, in exact arithmetic, 3.99999934 along, which the #39
    // arithmetic misses. Taking the difference in float moves the segment
    // off it.
    const Corner rounded_corners[4] = {{56.0F, 4.0F, 7.1015625F},
                                       {63.5F, 4.0F, 9.0390625F},
                                       {63.625F, 11.75F, 11.42578125F},
                                       {56.125F, 11.75F, 6.5546875F}};
    Image rounded_image;
    rounded_image.polygons.push_back(polygon(rounded_corners, 5));
    rounded_image.cells.push_back(cell(48.0F, 16.0F, {0}));
    const nfsu::TrackContactWorld rounded = world_of(build(rounded_image));
    check(rounded.valid, "the rounded-ends image is valid");
    if (rounded.valid) {
        const float from[3] = {0x1.fc42fcp+5F, 0x1.dbf0ccp+0F, 0x1.695d46p+2F};
        const float to[3] = {0x1.e35d04p+5F, 0x1.8903ccp+2F, 0x1.74a15cp+3F};
        float line[3];
        const float length = line_of(from, to, line);
        check(!nfsu::query_track_collision_ray(rounded.collision, from, line, length, ray),
              "the ray of issue #39 misses the edge the rounded sweep meets");
        const nfsu::TrackContactStatus met = segment(rounded, from, to, c);
        check(met == nfsu::TrackContactStatus::hit && c.polygon == 0 && c.triangle == 0 &&
                  near(c.distance, 3.99999934F, 1e-5F) && near(c.point[0], 61.9765625F, 1e-4F) &&
                  near(c.point[1], 4.0F, 1e-4F),
              "a sweep between two rounded ends meets the edge it crosses exactly: got " +
                  answer(met, c));
    }

    // An oblique sweep through a sloped quad's (c1, c2) edge - triangle 0's
    // (b, c) edge, which the #39 arithmetic bounds by u + v <= 1 - at
    // (42.875, 3227/512, 46681/4096), which the #39 ray meets 3.29846692
    // along where the exact distance is 3.29846668. The contact is that hit
    // bit for bit.
    const Corner bc_corners[4] = {{32.625F, 2.625F, 8.64453125F},
                                  {42.875F, 2.75F, 12.8203125F},
                                  {42.875F, 16.125F, 7.4609375F},
                                  {32.875F, 16.125F, 9.3046875F}};
    Image bc_image;
    bc_image.polygons.push_back(polygon(bc_corners, 5));
    bc_image.cells.push_back(cell(48.0F, 16.0F, {0}));
    const nfsu::TrackContactWorld bc = world_of(build(bc_image));
    check(bc.valid, "the (b, c) edge image is valid");
    if (bc.valid) {
        const float from[3] = {43.75F, 9.240234375F, 12.615478515625F};
        const float to[3] = {42.0F, 3.365234375F, 10.177978515625F};
        float line[3];
        const float length = line_of(from, to, line);
        const bool ray_met = nfsu::query_track_collision_ray(bc.collision, from, line, length, ray);
        check(ray_met && ray.polygon == 0 && ray.triangle == 0,
              "the ray of issue #39 meets the quad on its (c1, c2) edge");
        const nfsu::TrackContactStatus met = segment(bc, from, to, c);
        check(ray_met && met == nfsu::TrackContactStatus::hit && c.polygon == 0 &&
                  c.triangle == 0 && c.distance == ray.distance && c.point[0] == ray.point[0] &&
                  c.point[1] == ray.point[1] && c.point[2] == ray.point[2],
              "on a (b, c) edge the #39 ray meets, the contact is that hit bit for bit: got " +
                  answer(met, c) + ", the ray " + text(ray.distance));
    }
}

// ---------------------------------------------------------------------
// Refusals and bounds
// ---------------------------------------------------------------------

// Would catch: a query over a world that failed its checks, a non-finite or
// zero-length segment searched, a negative probe accepted, a cap that
// truncates instead of refusing, and a hand-built world indexing past its
// triangle table.
void test_refusals() {
    std::vector<std::size_t> base;
    const nfsu::TrackContactWorld world = world_of(build(whole_image(sections(), base)));
    nfsu::TrackContact c;
    c.polygon = 777;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float a[3] = {1000.0F, -400.0F, 51.0F};
    const float b[3] = {1000.0F, -420.0F, 51.0F};

    const nfsu::TrackContactWorld empty;
    check(nfsu::query_track_contact_segment(empty, a, b, c) ==
              nfsu::TrackContactStatus::refused,
          "an empty world refuses");
    check(nfsu::query_track_contact_segment(world, nullptr, b, c) ==
                  nfsu::TrackContactStatus::refused &&
              nfsu::query_track_contact_segment(world, a, nullptr, c) ==
                  nfsu::TrackContactStatus::refused,
          "a null endpoint refuses");
    const float bad[3][3] = {{nan, -400.0F, 51.0F}, {1000.0F, inf, 51.0F},
                             {1000.0F, -400.0F, -inf}};
    for (const auto& p : bad) {
        check(nfsu::query_track_contact_segment(world, p, b, c) ==
                      nfsu::TrackContactStatus::refused &&
                  nfsu::query_track_contact_segment(world, a, p, c) ==
                      nfsu::TrackContactStatus::refused,
              "a non-finite endpoint refuses");
    }
    check(nfsu::query_track_contact_segment(world, a, a, c) ==
              nfsu::TrackContactStatus::refused,
          "a segment of zero length refuses");
    // Two endpoints so far apart that the difference overflows.
    const float far0[3] = {-3.0e38F, 0.0F, 0.0F};
    const float far1[3] = {3.0e38F, 0.0F, 0.0F};
    check(nfsu::query_track_contact_segment(world, far0, far1, c) ==
              nfsu::TrackContactStatus::refused,
          "a segment whose length overflows refuses");
    const float point[3] = {980.0F, -395.0F, 52.25F};
    check(nfsu::query_track_contact_point(world, nullptr, 1.0F, 1.0F, c) ==
              nfsu::TrackContactStatus::refused,
          "a null point refuses");
    check(nfsu::query_track_contact_point(world, point, -1.0F, 10.0F, c) ==
                  nfsu::TrackContactStatus::refused &&
              nfsu::query_track_contact_point(world, point, 1.0F, -10.0F, c) ==
                  nfsu::TrackContactStatus::refused,
          "a negative rise or drop refuses");
    check(nfsu::query_track_contact_point(world, point, 0.0F, 0.0F, c) ==
              nfsu::TrackContactStatus::refused,
          "a column of no height refuses");
    check(nfsu::query_track_contact_point(world, point, nan, 1.0F, c) ==
                  nfsu::TrackContactStatus::refused &&
              nfsu::query_track_contact_point(world, point, 1.0F, inf, c) ==
                  nfsu::TrackContactStatus::refused,
          "a non-finite rise or drop refuses");
    check(c.polygon == 777, "a refusal leaves the contact untouched");

    // The length cap: a sweep one unit longer than allowed is refused, and
    // exactly at the cap it runs.
    nfsu::TrackContactQueryLimits limits;
    limits.max_length = 20.0F;
    check(nfsu::query_track_contact_segment(world, a, b, c, limits) ==
              nfsu::TrackContactStatus::hit,
          "a sweep exactly at the length cap runs");
    limits.max_length = 19.0F;
    check(nfsu::query_track_contact_segment(world, a, b, c, limits) ==
              nfsu::TrackContactStatus::refused,
          "a sweep one unit over the length cap refuses");
    limits.max_length = 0.0F;
    check(nfsu::query_track_contact_segment(world, a, b, c, limits) ==
              nfsu::TrackContactStatus::refused,
          "a length cap of zero refuses");
    limits.max_length = nan;
    check(nfsu::query_track_contact_segment(world, a, b, c, limits) ==
              nfsu::TrackContactStatus::refused,
          "a length cap that is not a number refuses");

    // The cell cap: the origin slab sits in four cells, so a sweep across
    // all four needs four and is refused with three, not answered from
    // three.
    const float o0[3] = {-20.0F, -20.0F, 3.0F};
    const float o1[3] = {20.0F, 20.0F, 3.0F};
    nfsu::TrackContactQueryLimits cells;
    cells.max_cells = 4;
    check(nfsu::query_track_contact_segment(world, o0, o1, c, cells) ==
              nfsu::TrackContactStatus::hit,
          "four cells within a cap of four run");
    cells.max_cells = 3;
    check(nfsu::query_track_contact_segment(world, o0, o1, c, cells) ==
              nfsu::TrackContactStatus::refused,
          "four cells over a cap of three refuse rather than truncate");
    cells.max_cells = 0;
    check(nfsu::query_track_contact_segment(world, o0, o1, c, cells) ==
              nfsu::TrackContactStatus::refused,
          "a cell cap of zero refuses");

    // A world assembled by hand whose triangle table does not match.
    nfsu::TrackContactWorld forged = world;
    forged.triangles.pop_back();
    check(nfsu::query_track_contact_segment(forged, a, b, c) ==
              nfsu::TrackContactStatus::refused,
          "a triangle table that does not match the polygons refuses");
    nfsu::TrackContactWorld unmarked = world;
    unmarked.valid = false;
    check(nfsu::query_track_contact_segment(unmarked, a, b, c) ==
              nfsu::TrackContactStatus::refused,
          "a world not marked valid refuses");

    // Names, for the probe's output.
    check(std::string(nfsu::track_contact_status_name(nfsu::TrackContactStatus::hit)) ==
                  "hit" &&
              std::string(nfsu::track_contact_status_name(
                  nfsu::TrackContactStatus::clear)) == "clear" &&
              std::string(nfsu::track_contact_status_name(
                  nfsu::TrackContactStatus::outside)) == "outside" &&
              std::string(nfsu::track_contact_status_name(
                  nfsu::TrackContactStatus::refused)) == "refused",
          "status names");
    check(std::string(nfsu::track_contact_kind_name(nfsu::TrackContactKind::road)) ==
                  "road" &&
              std::string(nfsu::track_contact_kind_name(nfsu::TrackContactKind::wall)) ==
                  "wall",
          "kind names");
}

// ---------------------------------------------------------------------
// Memory: the ceiling and the plan of issue #52
// ---------------------------------------------------------------------

nfsu::MemoryBudget budget(std::uint64_t persistent, std::uint64_t streaming) {
    nfsu::MemoryBudget out;
    out.capacity[static_cast<std::size_t>(nfsu::MemoryCategory::persistent)] = persistent;
    out.capacity[static_cast<std::size_t>(nfsu::MemoryCategory::streaming)] = streaming;
    out.capacity[static_cast<std::size_t>(nfsu::MemoryCategory::transient)] = 4096;
    out.capacity[static_cast<std::size_t>(nfsu::MemoryCategory::gpu)] = 4096;
    return out;
}

// Would catch: the resident arrays placed in the wrong arena, the load
// buffers left behind, a refusal that leaves half a world placed, and a
// ceiling that wraps instead of saturating.
void test_memory_plan() {
    std::vector<std::size_t> base;
    nfsu::TrackContactWorld world = world_of(build(whole_image(sections(), base)));
    world.memory.packed_bytes = 3000;
    world.memory.image_bytes = 5000;
    const nfsu::TrackContactMemory& m = world.memory;

    nfsu::MemoryPlan plan;
    std::string error;
    check(plan.create(budget(1U << 20, 1U << 20), error), "a plan: " + error);
    nfsu::TrackContactPlacement placement;
    check(nfsu::place_track_contact_memory(m, plan, placement),
          "the world fits a megabyte");
    check(placement.placed && placement.refused_part.empty() &&
              placement.refusal == nfsu::ArenaRefusal::none,
          "placed with no refusal");
    check(placement.persistent_used_bytes == m.resident_bytes,
          "the persistent arena holds exactly the resident bytes: " +
              std::to_string(placement.persistent_used_bytes));
    check(placement.persistent_extent >= placement.persistent_used_bytes &&
              placement.persistent_extent < placement.persistent_used_bytes + 5 * 16,
          "the resident extent adds at most the alignment gaps");
    check(placement.streaming_peak_bytes == 8000,
          "both load buffers were held at once in the streaming arena");
    const auto stats = plan.stats();
    check(stats[static_cast<std::size_t>(nfsu::MemoryCategory::streaming)].used_bytes == 0,
          "the load buffers are released when the load is over");
    check(stats[static_cast<std::size_t>(nfsu::MemoryCategory::persistent)]
                  .live_allocations == 5,
          "five resident allocations stay");
    check(stats[static_cast<std::size_t>(nfsu::MemoryCategory::transient)].used_bytes == 0,
          "nothing is placed in the frame arena");
    check(placement.persistent_capacity == (1U << 20) &&
              placement.streaming_capacity == (1U << 20),
          "the capacities are the plan's");

    // One byte short of the resident arrays: refused at the last one, and
    // nothing of the world stays in either arena.
    nfsu::MemoryPlan tight;
    check(tight.create(budget(m.resident_bytes - 1, 1U << 20), error), "a tight plan");
    check(!nfsu::place_track_contact_memory(m, tight, placement),
          "one byte short is refused");
    check(!placement.placed && placement.refused_part == "triangles" &&
              placement.refusal == nfsu::ArenaRefusal::over_budget,
          "the refusal names the triangle table: got " + placement.refused_part);
    const auto after = tight.stats();
    check(after[static_cast<std::size_t>(nfsu::MemoryCategory::persistent)].used_bytes == 0 &&
              after[static_cast<std::size_t>(nfsu::MemoryCategory::streaming)].used_bytes == 0,
          "a refused world leaves nothing placed");
    // A streaming arena too small for the image refuses before anything
    // persistent is placed.
    nfsu::MemoryPlan small_stream;
    check(small_stream.create(budget(1U << 20, 7000), error), "a small streaming plan");
    check(!nfsu::place_track_contact_memory(m, small_stream, placement) &&
              placement.refused_part == "image",
          "an image the streaming arena cannot hold is refused by name");
    check(small_stream.stats()[static_cast<std::size_t>(nfsu::MemoryCategory::persistent)]
                  .used_bytes == 0,
          "nothing persistent was placed for it");

    // A world built in memory was never loaded, so it has no load buffers:
    // placing it asks the streaming arena for nothing, rather than for two
    // requests of zero bytes, which an arena refuses.
    const nfsu::TrackContactWorld in_memory =
        world_of(build(whole_image(sections(), base)));
    nfsu::MemoryPlan fresh;
    check(fresh.create(budget(1U << 20, 1U << 20), error), "a plan for a world built in memory");
    check(nfsu::place_track_contact_memory(in_memory.memory, fresh, placement) &&
              placement.placed && placement.streaming_peak_bytes == 0 &&
              placement.persistent_used_bytes == in_memory.memory.resident_bytes,
          "a world with no load buffers is placed without asking the streaming arena");

    // The ceiling: every array at its limit, and saturation.
    nfsu::TrackCollisionLimits limits;
    const std::uint64_t expected =
        limits.max_polygons * sizeof(nfsu::TrackCollisionPolygon) +
        limits.max_cells * sizeof(nfsu::TrackCollisionCell) +
        limits.max_indices * sizeof(std::uint32_t) +
        limits.max_tree_nodes * sizeof(nfsu::TrackCollisionTreeNode) +
        limits.max_polygons * 2 * sizeof(nfsu::TrackContactTriangle);
    check(nfsu::track_contact_resident_ceiling(limits) == expected,
          "the ceiling is every array at its limit: got " +
              std::to_string(nfsu::track_contact_resident_ceiling(limits)));
    limits.max_indices = std::numeric_limits<std::uint64_t>::max() / 2;
    check(nfsu::track_contact_resident_ceiling(limits) ==
              std::numeric_limits<std::uint64_t>::max(),
          "a ceiling past 2^64 saturates");
}

// ---------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------

// Would catch: the renderer map written as a reflection, a component
// swapped, or an inverse that is not one.
void test_conversions() {
    const float slot[3] = {1.5F, -2.25F, 7.0F};
    float render[3];
    nfsu::track_slot_to_render(slot, render);
    check(render[0] == 1.5F && render[1] == 7.0F && render[2] == 2.25F,
          "slot (px, pz, py) is render (px, py, -pz)");
    float back[3];
    nfsu::track_render_to_slot(render, back);
    check(back[0] == slot[0] && back[1] == slot[1] && back[2] == slot[2],
          "the inverse gives the slot back exactly");
    // The images of the three slot axes: a proper rotation has determinant
    // +1, so a normal keeps its length and its handedness.
    const float e[3][3] = {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    float m[3][3];
    for (std::size_t i = 0; i < 3; ++i) {
        nfsu::track_slot_to_render(e[i], m[i]);
    }
    const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                      m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                      m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    check(det == 1.0F, "the map is a rotation, determinant " + text(det));
    // The up of the collision frame is the renderer's +y.
    const float up[3] = {0.0F, 0.0F, 1.0F};
    nfsu::track_slot_to_render(up, render);
    check(render[0] == 0.0F && render[1] == 1.0F && render[2] == 0.0F,
          "a road normal is the renderer's +y");
}

// ---------------------------------------------------------------------
// Loading from a file
// ---------------------------------------------------------------------

// A JDLZ stream of literals only, which the decoder's contract allows:
// a flag byte of zeros before every eight literals, and one flags2 byte at
// the start that no literal consumes.
Bytes jdlz_literals(const Bytes& payload) {
    Bytes out = {'J', 'D', 'L', 'Z', 0x02, 0x10, 0x00, 0x00};
    append_u32(out, static_cast<std::uint32_t>(payload.size()));
    append_u32(out, 0);
    out.push_back(0x00);
    out.push_back(0x00);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        if (i != 0 && i % 8 == 0) {
            out.push_back(0x00);
        }
        out.push_back(payload[i]);
    }
    write_u32(out, 12, static_cast<std::uint32_t>(out.size()));
    return out;
}

bool write_file(const fs::path& path, const Bytes& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

// Would catch: the load figures left at zero or swapped, the peak that
// forgets the buffers, and a load that answers differently from the same
// bytes parsed in memory.
void test_load() {
    std::vector<std::size_t> base;
    const Bytes image = build(whole_image(sections(), base));
    const Bytes packed = jdlz_literals(image);
    std::error_code code;
    const fs::path directory = fs::temp_directory_path(code) / unique_fixture("load");
    fs::remove_all(directory, code);
    fs::create_directories(directory, code);
    check(!code, "the fixture directory is created");
    check(write_file(directory / "TRACKB9001.lzc", packed), "the packed fixture is written");
    check(write_file(directory / "TRACKB9002.bin", image), "the raw fixture is written");

    const nfsu::TrackContactWorld memory = world_of(image);
    const nfsu::TrackContactWorld loaded =
        nfsu::load_track_contact_world(directory / "TRACKB9001.lzc");
    check(loaded.valid, "the packed fixture loads");
    check(loaded.memory.packed_bytes == packed.size() &&
              loaded.memory.image_bytes == image.size(),
          "the load buffers are the file and the image: got " +
              std::to_string(loaded.memory.packed_bytes) + " and " +
              std::to_string(loaded.memory.image_bytes));
    const std::uint64_t parsing = packed.size() + image.size() +
                                  loaded.memory.resident_bytes -
                                  loaded.memory.triangle_bytes;
    check(loaded.memory.load_peak_bytes ==
              std::max<std::uint64_t>(parsing, loaded.memory.resident_bytes),
          "the peak is the buffers and the parsed arrays together");
    check(loaded.memory.resident_bytes == memory.memory.resident_bytes,
          "a loaded world holds what the same bytes parsed in memory hold");
    const nfsu::TrackContactWorld raw =
        nfsu::load_track_contact_world(directory / "TRACKB9002.bin");
    check(raw.valid && raw.memory.packed_bytes == image.size() &&
              raw.memory.image_bytes == image.size(),
          "an uncompressed image loads as it stands");
    nfsu::TrackContact a;
    nfsu::TrackContact b;
    check(column(loaded, 980.0F, -395.0F, 60.0F, 0.0F, 20.0F, a) ==
                  nfsu::TrackContactStatus::hit &&
              column(memory, 980.0F, -395.0F, 60.0F, 0.0F, 20.0F, b) ==
                  nfsu::TrackContactStatus::hit &&
              same(a, 0, b),
          "a loaded world answers as the in-memory one");

    const nfsu::TrackContactWorld missing =
        nfsu::load_track_contact_world(directory / "TRACKB9003.lzc");
    check(!missing.valid && !missing.collision.issues.empty(),
          "a missing file is reported, not loaded");
    check(column(missing, 980.0F, -395.0F, 60.0F, 0.0F, 20.0F, a) ==
              nfsu::TrackContactStatus::refused,
          "a world that did not load refuses every query");
    fs::remove_all(directory, code);
}

// --write-fixture <image> <packed> <broken>: lays three files down for
// tests/track_contact_guards.cmake and runs no check - the four-section
// image uncompressed, the same image JDLZ-packed, and the image cut short.
// It never replaces a file.
bool write_fixture(const std::vector<fs::path>& paths) {
    for (const fs::path& path : paths) {
        std::error_code code;
        if (fs::exists(path, code)) {
            std::cerr << "error: --write-fixture " << path.string()
                      << " already exists; this command never replaces a file\n";
            return false;
        }
    }
    std::vector<std::size_t> base;
    const Bytes image = build(whole_image(sections(), base));
    Bytes broken = image;
    broken.resize(broken.size() - 3);
    const bool ok = write_file(paths[0], image) &&
                    write_file(paths[1], jdlz_literals(image)) &&
                    write_file(paths[2], broken);
    if (!ok) {
        std::cerr << "failed to write the fixture\n";
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[1]) == "--write-fixture") {
        // The build directory can sit under a path that is not ASCII, and
        // argv is in the active code page, so each path goes through the
        // conversion nfsu_probe uses rather than through fs::path(argv[k]).
        std::string reason;
        if (!nfsu::check_arguments(argc, argv, 2, reason)) {
            std::cerr << "error: " << reason << '\n';
            return 1;
        }
        return write_fixture({nfsu::argument_path(argv[2]), nfsu::argument_path(argv[3]),
                              nfsu::argument_path(argv[4])})
                   ? 0
                   : 1;
    }
    if (argc != 1) {
        std::cerr << "usage: nfsu_track_contact_tests [--write-fixture <image> "
                     "<packed> <broken>]\n";
        return 1;
    }
    test_world_build();
    test_point_queries();
    test_segment_queries();
    test_region_equals_whole();
    test_footprints();
    test_windings_and_edges();
    test_edges_the_ray_misses();
    test_nearest_surface_wins();
    test_conservative_bounds();
    test_refusals();
    test_memory_plan();
    test_conversions();
    test_load();
    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " check(s) failed\n";
        return 1;
    }
    std::cout << "all track contact tests passed (" << g_checks << " checks)\n";
    return 0;
}
