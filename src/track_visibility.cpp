#include "nfsu/track_visibility.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {
namespace {

constexpr std::size_t kNoParent = static_cast<std::size_t>(-1);

std::string hex32(std::uint32_t value) {
    static const char* kDigits = "0123456789ABCDEF";
    std::string out = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        out.push_back(kDigits[(value >> shift) & 0xFU]);
    }
    return out;
}

void add_issue(
    std::vector<TrackIssue>* issues,
    std::uint64_t offset,
    const std::string& message) {
    if (issues != nullptr) {
        issues->push_back(TrackIssue{offset, message});
    }
}

bool finite3(const float v[3]) noexcept {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

float dot3(const float a[3], const float b[3]) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void cross3(const float a[3], const float b[3], float out[3]) noexcept {
    const float x = a[1] * b[2] - a[2] * b[1];
    const float y = a[2] * b[0] - a[0] * b[2];
    const float z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

bool normalize3(float v[3]) noexcept {
    const float length = std::sqrt(dot3(v, v));
    if (!std::isfinite(length) || length <= 0.0F) {
        return false;
    }
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
    return finite3(v);
}

// A world-frame plane through `point` with inward normal `normal`, written
// into the raw slot order the stored boxes use. The world map of
// docs/FORMATS.md:286 is x = px, y = py, z = -pz, so a world normal
// (nx, ny, nz) reads (nx, -nz, ny) against the raw slots and the constant is
// unchanged. The map is a signed axis permutation, so a unit world normal
// stays a unit raw normal.
bool make_plane(
    const float normal_world[3],
    const float point_world[3],
    TrackPlane& out) noexcept {
    float n[3] = {normal_world[0], normal_world[1], normal_world[2]};
    if (!normalize3(n)) {
        return false;
    }
    out.normal[0] = n[0];
    out.normal[1] = -n[2];
    out.normal[2] = n[1];
    out.d = -dot3(n, point_world);
    return std::isfinite(out.d);
}

// The subtree shape one section's traversal needs, and whether the tree can
// be walked at all.
struct TreePrep {
    bool usable = false;
    // Instances at or below each node.
    std::vector<std::uint64_t> subtree_instances;
    std::uint64_t reachable_instances = 0;
};

TreePrep prepare_tree(
    const TrackSection& section,
    const TrackCullLimits& limits,
    std::vector<TrackIssue>* issues) {
    TreePrep prep;
    const std::size_t node_count = section.nodes.size();
    const std::size_t instance_count = section.instances.size();
    if (node_count == 0) {
        add_issue(issues, section.offset,
                  "section " + std::to_string(section.number) +
                      ": the bounding tree holds no node, so nothing can be "
                      "culled with it");
        return prep;
    }

    prep.subtree_instances.assign(node_count, 0);
    std::vector<std::size_t> parent(node_count, kNoParent);
    std::vector<bool> seen(node_count, false);
    // The instance side needs its own seen vector, not a counter. A tree
    // that names one instance twice and another never has exactly as many
    // instance entries as instances, so counting alone cannot tell the two
    // apart and the walk would silently drop the unnamed one.
    std::vector<bool> seen_instance(instance_count, false);
    std::vector<std::size_t> order;
    order.reserve(node_count);
    std::vector<std::size_t> stack;
    stack.reserve(limits.max_pending_nodes + 1);

    stack.push_back(0);
    seen[0] = true;
    while (!stack.empty()) {
        const std::size_t index = stack.back();
        stack.pop_back();
        order.push_back(index);
        const TrackBoundNode& node = section.nodes[index];
        if (node.entry_count == 0 || node.entry_count > kSceneryBoundNodeEntries) {
            add_issue(issues, node.offset,
                      "section " + std::to_string(section.number) + " node " +
                          std::to_string(index) + ": entry count " +
                          std::to_string(node.entry_count) +
                          " is outside 1 to " +
                          std::to_string(kSceneryBoundNodeEntries));
            return prep;
        }
        for (std::uint32_t slot = 0; slot < node.entry_count; ++slot) {
            const std::int16_t entry = node.entries[slot];
            if (entry < 0) {
                const std::size_t child =
                    static_cast<std::size_t>(-static_cast<int>(entry));
                if (child >= node_count) {
                    add_issue(issues, node.offset,
                              "section " + std::to_string(section.number) +
                                  " node " + std::to_string(index) + " entry " +
                                  std::to_string(slot) + ": child node " +
                                  std::to_string(child) +
                                  " is outside the tree of " +
                                  std::to_string(node_count) + " nodes");
                    return prep;
                }
                if (seen[child]) {
                    add_issue(issues, node.offset,
                              "section " + std::to_string(section.number) +
                                  " node " + std::to_string(index) + " entry " +
                                  std::to_string(slot) + ": node " +
                                  std::to_string(child) +
                                  " is reached more than once, so the tree is "
                                  "not a tree and cannot be walked");
                    return prep;
                }
                seen[child] = true;
                parent[child] = index;
                if (stack.size() >= limits.max_pending_nodes) {
                    add_issue(issues, node.offset,
                              "section " + std::to_string(section.number) +
                                  ": the traversal needs more than " +
                                  std::to_string(limits.max_pending_nodes) +
                                  " pending nodes");
                    return prep;
                }
                stack.push_back(child);
            } else {
                const std::size_t instance = static_cast<std::size_t>(entry);
                if (instance >= instance_count) {
                    add_issue(issues, node.offset,
                              "section " + std::to_string(section.number) +
                                  " node " + std::to_string(index) + " entry " +
                                  std::to_string(slot) + ": instance " +
                                  std::to_string(instance) +
                                  " is outside the array of " +
                                  std::to_string(instance_count) +
                                  " instances");
                    return prep;
                }
                if (seen_instance[instance]) {
                    add_issue(issues, node.offset,
                              "section " + std::to_string(section.number) +
                                  " node " + std::to_string(index) + " entry " +
                                  std::to_string(slot) + ": instance " +
                                  std::to_string(instance) +
                                  " is reached more than once, so the tree is "
                                  "not a tree and cannot be walked");
                    return prep;
                }
                seen_instance[instance] = true;
                ++prep.subtree_instances[index];
                ++prep.reachable_instances;
            }
        }
    }

    // Reverse pre-order: a child is always folded into its parent before the
    // parent is folded into its own.
    for (std::size_t i = order.size(); i-- > 0;) {
        const std::size_t index = order[i];
        const std::size_t up = parent[index];
        if (up != kNoParent) {
            prep.subtree_instances[up] += prep.subtree_instances[index];
        }
    }

    if (prep.reachable_instances != static_cast<std::uint64_t>(instance_count)) {
        add_issue(issues, section.offset,
                  "section " + std::to_string(section.number) + ": the tree "
                  "names " +
                      std::to_string(prep.reachable_instances) + " of " +
                      std::to_string(instance_count) +
                      " instances, so walking it would never reach the rest");
        return prep;
    }

    prep.usable = true;
    return prep;
}

struct Pending {
    std::size_t node = 0;
    TrackBoxClass code = TrackBoxClass::intersecting;
};

} // namespace

void track_world_from_raw(const float raw[3], float world[3]) noexcept {
    const float x = raw[0];
    const float y = raw[2];
    const float z = -raw[1];
    world[0] = x;
    world[1] = y;
    world[2] = z;
}

void track_raw_from_world(const float world[3], float raw[3]) noexcept {
    const float p0 = world[0];
    const float p1 = -world[2];
    const float p2 = world[1];
    raw[0] = p0;
    raw[1] = p1;
    raw[2] = p2;
}

void track_box_to_float(
    const std::int16_t bounds_min[3],
    const std::int16_t bounds_max[3],
    float out_min[3],
    float out_max[3]) noexcept {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        out_min[axis] = static_cast<float>(static_cast<int>(bounds_min[axis]));
        out_max[axis] = static_cast<float>(static_cast<int>(bounds_max[axis]));
    }
}

