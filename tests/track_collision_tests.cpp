#include "nfsu/track_collision.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;

// The format's own numbers, written as literals on purpose. A fixture
// built out of the header's constants moves with them, so a mutation of a
// constant would change the image and the expectation at once and no
// assertion could see it. test_constants below is what ties the two
// together, in one place and deliberately.
constexpr std::size_t kAlignment = 16;
constexpr unsigned char kFiller = 0x11;
constexpr std::size_t kPolygonRecord = 32;
constexpr std::size_t kCellRecord = 16;
constexpr std::size_t kNodeRecord = 8;
constexpr std::int16_t kSaturation = 32512;
constexpr float kCellSquare = 32.0F;
// The JDLZ container refills a literal flag byte and a match flag
// byte before a step runs, so a decompressor that stops on a full
// output leaves at most that pair of bytes behind.
constexpr std::size_t kMaxUnusedPacked = 2;


int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
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

// Every refusal below names the rule it broke, not merely "invalid". A
// different rule firing first would leave the rule under test unexercised
// and the assertion still passing, which is the failure mode this form
// exists to prevent.
void check_refused(
    const nfsu::TrackCollisionReport& report,
    const std::string& text,
    const std::string& what) {
    check(!report.valid, what + ": the image must not be valid");
    check(has_text(report.issues, text),
          what + ": expected an issue mentioning \"" + text + "\"" +
              (report.issues.empty()
                   ? std::string(", got none")
                   : ", got \"" + report.issues[0].message + "\""));
}

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_i16(Bytes& bytes, std::int16_t value) {
    append_u16(bytes, static_cast<std::uint16_t>(value));
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

void write_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>(value & 0xFFU);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

void write_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[offset + static_cast<std::size_t>(i)] =
            static_cast<unsigned char>((value >> (8 * i)) & 0xFFU);
    }
}

void write_f32(Bytes& bytes, std::size_t offset, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32(bytes, offset, bits);
}

