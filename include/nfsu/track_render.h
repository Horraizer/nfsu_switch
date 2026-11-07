#pragma once

#include "nfsu/render.h"
#include "nfsu/track_draw_list.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nfsu {

// One bounded track region, drawn with the deterministic CPU reference
// renderer of include/nfsu/render.h.
//
// This header decodes nothing and selects nothing. It reads a
// PreparedTrackScene and a TrackDrawList and draws exactly the entries the
// list holds, in the order the list holds them. That is issue #40's
// boundary and it is not reopened here: visibility selection stays above
// every backend, so nothing below calls the culler, builds a frustum or
// reads a residency rule. See docs/TRACK_RENDER.md.
//
// WHAT THIS IS A BACKEND OF, AND WHAT IT IS NOT
//
//   * It is a second consumer of TrackDrawList, beside whatever GPU backend
//     comes later. It exists to make the CPU reference renderer able to
//     draw a track region at all, and to give a later backend a frame to
//     be identical to.
//   * It is NOT a change to the car path. src/render.cpp is not modified by
//     the commit that adds this file: render_scene, the Rasterizer, the
//     camera, the clipper and the frame writer are used exactly as the car
//     path uses them, through the public declarations of nfsu/render.h. The
//     car path's reference frames are therefore byte-identical by
//     construction and not only by measurement; docs/RENDERER.md's five
//     reference hashes were re-measured anyway.
//
// THE PLACEMENT, AND WHY IT IS SPELLED OUT AGAIN
//
// Three coordinate conventions meet here and two of them have already cost
// this project a wrong picture that looked plausible:
//
//   the file's raw slot order, which for a TrackVertex is (px, pz, py);
//   the renderer's world frame, docs/FORMATS.md:286, x = px, y = py,
//     z = -pz, which is Y-up;
//   the instance transform, TrackTransform, which is row-major over the
//     raw slot order and whose row 3 is the translation.
//
// track_placed_vertex composes them in that order and in no other:
//
//   slot  = (v.px, v.pz, v.py)
//   moved = slot * linear + translation      (row vector, row-major linear)
//   world = (moved[0], moved[2], -moved[1])
//
// which is the same shape placed_vertex uses for a car part, with
// TrackTransform's 3x3 and translation in place of the car's 4x4 affine
// part. The second half is exactly track_world_from_raw of
// nfsu/track_visibility.h, and a test pins that the two agree rather than
// leaving one map written twice.

// The renderer world frame axis-aligned bounds of something drawn.
struct TrackWorldBounds {
    // False when nothing contributed a point. min and max are then zero and
    // mean nothing.
    bool valid = false;
    double min[3] = {0.0, 0.0, 0.0};
    double max[3] = {0.0, 0.0, 0.0};
    // Points that contributed.
    std::uint64_t points = 0;
};

// The bounds a draw list covers, measured two independent ways.
//
// Reading both is the point. `drawn` is the truth about the frame: it is
// the vertices the renderer will actually transform and project. `stored`
// is the union of the same instances' stored int16 boxes, widened by
// track_box_to_float, which is what the culler tests and what
// TrackScenePlacementCost reports - and which docs/TRACK_OBJECTS.md
// measured as failing to bound its own mesh for 327 of 29674 retail
// instances. A caller frames on `drawn` and reports the disagreement.
struct TrackDrawListBounds {
    TrackWorldBounds drawn;
    TrackWorldBounds stored;
    // Instances the two were measured over. `instances` is the distinct
    // instances the list names; `instances_without_vertices` is how many of
    // them contributed no point to `drawn`, which happens when every group
    // the list names for that instance has an empty index range.
    std::uint64_t instances = 0;
    std::uint64_t instances_without_vertices = 0;
};

// Where one track vertex lands in the renderer world frame, for one placed
// instance. See THE PLACEMENT above.
Vec3 track_placed_vertex(
    const TrackVertex& vertex,
    const TrackTransform& transform) noexcept;

// Measures both bounds of what `list` draws out of `scene`.
//
// The scene and the list are read as given; validate_prepared_track_scene
// and validate_track_draw_list belong before this, exactly as they do
// before render_track_draw_list, and this function runs neither. A list
// whose entries name handles outside the scene is an error here, not a
// silently smaller box.
//
// `drawn` visits every vertex of every triangle of every entry's index
// range, so a vertex three entries share is visited three times; that
// changes no bound and `points` counts the visits, not the vertices.
bool track_draw_list_bounds(
    const PreparedTrackScene& scene,
    const TrackDrawList& list,
    TrackDrawListBounds& bounds,
    std::string& error);