TrackBoxClass track_classify_box(
    const TrackFrustum& frustum,
    const float bounds_min[3],
    const float bounds_max[3]) noexcept {
    // 0x0040DB68 to 0x0040DBAC: centre = (min + max) * 0.5, with the 0.5 at
    // 0x006CC7DC. 0x0040DBB0 to 0x0040DBD0 then takes max - centre, which is
    // the positive half extent.
    float centre[3];
    float extent[3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        centre[axis] = (bounds_max[axis] + bounds_min[axis]) * 0.5F;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        extent[axis] = bounds_max[axis] - centre[axis];
    }

    bool intersecting = false;
    for (std::size_t i = 0; i < kTrackFrustumPlanes; ++i) {
        const TrackPlane& plane = frustum.planes[i];
        // 0x0040DBD4 to 0x0040DBF0 sums the three |normal| * extent terms in
        // the slot order 2, 0, 1, and 0x0040DBF2 to 0x0040DC0A sums the
        // centre terms in the same order before adding the constant. The
        // order is kept because the result decides a strict comparison
        // against zero.
        const float radius = std::fabs(plane.normal[2]) * extent[2] +
                             std::fabs(plane.normal[0]) * extent[0] +
                             std::fabs(plane.normal[1]) * extent[1];
        const float distance = centre[2] * plane.normal[2] +
                               centre[0] * plane.normal[0] +
                               centre[1] * plane.normal[1] + plane.d;
        // 0x0040DC11: the far corner behind the plane rejects the box.
        if (distance + radius < 0.0F) {
            return TrackBoxClass::outside;
        }
        // 0x0040DC20: the near corner behind the plane leaves it crossing.
        if (distance - radius < 0.0F) {
            intersecting = true;
        }
    }
    // 0x0040DC3D: (esi == 0) ? 2 : 1.
    return intersecting ? TrackBoxClass::intersecting : TrackBoxClass::inside;
}