std::uint32_t read_u32(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

// ---------------------------------------------------------------------
// A JDLZ encoder, written to the decoder's contract so that a round trip
// exercises the decoder rather than a shared misreading.
// ---------------------------------------------------------------------

struct JdlzOp {
    bool match = false;
    // Literal byte, when match is false.
    unsigned char literal = 0;
    // Match, when match is true. near selects the 1..16 distance encoding.
    bool near = false;
    std::uint32_t length = 0;
    std::uint32_t distance = 0;
};

// Builds the flag byte whose bit k, counting from the least significant,
// is bits[first + k]; bits past the end are zero, which is safe because the
// decoder only reads a bit for a step the stream actually reaches.
unsigned char flag_byte(const std::vector<int>& bits, std::size_t first) {
    unsigned int value = 0;
    for (std::size_t k = 0; k < 8; ++k) {
        const std::size_t at = first + k;
        if (at < bits.size() && bits[at] != 0) {
            value |= 1U << k;
        }
    }
    return static_cast<unsigned char>(value);
}

Bytes encode_jdlz(const std::vector<JdlzOp>& ops, std::uint32_t unpacked_size) {
    std::vector<int> f1;
    std::vector<int> f2;
    for (const JdlzOp& op : ops) {
        f1.push_back(op.match ? 1 : 0);
        if (op.match) {
            f2.push_back(op.near ? 1 : 0);
        }
    }

    Bytes body;
    std::size_t f1_used = 0;
    std::size_t f2_used = 0;
    body.push_back(flag_byte(f1, 0));
    body.push_back(flag_byte(f2, 0));
    for (const JdlzOp& op : ops) {
        if (op.match) {
            unsigned int control = 0;
            unsigned int trailer = 0;
            if (op.near) {
                const std::uint32_t coded = op.length - 3U;
                control = ((coded >> 4U) & 0xF0U) | ((op.distance - 1U) & 0x0FU);
                trailer = coded & 0xFFU;
            } else {
                const std::uint32_t coded = op.distance - 0x11U;
                control = ((coded >> 3U) & 0xE0U) | ((op.length - 3U) & 0x1FU);
                trailer = coded & 0xFFU;
            }
            body.push_back(static_cast<unsigned char>(control));
            body.push_back(static_cast<unsigned char>(trailer));
            ++f2_used;
        } else {
            body.push_back(op.literal);
        }
        ++f1_used;
        if (f1_used % 8 == 0) {
            body.push_back(flag_byte(f1, f1_used));
        }
        if (op.match && f2_used % 8 == 0) {
            body.push_back(flag_byte(f2, f2_used));
        }
    }

    Bytes image;
    image.push_back('J');
    image.push_back('D');
    image.push_back('L');
    image.push_back('Z');
    image.push_back(2);
    image.push_back(0x10);
    image.push_back(0);
    image.push_back(0);
    append_u32(image, unpacked_size);
    append_u32(image, 0);
    image.insert(image.end(), body.begin(), body.end());
    write_u32(image, 12, static_cast<std::uint32_t>(image.size()));
    return image;
}

// ---------------------------------------------------------------------
// A synthetic collision image.
// ---------------------------------------------------------------------

struct Polygon {
    unsigned char unknown0[4] = {0, 0, 0, 0};
    // +4, the whole-unit part of the vertical. Every fixture below that
    // does not say otherwise leaves it zero, which is why it has to be
    // exercised deliberately: five retail records in six carry one.
    std::int16_t vertical_base = 0;
    std::uint16_t scratch = 0xFFFFU;
    std::int16_t slot0[4] = {0, 0, 0, 0};
    std::int16_t slot1[4] = {0, 0, 0, 0};
    std::int16_t slot2[4] = {0, 0, 0, 0};
};

// A flat quad centred on (x, z) in world units, half a side wide, at the
// given height, in perimeter order.
Polygon flat_quad(float x, float z, float half, float height) {
    Polygon polygon;
    const float xs[4] = {x - half, x + half, x + half, x - half};
    const float zs[4] = {z - half, z - half, z + half, z + half};
    for (std::size_t i = 0; i < 4; ++i) {
        polygon.slot0[i] = static_cast<std::int16_t>(std::lround(xs[i] * 8.0F));
        polygon.slot1[i] = static_cast<std::int16_t>(std::lround(zs[i] * 8.0F));
        polygon.slot2[i] = static_cast<std::int16_t>(std::lround(height * 256.0F));
    }
    return polygon;
}

// A vertical wall: a segment in the horizontal plane, saturated in the
// vertical, which is the shape 42466 retail polygons carry.
Polygon wall(float x0, float z0, float x1, float z1) {
    Polygon polygon;
    const float xs[4] = {x0, x0, x1, x1};
    const float zs[4] = {z0, z0, z1, z1};
    const std::int16_t ys[4] = {-kSaturation, kSaturation, kSaturation,
                                -kSaturation};
    for (std::size_t i = 0; i < 4; ++i) {
        polygon.slot0[i] = static_cast<std::int16_t>(std::lround(xs[i] * 8.0F));
        polygon.slot1[i] = static_cast<std::int16_t>(std::lround(zs[i] * 8.0F));
        polygon.slot2[i] = ys[i];
    }
    return polygon;
}

// A triangle stored in the quad slot, by repeating the last corner.
Polygon flat_triangle(float x, float z, float half, float height) {
    Polygon polygon;
    const float xs[4] = {x - half, x + half, x + half, x + half};
    const float zs[4] = {z - half, z - half, z + half, z + half};
    for (std::size_t i = 0; i < 4; ++i) {
        polygon.slot0[i] = static_cast<std::int16_t>(std::lround(xs[i] * 8.0F));
        polygon.slot1[i] = static_cast<std::int16_t>(std::lround(zs[i] * 8.0F));
        polygon.slot2[i] = static_cast<std::int16_t>(std::lround(height * 256.0F));
    }
    return polygon;
}

struct Cell {
    float center[2] = {0.0F, 0.0F};
    std::vector<std::uint16_t> polygons;
};

struct Image {
    std::vector<Polygon> polygons;
    // Cell 0 is the sentinel and is added by build_image().
    std::vector<Cell> cells;
    // Extra top-level chunks written before the five, to prove that the
    // parser walks past what it does not claim.
    bool with_unrelated_chunk = true;
    // Corruptions applied while the image is built rather than patched in
    // afterwards. Editing a payload length in place would move every chunk
    // after it out of step with the top-level walk and out of its 16-byte
    // alignment, so the parser would refuse the image for a rule other
    // than the one under test and the test would pass without exercising
    // it. Built in, everything after the corruption stays well formed.
    std::size_t tree_extra_bytes = 0;
    std::size_t cell_extra_bytes = 0;
    std::size_t polygon_extra_bytes = 0;
    std::size_t index_extra_bytes = 0;
    std::size_t bounds_extra_bytes = 0;
    // Nodes appended to the array that no parent names.
    std::size_t orphan_nodes = 0;
    // Indices appended past the padding the format allows.
    std::size_t spare_indices = 0;
};

// The quadrant path from the root square down to a leaf whose centre is
// (x, z). Every retail cell centre is an odd multiple of 16, which is
// exactly the set of centres this path can reach.
std::vector<std::size_t> cell_path(float x, float z) {
    std::vector<std::size_t> path;
    float cx = 0.0F;
    float cz = 0.0F;
    float half = nfsu::kCollisionRootHalfExtent;
    for (std::uint32_t depth = 0; depth < nfsu::kCollisionTreeDepth; ++depth) {
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

struct TreeNode {
    std::uint16_t entries[4] = {0, 0, 0, 0};
};

std::vector<TreeNode> build_tree(const std::vector<Cell>& cells) {
    std::vector<TreeNode> nodes(1);
    for (std::size_t i = 1; i < cells.size(); ++i) {
        const std::vector<std::size_t> path =
            cell_path(cells[i].center[0], cells[i].center[1]);
        std::size_t node = 0;
        for (std::size_t d = 0; d + 1 < path.size(); ++d) {
            std::uint16_t next = nodes[node].entries[path[d]];
            if (next == 0) {
                nodes.push_back(TreeNode{});
                next = static_cast<std::uint16_t>(nodes.size() - 1);
                nodes[node].entries[path[d]] = next;
            }
            node = next;
        }
        nodes[node].entries[path.back()] = static_cast<std::uint16_t>(i);
    }
    return nodes;
}

// Offsets of the pieces a mutation needs to reach.
struct Layout {
    std::size_t tree_chunk = 0;
    std::size_t tree_data = 0;
    std::size_t cell_chunk = 0;
    std::size_t cell_data = 0;
    std::size_t polygon_chunk = 0;
    std::size_t polygon_data = 0;
    std::size_t index_chunk = 0;
    std::size_t index_data = 0;
    std::size_t bounds_chunk = 0;
    std::size_t bounds_data = 0;
    std::size_t index_used = 0;
};

void pad_to_alignment(Bytes& bytes) {
    while (bytes.size() % kAlignment != 0) {
        bytes.push_back(kFiller);
    }
}

Bytes build_image(const Image& source, Layout& layout) {
    Image image = source;
    Cell sentinel;
    sentinel.center[0] = 999999.0F;
    sentinel.center[1] = 999999.0F;
    image.cells.insert(image.cells.begin(), sentinel);
    const std::vector<TreeNode> nodes = build_tree(image.cells);

    Bytes bytes;
    if (image.with_unrelated_chunk) {
        append_u32(bytes, 0x00000000U);
        append_u32(bytes, 8);
        for (int i = 0; i < 8; ++i) {
            bytes.push_back(0);
        }
    }

    // 0x00034130: the quadtree, at a 16-byte boundary.
    layout.tree_chunk = bytes.size();
    append_u32(bytes, nfsu::kCollisionTreeId);
    append_u32(bytes, 0);
    pad_to_alignment(bytes);
    layout.tree_data = bytes.size();
    for (const TreeNode& node : nodes) {
        for (std::size_t e = 0; e < 4; ++e) {
            append_u16(bytes, node.entries[e]);
        }
    }
    for (std::size_t i = 0; i < image.orphan_nodes; ++i) {
        for (std::size_t e = 0; e < 4; ++e) {
            append_u16(bytes, 0);
        }
    }
    bytes.insert(bytes.end(), image.tree_extra_bytes, 0);
    write_u32(bytes, layout.tree_chunk + 4,
              static_cast<std::uint32_t>(bytes.size() - layout.tree_chunk - 8));

    // 0x00034131: the cells, at a 16-byte boundary. The index runs are
    // laid out here and the index list itself is written from them.
    std::vector<std::uint16_t> indices;
    layout.cell_chunk = bytes.size();
    append_u32(bytes, nfsu::kCollisionCellListId);
    append_u32(bytes, 0);
    pad_to_alignment(bytes);
    layout.cell_data = bytes.size();
    for (const Cell& cell : image.cells) {
        append_f32(bytes, cell.center[0]);
        append_f32(bytes, cell.center[1]);
        append_u32(bytes, static_cast<std::uint32_t>(indices.size()));
        append_u32(bytes, static_cast<std::uint32_t>(cell.polygons.size()));
        indices.insert(indices.end(), cell.polygons.begin(), cell.polygons.end());
    }
    bytes.insert(bytes.end(), image.cell_extra_bytes, 0);
    write_u32(bytes, layout.cell_chunk + 4,
              static_cast<std::uint32_t>(bytes.size() - layout.cell_chunk - 8));

    // 0x00034132: the polygons, at the payload.
    layout.polygon_chunk = bytes.size();
    append_u32(bytes, nfsu::kCollisionPolygonListId);
    append_u32(bytes, static_cast<std::uint32_t>(
                          image.polygons.size() * kPolygonRecord +
                          image.polygon_extra_bytes));
    layout.polygon_data = bytes.size();
    for (const Polygon& polygon : image.polygons) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes.push_back(polygon.unknown0[i]);
        }
        append_i16(bytes, polygon.vertical_base);
        append_u16(bytes, polygon.scratch);
        for (std::size_t i = 0; i < 4; ++i) {
            append_i16(bytes, polygon.slot0[i]);
        }
        for (std::size_t i = 0; i < 4; ++i) {
            append_i16(bytes, polygon.slot1[i]);
        }
        for (std::size_t i = 0; i < 4; ++i) {
            append_i16(bytes, polygon.slot2[i]);
        }
    }
    bytes.insert(bytes.end(), image.polygon_extra_bytes, 0);

    // 0x00034133: the index list, at the payload, padded to a multiple of
    // four bytes the way retail pads it.
    layout.index_used = indices.size();
    layout.index_chunk = bytes.size();
    append_u32(bytes, nfsu::kCollisionIndexListId);
    append_u32(bytes, 0);
    layout.index_data = bytes.size();
    for (std::uint16_t index : indices) {
        append_u16(bytes, index);
    }
    if (indices.size() % 2 != 0) {
        append_u16(bytes, 0);
    }
    for (std::size_t i = 0; i < image.spare_indices; ++i) {
        append_u16(bytes, 0);
    }
    bytes.insert(bytes.end(), image.index_extra_bytes, 0);
    write_u32(bytes, layout.index_chunk + 4,
              static_cast<std::uint32_t>(bytes.size() - layout.index_chunk - 8));

    // 0x00034134: the bounds, at a 16-byte boundary.
    float low[3] = {0.0F, 0.0F, 0.0F};
    float high[3] = {0.0F, 0.0F, 0.0F};
    bool first = true;
    for (const Polygon& polygon : image.polygons) {
        for (std::size_t c = 0; c < 4; ++c) {
            // The same arithmetic speed.exe 0x0043FD20 does, spelled in
            // literals: the vertical carries the record's whole-unit base.
            const float point[3] = {
                static_cast<float>(polygon.slot0[c]) / 8.0F,
                static_cast<float>(polygon.slot1[c]) / 8.0F,
                static_cast<float>(polygon.vertical_base) +
                    static_cast<float>(polygon.slot2[c]) / 256.0F};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                low[axis] = first ? point[axis] : std::min(low[axis], point[axis]);
                high[axis] = first ? point[axis] : std::max(high[axis], point[axis]);
            }
            first = false;
        }
    }
    layout.bounds_chunk = bytes.size();
    append_u32(bytes, nfsu::kCollisionBoundsId);
    append_u32(bytes, 0);
    pad_to_alignment(bytes);
    layout.bounds_data = bytes.size();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        append_f32(bytes, low[axis]);
    }
    append_f32(bytes, 0.0F);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        append_f32(bytes, high[axis]);
    }
    append_f32(bytes, 0.0F);
    bytes.insert(bytes.end(), image.bounds_extra_bytes, 0);
    write_u32(bytes, layout.bounds_chunk + 4,
              static_cast<std::uint32_t>(bytes.size() - layout.bounds_chunk - 8));
    return bytes;
}

// Two cells side by side, one polygon each, plus a cell far away.
Image sample_image() {
    Image image;
    image.polygons.push_back(flat_quad(16.0F, 16.0F, 8.0F, 1.0F));
    image.polygons.push_back(flat_quad(48.0F, 16.0F, 8.0F, 2.0F));
    image.polygons.push_back(flat_quad(16.0F, 16.0F, 8.0F, 5.0F));
    image.polygons.push_back(flat_quad(-496.0F, 272.0F, 8.0F, -3.0F));

    Cell a;
    a.center[0] = 16.0F;
    a.center[1] = 16.0F;
    a.polygons = {0, 2};
    Cell b;
    b.center[0] = 48.0F;
    b.center[1] = 16.0F;
    b.polygons = {1};
    Cell c;
    c.center[0] = -496.0F;
    c.center[1] = 272.0F;
    c.polygons = {3};
    image.cells = {a, b, c};
    return image;
}

