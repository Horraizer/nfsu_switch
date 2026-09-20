#include "nfsu/track_visibility.h"

#include "nfsu/track_objects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;

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
    check(got == want, what + ": expected " + std::to_string(want) + ", got " +
                           std::to_string(got));
}

void check_near(float got, float want, const std::string& what) {
    ++g_checks;
    if (!(std::fabs(got - want) <= 1e-5F)) {
        std::cerr << "test failure: " << what << ": expected " << want
                  << ", got " << got << '\n';
        ++g_failures;
    }
}

std::string join(const std::vector<std::size_t>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += std::to_string(values[i]);
    }
    out += "]";
    return out;
}

// The visible instances, in the order the report holds them. Order is part
// of the claim: the retail traversal emits a node's own instances in entry
// order and then takes its children in reverse entry order, so a walker
// that visited children in entry order would produce the same set in a
// different sequence and this comparison would catch it.
std::vector<std::size_t> visible_indices(const nfsu::TrackCullReport& report) {
    std::vector<std::size_t> out;
    out.reserve(report.visible.size());
    for (const nfsu::TrackVisibleInstance& item : report.visible) {
        out.push_back(item.instance_index);
    }
    return out;
}

void check_visible(
    const nfsu::TrackCullReport& report,
    const std::vector<std::size_t>& want,
    const std::string& what) {
    const std::vector<std::size_t> got = visible_indices(report);
    check(got == want,
          what + ": expected visible " + join(want) + ", got " + join(got));
}

// ---------------------------------------------------------------------------
// Fixture: a 0x80034100 region written at an absolute offset, so every
// alignment rule of docs/TRACK_OBJECTS.md applies exactly as it would in a
// retail file.
// ---------------------------------------------------------------------------

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

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

class Writer {
public:
    explicit Writer(std::uint64_t base) : base_(base) {}

    std::uint64_t at() const { return base_ + bytes_.size(); }
    const Bytes& bytes() const { return bytes_; }
    std::uint64_t base() const { return base_; }

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

    void append(const Bytes& payload) {
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    void leaf(std::uint32_t id, const Bytes& payload) {
        begin(id);
        append(payload);
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

struct InstanceSpec {
    std::int16_t bounds_min[3] = {0, 0, 0};
    std::int16_t bounds_max[3] = {0, 0, 0};
    std::uint16_t object_index = 0;
    std::uint16_t flags = 0;
    float position[3] = {0.0F, 0.0F, 0.0F};
};

struct NodeSpec {
    std::int16_t bounds_min[3] = {0, 0, 0};
    std::int16_t bounds_max[3] = {0, 0, 0};
    std::uint16_t entry_count = 0;
    std::int16_t entries[5] = {0, 0, 0, 0, 0};
};

struct SectionSpec {
    std::uint32_t number = 303;
    float radius = 5.0F;
    std::vector<InstanceSpec> instances;
    std::vector<NodeSpec> nodes;
};

void write_section(Writer& writer, const SectionSpec& spec) {
    writer.begin(nfsu::kScenerySectionId);

    writer.begin(nfsu::kSceneryHeaderId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes record(nfsu::kSceneryHeaderRecordSize, 0U);
    put_u32(record, 12, spec.number);
    writer.append(record);
    writer.end();

    writer.begin(nfsu::kSceneryInstanceListId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes instances;
    for (const InstanceSpec& instance : spec.instances) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(instances, instance.bounds_min[axis]);
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(instances, instance.bounds_max[axis]);
        }
        append_u16(instances, instance.object_index);
        append_u16(instances, instance.flags);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_f32(instances, instance.position[axis]);
        }
        const std::int16_t identity[9] = {8192, 0, 0, 0, 8192, 0, 0, 0, 8192};
        for (std::size_t word = 0; word < 9; ++word) {
            append_i16(instances, identity[word]);
        }
        append_i16(instances, 0);
    }
    writer.append(instances);
    writer.end();

    Bytes objects;
    for (std::size_t slot = 0; slot < nfsu::kSceneryObjectMeshSlots; ++slot) {
        append_u32(objects, slot == 0 ? 0xAAAA0001U : 0U);
    }
    objects.resize(68, 0U);
    append_f32(objects, spec.radius);
    objects.resize(nfsu::kSceneryObjectRecordSize, 0U);
    writer.leaf(nfsu::kSceneryObjectListId, objects);

    Bytes tree;
    for (const NodeSpec& node : spec.nodes) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(tree, node.bounds_min[axis]);
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(tree, node.bounds_max[axis]);
        }
        append_u16(tree, node.entry_count);
        for (std::size_t slot = 0; slot < nfsu::kSceneryBoundNodeEntries; ++slot) {
            append_i16(tree, node.entries[slot]);
        }
    }
    writer.leaf(nfsu::kSceneryBoundTreeId, tree);

    writer.end();
}

// Eight instances in a row along world +X, at world x = 100, 200 .. 800,
// each a cube of half extent 5 centred on the world origin's y and z.
// docs/FORMATS.md:286 maps world to the raw slot order as px = x, pz = -z,
// py = y, so the raw boxes below are the world ones with y and z swapped
// and z negated; the cubes are symmetric about z, so the negation is not
// observable here and the layout stays easy to read.
//
// The tree groups them 0..3 under node 1 and 4..7 under node 2, and node 0
// names both children in that order.
SectionSpec row_section(std::uint32_t number = 303) {
    SectionSpec spec;
    spec.number = number;
    for (int i = 0; i < 8; ++i) {
        const int centre = 100 * (i + 1);
        InstanceSpec instance;
        instance.bounds_min[0] = static_cast<std::int16_t>(centre - 5);
        instance.bounds_min[1] = -5;
        instance.bounds_min[2] = -5;
        instance.bounds_max[0] = static_cast<std::int16_t>(centre + 5);
        instance.bounds_max[1] = 5;
        instance.bounds_max[2] = 5;
        instance.position[0] = static_cast<float>(centre);
        spec.instances.push_back(instance);
    }

    NodeSpec root;
    root.bounds_min[0] = 95;
    root.bounds_min[1] = -5;
    root.bounds_min[2] = -5;
    root.bounds_max[0] = 805;
    root.bounds_max[1] = 5;
    root.bounds_max[2] = 5;
    root.entry_count = 2;
    root.entries[0] = -1;
    root.entries[1] = -2;

    NodeSpec near_group;
    near_group.bounds_min[0] = 95;
    near_group.bounds_min[1] = -5;
    near_group.bounds_min[2] = -5;
    near_group.bounds_max[0] = 405;
    near_group.bounds_max[1] = 5;
    near_group.bounds_max[2] = 5;
    near_group.entry_count = 4;
    for (int i = 0; i < 4; ++i) {
        near_group.entries[i] = static_cast<std::int16_t>(i);
    }

    NodeSpec far_group;
    far_group.bounds_min[0] = 495;
    far_group.bounds_min[1] = -5;
    far_group.bounds_min[2] = -5;
    far_group.bounds_max[0] = 805;
    far_group.bounds_max[1] = 5;
    far_group.bounds_max[2] = 5;
    far_group.entry_count = 4;
    for (int i = 0; i < 4; ++i) {
        far_group.entries[i] = static_cast<std::int16_t>(4 + i);
    }

    spec.nodes.push_back(root);
    spec.nodes.push_back(near_group);
    spec.nodes.push_back(far_group);
    return spec;
}

// One root over `count` instances, each a cube of half extent 5 at world
// x = 100 * (i + 1). count must not exceed the five entry slots a node
// record holds.
SectionSpec small_section(std::uint32_t number, int count) {
    SectionSpec spec;
    spec.number = number;
    NodeSpec root;
    root.entry_count = static_cast<std::uint16_t>(count);
    for (int i = 0; i < count; ++i) {
        const int centre = 100 * (i + 1);
        InstanceSpec instance;
        instance.bounds_min[0] = static_cast<std::int16_t>(centre - 5);
        instance.bounds_min[1] = -5;
        instance.bounds_min[2] = -5;
        instance.bounds_max[0] = static_cast<std::int16_t>(centre + 5);
        instance.bounds_max[1] = 5;
        instance.bounds_max[2] = 5;
        instance.position[0] = static_cast<float>(centre);
        spec.instances.push_back(instance);
        root.entries[i] = static_cast<std::int16_t>(i);
    }
    root.bounds_min[0] = 95;
    root.bounds_min[1] = -5;
    root.bounds_min[2] = -5;
    root.bounds_max[0] = static_cast<std::int16_t>(100 * count + 5);
    root.bounds_max[1] = 5;
    root.bounds_max[2] = 5;
    spec.nodes.push_back(root);
    return spec;
}

