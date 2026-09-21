#include "nfsu/track_probe.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

// The same guarantee src/render.cpp and src/track_render.cpp assert, for the
// same reason: with FLT_EVAL_METHOD != 0 an intermediate could be evaluated
// wider than double and two builds would disagree on a boundary. Every
// decision below is a comparison - a plane distance against zero, a screen
// level against 2, a camera value against a range - and a boundary that moved
// would change a reported verdict rather than a rounded digit.
#include <cfloat>
static_assert(FLT_EVAL_METHOD == 0, "track probe requires FLT_EVAL_METHOD == 0");

namespace nfsu {
namespace {

constexpr double kDegreesToRadians = 0.017453292519943295769236907684886;

bool finite_value(double value) noexcept {
    return value == value && value != std::numeric_limits<double>::infinity() &&
           value != -std::numeric_limits<double>::infinity();
}

bool finite_vec(const Vec3& v) noexcept {
    return finite_value(v.x) && finite_value(v.y) && finite_value(v.z);
}

Vec3 add(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 sub(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 scale(const Vec3& a, double s) noexcept {
    return Vec3{a.x * s, a.y * s, a.z * s};
}

double dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

bool normalize(const Vec3& v, Vec3& out) noexcept {
    const double length = std::sqrt(dot(v, v));
    if (!finite_value(length) || !(length > 0.0)) {
        return false;
    }
    out = Vec3{v.x / length, v.y / length, v.z / length};
    return finite_vec(out);
}

// Rodrigues, with the project's own sine and cosine rather than libm's, so
// that a moved camera reproduces wherever a rendered frame does. `axis` must
// be unit length.
Vec3 rotate_about(const Vec3& v, const Vec3& axis, double degrees) noexcept {
    const double c = det_cos_deg(degrees);
    const double s = det_sin_deg(degrees);
    const Vec3 term_v = scale(v, c);
    const Vec3 term_cross = scale(cross(axis, v), s);
    const Vec3 term_axis = scale(axis, dot(axis, v) * (1.0 - c));
    return add(add(term_v, term_cross), term_axis);
}

std::string number(double value) {
    return std::to_string(value);
}

void extend(TrackWorldBounds& bounds, const Vec3& point) noexcept {
    const double v[3] = {point.x, point.y, point.z};
    if (!bounds.valid) {
        for (int k = 0; k < 3; ++k) {
            bounds.min[k] = v[k];
            bounds.max[k] = v[k];
        }
        bounds.valid = true;
    } else {
        for (int k = 0; k < 3; ++k) {
            bounds.min[k] = std::min(bounds.min[k], v[k]);
            bounds.max[k] = std::max(bounds.max[k], v[k]);
        }
    }
    ++bounds.points;
}

// A box stated in the raw slot order, in the world frame. The map negates
// slot 1, so the world z interval is the negated raw interval with its ends
// exchanged; the same rule src/track_render.cpp applies, and the same reason
// it is not written the obvious way.
void extend_raw_box(
    TrackWorldBounds& bounds,
    const float raw_min[3],
    const float raw_max[3]) noexcept {
    const double lo[3] = {raw_min[0], raw_min[1], raw_min[2]};
    const double hi[3] = {raw_max[0], raw_max[1], raw_max[2]};
    extend(bounds, Vec3{lo[0], lo[2], -hi[1]});
    extend(bounds, Vec3{hi[0], hi[2], -lo[1]});
}

// The culler's record for one instance, rebuilt from TrackCullReport::visible
// so that every probed instance can be compared against what the culler said.
struct CullerAnswer {
    bool visible = false;
    std::size_t node_index = 0;
    TrackBoxClass node_class = TrackBoxClass::inside;
    TrackBoxClass instance_class = TrackBoxClass::inside;
    int screen_level = 0;
};

// The shape of one section's bounding tree, plus the frustum codes the
// traversal carries through it. Parents always precede children in `order`,
// which is what lets the code pass below run in one sweep.
struct TreeShape {
    std::vector<std::size_t> parent;
    std::vector<std::uint32_t> depth;
    std::vector<std::size_t> order;
    // Per instance: the leaf node whose entry names it.
    std::vector<std::size_t> naming_node;
    // Per node: the code the traversal resolves for it, and the first node at
    // or above it that classified outside.
    std::vector<TrackBoxClass> code;
    std::vector<std::size_t> prune_root;
    std::uint32_t max_depth = 0;
};

// The same shape rules prepare_tree applies in src/track_visibility.cpp: one
// parent per node, every node and every instance named exactly once, every
// instance reachable, and a bounded pending stack. A tree the culler walked
// passes these too, so a failure here is a disagreement rather than a
// property of the data.
bool build_tree_shape(
    const TrackSection& section,
    const TrackCullLimits& limits,
    TreeShape& shape,
    std::string& reason) {
    const std::size_t node_count = section.nodes.size();
    const std::size_t instance_count = section.instances.size();
    if (node_count == 0) {
        reason = "the bounding tree holds no node";
        return false;
    }
    shape.parent.assign(node_count, kTrackProbeNone);
    shape.depth.assign(node_count, 0);
    shape.naming_node.assign(instance_count, kTrackProbeNone);
    shape.order.clear();
    shape.order.reserve(node_count);

    std::vector<bool> seen_node(node_count, false);
    std::vector<bool> seen_instance(instance_count, false);
    std::vector<std::size_t> stack;
    stack.reserve(limits.max_pending_nodes + 1);
    stack.push_back(0);
    // A dead store, kept because prepare_tree in
    // src/track_visibility.cpp carries the same one and the two walks
    // are meant to read alike: a child index is the negation of a
    // negative entry and is therefore never 0, so seen_node[0] is
    // written and never read.
    seen_node[0] = true;
    std::uint64_t reached = 0;

    while (!stack.empty()) {
        const std::size_t index = stack.back();
        stack.pop_back();
        shape.order.push_back(index);
        const TrackBoundNode& node = section.nodes[index];
        if (node.entry_count == 0 ||
            node.entry_count > kSceneryBoundNodeEntries) {
            reason = "node " + std::to_string(index) + " has an entry count of " +
                     std::to_string(node.entry_count);
            return false;
        }
        for (std::uint32_t slot = 0; slot < node.entry_count; ++slot) {
            const std::int16_t entry = node.entries[slot];
            if (entry < 0) {
                const std::size_t child =
                    static_cast<std::size_t>(-static_cast<int>(entry));
                if (child >= node_count || seen_node[child]) {
                    reason = "node " + std::to_string(index) + " entry " +
                             std::to_string(slot) + " names node " +
                             std::to_string(child) +
                             ", which is outside the tree or already reached";
                    return false;
                }
                seen_node[child] = true;
                shape.parent[child] = index;
                shape.depth[child] = shape.depth[index] + 1;
                shape.max_depth = std::max(shape.max_depth, shape.depth[child]);
                if (stack.size() >= limits.max_pending_nodes) {
                    reason = "the traversal needs more than " +
                             std::to_string(limits.max_pending_nodes) +
                             " pending nodes";
                    return false;
                }
                stack.push_back(child);
            } else {
                const std::size_t instance = static_cast<std::size_t>(entry);
                if (instance >= instance_count || seen_instance[instance]) {
                    reason = "node " + std::to_string(index) + " entry " +
                             std::to_string(slot) + " names instance " +
                             std::to_string(instance) +
                             ", which is outside the array or already reached";
                    return false;
                }
                seen_instance[instance] = true;
                shape.naming_node[instance] = index;
                ++reached;
            }
        }
    }
    if (reached != static_cast<std::uint64_t>(instance_count)) {
        reason = "the tree names " + std::to_string(reached) + " of " +
                 std::to_string(instance_count) + " instances";
        return false;
    }
    return true;
}

// The codes the retail traversal carries, in one sweep over `order`. A node
// reached with the inside code is not tested at all, which is the assumption
// the retail prune rests on and the reason `code` is path dependent.
void resolve_tree_codes(
    const TrackSection& section,
    const TrackFrustum& frustum,
    TreeShape& shape) {
    const std::size_t node_count = section.nodes.size();
    shape.code.assign(node_count, TrackBoxClass::inside);
    shape.prune_root.assign(node_count, kTrackProbeNone);
    for (const std::size_t index : shape.order) {
        const std::size_t up = shape.parent[index];
        if (up != kTrackProbeNone && shape.prune_root[up] != kTrackProbeNone) {
            // Nothing below a pruned node is reached, so no code is resolved
            // for it; the prune root is carried down instead.
            shape.code[index] = TrackBoxClass::outside;
            shape.prune_root[index] = shape.prune_root[up];
            continue;
        }
        TrackBoxClass incoming = TrackBoxClass::intersecting;
        if (up != kTrackProbeNone) {
            incoming = shape.code[up];
        }
        TrackBoxClass resolved = incoming;
        if (incoming == TrackBoxClass::intersecting) {
            const TrackBoundNode& node = section.nodes[index];
            float box_min[3];
            float box_max[3];
            track_box_to_float(node.bounds_min, node.bounds_max, box_min,
                               box_max);
            resolved = track_classify_box(frustum, box_min, box_max);
        }
        shape.code[index] = resolved;
        if (resolved == TrackBoxClass::outside) {
            shape.prune_root[index] = index;
        }
    }
}

TrackResidencyReason residency_reason_of(
    std::uint32_t number_value,
    const TrackResidency& residency,
    bool& resident,
    std::size_t& zone_position) {
    zone_position = kTrackProbeNone;
    switch (residency.rule) {
        case TrackResidencyRule::every_section:
            resident = true;
            return TrackResidencyReason::every_section;
        case TrackResidencyRule::no_zone:
            resident = track_section_resident_without_zone(number_value);
            return resident ? TrackResidencyReason::sub_index_in_range
                            : TrackResidencyReason::sub_index_out_of_range;
        case TrackResidencyRule::zone_list:
            if (track_section_always_resident(number_value)) {
                resident = true;
                return TrackResidencyReason::always_resident_band;
            }
            for (std::size_t i = 0; i < residency.zone_sections.size(); ++i) {
                if (residency.zone_sections[i] == number_value) {
                    resident = true;
                    zone_position = i;
                    return TrackResidencyReason::zone_list_entry;
                }
            }
            resident = false;
            return TrackResidencyReason::not_in_zone_list;
    }
    resident = false;
    return TrackResidencyReason::not_in_zone_list;
}

// Where each prepared mesh's material groups sit in the prepared draw list.
// They are consecutive by construction - a prepared region holds one ordered
// draw per material record in mesh order - and this builds the table and
// refuses a region where they are not, rather than reading a range that
// happens to start in the right place.
bool build_mesh_draw_table(
    const PreparedTrackScene& scene,
    std::vector<std::size_t>& first,
    std::vector<std::size_t>& count,
    std::string& error) {
    const std::size_t mesh_count = scene.region.meshes.size();
    first.assign(mesh_count, kNoHandle);
    count.assign(mesh_count, 0);
    for (std::size_t d = 0; d < scene.region.draws.size(); ++d) {
        const std::size_t mesh = scene.region.draws[d].mesh;
        if (mesh >= mesh_count) {
            error = "prepared draw " + std::to_string(d) + " names mesh " +
                    std::to_string(mesh) + "; the region holds " +
                    std::to_string(mesh_count);
            return false;
        }
        if (first[mesh] == kNoHandle) {
            first[mesh] = d;
        } else if (first[mesh] + count[mesh] != d) {
            error = "the prepared groups of mesh " + std::to_string(mesh) +
                    " are not consecutive: group " + std::to_string(d) +
                    " follows a run that started at " +
                    std::to_string(first[mesh]);
            return false;
        }
        ++count[mesh];
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------
// The free camera
// ---------------------------------------------------------------------

bool validate_track_free_camera(
    const TrackFreeCamera& camera,
    std::string& error) {
    if (!finite_vec(camera.eye)) {
        error = "the camera eye is not finite";
        return false;
    }
    if (!finite_vec(camera.target)) {
        error = "the camera target is not finite";
        return false;
    }
    if (!finite_vec(camera.up)) {
        error = "the camera up vector is not finite";
        return false;
    }
    // The up vector names a direction and its length is thrown away,
    // so a bound on it looks redundant and is not: normalize squares
    // each component, and a finite vector near the top of the double
    // range overflows to infinity there. The guard inside normalize
    // catches that and reports it as an up vector parallel to the
    // view, which names the wrong thing. This names the right one.
    const double up[3] = {camera.up.x, camera.up.y, camera.up.z};
    for (int k = 0; k < 3; ++k) {
        if (up[k] < -kMaxTargetMagnitude || up[k] > kMaxTargetMagnitude) {
            error = "the camera up vector is " + number(up[k]) +
                    " on axis " + std::to_string(k) +
                    ", outside the range of +-" +
                    number(kMaxTargetMagnitude);
            return false;
        }
    }
    const double eye[3] = {camera.eye.x, camera.eye.y, camera.eye.z};
    const double target[3] = {camera.target.x, camera.target.y, camera.target.z};
    for (int k = 0; k < 3; ++k) {
        if (eye[k] < -kMaxTargetMagnitude || eye[k] > kMaxTargetMagnitude) {
            error = "the camera eye is at " + number(eye[k]) + " on axis " +
                    std::to_string(k) + ", outside the range of +-" +
                    number(kMaxTargetMagnitude);
            return false;
        }
        if (target[k] < -kMaxTargetMagnitude || target[k] > kMaxTargetMagnitude) {
            error = "the camera target is at " + number(target[k]) +
                    " on axis " + std::to_string(k) +
                    ", outside the range of +-" + number(kMaxTargetMagnitude);
            return false;
        }
    }
    if (!finite_value(camera.fov_y_deg) || camera.fov_y_deg < kMinFov ||
        camera.fov_y_deg > kMaxFov) {
        error = "the camera field of view must be within " + number(kMinFov) +
                ".." + number(kMaxFov);
        return false;
    }
    if (!finite_value(camera.near_plane) || camera.near_plane < kMinNear ||
        camera.near_plane > kMaxNear) {
        error = "the camera near plane must be within " + number(kMinNear) +
                ".." + number(kMaxNear);
        return false;
    }
    if (!finite_value(camera.far_plane) ||
        !(camera.far_plane > camera.near_plane) ||
        camera.far_plane > kMaxFar) {
        error = "the camera far plane must be greater than near and at most " +
                number(kMaxFar);
        return false;
    }
    if (camera.width == 0 || camera.width > kMaxTrackProbeFrameSize ||
        camera.height == 0 || camera.height > kMaxTrackProbeFrameSize) {
        error = "the camera frame size must be within 1 to " +
                std::to_string(kMaxTrackProbeFrameSize) + " on each axis";
        return false;
    }
    const Vec3 view = sub(camera.target, camera.eye);
    const double distance = std::sqrt(dot(view, view));
    if (!finite_value(distance) || distance < kMinDistance ||
        distance > kMaxDistance) {
        error = "the eye-to-target distance must be within " +
                number(kMinDistance) + ".." + number(kMaxDistance);
        return false;
    }
    return true;
}

bool track_camera_basis(
    const TrackFreeCamera& camera,
    TrackCameraBasis& basis,
    std::string& error) {
    if (!validate_track_free_camera(camera, error)) {
        return false;
    }
    const Vec3 view = sub(camera.target, camera.eye);
    TrackCameraBasis built;
    built.distance = std::sqrt(dot(view, view));
    if (!normalize(view, built.forward)) {
        error = "the camera eye is at its target, so there is no view "
                "direction";
        return false;
    }
    if (!normalize(cross(built.forward, camera.up), built.right)) {
        error = "the camera up vector is parallel to the view direction, so "
                "there is no camera basis";
        return false;
    }
    built.up_axis = cross(built.right, built.forward);
    if (!finite_vec(built.up_axis)) {
        error = "the camera basis is not finite";
        return false;
    }
    basis = built;
    return true;
}

bool track_frame_free_camera(
    const TrackWorldBounds& bounds,
    const TrackFramingOptions& framing,
    std::uint32_t width,
    std::uint32_t height,
    TrackFreeCamera& camera,
    std::string& error) {
    if (width == 0 || width > kMaxTrackProbeFrameSize || height == 0 ||
        height > kMaxTrackProbeFrameSize) {
        error = "the camera frame size must be within 1 to " +
                std::to_string(kMaxTrackProbeFrameSize) + " on each axis";
        return false;
    }
    OrbitCamera orbit;
    if (!frame_track_bounds(bounds, width, height, framing, orbit, error)) {
        return false;
    }
    TrackFreeCamera built;
    built.eye = orbit_eye(orbit);
    built.target = orbit.target;
    built.up = Vec3{0.0, 1.0, 0.0};
    built.fov_y_deg = orbit.fov_y_deg;
    built.near_plane = orbit.near_plane;
    built.far_plane = orbit.far_plane;
    built.width = width;
    built.height = height;
    if (!validate_track_free_camera(built, error)) {
        error = "the framed camera is invalid: " + error;
        return false;
    }
    camera = built;
    return true;
}

bool track_move_free_camera(
    const TrackFreeCamera& camera,
    const TrackCameraMoves& moves,
    TrackFreeCamera& out,
    std::string& error) {
    TrackCameraBasis basis;
    if (!track_camera_basis(camera, basis, error)) {
        return false;
    }
    if (moves.has_turn) {
        if (!finite_value(moves.turn_yaw_deg) ||
            !finite_value(moves.turn_pitch_deg) ||
            moves.turn_yaw_deg < -kMaxTrackTurnDegrees ||
            moves.turn_yaw_deg > kMaxTrackTurnDegrees ||
            moves.turn_pitch_deg < -kMaxTrackTurnDegrees ||
            moves.turn_pitch_deg > kMaxTrackTurnDegrees) {
            error = "a turn needs a yaw and a pitch within +-" +
                    number(kMaxTrackTurnDegrees) + " degrees";
            return false;
        }
    }
    if (moves.has_fly) {
        const double legs[3] = {moves.fly_forward, moves.fly_right,
                                moves.fly_up};
        for (int k = 0; k < 3; ++k) {
            if (!finite_value(legs[k]) || legs[k] < -kMaxTrackFlyDistance ||
                legs[k] > kMaxTrackFlyDistance) {
                error = "a fly needs three distances within +-" +
                        number(kMaxTrackFlyDistance);
                return false;
            }
        }
    }

    TrackFreeCamera moved = camera;

    // Turn first: the fly below is stated in the camera basis, and after a
    // turn that basis is the turned one.
    if (moves.has_turn) {
        Vec3 axis;
        if (!normalize(camera.up, axis)) {
            error = "the camera up vector is zero, so a turn has no yaw axis";
            return false;
        }
        // Positive yaw turns the view towards the camera's own right, which
        // is a rotation about the NEGATED reference up; positive pitch raises
        // it. Both conventions are this project's, stated in
        // docs/TRACK_PROBE.md: retail is handed a frustum and builds none.
        const Vec3 yawed =
            rotate_about(basis.forward, axis, -moves.turn_yaw_deg);
        Vec3 forward_after_yaw;
        if (!normalize(yawed, forward_after_yaw)) {
            error = "the yaw left no view direction";
            return false;
        }
        Vec3 right_after_yaw;
        if (!normalize(cross(forward_after_yaw, axis), right_after_yaw)) {
            error = "the yawed view direction is parallel to the up vector, "
                    "so the pitch has no axis";
            return false;
        }
        const Vec3 pitched = rotate_about(forward_after_yaw, right_after_yaw,
                                          moves.turn_pitch_deg);
        Vec3 forward;
        if (!normalize(pitched, forward)) {
            error = "the pitch left no view direction";
            return false;
        }
        moved.target = add(moved.eye, scale(forward, basis.distance));
        if (!validate_track_free_camera(moved, error)) {
            error = "the turned camera is invalid: " + error;
            return false;
        }
        if (!track_camera_basis(moved, basis, error)) {
            error = "the turned camera has no basis: " + error;
            return false;
        }
    }

    if (moves.has_fly) {
        const Vec3 step = add(add(scale(basis.forward, moves.fly_forward),
                                  scale(basis.right, moves.fly_right)),
                              scale(basis.up_axis, moves.fly_up));
        if (!finite_vec(step)) {
            error = "the fly step is not finite";
            return false;
        }
        moved.eye = add(moved.eye, step);
        moved.target = add(moved.target, step);
        if (!validate_track_free_camera(moved, error)) {
            error = "the flown camera is invalid: " + error;
            return false;
        }
    }

    out = moved;
    return true;
}

bool track_cull_camera_of(
    const TrackFreeCamera& camera,
    TrackCamera& out,
    std::string& error) {
    if (!validate_track_free_camera(camera, error)) {
        return false;
    }
    TrackCamera built;
    built.eye[0] = static_cast<float>(camera.eye.x);
    built.eye[1] = static_cast<float>(camera.eye.y);
    built.eye[2] = static_cast<float>(camera.eye.z);
    built.target[0] = static_cast<float>(camera.target.x);
    built.target[1] = static_cast<float>(camera.target.y);
    built.target[2] = static_cast<float>(camera.target.z);
    built.up[0] = static_cast<float>(camera.up.x);
    built.up[1] = static_cast<float>(camera.up.y);
    built.up[2] = static_cast<float>(camera.up.z);
    built.vertical_fov =
        static_cast<float>(camera.fov_y_deg * kDegreesToRadians);
    built.aspect = static_cast<float>(static_cast<double>(camera.width) /
                                      static_cast<double>(camera.height));
    built.near_plane = static_cast<float>(camera.near_plane);
    built.far_plane = static_cast<float>(camera.far_plane);
    // The narrowing is the only step that can lose a bound the double camera
    // held, so it is checked rather than assumed: a near plane of 1e-4 and a
    // far plane one ulp above it both survive, and a pair that collapses does
    // not reach track_frustum_look_at as a frustum with no depth.
    if (!(built.vertical_fov > 0.0F) || !(built.aspect > 0.0F) ||
        !(built.near_plane > 0.0F) || !(built.far_plane > built.near_plane)) {
        error = "the camera does not narrow to a usable float frustum";
        return false;
    }
    out = built;
    return true;
}

// ---------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------

const char* track_residency_reason_name(TrackResidencyReason reason) noexcept {
    switch (reason) {
        case TrackResidencyReason::every_section:
            return "every-section";
        case TrackResidencyReason::sub_index_in_range:
            return "sub-index-in-range";
        case TrackResidencyReason::sub_index_out_of_range:
            return "sub-index-out-of-range";
        case TrackResidencyReason::always_resident_band:
            return "always-resident-band";
        case TrackResidencyReason::zone_list_entry:
            return "zone-list-entry";
        case TrackResidencyReason::not_in_zone_list:
            return "not-in-zone-list";
    }
    return "unknown";
}

const char* track_gate_verdict_name(TrackGateVerdict verdict) noexcept {
    switch (verdict) {
        case TrackGateVerdict::passed:
            return "passed";
        case TrackGateVerdict::rejected:
            return "rejected";
        case TrackGateVerdict::not_applied:
            return "not-applied";
    }
    return "unknown";
}

// ---------------------------------------------------------------------
// The probe
// ---------------------------------------------------------------------

bool probe_track_scene(
    const TrackRegionReport& geometry,
    const TrackObjectReport& objects,
    const PreparedTrackScene& scene,
    const TrackCullReport& cull,
    const TrackFrustum& frustum,
    const TrackProbeOptions& options,
    TrackProbeReport& report,
    std::string& error) {
    report = TrackProbeReport{};
    try {
        // The three reports have to be about the same two regions, or every
        // offset below describes something the caller did not ask about.
        if (scene.object_region.offset != objects.region.offset ||
            scene.object_region.size != objects.region.size) {
            error = "the scene was placed from the object region at " +
                    std::to_string(scene.object_region.offset) + "+" +
                    std::to_string(scene.object_region.size) +
                    " and the object report covers " +
                    std::to_string(objects.region.offset) + "+" +
                    std::to_string(objects.region.size);
            return false;
        }
        if (scene.region.region.offset != geometry.region.offset ||
            scene.region.region.size != geometry.region.size) {
            error = "the scene was prepared from the geometry region at " +
                    std::to_string(scene.region.region.offset) + "+" +
                    std::to_string(scene.region.region.size) +
                    " and the geometry report covers " +
                    std::to_string(geometry.region.offset) + "+" +
                    std::to_string(geometry.region.size);
            return false;
        }

        const std::size_t section_count = objects.sections.size();
        const TrackProbeSelection& select = options.select;

        // The selection, checked before anything is measured. A selection
        // that names nothing is refused rather than answered with an empty
        // report, which cannot be told from a region that holds nothing.
        if (!select.instances.empty() && !select.has_section) {
            error = "an instance selection needs a section, because an "
                    "instance index means nothing on its own";
            return false;
        }
        if (select.has_section && select.section_index >= section_count) {
            error = "the selected section " +
                    std::to_string(select.section_index) +
                    " is outside the region's " +
                    std::to_string(section_count) + " sections";
            return false;
        }
        if (select.has_section && !objects.sections[select.section_index].valid) {
            error = "the selected section " +
                    std::to_string(select.section_index) +
                    " is not valid, so it carries no instance to probe";
            return false;
        }
        std::vector<std::size_t> wanted_instances = select.instances;
        if (!wanted_instances.empty()) {
            const std::size_t available =
                objects.sections[select.section_index].instances.size();
            std::sort(wanted_instances.begin(), wanted_instances.end());
            for (std::size_t i = 0; i < wanted_instances.size(); ++i) {
                if (wanted_instances[i] >= available) {
                    error = "the selected instance " +
                            std::to_string(wanted_instances[i]) +
                            " is outside section " +
                            std::to_string(select.section_index) + "'s " +
                            std::to_string(available) + " instances";
                    return false;
                }
                if (i > 0 && wanted_instances[i] == wanted_instances[i - 1]) {
                    error = "instance " + std::to_string(wanted_instances[i]) +
                            " is selected more than once";
                    return false;
                }
            }
        }
        if (!select.mesh_name.empty()) {
            bool found = false;
            for (const PreparedTrackMesh& mesh : scene.region.meshes) {
                if (mesh.name == select.mesh_name) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                error = "no prepared mesh of this region is named '" +
                        select.mesh_name + "'";
                return false;
            }
        }

        std::vector<std::size_t> mesh_first_draw;
        std::vector<std::size_t> mesh_draw_count;
        if (!build_mesh_draw_table(scene, mesh_first_draw, mesh_draw_count,
                                   error)) {
            return false;
        }

        // The culler's own answers, keyed by (section, instance).
        std::vector<std::size_t> slot_base(section_count, 0);
        std::size_t total_slots = 0;
        for (std::size_t i = 0; i < section_count; ++i) {
            slot_base[i] = total_slots;
            total_slots += objects.sections[i].instances.size();
        }
        std::vector<CullerAnswer> answers(total_slots);
        for (const TrackVisibleInstance& item : cull.visible) {
            if (item.section_index >= section_count ||
                item.instance_index >=
                    objects.sections[item.section_index].instances.size()) {
                error = "the cull report names section " +
                        std::to_string(item.section_index) + " instance " +
                        std::to_string(item.instance_index) +
                        ", which this object region does not hold";
                return false;
            }
            CullerAnswer& answer =
                answers[slot_base[item.section_index] + item.instance_index];
            if (answer.visible) {
                error = "the cull report names section " +
                        std::to_string(item.section_index) + " instance " +
                        std::to_string(item.instance_index) + " twice";
                return false;
            }
            answer.visible = true;
            answer.node_index = item.node_index;
            answer.node_class = item.node_class;
            answer.instance_class = item.instance_class;
            answer.screen_level = item.screen_level;
        }

        // The culler's per-section summary, by section index.
        std::vector<const TrackSectionCull*> summaries(section_count, nullptr);
        for (const TrackSectionCull& summary : cull.sections) {
            if (summary.section_index >= section_count) {
                error = "the cull report summarises section " +
                        std::to_string(summary.section_index) +
                        ", which this object region does not hold";
                return false;
            }
            summaries[summary.section_index] = &summary;
        }

        // The screen-size gate's forward axis, normalized exactly as
        // cull_track_region normalizes it, so this gate sees the culler's
        // input and not the caller's unscaled direction.
        TrackCullOptions cull_options = options.cull;
        if (cull_options.use_screen_size) {
            const Vec3 given{cull_options.screen_forward[0],
                             cull_options.screen_forward[1],
                             cull_options.screen_forward[2]};
            Vec3 axis;
            if (!normalize(given, axis)) {
                cull_options.use_screen_size = false;
                report.issues.push_back(TrackIssue{
                    0, "the screen-size gate needs a non-zero finite forward "
                       "axis; it is disabled for this run"});
            } else {
                cull_options.screen_forward[0] = static_cast<float>(axis.x);
                cull_options.screen_forward[1] = static_cast<float>(axis.y);
                cull_options.screen_forward[2] = static_cast<float>(axis.z);
            }
        }

        report.cost.sections = section_count;
        for (std::size_t i = 0; i < section_count; ++i) {
            const TrackSection& section = objects.sections[i];
            TrackSectionProbe record;
            record.section_index = i;
            record.number = section.number;
            record.letter = section.letter;
            record.has_letter = section.has_letter;
            record.offset = section.offset;
            record.valid = section.valid;
            record.instances = section.instances.size();
            record.nodes = section.nodes.size();
            record.tree_depth = section.tree_depth;
            if (!section.valid) {
                report.sections.push_back(record);
                continue;
            }
            ++report.cost.valid_sections;
            report.cost.instances += section.instances.size();

            const TrackSectionCull* summary = summaries[i];
            if (summary == nullptr) {
                error = "the cull report does not cover valid section " +
                        std::to_string(i) +
                        ", so it was produced from another object region";
                return false;
            }
            record.policy_used = summary->policy_used;
            record.containment_checked = summary->containment_checked;
            record.containment_holds = summary->containment_holds;

            bool resident = false;
            std::size_t zone_position = kTrackProbeNone;
            record.residency_reason = residency_reason_of(
                section.number, cull_options.residency, resident, zone_position);
            record.zone_position = zone_position;
            record.resident = resident;
            if (resident != summary->resident) {
                report.issues.push_back(TrackIssue{
                    section.offset,
                    "section " + std::to_string(section.number) +
                        ": the residency rule says " +
                        (resident ? "resident" : "not resident") +
                        " and the cull report says " +
                        (summary->resident ? "resident" : "not resident")});
                ++report.cost.disagreements;
                resident = summary->resident;
                record.resident = resident;
            }
            if (resident) {
                ++report.cost.resident_sections;
            }

            const bool use_frustum =
                record.policy_used != TrackCullPolicy::draw_everything;
            // The culler keeps these two apart - cull_section_flat
            // takes both - and passes (false, false) under
            // draw_everything and (true, true) everywhere else, so
            // collapsing them is faithful to every call site it has
            // today. It is a coupling to another file's shape rather
            // than a rule of this one, and a policy that ever
            // separated the two would need this line separated too.
            const bool apply_gates = use_frustum;

            TreeShape shape;
            if (resident && record.policy_used == TrackCullPolicy::retail_tree) {
                std::string reason;
                if (!build_tree_shape(section, cull_options.limits, shape,
                                      reason)) {
                    report.issues.push_back(TrackIssue{
                        section.offset,
                        "section " + std::to_string(section.number) +
                            ": the cull report walked this bounding tree and "
                            "this probe cannot: " + reason});
                    ++report.cost.disagreements;
                } else {
                    resolve_tree_codes(section, frustum, shape);
                    record.tree_walked = true;
                    record.tree_depth = shape.max_depth;
                }
            }
            // A retail-tree section this probe could not walk carries no
            // verdict of its own, so nothing below may be compared against
            // the culler's node, box class or level for it.
            const bool have_tree =
                record.policy_used != TrackCullPolicy::retail_tree ||
                record.tree_walked;

            const bool section_selected =
                !select.has_section || select.section_index == i;
            for (std::size_t k = 0;
                 section_selected && k < section.instances.size(); ++k) {
                if (!wanted_instances.empty() &&
                    !std::binary_search(wanted_instances.begin(),
                                        wanted_instances.end(), k)) {
                    continue;
                }
                const TrackInstance& instance = section.instances[k];
                const CullerAnswer& answer = answers[slot_base[i] + k];

                // Every issue about THIS instance waits here until
                // the selection below is known to keep it. An issue
                // raised earlier reached the report - and the exit
                // status - for an instance the report does not list,
                // so `--mesh NAME` could fail over something it had
                // filtered out.
                std::vector<TrackIssue> pending;
                std::uint64_t pending_disagreements = 0;

                TrackInstanceProbe probe;
                probe.section_index = i;
                probe.instance_index = k;
                probe.section_number = section.number;
                probe.section_letter = section.letter;
                probe.has_section_letter = section.has_letter;
                probe.section_offset = section.offset;
                probe.instance_offset = instance.offset;
                probe.object_index = instance.object_index;
                probe.instance_flags = instance.flags;
                probe.transform = track_instance_transform(instance);
                for (int axis = 0; axis < 3; ++axis) {
                    probe.stored_min[axis] = instance.bounds_min[axis];
                    probe.stored_max[axis] = instance.bounds_max[axis];
                }
                float box_min[3];
                float box_max[3];
                track_box_to_float(instance.bounds_min, instance.bounds_max,
                                   box_min, box_max);
                extend_raw_box(probe.stored_world, box_min, box_max);

                const TrackObject* object = nullptr;
                if (instance.object_index < section.objects.size()) {
                    object = &section.objects[instance.object_index];
                    probe.object_offset = object->offset;
                    for (std::size_t s = 0; s < kSceneryObjectMeshSlots; ++s) {
                        probe.object_mesh_hash[s] = object->mesh_hash[s];
                    }
                    probe.object_flags = object->flags;
                    probe.object_flags2 = object->flags2;
                    probe.object_radius = object->radius;
                } else {
                    pending.push_back(TrackIssue{
                        instance.offset,
                        "section " + std::to_string(section.number) +
                            " instance " + std::to_string(k) +
                            ": object index " +
                            std::to_string(instance.object_index) +
                            " is outside the directory of " +
                            std::to_string(section.objects.size()) +
                            " objects"});
                }

                probe.section_resident = record.resident;
                probe.residency_reason = record.residency_reason;
                probe.zone_position = record.zone_position;
                probe.policy_used = record.policy_used;

                // The measurements, taken for every probed instance whether
                // or not the gate that reads one ran. This is what lets the
                // report say "pruned at node 3, own box inside".
                probe.mask_bits =
                    (instance.flags ^ kTrackInstanceMaskBias) &
                    static_cast<std::uint32_t>(cull_options.instance_mask);
                probe.own_box_class =
                    track_classify_box(frustum, box_min, box_max);
                if (cull_options.use_screen_size) {
                    const float radius =
                        (object != nullptr ? object->radius : 0.0F) +
                        kTrackRadiusBias;
                    probe.screen_level = track_screen_size_level(
                        cull_options.screen_eye, cull_options.screen_forward,
                        cull_options.screen_scale, instance.position, radius);
                    probe.screen_level_measured = true;
                }

                // The traversal, reproduced. Everything below is the order
                // src/track_visibility.cpp applies, and a gate after a
                // rejection is not-applied because the culler returned.
                bool visible = record.resident;
                if (record.resident && have_tree) {
                    if (record.policy_used == TrackCullPolicy::retail_tree) {
                        const std::size_t naming = shape.naming_node[k];
                        probe.node_index = naming;
                        probe.node_depth = shape.depth[naming];
                        probe.pruned_by_node = shape.prune_root[naming];
                        probe.node_class = shape.code[naming];
                    } else {
                        probe.node_index = 0;
                        probe.node_depth = 0;
                        probe.node_class = use_frustum
                                               ? TrackBoxClass::intersecting
                                               : TrackBoxClass::inside;
                    }

                    if (probe.pruned_by_node != kTrackProbeNone) {
                        visible = false;
                    } else {
                        if (apply_gates) {
                            probe.mask_gate = probe.mask_bits != 0
                                                  ? TrackGateVerdict::rejected
                                                  : TrackGateVerdict::passed;
                        }
                        if (probe.mask_gate == TrackGateVerdict::rejected) {
                            visible = false;
                        } else {
                            if (use_frustum &&
                                probe.node_class ==
                                    TrackBoxClass::intersecting) {
                                probe.instance_class = probe.own_box_class;
                                probe.box_gate =
                                    probe.instance_class ==
                                            TrackBoxClass::outside
                                        ? TrackGateVerdict::rejected
                                        : TrackGateVerdict::passed;
                            }
                            if (probe.box_gate == TrackGateVerdict::rejected) {
                                visible = false;
                            } else if (apply_gates &&
                                       cull_options.use_screen_size) {
                                probe.size_gate =
                                    probe.screen_level < kTrackMinDrawLevel
                                        ? TrackGateVerdict::rejected
                                        : TrackGateVerdict::passed;
                                if (probe.size_gate ==
                                    TrackGateVerdict::rejected) {
                                    visible = false;
                                }
                            }
                        }
                    }
                } else if (record.resident) {
                    // No verdict of this probe's own is possible for the
                    // section; the culler's answer is carried and the
                    // disagreement is already an issue above.
                    visible = answer.visible;
                }
                probe.visible = visible;
                probe.culler_visible = answer.visible;

                if (probe.visible != probe.culler_visible) {
                    pending.push_back(TrackIssue{
                        instance.offset,
                        "section " + std::to_string(section.number) +
                            " instance " + std::to_string(k) +
                            ": this probe says " +
                            (probe.visible ? "visible" : "not visible") +
                            " and the cull report says " +
                            (probe.culler_visible ? "visible"
                                                  : "not visible")});
                    ++pending_disagreements;
                } else if (probe.culler_visible && have_tree &&
                           (probe.node_index != answer.node_index ||
                            probe.node_class != answer.node_class ||
                            probe.instance_class != answer.instance_class ||
                            (apply_gates && cull_options.use_screen_size &&
                             probe.screen_level != answer.screen_level))) {
                    pending.push_back(TrackIssue{
                        instance.offset,
                        "section " + std::to_string(section.number) +
                            " instance " + std::to_string(k) +
                            ": the cull report recorded node " +
                            std::to_string(answer.node_index) + " " +
                            track_box_class_name(answer.node_class) + " box " +
                            track_box_class_name(answer.instance_class) +
                            " level " + std::to_string(answer.screen_level) +
                            " and this probe reproduces node " +
                            std::to_string(probe.node_index) + " " +
                            track_box_class_name(probe.node_class) + " box " +
                            track_box_class_name(probe.instance_class) +
                            " level " + std::to_string(probe.screen_level)});
                    ++pending_disagreements;
                }

                // Placement and materials.
                const std::size_t handle = track_scene_instance_of(scene, i, k);
                if (handle != kNoHandle) {
                    if (handle >= scene.instances.size()) {
                        error = "the scene reports instance handle " +
                                std::to_string(handle) + " for section " +
                                std::to_string(i) + " instance " +
                                std::to_string(k) + "; it holds " +
                                std::to_string(scene.instances.size());
                        return false;
                    }
                    const PreparedTrackInstance& placed = scene.instances[handle];
                    if (placed.mesh >= scene.region.meshes.size()) {
                        error = "the scene places section " + std::to_string(i) +
                                " instance " + std::to_string(k) + " on mesh " +
                                std::to_string(placed.mesh) +
                                "; the region holds " +
                                std::to_string(scene.region.meshes.size());
                        return false;
                    }
                    probe.placed = true;
                    probe.scene_instance = handle;
                    probe.mesh = placed.mesh;
                    probe.slot = placed.slot;
                    probe.requested_slot = placed.requested_slot;
                    probe.slot_fallback_used = placed.slot_fallback_used;
                    probe.mesh_hash = placed.mesh_hash;
                    const PreparedTrackMesh& mesh =
                        scene.region.meshes[placed.mesh];
                    probe.mesh_name = mesh.name;
                    probe.source_mesh_index = mesh.source_index;
                    probe.mesh_hash_record_offset = mesh.hash_record_offset;
                    probe.has_mesh_hash_record = mesh.has_hash_record;
                    if (mesh.source_index < geometry.meshes.size()) {
                        const TrackMesh& source =
                            geometry.meshes[mesh.source_index];
                        probe.mesh_container_offset = source.offset;
                        probe.mesh_record_offset =
                            source.mesh_metadata_record_offset;
                    } else {
                        pending.push_back(TrackIssue{
                            0, "prepared mesh '" + mesh.name +
                                   "' names source mesh " +
                                   std::to_string(mesh.source_index) +
                                   ", which the geometry report does not "
                                   "hold"});
                    }

                    probe.first_draw = mesh_first_draw[placed.mesh];
                    probe.draw_count = mesh_draw_count[placed.mesh];
                    for (std::size_t d = 0; d < probe.draw_count; ++d) {
                        const TrackDraw& draw =
                            scene.region.draws[probe.first_draw + d];
                        if (!draw.drawable) {
                            continue;
                        }
                        ++probe.groups_drawable;
                        probe.triangles += draw.index_count / 3;
                        switch (draw.binding) {
                            case BindingKind::resolved:
                                ++probe.groups_resolved;
                                break;
                            case BindingKind::unresolved:
                                ++probe.groups_unresolved;
                                break;
                            case BindingKind::unusable:
                                ++probe.groups_unusable;
                                break;
                            case BindingKind::invalid_slot:
                                ++probe.groups_invalid_slot;
                                break;
                        }
                        // The drawn box, over the same vertices a draw list of
                        // this one instance would transform, through the one
                        // placement rule track_placed_vertex states.
                        const std::uint64_t first = draw.first_index;
                        const std::uint64_t count = draw.index_count;
                        for (std::uint64_t t = first / 3;
                             t < (first + count) / 3; ++t) {
                            if (t >= mesh.triangles.size()) {
                                break;
                            }
                            const MeshTriangle& triangle =
                                mesh.triangles[static_cast<std::size_t>(t)];
                            const std::uint16_t indices[3] = {
                                triangle.a, triangle.b, triangle.c};
                            if (indices[0] >= mesh.vertices.size() ||
                                indices[1] >= mesh.vertices.size() ||
                                indices[2] >= mesh.vertices.size()) {
                                continue;
                            }
                            for (int c = 0; c < 3; ++c) {
                                extend(probe.drawn_world,
                                       track_placed_vertex(
                                           mesh.vertices[indices[c]],
                                           placed.transform));
                            }
                        }
                    }
                } else {
                    for (const TrackUnplacedInstance& unplaced :
                         scene.unplaced) {
                        if (unplaced.section_index == i &&
                            unplaced.instance_index == k) {
                            probe.unplaced_reason = unplaced.reason;
                            probe.has_unplaced_reason = true;
                            break;
                        }
                    }
                }

                // The mesh filter is the last thing applied, so that an
                // instance it drops contributes to no count. Every counter
                // below is over the probed instances and nothing else.
                if (!select.mesh_name.empty() &&
                    (!probe.placed || probe.mesh_name != select.mesh_name)) {
                    continue;
                }
                if (report.cost.probed >= options.limits.max_instances) {
                    error = "the selection covers more than " +
                            std::to_string(options.limits.max_instances) +
                            " instances";
                    return false;
                }

                for (TrackIssue& held : pending) {
                    report.issues.push_back(std::move(held));
                }
                report.cost.disagreements += pending_disagreements;
                ++record.probed;
                ++report.cost.probed;
                if (probe.visible) {
                    ++record.visible;
                    ++report.cost.visible;
                }
                if (probe.culler_visible) {
                    ++record.culler_visible;
                    ++report.cost.culler_visible;
                }
                if (probe.pruned_by_node != kTrackProbeNone) {
                    ++record.pruned;
                    ++report.cost.pruned;
                }
                if (probe.mask_gate == TrackGateVerdict::rejected) {
                    ++record.rejected_by_mask;
                    ++report.cost.rejected_by_mask;
                }
                if (probe.box_gate == TrackGateVerdict::rejected) {
                    ++record.rejected_by_box;
                    ++report.cost.rejected_by_box;
                }
                if (probe.size_gate == TrackGateVerdict::rejected) {
                    ++record.rejected_by_size;
                    ++report.cost.rejected_by_size;
                }
                if (probe.placed) {
                    ++record.placed;
                    ++report.cost.placed;
                    report.cost.groups += probe.groups_drawable;
                    report.cost.triangles += probe.triangles;
                } else {
                    ++report.cost.unplaced;
                }
                report.instances.push_back(std::move(probe));
            }
            report.sections.push_back(record);
        }
        return true;
    } catch (const std::bad_alloc&) {
        report = TrackProbeReport{};
        error = "not enough memory to probe the region";
        return false;
    }
}

} // namespace nfsu
