#pragma once

#include "nfsu/track_render.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Inspection of one bounded track region: where every placed instance came
// from, what places it, what it draws, and WHY it is or is not on screen.
//
// This header decodes nothing and renders nothing. Every byte it reports was
// decoded by docs/TRACK.md, docs/TRACK_OBJECTS.md, docs/TRACK_ZONES.md or
// docs/TRACK_VISIBILITY.md, and every placement it reports was composed by
// docs/TRACK_DRAW_LIST.md. What it adds is the one thing none of those
// prints: the provenance of a visibility decision, instance by instance.
// See docs/TRACK_PROBE.md.
//
// WHAT "PROVENANCE" MEANS HERE, PRECISELY
//
// nfsu::cull_track_region answers one question - which instances are visible
// - and stops at the first gate that rejects one. A caller reading its report
// can see THAT an instance is missing and cannot see why. This header answers
// the why, for every instance of every section, in four parts:
//
//   * residency. Whether the section is drawn at all, and under which arm of
//     the rule: every_section, the no-zone modulo rule, the always-resident
//     2600..2699 band, or a named position in a zone's own section list.
//   * the bounding tree. Which leaf node names the instance, how deep it
//     sits, and - when the instance was never reached - which ANCESTOR node
//     classified outside and pruned it.
//   * the gates. What the culler actually did at each of the three gates
//     (the flags mask, the box test and the screen-size level), as pass,
//     reject or not-applied, beside the measurement each gate is made of.
//     A gate the culler never reached still reports its measurement, so
//     "this instance was pruned at node 3 and its own box is inside the
//     frustum" is a sentence this report can make.
//   * the answer, twice. TrackInstanceProbe::visible is what this header
//     concludes from the four parts above; ::culler_visible is what
//     cull_track_region said. probe_track_scene compares them for every
//     instance and raises an issue where they differ, so the provenance
//     cannot drift away from the decision it claims to explain.
//
// WHAT THIS IS NOT
//
//   * It is NOT a second culler. It reproduces the traversal in order to
//     explain it, and it takes the residency answer, the policy actually
//     used and the degradation from the TrackCullReport rather than deciding
//     any of them again. Where it does recompute something - the carried
//     node code, the three gates - it reports both answers and says when
//     they disagree.
//   * It is NOT the retail rule for what the game draws. Every caveat
//     docs/TRACK_DRAW_LIST.md states still holds: the mesh slot is the
//     caller's, the projection scale behind the screen-size gate was not
//     recovered, and which sections are resident together is undecoded.

// ---------------------------------------------------------------------
// The free camera
// ---------------------------------------------------------------------

// A camera stated by position and look point rather than by an orbit, in the
// renderer world frame of docs/FORMATS.md:286, with y up.
//
// WHY NOT OrbitCamera. An orbit camera cannot put the eye somewhere and look
// elsewhere: its eye is a function of its target. That is the right model for
// a car, which sits in a box and is looked AT. A track region is a place, and
// the two questions a probe is asked about one - "what does the camera at the
// end of this street see" and "why is that tower missing" - both need an eye
// and a direction that are independent. So this is six degrees of freedom and
// a lens, and OrbitCamera is where it comes FROM: the default below is the
// framing docs/TRACK_RENDER.md fits, read out as an eye and a target.
//
// `width` and `height` are the frame the camera is stated for. NOTHING IS
// RENDERED here; they are the aspect ratio and the framing input, and they
// are a pixel pair rather than a ratio so that the framing below is the same
// call frame_track_bounds makes for a rendered frame of that size.
struct TrackFreeCamera {
    Vec3 eye{0.0, 0.0, 0.0};
    Vec3 target{0.0, 0.0, -1.0};
    // The reference up. The camera's own up axis is derived from it and the
    // view direction; this vector need only be non-zero and not parallel to
    // that direction.
    Vec3 up{0.0, 1.0, 0.0};
    double fov_y_deg = 35.0;
    double near_plane = 1.0;
    double far_plane = 2000.0;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
};

// The derived orthonormal basis, right-handed like CameraFrame's: forward
// runs from the eye to the target, right is forward x up, and up_axis is
// right x forward. `distance` is the eye-to-target distance, which the turn
// below preserves.
struct TrackCameraBasis {
    Vec3 forward{0.0, 0.0, -1.0};
    Vec3 right{1.0, 0.0, 0.0};
    Vec3 up_axis{0.0, 1.0, 0.0};
    double distance = 1.0;
};