// The same eight instances, but three levels deep instead of two:
// node 0 -> {node 1, node 2}, node 1 -> {node 3, instance 0},
// node 3 -> {1, 2, 3}, node 2 -> {4, 5, 6, 7}. A two-level tree cannot
// tell a fold that accumulates from the leaves up from one that does not,
// because the two orders agree there.
SectionSpec deep_section() {
    SectionSpec spec = row_section();
    spec.nodes.clear();

    NodeSpec root;
    root.bounds_min[0] = 95;
    root.bounds_min[1] = -5;
    root.bounds_min[2] = -5;
    root.bounds_max[0] = 805;
    root.bounds_max[1] = 5;
    root.bounds_max[2] = 5;
    root.entry_count = 2;
    root.entries[0] = -1;
    root.entries[1] = -2;

    NodeSpec near_group = root;
    near_group.bounds_max[0] = 405;
    near_group.entry_count = 2;
    near_group.entries[0] = -3;
    near_group.entries[1] = 0;

    NodeSpec far_group = root;
    far_group.bounds_min[0] = 495;
    far_group.bounds_max[0] = 805;
    far_group.entry_count = 4;
    for (int i = 0; i < 4; ++i) {
        far_group.entries[i] = static_cast<std::int16_t>(4 + i);
    }

    NodeSpec inner = root;
    inner.bounds_min[0] = 195;
    inner.bounds_max[0] = 405;
    inner.entry_count = 3;
    for (int i = 0; i < 3; ++i) {
        inner.entries[i] = static_cast<std::int16_t>(1 + i);
    }

    spec.nodes.push_back(root);
    spec.nodes.push_back(near_group);
    spec.nodes.push_back(far_group);
    spec.nodes.push_back(inner);
    return spec;
}

constexpr std::uint64_t kBase = 23011344;

nfsu::TrackObjectReport parse_sections(const std::vector<SectionSpec>& specs) {
    Writer writer(kBase);
    for (const SectionSpec& spec : specs) {
        write_section(writer, spec);
    }
    return nfsu::parse_track_objects(writer.bytes().data(), writer.bytes().size(),
                                     writer.base());
}

nfsu::TrackObjectReport parse_row_region() {
    return parse_sections(std::vector<SectionSpec>{row_section()});
}

// A camera on the world origin looking along +X. Only the far plane moves
// between the cases below, so every visible set is a statement about one
// distance and nothing else.
nfsu::TrackCamera axis_camera(float far_plane) {
    nfsu::TrackCamera camera;
    camera.eye[0] = 0.0F;
    camera.eye[1] = 0.0F;
    camera.eye[2] = 0.0F;
    camera.target[0] = 1.0F;
    camera.target[1] = 0.0F;
    camera.target[2] = 0.0F;
    camera.up[0] = 0.0F;
    camera.up[1] = 1.0F;
    camera.up[2] = 0.0F;
    camera.vertical_fov = 1.0471975512F;
    camera.aspect = 4.0F / 3.0F;
    camera.near_plane = 1.0F;
    camera.far_plane = far_plane;
    return camera;
}

nfsu::TrackFrustum frustum_or_fail(const nfsu::TrackCamera& camera,
                                   const std::string& what) {
    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(camera, frustum),
          what + ": the camera must produce a frustum");
    return frustum;
}

// ---------------------------------------------------------------------------

void test_coordinate_map() {
    // docs/FORMATS.md:286. Stated on an asymmetric point so a swapped pair
    // or a dropped negation cannot pass.
    const float raw[3] = {1.0F, 2.0F, 3.0F};
    float world[3] = {0.0F, 0.0F, 0.0F};
    nfsu::track_world_from_raw(raw, world);
    check(world[0] == 1.0F && world[1] == 3.0F && world[2] == -2.0F,
          "world_from_raw must map (px, pz, py) to (px, py, -pz)");

    float back[3] = {0.0F, 0.0F, 0.0F};
    nfsu::track_raw_from_world(world, back);
    check(back[0] == raw[0] && back[1] == raw[1] && back[2] == raw[2],
          "raw_from_world must invert world_from_raw exactly");

    // Aliasing: the same buffer in and out must still be correct, because a
    // naive implementation that writes out[0] before reading in[2] would
    // pass the two checks above and fail this one.
    float same[3] = {1.0F, 2.0F, 3.0F};
    nfsu::track_world_from_raw(same, same);
    check(same[0] == 1.0F && same[1] == 3.0F && same[2] == -2.0F,
          "world_from_raw must be safe when input and output alias");
}

void test_box_test_classes() {
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "box classes");

    // Raw slot order throughout: px, pz, py.
    const float inside_min[3] = {95.0F, -5.0F, -5.0F};
    const float inside_max[3] = {105.0F, 5.0F, 5.0F};
    check(nfsu::track_classify_box(frustum, inside_min, inside_max) ==
              nfsu::TrackBoxClass::inside,
          "a box well inside the six planes classifies as inside");

    // Straddling the near plane at px == 1.
    const float straddle_min[3] = {-10.0F, -5.0F, -5.0F};
    const float straddle_max[3] = {10.0F, 5.0F, 5.0F};
    check(nfsu::track_classify_box(frustum, straddle_min, straddle_max) ==
              nfsu::TrackBoxClass::intersecting,
          "a box crossing the near plane classifies as intersecting");

    // Entirely behind the eye.
    const float behind_min[3] = {-200.0F, -5.0F, -5.0F};
    const float behind_max[3] = {-100.0F, 5.0F, 5.0F};
    check(nfsu::track_classify_box(frustum, behind_min, behind_max) ==
              nfsu::TrackBoxClass::outside,
          "a box behind the near plane classifies as outside");

    // Beyond the far plane at px == 1000.
    const float beyond_min[3] = {1100.0F, -5.0F, -5.0F};
    const float beyond_max[3] = {1200.0F, 5.0F, 5.0F};
    check(nfsu::track_classify_box(frustum, beyond_min, beyond_max) ==
              nfsu::TrackBoxClass::outside,
          "a box beyond the far plane classifies as outside");

    // Far off each side in turn, inside the depth range. All four are
    // needed: with any one of the four side planes duplicated onto another,
    // exactly one of these stops being rejected, so a single off-axis box
    // would leave three of the six planes unpinned. Raw slot 1 is pz and
    // raw slot 2 is py.
    const float side_offsets[4][2] = {{400.0F, 0.0F}, {-420.0F, 0.0F},
                                      {0.0F, 400.0F}, {0.0F, -420.0F}};
    const char* side_names[4] = {"+pz", "-pz", "+py", "-py"};
    for (std::size_t i = 0; i < 4; ++i) {
        const float aside_min[3] = {95.0F, side_offsets[i][0],
                                    side_offsets[i][1]};
        const float aside_max[3] = {105.0F, side_offsets[i][0] + 20.0F,
                                    side_offsets[i][1] + 20.0F};
        check(nfsu::track_classify_box(frustum, aside_min, aside_max) ==
                  nfsu::TrackBoxClass::outside,
              std::string("a box past the side planes on ") + side_names[i] +
                  " classifies as outside");
    }

    // The near plane on its own. A box nearer than the near plane but well
    // inside the cone and well short of the far plane is rejected by nothing
    // else, so this is the only case that separates plane 0 from plane 1.
    nfsu::TrackCamera far_near = axis_camera(1000.0F);
    far_near.near_plane = 200.0F;
    const nfsu::TrackFrustum deep =
        frustum_or_fail(far_near, "near plane alone");
    const float too_close_min[3] = {50.0F, -5.0F, -5.0F};
    const float too_close_max[3] = {60.0F, 5.0F, 5.0F};
    check(nfsu::track_classify_box(deep, too_close_min, too_close_max) ==
              nfsu::TrackBoxClass::outside,
          "a box nearer than the near plane classifies as outside");
    const float past_near_min[3] = {300.0F, -5.0F, -5.0F};
    const float past_near_max[3] = {310.0F, 5.0F, 5.0F};
    check(nfsu::track_classify_box(deep, past_near_min, past_near_max) ==
              nfsu::TrackBoxClass::inside,
          "a box past the near plane and short of the far one is inside");
}

// The six planes of the reference camera, written out. Every earlier test
// observes the frustum only through a classification, which cannot tell a
// permuted normal from a correct one, cannot tell tan(fov) from
// tan(fov / 2), and cannot tell the horizontal half-angle from the
// vertical one while every box sits outside both. This pins all of it,
// including which plane is which: a handedness flip swaps left with right
// and bottom with top, leaving the six-plane set identical and every
// classification unchanged, so only a per-index comparison catches it.
//
// eye at the world origin, looking along +X, up +Y, 60 degrees vertical,
// aspect 4:3, near 1, far 1000. half_v = tan(30 deg) = 0.57735 and
// half_h = half_v * 4 / 3 = 0.76980. Normals are in the raw slot order
// (px, pz, py) of docs/FORMATS.md:286, so the world normal (nx, ny, nz)
// appears here as (nx, -nz, ny).
void test_frustum_planes() {
    const nfsu::TrackFrustum f =
        frustum_or_fail(axis_camera(1000.0F), "plane values");

    const float want[6][4] = {
        // near: world +X through (1, 0, 0).
        {1.0F, 0.0F, 0.0F, -1.0F},
        // far: world -X through (1000, 0, 0).
        {-1.0F, 0.0F, 0.0F, 1000.0F},
        // left and right, through the eye. 0.6099943 = h / sqrt(1 + h*h)
        // and 0.7924058 = 1 / sqrt(1 + h*h) with h = 0.7698004, so the pair
        // moves if the aspect ratio does.
        {0.6099943F, -0.7924058F, 0.0F, 0.0F},
        {0.6099943F, 0.7924058F, 0.0F, 0.0F},
        // bottom and top. 0.5 and 0.8660254 come from half_v = 0.57735,
        // so the pair moves if the field of view stops being halved.
        {0.5F, 0.0F, 0.8660254F, 0.0F},
        {0.5F, 0.0F, -0.8660254F, 0.0F},
    };
    const char* names[6] = {"near", "far", "left", "right", "bottom", "top"};
    for (std::size_t i = 0; i < nfsu::kTrackFrustumPlanes; ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            check_near(f.planes[i].normal[axis], want[i][axis],
                       std::string("the ") + names[i] + " plane's normal slot " +
                           std::to_string(axis));
        }
        check_near(f.planes[i].d, want[i][3],
                   std::string("the ") + names[i] + " plane's constant");
    }

    // Every normal is unit length, which the classification relies on only
    // through the radius term but which a dropped normalize3 would break.
    for (std::size_t i = 0; i < nfsu::kTrackFrustumPlanes; ++i) {
        const float* n = f.planes[i].normal;
        check_near(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 1.0F,
                   std::string("the ") + names[i] + " plane's normal is unit");
    }
}