bool track_frustum_look_at(const TrackCamera& camera, TrackFrustum& frustum) noexcept {
    if (!finite3(camera.eye) || !finite3(camera.target) || !finite3(camera.up)) {
        return false;
    }
    if (!std::isfinite(camera.vertical_fov) || !std::isfinite(camera.aspect) ||
        !std::isfinite(camera.near_plane) || !std::isfinite(camera.far_plane)) {
        return false;
    }
    if (camera.vertical_fov <= 0.0F || camera.vertical_fov >= 3.14159265F) {
        return false;
    }
    if (camera.aspect <= 0.0F) {
        return false;
    }
    if (camera.near_plane <= 0.0F || camera.far_plane <= camera.near_plane) {
        return false;
    }

    float forward[3] = {camera.target[0] - camera.eye[0],
                        camera.target[1] - camera.eye[1],
                        camera.target[2] - camera.eye[2]};
    if (!normalize3(forward)) {
        return false;
    }
    float right[3];
    cross3(forward, camera.up, right);
    if (!normalize3(right)) {
        return false;
    }
    float up[3];
    cross3(right, forward, up);
    if (!normalize3(up)) {
        return false;
    }

    const float half_v = std::tan(camera.vertical_fov * 0.5F);
    if (!std::isfinite(half_v) || half_v <= 0.0F) {
        return false;
    }
    const float half_h = half_v * camera.aspect;
    if (!std::isfinite(half_h) || half_h <= 0.0F) {
        return false;
    }

    const float near_point[3] = {camera.eye[0] + forward[0] * camera.near_plane,
                                 camera.eye[1] + forward[1] * camera.near_plane,
                                 camera.eye[2] + forward[2] * camera.near_plane};
    const float far_point[3] = {camera.eye[0] + forward[0] * camera.far_plane,
                                camera.eye[1] + forward[1] * camera.far_plane,
                                camera.eye[2] + forward[2] * camera.far_plane};
    const float back[3] = {-forward[0], -forward[1], -forward[2]};

    const float left_edge[3] = {forward[0] - right[0] * half_h,
                                forward[1] - right[1] * half_h,
                                forward[2] - right[2] * half_h};
    const float right_edge[3] = {forward[0] + right[0] * half_h,
                                 forward[1] + right[1] * half_h,
                                 forward[2] + right[2] * half_h};
    const float bottom_edge[3] = {forward[0] - up[0] * half_v,
                                  forward[1] - up[1] * half_v,
                                  forward[2] - up[2] * half_v};
    const float top_edge[3] = {forward[0] + up[0] * half_v,
                               forward[1] + up[1] * half_v,
                               forward[2] + up[2] * half_v};

    float left_normal[3];
    float right_normal[3];
    float bottom_normal[3];
    float top_normal[3];
    cross3(left_edge, up, left_normal);
    cross3(up, right_edge, right_normal);
    cross3(right, bottom_edge, bottom_normal);
    cross3(top_edge, right, top_normal);

    TrackFrustum built;
    if (!make_plane(forward, near_point, built.planes[0]) ||
        !make_plane(back, far_point, built.planes[1]) ||
        !make_plane(left_normal, camera.eye, built.planes[2]) ||
        !make_plane(right_normal, camera.eye, built.planes[3]) ||
        !make_plane(bottom_normal, camera.eye, built.planes[4]) ||
        !make_plane(top_normal, camera.eye, built.planes[5])) {
        return false;
    }
    frustum = built;
    return true;
}

