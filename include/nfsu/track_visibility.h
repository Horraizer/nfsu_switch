#pragma once

#include "nfsu/track_objects.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Which scenery instances a camera can see, and why.
//
// docs/TRACK.md decodes the geometry a section draws and
// docs/TRACK_OBJECTS.md decodes where that geometry goes. This header
// decides what is drawn from a camera position. The rule below was read out
// of speed.exe, not inferred from the data: 0x00446140 walks the
// 0x00034104 bounding tree, 0x0040DB20 is the box test it calls, and
// 0x00445EC0 is what it hands each instance to. Every address in this file
// is a speed.exe virtual address; docs/TRACK_VISIBILITY.md carries the
// disassembly each one stands for, and says which parts of the retail rule
// could not be recovered and what this header does instead.
//
// Nothing here reads a file. A caller decodes a region with
// parse_track_objects and passes the report in.

// 0x0040DC38 `cmp edx, 6` with edx starting at 1 and the plane pointer
// advancing by 0x10 each turn: six planes of four floats.
constexpr std::size_t kTrackFrustumPlanes = 6;

// 0x00445EE6 `xor eax, 0x60` on the instance flags word before the render
// context's mask byte is applied to its low byte.
constexpr std::uint16_t kTrackInstanceMaskBias = 0x0060U;

// 0x00445F3B `fadd dword ptr [esi + 0x44]`, that is object record +68, onto
// the constant at 0x006B780C, which is 4.0. The sum is the radius the
// screen-size gate uses.
constexpr float kTrackRadiusBias = 4.0F;

// 0x00445F4E `cmp eax, 1` / `jle`: an instance whose screen-size level is 1
// or less is not drawn, so 2 is the lowest level that draws.
constexpr int kTrackMinDrawLevel = 2;

// 0x00446473 `mov eax, 0xa28` and 0x00446497 `cmp eax, 0xa8c`: when a
// visibility zone is active the section numbers 2600 to 2699 are added to
// the drawn set unconditionally. Those are the letter 'Z' sections, which
// the loader at 0x00445830 also links at the head of the section list
// rather than the tail.
constexpr std::uint32_t kTrackAlwaysResidentFirst = 2600;
constexpr std::uint32_t kTrackAlwaysResidentLast = 2699;

// 0x00446459 `cmp edx, 0xa` after a division of the section number by 100:
// with no zone active, a loaded section is drawn when its number modulo 100
// is below 10.
constexpr std::uint32_t kTrackNoZoneSubIndexLimit = 10;

// 0x00446AB4 `cmp dword ptr [eax], 0x80034150`: the chunk id of the
// visibility-zone container, which builds the polygon list at 0x0077A930.
// No file of the sampled installation carries this chunk at its top level;
// see docs/TRACK_VISIBILITY.md for where the bytes are and why they are out
// of reach here.
constexpr std::uint32_t kTrackVisibilityZoneId = 0x80034150U;

// One plane of the retail frustum, in the raw file slot order: a point p is
// on the inside when normal[0] * p[0] + normal[1] * p[1] + normal[2] * p[2]
// + d is at or above zero. 0x0040DBD4 reads the three components at plane
// +0, +4 and +8 and the constant at +12; 0x0040DC11 and 0x0040DC20 compare
// against 0.0 at 0x006CC7A4.
struct TrackPlane {
    float normal[3] = {0.0F, 0.0F, 0.0F};
    float d = 0.0F;
};

// The six planes speed.exe reads from the render context. Retail never
// builds this itself at the point the tree is walked; it is handed the
// planes. track_frustum_look_at below is this project's construction, not a
// recovered one.
struct TrackFrustum {
    TrackPlane planes[kTrackFrustumPlanes];
};

// What 0x0040DB20 returns. 0x0040DC4E returns 0, and 0x0040DC3D computes
// `(esi == 0) ? 2 : 1`, where esi was set when some plane left the box only
// partly inside.
enum class TrackBoxClass {
    // Some plane rejects the whole box. The subtree is skipped.
    outside = 0,
    // The box crosses at least one plane. Children must be tested too.
    intersecting = 1,
    // The box is inside all six planes. 0x0044617D and 0x00445EFE both skip
    // the test when the code they carry is this one.
    inside = 2,
};

