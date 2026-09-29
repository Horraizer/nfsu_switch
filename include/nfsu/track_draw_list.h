#pragma once

#include "nfsu/track_scene.h"
#include "nfsu/track_visibility.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// One bounded track region, placed and selected: the prepared draw list of
// include/nfsu/track_scene.h, the scenery instances of
// include/nfsu/track_objects.h that put its meshes in the world, and the
// per-frame draw list a visible set picks out of the two.
//
// This header decodes nothing. Every byte it carries was decoded by
// docs/TRACK.md, docs/TRACK_OBJECTS.md or docs/TRACK_VISIBILITY.md; what it
// adds is one contract a backend can consume. See docs/TRACK_DRAW_LIST.md.
//
// WHERE THE BOUNDARY IS
//
// A backend reads a PreparedTrackScene and a TrackDrawList and nothing else,
// and it never decides what is drawn: build_track_draw_list turns a visible
// set into ordered (instance, draw) pairs here, above every backend. That is
// issue #40's second criterion, and it is the reason the culler is a
// producer of input to this header rather than something this header calls.
//
// State that precisely, because the weaker claim is the true one. A backend
// does not have to run, understand, configure or reproduce the culler: the
// frustum, the bounding tree, the box test, the residency rule and the
// traversal are all on the other side of this call, and no field below is an
// input to any of them.
//
// It is NOT true that a backend never sees the culler's vocabulary. This
// header includes nfsu/track_visibility.h, so TrackFrustum, TrackCullReport,
// TrackCullPolicy and TrackResidencyRule are all declared in any translation
// unit that includes it. More than that, some of them are in the data: a
// PreparedTrackInstance carries its section_number, a TrackSceneSection
// carries its number, and a TrackDrawList carries requested_policy,
// policy_held, sections_resident and sections_degraded. Those are carried
// deliberately - they are the provenance a caller needs to judge how
// complete a list is, and HOW COMPLETE A DRAW LIST IS below tells a caller
// to read them. They are there to be read, not to be acted on: nothing a
// backend does with a draw list depends on any of them, and a backend that
// ignores every one of them draws exactly the same frame.
//
// The scene holds the whole region and a draw list names a subset of it. The
// other arrangement - preparing a scene per visible set - was rejected: a
// prepared region owns a copy of every vertex, index and texture of the
// region, the visible set changes every frame, and rebuilding it per frame
// would copy megabytes to draw a few hundred instances. The cost of this
// choice is the mirror of that, and it is real: a scene occupies memory
// proportional to the region, not to the frame, so a backend uploads the
// region once and draws whatever the list names. Nothing here streams, and
// nothing here evicts.
//
// WHAT A PREPARED SCENE GUARANTEES
//
//   * It owns every byte a backend reads. The TrackRegionReport, the
//     TrackObjectReport and the RenderTextureSet it was built from may
//     change or be destroyed afterwards.
//   * Every instance it holds names a prepared mesh that exists, with a
//     finite transform, and every mesh handle, draw handle and texture
//     handle in it is in range. validate_prepared_track_scene checks all of
//     that and build_track_scene runs it before returning true.
//   * It is a function of its inputs. Two runs over the same reports give
//     the same instances in the same order with the same handles.
//
// WHAT IT DOES NOT GUARANTEE
//
//   * IT IS NOT THE SET OF THINGS THE GAME WOULD DRAW. Which mesh slot an
//     object draws at a given distance is not decoded: the per-slot
//     thresholds at 0x00034102 +24 and the projection scale the screen-size
//     level needs were not recovered (docs/TRACK_OBJECTS.md). This layer
//     places one caller-named slot, and TrackSceneOptions::lod_slot says
//     which. It is a documented choice, not the retail rule.
//   * IT IS NOT COMPLETE. An instance whose slot names a mesh that streams
//     in with another region is not placed at all, and the scene counts it
//     rather than drawing a stand-in. A backend drawing such a scene leaves
//     a hole exactly where the game draws scenery.
//     TrackScenePlacementCost::unplaced_unresolved is how many.
//   * A DRAW LIST IS ONLY AS COMPLETE AS THE VISIBLE SET IT WAS GIVEN. See
//     TrackDrawList below, which is where a caller reads that.