// The two free moves, applied in the order they are declared and each at
// most once. A caller that names one twice is making the answer depend on
// the command line's order, which is the one thing this header refuses to
// let a selection do.
//
//   turn  rotates the view direction about the EYE: yaw about the reference
//         up, then pitch about the right axis the yaw produced. The eye does
//         not move and the eye-to-target distance is preserved. This is the
//         move an orbit cannot make.
//   fly   translates the eye AND the target together along the camera basis:
//         forward, right and up_axis. Dolly, truck and pedestal in one.
//
// Turn before fly, deliberately: `fly` is stated in the camera basis, and
// after a turn the basis is the turned one, so "forward" means along the
// direction the camera is now pointing. The other order would make a turn
// silently redirect a translation that had already happened.
struct TrackCameraMoves {
    bool has_turn = false;
    double turn_yaw_deg = 0.0;
    double turn_pitch_deg = 0.0;
    bool has_fly = false;
    double fly_forward = 0.0;
    double fly_right = 0.0;
    double fly_up = 0.0;
};

// Bounds on the two moves. They are this header's own, not retail's: a turn
// wraps and a fly does not, so the fly is what needs a bound at all, and it
// is bounded by the largest camera position validate_track_free_camera
// accepts.
constexpr double kMaxTrackTurnDegrees = 36000.0;
constexpr double kMaxTrackFlyDistance = 20000.0;

// Every value finite and inside the ranges nfsu::validate_camera enforces -
// kMinFov, kMaxFov, kMinNear, kMaxNear, kMaxFar, kMaxTargetMagnitude,
// kMinDistance and kMaxDistance - plus a frame size in 1..8192 and an eye
// that is not the target. Those are the renderer's bounds rather than a
// second policy, so a camera this accepts is one track_frustum_look_at
// builds a frustum from.
constexpr std::uint32_t kMaxTrackProbeFrameSize = 8192;

bool validate_track_free_camera(
    const TrackFreeCamera& camera,
    std::string& error);

// Fails, with error set, for a camera validate_track_free_camera refuses, an
// eye at the target, or an up vector that is zero or parallel to the view
// direction.
bool track_camera_basis(
    const TrackFreeCamera& camera,
    TrackCameraBasis& basis,
    std::string& error);

// The default camera for a region: nfsu::frame_track_bounds over `bounds`,
// read out as an eye and a target with orbit_eye.
//
// It is the same view-independent sphere fit docs/TRACK_RENDER.md states, and
// it is reused rather than restated so that the two commands frame a region
// identically. The property that matters is the one that rule was chosen for:
// the distance is fitted to the box's bounding SPHERE, so it does not depend
// on the view direction and two runs at different angles are comparable.
//
// The bounds a caller passes here should be the region's own - the drawn
// vertices of every placed instance - and NOT the ones a camera selected.
// docs/TRACK_PROBE.md says why: the camera decides the visible set, so a
// default framed from the visible set would be a camera defined in terms of
// itself.
bool track_frame_free_camera(
    const TrackWorldBounds& bounds,
    const TrackFramingOptions& framing,
    std::uint32_t width,
    std::uint32_t height,
    TrackFreeCamera& camera,
    std::string& error);

// Applies `moves` to `camera` in the order declared above. No libm
// transcendental is used: det_sin_deg and det_cos_deg only, the same
// restriction the renderer and the framing keep, so a moved camera is
// reproducible wherever the reports are.
//
// Fails, with error set and `out` untouched, for an invalid input camera, a
// move that is not finite or is outside the bounds above, a turn that leaves
// the view direction parallel to the reference up, or a result
// validate_track_free_camera refuses.
bool track_move_free_camera(
    const TrackFreeCamera& camera,
    const TrackCameraMoves& moves,
    TrackFreeCamera& out,
    std::string& error);

// The same camera as the culler's TrackCamera, so that ONE camera drives the
// frustum the gates are evaluated against and the projection the report
// prints. nfsu_probe track-render keeps a cull camera and a render camera
// apart because they answer different questions; a probe has one question and
// one camera, and a second one would let the provenance describe a frustum
// the report never named.
//
// The conversion is a narrowing to float and a degrees-to-radians multiply.
// Fails for a camera validate_track_free_camera refuses.
bool track_cull_camera_of(
    const TrackFreeCamera& camera,
    TrackCamera& out,
    std::string& error);

// ---------------------------------------------------------------------
// Provenance vocabulary
// ---------------------------------------------------------------------