// The band between the two half-angles. At this camera the horizontal
// half-angle is 0.76980 and the vertical is 0.57735, so at px 100 a box
// 65 units off axis is inside the horizontal cone and outside the vertical
// one. A box at 400, which is what every other test uses, is outside both,
// which is why those cannot tell the two apart.
void test_half_angles_differ() {
    const nfsu::TrackFrustum f =
        frustum_or_fail(axis_camera(1000.0F), "half angles");

    const float pz_min[3] = {95.0F, 65.0F, -5.0F};
    const float pz_max[3] = {105.0F, 85.0F, 5.0F};
    check(nfsu::track_classify_box(f, pz_min, pz_max) ==
              nfsu::TrackBoxClass::intersecting,
          "65 units off axis on pz is inside the horizontal half-angle");

    const float py_min[3] = {95.0F, -5.0F, 65.0F};
    const float py_max[3] = {105.0F, 5.0F, 85.0F};
    check(nfsu::track_classify_box(f, py_min, py_max) ==
              nfsu::TrackBoxClass::outside,
          "65 units off axis on py is outside the vertical half-angle");

    // A box straddling the top plane. The top plane is the only one whose
    // raw normal is negative on slot 2, so it is the only place the
    // absolute value in the radius term changes an answer: without it the
    // term subtracts instead of adding, the box looks smaller than it is
    // and a crossing box is reported outside.
    const float straddle_min[3] = {95.0F, -5.0F, 50.0F};
    const float straddle_max[3] = {105.0F, 5.0F, 70.0F};
    check(nfsu::track_classify_box(f, straddle_min, straddle_max) ==
              nfsu::TrackBoxClass::intersecting,
          "a box straddling the top plane is crossing, which needs the "
          "absolute value on the slot-2 radius term");
}

// Both comparisons in track_classify_box are strict, and the library
// disables floating-point contraction on the strength of that. These two
// boxes sit exactly on the two boundaries, so relaxing either < to <=
// changes an answer.
void test_classify_boundaries() {
    const nfsu::TrackFrustum f =
        frustum_or_fail(axis_camera(450.0F), "boundaries");

    // Far plane at px 450. Centre 455, extent 5: distance + radius is
    // exactly 0, so the box is not outside, only crossing.
    const float touch_min[3] = {450.0F, -5.0F, -5.0F};
    const float touch_max[3] = {460.0F, 5.0F, 5.0F};
    check(nfsu::track_classify_box(f, touch_min, touch_max) ==
              nfsu::TrackBoxClass::intersecting,
          "a box whose near corner rests on the far plane is crossing, not "
          "outside");

    // Centre 445, extent 5: distance - radius is exactly 0, so the box is
    // inside, not crossing.
    const float flush_min[3] = {440.0F, -5.0F, -5.0F};
    const float flush_max[3] = {450.0F, 5.0F, 5.0F};
    check(nfsu::track_classify_box(f, flush_min, flush_max) ==
              nfsu::TrackBoxClass::inside,
          "a box whose far corner rests on the far plane is inside, not "
          "crossing");
}

void test_fixture_parses() {
    const nfsu::TrackObjectReport report = parse_row_region();
    check(report.ok(), "the fixture region must parse with no issue");
    check(report.valid_section_count() == 1,
          "the fixture region must hold one valid section");
    if (report.sections.empty()) {
        return;
    }
    const nfsu::TrackSection& section = report.sections[0];
    check_eq_u64(section.instances.size(), 8, "fixture instances");
    check_eq_u64(section.nodes.size(), 3, "fixture nodes");
    check_eq_u64(nfsu::track_check_tree_containment(section, nullptr), 0,
                 "fixture containment overshoots");
}

// The whole row inside the frustum. The root box is inside all six planes,
// so 0x00446181's shortcut fires: no other node and no instance is tested
// at all. That is the retail rule's containment assumption, and the counts
// below are what make it visible.
void test_camera_sees_everything() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "whole row");

    const nfsu::TrackCullReport report = cull_track_region(objects, frustum);
    check(report.ok(), "the whole-row cull must raise no issue");
    check(report.policy_held, "the whole-row cull must not degrade");
    check_visible(report, {4, 5, 6, 7, 0, 1, 2, 3}, "whole row");
    check_eq_u64(report.stats.nodes_visited, 3, "whole row nodes visited");
    check_eq_u64(report.stats.node_tests, 1, "whole row node tests");
    check_eq_u64(report.stats.nodes_culled, 0, "whole row nodes culled");
    check_eq_u64(report.stats.instance_tests, 0, "whole row instance tests");
    check_eq_u64(report.stats.instances_pruned, 0, "whole row instances pruned");
    check_eq_u64(report.stats.instances_visible, 8, "whole row visible");
    for (const nfsu::TrackVisibleInstance& item : report.visible) {
        check(item.node_class == nfsu::TrackBoxClass::inside,
              "an instance under an inside node carries the inside code");
        check(item.instance_class == nfsu::TrackBoxClass::inside,
              "an instance under an inside node is not box-tested");
    }
}

// The far plane at 450 puts node 2 wholly beyond it and node 1 wholly
// inside it. The subtree of node 2 is never entered.
void test_camera_prunes_far_group() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(450.0F), "near half");

    const nfsu::TrackCullReport report = cull_track_region(objects, frustum);
    check(report.ok(), "the near-half cull must raise no issue");
    check_visible(report, {0, 1, 2, 3}, "near half");
    check_eq_u64(report.stats.nodes_visited, 3, "near half nodes visited");
    check_eq_u64(report.stats.node_tests, 3, "near half node tests");
    check_eq_u64(report.stats.nodes_culled, 1, "near half nodes culled");
    check_eq_u64(report.stats.instances_pruned, 4, "near half instances pruned");
    check_eq_u64(report.stats.instances_visited, 4, "near half instances visited");
    check_eq_u64(report.stats.instance_tests, 0, "near half instance tests");
    check_eq_u64(report.stats.instances_visible, 4, "near half visible");
}

// The same tree from the other end of the row. The visible set is the
// complement of the previous case, and it comes out under node 2, which is
// the second entry of the root: an implementation that walked children in
// entry order would emit the same four indices, so this case pins the
// pruning rather than the order.
void test_camera_prunes_near_group() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    nfsu::TrackCamera camera = axis_camera(450.0F);
    camera.eye[0] = 900.0F;
    camera.target[0] = 0.0F;
    const nfsu::TrackFrustum frustum = frustum_or_fail(camera, "far half");

    const nfsu::TrackCullReport report = cull_track_region(objects, frustum);
    check(report.ok(), "the far-half cull must raise no issue");
    check_visible(report, {4, 5, 6, 7}, "far half");
    check_eq_u64(report.stats.nodes_culled, 1, "far half nodes culled");
    check_eq_u64(report.stats.instances_pruned, 4, "far half instances pruned");
}

// A far plane at 150 leaves node 1 crossing it, so every instance under it
// is box-tested one at a time and only the first survives.
void test_camera_tests_instances() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(150.0F), "first only");

    const nfsu::TrackCullReport report = cull_track_region(objects, frustum);
    check(report.ok(), "the first-only cull must raise no issue");
    check_visible(report, {0}, "first only");
    check_eq_u64(report.stats.node_tests, 3, "first only node tests");
    check_eq_u64(report.stats.nodes_culled, 1, "first only nodes culled");
    check_eq_u64(report.stats.instances_pruned, 4, "first only instances pruned");
    check_eq_u64(report.stats.instance_tests, 4, "first only instance tests");
    check_eq_u64(report.stats.instances_culled_by_box, 3,
                 "first only instances culled by box");
    check(report.visible.size() == 1 &&
              report.visible[0].node_class == nfsu::TrackBoxClass::intersecting &&
              report.visible[0].instance_class == nfsu::TrackBoxClass::inside &&
              report.visible[0].node_index == 1,
          "the surviving instance is the one node 1 crossed the far plane with");
}