// ---------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------

// Why an instance of a valid section placed no mesh. Every reason is a
// count in TrackScenePlacementCost and an entry in
// PreparedTrackScene::unplaced.
enum class TrackUnplacedReason {
    // The object's selected slot holds hash 0, and no recovered fallback
    // fills it. Only slots 4 and 5 have one.
    empty_slot,
    // The hash is one of the two speed.exe 0x0044590B and 0x00445917 skip
    // without a lookup: kSkydomeMeshHash and kReservedMeshHash. The loader
    // resolves no solid for them, so neither does this layer.
    skipped_hash,
    // No valid mesh of the prepared region claims the hash. A section
    // routinely names meshes that stream in with another region, so this is
    // the common case and not an error.
    unresolved_hash,
};

// "empty slot", "skipped hash" or "unresolved hash". Stable: tests compare
// against it.
const char* track_unplaced_reason_name(TrackUnplacedReason reason) noexcept;

// One instance of a valid section that placed nothing.
struct TrackUnplacedInstance {
    std::size_t section_index = 0;
    std::size_t instance_index = 0;
    std::size_t object_index = 0;
    // The slot the caller asked for. The slots 4 and 5 fallback is tried
    // before an instance is called unplaced, and failing it does not move
    // this field or the reason below: both describe the requested slot.
    std::size_t slot = 0;
    // The hash that failed; 0 for TrackUnplacedReason::empty_slot.
    std::uint32_t mesh_hash = 0;
    TrackUnplacedReason reason = TrackUnplacedReason::empty_slot;
};

// One placed copy of one prepared mesh: a prepared mesh handle, the
// transform that puts it in the world, and the provenance of both.
//
// The transform is TrackTransform, a row-major 3x3 and a translation, and
// not the 4x4 PreparedInstance carries on the car side. That is deliberate:
// the instance record holds nine int16 and three floats, and
// track_instance_transform only divides the nine by kSceneryMatrixOne. A
// 4x4 would add a row no record stores.
struct PreparedTrackInstance {
    // Index into PreparedTrackScene::region.meshes.
    std::size_t mesh = 0;
    // World slot j is translation[j] + sum over i of local[i] *
    // linear[i * 3 + j], in the raw slot order of TrackVertex: slot 1 is pz
    // and slot 2 is py.
    TrackTransform transform;
    // Provenance. Opaque numbers here, exactly as texture source, pack and
    // record are opaque in PreparedTrackRegion.
    std::size_t section_index = 0;
    std::uint32_t section_number = 0;
    char section_letter = '\0';
    bool has_section_letter = false;
    std::size_t instance_index = 0;
    std::size_t object_index = 0;
    // The slot whose hash placed the mesh, and whether it got there through
    // the slots 4 and 5 fallback at speed.exe 0x00445A81 and 0x00445A8F.
    // With slot_fallback_used, `slot` is the slot the fallback landed on
    // and requested_slot is the one the caller asked for.
    std::size_t slot = 0;
    std::size_t requested_slot = 0;
    bool slot_fallback_used = false;
    std::uint32_t mesh_hash = 0;
    // Instance record +14, kept raw. Bit 0x10 is runtime scratch and the
    // low byte is what the retail mask gate reads; nothing here interprets
    // either.
    std::uint32_t instance_flags = 0;
    // Object record +68 as stored, without kTrackRadiusBias. The gate that
    // adds the bias lives in the culler, not here.
    float radius = 0.0F;
    // The instance's stored int16 box widened by track_box_to_float, in the
    // raw slot order. It bounds the placed mesh for 29347 of 29674 retail
    // instances and is reported, never trusted; see docs/TRACK_OBJECTS.md.
    float bounds_min[3] = {0.0F, 0.0F, 0.0F};
    float bounds_max[3] = {0.0F, 0.0F, 0.0F};
};