// Why a section is or is not resident, under the caller's TrackResidency.
// One value per arm of the rule, so that "resident" and "resident because"
// are never the same field.
enum class TrackResidencyReason {
    // TrackResidencyRule::every_section. Not a retail rule; the explicit
    // fallback nfsu/track_visibility.h documents.
    every_section,
    // TrackResidencyRule::no_zone, number modulo 100 below
    // kTrackNoZoneSubIndexLimit.
    sub_index_in_range,
    // The same rule, rejecting.
    sub_index_out_of_range,
    // TrackResidencyRule::zone_list, number in the 2600..2699 band retail
    // adds unconditionally at 0x00446473.
    always_resident_band,
    // TrackResidencyRule::zone_list, named by the zone's own list;
    // TrackInstanceProbe::zone_position is where in it.
    zone_list_entry,
    // The same rule, rejecting: the list does not name this number.
    not_in_zone_list,
};

// "every-section", "sub-index-in-range", "sub-index-out-of-range",
// "always-resident-band", "zone-list-entry" or "not-in-zone-list". Stable:
// tests compare against it.
const char* track_residency_reason_name(TrackResidencyReason reason) noexcept;

// What one gate did to one instance, in this run.
enum class TrackGateVerdict {
    // The gate ran and the instance survived it.
    passed,
    // The gate ran and rejected the instance.
    rejected,
    // The gate did not run. Under TrackCullPolicy::draw_everything no gate
    // runs at all; the box test is skipped for an instance whose node was
    // already inside, exactly as retail skips it; the screen-size gate is off
    // unless TrackCullOptions::use_screen_size is set; and a pruned instance
    // reaches no gate at all.
    not_applied,
};

// "passed", "rejected" or "not-applied". Stable: tests compare against it.
const char* track_gate_verdict_name(TrackGateVerdict verdict) noexcept;

// The value a node or list index carries when there is none.
constexpr std::size_t kTrackProbeNone = static_cast<std::size_t>(-1);

// ---------------------------------------------------------------------
// One probed instance
// ---------------------------------------------------------------------

struct TrackInstanceProbe {
    // ---- where it came from. Every offset is an absolute FILE offset. ----
    std::size_t section_index = 0;
    std::size_t instance_index = 0;
    std::uint32_t section_number = 0;
    char section_letter = '\0';
    bool has_section_letter = false;
    // The 0x80034100 chunk header, the 48-byte instance record, and the
    // 72-byte object record the instance names.
    std::uint64_t section_offset = 0;
    std::uint64_t instance_offset = 0;
    std::uint64_t object_offset = 0;
    std::size_t object_index = 0;

    // ---- hashes and raw fields ----
    // The object's six mesh slots, as stored.
    std::uint32_t object_mesh_hash[kSceneryObjectMeshSlots] = {0, 0, 0, 0, 0, 0};
    std::uint32_t object_flags = 0;
    std::uint32_t object_flags2 = 0;
    // Object record +68, without kTrackRadiusBias; the gate below adds it.
    float object_radius = 0.0F;
    // Instance record +14, kept raw.
    std::uint32_t instance_flags = 0;

    // ---- the transform, and the two boxes ----
    TrackTransform transform;
    // The stored int16 box, in the raw slot order.
    std::int16_t stored_min[3] = {0, 0, 0};
    std::int16_t stored_max[3] = {0, 0, 0};
    // The same box in the renderer world frame, and the box of the vertices
    // this instance actually draws. `drawn` is valid only when the instance
    // placed a mesh with at least one drawable group; the two are reported
    // side by side for the reason docs/TRACK_OBJECTS.md gives - the stored
    // box fails to bound its own mesh for 327 of 29674 retail instances, so
    // it is reported and never trusted.
    TrackWorldBounds stored_world;
    TrackWorldBounds drawn_world;

    // ---- visibility provenance ----
    bool section_resident = false;
    TrackResidencyReason residency_reason = TrackResidencyReason::every_section;
    // Position in TrackResidency::zone_sections, for zone_list_entry only.
    std::size_t zone_position = kTrackProbeNone;
    // What the section was actually culled with, copied from
    // TrackSectionCull::policy_used, so a degraded section says so here.
    TrackCullPolicy policy_used = TrackCullPolicy::retail_tree;

    // The leaf node whose entry names this instance, and its depth with the
    // root at 0. kTrackProbeNone under a policy that walks no tree.
    std::size_t node_index = kTrackProbeNone;
    std::uint32_t node_depth = 0;
    // The ancestor node - possibly the naming node itself - that classified
    // outside and stopped the traversal before it reached this instance.
    // kTrackProbeNone when the traversal did reach it.
    std::size_t pruned_by_node = kTrackProbeNone;
    // The code the traversal carried into the instance, which is what decides
    // whether the box gate runs. Meaningful only when pruned_by_node is
    // kTrackProbeNone.
    TrackBoxClass node_class = TrackBoxClass::inside;
    // What the instance's own box tested as INSIDE the traversal, which is
    // TrackBoxClass::inside when the node made the test unnecessary.
    TrackBoxClass instance_class = TrackBoxClass::inside;

