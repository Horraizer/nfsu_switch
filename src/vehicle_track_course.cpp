#include "nfsu/vehicle_track.h"

#include "nfsu/car_collision_hull.h"

#include <cmath>



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



// Portable regression records after the recovered selector1 callbacks.

// Their operation-level native oracles are separate; these are not original-game captures.

std::vector<VehicleTrackScenario> proving_ground_scenarios() {

    // The start is (32, 48) facing +x; forward is +x and left is +y.

    return {

        scenario("rest", "at the start, the brake held for two seconds", 0.0, 0.0, 0.0, 0,

                 {segment(240, 0.0, 1.0, 0.0)},

                 "2786EEED0625538554ACA9673003D1303B4E3A6602F05F16173F23DF14F35EF6"),

        scenario("drive", "full throttle for six seconds from (64, 48) along +x, across the "

                          "road's seams at px 96 and 128",

                 32.0, 0.0, 0.0, 0, {segment(720, 1.0, 0.0, 0.0)},

                 "606CF4053F5719957B63F757A8B78A411B36BC594F2EF9D0DDD5A8450B105DFB"),

        scenario("slope", "full throttle from (152, 48) up the 8.5-degree ramp at px 192..232 "

                          "onto the plateau",

                 120.0, 0.0, 0.0, 0, {segment(780, 1.0, 0.0, 0.0)},

                 "E124F9D34411E5BDED01400D884DFA03378F199CB682A721A0D49559C88E9F28"),

        scenario("wall", "full throttle from (32, 80) at 45 degrees into the north wall at "

                         "pz 96, then along it",

                 0.0, 32.0, 0.0, kTurn45, {segment(480, 1.0, 0.0, 0.0)},

                 "3E57CAD1C2962B938602A134394B93EB07C958D20B9827F23C0F52046CDB81AF"),

        scenario("wall-glance", "full throttle from (32, 88) at 15 degrees into the north "

                                "wall",

                 0.0, 40.0, 0.0, kTurn15, {segment(600, 1.0, 0.0, 0.0)},

                 "F10C543374EEBA519020F3036BB5CB765887862F40A3884A8033F39CD380256A"),

        scenario("wall-fast", "from (32, 88) at 35 m/s, 15 degrees into the north wall, "

                              "full throttle: an upright car above 30 m/s",

                 0.0, 40.0, 0.0, kTurn15, {segment(240, 1.0, 0.0, 0.0)},

                 "AF1A29DACB2FD3555CA9B0351B8F50BF9BD47AA377D7A1875352C8C5313A9F50", 35.0),

        scenario("wall-square", "full throttle from (48, 80) straight at the north wall",

                 16.0, 32.0, 0.0, kTurn90, {segment(480, 1.0, 0.0, 0.0)},

                 "3DFC3A3EC1FD84FB56923C86F6715FEA5CAD50EEC7E3FE39B845DA1A17960815"),

        scenario("wall-oblique", "full throttle from (128, 48) along -y into the oblique wall "

                                 "from (112, 8) to (160, 36)",

                 96.0, 0.0, 0.0, -kTurn90, {segment(480, 1.0, 0.0, 0.0)},

                 "A66A940CF9AB197AEBB0F7A0974170AC7D8850E44AD37B302C0834412C79F90B"),

        scenario("drop", "dropped from five metres above the start, no input", 0.0, 0.0, 5.0,

                 0, {segment(360, 0.0, 0.0, 0.0)},

                 "A404CC5C429D971CE2915DA4CEC71B9AD9112C373148751885D11E6F87DAED53"),

        scenario("leave", "full throttle from (176, 20) along -y off the open edge at pz 0, "

                          "over no cell: out of bounds, then reset",

                 144.0, -28.0, 0.0, -kTurn90, {segment(600, 1.0, 0.0, 0.0)},

                 "B2D804AA75051CCDBB2E33FA9BDD7A6CC1230AD1C6B9B46BC73B923DD781AE47"),

        scenario("zone", "0.6 throttle at 8 m/s from (128, 60) along +y into the RESET zone at "

                         "pz 80..92",

                 96.0, 12.0, 0.0, kTurn90, {segment(600, 0.6, 0.0, 0.0)},

                 "DB925A690B40AF8D4ACD7E798E4B30BC299A903D05BD382E43EDA9DF20A1DB0B", 8.0),

        scenario("zone-point", "0.6 throttle at 8 m/s from (152, 60) along +y into the RESET_TO_POINT "

                               "zone, which sends the car to (40, 56) at 8 m/s",

                 120.0, 12.0, 0.0, kTurn90,

                 {segment(480, 0.6, 0.0, 0.0), segment(240, 0.0, 0.0, 0.0)},

                 "898196E5FBEFB9C453B795CF138469DA0AB7B1613633ED3A71F75279397C886B", 8.0),

        scenario("far", "at rest on the pad at (-4032, -3040), more than 5000 from the "

                        "route: off the path, then reset",

                 -4064.0, -3088.0, 0.0, 0, {segment(240, 0.0, 0.0, 0.0)},

                 "86B73FEAB6EA7691A816DE945D683C7EAE826BE40315CD934B4A87D603532831"),

    };

}