// A far plane at 100 leaves node 1 crossing it and leaves instance 0
// crossing it too, which is the only case that distinguishes the class the
// instance box tested as from the class it inherited from its node.
void test_instance_box_can_cross() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(100.0F), "crossing instance");

    const nfsu::TrackCullReport report = cull_track_region(objects, frustum);
    check(report.ok(), "the crossing-instance cull must raise no issue");
    check_visible(report, {0}, "crossing instance");
    check(report.visible.size() == 1 &&
              report.visible[0].node_class ==
                  nfsu::TrackBoxClass::intersecting &&
              report.visible[0].instance_class ==
                  nfsu::TrackBoxClass::intersecting,
          "an instance straddling the far plane reports itself as crossing");
    check(!report.sections.empty() && report.sections[0].visible == 1 &&
              report.sections[0].instances == 8,
          "the per-section summary carries its own instance and visible "
          "counts");
}

// Every policy must agree on the invariant that the traversal reaches every
// resident instance exactly once, whether it draws it or not.
void test_visit_accounting() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const float far_planes[4] = {80.0F, 150.0F, 450.0F, 1000.0F};
    for (float far_plane : far_planes) {
        const nfsu::TrackFrustum frustum =
            frustum_or_fail(axis_camera(far_plane), "accounting");
        const nfsu::TrackCullReport report = cull_track_region(objects, frustum);
        check_eq_u64(report.stats.instances_visited +
                         report.stats.instances_pruned,
                     8,
                     "visited plus pruned at far " +
                         std::to_string(static_cast<int>(far_plane)));
    }
}

// The fallback. The frustum is a real one that sees nothing, and the policy
// still draws the whole section: that is what makes it safe when retail
// visibility is unknown, and it is the reason it is a separate policy
// rather than a wide frustum.
void test_draw_everything_ignores_the_frustum() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    nfsu::TrackCamera camera = axis_camera(1000.0F);
    camera.target[0] = -1.0F;
    const nfsu::TrackFrustum frustum = frustum_or_fail(camera, "fallback");

    nfsu::TrackCullOptions retail;
    const nfsu::TrackCullReport away = cull_track_region(objects, frustum, retail);
    check_visible(away, {}, "a camera pointing away sees nothing under the tree");

    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::draw_everything;
    const nfsu::TrackCullReport report =
        cull_track_region(objects, frustum, options);
    check(report.ok(), "the fallback must raise no issue");
    check_visible(report, {0, 1, 2, 3, 4, 5, 6, 7},
                  "the fallback draws the whole section in instance order");
    for (const nfsu::TrackVisibleInstance& item : report.visible) {
        check(item.node_index == 0,
              "a policy that walks no tree reports no node");
    }
    check_eq_u64(report.stats.node_tests, 0, "the fallback tests no node");
    check_eq_u64(report.stats.instance_tests, 0, "the fallback tests no box");
    check(!report.sections.empty() &&
              report.sections[0].policy_used ==
                  nfsu::TrackCullPolicy::draw_everything,
          "the report names the policy the section was culled with");
    check(std::string(nfsu::track_cull_policy_name(
              nfsu::TrackCullPolicy::draw_everything)) == "draw-everything",
          "the fallback policy has a stable name");
    check(report.requested_policy == nfsu::TrackCullPolicy::draw_everything,
          "the report repeats the policy it was asked for");
    check(away.requested_policy == nfsu::TrackCullPolicy::retail_tree,
          "and a default run repeats the default");
    check_eq_u64(report.stats.sections_considered, 1,
                 "the fallback still counts the section it considered");
}

// The fallback's contract is unconditional, so it has to hold with the
// gates armed. Both of these gates take a retail input this project did not
// recover, which is exactly why a fallback that still applied them could
// drop an instance for a value it invented.
void test_draw_everything_ignores_the_gates() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "fallback gates");

    // 0x60 masks out every instance of this fixture under the retail rule,
    // and does so under the two culling policies.
    nfsu::TrackCullOptions masked;
    masked.instance_mask = 0x60U;
    const nfsu::TrackCullReport tree =
        cull_track_region(objects, frustum, masked);
    check_visible(tree, {}, "the mask empties the tree policy");

    masked.policy = nfsu::TrackCullPolicy::draw_everything;
    const nfsu::TrackCullReport report =
        cull_track_region(objects, frustum, masked);
    check_visible(report, {0, 1, 2, 3, 4, 5, 6, 7},
                  "the fallback draws everything with the mask armed");
    check_eq_u64(report.stats.instances_culled_by_mask, 0,
                 "the fallback culls nothing by mask");

    // The screen-size gate drops half the row under the tree policy.
    nfsu::TrackCullOptions sized;
    sized.use_screen_size = true;
    sized.screen_scale = 100.0F;
    sized.screen_forward[0] = 1.0F;
    sized.screen_forward[1] = 0.0F;
    sized.screen_forward[2] = 0.0F;
    const nfsu::TrackCullReport sized_tree =
        cull_track_region(objects, frustum, sized);
    check_eq_u64(sized_tree.stats.instances_culled_by_size, 4,
                 "the size gate drops four under the tree policy");

    sized.policy = nfsu::TrackCullPolicy::draw_everything;
    const nfsu::TrackCullReport sized_report =
        cull_track_region(objects, frustum, sized);
    check_visible(sized_report, {0, 1, 2, 3, 4, 5, 6, 7},
                  "the fallback draws everything with the size gate armed");
    check_eq_u64(sized_report.stats.instances_culled_by_size, 0,
                 "the fallback culls nothing by size");
    for (const nfsu::TrackVisibleInstance& item : sized_report.visible) {
        check(item.screen_level == 0,
              "the fallback reports no level, because it computed none");
    }
}

// The flat policy tests every instance and prunes nothing, so it finds the
// same set as the tree whenever the tree is sound. Run over the same four
// cameras the tree was run over.
void test_flat_policy_agrees_with_the_tree() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const float far_planes[4] = {80.0F, 150.0F, 450.0F, 1000.0F};
    for (float far_plane : far_planes) {
        const nfsu::TrackFrustum frustum =
            frustum_or_fail(axis_camera(far_plane), "flat agreement");
        nfsu::TrackCullOptions flat;
        flat.policy = nfsu::TrackCullPolicy::flat_instance_test;
        const nfsu::TrackCullReport a = cull_track_region(objects, frustum);
        const nfsu::TrackCullReport b =
            cull_track_region(objects, frustum, flat);
        for (const nfsu::TrackVisibleInstance& item : b.visible) {
            check(item.node_index == 0,
                  "the flat policy walks no tree and reports no node");
        }
        std::vector<std::size_t> tree_set = visible_indices(a);
        std::vector<std::size_t> flat_set = visible_indices(b);
        std::sort(tree_set.begin(), tree_set.end());
        check(tree_set == flat_set,
              "tree and flat culling agree at far " +
                  std::to_string(static_cast<int>(far_plane)) + ": " +
                  join(tree_set) + " against " + join(flat_set));
        check_eq_u64(b.stats.instance_tests, 8,
                     "the flat policy tests every instance at far " +
                         std::to_string(static_cast<int>(far_plane)));
    }
}