// The retail box test, 0x0040DB20, in its own arithmetic order: the centre
// is (min + max) * 0.5 with the 0.5 at 0x006CC7DC, the extent is
// max - centre, and for each plane D = dot(normal, centre) + d and
// r = |normal| . extent. D + r below zero is outside; D - r below zero on
// any plane leaves the box intersecting; otherwise it is inside.
//
// The walker calls this with the transform argument zero (0x004461E0
// `xor eax, eax`), so the box is used in world space with no transform, and
// this function reproduces only that path.
TrackBoxClass track_classify_box(
    const TrackFrustum& frustum,
    const float bounds_min[3],
    const float bounds_max[3]) noexcept;

// The stored int16 box of one record, widened to float exactly as
// 0x00445520 does: each word is sign-extended and converted, with no
// inflation of any kind.
void track_box_to_float(
    const std::int16_t bounds_min[3],
    const std::int16_t bounds_max[3],
    float out_min[3],
    float out_max[3]) noexcept;

// docs/FORMATS.md:286. The renderer's world map over the file's raw slot
// order (px, pz, py): x = px, y = py, z = -pz. Both frames have y up. Every
// bound and every plane in this header is in the raw slot order, because
// that is the order the stored boxes use; TrackCamera below is the only
// thing stated in the world frame, and it says so.
void track_world_from_raw(const float raw[3], float world[3]) noexcept;
void track_raw_from_world(const float world[3], float raw[3]) noexcept;

// A camera, in the renderer world frame of docs/FORMATS.md:286, with y up.
// This is not a retail structure: retail reads six planes from its render
// context and this project has not recovered how they are built. It exists
// so a test can name a camera position and get a frustum that is a function
// of that position alone.
struct TrackCamera {
    float eye[3] = {0.0F, 0.0F, 0.0F};
    float target[3] = {0.0F, 0.0F, -1.0F};
    float up[3] = {0.0F, 1.0F, 0.0F};
    // Full vertical angle, in radians.
    float vertical_fov = 1.0471975512F;
    // Width over height.
    float aspect = 4.0F / 3.0F;
    float near_plane = 1.0F;
    float far_plane = 2000.0F;
};

// Builds a symmetric perspective frustum, with inward normals and unit-
// length normals, in the raw slot order. Returns false and leaves frustum
// untouched for a degenerate camera: a non-finite field, an eye at the
// target, an up vector that is zero or parallel to the view direction, a
// non-positive near plane, or a far plane at or below the near plane. The
// plane order is near, far, left, right, bottom, top, which is this
// project's choice: the test is order-independent.
bool track_frustum_look_at(const TrackCamera& camera, TrackFrustum& frustum) noexcept;

// 0x00445E10. The screen-size number the retail draw path computes for one
// instance, and 0x00445F4E drops the instance when it is 1 or less.
//
//   forward is the render context's axis at +0xB0..+0xB8 and eye its
//   position at +0xA0..+0xA8, both in the raw slot order;
//   radius is object record +68 plus kTrackRadiusBias;
//   scale is the render context's value at +0xC0.
//
// With d = |position - eye| - radius, the result is
// trunc(d <= radius ? scale : scale * radius / d), and 0 when
// dot(position - eye, forward) is at or below -radius, which is the
// behind-the-camera reject at 0x00445E58.
//
// scale is read from the render context and this project has not recovered
// it, so a caller must supply one; TrackCullOptions leaves the gate off.
int track_screen_size_level(
    const float eye[3],
    const float forward[3],
    float scale,
    const float position[3],
    float radius) noexcept;