std::vector<VehicleTrackScenario> circuit_scenarios() {

    // The start is grid slot 0 of event 1 on track 1003, facing +y.

    return {

        scenario("grid", "on grid slot 0, the brake held for three seconds", 0.0, 0.0, 0.0, 0,

                 {segment(360, 0.0, 1.0, 0.0)},

                 "C3B7E039F5924D31424A96193F0CB83108F2F42286037147E3411E7848552AD6"),

        scenario("drive", "full throttle for four seconds from the grid along the road", 0.0,

                 0.0, 0.0, 0, {segment(480, 1.0, 0.0, 0.0)},

                 "5EB678B92163512210B2CEF1F83D9679D507FD55E317E92323B9B54C77636DA7"),

        scenario("wall", "full throttle from the grid turned 60 degrees left, into the wall "

                         "10.6 m to the left, then along it",

                 0.0, 0.0, 0.0, kTurn60, {segment(480, 1.0, 0.0, 0.0)},

                 "398D2C8BB150E5E1463A0133FC730FE19237671DD6EF6C968C5C1077C0B430CB"),

        scenario("wall-square", "full throttle from the grid turned 90 degrees left, "

                                "straight at that wall",

                 0.0, 0.0, 0.0, kTurn90, {segment(360, 1.0, 0.0, 0.0)},

                 "BE1363FF26B31F199DEEDFAE55607C334E2CB93A5F9BF65A390AFBA5A6061EC5"),

        scenario("drop", "dropped from five metres above the grid, no input", 0.0, 0.0, 5.0, 0,

                 {segment(360, 0.0, 0.0, 0.0)},

                 "45396D4BF2C8F5F55A93687E5981AB43F5735FB92074D73332C65B1F72005950"),

        scenario("leave", "20 m left of the grid, over no collision cell, no input: it falls, "

                          "is out of bounds, and is reset",

                 0.0, 20.0, 1.0, 0, {segment(360, 0.0, 0.0, 0.0)},

                 "9C3DAB2E16C85410DE87C7FF0D03541BFC8CD670C5553EA166C1A5EA4540F2F0"),

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

    // Original4407B0 places only flags&0x0A==0 polygons in the ground

    // partition consumed by440A40. Horizontal scenery (including1210's

    // roof above the backward grid) is not a starting road.

    const auto grid_road = [&](const TrackContact& hit) {

        return hit.kind == TrackContactKind::road && hit.up > 0.5F &&

            !(course.world.collision.polygons[hit.polygon].unknown0[3] & 0x0AU);

    };

    TrackContact contact;

    if (query_track_contact_segment(course.world, from, to, contact) !=

        TrackContactStatus::hit || !grid_road(contact)) {

        // A short column can hit scenery above the road. It must satisfy

        // the same driveable-surface predicate as the extended column.

        // The drag tracks' backward grid markers sit well above their road

        // (TRACKB1201's hang about 15 units over it; the forward markers of

        // the same bundles sit on it), so the short window widens into the

        // road-preferring column walk the reset placements use: down from

        // the missed window, the first road surface takes the car.

        const float deep[3] = {grid.position[0], grid.position[1],

                               grid.position[2] - 128.0F};

        float top[3] = {from[0], from[1], from[2]};

        TrackContact road;

        bool found = false;

        for (int surface = 0; surface < 32; ++surface) {

            if (query_track_contact_segment(course.world, top, deep, road) !=

                TrackContactStatus::hit) {

                break;

            }

            if (grid_road(road)) {

                found = true;

                break;

            }

            const float next[3] = {top[0], top[1], road.point[2] - 0.05F};

            if (next[2] <= deep[2]) break;

            top[2] = next[2];

        }

        if (!found) {

            // A sprint track's backward grid can hang over the void -

            // TRACKB1102's sits at the world edge (x 2320) with no surface

            // within 300 units below it. Fall back to the nearest path

            // node, on the road under it, facing along the spline: the

            // race starts one node off the line instead of nowhere.

            std::size_t best = 0;

            double best_d2 = 1.0e18;

            for (std::size_t i = 0; i < course.track.nodes.size(); ++i) {

                const double dx = static_cast<double>(course.track.nodes[i].x) -

                                  static_cast<double>(grid.position[0]);

                const double dy = static_cast<double>(course.track.nodes[i].y) -

                                  static_cast<double>(grid.position[1]);

                const double d2 = dx * dx + dy * dy;

                if (d2 < best_d2) { best_d2 = d2; best = i; }

            }

            const auto& node = course.track.nodes[best];

            float ntop[3] = {node.x, node.y, 128.0F};

            const float ndeep[3] = {node.x, node.y, -128.0F};

            TrackContact nroad;

            bool nfound = false;

            for (int surface = 0; surface < 32; ++surface) {

                if (query_track_contact_segment(course.world, ntop, ndeep, nroad) !=

                    TrackContactStatus::hit) {

                    break;

                }

                if (grid_road(nroad)) {

                    nfound = true;

                    break;

                }

                const float next[3] = {ntop[0], ntop[1], nroad.point[2] - 0.05F};

                if (next[2] <= ndeep[2]) break;

                ntop[2] = next[2];

            }

            if (!nfound) {

                error = "no road lies under the grid marker or its nearest node";

                return false;

            }

            pose = VehiclePose{};

            pose.ground.x = static_cast<double>(node.x);

            pose.ground.y = static_cast<double>(node.y);

            pose.ground.z = static_cast<double>(nroad.point[2]);

            float tangent[3] = {1.0F, 0.0F, 0.0F};

            if (course.has_route) {

                const double span = course.route.spline.open &&

                                            course.route.spline.count > 1

                                        ? static_cast<double>(course.route.spline.count - 1)

                                        : static_cast<double>(course.route.spline.count);

                const float parameter =

                    static_cast<float>(static_cast<double>(best) < span

                                           ? static_cast<double>(best)

                                           : span);

                evaluate_track_path(course.route.spline, parameter, 1, tangent);

                if (course.route.reverse) { tangent[0] = -tangent[0]; tangent[1] = -tangent[1]; }

            }

            pose.heading = std::atan2(static_cast<double>(tangent[1]),

                                      static_cast<double>(tangent[0]));

            pose.speed = 0.0;

            return true;

        }

        contact = road;

    }

    pose = VehiclePose{};

    pose.ground.x = static_cast<double>(grid.position[0]);

    pose.ground.y = static_cast<double>(grid.position[1]);

    pose.ground.z = static_cast<double>(contact.point[2]);

    pose.heading = vehicle_track_heading(grid.angle);

    pose.speed = 0.0;

    return true;

}



namespace {

bool load_support_faces(const std::vector<unsigned char>& image,int direction,

                        std::vector<VehicleSupportFace>& faces) {

    // 578880 publishes group zero WorldObjects when loading the track.

    // 578188 admits only records with a collision shape and a topology flag.

    for (std::size_t chunk=0;chunk+8<=image.size();) {

        const auto kind=hull_u32(image.data()+chunk);

        const auto size=hull_u32(image.data()+chunk+4);

        const auto start=chunk+8;

        if(size>image.size()-start)return false;

        const auto end=start+size;

        if(kind==0x3B300) {

            const auto aligned=(start+15)&~std::size_t(15);

            if(aligned>end||(end-aligned)%160)return false;

            for(auto at=aligned;at<end;at+=160) {

                const auto* record=image.data()+at;

                const auto shape=hull_u32(record+8);

                if(!shape||hull_u32(record+12)!=0||!hull_u32(record+104))continue;

                // 578820 skips TRSABC_Track_Jump in the reverse direction.

                if(direction==1&&hull_u32(record+4)==0x6691470DU)continue;

                // The qualifying retail records are unparented WorldObjects

                // with a named convex hull and no independently driven body.

                if(hull_u32(record)!=0x3E83CBDEU||hull_u32(record+116)!=0)return false;

                VehicleHull hull;

                if(!decode_car_collision_hull(image,shape,hull))return false;

                auto lo=hull.points.front(),hi=lo;

                for(const auto& p:hull.points)for(unsigned k=0;k<3;++k) {

                    if(p[k]<lo[k])lo[k]=p[k];

                    if(p[k]>hi[k])hi[k]=p[k];

                }

                std::array<float,16> pose;

                std::memcpy(pose.data(),record+16,64);

                for(float value:pose)if(!std::isfinite(value))return false;

                // 577450 builds the top of the local AABB, and 577730

                // transforms the four corners with the published pose.

                const std::array<std::array<float,3>,4> local{{

                    {lo[0],lo[1],hi[2]},{hi[0],lo[1],hi[2]},

                    {hi[0],hi[1],hi[2]},{lo[0],hi[1],hi[2]}

                }};

                VehicleSupportFace face{};

                for(unsigned i=0;i<4;++i)for(unsigned k=0;k<3;++k)

                    face.corners[i][k]=((local[i][0]*pose[k]+local[i][1]*pose[4+k])+

                        local[i][2]*pose[8+k])+pose[12+k];

                for(unsigned k=0;k<3;++k)face.plane[k]=pose[8+k];

                const auto& p=face.corners.front();

                face.plane[3]=(p[0]*face.plane[0]+p[1]*face.plane[1])+p[2]*face.plane[2];

                face.low={p[0],p[1]};face.high=face.low;

                for(const auto& corner:face.corners)for(unsigned k=0;k<2;++k) {

                    if(corner[k]<face.low[k])face.low[k]=corner[k];

                    if(corner[k]>face.high[k])face.high[k]=corner[k];

                }

                faces.push_back(face);

            }

        }

        chunk=end;

    }

    return true;

}

bool load_world_objects(const std::vector<unsigned char>& image,int direction,
                        std::vector<VehicleWorldObject>& objects,std::vector<VehicleWorldTrigger>& triggers) {
    const auto walk=[&](const auto& self,std::size_t begin,std::size_t finish)->bool {
    for(std::size_t chunk=begin;chunk+8<=finish;) {
        const auto kind=hull_u32(image.data()+chunk),size=hull_u32(image.data()+chunk+4);
        const auto start=chunk+8;if(size>finish-start)return false;
        const auto end=start+size;
        if(kind==0x80036000U&&!self(self,start,end))return false;
        if(kind==0x36003) {
            const auto aligned=(start+15)&~std::size_t(15);
            if(aligned>end||(end-aligned)%32)return false;
            for(auto at=aligned;at<end;at+=32)if(hull_u32(image.data()+at+4)==0x10006) {
                VehicleWorldTrigger trigger;trigger.player_mask=hull_u32(image.data()+at+12);
                std::memcpy(trigger.centre.data(),image.data()+at+16,12);
                std::memcpy(&trigger.radius,image.data()+at+28,4);
                for(float value:trigger.centre)if(!std::isfinite(value))return false;
                if(!std::isfinite(trigger.radius)||trigger.radius<=0)return false;
                triggers.push_back(trigger);
            }
        }
        if(kind==0x3B300) {
            const auto aligned=(start+15)&~std::size_t(15);
            if(aligned>end||(end-aligned)%160)return false;
            for(auto at=aligned;at<end;at+=160) {
                const auto* record=image.data()+at;
                if(hull_u32(record+12)||!hull_u32(record+8))continue;
                //578820 suppresses these constructors before registration.
                if(hull_u32(record)==track_marker_hash("DrawBridge")||
                   (direction==1&&hull_u32(record+4)==0x6691470DU))continue;
                if(objects.size()==kRaceWorldObjectCapacity)return false;
                VehicleWorldObject object;object.type=hull_u32(record);object.name=hull_u32(record+4);
                object.shape=hull_u32(record+8);object.model=hull_u32(record+80);object.source_offset=at;
                std::memcpy(object.transform.data(),record+16,64);
                for(float value:object.transform)if(!std::isfinite(value))return false;
                //116 creates the optional path helper, not a rigid body.
                object.path_cache=hull_u32(record+116)!=0;
                object.fixed_collision=object.type==0x3E83CBDEU;
                object.motion_collision=object.type==0x24CEBC53U||object.type==0x427A6453U||object.type==0x67DBE3EAU;
                if(object.motion_collision) {
                    std::memcpy(object.animation.data(),record+128,12);
                    std::memcpy(&object.animation_rate,record+148,4);
                    std::memcpy(&object.animation_hold,record+156,4);
                    object.animation_flags=hull_u32(record+152);
                    if(!std::isfinite(object.animation_rate)||!std::isfinite(object.animation_hold)||
                       !object.animation[0]||!object.animation[1]||object.animation[2])return false;
                }
                if(object.fixed_collision||object.motion_collision) {
                    if(hull_u32(record+144))return false; // Only the default ROOT skeleton is recovered here.
                    if(!decode_car_collision_hull(image,object.shape,object.hull)) {
                        //5922DA..592414: this absent retail shape uses the original
                        //default primitive. Other missing/malformed inputs refuse.
                        if(object.shape!=0x84E701E8U)return false;
                        object.default_shape=true;
                        const float lo[3]={-2.217F,-.971F,-.2F},hi[3]={2.165F,.971F,.624F};
                        for(unsigned vertex=0;vertex<8;++vertex) {
                            std::array<float,3> point{};
                            for(unsigned k=0;k<3;++k) {
                                const float centre=(lo[k]+hi[k])*.5F,half=(hi[k]-lo[k])*.5F;
                                point[k]=centre+(vertex&(1U<<k)?-half:half);
                            }
                            object.hull.points.push_back(point);
                        }
                    }
                }
                objects.push_back(std::move(object));
            }
        }
        chunk=end;
    }
    return true;
    };
    return walk(walk,0,image.size());
}
} // namespace



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

    if(!load_support_faces(image,direction,course.support_faces)) {

        error="invalid WorldObject wheel-support records in "+course.name;

        return false;

    }



    if(!load_world_objects(image,direction,course.world_objects,course.train_triggers)) {
        error="invalid WorldObject collision records in "+course.name;return false;
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
