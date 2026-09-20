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

    // Far off to one side, inside the depth range: this is the case a test
    // that only moved the far plane would never reach.
    const float aside_min[3] = {95.0F, 400.0F, -5.0F};
    const float aside_max[3] = {105.0F, 420.0F, 5.0F};
    check(nfsu::track_classify_box(frustum, aside_min, aside_max) ==
              nfsu::TrackBoxClass::outside,
          "a box outside the side planes classifies as outside");
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
// so 0x0044617D's shortcut fires: no other node and no instance is tested
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
    check_eq_u64(report.stats.node_tests, 0, "the fallback tests no node");
    check_eq_u64(report.stats.instance_tests, 0, "the fallback tests no box");
    check(!report.sections.empty() &&
              report.sections[0].policy_used ==
                  nfsu::TrackCullPolicy::draw_everything,
          "the report names the policy the section was culled with");
    check(std::string(nfsu::track_cull_policy_name(
              nfsu::TrackCullPolicy::draw_everything)) == "draw-everything",
          "the fallback policy has a stable name");
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

    // The unmodified tree must not trip the check, or the check would be
    // reporting on the fixture rather than on the mutation.
    const nfsu::TrackObjectReport sound = parse_row_region();
    const nfsu::TrackCullReport sound_report =
        cull_track_region(sound, frustum);
    check(sound_report.ok() && sound_report.policy_held,
          "the unmodified fixture must not report a containment failure");
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
    check(!nfsu::track_instance_masked_out(0xFF60U, 0xFFU),
          "only the low byte of the flags word takes part");

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
    check_eq_u64(report.stats.instance_tests, 0,
                 "the mask is applied before the box test");
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
        check_eq_u64(report.stats.sections_resident, 3,
                     "every_section keeps all three");
        check_eq_u64(report.stats.instances_visible, 24,
                     "every_section draws all three sections");
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
    test_fixture_parses();
    test_camera_sees_everything();
    test_camera_prunes_far_group();
    test_camera_prunes_near_group();
    test_camera_tests_instances();
    test_visit_accounting();
    test_draw_everything_ignores_the_frustum();
    test_flat_policy_agrees_with_the_tree();
    test_containment_failure_changes_the_answer();
    test_broken_trees_degrade();
    test_instance_mask();
    test_screen_size_level();
    test_screen_size_gate();
    test_residency_rules();
    test_degenerate_cameras();
    test_invalid_sections_are_skipped();
    test_determinism();

    std::cout << "track visibility tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