int track_screen_size_level(
    const float eye[3],
    const float forward[3],
    float scale,
    const float position[3],
    float radius) noexcept {
    if (!finite3(eye) || !finite3(forward) || !finite3(position) ||
        !std::isfinite(scale) || !std::isfinite(radius)) {
        return 0;
    }
    const float delta[3] = {position[0] - eye[0], position[1] - eye[1],
                            position[2] - eye[2]};
    // 0x00445E33 to 0x00445E58: the dot against the context axis at +0xB0,
    // rejected when it falls below the negated radius.
    const float along = dot3(delta, forward);
    if (!(along >= -radius)) {
        return 0;
    }
    // 0x00445E69 to 0x00445E84: the length of the same vector, less radius.
    const float length = std::sqrt(dot3(delta, delta));
    const float distance = length - radius;
    if (!std::isfinite(distance)) {
        return 0;
    }
    // 0x00445E88 to 0x00445EA8: scale when distance is at or below radius,
    // and (scale / distance) * radius otherwise, in that order.
    float value = scale;
    if (!(distance <= radius)) {
        value = (scale / distance) * radius;
    }
    if (!std::isfinite(value)) {
        return 0;
    }
    // 0x00445EAB: a truncating float-to-int conversion.
    if (value >= 2147483647.0F) {
        return 2147483647;
    }
    if (value <= -2147483648.0F) {
        return -2147483647 - 1;
    }
    return static_cast<int>(value);
}

std::size_t track_check_tree_containment(
    const TrackSection& section,
    std::vector<TrackIssue>* issues) {
    std::size_t failures = 0;
    static const char* kAxisName[3] = {"px", "pz", "py"};
    for (std::size_t index = 0; index < section.nodes.size(); ++index) {
        const TrackBoundNode& node = section.nodes[index];
        // The record holds five entry slots, so a count outside 1 to 5 is
        // reported rather than silently clamped: clamping would read the
        // right bytes for the wrong reason, and a caller-built node with a
        // count of nine would otherwise look contained.
        if (node.entry_count == 0 ||
            node.entry_count > kSceneryBoundNodeEntries) {
            ++failures;
            add_issue(issues, node.offset,
                      "section " + std::to_string(section.number) + " node " +
                          std::to_string(index) + ": entry count " +
                          std::to_string(node.entry_count) +
                          " is outside 1 to " +
                          std::to_string(kSceneryBoundNodeEntries));
            continue;
        }
        const std::uint32_t used = node.entry_count;
        for (std::uint32_t slot = 0; slot < used; ++slot) {
            const std::int16_t entry = node.entries[slot];
            const std::int16_t* child_min = nullptr;
            const std::int16_t* child_max = nullptr;
            std::string what;
            if (entry < 0) {
                const std::size_t child =
                    static_cast<std::size_t>(-static_cast<int>(entry));
                if (child >= section.nodes.size()) {
                    continue;
                }
                child_min = section.nodes[child].bounds_min;
                child_max = section.nodes[child].bounds_max;
                what = "node " + std::to_string(child);
            } else {
                const std::size_t instance = static_cast<std::size_t>(entry);
                if (instance >= section.instances.size()) {
                    continue;
                }
                child_min = section.instances[instance].bounds_min;
                child_max = section.instances[instance].bounds_max;
                what = "instance " + std::to_string(instance);
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const int low = static_cast<int>(child_min[axis]) -
                                static_cast<int>(node.bounds_min[axis]);
                const int high = static_cast<int>(node.bounds_max[axis]) -
                                 static_cast<int>(child_max[axis]);
                if (low < 0) {
                    ++failures;
                    add_issue(issues, node.offset,
                              "section " + std::to_string(section.number) +
                                  " node " + std::to_string(index) +
                                  ": " + what + " reaches " +
                                  std::to_string(-low) + " units below the " +
                                  "node box on " + kAxisName[axis]);
                }
                if (high < 0) {
                    ++failures;
                    add_issue(issues, node.offset,
                              "section " + std::to_string(section.number) +
                                  " node " + std::to_string(index) +
                                  ": " + what + " reaches " +
                                  std::to_string(-high) + " units above the " +
                                  "node box on " + kAxisName[axis]);
                }
            }
        }
    }
    return failures;
}