    // The three gates, as this run applied them.
    TrackGateVerdict mask_gate = TrackGateVerdict::not_applied;
    TrackGateVerdict box_gate = TrackGateVerdict::not_applied;
    TrackGateVerdict size_gate = TrackGateVerdict::not_applied;
    // The measurements the gates are made of, computed for EVERY probed
    // instance whether or not its gate ran. That is what lets this report say
    // "pruned at node 3, and its own box is inside the frustum".
    //
    //   mask_bits   (instance_flags ^ kTrackInstanceMaskBias) & mask, the
    //               8-bit test at 0x00445EDA. Non-zero means the mask gate
    //               would reject.
    //   own_box_class  the instance box against the frustum, unconditionally.
    //   screen_level   track_screen_size_level for this instance, or 0 when
    //               TrackCullOptions::use_screen_size is false. Below
    //               kTrackMinDrawLevel the screen gate would reject.
    std::uint32_t mask_bits = 0;
    TrackBoxClass own_box_class = TrackBoxClass::inside;
    int screen_level = 0;
    bool screen_level_measured = false;

    // This header's conclusion, and the culler's. probe_track_scene raises an
    // issue wherever they differ.
    bool visible = false;
    bool culler_visible = false;

    // ---- placement and materials ----
    bool placed = false;
    // Index into PreparedTrackScene::instances, or kNoHandle.
    std::size_t scene_instance = kNoHandle;
    // Index into PreparedTrackScene::region.meshes, or kNoHandle.
    std::size_t mesh = kNoHandle;
    std::string mesh_name;
    std::uint32_t mesh_hash = 0;
    // The source mesh's provenance, out of the geometry report: its
    // 0x80134010 container, its 176-byte 0x00134011 record, and the
    // 0x00134003 hash-table record it was matched to.
    std::size_t source_mesh_index = kNoHandle;
    std::uint64_t mesh_container_offset = 0;
    std::uint64_t mesh_record_offset = 0;
    std::uint64_t mesh_hash_record_offset = 0;
    bool has_mesh_hash_record = false;
    // Which object slot placed it, and whether the slots 4 and 5 fallback
    // got it there.
    std::size_t slot = 0;
    std::size_t requested_slot = 0;
    bool slot_fallback_used = false;
    // Why nothing was placed. Meaningful only when placed is false and
    // has_unplaced_reason is true; an instance of a section the scene does
    // not hold has neither.
    TrackUnplacedReason unplaced_reason = TrackUnplacedReason::empty_slot;
    bool has_unplaced_reason = false;

    // The prepared material groups of the placed mesh: `draw_count`
    // consecutive entries of PreparedTrackScene::region.draws starting at
    // `first_draw`. They are consecutive by construction - a prepared region
    // holds one ordered draw per material record in mesh order - and
    // probe_track_scene checks it rather than assuming it.
    std::size_t first_draw = kNoHandle;
    std::size_t draw_count = 0;
    std::uint64_t groups_drawable = 0;
    std::uint64_t groups_resolved = 0;
    std::uint64_t groups_unresolved = 0;
    std::uint64_t groups_unusable = 0;
    std::uint64_t groups_invalid_slot = 0;
    // Triangles the drawable groups issue, which is what this instance
    // contributes to a draw list.
    std::uint64_t triangles = 0;
};

// ---------------------------------------------------------------------
// One probed section
// ---------------------------------------------------------------------

struct TrackSectionProbe {
    std::size_t section_index = 0;
    std::uint32_t number = 0;
    char letter = '\0';
    bool has_letter = false;
    std::uint64_t offset = 0;
    bool valid = false;
    bool resident = false;
    TrackResidencyReason residency_reason = TrackResidencyReason::every_section;
    std::size_t zone_position = kTrackProbeNone;
    TrackCullPolicy policy_used = TrackCullPolicy::retail_tree;
    bool containment_checked = false;
    bool containment_holds = true;
    // Whether this header could walk the bounding tree. False under a policy
    // that walks none, and false with an issue when the culler walked one and
    // this walk could not.
    bool tree_walked = false;
    std::uint64_t nodes = 0;
    std::uint32_t tree_depth = 0;
    // Instances the section holds, and how many of them the selection kept.
    std::uint64_t instances = 0;
    std::uint64_t probed = 0;
    // Over the probed instances only.
    std::uint64_t visible = 0;
    std::uint64_t culler_visible = 0;
    std::uint64_t rejected_by_mask = 0;
    std::uint64_t rejected_by_box = 0;
    std::uint64_t rejected_by_size = 0;
    std::uint64_t pruned = 0;
    std::uint64_t placed = 0;
};