// One valid source section, and the slice of
// PreparedTrackScene::instance_handles that covers its instances.
struct TrackSceneSection {
    // Position in TrackObjectReport::sections.
    std::size_t section_index = 0;
    std::uint32_t number = 0;
    char letter = '\0';
    bool has_letter = false;
    // TrackSection::instances.size() of the source.
    std::size_t source_instances = 0;
    // First of source_instances consecutive entries of
    // PreparedTrackScene::instance_handles. Entry k holds the handle of
    // source instance k: an index into PreparedTrackScene::instances, or
    // kNoHandle when that instance placed nothing.
    std::size_t first_handle = 0;
    std::uint64_t placed = 0;
};

// Deterministic placement cost. Every field is derived from the inputs in
// source order, so two runs over the same reports give the same numbers.
struct TrackScenePlacementCost {
    std::uint64_t source_sections = 0;
    std::uint64_t valid_sections = 0;
    // Instances of the valid sections. The invalid ones carry no decoded
    // record at all, so they contribute nothing to any count here.
    std::uint64_t source_instances = 0;
    std::uint64_t placed_instances = 0;
    std::uint64_t unplaced_empty_slot = 0;
    std::uint64_t unplaced_skipped_hash = 0;
    std::uint64_t unplaced_unresolved = 0;
    // Placements that went through the slots 4 and 5 fallback.
    std::uint64_t slot_fallbacks = 0;
    // Distinct mesh hashes the placed instances resolved, and distinct
    // hashes that failed to resolve.
    std::uint64_t placed_hashes = 0;
    std::uint64_t unplaced_hashes = 0;
    // Prepared meshes at least one instance names, and the rest. A prepared
    // mesh no instance names is drawn zero times by this layer; it is not
    // drawn once. That is the one car-scene rule that does not carry over,
    // and PreparedMeshInstances::copies is where the car rule lives.
    std::uint64_t drawn_meshes = 0;
    std::uint64_t undrawn_meshes = 0;
    // What a draw list would hold if every placed instance were visible:
    // the drawable groups of each instance's mesh, and their triangles.
    // This is the ceiling a backend sizes a per-frame buffer against.
    std::uint64_t resident_entries = 0;
    std::uint64_t resident_triangles = 0;
    // Union of the placed instances' stored boxes, in the raw slot order.
    bool has_bounds = false;
    float bounds_min[3] = {0.0F, 0.0F, 0.0F};
    float bounds_max[3] = {0.0F, 0.0F, 0.0F};
};

struct TrackPlacementLimits {
    std::uint64_t max_instances = 1ULL << 20;
    std::uint64_t max_sections = 4096;
};

struct TrackSceneOptions {
    // Which of the six object mesh slots places an instance. Slot 0 is the
    // head of the level-of-detail chain and is set in all 9681 retail
    // objects; slots 1 to 5 are optional, and an instance whose object
    // leaves the named slot empty places nothing unless the slots 4 and 5
    // fallback fills it. Outside 0..kSceneryObjectMeshSlots-1
    // build_track_scene fails.
    //
    // This is not a distance rule and picking a slot is not what retail
    // does per frame; see WHAT IT DOES NOT GUARANTEE above.
    //
    // THE SLOTS 4 AND 5 FALLBACK, AND WHERE IT DIVERGES FROM RETAIL.
    // speed.exe 0x00445A81 and 0x00445A8F fill an unresolved slot 4 or 5
    // with the first solid that slots 0 to 3 resolved. The shape is
    // reproduced here exactly - those two slots and no others, slots 0 to 3
    // walked in that order, stopping at the first that resolves - but the
    // predicate is not the same one, and the difference is not small:
    //
    //   retail resolves a hash against every solid the game has loaded;
    //   this layer resolves it against the valid meshes of ONE prepared
    //   region.
    //
    // A section routinely names meshes that stream in with another region
    // (21 of 39 instances of the retail section docs/TRACK_DRAW_LIST.md
    // measures). For such a hash retail's chain stops at slot 0 and this
    // one walks past it, so the two can land on different slots for the
    // same object. A caller that needs the retail answer needs a scene
    // built over every region the section reaches, which this project
    // cannot assemble yet because the streaming tables are undecoded.
    // PreparedTrackInstance::slot records where this layer actually landed.
    std::size_t lod_slot = 0;
    TrackSceneLimits region;
    TrackPlacementLimits placement;
};