// How the default camera frames a region.
//
// A car sits at the origin and fits in a box a fixed distance works for. A
// track region does not: the retail region docs/TRACK_RENDER.md measures is
// 429 by 160 by 139 world units across and sits 650 units from the origin,
// so the car defaults - distance 7, target 0.2,0.35,0 - render it entirely
// outside the frustum and produce a frame of nothing but background. This
// is the rule that replaces them, and it needs no flags to produce a
// picture.
struct TrackFramingOptions {
    // Orbit angles, in the OrbitCamera convention of nfsu/render.h: yaw 0
    // looks from +X, yaw 90 from +Z, pitch above 0 looks from above.
    double yaw_deg = 35.0;
    double pitch_deg = 30.0;
    // Full vertical field of view, the same default OrbitCamera carries.
    double fov_y_deg = 35.0;
    // The bounding sphere is fitted and then the distance is multiplied by
    // this, so 1.05 leaves a 5 percent margin. At or below 1 the sphere
    // touches the frame edge and any rounding puts a vertex outside it.
    double margin = 1.05;
};

constexpr double kMinTrackFramingMargin = 1.0;
constexpr double kMaxTrackFramingMargin = 100.0;

// Builds an OrbitCamera that contains `bounds` in the frame, for an output
// of `width` by `height`.
//
// THE RULE, IN FULL. The target is the centre of the box. The distance is
// fitted to the box's BOUNDING SPHERE, radius = half the box diagonal:
//
//   ty = tan(fov_y / 2), tx = ty * width / height
//   s  = min(tx / sqrt(1 + tx * tx), ty / sqrt(1 + ty * ty))
//   distance = radius / s * margin
//
// s is the sine of the smaller of the two half-angles, so distance * s is
// the radius of the LARGEST SPHERE the cone admits at that distance, and
// the box's bounding sphere therefore fits in both axes. It is not the
// half-extent at the target plane, which is distance * tan of the same
// angle and is larger; the formula here is the stricter of the two, which
// is the one a containment claim needs. The sphere and not the box, deliberately:
// a box fit depends on the view direction, so the framing would change
// whenever yaw or pitch did and two runs at different angles could not be
// compared. The sphere fit is view-independent and always contains the
// box, at the cost of standing farther back than a box fit would. That
// cost is bounded and small: the sphere's radius is the box's half
// diagonal, so the ratio to the largest half-extent is at most sqrt(3),
// and for the retail region docs/TRACK_RENDER.md measures it is
// 239.07 / 214.33 = 1.115, eleven and a half percent.
//
// The near plane is (distance - radius) / 2, clamped into
// [kMinNear, kMaxNear], and the far plane is (distance + radius) * 1.25,
// clamped to kMaxFar; distance itself is clamped into
// [kMinDistance, kMaxDistance] and the target into +-kMaxTargetMagnitude.
// Every clamp is a validate_camera bound, so the camera this returns always
// passes validate_camera, and this function says so by running it.
//
// No libm transcendental is used: det_sin_deg, det_cos_deg and det_sqrt only.
// The shared square-root helper keeps this camera on the renderer's
// single-rounded path across optimisation levels.
//
// Fails, with error set and camera untouched, on bounds that are not valid,
// on a non-finite or out-of-range framing option, on a zero width or
// height, and on a box so large or so far from the origin that no clamped
// camera contains it.
bool frame_track_bounds(
    const TrackWorldBounds& bounds,
    std::uint32_t width,
    std::uint32_t height,
    const TrackFramingOptions& options,
    OrbitCamera& camera,
    std::string& error);

