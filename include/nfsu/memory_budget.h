#pragma once

#include "nfsu/gpu_upload.h"
#include "nfsu/memory_arena.h"
#include "nfsu/scene.h"
#include "nfsu/track_draw_list.h"
#include "nfsu/track_scene.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nfsu {

// The numbers the four arenas of include/nfsu/memory_arena.h are sized with,
// and what one prepared scene needs from them, derived from the scene itself.
// docs/MEMORY_BUDGETS.md lists every number with its status.
//
// Each number here is exactly one of two things, and its comment says which:
//
//   DERIVED      computed from a structure, a limit or an API this project
//                already has, so it holds on any platform the code builds on;
//   PLACEHOLDER  no measurement stands behind it yet. Its comment names the
//                measurement that would replace it, and the placeholder is
//                not a guess at that measurement's result.
//
// Nothing here is a measurement. The retail numbers the documentation quotes
// were recorded by earlier work in docs/TRACK.md and docs/TRACK_DRAW_LIST.md,
// on a desktop, and none of them is a Switch number.

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;

// ---------------------------------------------------------------------------
// DERIVED ceilings
// ---------------------------------------------------------------------------

// The most vertex, index and texture payload a car scene can hold and still
// be prepared under `limits`: max_vertices vertices of sizeof(MeshVertex),
// max_triangles triangles of sizeof(MeshTriangle) and max_texture_bytes of
// RGBA8. prepare_car_scene refuses anything larger, so no car upload table
// totals more. Saturates at UINT64_MAX.
std::uint64_t car_scene_upload_ceiling(const SceneLimits& limits) noexcept;

// The same for one prepared track region, with sizeof(TrackVertex).
std::uint64_t track_region_upload_ceiling(const TrackSceneLimits& limits) noexcept;

// The largest draw list build_track_draw_list returns under `limits`:
// max_entries entries of sizeof(TrackDrawListEntry). It refuses a longer one,
// so no frame's list over any scene is bigger. Saturates at UINT64_MAX.
std::uint64_t track_draw_list_frame_ceiling(const TrackDrawListLimits& limits) noexcept;

// The largest draw list one frame can build over this scene, which is the
// frame where every placed instance is visible:
// TrackScenePlacementCost::resident_entries entries of
// sizeof(TrackDrawListEntry). That field is documented as the ceiling a
// backend sizes a per-frame buffer against, and this is it in bytes.
std::uint64_t track_scene_frame_ceiling(const PreparedTrackScene& scene) noexcept;

// ---------------------------------------------------------------------------
// PLACEHOLDERS
// ---------------------------------------------------------------------------

// PLACEHOLDER. Replaced by the peak of every persistent allocation of a race
// measured on hardware: the car scenes once uploaded, and whatever else is
// loaded once per session.
constexpr std::uint64_t kPersistentBudgetPlaceholder = 64 * kMiB;

// PLACEHOLDER. Replaced by the peak streaming allocation of one retail region
// measured on hardware, over every region a route loads.
constexpr std::uint64_t kStreamingRegionBudgetPlaceholder = 16 * kMiB;

// PLACEHOLDER. Replaced by the most regions the retail streaming tables keep
// resident at once. Those tables are not decoded (docs/TRACK_OBJECTS.md), so
// nothing in this project can say how many that is.
constexpr std::uint64_t kResidentRegionsPlaceholder = 3;

// PLACEHOLDER. Replaced by the peak per-frame allocation measured on hardware.
// It is far below the derived ceiling track_draw_list_frame_ceiling gives for
// the default TrackDrawListLimits, and that is deliberate: the ceiling says
// what a list can be, not what a frame should be allowed, and a list the
// frame budget cannot take is refused by the transient arena rather than
// silently given more memory.
constexpr std::uint64_t kTransientBudgetPlaceholder = 1 * kMiB;

// PLACEHOLDER. Replaced by the peak GPU residency measured on hardware, with
// every image sized by dkImageLayoutGetSize rather than by its texels, inside
// whatever memory mode the application runs in.
constexpr std::uint64_t kGpuBudgetPlaceholder = 256 * kMiB;

// PLACEHOLDER. Replaced by the peak live allocation count per arena measured
// on hardware. It bounds the bookkeeping each arena reserves up front, 52
// bytes per allocation.
constexpr std::uint32_t kArenaAllocationsPlaceholder = 4096;

// The budget the Switch build creates its MemoryPlan with until measurements
// replace the placeholders:
//
//   persistent  kPersistentBudgetPlaceholder
//   streaming   kResidentRegionsPlaceholder * kStreamingRegionBudgetPlaceholder
//   transient   kTransientBudgetPlaceholder
//   gpu         kGpuBudgetPlaceholder
//
// The alignments are DERIVED. The three CPU arenas take
// alignof(std::max_align_t), what the platform allocator guarantees for any
// object. The GPU arena takes kDekoMemBlockAlignment, the alignment deko3d
// requires of a memory block's size, and a layout asking for more is refused
// rather than placed misaligned. That a block's GPU address is aligned to it
// as well - which is what makes an offset aligned inside the block an aligned
// address - the header does not state and nothing here has verified.
MemoryBudget switch_memory_budget() noexcept;

// ---------------------------------------------------------------------------
// What one scene needs
// ---------------------------------------------------------------------------

// DERIVED from one upload table, by placing it.
struct SceneMemoryRequirement {
    // Buffer memory: the payload, and how far first-fit placement of the
    // table's buffers, in table order, reaches in an empty arena. The
    // difference is what alignment gaps cost.
    std::uint64_t buffer_bytes = 0;
    std::uint64_t buffer_extent = 0;
    // Image memory: the size the layout query gave every texture, and how far
    // the same placement reaches. With linear_image_layout both are lower
    // bounds; with the deko3d layout they are the real figures.
    std::uint64_t image_bytes = 0;
    std::uint64_t image_extent = 0;
    // Staging: every texture copied before one fence, and one at a time.
    std::uint64_t staging_batch_bytes = 0;
    std::uint64_t staging_largest_bytes = 0;
};

// Places every resource of `table` in empty arenas, exactly as an
// UploadLedger over fresh arenas would, and reports the extents. Fails on a
// table the arenas refuse, which a table the builders produced never is, and
// on a layout query that fails or asks for an alignment above
// kMaxArenaAlignment.
bool upload_table_requirement(
    const UploadTable& table,
    const ImageLayoutQuery& image_layout,
    SceneMemoryRequirement& requirement,
    std::string& error);

} // namespace nfsu