nfsu::TrackCollisionReport parse(const Bytes& bytes) {
    return nfsu::parse_track_collision(bytes.data(), bytes.size(), 0);
}

// -------------------------------------------------------------------
// JDLZ
// -------------------------------------------------------------------

// The container compression is nfsu::jdlz_decompress, which
// tests/texture_tests.cpp already covers for texture blobs. What is new
// here is the shape of a track image: megabytes rather than kilobytes, and
// leaning on the near-distance run encoding that a small blob rarely
// reaches. This round trips exactly those shapes, through an encoder
// written from the decoder's contract rather than from its code.
void test_jdlz() {
    std::vector<JdlzOp> ops;
    Bytes expected;
    for (int i = 0; i < 20; ++i) {
        JdlzOp op;
        op.literal = static_cast<unsigned char>('A' + i);
        ops.push_back(op);
        expected.push_back(op.literal);
    }
    {
        // The far encoding: distance 17 to 2064, length 3 to 34.
        JdlzOp op;
        op.match = true;
        op.near = false;
        op.distance = 20;
        op.length = 6;
        ops.push_back(op);
        const std::size_t src = expected.size() - op.distance;
        for (std::uint32_t i = 0; i < op.length; ++i) {
            expected.push_back(expected[src + i]);
        }
    }
    {
        // The near encoding: distance 1 to 16, length 3 to 4098. This is
        // the run a track image is full of and a texture blob is not.
        JdlzOp op;
        op.match = true;
        op.near = true;
        op.distance = 1;
        op.length = 4098;
        ops.push_back(op);
        const std::size_t src = expected.size() - op.distance;
        for (std::uint32_t i = 0; i < op.length; ++i) {
            expected.push_back(expected[src + i]);
        }
    }
    {
        // A near run over sixteen bytes, which is how a repeating record
        // pattern compresses.
        JdlzOp op;
        op.match = true;
        op.near = true;
        op.distance = 16;
        op.length = 400;
        ops.push_back(op);
        const std::size_t src = expected.size() - op.distance;
        for (std::uint32_t i = 0; i < op.length; ++i) {
            expected.push_back(expected[src + i]);
        }
    }
    for (int i = 0; i < 3; ++i) {
        JdlzOp op;
        op.literal = static_cast<unsigned char>('!' + i);
        ops.push_back(op);
        expected.push_back(op.literal);
    }

    const Bytes image =
        encode_jdlz(ops, static_cast<std::uint32_t>(expected.size()));
    Bytes out;
    std::string error;
    std::size_t unused = 0;
    check(nfsu::jdlz_decompress(image.data(), image.size(),
                                static_cast<std::uint32_t>(expected.size()), out,
                                unused, error),
          "jdlz: a track-shaped image decompresses (" + error + ")");
    check(out == expected, "jdlz: the round trip reproduces the input");
    check(unused == 0, "jdlz: a track-shaped image leaves no packed byte over");
}

// -------------------------------------------------------------------
// Parsing
// -------------------------------------------------------------------

void test_valid_image() {
    Layout layout;
    const Bytes bytes = build_image(sample_image(), layout);
    const nfsu::TrackCollisionReport report = parse(bytes);
    check(report.valid, "valid image: the image parses" +
                            (report.issues.empty()
                                 ? std::string()
                                 : " (" + report.issues[0].message + ")"));
    check(report.ok(), "valid image: no issue is raised");
    check(report.polygons.size() == 4, "valid image: four polygons");
    check(report.cells.size() == 4, "valid image: three cells and the sentinel");
    check(report.indices.size() == 4, "valid image: four indices");
    check(report.cost.cell_polygon_pairs == 4, "valid image: four pairs");
    check(report.cost.unreferenced_polygons == 0,
          "valid image: every polygon is reachable");
    check(report.cost.has_bounds, "valid image: the bounds were read");
    check(report.cost.stored_min[0] == report.cost.corner_min[0] &&
              report.cost.stored_max[2] == report.cost.corner_max[2],
          "valid image: the stored bounds are the corner bounds");
    check(report.cost.chunks == 6,
          "valid image: the unrelated top-level chunk is counted and ignored");

    // The strides the loader's shifts fix, pinned against the records the
    // parser actually walked rather than against the constants alone.
    check(report.polygons[1].offset - report.polygons[0].offset ==
              nfsu::kCollisionPolygonRecordSize,
          "valid image: polygons are 32 bytes apart");
    check(report.cells[1].offset - report.cells[0].offset ==
              nfsu::kCollisionCellRecordSize,
          "valid image: cells are 16 bytes apart");
    check(report.nodes[1].offset - report.nodes[0].offset ==
              nfsu::kCollisionTreeNodeSize,
          "valid image: quadtree nodes are 8 bytes apart");
    check(report.polygons.back().offset + nfsu::kCollisionPolygonRecordSize ==
              report.polygons[0].offset +
                  report.polygons.size() * nfsu::kCollisionPolygonRecordSize,
          "valid image: the polygon array ends where its stride says");

    // The quadtree covers the world square and its leaves land on the
    // cells, which is what makes a query able to find them.
    check(report.nodes.size() > nfsu::kCollisionTreeDepth,
          "valid image: the tree is deeper than one node");
    for (const nfsu::TrackCollisionTreeNode& node : report.nodes) {
        check(node.reached, "valid image: every node is reached");
    }
    check(report.nodes[0].half_extent == nfsu::kCollisionRootHalfExtent,
          "valid image: the root covers the world square");

    // The decoded corners are the stored halfwords divided by the
    // documented units and nothing else.
    const nfsu::TrackCollisionPolygon& polygon = report.polygons[0];
    check(polygon.corner[0][0] ==
              static_cast<float>(polygon.slot0[0]) * nfsu::kCollisionHorizontalUnit,
          "valid image: slot 0 is an eighth of a world unit");
    check(polygon.corner[0][2] ==
              static_cast<float>(polygon.slot2[0]) * nfsu::kCollisionVerticalUnit,
          "valid image: slot 2 is a 256th of a world unit");
    check(polygon.corner[0][2] == 1.0F, "valid image: the first quad is at y=1");
}

void test_empty_and_missing() {
    const nfsu::TrackCollisionReport empty =
        nfsu::parse_track_collision(nullptr, 0, 0);
    check_refused(empty, "no 0x00034130", "empty image");

    Bytes only_other;
    append_u32(only_other, 0x00000000U);
    append_u32(only_other, 16);
    for (int i = 0; i < 16; ++i) {
        only_other.push_back(0);
    }
    check_refused(parse(only_other), "no 0x00034130",
                  "a region of unrelated chunks");

    // Each of the five, removed in turn by renaming its id, is named.
    const std::uint32_t ids[] = {
        nfsu::kCollisionTreeId, nfsu::kCollisionCellListId,
        nfsu::kCollisionPolygonListId, nfsu::kCollisionIndexListId,
        nfsu::kCollisionBoundsId};
    const std::size_t Layout::*chunk_field[] = {
        &Layout::tree_chunk, &Layout::cell_chunk, &Layout::polygon_chunk,
        &Layout::index_chunk, &Layout::bounds_chunk};
    for (std::size_t i = 0; i < 5; ++i) {
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u32(bytes, layout.*(chunk_field[i]), 0x0003FFFFU);
        char text[16];
        std::snprintf(text, sizeof(text), "0x%08X", ids[i]);
        check_refused(parse(bytes), text,
                      "an image missing " + std::string(text));
    }

    // A second copy of one of them is refused rather than shadowed.
    {
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        const std::size_t size = 8 + nfsu::kCollisionPolygonRecordSize;
        Bytes extra;
        append_u32(extra, nfsu::kCollisionPolygonListId);
        append_u32(extra, nfsu::kCollisionPolygonRecordSize);
        extra.resize(size, 0);
        bytes.insert(bytes.end(), extra.begin(), extra.end());
        check_refused(parse(bytes), "more than one 0x00034132",
                      "a second polygon list");
    }
}