// 0x00445EDA to 0x00445EEB: the low byte of (flags ^ 0x60) is ANDed with a
// byte of the render context and a non-zero result skips the instance.
inline bool track_instance_masked_out(
    std::uint32_t instance_flags,
    std::uint8_t mask) noexcept {
    const std::uint32_t biased = instance_flags ^ kTrackInstanceMaskBias;
    return ((biased & 0xFFU) & static_cast<std::uint32_t>(mask)) != 0U;
}

// 0x00446473: a section the drawn set always carries when a zone is active.
inline bool track_section_always_resident(std::uint32_t number) noexcept {
    return number >= kTrackAlwaysResidentFirst &&
           number <= kTrackAlwaysResidentLast;
}

// 0x00446459: the rule retail falls back on when the camera is inside no
// zone polygon.
inline bool track_section_resident_without_zone(std::uint32_t number) noexcept {
    return (number % 100U) < kTrackNoZoneSubIndexLimit;
}

// How a caller decides which sections of a region are drawn at all.
enum class TrackResidencyRule {
    // Every valid section of the region. Not a retail rule: the explicit
    // fallback for a caller that has no zone data, which is every caller
    // this project can serve today.
    every_section,
    // 0x0044642C to 0x00446471: what retail does when the camera is inside
    // no zone polygon. Sections whose number modulo 100 is 10 or above are
    // dropped.
    no_zone,
    // 0x00446473 to 0x004464D6: sections 2600 to 2699 first, then the
    // caller's zone list in its own order. A number the region does not
    // hold is skipped, exactly as a null entry of the global table at
    // 0x0072A008 is skipped.
    zone_list,
};

struct TrackResidency {
    TrackResidencyRule rule = TrackResidencyRule::every_section;
    // Read only under TrackResidencyRule::zone_list. Retail holds at most
    // 64 of these in one zone record (0x00446C10 `cmp edx, 0x40`).
    std::vector<std::uint32_t> zone_sections;
};

// Which visible set a caller wants.
enum class TrackCullPolicy {
    // The recovered rule: walk the bounding tree, prune a subtree whose
    // node box is outside, and stop testing under a node that is inside.
    retail_tree,
    // The explicit fallback. No frustum is consulted at all: every instance
    // of every resident section is visible, in section and instance order.
    // This is not what the game does, and a report produced under it says
    // so in TrackSectionCull::policy_used.
    draw_everything,
    // Test every instance box and never prune on a node. Exact for the
    // stored instance boxes however the tree is shaped, and what
    // retail_tree degrades into for a section whose tree fails containment.
    flat_instance_test,
};

struct TrackCullLimits {
    // Retail's traversal stack lives between [esp+0x40] and the top of a
    // 0x174-byte frame, which leaves room for 64 codes and 61 node
    // pointers. The exact retail cap is inferred from that frame; this one
    // is the project's own, and a traversal that needs more is refused with
    // an issue rather than allowed to grow without bound.
    std::size_t max_pending_nodes = 64;
};

struct TrackCullOptions {
    TrackCullPolicy policy = TrackCullPolicy::retail_tree;
    TrackResidency residency;
    TrackCullLimits limits;
    // The render context's mask byte. Zero, the default, masks nothing.
    // Retail's value was not recovered, so the default set is a superset of
    // the retail one on this gate.
    std::uint8_t instance_mask = 0;
    // The screen-size gate of 0x00445F34. Off by default because its scale
    // was not recovered; a set produced with it off is a superset of the
    // retail one on this gate too.
    bool use_screen_size = false;
    float screen_scale = 0.0F;
    // The eye and forward axis track_screen_size_level needs, in the raw
    // slot order. Read only when use_screen_size is true.
    float screen_eye[3] = {0.0F, 0.0F, 0.0F};
    float screen_forward[3] = {0.0F, 0.0F, 1.0F};
    // Check, before pruning on it, that every node box contains the box of
    // every entry below it. Retail assumes this and never checks; over all
    // 593 retail sections it holds for all 36710 parent-to-entry relations
    // with no exception, which is why retail can. A section that fails it
    // is culled with flat_instance_test instead and raises an issue.
    bool verify_containment = true;
};