// A self-contained, validated, placed track region.
struct PreparedTrackScene {
    // The geometry, draws and textures, owned. prepare_track_region built
    // it and validate_prepared_track_region passed on it.
    PreparedTrackRegion region;
    // The region the instances were decoded from, copied from the object
    // report. It is a different region of the same container as
    // region.region in every retail case, and the two are never required to
    // be related.
    TrackRegionSelector object_region;
    // The slot options.lod_slot named.
    std::size_t lod_slot = 0;
    // Ascending (section_index, instance_index).
    std::vector<PreparedTrackInstance> instances;
    // Valid sections only, ascending section_index.
    std::vector<TrackSceneSection> sections;
    // One entry per source instance of every valid section, in section
    // order; see TrackSceneSection::first_handle.
    std::vector<std::size_t> instance_handles;
    // Ascending (section_index, instance_index).
    std::vector<TrackUnplacedInstance> unplaced;
    // Distinct hashes that placed nothing, ascending. Hash 0 is never in
    // it: an empty slot names no hash.
    std::vector<std::uint32_t> unplaced_hashes;
    // Index into `instances` of the first instance that names prepared mesh
    // m, or kNoHandle when no instance does. One entry per prepared mesh.
    // Only a witness, not a list: a mesh is normally named many times.
    std::vector<std::size_t> first_instance_of_mesh;
    TrackScenePlacementCost cost;
};

// Places every instance of every valid section of `objects` against the
// meshes of `geometry`, having prepared `geometry` and `textures` with
// prepare_track_region first.
//
// A hash resolves to the lowest-numbered valid mesh of the region that
// carries it, which is the rule resolve_track_objects uses, so the two
// agree on every instance they both bind.
//
// Fails, with error set and scene left empty, on a slot outside the six, on
// a limit, on an allocation failure or on a failed self-check;
// std::bad_alloc is caught. An invalid section is skipped and contributes
// nothing, exactly as an invalid mesh is skipped by prepare_track_region.
bool build_track_scene(
    const TrackRegionReport& geometry,
    const TrackObjectReport& objects,
    const RenderTextureSet& textures,
    const TrackSceneOptions& options,
    PreparedTrackScene& scene,
    std::string& error);

// Consumes geometry/texture payloads, including on failure. Object reports
// stay available for culling and collision. The output is cleared on failure.
bool build_track_scene(
    TrackRegionReport&& geometry,
    const TrackObjectReport& objects,
    RenderTextureSet&& textures,
    const TrackSceneOptions& options,
    PreparedTrackScene& scene,
    std::string& error);

// Checks every structural invariant a backend relies on: the prepared
// region itself, ascending unique sections and instances, mesh handles
// inside the region, finite transforms, the handle table against the
// instances it indexes, the unplaced list and its hash set, the first-use
// witnesses and the recomputed cost statistics. build_track_scene runs it
// before returning true.
bool validate_prepared_track_scene(
    const PreparedTrackScene& scene,
    std::string& error);