void test_malformed_chunks() {
    {
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        bytes.resize(bytes.size() + 4, 0);
        check_refused(parse(bytes), "shorter than a chunk header",
                      "a trailing fragment");
    }
    {
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u32(bytes, layout.polygon_chunk + 4, 0x7FFFFFFFU);
        check_refused(parse(bytes), "reaches past the end of the image",
                      "a payload past the end of the image");
    }
    {
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        check(bytes[layout.tree_data - 1] == nfsu::kCollisionFillerByte,
              "filler: the synthetic image pads with 0x11");
        bytes[layout.tree_data - 1] = 0x22;
        check_refused(parse(bytes), "alignment filler",
                      "a wrong alignment filler");
    }
    {
        Image image = sample_image();
        image.polygon_extra_bytes = 8;
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "not a whole number of 32-byte records",
                      "a polygon payload that is not a whole record count");
    }
    {
        Image image = sample_image();
        image.tree_extra_bytes = 4;
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "not a whole number of 8-byte nodes",
                      "a quadtree payload that is not a whole node count");
    }
    {
        Image image = sample_image();
        image.cell_extra_bytes = 8;
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "not a whole number of 16-byte records",
                      "a cell payload that is not a whole record count");
    }
    {
        Image image = sample_image();
        image.bounds_extra_bytes = 16;
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "bounds chunk holds", "a long bounds chunk");
    }
    {
        // An image that places nothing: no polygon, no index, one empty
        // cell. Both empty arrays are named.
        Image image;
        Cell cell;
        cell.center[0] = 16.0F;
        cell.center[1] = 16.0F;
        image.cells = {cell};
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        const nfsu::TrackCollisionReport report = parse(bytes);
        check_refused(report, "polygon list holds no record",
                      "an empty polygon list");
        check(has_text(report.issues, "index list holds no index"),
              "an empty index list is named too");
    }
}

void test_malformed_indices() {
    {
        // An index that names a polygon the list does not hold.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u16(bytes, layout.index_data + 2, 9);
        check_refused(parse(bytes), "but the polygon list holds",
                      "an index past the end of the polygon list");
    }
    {
        // An odd index payload, which cannot be a whole number of indices.
        Image image = sample_image();
        image.index_extra_bytes = 1;
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "not a whole number of 16-bit indices",
                      "an odd index payload");
    }
    {
        // A cell whose slice runs past the end of the index list.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u32(bytes, layout.cell_data + nfsu::kCollisionCellRecordSize + 12,
                  64);
        check_refused(parse(bytes), "but the index list holds",
                      "a cell slice past the end of the index list");
    }
    {
        // A gap between two cells' slices: the runs must be contiguous.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u32(bytes,
                  layout.cell_data + 2 * nfsu::kCollisionCellRecordSize + 8, 3);
        write_u32(bytes,
                  layout.cell_data + 2 * nfsu::kCollisionCellRecordSize + 12, 1);
        check_refused(parse(bytes), "but the cells before it end at",
                      "a gap between two cell slices");
    }
    {
        // The sentinel cell must slice nothing.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u32(bytes, layout.cell_data + 12, 1);
        check_refused(parse(bytes), "empty sentinel and must slice nothing",
                      "a sentinel cell that slices indices");
    }
    {
        // Two spare indices are more padding than the format allows.
        Image image = sample_image();
        image.spare_indices = 2;
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "unaccounted for", "two spare indices");
    }
    {
        // One spare index is padding only when the used count is odd, and
        // the sample uses four.
        Image image = sample_image();
        image.spare_indices = 1;
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "only padding when the used count is odd",
                      "one spare index after an even used count");
    }
    {
        // A cell that names a polygon nowhere near it: the index list and
        // the grid must agree, or a query would miss the polygon.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u16(bytes, layout.index_data, 3);
        check_refused(parse(bytes), "which does not overlap it",
                      "a cell naming a polygon outside it");
    }
}

void test_malformed_tree() {
    // The root entry that leads to the first cell, so that every mutation
    // below edits a node the walk really reaches.
    {
        // An entry above the leaf level naming a node the array does not
        // hold.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u16(bytes, layout.tree_data, 4000);
        check_refused(parse(bytes), "but the quadtree holds",
                      "a node entry past the end of the node array");
    }
    {
        // One node named twice: a shared subtree is not a tree.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        std::uint16_t taken = 0;
        std::size_t free_slot = 0;
        bool found_taken = false;
        bool found_free = false;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            const std::size_t at = layout.tree_data + 2 * slot;
            const std::uint16_t entry = static_cast<std::uint16_t>(
                bytes[at] | (static_cast<unsigned int>(bytes[at + 1]) << 8U));
            if (entry != 0 && !found_taken) {
                taken = entry;
                found_taken = true;
            } else if (entry == 0 && !found_free) {
                free_slot = at;
                found_free = true;
            }
        }
        check(found_taken && found_free,
              "tree: the sample root has both a child and a free slot");
        write_u16(bytes, free_slot, taken);
        check_refused(parse(bytes), "already part of the tree",
                      "one node named by two entries");
    }
    {
        // A node no parent names.
        Image image = sample_image();
        image.orphan_nodes = 1;
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "is not reachable from the root",
                      "an orphan node");
    }
    {
        // A leaf entry naming a cell the list does not hold.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        // The deepest nodes are the last ones the builder created.
        const std::size_t last = layout.tree_data +
                                 (read_u32(bytes, layout.tree_chunk + 4) -
                                  (layout.tree_data - layout.tree_chunk - 8)) -
                                 nfsu::kCollisionTreeNodeSize;
        bool patched = false;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            const std::size_t at = last + 2 * slot;
            if (bytes[at] != 0 || bytes[at + 1] != 0) {
                write_u16(bytes, at, 500);
                patched = true;
                break;
            }
        }
        check(patched, "tree: the last node carries a leaf entry");
        check_refused(parse(bytes), "but the cell list holds",
                      "a leaf entry past the end of the cell list");
    }
    {
        // A cell no leaf names.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        const std::size_t last = layout.tree_data +
                                 (read_u32(bytes, layout.tree_chunk + 4) -
                                  (layout.tree_data - layout.tree_chunk - 8)) -
                                 nfsu::kCollisionTreeNodeSize;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            const std::size_t at = last + 2 * slot;
            if (bytes[at] != 0 || bytes[at + 1] != 0) {
                write_u16(bytes, at, 0);
                break;
            }
        }
        check_refused(parse(bytes), "is not named by any quadtree leaf",
                      "a cell no leaf names");
    }
    {
        // A leaf that lands somewhere other than the centre its cell
        // record stores. A query walks the tree, so a leaf in the wrong
        // square returns the wrong polygons.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_f32(bytes, layout.cell_data + nfsu::kCollisionCellRecordSize,
                  1024.0F);
        check_refused(parse(bytes), "but the quadtree places it at",
                      "a cell centre the tree disagrees with");
    }
    {
        // One cell named by two leaves. An entry of 0 is the empty
        // marker, so no leaf can name the sentinel and there is no rule
        // about that to break; this is the reachable half of the same
        // invariant.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        const std::size_t last = layout.tree_data +
                                 (read_u32(bytes, layout.tree_chunk + 4) -
                                  (layout.tree_data - layout.tree_chunk - 8)) -
                                 nfsu::kCollisionTreeNodeSize;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            const std::size_t at = last + 2 * slot;
            if (bytes[at] == 0 && bytes[at + 1] == 0) {
                continue;
            }
            // Move the entry into a free slot and leave the original,
            // which makes two leaves name one cell.
            for (std::size_t other = 0; other < 4; ++other) {
                const std::size_t free_slot = last + 2 * other;
                if (bytes[free_slot] == 0 && bytes[free_slot + 1] == 0) {
                    write_u16(bytes, free_slot,
                              static_cast<std::uint16_t>(
                                  bytes[at] |
                                  (static_cast<unsigned int>(bytes[at + 1]) << 8U)));
                    break;
                }
            }
            break;
        }
        check_refused(parse(bytes), "which another leaf already names",
                      "two leaves naming one cell");
    }
}

void test_malformed_bounds() {
    {
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_f32(bytes, layout.bounds_data, 1000.0F);
        check_refused(parse(bytes), "above its maximum",
                      "a bounds minimum above its maximum");
    }
    {
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_f32(bytes, layout.bounds_data + 8, -1.0F);
        check_refused(parse(bytes), "does not contain the polygons",
                      "bounds that do not contain the polygons");
    }
    {
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u32(bytes, layout.bounds_data, 0x7F800001U);
        check_refused(parse(bytes), "is not finite", "a non-finite bound");
    }
    {
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u32(bytes, layout.cell_data + nfsu::kCollisionCellRecordSize,
                  0x7FC00000U);
        check_refused(parse(bytes), "centre that is not finite",
                      "a non-finite cell centre");
    }
    {
        // The sentinel is held to the same rule. Retail stores exactly
        // (999999.0, 999999.0) there in all 31 tracks, so nothing real is
        // refused, and exempting it would carry a NaN through the parser.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u32(bytes, layout.cell_data, 0x7FC00000U);
        check_refused(parse(bytes), "cell 0 stores a centre that is not finite",
                      "a non-finite sentinel centre");
    }
}

