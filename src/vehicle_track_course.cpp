#include "nfsu/vehicle_track.h"

#include "nfsu/det_sqrt.h"

#include <cstring>
#include <map>
#include <utility>

namespace nfsu {
namespace {

using Bytes = std::vector<unsigned char>;

// The chunk ids and layout parse_track_collision reads
// (docs/TRACK_COLLISION.md): three of the five chunks start their records
// on a 16-byte boundary of the image, padded with 0x11.
constexpr std::uint32_t kTreeChunk = 0x00034130U;
constexpr std::uint32_t kCellChunk = 0x00034131U;
constexpr std::uint32_t kPolygonChunk = 0x00034132U;
constexpr std::uint32_t kIndexChunk = 0x00034133U;
constexpr std::uint32_t kBoundsChunk = 0x00034134U;
constexpr std::size_t kAlignment = 16;
constexpr unsigned char kFiller = 0x11;
constexpr float kCellSize = 32.0F;

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

// A value on a grid of 1/scale within an int16, exactly.
bool on_grid(float value, float scale, std::int16_t& out) {
    const float scaled = value * scale;
    if (!(scaled >= -32767.0F && scaled <= 32767.0F)) {
        return false;
    }
    const long whole = static_cast<long>(scaled);
    if (static_cast<float>(whole) != scaled) {
        return false;
    }
    out = static_cast<std::int16_t>(whole);
    return true;
}

// The floor of a float as a whole number, for the vertical base and for
// the cell a coordinate falls in; inputs here are small and exact.
long floor_long(float value) {
    long whole = static_cast<long>(value);
    if (static_cast<float>(whole) > value) {
        --whole;
    }
    return whole;
}

struct Record {
    std::int16_t base = 0;
    std::int16_t slot0[4] = {0, 0, 0, 0};
    std::int16_t slot1[4] = {0, 0, 0, 0};
    std::int16_t slot2[4] = {0, 0, 0, 0};
};

// The quadtree path to a leaf cell's centre, as the collision tree lays it
// out: ten levels under a root of half extent 16384, slot bit 0 for +px
// and bit 1 for +pz.
std::vector<std::size_t> cell_path(float x, float z) {
    std::vector<std::size_t> path;
    float cx = 0.0F;
    float cz = 0.0F;
    float half = kCollisionRootHalfExtent;
    for (std::uint32_t depth = 0; depth < kCollisionTreeDepth; ++depth) {
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

// The proving ground's layout, docs/VEHICLE_TRACK.md "The proving ground".
constexpr float kGroundHeight = 10.0F;
constexpr float kPlateauHeight = 16.0F;
constexpr float kWallBottom = 4.0F;
constexpr float kWallTop = 24.0F;

VehicleTrackQuad flat(float x0, float z0, float x1, float z1, float py) {
    VehicleTrackQuad q;
    const float corners[4][3] = {{x0, z0, py}, {x1, z0, py}, {x1, z1, py}, {x0, z1, py}};
    std::memcpy(q.corner, corners, sizeof(corners));
    return q;
}

// A plane rising along +px from h0 at x0 to h1 at x1.
VehicleTrackQuad ramp(float x0, float z0, float x1, float z1, float h0, float h1) {
    VehicleTrackQuad q;
    const float corners[4][3] = {{x0, z0, h0}, {x1, z0, h1}, {x1, z1, h1}, {x0, z1, h0}};
    std::memcpy(q.corner, corners, sizeof(corners));
    return q;
}

// A vertical wall from (x0, z0) to (x1, z1).
VehicleTrackQuad wall(float x0, float z0, float x1, float z1) {
    VehicleTrackQuad q;
    const float corners[4][3] = {{x0, z0, kWallBottom},
                                 {x0, z0, kWallTop},
                                 {x1, z1, kWallTop},
                                 {x1, z1, kWallBottom}};
    std::memcpy(q.corner, corners, sizeof(corners));
    return q;
}

// Road in 32-unit squares, so that the proving ground has seams everywhere,
// as a retail road has.
void add_flat(std::vector<VehicleTrackQuad>& quads, float x0, float z0, float x1, float z1,
              float py) {
    for (float x = x0; x < x1; x += kCellSize) {
        const float xe = x + kCellSize < x1 ? x + kCellSize : x1;
        for (float z = z0; z < z1; z += kCellSize) {
            const float ze = z + kCellSize < z1 ? z + kCellSize : z1;
            quads.push_back(flat(x, z, xe, ze, py));
        }
    }
}

void add_wall(std::vector<VehicleTrackQuad>& quads, float x0, float z0, float x1, float z1,
              int panels) {
    for (int i = 0; i < panels; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(panels);
        const float b = static_cast<float>(i + 1) / static_cast<float>(panels);
        quads.push_back(wall(x0 + (x1 - x0) * a, z0 + (z1 - z0) * a, x0 + (x1 - x0) * b,
                             z0 + (z1 - z0) * b));
    }
}

TrackPathZone zone_box(std::uint32_t type, float x0, float z0, float x1, float z1,
                       std::int8_t direction_filter) {
    TrackPathZone zone;
    zone.type = type;
    zone.direction_filter = direction_filter;
    zone.box_min[0] = x0;
    zone.box_min[1] = z0;
    zone.box_max[0] = x1;
    zone.box_max[1] = z1;
    zone.vertex_count = 4;
    zone.vertices = {x0, z0, x1, z0, x1, z1, x0, z1};
    return zone;
}

VehicleScriptSegment segment(std::uint32_t steps, double throttle, double brake, double steer) {
    VehicleScriptSegment s;
    s.steps = steps;
    s.input.throttle = throttle;
    s.input.brake = brake;
    s.input.steer = steer;
    return s;
}

VehicleTrackScenario scenario(const char* name, const char* description, double forward,
                              double left, double up, std::int32_t turn,
                              std::vector<VehicleScriptSegment> segments, const char* pinned,
                              double speed = 0.0) {
    VehicleTrackScenario s;
    s.name = name;
    s.description = description;
    s.forward = forward;
    s.left = left;
    s.up = up;
    s.turn = turn;
    s.script.name = name;
    s.script.segments = std::move(segments);
    s.script.speed = speed;
    s.pinned = pinned;
    return s;
}

// 15 degrees in 16-bit units, 65536 * 15 / 360 = 2730.67, rounded.
constexpr std::int32_t kTurn15 = 2731;
constexpr std::int32_t kTurn45 = 0x2000;
constexpr std::int32_t kTurn60 = 0x2AAB;
constexpr std::int32_t kTurn90 = 0x4000;

std::vector<VehicleTrackScenario> proving_ground_scenarios() {
    // The start is (32, 48) facing +x; forward is +x and left is +y.
    return {
        scenario("rest", "at the start, the brake held for two seconds", 0.0, 0.0, 0.0, 0,
                 {segment(240, 0.0, 1.0, 0.0)},
                 "C67E6C0839F2CF29F89CA1381BC16A17A7DB2F9BF04944BE23A7313B814158C9"),
        scenario("drive", "full throttle for six seconds from (64, 48) along +x, across the "
                          "road's seams at px 96 and 128",
                 32.0, 0.0, 0.0, 0, {segment(720, 1.0, 0.0, 0.0)},
                 "2D61E7FC354F7394BA4DC2721298041E7799D276BF3F9075C18DBB114D3BB79C"),
        scenario("slope", "full throttle from (152, 48) up the 8.5-degree ramp at px 192..232 "
                          "onto the plateau",
                 120.0, 0.0, 0.0, 0, {segment(780, 1.0, 0.0, 0.0)},
                 "44067B93976C930399611071C2D9E1E654F0AF665A37A808D3BDCBE71848A643"),
        scenario("wall", "full throttle from (32, 80) at 45 degrees into the north wall at "
                         "pz 96, then along it",
                 0.0, 32.0, 0.0, kTurn45, {segment(480, 1.0, 0.0, 0.0)},
                 "B9B4B315848D042E245E21B075E6A8C61472ABB8437D0F40B35C7BCF8187F563"),
        scenario("wall-glance", "full throttle from (32, 88) at 15 degrees into the north "
                                "wall",
                 0.0, 40.0, 0.0, kTurn15, {segment(600, 1.0, 0.0, 0.0)},
                 "443829D8BE0773FD221BFB92DF5E3C3061D40D4EEECAB34BCA797BAB10C9745D"),
        scenario("wall-fast", "from (32, 88) at 35 m/s, 15 degrees into the north wall, "
                              "full throttle: an upright car above 30 m/s",
                 0.0, 40.0, 0.0, kTurn15, {segment(240, 1.0, 0.0, 0.0)},
                 "D6670320A5F6045C8BC2E3E6F3877193F7A0FDD94411BA45D5AF83016E25E27E", 35.0),
        scenario("wall-square", "full throttle from (48, 80) straight at the north wall",
                 16.0, 32.0, 0.0, kTurn90, {segment(480, 1.0, 0.0, 0.0)},
                 "5951C8C7355ACB186C5FC3ACAF96635B9AA704706F5CDDF275F3F83982EC05A6"),
        scenario("wall-oblique", "full throttle from (128, 48) along -y into the oblique wall "
                                 "from (112, 8) to (160, 36)",
                 96.0, 0.0, 0.0, -kTurn90, {segment(480, 1.0, 0.0, 0.0)},
                 "BDF62E339B53C01152B541BA871DD3B05492387EB4E3B90A3BF606249A13E4E5"),
        scenario("drop", "dropped from five metres above the start, no input", 0.0, 0.0, 5.0,
                 0, {segment(360, 0.0, 0.0, 0.0)},
                 "AD556D8D302B12148ACCCE2120B6B2CEA9F5D2AEE0839DE7774F5FCA09746488"),
        scenario("leave", "full throttle from (176, 20) along -y off the open edge at pz 0, "
                          "over no cell: out of bounds, then reset",
                 144.0, -28.0, 0.0, -kTurn90, {segment(600, 1.0, 0.0, 0.0)},
                 "80623E22996DAFF39A40B8720B370FA695F8DAC54FF2EBB8D0AB9A1520D80D99"),
        scenario("zone", "0.6 throttle from (128, 60) along +y into the RESET zone at "
                         "pz 80..92",
                 96.0, 12.0, 0.0, kTurn90, {segment(600, 0.6, 0.0, 0.0)},
                 "2D32DB2D7F4C1894B5A4CC2E6754CCAFD9416B7BE9362D0B459868BA99CF6166"),
        scenario("zone-point", "0.6 throttle from (152, 60) along +y into the RESET_TO_POINT "
                               "zone, which sends the car to (40, 56) at 8 m/s",
                 120.0, 12.0, 0.0, kTurn90,
                 {segment(480, 0.6, 0.0, 0.0), segment(240, 0.0, 0.0, 0.0)},
                 "B8214FE628881FFECAC36AAAFAB9454F0C2D7626EB502955A3BE59FD3F386894"),
        scenario("far", "at rest on the pad at (-4032, -3040), more than 5000 from the "
                        "route: off the path, then reset",
                 -4064.0, -3088.0, 0.0, 0, {segment(240, 0.0, 0.0, 0.0)},
                 "6340A7032F1A09177E3145C9ED5EC98B07DE7D80B456697D258C3A353FC67201"),
    };
}

std::vector<VehicleTrackScenario> circuit_scenarios() {
    // The start is grid slot 0 of event 1 on track 1003, facing +y.
    return {
        scenario("grid", "on grid slot 0, the brake held for three seconds", 0.0, 0.0, 0.0, 0,
                 {segment(360, 0.0, 1.0, 0.0)},
                 "AEA0F8766D3A88CEF6BDC133570E7196095B75FC8980A7F72BC8404C31EAFDEF"),
        scenario("drive", "full throttle for four seconds from the grid along the road", 0.0,
                 0.0, 0.0, 0, {segment(480, 1.0, 0.0, 0.0)},
                 "F1D2B78F4FCC2A1B591392BB9CB9243DAA9B2CC6130CC553D0E6C0D0CB72015A"),
        scenario("wall", "full throttle from the grid turned 60 degrees left, into the wall "
                         "10.6 m to the left, then along it",
                 0.0, 0.0, 0.0, kTurn60, {segment(480, 1.0, 0.0, 0.0)},
                 "00C09F45E857B2F0D98BF378989D8478BD3B05205149A949820E9A0A098E7820"),
        scenario("wall-square", "full throttle from the grid turned 90 degrees left, "
                                "straight at that wall",
                 0.0, 0.0, 0.0, kTurn90, {segment(360, 1.0, 0.0, 0.0)},
                 "EA840FAA7F5CBDBC700C6B68B4AEAD342F0AED63D6DA687F8F83BA5A529ABDDB"),
        scenario("drop", "dropped from five metres above the grid, no input", 0.0, 0.0, 5.0, 0,
                 {segment(360, 0.0, 0.0, 0.0)},
                 "B84FBE418214241190BB6CB3519B717AABBC122BD6E45566F8FAE017E03D5158"),
        scenario("leave", "20 m left of the grid, over no collision cell, no input: it falls, "
                          "is out of bounds, and is reset",
                 0.0, 20.0, 1.0, 0, {segment(360, 0.0, 0.0, 0.0)},
                 "3F95D4D51E3006F4B8C811B8E6F427CEC3B7425F7DFFDD4DB98E5EB6F731ABA4"),
    };
}

} // namespace

// ---------------------------------------------------------------------------
// A synthetic collision image
// ---------------------------------------------------------------------------

std::vector<unsigned char> build_vehicle_track_collision_image(
    const std::vector<VehicleTrackQuad>& quads,
    std::string& error) {
    error.clear();
    if (quads.empty() || quads.size() > 65535U) {
        error = "a collision image holds 1 to 65535 quads";
        return {};
    }
    std::vector<Record> records(quads.size());
    // Cells by (column, row) of the 32-unit grid, in order, each naming the
    // quads whose closed horizontal box meets its half-open square.
    std::map<std::pair<long, long>, std::vector<std::uint16_t>> cells;
    for (std::size_t q = 0; q < quads.size(); ++q) {
        const VehicleTrackQuad& quad = quads[q];
        Record& record = records[q];
        float low_py = quad.corner[0][2];
        float box[4] = {quad.corner[0][0], quad.corner[0][1], quad.corner[0][0],
                        quad.corner[0][1]};
        for (std::size_t c = 0; c < 4; ++c) {
            low_py = quad.corner[c][2] < low_py ? quad.corner[c][2] : low_py;
            box[0] = quad.corner[c][0] < box[0] ? quad.corner[c][0] : box[0];
            box[1] = quad.corner[c][1] < box[1] ? quad.corner[c][1] : box[1];
            box[2] = quad.corner[c][0] > box[2] ? quad.corner[c][0] : box[2];
            box[3] = quad.corner[c][1] > box[3] ? quad.corner[c][1] : box[3];
        }
        const long base = floor_long(low_py);
        if (!(base >= -32767 && base <= 32767)) {
            error = "quad " + std::to_string(q) + ": its base is outside an int16";
            return {};
        }
        record.base = static_cast<std::int16_t>(base);
        for (std::size_t c = 0; c < 4; ++c) {
            if (!on_grid(quad.corner[c][0], 8.0F, record.slot0[c]) ||
                !on_grid(quad.corner[c][1], 8.0F, record.slot1[c]) ||
                !on_grid(quad.corner[c][2] - static_cast<float>(base), 256.0F,
                         record.slot2[c])) {
                error = "quad " + std::to_string(q) + " corner " + std::to_string(c) +
                        " is off the format's grids (eighths across, 256ths up)";
                return {};
            }
        }
        const long c0 = floor_long(box[0] / kCellSize);
        const long c1 = floor_long(box[2] / kCellSize);
        const long r0 = floor_long(box[1] / kCellSize);
        const long r1 = floor_long(box[3] / kCellSize);
        for (long column = c0; column <= c1; ++column) {
            for (long row = r0; row <= r1; ++row) {
                cells[{column, row}].push_back(static_cast<std::uint16_t>(q));
            }
        }
    }
    if (cells.size() + 1 > 65535U) {
        error = "too many cells for a u16 index";
        return {};
    }

    // The quadtree, cell 0 being the sentinel the parser expects first.
    struct Node {
        std::uint16_t entries[4] = {0, 0, 0, 0};
    };
    std::vector<Node> nodes(1);
    std::vector<std::pair<float, float>> centres;
    std::vector<const std::vector<std::uint16_t>*> lists;
    centres.push_back({999999.0F, 999999.0F});
    lists.push_back(nullptr);
    for (const auto& cell : cells) {
        const float x = static_cast<float>(cell.first.first) * kCellSize + kCellSize * 0.5F;
        const float z = static_cast<float>(cell.first.second) * kCellSize + kCellSize * 0.5F;
        centres.push_back({x, z});
        lists.push_back(&cell.second);
        const std::vector<std::size_t> path = cell_path(x, z);
        std::size_t node = 0;
        for (std::size_t d = 0; d + 1 < path.size(); ++d) {
            std::uint16_t next = nodes[node].entries[path[d]];
            if (next == 0) {
                if (nodes.size() >= 65535U) {
                    error = "too many quadtree nodes for a u16 entry";
                    return {};
                }
                nodes.push_back(Node{});
                next = static_cast<std::uint16_t>(nodes.size() - 1);
                nodes[node].entries[path[d]] = next;
            }
            node = next;
        }
        nodes[node].entries[path.back()] = static_cast<std::uint16_t>(centres.size() - 1);
    }

    Bytes bytes;
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
    for (std::size_t c = 0; c < centres.size(); ++c) {
        append_f32(bytes, centres[c].first);
        append_f32(bytes, centres[c].second);
        const std::size_t count = lists[c] == nullptr ? 0 : lists[c]->size();
        append_u32(bytes, static_cast<std::uint32_t>(indices.size()));
        append_u32(bytes, static_cast<std::uint32_t>(count));
        if (lists[c] != nullptr) {
            indices.insert(indices.end(), lists[c]->begin(), lists[c]->end());
        }
    }
    write_u32(bytes, header + 4, static_cast<std::uint32_t>(bytes.size() - header - 8));

    append_u32(bytes, kPolygonChunk);
    append_u32(bytes, static_cast<std::uint32_t>(records.size() * 32U));
    for (const Record& r : records) {
        for (int i = 0; i < 4; ++i) {
            bytes.push_back(0);
        }
        append_u16(bytes, static_cast<std::uint16_t>(r.base));
        append_u16(bytes, 0xFFFFU);
        for (std::size_t i = 0; i < 4; ++i) {
            append_u16(bytes, static_cast<std::uint16_t>(r.slot0[i]));
        }
        for (std::size_t i = 0; i < 4; ++i) {
            append_u16(bytes, static_cast<std::uint16_t>(r.slot1[i]));
        }
        for (std::size_t i = 0; i < 4; ++i) {
            append_u16(bytes, static_cast<std::uint16_t>(r.slot2[i]));
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
    for (const Record& r : records) {
        for (std::size_t c = 0; c < 4; ++c) {
            const float point[3] = {static_cast<float>(r.slot0[c]) / 8.0F,
                                    static_cast<float>(r.slot1[c]) / 8.0F,
                                    static_cast<float>(r.base) +
                                        static_cast<float>(r.slot2[c]) / 256.0F};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (first || point[axis] < low[axis]) {
                    low[axis] = point[axis];
                }
                if (first || point[axis] > high[axis]) {
                    high[axis] = point[axis];
                }
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

// ---------------------------------------------------------------------------
// Courses
// ---------------------------------------------------------------------------

bool vehicle_track_grid_pose(const VehicleTrackCourse& course,
                             int slot,
                             VehiclePose& pose,
                             int& marker,
                             std::string& error) {
    marker = -1;
    if (!course.has_route) {
        error = "the course has no route, so no grid";
        return false;
    }
    const char* name =
        course.route.reverse ? "BackwardStartingPosition" : "ForwardStartingPosition";
    const std::uint32_t hash = track_marker_hash(name);
    for (std::size_t m = 0; m < course.track.markers.size(); ++m) {
        const TrackMarker& candidate = course.track.markers[m];
        if (candidate.name_hash == hash &&
            candidate.param == static_cast<std::uint32_t>(slot)) {
            marker = static_cast<int>(m);
            break;
        }
    }
    if (marker < 0) {
        error = std::string("the track holds no ") + name + " marker numbered " +
                std::to_string(slot);
        return false;
    }
    const TrackMarker& grid = course.track.markers[static_cast<std::size_t>(marker)];
    const float from[3] = {grid.position[0], grid.position[1], grid.position[2] + 2.0F};
    const float to[3] = {grid.position[0], grid.position[1], grid.position[2] - 8.0F};
    TrackContact contact;
    if (query_track_contact_segment(course.world, from, to, contact) !=
        TrackContactStatus::hit) {
        error = "no surface lies under the grid marker within 2 above and 8 below it";
        return false;
    }
    pose = VehiclePose{};
    pose.ground.x = static_cast<double>(grid.position[0]);
    pose.ground.y = static_cast<double>(grid.position[1]);
    pose.ground.z = static_cast<double>(contact.point[2]);
    pose.heading = vehicle_track_heading(grid.angle);
    pose.speed = 0.0;
    return true;
}

bool load_vehicle_track_course(const std::filesystem::path& trackb,
                               int laps,
                               int direction,
                               std::int8_t lap_skip,
                               int slot,
                               VehicleTrackCourse& course,
                               std::string& error) {
    course = VehicleTrackCourse{};
    course.name = trackb.filename().string();
    course.world = load_track_contact_world(trackb);
    if (!course.world.valid) {
        error = "the collision of " + course.name + " did not load";
        return false;
    }
    std::vector<unsigned char> image;
    course.track = load_race_route_track(trackb, image);
    if (!course.track.ok()) {
        error = "the route of " + course.name + " did not parse";
        return false;
    }
    if (!build_race_route(course.track, laps, direction, lap_skip, course.route, error)) {
        return false;
    }
    course.has_route = true;
    return vehicle_track_grid_pose(course, slot, course.start, course.start_marker, error);
}

bool build_vehicle_track_course(const VehicleTrackLayout& layout,
                                VehicleTrackCourse& course,
                                std::string& error) {
    course = VehicleTrackCourse{};
    course.name = layout.name;
    const std::vector<unsigned char> image =
        build_vehicle_track_collision_image(layout.quads, error);
    if (image.empty()) {
        return false;
    }
    course.world =
        build_track_contact_world(parse_track_collision(image.data(), image.size(), 0));
    if (!course.world.valid) {
        error = "the collision image did not parse";
        return false;
    }
    if (layout.loop.empty()) {
        const float from[3] = {layout.grid[0], layout.grid[1], layout.grid[2] + 2.0F};
        const float to[3] = {layout.grid[0], layout.grid[1], layout.grid[2] - 8.0F};
        TrackContact contact;
        if (query_track_contact_segment(course.world, from, to, contact) !=
            TrackContactStatus::hit) {
            error = "no surface lies under the grid point within 2 above and 8 below it";
            return false;
        }
        course.start.ground.x = static_cast<double>(layout.grid[0]);
        course.start.ground.y = static_cast<double>(layout.grid[1]);
        course.start.ground.z = static_cast<double>(contact.point[2]);
        course.start.heading = vehicle_track_heading(layout.grid_angle);
        return true;
    }
    if (layout.loop.size() < 3 || layout.loop.size() > 16000U) {
        error = "a route loop holds 3 to 16000 points";
        return false;
    }

    RaceRouteTrack& track = course.track;
    for (const std::array<float, 2>& point : layout.loop) {
        TrackPathNode node;
        node.x = point[0];
        node.y = point[1];
        node.path = 0;
        track.nodes.push_back(node);
    }
    const std::size_t count = track.nodes.size();
    TrackPathNode closing = track.nodes.front();
    track.nodes.push_back(closing);
    TrackPathRecord path;
    path.closed = 1;
    path.first = 0;
    path.end = static_cast<std::int16_t>(count);
    path.count = static_cast<std::int16_t>(count);
    path.distance_scale = 1.0F;
    path.distance_offset = 0.0F;
    track.paths.push_back(path);
    track.zones = layout.zones;
    TrackMarker grid;
    grid.name_hash = track_marker_hash("ForwardStartingPosition");
    grid.param = 0;
    grid.position[0] = layout.grid[0];
    grid.position[1] = layout.grid[1];
    grid.position[2] = layout.grid[2];
    grid.angle = layout.grid_angle;
    track.markers.push_back(grid);
    track.valid = true;

    // The route layer refuses a loop whose record does not measure it as 0
    // to 100 percent to within 1: the chord length is tried, then shorter
    // and longer a percent at a time up to a fifth, and then the spline's
    // own length is written back.
    float chords = 0.0F;
    for (std::size_t i = 0; i < count; ++i) {
        const std::array<float, 2>& a = layout.loop[i];
        const std::array<float, 2>& b = layout.loop[(i + 1) % count];
        const float dx = b[0] - a[0];
        const float dy = b[1] - a[1];
        chords = chords + static_cast<float>(det_sqrt(
                              static_cast<double>(dy * dy + dx * dx)));
    }
    TrackPathSpline spline;
    bool measured = false;
    for (int k = 0; k <= 40 && !measured; ++k) {
        const float percent = static_cast<float>((k + 1) / 2) * (k % 2 == 0 ? 0.01F : -0.01F);
        track.paths.front().length = chords * (1.0F - percent);
        std::string ignored;
        measured = build_track_path_spline(track, false, spline, ignored);
    }
    if (!measured) {
        error = "the route does not measure as a loop";
        return false;
    }
    track.paths.front().length = spline.spline_length;
    if (!build_track_path_spline(track, false, spline, error)) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        track.nodes[i].distance = spline.points[i * 3 + 2];
    }
    if (!build_race_route(track, 1, 0, 0, course.route, error)) {
        return false;
    }
    course.has_route = true;
    return vehicle_track_grid_pose(course, 0, course.start, course.start_marker, error);
}

bool build_vehicle_proving_ground(VehicleTrackCourse& course, std::string& error) {
    VehicleTrackLayout layout;
    layout.name = "proving-ground";
    std::vector<VehicleTrackQuad>& quads = layout.quads;
    // Road: two slabs at 10 from px 0 to 192, the ramp to 16 from 192 to
    // 232, the plateau to 288, and a pad more than 5000 from the route,
    // near the corner of the format's range (+-4095.875 across).
    add_flat(quads, 0.0F, 0.0F, 192.0F, 96.0F, kGroundHeight);
    for (float z = 0.0F; z < 96.0F; z += kCellSize) {
        quads.push_back(ramp(192.0F, z, 232.0F, z + kCellSize, kGroundHeight, kPlateauHeight));
    }
    add_flat(quads, 232.0F, 0.0F, 288.0F, 96.0F, kPlateauHeight);
    add_flat(quads, -4064.0F, -3072.0F, -4000.0F, -3008.0F, kGroundHeight);
    // Walls: north along pz 96, west along px 0, the west half of the south
    // edge, east along px 288, and an oblique one; the south edge from
    // px 96 to 288 is open.
    add_wall(quads, 0.0F, 96.0F, 288.0F, 96.0F, 9);
    add_wall(quads, 0.0F, 0.0F, 0.0F, 96.0F, 3);
    add_wall(quads, 0.0F, 0.0F, 96.0F, 0.0F, 3);
    add_wall(quads, 288.0F, 0.0F, 288.0F, 96.0F, 3);
    add_wall(quads, 112.0F, 8.0F, 160.0F, 36.0F, 2);

    // The route: a closed loop, anticlockwise seen from above, along pz 40
    // east, px 176 north, pz 72 west and px 16 south, a node every 16.
    for (float x = 16.0F; x <= 176.0F; x += 16.0F) {
        layout.loop.push_back({x, 40.0F});
    }
    layout.loop.push_back({176.0F, 56.0F});
    for (float x = 176.0F; x >= 16.0F; x -= 16.0F) {
        layout.loop.push_back({x, 72.0F});
    }
    layout.loop.push_back({16.0F, 56.0F});
    // A RESET zone and, for a forward race, a RESET_TO_POINT zone that
    // sends the car to (40, 56) facing +x.
    layout.zones.push_back(zone_box(kZoneReset, 120.0F, 80.0F, 136.0F, 92.0F, -1));
    TrackPathZone point = zone_box(kZoneResetToPoint, 144.0F, 80.0F, 160.0F, 92.0F, 0);
    point.point[0] = 40.0F;
    point.point[1] = 56.0F;
    point.direction[0] = 1.0F;
    point.direction[1] = 0.0F;
    layout.zones.push_back(point);
    // Grid slot 0 at (32, 48), facing +x.
    layout.grid[0] = 32.0F;
    layout.grid[1] = 48.0F;
    layout.grid[2] = kGroundHeight;
    layout.grid_angle = 0;
    return build_vehicle_track_course(layout, course, error);
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

const std::vector<VehicleTrackScenario>& vehicle_track_scenarios(VehicleTrackCourseKind kind) {
    static const std::vector<VehicleTrackScenario> proving = proving_ground_scenarios();
    static const std::vector<VehicleTrackScenario> circuit = circuit_scenarios();
    return kind == VehicleTrackCourseKind::proving_ground ? proving : circuit;
}

bool vehicle_track_scenario(VehicleTrackCourseKind kind, const std::string& name,
                            VehicleTrackScenario& out) {
    for (const VehicleTrackScenario& s : vehicle_track_scenarios(kind)) {
        if (s.name == name) {
            out = s;
            return true;
        }
    }
    return false;
}

VehiclePose vehicle_track_scenario_pose(const VehiclePose& start,
                                        const VehicleTrackScenario& scenario) noexcept {
    const double c = vehicle_cos(start.heading);
    const double s = vehicle_sin(start.heading);
    VehiclePose pose = start;
    pose.ground.x = start.ground.x + c * scenario.forward - s * scenario.left;
    pose.ground.y = start.ground.y + s * scenario.forward + c * scenario.left;
    pose.ground.z = start.ground.z + scenario.up;
    pose.heading = start.heading + vehicle_track_heading(scenario.turn);
    pose.speed = scenario.script.speed;
    pose.gearbox = scenario.script.gearbox;
    return pose;
}

} // namespace nfsu