// The prepared instance that places source instance `instance_index` of
// source section `section_index`, or kNoHandle when the scene placed none.
// A section the scene does not hold answers kNoHandle too, so a caller that
// needs those apart looks the section up itself.
std::size_t track_scene_instance_of(
    const PreparedTrackScene& scene,
    std::size_t section_index,
    std::size_t instance_index) noexcept;

// ---------------------------------------------------------------------
// The per-frame draw list
// ---------------------------------------------------------------------

// The order the entries are in. The retail bucket option applies the
// texture row and batching recovered at speed.exe 0x40F225..0x40F258 and
// 0x40ED50..0x40F0CF. Effect-column selection for scenery is still an
// approximation; see docs/TRACK_MATERIALS.md.
enum class TrackDrawOrder {
    // The visible set's own order, and inside one instance the drawable
    // groups of its mesh in prepared order. When the set came out of
    // cull_track_region under TrackCullPolicy::retail_tree this is the
    // retail traversal order, so a backend walking it visits scenery the
    // way the game's tree walk reaches it.
    visible_instance,
    // Grouped by material: every copy of one prepared group is contiguous,
    // groups in prepared order and copies in visible order inside a group.
    // This is the order a backend wants - at most one texture bind per
    // group rather than one per copy, and a run of copies it may submit as
    // one instanced call. At most, not exactly: two consecutive groups that
    // resolved the same texture share a bind, and over a retail region that
    // is measurable, so read cost.texture_binds rather than assuming it
    // equals cost.groups.
    //
    // It is a stable reordering of the same entries, so all orders
    // hold the same multiset.
    material_group,
    // Ascending texture row, then first use of each texture in visible
    // instance order; copies of that texture stay in their submitted order.
    retail_bucket,
};

// "visible instance", "material group" or "retail bucket".
const char* track_draw_order_name(TrackDrawOrder order) noexcept;

struct TrackDrawListLimits {
    std::uint64_t max_entries = 1ULL << 22;
    std::uint64_t max_triangles = 1ULL << 26;
};

struct TrackDrawListOptions {
    TrackDrawOrder order = TrackDrawOrder::visible_instance;
    TrackDrawListLimits limits;
};

// One draw call: one copy of one prepared material group.
//
// A backend uploads PreparedTrackScene::region once - the meshes' vertices
// and triangles and the region's textures - and then, for each entry, binds
// the texture region.draws[draw].texture names, applies
// scene.instances[instance].transform, and issues
// region.draws[draw].index_count indices from region.draws[draw].first_index
// out of the index stream of mesh region.draws[draw].mesh. That mesh is
// always scene.instances[instance].mesh; the field is not repeated here,
// and validate_track_draw_list checks the two agree.
//
// A group whose binding is not BindingKind::resolved carries texture
// kNoHandle and names no texture at all, exactly as TrackDraw says. A
// backend binds its own fallback there; cost.texture_binds counts every
// such group as one shared fallback binding, which is the same rule
// TrackDrawCost::texture_binds uses.
struct TrackDrawListEntry {
    // Index into PreparedTrackScene::instances.
    std::uint32_t instance = 0;
    // Index into PreparedTrackScene::region.draws. Always a drawable group.
    std::uint32_t draw = 0;
};

struct TrackDrawListCost {
    // Visible instances the list was built from, and what became of them.
    std::uint64_t visible = 0;
    std::uint64_t visible_placed = 0;
    // Visible instances the scene placed nothing for. A backend draws
    // nothing there and the game draws scenery there; this is the count
    // that says how much.
    std::uint64_t visible_unplaced = 0;
    // Placed visible instances whose mesh has no drawable group at all.
    std::uint64_t instances_without_entries = 0;
    std::uint64_t entries = 0;
    std::uint64_t triangles = 0;
    // Distinct prepared groups named, and distinct prepared meshes.
    std::uint64_t groups = 0;
    std::uint64_t meshes = 0;
    // Texture binds a backend issues walking `entries` in order and binding
    // only on a change, counting the first entry and treating every
    // unresolved or unusable group as one shared fallback binding. The same
    // rule TrackDrawCost::texture_binds uses, over this order.
    std::uint64_t texture_binds = 0;
};