// The hazard the whole design turns on. A node box that does not contain an
// entry below it makes the prune unsound: the same camera and the same
// instances lose a visible instance when the tree is trusted, and keep it
// when containment is checked first.
//
// parse_track_objects refuses a section like this, so the only way a caller
// can reach it is by building the section itself, which is what this test
// does to the parsed report.
void test_containment_failure_changes_the_answer() {
    nfsu::TrackObjectReport objects = parse_row_region();
    check(objects.valid_section_count() == 1, "the fixture must parse first");
    if (objects.sections.empty()) {
        return;
    }
    // Shrink node 1 so that it no longer covers instance 0, which it still
    // names. Nothing else moves.
    objects.sections[0].nodes[1].bounds_min[0] = 195;

    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(150.0F), "containment");

    nfsu::TrackCullOptions trusting;
    trusting.verify_containment = false;
    const nfsu::TrackCullReport trusted =
        cull_track_region(objects, frustum, trusting);
    check_visible(trusted, {},
                  "trusting a tree that does not contain its entries loses the "
                  "one visible instance");
    check(trusted.ok(),
          "a trusting run reports no issue, which is exactly why the check is "
          "not optional in practice");

    const nfsu::TrackCullReport checked = cull_track_region(objects, frustum);
    check_visible(checked, {0},
                  "checking containment first recovers the visible instance");
    check(!checked.ok(), "the containment failure must be reported");
    check(!checked.policy_held, "the report must say the policy did not hold");
    check_eq_u64(checked.stats.sections_degraded, 1, "degraded sections");
    check(!checked.sections.empty() &&
              !checked.sections[0].containment_holds &&
              checked.sections[0].containment_checked &&
              checked.sections[0].policy_used ==
                  nfsu::TrackCullPolicy::flat_instance_test,
          "the degraded section names the policy it fell back to");

    bool named = false;
    for (const nfsu::TrackIssue& issue : checked.issues) {
        if (issue.message.find("reaches 100 units below the node box on px") !=
            std::string::npos) {
            named = true;
        }
    }
    check(named, "the issue names the axis and the overshoot in units");

    // The other side of the same check. Shrinking the node's maximum makes
    // an entry reach above it instead of below it, and the two sides are
    // separate comparisons: a culler that checked only one would pass every
    // assertion above.
    nfsu::TrackObjectReport above = parse_row_region();
    above.sections[0].nodes[1].bounds_max[0] = 305;
    const nfsu::TrackCullReport above_report =
        cull_track_region(above, frustum);
    check(!above_report.ok(), "an entry above the node box must be reported");
    check_eq_u64(above_report.stats.sections_degraded, 1,
                 "an entry above the node box degrades the section");
    bool named_above = false;
    for (const nfsu::TrackIssue& issue : above_report.issues) {
        if (issue.message.find("reaches 100 units above the node box on px") !=
            std::string::npos) {
            named_above = true;
        }
    }
    check(named_above,
          "the issue names the overshoot on the maximum side too");

    // Every axis, not just px. The check makes six comparisons per entry
    // and only two of them were ever violated above, so a loop that ran
    // over one axis, or an axis-name table in the wrong order, would pass
    // everything so far.
    const char* axis_name[3] = {"px", "pz", "py"};
    for (std::size_t axis = 1; axis < 3; ++axis) {
        nfsu::TrackObjectReport low = parse_row_region();
        low.sections[0].nodes[1].bounds_min[axis] = -4;
        const nfsu::TrackCullReport low_report =
            cull_track_region(low, frustum);
        check(!low_report.ok(),
              std::string("an overshoot below on ") + axis_name[axis] +
                  " must be reported");
        bool named_low = false;
        for (const nfsu::TrackIssue& issue : low_report.issues) {
            if (issue.message.find(std::string("reaches 1 units below the "
                                               "node box on ") +
                                   axis_name[axis]) != std::string::npos) {
                named_low = true;
            }
        }
        check(named_low,
              std::string("the issue names one unit below on ") +
                  axis_name[axis]);

        nfsu::TrackObjectReport high = parse_row_region();
        high.sections[0].nodes[1].bounds_max[axis] = 4;
        const nfsu::TrackCullReport high_report =
            cull_track_region(high, frustum);
        check(!high_report.ok(),
              std::string("an overshoot above on ") + axis_name[axis] +
                  " must be reported");
        bool named_high = false;
        for (const nfsu::TrackIssue& issue : high_report.issues) {
            if (issue.message.find(std::string("reaches 1 units above the "
                                               "node box on ") +
                                   axis_name[axis]) != std::string::npos) {
                named_high = true;
            }
        }
        check(named_high,
              std::string("the issue names one unit above on ") +
                  axis_name[axis]);
    }

    // One unit, not a hundred. 38468 of the 220260 retail axis comparisons
    // sit at exactly one unit of slack, so a check that tolerated a single
    // unit would still pass every other assertion in this suite while
    // being wrong on the densest part of the corpus.
    nfsu::TrackObjectReport unit = parse_row_region();
    unit.sections[0].nodes[1].bounds_min[0] = 96;
    const nfsu::TrackCullReport unit_report = cull_track_region(unit, frustum);
    check(!unit_report.ok(), "a one-unit overshoot must be reported");
    check_eq_u64(unit_report.stats.sections_degraded, 1,
                 "a one-unit overshoot degrades the section");
    bool named_unit = false;
    for (const nfsu::TrackIssue& issue : unit_report.issues) {
        if (issue.message.find("reaches 1 units below the node box on px") !=
            std::string::npos) {
            named_unit = true;
        }
    }
    check(named_unit, "the issue names the single unit");

    // A child node escaping its parent, rather than an instance. Both
    // containment tests above resolve to an instance entry, so the branch
    // that resolves a negative entry was never exercised.
    nfsu::TrackObjectReport child = parse_row_region();
    child.sections[0].nodes[0].bounds_max[0] = 804;
    const nfsu::TrackCullReport child_report =
        cull_track_region(child, frustum);
    check(!child_report.ok(),
          "a child node escaping its parent must be reported");
    check_eq_u64(child_report.stats.sections_degraded, 1,
                 "a child node escaping its parent degrades the section");
    bool named_child = false;
    for (const nfsu::TrackIssue& issue : child_report.issues) {
        if (issue.message.find("node 2 reaches 1 units above the node box on "
                               "px") != std::string::npos) {
            named_child = true;
        }
    }
    check(named_child, "the issue names the child node");

    // The unmodified tree must not trip the check, or the check would be
    // reporting on the fixture rather than on the mutation.
    const nfsu::TrackObjectReport sound = parse_row_region();
    const nfsu::TrackCullReport sound_report =
        cull_track_region(sound, frustum);
    check(sound_report.ok() && sound_report.policy_held,
          "the unmodified fixture must not report a containment failure");
}

// track_check_tree_containment is a public entry point of its own, and its
// entry-count guard is what keeps it inside the five slots the record
// holds. Reached through cull_track_region the guard is invisible, because
// prepare_tree refuses the same node a moment later with the same words;
// called directly, only the guard can produce this message.
void test_containment_entry_count_guard() {
    nfsu::TrackObjectReport objects = parse_row_region();
    check(objects.valid_section_count() == 1, "the fixture must parse first");
    if (objects.sections.empty()) {
        return;
    }
    objects.sections[0].nodes[1].entry_count = 9;

    std::vector<nfsu::TrackIssue> issues;
    const std::size_t failures =
        nfsu::track_check_tree_containment(objects.sections[0], &issues);
    check_eq_u64(failures, 1,
                 "an entry count outside 1 to 5 is one containment failure");
    check(issues.size() == 1 &&
              issues[0].message.find("entry count 9 is outside 1 to 5") !=
                  std::string::npos,
          "and the check names it rather than clamping and walking on");

    // The unmodified section raises nothing, so the assertion above is
    // about the count and not about the fixture.
    const nfsu::TrackObjectReport sound = parse_row_region();
    std::vector<nfsu::TrackIssue> none;
    check_eq_u64(nfsu::track_check_tree_containment(sound.sections[0], &none),
                 0, "the unmodified fixture is contained");
    check(none.empty(), "and raises no issue");
}

// A tree that is not a tree cannot be walked, and must fall back rather
// than loop or read out of range.
void test_broken_trees_degrade() {
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "broken trees");

    const auto names = [](const nfsu::TrackCullReport& report,
                          const std::string& text) {
        for (const nfsu::TrackIssue& issue : report.issues) {
            if (issue.message.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    {
        // Two parents naming the same child. Containment is switched off so
        // that the tree walk is what catches this and not the box check,
        // which would also fire because node 2 sits outside node 1.
        nfsu::TrackObjectReport objects = parse_row_region();
        objects.sections[0].nodes[1].entries[0] = -2;
        nfsu::TrackCullOptions options;
        options.verify_containment = false;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum, options);
        check(!report.ok(), "a shared subtree must be reported");
        check(names(report, "is reached more than once"),
              "the issue says the tree is not a tree");
        check_eq_u64(report.stats.sections_degraded, 1,
                     "a shared subtree degrades the section");
        check_visible(report, {0, 1, 2, 3, 4, 5, 6, 7},
                      "the degraded section still offers every instance");
    }
    {
        // A child index past the end of the tree. track_check_tree_containment
        // skips an entry it cannot resolve, so this reaches the tree walk
        // with containment still on.
        nfsu::TrackObjectReport objects = parse_row_region();
        objects.sections[0].nodes[0].entries[0] = -9;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum);
        check(!report.ok(), "an out-of-range child must be reported");
        check(names(report, "is outside the tree of 3 nodes"),
              "the issue names the tree size");
        check_eq_u64(report.stats.sections_degraded, 1,
                     "an out-of-range child degrades the section");
    }
    {
        // An instance index past the end of the array.
        nfsu::TrackObjectReport objects = parse_row_region();
        objects.sections[0].nodes[1].entries[0] = 99;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum);
        check(!report.ok(), "an out-of-range instance must be reported");
        check(names(report, "is outside the array of 8 instances"),
              "the issue names the instance array size");
        check_eq_u64(report.stats.sections_degraded, 1,
                     "an out-of-range instance degrades the section");
    }
    {
        // One instance named twice and another named never. The entry
        // count still equals the instance count, so a walk that counted
        // entries instead of marking instances would see nothing wrong and
        // would silently drop the unnamed one.
        nfsu::TrackObjectReport objects = parse_row_region();
        objects.sections[0].nodes[1].entries[3] = 0;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum);
        check(!report.ok(), "a duplicated instance entry must be reported");
        check(names(report, "instance 0 is reached more than once"),
              "the issue names the instance, not just a count");
        check_eq_u64(report.stats.sections_degraded, 1,
                     "a duplicated instance entry degrades the section");
        check_visible(report, {0, 1, 2, 3, 4, 5, 6, 7},
                      "the degraded section offers the instance the tree "
                      "dropped");
    }
    {
        // An instance no node names would never be drawn under the tree.
        nfsu::TrackObjectReport objects = parse_row_region();
        objects.sections[0].nodes[1].entry_count = 3;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum);
        check(!report.ok(), "an unreachable instance must be reported");
        check_eq_u64(report.stats.sections_degraded, 1,
                     "an unreachable instance degrades the section");
        check_visible(report, {0, 1, 2, 3, 4, 5, 6, 7},
                      "the degraded section reaches the unreachable instance");
    }
    {
        // An entry count past the five slots the record holds. Reading
        // entries[5] would be out of bounds, so the guard is what keeps the
        // walk inside the record.
        nfsu::TrackObjectReport objects = parse_row_region();
        objects.sections[0].nodes[1].entry_count = 9;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum);
        check(!report.ok(), "an entry count past five must be reported");
        check(names(report, "entry count 9 is outside 1 to 5"),
              "the issue names the count and the bound");
        check_eq_u64(report.stats.sections_degraded, 1,
                     "an entry count past five degrades the section");
        check_visible(report, {0, 1, 2, 3, 4, 5, 6, 7},
                      "the degraded section still offers every instance");
    }
    {
        // The same entry count, reached through prepare_tree rather than
        // through the containment check. With containment off, prepare_tree's
        // own guard is the only thing standing between the walk and
        // entries[5..8], which the record does not have.
        nfsu::TrackObjectReport objects = parse_row_region();
        objects.sections[0].nodes[1].entry_count = 9;
        nfsu::TrackCullOptions options;
        options.verify_containment = false;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum, options);
        check(!report.ok(),
              "prepare_tree must refuse an entry count past five on its own");
        check(names(report, "entry count 9 is outside 1 to 5"),
              "and name the count and the bound");
        check_eq_u64(report.stats.sections_degraded, 1,
                     "the section degrades without the containment check");
        check_visible(report, {0, 1, 2, 3, 4, 5, 6, 7},
                      "and still offers every instance");
    }
    {
        // A traversal that would need more pending nodes than the limit.
        nfsu::TrackObjectReport objects = parse_row_region();
        nfsu::TrackCullOptions options;
        options.limits.max_pending_nodes = 1;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum, options);
        check(!report.ok(), "exhausting the pending stack must be reported");
        check_eq_u64(report.stats.sections_degraded, 1,
                     "exhausting the pending stack degrades the section");
    }
}