void test_limits() {
    Layout layout;
    const Bytes bytes = build_image(sample_image(), layout);
    {
        nfsu::TrackCollisionLimits limits;
        limits.max_region_bytes = 16;
        const nfsu::TrackCollisionReport report = nfsu::parse_track_collision(
            bytes.data(), bytes.size(), 0, limits);
        check_refused(report, "over the limit of 16", "a region over its limit");
    }
    {
        nfsu::TrackCollisionLimits limits;
        limits.max_polygons = 2;
        const nfsu::TrackCollisionReport report = nfsu::parse_track_collision(
            bytes.data(), bytes.size(), 0, limits);
        check_refused(report, "polygon list holds 4 records",
                      "a polygon list over its limit");
    }
    {
        nfsu::TrackCollisionLimits limits;
        limits.max_chunks = 3;
        const nfsu::TrackCollisionReport report = nfsu::parse_track_collision(
            bytes.data(), bytes.size(), 0, limits);
        check_refused(report, "more than 3 chunks", "a chunk count over its limit");
    }
    {
        nfsu::TrackCollisionLimits limits;
        limits.max_tree_nodes = 4;
        const nfsu::TrackCollisionReport report = nfsu::parse_track_collision(
            bytes.data(), bytes.size(), 0, limits);
        check_refused(report, "over the limit of 4",
                      "a quadtree over its limit");
    }
    {
        nfsu::TrackCollisionLimits limits;
        limits.max_cells = 2;
        const nfsu::TrackCollisionReport report = nfsu::parse_track_collision(
            bytes.data(), bytes.size(), 0, limits);
        check_refused(report, "cell list holds 4 records",
                      "a cell list over its limit");
    }
    {
        nfsu::TrackCollisionLimits limits;
        limits.max_indices = 3;
        const nfsu::TrackCollisionReport report = nfsu::parse_track_collision(
            bytes.data(), bytes.size(), 0, limits);
        check_refused(report, "index list holds 4 indices",
                      "an index list over its limit");
    }
}

// -------------------------------------------------------------------
// Queries
// -------------------------------------------------------------------

void test_queries() {
    Layout layout;
    const Bytes bytes = build_image(sample_image(), layout);
    const nfsu::TrackCollisionReport report = parse(bytes);
    check(report.valid, "queries: the sample image parses");

    std::vector<std::size_t> found;
    {
        const float min[3] = {10.0F, 10.0F, -10.0F};
        const float max[3] = {20.0F, 20.0F, 10.0F};
        check(nfsu::query_track_collision_box(report, min, max, found),
              "box: a small box succeeds");
        const std::vector<std::size_t> expected = {0, 2};
        check(found == expected,
              "box: the box over the first cell finds its two polygons");
    }
    {
        // The vertical narrows the answer after the cells are chosen.
        const float min[3] = {10.0F, 10.0F, 4.0F};
        const float max[3] = {20.0F, 20.0F, 6.0F};
        check(nfsu::query_track_collision_box(report, min, max, found),
              "box: a narrow vertical slice succeeds");
        const std::vector<std::size_t> expected = {2};
        check(found == expected, "box: the vertical slice keeps only the high quad");
    }
    {
        const float min[3] = {-2000.0F, -2000.0F, -2000.0F};
        const float max[3] = {2000.0F, 2000.0F, 2000.0F};
        check(nfsu::query_track_collision_box(report, min, max, found),
              "box: a world-sized box succeeds");
        check(found.size() == 4, "box: a world-sized box finds every polygon");
        check(std::is_sorted(found.begin(), found.end()),
              "box: the answer is ascending");
    }
    {
        const float min[3] = {900.0F, 900.0F, -10.0F};
        const float max[3] = {910.0F, 910.0F, 10.0F};
        check(nfsu::query_track_collision_box(report, min, max, found),
              "box: an empty region succeeds");
        check(found.empty(), "box: an empty region finds nothing");
    }
    {
        const float min[3] = {20.0F, 10.0F, -10.0F};
        const float max[3] = {10.0F, 20.0F, 10.0F};
        check(!nfsu::query_track_collision_box(report, min, max, found),
              "box: a minimum above its maximum is refused");
        check(found.empty(), "box: a refused query answers nothing");
    }
    {
        const float min[3] = {10.0F, 10.0F, -10.0F};
        const float max[3] = {20.0F, 20.0F, 10.0F};
        nfsu::TrackCollisionQueryLimits limits;
        limits.max_results = 1;
        check(!nfsu::query_track_collision_box(report, min, max, found, limits),
              "box: a result cap is refused rather than truncated");
        check(found.empty(), "box: a capped query answers nothing");
    }
    {
        const float min[3] = {-2000.0F, -2000.0F, -10.0F};
        const float max[3] = {2000.0F, 2000.0F, 10.0F};
        nfsu::TrackCollisionQueryLimits limits;
        limits.max_cells = 1;
        check(!nfsu::query_track_collision_box(report, min, max, found, limits),
              "box: a cell cap is refused rather than truncated");
    }
    {
        nfsu::TrackCollisionReport broken;
        const float min[3] = {0.0F, 0.0F, 0.0F};
        const float max[3] = {1.0F, 1.0F, 1.0F};
        check(!nfsu::query_track_collision_box(broken, min, max, found),
              "box: a report that is not valid answers nothing");
    }

    nfsu::TrackCollisionHit hit;
    {
        // Straight down through the two stacked quads of the first cell:
        // the nearer one is the one at y = 5.
        const float origin[3] = {16.0F, 16.0F, 100.0F};
        const float direction[3] = {0.0F, 0.0F, -1.0F};
        check(nfsu::query_track_collision_ray(report, origin, direction, 1000.0F,
                                              hit),
              "ray: a downward ray hits");
        check(hit.polygon == 2, "ray: the nearer of two stacked quads is hit");
        check(std::fabs(hit.distance - 95.0F) < 1e-3F,
              "ray: the distance is the one the quad sits at");
        check(std::fabs(hit.point[2] - 5.0F) < 1e-3F,
              "ray: the hit point is on the quad");
    }
    {
        // The same ray, bounded short of the quad.
        const float origin[3] = {16.0F, 16.0F, 100.0F};
        const float direction[3] = {0.0F, 0.0F, -1.0F};
        check(!nfsu::query_track_collision_ray(report, origin, direction, 10.0F,
                                               hit),
              "ray: a ray that stops short of the quad misses");
    }
    {
        // Past the edge of every quad.
        const float origin[3] = {900.0F, 900.0F, 100.0F};
        const float direction[3] = {0.0F, 0.0F, -1.0F};
        check(!nfsu::query_track_collision_ray(report, origin, direction, 1000.0F,
                                               hit),
              "ray: a ray over empty ground misses");
    }
    {
        const float origin[3] = {16.0F, 16.0F, 100.0F};
        const float zero[3] = {0.0F, 0.0F, 0.0F};
        check(!nfsu::query_track_collision_ray(report, origin, zero, 1000.0F, hit),
              "ray: a direction of zero length is refused");
    }
    {
        const float origin[3] = {16.0F, 16.0F, 100.0F};
        const float direction[3] = {0.0F, 0.0F, -1.0F};
        check(!nfsu::query_track_collision_ray(report, origin, direction, 0.0F,
                                               hit),
              "ray: a distance of zero is refused");
        check(!nfsu::query_track_collision_ray(report, origin, direction,
                                               -1.0F, hit),
              "ray: a negative distance is refused");
    }
    {
        // A vertical ray over the second cell. Its swept horizontal box is
        // a single point, so this shows the second cell is reachable and
        // nothing more; the ray whose swept box really spans two cells is
        // in test_ray_details.
        const float origin[3] = {48.0F, 16.0F, 100.0F};
        const float direction[3] = {0.0F, 0.0F, -1.0F};
        check(nfsu::query_track_collision_ray(report, origin, direction, 1000.0F,
                                              hit),
              "ray: the second cell is reachable");
        check(hit.polygon == 1, "ray: the second cell's polygon is hit");
    }
    {
        std::vector<std::size_t> cells;
        check(nfsu::query_track_collision_cells(report, 10.0F, 10.0F, 20.0F,
                                                20.0F, cells),
              "cells: a small box succeeds");
        check(cells.size() == 1, "cells: a small box touches one cell");
        check(cells[0] == 1, "cells: it is the first cell");
        check(!nfsu::query_track_collision_cells(report, 20.0F, 10.0F, 10.0F,
                                                 20.0F, cells),
              "cells: a minimum above its maximum is refused");
    }
}

// -------------------------------------------------------------------
// The ray answer in full. Every component of TrackCollisionHit is pinned
// here, and so is the sign convention and the tie-break, because a ray
// query that is right about which polygon and wrong about where is worse
// than one that fails.
// -------------------------------------------------------------------