// What a backend draws for one frame.
//
// HOW COMPLETE THIS IS. The list holds exactly the instances the visible set
// named that the scene placed. It is not "everything the camera can see",
// and three separate things narrow it:
//
//   * the visible set. Under TrackCullPolicy::draw_everything nothing was
//     tested at all and the set is every instance of every resident
//     section; under TrackCullPolicy::retail_tree it is the recovered
//     frustum rule, degraded to a flat test for any section whose tree
//     fails containment. TrackCullReport carries which, and the four
//     provenance fields below copy it in so that a caller holding only a
//     draw list can still read it.
//   * residency. Which sections are resident at all is the caller's
//     TrackResidency, and the retail zone list is undecoded (issue #96), so
//     TrackResidencyRule::every_section - draw every valid section - is the
//     only rule this project can serve for a real frame. The game draws a
//     smaller set than that, and this layer cannot say which.
//   * placement. cost.visible_unplaced counts the visible instances whose
//     mesh is not in this region.
//
// A caller that wants "is this list the whole frame" has no single flag to
// read, because no single fact answers it. policy_held false, a
// requested_policy of draw_everything, or a non-zero visible_unplaced each
// mean it is not.
struct TrackDrawList {
    TrackDrawOrder order = TrackDrawOrder::visible_instance;
    // In `order`.
    std::vector<TrackDrawListEntry> entries;
    // True when the list was built from a TrackCullReport, which is the
    // only input that carries the four fields below. False when it was
    // built from an explicit visible set, and then those four are at their
    // defaults and say nothing.
    bool has_cull_provenance = false;
    TrackCullPolicy requested_policy = TrackCullPolicy::retail_tree;
    bool policy_held = true;
    std::uint64_t sections_resident = 0;
    std::uint64_t sections_degraded = 0;
    TrackDrawListCost cost;
};

// Builds the draw list for one visible set, given as the (section,
// instance) pairs the culler emits. Nothing but those two fields of each
// entry is read, so a caller that selects visible instances some other way
// may use this directly.
//
// A visible entry the scene placed nothing for is counted in
// cost.visible_unplaced and contributes no entry. A visible entry that
// names a section or an instance the scene does not hold is an error, not a
// count: the two reports disagree and a silently shorter draw list would
// hide it.
//
// The scene is read as given, so validate_prepared_track_scene belongs
// before it. In particular the (section, instance) lookup is a binary
// search over PreparedTrackScene::sections, which that validator is what
// checks is ascending; a scene whose sections are out of order answers the
// wrong instance here rather than failing.
//
// Fails, with error set and list left empty, on such a mismatch, on a
// limit, on a repeated (section, instance) pair, on an allocation failure
// or on a failed self-check; std::bad_alloc is caught.
bool build_track_draw_list(
    const PreparedTrackScene& scene,
    const std::vector<TrackVisibleInstance>& visible,
    const TrackDrawListOptions& options,
    TrackDrawList& list,
    std::string& error);

// The same, from a cull report, which also fills the provenance fields
// above. Only TrackCullReport::visible and four statistics are read; the
// frustum, the tree and the residency rule stay on the culler's side of
// this call.
bool build_track_draw_list(
    const PreparedTrackScene& scene,
    const TrackCullReport& cull,
    const TrackDrawListOptions& options,
    TrackDrawList& list,
    std::string& error);

// Checks that every entry names an instance and a drawable group of this
// scene, that the group's mesh is the instance's mesh, that the order is
// the one the list claims, and that the cost statistics match.
// build_track_draw_list runs it before returning true.
bool validate_track_draw_list(
    const PreparedTrackScene& scene,
    const TrackDrawList& list,
    std::string& error);

} // namespace nfsu