// ---------------------------------------------------------------------
// Selection, limits, options and the report
// ---------------------------------------------------------------------

// Which instances to probe. Every field is explicit and every one narrows:
// the default probes every instance of every valid section.
//
// A selection never reorders anything. The report is always in ascending
// (section_index, instance_index), so two runs whose selections differ only
// in the order they were written produce the same records in the same order.
struct TrackProbeSelection {
    bool has_section = false;
    std::size_t section_index = 0;
    // Source instance indices inside the selected section. Empty selects
    // every instance. probe_track_scene refuses a repeated entry and an entry
    // outside the section rather than silently dropping it, because a
    // selection that quietly matches nothing is indistinguishable from one
    // that matched and found nothing visible.
    //
    // Reading these against ONE section is why `has_section` is required when
    // this list is non-empty: an instance index means nothing on its own.
    std::vector<std::size_t> instances;
    // Keep only instances that placed the prepared mesh of this name. Empty
    // keeps every instance. An exact, case-sensitive comparison against
    // PreparedTrackMesh::name; a name no prepared mesh carries is refused.
    std::string mesh_name;
};

struct TrackProbeLimits {
    std::uint64_t max_instances = 1ULL << 20;
};

struct TrackProbeOptions {
    // The SAME options cull_track_region was given. The gates are evaluated
    // with the culler's own inputs, including the screen-size scale and the
    // mask byte, so that a verdict here describes the run the report came
    // from. probe_track_scene normalizes screen_forward exactly as
    // cull_track_region does, for the same reason: otherwise the gate's
    // answer would depend on how long the caller's direction vector was.
    TrackCullOptions cull;
    TrackProbeSelection select;
    TrackProbeLimits limits;
};

struct TrackProbeCost {
    std::uint64_t sections = 0;
    std::uint64_t valid_sections = 0;
    std::uint64_t resident_sections = 0;
    // Instances of the valid sections, and how many the selection kept.
    std::uint64_t instances = 0;
    std::uint64_t probed = 0;
    // Over the probed instances only.
    std::uint64_t visible = 0;
    std::uint64_t culler_visible = 0;
    // Probed instances where `visible` and `culler_visible` differ, or where
    // the node, the box class or the screen level this header computed
    // differs from the one the culler recorded. Every one of them is also an
    // issue; this is the count.
    std::uint64_t disagreements = 0;
    std::uint64_t placed = 0;
    std::uint64_t unplaced = 0;
    std::uint64_t rejected_by_mask = 0;
    std::uint64_t rejected_by_box = 0;
    std::uint64_t rejected_by_size = 0;
    std::uint64_t pruned = 0;
    // Drawable groups and triangles over the probed, placed instances.
    std::uint64_t groups = 0;
    std::uint64_t triangles = 0;
};

struct TrackProbeReport {
    // One entry per source section, valid or not, ascending.
    std::vector<TrackSectionProbe> sections;
    // Ascending (section_index, instance_index).
    std::vector<TrackInstanceProbe> instances;
    std::vector<TrackIssue> issues;
    TrackProbeCost cost;

    bool ok() const noexcept { return issues.empty(); }
};

// Probes one region.
//
// `geometry`, `objects` and `textures` must be the reports `scene` was built
// from, `cull` must be the report cull_track_region produced for `objects`
// and `frustum` under `options.cull`, and `frustum` must be the frustum that
// run used. Those are cross-checked where they can be: a cull report whose
// sections do not line up with `objects`, or a scene whose object region is
// not the one `objects` decodes, is an error rather than a report about the
// wrong thing.
//
// Fails, with error set and `report` left empty, on such a mismatch, on a
// selection that names a section, an instance or a mesh name that does not
// exist, on a repeated instance in the selection, on a limit, on an
// allocation failure or on a failed internal check; std::bad_alloc is caught.
//
// A DISAGREEMENT IS NOT A FAILURE. When this header and the culler reach
// different answers the report is still produced, with the difference in
// TrackProbeCost::disagreements and one issue naming the instance, because a
// caller needs to see both answers to know which to distrust.
bool probe_track_scene(
    const TrackRegionReport& geometry,
    const TrackObjectReport& objects,
    const PreparedTrackScene& scene,
    const TrackCullReport& cull,
    const TrackFrustum& frustum,
    const TrackProbeOptions& options,
    TrackProbeReport& report,
    std::string& error);

} // namespace nfsu