void test_ray_details() {
    Layout layout;
    const Bytes bytes = build_image(sample_image(), layout);
    const nfsu::TrackCollisionReport report = parse(bytes);
    check(report.valid, "ray detail: the sample image parses");
    nfsu::TrackCollisionHit hit;

    {
        // Below every quad, pointing further down. The quads are behind
        // the origin, and a ray does not hit what is behind it.
        const float origin[3] = {16.0F, 16.0F, -10.0F};
        const float direction[3] = {0.0F, 0.0F, -1.0F};
        check(!nfsu::query_track_collision_ray(report, origin, direction,
                                               1000.0F, hit),
              "ray: geometry behind the origin is not hit");
    }
    {
        // The same ray reversed does hit, so the miss above is the sign
        // convention and not an empty column.
        const float origin[3] = {16.0F, 16.0F, -10.0F};
        const float direction[3] = {0.0F, 0.0F, 1.0F};
        check(nfsu::query_track_collision_ray(report, origin, direction,
                                              1000.0F, hit),
              "ray: pointing back up the same column hits");
        check(hit.polygon == 0,
              "ray: upward, the lowest quad is the nearest one");
        check(std::fabs(hit.distance - 11.0F) < 1e-3F,
              "ray: the upward distance reaches the lowest quad");
    }
    {
        // A slanted ray whose swept horizontal box spans two cells, and
        // whose hit point differs from the origin on all three slots.
        const float origin[3] = {16.0F, 16.0F, 10.0F};
        const float direction[3] = {1.0F, 0.125F, -0.25F};
        check(nfsu::query_track_collision_ray(report, origin, direction, 60.0F,
                                              hit),
              "ray: a ray crossing two cells hits");
        check(hit.polygon == 1,
              "ray: it reaches the polygon in the second cell");
        check(std::fabs(hit.distance - 32.0F) < 1e-3F,
              "ray: the parametric distance is the crossing point");
        check(std::fabs(hit.point[0] - 48.0F) < 1e-3F,
              "ray: the hit point carries slot 0, not the origin's");
        check(std::fabs(hit.point[1] - 20.0F) < 1e-3F,
              "ray: the hit point carries slot 1, not the origin's");
        check(std::fabs(hit.point[2] - 2.0F) < 1e-3F,
              "ray: the hit point carries slot 2");
    }
    {
        // Which of the two triangles was hit. flat_quad lays corners out
        // as (-,-) (+,-) (+,+) (-,+), so (0,1,2) is the half toward
        // corner 1 and (0,2,3) the half toward corner 3.
        const float toward_one[3] = {20.0F, 12.0F, 100.0F};
        const float down[3] = {0.0F, 0.0F, -1.0F};
        check(nfsu::query_track_collision_ray(report, toward_one, down, 1000.0F,
                                              hit),
              "ray: the corner-1 half of the quad is hit");
        check(hit.triangle == 0,
              "ray: that half is triangle (0,1,2)");
        const float toward_three[3] = {12.0F, 20.0F, 100.0F};
        check(nfsu::query_track_collision_ray(report, toward_three, down,
                                              1000.0F, hit),
              "ray: the corner-3 half of the quad is hit");
        check(hit.triangle == 1,
              "ray: that half is triangle (0,2,3)");
    }
    {
        // Two polygons at the same height in one cell: the tie must go to
        // the lower index, or a run is not reproducible.
        Image image;
        image.polygons.push_back(flat_quad(16.0F, 16.0F, 8.0F, 1.0F));
        image.polygons.push_back(flat_quad(16.0F, 16.0F, 8.0F, 1.0F));
        Cell cell;
        cell.center[0] = 16.0F;
        cell.center[1] = 16.0F;
        cell.polygons = {0, 1};
        image.cells = {cell};
        Layout tied_layout;
        const Bytes tied_bytes = build_image(image, tied_layout);
        const nfsu::TrackCollisionReport tied = parse(tied_bytes);
        check(tied.valid, "ray tie: the image parses");
        const float origin[3] = {16.0F, 14.0F, 100.0F};
        const float down[3] = {0.0F, 0.0F, -1.0F};
        check(nfsu::query_track_collision_ray(tied, origin, down, 1000.0F, hit),
              "ray tie: the stacked pair is hit");
        check(hit.polygon == 0,
              "ray tie: the lower polygon index wins an exact tie");
    }
}

// -------------------------------------------------------------------
// The cost counters. They are the only report of what was decoded that a
// caller sees without walking the arrays, so each one is pinned against an
// image built to make it a different number from its neighbours.
// -------------------------------------------------------------------

void test_cost_counters() {
    Image image;
    image.polygons.push_back(flat_quad(16.0F, 16.0F, 8.0F, 1.0F));
    image.polygons.push_back(flat_triangle(16.0F, 16.0F, 8.0F, 2.0F));
    image.polygons.push_back(wall(10.0F, 10.0F, 22.0F, 22.0F));
    image.polygons.push_back(flat_quad(48.0F, 16.0F, 8.0F, 1.0F));
    Cell a;
    a.center[0] = 16.0F;
    a.center[1] = 16.0F;
    a.polygons = {0, 1, 2};
    Cell b;
    b.center[0] = 48.0F;
    b.center[1] = 16.0F;
    b.polygons = {3, 3};
    image.cells = {a, b};
    Layout layout;
    const Bytes bytes = build_image(image, layout);
    const nfsu::TrackCollisionReport report = parse(bytes);
    check(report.valid, "cost: the image parses" +
                            (report.issues.empty()
                                 ? std::string()
                                 : " (" + report.issues[0].message + ")"));

    const nfsu::TrackCollisionCost& cost = report.cost;
    check(cost.polygons == 4, "cost: four polygons");
    check(cost.cells == 3, "cost: two cells and the sentinel");
    check(cost.indices == 6, "cost: five indices plus one pad");
    check(cost.tree_nodes == report.nodes.size(), "cost: the node count");
    check(cost.cell_polygon_pairs == 5, "cost: five cell/polygon pairs");
    check(cost.unreferenced_polygons == 0, "cost: none unreferenced");
    check(cost.saturated_vertical_polygons == 1,
          "cost: exactly the one wall is saturated");
    check(cost.degenerate_corner_polygons == 1,
          "cost: exactly the one triangle repeats a corner");
    check(cost.source_polygon_bytes ==
              4 * nfsu::kCollisionPolygonRecordSize,
          "cost: source polygon bytes follow the record size");
    check(cost.source_cell_bytes == 3 * nfsu::kCollisionCellRecordSize,
          "cost: source cell bytes follow the record size");
    check(cost.source_index_bytes == 12, "cost: source index bytes");
    check(cost.source_tree_bytes ==
              report.nodes.size() * nfsu::kCollisionTreeNodeSize,
          "cost: source node bytes follow the record size");

    // A polygon no cell names is counted, not refused.
    Image lonely = image;
    lonely.polygons.push_back(flat_quad(-496.0F, 272.0F, 8.0F, 0.0F));
    Layout lonely_layout;
    const Bytes lonely_bytes = build_image(lonely, lonely_layout);
    const nfsu::TrackCollisionReport lonely_report = parse(lonely_bytes);
    check(lonely_report.valid, "cost: an unreferenced polygon is not an error");
    check(lonely_report.cost.unreferenced_polygons == 1,
          "cost: the unreferenced polygon is counted");
}

// -------------------------------------------------------------------
// load_track_collision: the file path, the JDLZ branch and the branch that
// parses an image that was never compressed.
// -------------------------------------------------------------------

// +12 of the JDLZ header is the compressed size, which the decoder
// requires to equal the blob size exactly.
constexpr std::size_t kJdlzPackedSizeOffset = 12;

Bytes jdlz_all_literals(const Bytes& payload) {
    std::vector<JdlzOp> ops;
    ops.reserve(payload.size());
    for (unsigned char byte : payload) {
        JdlzOp op;
        op.literal = byte;
        ops.push_back(op);
    }
    return encode_jdlz(ops, static_cast<std::uint32_t>(payload.size()));
}