void test_instance_mask() {
    // 0x00445EE6 biases the flags word by 0x60 before the context byte is
    // applied to its low byte. The bias is the whole content of the rule, so
    // the cases below pin it from both sides.
    check(!nfsu::track_instance_masked_out(0x0060U, 0xFFU),
          "flags 0x0060 cancel the bias and survive any mask");
    check(nfsu::track_instance_masked_out(0x0020U, 0x40U),
          "flags 0x0020 raise bit 0x40 after the bias and are masked out");
    check(!nfsu::track_instance_masked_out(0x0020U, 0x20U),
          "flags 0x0020 clear bit 0x20 after the bias and survive");
    check(!nfsu::track_instance_masked_out(0x0000U, 0x00U),
          "a zero mask masks nothing");
    check(nfsu::track_instance_masked_out(0x0000U, 0x60U),
          "a zero flags word raises both bias bits");
    // The mask is a byte, so no bit of the flags word above bit 7 can ever
    // reach the result. That is a property of the parameter's type rather
    // than of an expression, which is why nothing here can pin it: an
    // explicit "& 0xFF" on the flags word would be an equivalent mutant.
    // What this case does pin is that the bias is applied to the low byte
    // and that a set high half does not leak into it.
    check(!nfsu::track_instance_masked_out(0xFF60U, 0xFFU),
          "a set high half of the flags word does not reach the result");

    const nfsu::TrackObjectReport objects = parse_row_region();
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "mask");
    nfsu::TrackCullOptions options;
    options.instance_mask = 0x60U;
    const nfsu::TrackCullReport report =
        cull_track_region(objects, frustum, options);
    check_visible(report, {},
                  "every fixture instance has flags 0 and is masked out by "
                  "0x60");
    check_eq_u64(report.stats.instances_culled_by_mask, 8,
                 "mask culled instances");

    // Gate order, which the camera above cannot show: at far 1000 the root
    // classifies inside, so no instance is box-tested whatever the order
    // is. At far 150 node 1 crosses, so its four instances would be tested
    // if the box came first.
    const nfsu::TrackFrustum crossing =
        frustum_or_fail(axis_camera(150.0F), "mask before box");
    const nfsu::TrackCullReport ordered =
        cull_track_region(objects, crossing, options);
    check_eq_u64(ordered.stats.instances_culled_by_mask, 4,
                 "the four instances the traversal reaches are masked out");
    check_eq_u64(ordered.stats.instance_tests, 0,
                 "0x00445EE9 runs before 0x00445F04: nothing is box-tested");

    // And the size gate after the box test, not before. With this scale
    // every instance of the near group fails the size gate, so the two
    // orders attribute the same four instances to different counters.
    nfsu::TrackCullOptions sized;
    sized.use_screen_size = true;
    sized.screen_scale = 20.0F;
    sized.screen_forward[0] = 1.0F;
    sized.screen_forward[1] = 0.0F;
    sized.screen_forward[2] = 0.0F;
    const nfsu::TrackCullReport after =
        cull_track_region(objects, crossing, sized);
    check_visible(after, {}, "nothing survives both gates");
    check_eq_u64(after.stats.instance_tests, 4, "all four are box-tested");
    check_eq_u64(after.stats.instances_culled_by_box, 3,
                 "three fail the box test first");
    check_eq_u64(after.stats.instances_culled_by_size, 1,
                 "and only the survivor reaches the size gate");
}

void test_screen_size_level() {
    const float eye[3] = {0.0F, 0.0F, 0.0F};
    const float forward[3] = {1.0F, 0.0F, 0.0F};

    // Nearer than the radius: the scale itself, with no division.
    const float near_point[3] = {15.0F, 0.0F, 0.0F};
    check(nfsu::track_screen_size_level(eye, forward, 100.0F, near_point,
                                        10.0F) == 100,
          "inside the radius the level is the scale");

    // distance = 400, so (100 / 400) * 10 = 2.5, truncated to 2.
    const float mid_point[3] = {410.0F, 0.0F, 0.0F};
    check(nfsu::track_screen_size_level(eye, forward, 100.0F, mid_point,
                                        10.0F) == 2,
          "the level truncates toward zero");
    check(2 >= nfsu::kTrackMinDrawLevel, "level 2 is drawn");

    // distance = 800, so (100 / 800) * 10 = 1.25, truncated to 1, which is
    // below the draw threshold.
    const float far_point[3] = {810.0F, 0.0F, 0.0F};
    check(nfsu::track_screen_size_level(eye, forward, 100.0F, far_point,
                                        10.0F) == 1,
          "a distant object truncates to 1");
    check(1 < nfsu::kTrackMinDrawLevel, "level 1 is not drawn");

    // 0x00445E58: behind the camera by more than the radius is rejected
    // outright, and behind it by less is not.
    const float behind[3] = {-20.0F, 0.0F, 0.0F};
    check(nfsu::track_screen_size_level(eye, forward, 100.0F, behind, 10.0F) == 0,
          "a point behind the camera by more than the radius gives 0");
    const float just_behind[3] = {-5.0F, 0.0F, 0.0F};
    check(nfsu::track_screen_size_level(eye, forward, 100.0F, just_behind,
                                        10.0F) == 100,
          "a point behind the camera by less than the radius is kept");

    // Exactly on the behind-the-camera boundary. 0x00445E58 continues when
    // the dot product equals the negated radius, so equality is kept.
    const float on_boundary[3] = {-10.0F, 0.0F, 0.0F};
    check(nfsu::track_screen_size_level(eye, forward, 100.0F, on_boundary,
                                        10.0F) == 100,
          "a point exactly one radius behind the camera is kept, not "
          "rejected");

    // Exactly on the other boundary, distance == radius. The scale and the
    // radius are chosen so that the two branches disagree in float:
    // (1000 / 15) * 15 is 999.99994, which truncates to 999, while the
    // branch retail takes yields the scale itself. Most pairs, 100 and 10
    // among them, round back exactly and would pin nothing.
    const float flush[3] = {30.0F, 0.0F, 0.0F};
    check(nfsu::track_screen_size_level(eye, forward, 1000.0F, flush, 15.0F) ==
              1000,
          "distance exactly equal to the radius takes the scale, not the "
          "division");

    const float nan_point[3] = {std::nanf(""), 0.0F, 0.0F};
    check(nfsu::track_screen_size_level(eye, forward, 100.0F, nan_point,
                                        10.0F) == 0,
          "a non-finite position gives 0 rather than an unspecified level");
}