// One visible instance, with the reason it survived.
struct TrackVisibleInstance {
    std::size_t section_index = 0;
    std::size_t instance_index = 0;
    // The leaf node whose entry named it. Meaningful only under
    // TrackCullPolicy::retail_tree; zero otherwise.
    std::size_t node_index = 0;
    // The code the traversal carried into the instance. Always
    // TrackBoxClass::inside under draw_everything, because no test ran.
    TrackBoxClass node_class = TrackBoxClass::inside;
    // What the instance box itself tested as. inside when the node was
    // already inside and the test was skipped, exactly as retail skips it.
    TrackBoxClass instance_class = TrackBoxClass::inside;
    // The screen-size level, when the gate ran; 0 when it did not.
    int screen_level = 0;
};

struct TrackCullStats {
    std::uint64_t sections_considered = 0;
    std::uint64_t sections_resident = 0;
    std::uint64_t sections_degraded = 0;
    std::uint64_t nodes_visited = 0;
    // Nodes whose box was actually handed to track_classify_box. Lower than
    // nodes_visited exactly when a parent tested inside.
    std::uint64_t node_tests = 0;
    std::uint64_t nodes_culled = 0;
    std::uint64_t instances_visited = 0;
    std::uint64_t instance_tests = 0;
    std::uint64_t instances_culled_by_mask = 0;
    std::uint64_t instances_culled_by_box = 0;
    std::uint64_t instances_culled_by_size = 0;
    std::uint64_t instances_visible = 0;
    // Instances the traversal never reached because a node above them was
    // outside. instances_visited + instances_pruned is the resident
    // instance count for every policy.
    std::uint64_t instances_pruned = 0;
};

// What one section contributed.
struct TrackSectionCull {
    std::size_t section_index = 0;
    std::uint32_t number = 0;
    char letter = '\0';
    bool has_letter = false;
    bool resident = false;
    // False when verify_containment found a node that does not contain an
    // entry below it, which is what forced the degradation.
    bool containment_holds = true;
    bool containment_checked = false;
    // What this section was actually culled with, which is the requested
    // policy unless it degraded.
    TrackCullPolicy policy_used = TrackCullPolicy::retail_tree;
    std::uint64_t instances = 0;
    std::uint64_t visible = 0;
};

struct TrackCullReport {
    // In section order, and inside a section in the order the retail
    // traversal emits them: a node's own instance entries in entry order,
    // then its children in reverse entry order, depth first through a
    // last-in-first-out stack (0x00446170 pops what 0x00446253 last
    // pushed).
    std::vector<TrackVisibleInstance> visible;
    std::vector<TrackSectionCull> sections;
    std::vector<TrackIssue> issues;
    TrackCullStats stats;
    TrackCullPolicy requested_policy = TrackCullPolicy::retail_tree;
    // True when every resident section was culled with the requested
    // policy. False when at least one degraded.
    bool policy_held = true;

    bool ok() const noexcept { return issues.empty(); }
};

// Checks that every node of the section contains the box of every entry it
// names, which is what the retail prune relies on. Returns the number of
// (node, entry, axis, side) overshoots, appending one issue per failing
// entry to issues when issues is not null.
std::size_t track_check_tree_containment(
    const TrackSection& section,
    std::vector<TrackIssue>* issues);

// The visible set of one already decoded region, for one frustum.
//
// A section that is not valid contributes nothing and is not listed as
// resident. The frustum is ignored entirely under
// TrackCullPolicy::draw_everything, which is what makes that policy the
// safe fallback rather than a degenerate frustum.
TrackCullReport cull_track_region(
    const TrackObjectReport& objects,
    const TrackFrustum& frustum,
    const TrackCullOptions& options = TrackCullOptions{});

// The name of a policy, for diagnostics. Stable: tests compare against it.
const char* track_cull_policy_name(TrackCullPolicy policy) noexcept;
const char* track_box_class_name(TrackBoxClass value) noexcept;

} // namespace nfsu