bool write_file(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

void test_load() {
    Layout layout;
    const Bytes image = build_image(sample_image(), layout);
    const nfsu::TrackCollisionReport direct = parse(image);
    check(direct.valid, "load: the in-memory image parses");

    std::error_code code;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path(code) / "nfsu_track_collision_tests";
    std::filesystem::create_directories(dir, code);
    const std::filesystem::path packed_path = dir / "packed.lzc";
    const std::filesystem::path raw_path = dir / "raw.bin";
    const std::filesystem::path short_path = dir / "short.lzc";
    const std::filesystem::path tail_path = dir / "tail.lzc";

    const Bytes packed = jdlz_all_literals(image);
    check(write_file(packed_path, packed), "load: the packed file was written");
    check(write_file(raw_path, image), "load: the raw file was written");

    {
        std::vector<unsigned char> out;
        const nfsu::TrackCollisionReport report =
            nfsu::load_track_collision(packed_path, out, nfsu::TrackCollisionLimits{});
        check(report.valid, "load: a JDLZ file decompresses and parses" +
                                (report.issues.empty()
                                     ? std::string()
                                     : " (" + report.issues[0].message + ")"));
        check(out == image, "load: the decompressed image is the original");
        check(report.cost.polygons == direct.cost.polygons &&
                  report.cost.cells == direct.cost.cells,
              "load: it decodes the same counts as the in-memory parse");
        check(report.file_size == packed.size(),
              "load: the packed file size is reported");
        check(report.cost.packed_bytes == packed.size(),
              "load: the packed byte count is reported");
    }
    {
        // No JDLZ header: the file is parsed as it stands. A decoder that
        // decompressed unconditionally would refuse this.
        std::vector<unsigned char> out;
        const nfsu::TrackCollisionReport report =
            nfsu::load_track_collision(raw_path, out, nfsu::TrackCollisionLimits{});
        check(report.valid, "load: an uncompressed image parses as it stands");
        check(out == image, "load: the uncompressed image is passed through");
        check(report.cost.unused_packed_bytes == 0,
              "load: an uncompressed image leaves no packed byte over");
    }
    {
        // Exactly the two flag bytes the cap allows must still be
        // accepted, and one byte more must not be, or the cap is not
        // the width it is documented to have. The padding below comes
        // from kMaxUnusedPacked, this file's own literal, and not from
        // the published constant it is checked against: an image built
        // out of that constant would move with it and neither
        // assertion could see the change.
        std::vector<unsigned char> probe_out;
        const nfsu::TrackCollisionReport probe = nfsu::load_track_collision(
            packed_path, probe_out, nfsu::TrackCollisionLimits{});
        check(probe.valid, "load: the packed file parses before padding");
        const std::uint64_t spare = probe.cost.unused_packed_bytes;
        check(spare <= kMaxUnusedPacked,
              "load: the encoder leaves no more than the flag byte pair");
        Bytes exact = packed;
        exact.insert(exact.end(),
                     static_cast<std::size_t>(kMaxUnusedPacked - spare), 0);
        write_u32(exact, kJdlzPackedSizeOffset,
                  static_cast<std::uint32_t>(exact.size()));
        const std::filesystem::path exact_path = dir / "exact.lzc";
        check(write_file(exact_path, exact), "load: the padded file was written");
        std::vector<unsigned char> out;
        const nfsu::TrackCollisionReport report =
            nfsu::load_track_collision(exact_path, out, nfsu::TrackCollisionLimits{});
        check(report.valid,
              "load: exactly the allowed number of unused packed bytes is kept" +
                  (report.issues.empty()
                       ? std::string()
                       : " (" + report.issues[0].message + ")"));
        check(report.cost.unused_packed_bytes == kMaxUnusedPacked,
              "load: and the count is reported as the flag byte pair");

        // One byte past the pair. The sixteen-byte tail below refuses
        // a leftover nothing could mistake for flag bytes; this is the
        // first leftover that is over the cap at all, and it is the
        // assertion a cap widened by one has to get past.
        Bytes over = exact;
        over.push_back(0);
        write_u32(over, kJdlzPackedSizeOffset,
                  static_cast<std::uint32_t>(over.size()));
        const std::filesystem::path over_path = dir / "over.lzc";
        check(write_file(over_path, over),
              "load: the over-padded file was written");
        std::vector<unsigned char> over_out;
        const nfsu::TrackCollisionReport over_report =
            nfsu::load_track_collision(over_path, over_out,
                                       nfsu::TrackCollisionLimits{});
        check(!over_report.valid,
              "load: one unused packed byte past the flag byte pair is "
              "refused");
        check(has_text(over_report.issues, "packed bytes unused"),
              "load: the leftover past the pair is named" +
                  (over_report.issues.empty()
                       ? std::string()
                       : " (got \"" + over_report.issues[0].message + "\")"));
        std::error_code rm;
        std::filesystem::remove(exact_path, rm);
        std::filesystem::remove(over_path, rm);
    }
    {
        // More packed bytes left over than a flag byte pair needs.
        Bytes tail = packed;
        tail.insert(tail.end(), 16, 0);
        write_u32(tail, kJdlzPackedSizeOffset,
                  static_cast<std::uint32_t>(tail.size()));
        check(write_file(tail_path, tail), "load: the padded file was written");
        std::vector<unsigned char> out;
        const nfsu::TrackCollisionReport report =
            nfsu::load_track_collision(tail_path, out, nfsu::TrackCollisionLimits{});
        check(!report.valid, "load: unaccounted packed bytes are refused");
        check(has_text(report.issues, "packed bytes unused"),
              "load: the leftover packed bytes are named" +
                  (report.issues.empty()
                       ? std::string()
                       : " (got \"" + report.issues[0].message + "\")"));
    }
    {
        // A JDLZ header whose packed size does not match the file.
        Bytes bad = packed;
        write_u32(bad, kJdlzPackedSizeOffset,
                  static_cast<std::uint32_t>(bad.size() + 32));
        check(write_file(short_path, bad), "load: the short file was written");
        std::vector<unsigned char> out;
        const nfsu::TrackCollisionReport report =
            nfsu::load_track_collision(short_path, out, nfsu::TrackCollisionLimits{});
        check(!report.valid, "load: a JDLZ length mismatch is refused");
    }
    {
        // The packed-file cap is enforced before the file is read.
        nfsu::TrackCollisionLimits limits;
        limits.max_packed_bytes = 8;
        std::vector<unsigned char> out;
        const nfsu::TrackCollisionReport report =
            nfsu::load_track_collision(packed_path, out, limits);
        check(!report.valid, "load: a file over max_packed_bytes is refused");
        check(has_text(report.issues, "over the limit of 8"),
              "load: the packed-file cap is named");
    }
    {
        std::vector<unsigned char> out;
        const nfsu::TrackCollisionReport report = nfsu::load_track_collision(
            dir / "no_such_file.lzc", out, nfsu::TrackCollisionLimits{});
        check(!report.valid, "load: a missing file is refused");
    }

    std::filesystem::remove(packed_path, code);
    std::filesystem::remove(raw_path, code);
    std::filesystem::remove(short_path, code);
    std::filesystem::remove(tail_path, code);
    std::filesystem::remove(dir, code);
}

// -------------------------------------------------------------------
// The constants this decoder publishes, against the format's own numbers
// written out independently. Everything else in this file builds its
// images from the literals above, so this is the single place where the
// two meet and the only place a wrong constant can be caught.
// -------------------------------------------------------------------

void test_constants() {
    check(nfsu::kCollisionMaxUnusedPackedBytes == kMaxUnusedPacked,
          "constant: at most a flag byte pair may be left unused");
    check(nfsu::kCollisionRecordAlignment == kAlignment,
          "constant: the record alignment is 16");
    check(nfsu::kCollisionFillerByte == kFiller,
          "constant: the alignment filler is 0x11");
    check(nfsu::kCollisionPolygonRecordSize == kPolygonRecord,
          "constant: a polygon record is 32 bytes");
    check(nfsu::kCollisionCellRecordSize == kCellRecord,
          "constant: a cell record is 16 bytes");
    check(nfsu::kCollisionTreeNodeSize == kNodeRecord,
          "constant: a quadtree node is 8 bytes");
    check(nfsu::kCollisionVerticalSaturation == kSaturation,
          "constant: the vertical saturates at 32512");
    check(nfsu::kCollisionCellSize == kCellSquare,
          "constant: a cell is 32 world units square");
    check(nfsu::kCollisionTreeDepth == 10, "constant: the tree is 10 deep");
    check(nfsu::kCollisionRootHalfExtent == 16384.0F,
          "constant: the root half extent is 16384");
    check(nfsu::kCollisionHorizontalUnit == 1.0F / 8.0F,
          "constant: the horizontal unit is an eighth");
    check(nfsu::kCollisionVerticalUnit == 1.0F / 256.0F,
          "constant: the vertical unit is a 256th");
    check(nfsu::kCollisionPolygonCorners == 4,
          "constant: a polygon has four corners");
    check(nfsu::kCollisionTreeNodeEntries == 4,
          "constant: a node has four entries");
}

// -------------------------------------------------------------------
// The vertical base at record +4. speed.exe 0x0043FD20 forms
// ((base << 8) + slot2) << 8 and multiplies by 1/65536, so a corner sits
// at base + slot2/256 and not at slot2/256. A decoder that drops the base
// is wrong for 240793 of the 290995 retail records, and every fixture in
// this file used to leave it zero, which is why nothing caught it.
// -------------------------------------------------------------------

void test_vertical_base() {
    Image image;
    Polygon low = flat_quad(16.0F, 16.0F, 8.0F, 0.25F);
    low.vertical_base = -23;
    Polygon high = flat_quad(16.0F, 16.0F, 8.0F, 0.50F);
    high.vertical_base = 94;
    Polygon plain = flat_quad(16.0F, 16.0F, 8.0F, 0.75F);
    image.polygons = {low, high, plain};
    Cell cell;
    cell.center[0] = 16.0F;
    cell.center[1] = 16.0F;
    cell.polygons = {0, 1, 2};
    image.cells = {cell};
    Layout layout;
    const Bytes bytes = build_image(image, layout);
    const nfsu::TrackCollisionReport report = parse(bytes);
    check(report.valid, "vertical base: the image parses" +
                            (report.issues.empty()
                                 ? std::string()
                                 : " (" + report.issues[0].message + ")"));

    check(report.polygons[0].vertical_base == -23,
          "vertical base: the stored base is kept");
    for (std::size_t c = 0; c < nfsu::kCollisionPolygonCorners; ++c) {
        check(std::fabs(report.polygons[0].corner[c][2] - (-22.75F)) < 1e-4F,
              "vertical base: a negative base lowers the corner");
        check(std::fabs(report.polygons[1].corner[c][2] - 94.5F) < 1e-4F,
              "vertical base: a positive base raises the corner");
        check(std::fabs(report.polygons[2].corner[c][2] - 0.75F) < 1e-4F,
              "vertical base: a zero base leaves the corner alone");
    }
    // The horizontal slots must not move with it.
    check(std::fabs(report.polygons[0].corner[0][0] - 8.0F) < 1e-4F &&
              std::fabs(report.polygons[0].corner[0][1] - 8.0F) < 1e-4F,
          "vertical base: only the vertical carries the base");

    // The decoded bounds follow the corrected height, not the stored one.
    check(std::fabs(report.cost.corner_min[2] - (-22.75F)) < 1e-4F,
          "vertical base: the corner bounds use the corrected vertical");
    check(std::fabs(report.cost.corner_max[2] - 94.5F) < 1e-4F,
          "vertical base: the corner bounds reach the raised polygon");

    // And so do the queries, which is where a caller meets it.
    std::vector<std::size_t> found;
    const float mn[3] = {10.0F, 10.0F, -23.0F};
    const float mx[3] = {20.0F, 20.0F, -22.0F};
    check(nfsu::query_track_collision_box(report, mn, mx, found),
          "vertical base: a slab at the corrected height succeeds");
    const std::vector<std::size_t> only_low = {0};
    check(found == only_low,
          "vertical base: the box finds the polygon at its real height");

    const float mn2[3] = {10.0F, 10.0F, 0.0F};
    const float mx2[3] = {20.0F, 20.0F, 1.0F};
    check(nfsu::query_track_collision_box(report, mn2, mx2, found),
          "vertical base: a slab at the uncorrected height succeeds");
    const std::vector<std::size_t> only_plain = {2};
    check(found == only_plain,
          "vertical base: a based polygon is not found at its stored height");

    nfsu::TrackCollisionHit hit;
    const float origin[3] = {16.0F, 16.0F, 1000.0F};
    const float down[3] = {0.0F, 0.0F, -1.0F};
    check(nfsu::query_track_collision_ray(report, origin, down, 2000.0F, hit),
          "vertical base: a downward ray hits");
    check(hit.polygon == 1,
          "vertical base: the raised polygon is the nearest from above");
    check(std::fabs(hit.point[2] - 94.5F) < 1e-3F,
          "vertical base: the hit point is at the corrected height");
    check(std::fabs(hit.distance - 905.5F) < 1e-2F,
          "vertical base: the distance follows the corrected height");
}

// -------------------------------------------------------------------
// Boundaries and off-by-ones. Each of these was a surviving mutation in an
// independent review: a rule that is one unit too generous, a comparison
// that flips on an exact touch, or a cap that admits one too many.
// -------------------------------------------------------------------

void test_boundaries() {
    {
        // An index exactly equal to the polygon count. `>=` refuses it;
        // `>` would let it through and then read one past the end of both
        // the polygon array and the referenced-polygon vector.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_u16(bytes, layout.index_data, 4);
        check_refused(parse(bytes), "but the polygon list holds",
                      "an index exactly equal to the polygon count");
    }
    {
        // A polygon just outside its cell's square. Cell 1 spans 0..32 on
        // slot 0, so a polygon at 36..40 is four units clear of it and
        // still well inside the -16..48 a doubled square would cover: a
        // rule that measured the half extent wrongly would accept it.
        Image image = sample_image();
        image.polygons[0] = flat_quad(38.0F, 16.0F, 2.0F, 1.0F);
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "which does not overlap it",
                      "a polygon four units outside the cell that names it");
    }
    {
        // A cell centre one world unit from where the tree puts it. The
        // rule is meant to be exact, not merely sane.
        Layout layout;
        Bytes bytes = build_image(sample_image(), layout);
        write_f32(bytes, layout.cell_data + kCellRecord, 17.0F);
        check_refused(parse(bytes), "but the quadtree places it at",
                      "a cell centre one unit off the tree");
    }
    {
        // A polygon list whose payload is a whole number of 16-byte units
        // but not of 32-byte records.
        Image image = sample_image();
        image.polygon_extra_bytes = 16;
        Layout layout;
        const Bytes bytes = build_image(image, layout);
        check_refused(parse(bytes), "not a whole number of 32-byte records",
                      "a polygon payload that is a multiple of 16 only");
    }
}