// What the track path counts, beside RenderStats.
//
// RenderStats is filled by the same rasterizer the car path uses and every
// counter below the group level means exactly what docs/RENDERER.md says it
// means. Four groups of its fields are DELIBERATELY LEFT AT ZERO here
// rather than filled with a number that would read like a car number and
// count something else:
//
//   material_groups, groups_resolved, groups_unresolved, groups_unusable
//     and groups_invalid. On the car path a material group is walked once
//     and its copies loop inside it. On this path a draw-list entry is one
//     copy of one group, so the same names would count copies. The entry
//     counts below are that number, under names that say so, and `groups`
//     is the distinct-group count.
//   draw_rows. The track renderer walks an already ordered draw list, so it
//     does not own row traversal. In retail mode it fills the alpha-test,
//     blend and fragment counters per submitted entry. See
//     docs/TRACK_MATERIALS.md for the recovered state and approximations.
//
// RenderStats::meshes, meshes_rendered, meshes_skipped_invalid and
// unresolved_hashes are filled, and describe the prepared REGION, not the
// frame: a region mesh no entry names still counts in them. meshes_drawn
// below is the frame's number.
//
// RenderStats::instances, instances_exact and instances_stand_in are filled
// as instances_drawn, instances_drawn and 0. The transform of every copy is
// track_instance_transform applied to the record the file stores, which is
// the recovered placement and not a stand-in, so nothing here is a stand-in
// copy in the sense docs/RENDERER.md gives the word. Which mesh SLOT an
// instance draws is a separate question and is the caller's
// TrackSceneOptions::lod_slot; it is not a placement claim and does not
// make a copy inexact.
struct TrackRenderStats {
    // Draw-list entries walked, and those that submitted at least one
    // triangle. An entry whose group has an empty index range submits none.
    //
    // The two are EQUAL for every scene a parser can produce, and the pair
    // is kept anyway. src/track.cpp refuses a material record with no
    // indices outright, and a group with an empty range is not drawable, so
    // a draw list - whose entries are always drawable groups - never holds
    // one. It is the same shape as docs/TRACK_DRAW_LIST.md's "no undrawable
    // group": the case is unreachable from a file rather than impossible,
    // a hand-built scene can differ, and a backend reading these two
    // should not have to know which.
    std::uint64_t reflection_draws = 0, reflection_triangles = 0, reflection_fragments = 0;
    std::uint64_t entries = 0;
    std::uint64_t entries_drawn = 0;
    // By the binding of the entry's group. resolved entries draw with a
    // texture; the other two draw the fallback pattern of fallback_color.
    std::uint64_t entries_resolved = 0;
    std::uint64_t entries_unresolved = 0;
    std::uint64_t entries_unusable = 0;
    // Distinct scene instances and prepared meshes that submitted a
    // triangle, and distinct prepared groups the entries named.
    std::uint64_t instances_drawn = 0;
    std::uint64_t meshes_drawn = 0;
    std::uint64_t groups = 0;
    // Texture binds a backend issues walking the entries in list order and
    // binding only on a change, counting the first entry and treating every
    // unresolved or unusable group as one shared fallback binding. The same
    // rule TrackDrawListCost::texture_binds uses, recomputed here from what
    // was actually drawn so that a frame and its cost cannot disagree.
    std::uint64_t texture_binds = 0;
    // The bounds of what was drawn, measured over the vertices the frame
    // transformed. Equal to track_draw_list_bounds over the same pair.
    TrackDrawListBounds bounds;
};

// Draws every entry of `list` into `frame`.
//
// ORDER AND IDS. The entries are drawn in list order and the triangle ids
// are the running count of submitted triangles, exactly as render_scene
// assigns them, so a depth tie goes to the later entry. TrackDrawOrder
// therefore changes which of two coplanar surfaces wins a tie, and
// docs/TRACK_RENDER.md measures how many pixels that is for the retail
// region rather than asserting it is none.
//
// The scene is checked with validate_prepared_track_scene and the list with
// validate_track_draw_list, and the options and limits are checked, before
// the frame is allocated, so a rejected input leaves `frame` untouched.
// Vertex indices are checked per triangle and counted as
// triangles_bad_index.
//
// THREE STATISTICS ARE DEAD ON THIS PATH, and the reason is the same one
// entries_drawn carries above. validate_prepared_track_region already
// refuses any triangle index outside its mesh's vertex table, and
// validate_prepared_track_scene runs it, so on a scene this function
// accepts triangles_bad_index is always 0, the bad-index branch below is
// never taken, and TrackRenderStats::bounds.instances_without_vertices is
// always 0. The checks stay because they are cheap and because the
// alternative is an out-of-range read if a validator ever loosens.
//
// The identical block inside track_draw_list_bounds is NOT dead: that
// function is public, is documented as running no validator, and is
// reached with hand-built scenes. Its version is covered.
//
// Fails, with error set, on an invalid scene or list, on a camera or size
// validate_camera or Frame::reset refuses, on a work limit, and on
// MaterialStateMode::retail, which this path does not claim.
bool render_track_draw_list(
    const PreparedTrackScene& scene,
    const TrackDrawList& list,
    const RenderOptions& options,
    const RenderLimits& limits,
    Frame& frame,
    RenderStats& stats,
    TrackRenderStats& track_stats,
    std::string& error,
    const ReferenceDrawObserver& observe = {});

} // namespace nfsu