const char* track_cull_policy_name(TrackCullPolicy policy) noexcept {
    switch (policy) {
        case TrackCullPolicy::retail_tree:
            return "retail-tree";
        case TrackCullPolicy::draw_everything:
            return "draw-everything";
        case TrackCullPolicy::flat_instance_test:
            return "flat-instance-test";
    }
    return "unknown";
}

const char* track_box_class_name(TrackBoxClass value) noexcept {
    switch (value) {
        case TrackBoxClass::outside:
            return "outside";
        case TrackBoxClass::intersecting:
            return "intersecting";
        case TrackBoxClass::inside:
            return "inside";
    }
    return "unknown";
}

namespace {

bool section_is_resident(
    const TrackSection& section,
    const TrackResidency& residency) {
    switch (residency.rule) {
        case TrackResidencyRule::every_section:
            return true;
        case TrackResidencyRule::no_zone:
            return track_section_resident_without_zone(section.number);
        case TrackResidencyRule::zone_list:
            if (track_section_always_resident(section.number)) {
                return true;
            }
            for (std::uint32_t number : residency.zone_sections) {
                if (number == section.number) {
                    return true;
                }
            }
            return false;
    }
    return false;
}

// One instance, past the node, through the gates 0x00445EC0 applies in its
// own order: the flags mask, then the box test the node may have made
// unnecessary, then the screen-size level.
void emit_instance(
    const TrackSection& section,
    std::size_t section_index,
    std::size_t instance_index,
    std::size_t node_index,
    TrackBoxClass node_class,
    const TrackFrustum& frustum,
    const TrackCullOptions& options,
    bool use_frustum,
    bool apply_gates,
    TrackCullReport& report,
    TrackSectionCull& summary) {
    const TrackInstance& instance = section.instances[instance_index];
    ++report.stats.instances_visited;

    // apply_gates is false only under TrackCullPolicy::draw_everything,
    // which consults nothing at all. The flags mask and the screen-size
    // level are retail gates whose own inputs were not recovered, so a
    // fallback that still ran them could drop an instance for a value this
    // project invented, which is the one thing the fallback exists to
    // rule out.
    if (apply_gates &&
        track_instance_masked_out(instance.flags, options.instance_mask)) {
        ++report.stats.instances_culled_by_mask;
        return;
    }

    TrackBoxClass instance_class = TrackBoxClass::inside;
    if (use_frustum && node_class == TrackBoxClass::intersecting) {
        float box_min[3];
        float box_max[3];
        track_box_to_float(instance.bounds_min, instance.bounds_max, box_min,
                           box_max);
        instance_class = track_classify_box(frustum, box_min, box_max);
        ++report.stats.instance_tests;
        if (instance_class == TrackBoxClass::outside) {
            ++report.stats.instances_culled_by_box;
            return;
        }
    }

    int level = 0;
    if (apply_gates && options.use_screen_size) {
        const TrackObject* object = nullptr;
        if (instance.object_index < section.objects.size()) {
            object = &section.objects[instance.object_index];
        } else {
            // Only a caller-built section reaches this: parse_track_objects
            // refuses an instance whose object index is outside its own
            // directory. Falling back to a zero radius silently would make
            // the instance look smaller than it is and cull it.
            report.issues.push_back(TrackIssue{
                instance.offset,
                "section " + std::to_string(section.number) + " instance " +
                    std::to_string(instance_index) + ": object index " +
                    std::to_string(instance.object_index) +
                    " is outside the directory of " +
                    std::to_string(section.objects.size()) +
                    " objects, so the screen-size gate has no radius for it"});
        }
        const float radius =
            (object != nullptr ? object->radius : 0.0F) + kTrackRadiusBias;
        level = track_screen_size_level(options.screen_eye,
                                        options.screen_forward,
                                        options.screen_scale, instance.position,
                                        radius);
        if (level < kTrackMinDrawLevel) {
            ++report.stats.instances_culled_by_size;
            return;
        }
    }

    TrackVisibleInstance visible;
    visible.section_index = section_index;
    visible.instance_index = instance_index;
    visible.node_index = node_index;
    visible.node_class = node_class;
    visible.instance_class = instance_class;
    visible.screen_level = level;
    report.visible.push_back(visible);
    ++report.stats.instances_visible;
    ++summary.visible;
}

void cull_section_flat(
    const TrackSection& section,
    std::size_t section_index,
    const TrackFrustum& frustum,
    const TrackCullOptions& options,
    bool use_frustum,
    bool apply_gates,
    TrackCullReport& report,
    TrackSectionCull& summary) {
    for (std::size_t i = 0; i < section.instances.size(); ++i) {
        emit_instance(section, section_index, i, 0,
                      use_frustum ? TrackBoxClass::intersecting
                                  : TrackBoxClass::inside,
                      frustum, options, use_frustum, apply_gates, report,
                      summary);
    }
}

void cull_section_tree(
    const TrackSection& section,
    std::size_t section_index,
    const TreePrep& prep,
    const TrackFrustum& frustum,
    const TrackCullOptions& options,
    TrackCullReport& report,
    TrackSectionCull& summary) {
    // 0x00446155 to 0x00446168: node 0 goes on the stack with the code 1,
    // so the root is always tested.
    //
    // The stack never grows past prepare_tree's, which was bounded by
    // max_pending_nodes: this loop pushes and pops in exactly the same
    // pattern and a pruned subtree only removes pushes.
    std::vector<Pending> stack;
    stack.reserve(options.limits.max_pending_nodes + 1);
    stack.push_back(Pending{0, TrackBoxClass::intersecting});

    while (!stack.empty()) {
        // 0x00446170: the code and the node come off the top of the stack.
        const Pending current = stack.back();
        stack.pop_back();
        const TrackBoundNode& node = section.nodes[current.node];
        ++report.stats.nodes_visited;

        TrackBoxClass code = current.code;
        // 0x0044617A `cmp bl, 1`: a node reached with the inside code is not
        // tested at all, which is where the retail rule assumes the parent
        // box contains this one.
        if (code == TrackBoxClass::intersecting) {
            float box_min[3];
            float box_max[3];
            track_box_to_float(node.bounds_min, node.bounds_max, box_min,
                               box_max);
            code = track_classify_box(frustum, box_min, box_max);
            ++report.stats.node_tests;
        }
        // 0x00446203: code 0 skips the whole subtree.
        if (code == TrackBoxClass::outside) {
            ++report.stats.nodes_culled;
            report.stats.instances_pruned += prep.subtree_instances[current.node];
            continue;
        }

        // 0x00446220 to 0x00446279: every entry of the node, in entry order.
        // An instance is emitted where it stands; a child is pushed, so the
        // children come back off in reverse entry order.
        for (std::uint32_t slot = 0; slot < node.entry_count; ++slot) {
            const std::int16_t entry = node.entries[slot];
            if (entry < 0) {
                const std::size_t child =
                    static_cast<std::size_t>(-static_cast<int>(entry));
                stack.push_back(Pending{child, code});
            } else {
                emit_instance(section, section_index,
                              static_cast<std::size_t>(entry), current.node,
                              code, frustum, options, true, true, report,
                              summary);
            }
        }
    }
}

} // namespace