// -------------------------------------------------------------------
// Query edges: a polygon named by two cells, exact-touch comparisons, and
// a cap that must admit exactly what it says.
// -------------------------------------------------------------------

void test_query_edges() {
    // A polygon straddling the boundary of two cells, named by both, as
    // retail names them.
    Image image;
    image.polygons.push_back(flat_quad(32.0F, 16.0F, 8.0F, 1.0F));
    Cell a;
    a.center[0] = 16.0F;
    a.center[1] = 16.0F;
    a.polygons = {0};
    Cell b;
    b.center[0] = 48.0F;
    b.center[1] = 16.0F;
    b.polygons = {0};
    image.cells = {a, b};
    Layout layout;
    const Bytes bytes = build_image(image, layout);
    const nfsu::TrackCollisionReport report = parse(bytes);
    check(report.valid, "query edges: the straddling image parses" +
                            (report.issues.empty()
                                 ? std::string()
                                 : " (" + report.issues[0].message + ")"));

    std::vector<std::size_t> found;
    const float mn[3] = {0.0F, 0.0F, -10.0F};
    const float mx[3] = {64.0F, 32.0F, 10.0F};
    check(nfsu::query_track_collision_box(report, mn, mx, found),
          "query edges: a box over both cells succeeds");
    const std::vector<std::size_t> once = {0};
    check(found == once,
          "query edges: a polygon named by two cells is returned once");

    {
        // The query's low face exactly on the shared cell edge. The cell
        // to its left ends there and must still be searched.
        std::vector<std::size_t> cells;
        check(nfsu::query_track_collision_cells(report, 32.0F, 16.0F, 40.0F,
                                                16.0F, cells),
              "query edges: a box starting on a cell edge succeeds");
        check(cells.size() == 2,
              "query edges: a box on a cell edge touches both cells");
    }
    {
        // The query's low face exactly on the polygon's own maximum. The
        // polygon touches it and must be kept.
        const float touch_min[3] = {40.0F, 16.0F, 1.0F};
        const float touch_max[3] = {48.0F, 20.0F, 2.0F};
        check(nfsu::query_track_collision_box(report, touch_min, touch_max,
                                              found),
              "query edges: a box touching the polygon's face succeeds");
        check(found == once,
              "query edges: a polygon touching the query face is kept");
    }
    {
        // A cap equal to the number of cells the query touches must be
        // enough, and one less must not be.
        nfsu::TrackCollisionQueryLimits limits;
        limits.max_cells = 2;
        std::vector<std::size_t> cells;
        check(nfsu::query_track_collision_cells(report, 0.0F, 0.0F, 64.0F,
                                                32.0F, cells, limits),
              "query edges: a cap of exactly two admits two cells");
        check(cells.size() == 2, "query edges: and returns both");
        limits.max_cells = 1;
        check(!nfsu::query_track_collision_cells(report, 0.0F, 0.0F, 64.0F,
                                                 32.0F, cells, limits),
              "query edges: a cap of one refuses a two-cell query");
    }
}

} // namespace

int main() {
    test_constants();
    test_jdlz();
    test_valid_image();
    test_empty_and_missing();
    test_malformed_chunks();
    test_malformed_indices();
    test_malformed_tree();
    test_malformed_bounds();
    test_limits();
    test_queries();
    test_ray_details();
    test_cost_counters();
    test_load();
    test_vertical_base();
    test_boundaries();
    test_query_edges();

    std::cout << "track collision tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