void test_screen_size_gate() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "screen size");

    nfsu::TrackCullOptions options;
    options.use_screen_size = true;
    options.screen_scale = 100.0F;
    options.screen_forward[0] = 1.0F;
    options.screen_forward[1] = 0.0F;
    options.screen_forward[2] = 0.0F;
    // The fixture radius is 5, and kTrackRadiusBias adds 4, so the radius
    // the gate uses is 9. Instance i sits at 100 * (i + 1), so the level is
    // trunc(9 * 100 / (100 * (i + 1) - 9)): 9, 4, 3, 2, then 1 from the
    // fifth on, and kTrackMinDrawLevel drops everything at 1.
    const nfsu::TrackCullReport report =
        cull_track_region(objects, frustum, options);
    check_visible(report, {0, 1, 2, 3},
                  "the screen-size gate drops the far half of the row");
    check_eq_u64(report.stats.instances_culled_by_size, 4,
                 "instances culled by size");
    check(report.visible.size() == 4 && report.visible[0].screen_level == 9 &&
              report.visible[1].screen_level == 4 &&
              report.visible[2].screen_level == 3 &&
              report.visible[3].screen_level == 2,
          "the report carries the level each drawn instance earned");
    for (const nfsu::TrackVisibleInstance& item : report.visible) {
        check(item.screen_level >= nfsu::kTrackMinDrawLevel,
              "every drawn instance reports the level that let it through");
    }

    // Off by default: the same region and frustum keep everything.
    const nfsu::TrackCullReport ungated = cull_track_region(objects, frustum);
    check_eq_u64(ungated.stats.instances_visible, 8,
                 "the gate is off unless a scale is supplied");
    check_eq_u64(ungated.stats.instances_culled_by_size, 0,
                 "no instance is size-culled with the gate off");
}

// The forward axis names a direction. Retail's is a camera basis vector
// and is unit length, and the behind-the-camera reject compares a dot
// product against -radius, so a longer vector would move that boundary and
// the visible set with it.
void test_screen_forward_length_is_ignored() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "forward length");

    std::vector<std::size_t> first;
    const float lengths[3] = {1.0F, 450.0F, 45000.0F};
    for (std::size_t i = 0; i < 3; ++i) {
        nfsu::TrackCullOptions options;
        options.use_screen_size = true;
        options.screen_scale = 100.0F;
        options.screen_forward[0] = lengths[i];
        options.screen_forward[1] = 0.0F;
        options.screen_forward[2] = 0.0F;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum, options);
        check(report.ok(), "a non-unit forward axis is not an error");
        if (i == 0) {
            first = visible_indices(report);
            check(!first.empty(),
                  "the size gate must select something, or this compares two "
                  "empty sets");
        } else {
            check(visible_indices(report) == first,
                  "the visible set does not depend on the forward axis's "
                  "length (" + std::to_string(static_cast<int>(lengths[i])) +
                      ")");
        }
    }

    // A zero axis names no direction at all. The gate is refused rather
    // than run on it, and the run says so.
    nfsu::TrackCullOptions zero;
    zero.use_screen_size = true;
    zero.screen_scale = 100.0F;
    zero.screen_forward[0] = 0.0F;
    zero.screen_forward[1] = 0.0F;
    zero.screen_forward[2] = 0.0F;
    const nfsu::TrackCullReport report =
        cull_track_region(objects, frustum, zero);
    check(!report.ok(), "a zero forward axis must be reported");
    check_eq_u64(report.stats.instances_culled_by_size, 0,
                 "a zero forward axis disables the gate rather than culling");
    check_eq_u64(report.stats.instances_visible, 8,
                 "and the run still produces a superset");
}

void test_residency_rules() {
    // 0x00446473 and 0x00446497: 2600 to 2699 inclusive.
    check(!nfsu::track_section_always_resident(2599), "2599 is not always on");
    check(nfsu::track_section_always_resident(2600), "2600 is always on");
    check(nfsu::track_section_always_resident(2699), "2699 is always on");
    check(!nfsu::track_section_always_resident(2700), "2700 is not always on");
    // 0x00446459: number modulo 100 below 10.
    check(nfsu::track_section_resident_without_zone(303), "303 mod 100 is 3");
    check(nfsu::track_section_resident_without_zone(1209), "1209 mod 100 is 9");
    check(!nfsu::track_section_resident_without_zone(1210),
          "1210 mod 100 is 10");
    check(!nfsu::track_section_resident_without_zone(1299),
          "1299 mod 100 is 99");

    std::vector<SectionSpec> specs;
    specs.push_back(row_section(303));
    specs.push_back(row_section(1210));
    specs.push_back(row_section(2606));
    const nfsu::TrackObjectReport objects = parse_sections(specs);
    check(objects.ok() && objects.valid_section_count() == 3,
          "the three-section fixture must parse");
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "residency");

    {
        const nfsu::TrackCullReport report = cull_track_region(objects, frustum);
        check_eq_u64(report.stats.sections_considered, 3,
                     "every_section considers all three");
        check_eq_u64(report.stats.sections_resident, 3,
                     "every_section keeps all three");
        check_eq_u64(report.stats.instances_visible, 24,
                     "every_section draws all three sections");
        // 0x0044580E: the letter is 0x40 + number / 100, so 303 is C, 1210
        // is L and 2606 is Z. A summary that did not carry its own section's
        // identity and counts would be unusable, and nothing else in this
        // suite reads those four fields.
        check(report.sections.size() == 3 &&
                  report.sections[0].number == 303 &&
                  report.sections[1].number == 1210 &&
                  report.sections[2].number == 2606,
              "each summary carries its own section number");
        check(report.sections.size() == 3 && report.sections[0].has_letter &&
                  report.sections[1].has_letter &&
                  report.sections[2].has_letter &&
                  report.sections[0].letter == 'C' &&
                  report.sections[1].letter == 'L' &&
                  report.sections[2].letter == 'Z',
              "each summary carries its own section letter");
        check(report.sections.size() == 3 &&
                  report.sections[0].instances == 8 &&
                  report.sections[1].instances == 8 &&
                  report.sections[2].instances == 8 &&
                  report.sections[0].visible == 8 &&
                  report.sections[1].visible == 8 &&
                  report.sections[2].visible == 8,
              "each summary carries its own instance and visible counts");
    }
    {
        nfsu::TrackCullOptions options;
        options.residency.rule = nfsu::TrackResidencyRule::no_zone;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum, options);
        check_eq_u64(report.stats.sections_resident, 2,
                     "no_zone drops 1210 and keeps 303 and 2606");
        check_eq_u64(report.stats.instances_visible, 16, "no_zone visible");
        check(report.sections.size() == 3 && report.sections[0].resident &&
                  !report.sections[1].resident && report.sections[2].resident,
              "no_zone names which section it dropped");
    }
    {
        nfsu::TrackCullOptions options;
        options.residency.rule = nfsu::TrackResidencyRule::zone_list;
        options.residency.zone_sections.push_back(1210);
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum, options);
        check(report.sections.size() == 3 && !report.sections[0].resident &&
                  report.sections[1].resident && report.sections[2].resident,
              "a zone list keeps its own sections and the 26xx set");
        check_eq_u64(report.stats.sections_resident, 2, "zone_list resident");
    }
    {
        nfsu::TrackCullOptions options;
        options.residency.rule = nfsu::TrackResidencyRule::zone_list;
        const nfsu::TrackCullReport report =
            cull_track_region(objects, frustum, options);
        check(!report.ok(),
              "an empty zone list says the zone chunk is not decoded");
        bool named = false;
        for (const nfsu::TrackIssue& issue : report.issues) {
            if (issue.message.find("0x80034150") != std::string::npos) {
                named = true;
            }
        }
        check(named, "the issue names the undecoded chunk id");
        check_eq_u64(report.stats.sections_resident, 1,
                     "only the always-resident section survives an empty zone");
    }
}

// Three sections that differ in the two summary fields every fixture
// section above shares. Without these, forcing summary.instances to 8 or
// summary.has_letter to true passes the whole suite.
void test_summary_identity() {
    std::vector<SectionSpec> specs;
    specs.push_back(small_section(303, 3));
    specs.push_back(row_section(1210));
    // 0x0044580E gives 0x40 + 2700 / 100 = 0x5B, which is not a letter, so
    // this is the one retail-shaped number that produces has_letter false.
    specs.push_back(small_section(2700, 5));
    const nfsu::TrackObjectReport objects = parse_sections(specs);
    check(objects.ok() && objects.valid_section_count() == 3,
          "the mixed fixture must parse");
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(4000.0F), "summary identity");
    const nfsu::TrackCullReport report = cull_track_region(objects, frustum);

    check(report.sections.size() == 3 && report.sections[0].instances == 3 &&
              report.sections[1].instances == 8 &&
              report.sections[2].instances == 5,
          "each summary carries its own instance count, and they differ");
    check(report.sections.size() == 3 && report.sections[0].has_letter &&
              report.sections[1].has_letter &&
              !report.sections[2].has_letter,
          "a section number that yields no letter says so");
    check(report.sections.size() == 3 && report.sections[0].letter == 'C' &&
              report.sections[1].letter == 'L',
          "and the other two carry theirs");
    check_eq_u64(report.stats.instances_visible, 16,
                 "3 + 8 + 5 instances are visible");
}