TrackCullReport cull_track_region(
    const TrackObjectReport& objects,
    const TrackFrustum& frustum,
    const TrackCullOptions& in_options) {
    TrackCullReport report;
    report.requested_policy = in_options.policy;

    // Retail's axis at render context +0xB0 is a camera basis vector and is
    // therefore unit length, and track_screen_size_level compares a dot
    // product against -radius, which only means "behind by more than the
    // radius" for a unit axis. A caller that hands in a direction of some
    // other length would otherwise get a visible set that depends on how
    // long the vector is, so the length is taken out here, once.
    TrackCullOptions options = in_options;
    if (options.use_screen_size) {
        float axis[3] = {options.screen_forward[0], options.screen_forward[1],
                         options.screen_forward[2]};
        if (!normalize3(axis)) {
            report.issues.push_back(TrackIssue{
                0, "the screen-size gate needs a non-zero finite forward "
                   "axis; it is disabled for this run"});
            options.use_screen_size = false;
        } else {
            options.screen_forward[0] = axis[0];
            options.screen_forward[1] = axis[1];
            options.screen_forward[2] = axis[2];
        }
    }

    for (std::size_t i = 0; i < objects.sections.size(); ++i) {
        const TrackSection& section = objects.sections[i];
        if (!section.valid) {
            continue;
        }
        ++report.stats.sections_considered;

        TrackSectionCull summary;
        summary.section_index = i;
        summary.number = section.number;
        summary.letter = section.letter;
        summary.has_letter = section.has_letter;
        summary.policy_used = options.policy;
        summary.instances = section.instances.size();

        if (!section_is_resident(section, options.residency)) {
            summary.resident = false;
            report.sections.push_back(summary);
            continue;
        }
        summary.resident = true;
        ++report.stats.sections_resident;

        if (options.policy == TrackCullPolicy::draw_everything) {
            cull_section_flat(section, i, frustum, options, false, false,
                              report, summary);
            report.sections.push_back(summary);
            continue;
        }
        if (options.policy == TrackCullPolicy::flat_instance_test) {
            cull_section_flat(section, i, frustum, options, true, true, report,
                              summary);
            report.sections.push_back(summary);
            continue;
        }

        // TrackCullPolicy::retail_tree from here down.
        bool degrade = false;
        if (options.verify_containment) {
            summary.containment_checked = true;
            const std::size_t failures =
                track_check_tree_containment(section, &report.issues);
            if (failures != 0) {
                summary.containment_holds = false;
                report.issues.push_back(TrackIssue{
                    section.offset,
                    "section " + std::to_string(section.number) + ": " +
                        std::to_string(failures) +
                        " node box overshoots; pruning on this tree could drop "
                        "visible geometry, so the section falls back to " +
                        track_cull_policy_name(
                            TrackCullPolicy::flat_instance_test)});
                degrade = true;
            }
        }

        TreePrep prep;
        if (!degrade) {
            // prepare_tree returns as soon as it raises an issue, so
            // prep.usable being false is the only signal needed here.
            prep = prepare_tree(section, options.limits, &report.issues);
            if (!prep.usable) {
                report.issues.push_back(TrackIssue{
                    section.offset,
                    "section " + std::to_string(section.number) +
                        ": the bounding tree cannot be walked, so the section "
                        "falls back to " +
                        track_cull_policy_name(
                            TrackCullPolicy::flat_instance_test)});
                degrade = true;
            }
        }

        if (degrade) {
            summary.policy_used = TrackCullPolicy::flat_instance_test;
            ++report.stats.sections_degraded;
            report.policy_held = false;
            cull_section_flat(section, i, frustum, options, true, true, report,
                              summary);
        } else {
            cull_section_tree(section, i, prep, frustum, options, report,
                              summary);
        }
        report.sections.push_back(summary);
    }

    // The id exists only so a diagnostic can name the chunk this project
    // cannot read; see docs/TRACK_VISIBILITY.md.
    if (in_options.residency.rule == TrackResidencyRule::zone_list &&
        in_options.residency.zone_sections.empty()) {
        report.issues.push_back(TrackIssue{
            0, "a zone list was requested with no section in it; the record "
               "layout inside the visibility-zone chunk " +
                   hex32(kTrackVisibilityZoneId) +
                   " is not decoded by this project"});
    }
    return report;
}

} // namespace nfsu