// Every name the acceptance block in docs/TRACK_VISIBILITY.md quotes. A
// renamed policy or class would leave the paper describing output the
// program no longer prints.
void test_names_are_stable() {
    check(std::string(nfsu::track_cull_policy_name(
              nfsu::TrackCullPolicy::retail_tree)) == "retail-tree",
          "retail-tree keeps its name");
    check(std::string(nfsu::track_cull_policy_name(
              nfsu::TrackCullPolicy::flat_instance_test)) ==
              "flat-instance-test",
          "flat-instance-test keeps its name");
    check(std::string(nfsu::track_cull_policy_name(
              nfsu::TrackCullPolicy::draw_everything)) == "draw-everything",
          "draw-everything keeps its name");
    check(std::string(nfsu::track_box_class_name(
              nfsu::TrackBoxClass::outside)) == "outside",
          "outside keeps its name");
    check(std::string(nfsu::track_box_class_name(
              nfsu::TrackBoxClass::intersecting)) == "intersecting",
          "intersecting keeps its name");
    check(std::string(nfsu::track_box_class_name(
              nfsu::TrackBoxClass::inside)) == "inside",
          "inside keeps its name");

    // The default is the retail-derived cap, and the header derives it
    // from the 0x174-byte frame at 0x00446146 plus the three pushes after
    // it. Nothing else in the suite reads it.
    check(nfsu::TrackCullLimits{}.max_pending_nodes == 64,
          "the pending-node limit defaults to the retail cap of 64");
}

// A three-level tree. prepare_tree folds each node's subtree count into its
// parent in reverse pre-order so that a child is complete before its parent
// is folded; on a two-level tree the forward and reverse orders agree, so
// every other test in this suite passes either way.
void test_deep_tree_accounting() {
    const nfsu::TrackObjectReport objects =
        parse_sections(std::vector<SectionSpec>{deep_section()});
    check(objects.ok() && objects.valid_section_count() == 1,
          "the three-level fixture must parse");

    // A camera looking the other way puts the root outside, so the whole
    // tree is pruned in one step and instances_pruned has to be the total.
    nfsu::TrackCamera camera = axis_camera(1000.0F);
    camera.target[0] = -1.0F;
    const nfsu::TrackFrustum frustum = frustum_or_fail(camera, "deep tree");
    const nfsu::TrackCullReport report = cull_track_region(objects, frustum);
    check(report.ok(), "the three-level cull must raise no issue");
    check_visible(report, {}, "a camera facing away sees nothing");
    check_eq_u64(report.stats.nodes_culled, 1, "the root alone is culled");
    check_eq_u64(report.stats.instances_visited, 0, "nothing is visited");
    check_eq_u64(report.stats.instances_pruned, 8,
                 "the root's subtree count reaches three levels down");

    // And the same tree seen head on still reaches every instance.
    const nfsu::TrackFrustum ahead =
        frustum_or_fail(axis_camera(1000.0F), "deep tree ahead");
    const nfsu::TrackCullReport all = cull_track_region(objects, ahead);
    check_eq_u64(all.stats.instances_visited + all.stats.instances_pruned, 8,
                 "visited plus pruned is the instance count either way");
    check_eq_u64(all.stats.instances_visible, 8, "all eight are visible");
}

// Only a caller-built section can name an object the directory does not
// hold. Falling back to a zero radius without a word would make the
// instance look smaller than it is and cull it.
void test_object_index_out_of_range() {
    nfsu::TrackObjectReport objects = parse_row_region();
    objects.sections[0].instances[0].object_index = 99;
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "object index");

    // Without the size gate nothing reads the object, so nothing is said.
    const nfsu::TrackCullReport quiet = cull_track_region(objects, frustum);
    check(quiet.ok(), "an unread object index is not an error");

    nfsu::TrackCullOptions options;
    options.use_screen_size = true;
    options.screen_scale = 100.0F;
    options.screen_forward[0] = 1.0F;
    options.screen_forward[1] = 0.0F;
    options.screen_forward[2] = 0.0F;
    const nfsu::TrackCullReport report =
        cull_track_region(objects, frustum, options);
    check(!report.ok(), "an object index the gate needs must be reported");
    bool named = false;
    for (const nfsu::TrackIssue& issue : report.issues) {
        if (issue.message.find("object index 99 is outside the directory") !=
            std::string::npos) {
            named = true;
        }
    }
    check(named, "the issue names the index and the directory");
}

void test_degenerate_cameras() {
    nfsu::TrackFrustum frustum;
    nfsu::TrackCamera camera = axis_camera(1000.0F);
    check(nfsu::track_frustum_look_at(camera, frustum),
          "the reference camera builds a frustum");

    const auto refuses = [&](nfsu::TrackCamera bad, const std::string& what) {
        nfsu::TrackFrustum before = frustum;
        nfsu::TrackFrustum out = before;
        check(!nfsu::track_frustum_look_at(bad, out), what);
        check(std::memcmp(&out, &before, sizeof(out)) == 0,
              what + ": the frustum must be left untouched");
    };

    nfsu::TrackCamera eye_at_target = camera;
    eye_at_target.target[0] = eye_at_target.eye[0];
    refuses(eye_at_target, "an eye at the target is refused");

    nfsu::TrackCamera parallel_up = camera;
    parallel_up.up[0] = 1.0F;
    parallel_up.up[1] = 0.0F;
    refuses(parallel_up, "an up vector along the view direction is refused");

    nfsu::TrackCamera zero_up = camera;
    zero_up.up[1] = 0.0F;
    refuses(zero_up, "a zero up vector is refused");

    nfsu::TrackCamera reversed = camera;
    reversed.far_plane = reversed.near_plane;
    refuses(reversed, "a far plane at the near plane is refused");

    nfsu::TrackCamera no_near = camera;
    no_near.near_plane = 0.0F;
    refuses(no_near, "a near plane at zero is refused");

    nfsu::TrackCamera flat = camera;
    flat.vertical_fov = 0.0F;
    refuses(flat, "a zero field of view is refused");

    nfsu::TrackCamera straight = camera;
    straight.vertical_fov = 3.14159265F;
    refuses(straight, "a field of view of half a turn is refused");

    nfsu::TrackCamera squashed = camera;
    squashed.aspect = 0.0F;
    refuses(squashed, "a zero aspect ratio is refused");

    nfsu::TrackCamera infinite = camera;
    infinite.eye[1] = std::numeric_limits<float>::infinity();
    refuses(infinite, "a non-finite eye is refused");

    nfsu::TrackCamera not_a_number = camera;
    not_a_number.aspect = std::nanf("");
    refuses(not_a_number, "a non-finite aspect ratio is refused");
}

void test_invalid_sections_are_skipped() {
    // A region whose only section is refused contributes nothing, and the
    // report says so rather than pretending the section was culled away.
    std::vector<SectionSpec> specs;
    SectionSpec broken = row_section(303);
    // Node 1 names instance 0 twice, which parse_track_objects refuses.
    broken.nodes[1].entries[1] = 0;
    specs.push_back(broken);
    const nfsu::TrackObjectReport objects = parse_sections(specs);
    check(!objects.ok(), "the broken fixture must be refused by the parser");
    check_eq_u64(objects.valid_section_count(), 0, "no valid section");

    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(1000.0F), "invalid section");
    const nfsu::TrackCullReport report = cull_track_region(objects, frustum);
    check_eq_u64(report.stats.sections_considered, 0, "sections considered");
    check_eq_u64(report.stats.sections_resident, 0, "sections resident");
    check(report.sections.empty(), "an invalid section is not summarised");
    check(report.visible.empty(), "an invalid section draws nothing");
}

void test_determinism() {
    const nfsu::TrackObjectReport objects = parse_row_region();
    const nfsu::TrackFrustum frustum =
        frustum_or_fail(axis_camera(450.0F), "determinism");
    const nfsu::TrackCullReport a = cull_track_region(objects, frustum);
    const nfsu::TrackCullReport b = cull_track_region(objects, frustum);
    check(visible_indices(a) == visible_indices(b),
          "two runs over the same bytes give the same visible set");
    check(a.stats.node_tests == b.stats.node_tests &&
              a.stats.instance_tests == b.stats.instance_tests,
          "two runs over the same bytes give the same counts");
    check(std::string(nfsu::track_box_class_name(nfsu::TrackBoxClass::inside)) ==
              "inside",
          "box class names are stable");
}

} // namespace

int main() {
    test_coordinate_map();
    test_box_test_classes();
    test_frustum_planes();
    test_half_angles_differ();
    test_classify_boundaries();
    test_fixture_parses();
    test_camera_sees_everything();
    test_camera_prunes_far_group();
    test_camera_prunes_near_group();
    test_camera_tests_instances();
    test_instance_box_can_cross();
    test_visit_accounting();
    test_draw_everything_ignores_the_frustum();
    test_draw_everything_ignores_the_gates();
    test_flat_policy_agrees_with_the_tree();
    test_containment_failure_changes_the_answer();
    test_containment_entry_count_guard();
    test_broken_trees_degrade();
    test_instance_mask();
    test_screen_size_level();
    test_screen_size_gate();
    test_screen_forward_length_is_ignored();
    test_residency_rules();
    test_summary_identity();
    test_names_are_stable();
    test_deep_tree_accounting();
    test_object_index_out_of_range();
    test_degenerate_cameras();
    test_invalid_sections_are_skipped();
    test_determinism();

    std::cout << "track visibility tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
