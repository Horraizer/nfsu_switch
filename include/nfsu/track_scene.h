#pragma once

#include "nfsu/scene.h"
#include "nfsu/track.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// A backend-neutral, validated draw list for one bounded track region,
// plus the cost statistics a desktop or deko3d backend needs to size its
// buffers. It is the track counterpart of PreparedCarScene: material
// binding happens once, here, and a backend never looks up a texture hash
// again. This layer has no file, path, TexturePack or command-line API.
//
// It is deliberately not a renderer and not a scene graph. Instance
// transforms, visibility groups and collision are separate work and are
// not decoded (see docs/TRACK.md).

struct TrackSceneLimits {
    // Material groups over the region's valid meshes.
    std::uint64_t max_draws = 65536;
    std::uint64_t max_vertices = 1ULL << 22;
    std::uint64_t max_triangles = 1ULL << 22;
    // Triangles over all drawable ranges; ranges may overlap.
    std::uint64_t max_draw_triangles = 1ULL << 24;
    // Payload bytes (RGBA8 or native DXT) of the textures copied into the prepared region.
    std::uint64_t max_texture_bytes = 128ULL * 1024ULL * 1024ULL;
};

// One valid source mesh, copied. Nothing refers back to the report.
struct PreparedTrackMesh {
    std::size_t source_index = 0;
    std::string name;
    std::uint32_t mesh_hash = 0;
    std::uint64_t hash_record_offset = 0;
    bool has_hash_record = false;
    // Source material record count. Validation requires exactly this many
    // consecutive draws with material indices 0..material_count-1.
    std::size_t material_count = 0;
    std::vector<TrackVertex> vertices;
    std::vector<MeshTriangle> triangles;
};

// One material group of a prepared mesh, in record order. A backend
// uploads PreparedTrackMesh::vertices and ::triangles and the region's
// textures, then issues index_count indices from first_index with the
// texture at handle texture, or a fallback for any other binding.
struct TrackDraw {
    std::size_t mesh = 0;
    std::size_t material_index = 0;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    std::uint32_t texture_slot = 0;
    // The hash the slot names; 0 for invalid_slot.
    std::uint32_t texture_hash = 0;
    BindingKind binding = BindingKind::unresolved;
    // Resolved only: index into PreparedTrackRegion::textures and the
    // source, pack and record of that texture. Otherwise kNoHandle and 0.
    std::size_t texture = kNoHandle;
    std::size_t texture_source = 0;
    std::size_t texture_pack = 0;
    std::size_t texture_record = 0;
    std::uint32_t material_key_slot = 0;
    std::uint32_t flags = 0;
    std::uint64_t record_offset = 0;
    float bounds_min[3] = {0.0F, 0.0F, 0.0F};
    float bounds_max[3] = {0.0F, 0.0F, 0.0F};
    // The untouched 0x00134B02 record.
    std::array<unsigned char, kMaterialRecordSize> raw{};
    // False for an invalid slot or an index range that is not a multiple
    // of 3 inside the mesh index stream. Such a group is never drawn.
    bool drawable = false;
};

// Draw-list cost of the prepared region. Every field is derived from the
// prepared data in source order, so two runs over the same inputs give the
// same numbers.
struct TrackDrawCost {
    std::uint64_t meshes = 0;
    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    // What a backend uploads: 24 bytes per vertex in the source layout and
    // 6 bytes per triangle of u16 indices.
    std::uint64_t vertex_buffer_bytes = 0;
    std::uint64_t index_buffer_bytes = 0;
    std::uint64_t draws = 0;
    std::uint64_t drawable_draws = 0;
    std::uint64_t resolved_draws = 0;
    std::uint64_t unresolved_draws = 0;
    std::uint64_t unusable_draws = 0;
    std::uint64_t invalid_slot_draws = 0;
    // Triangles issued by the drawable draws; ranges may overlap, so this
    // is not bounded by triangles.
    std::uint64_t drawn_triangles = 0;
    std::uint64_t largest_draw_triangles = 0;
    // Textures retained and their payload bytes (RGBA8 or native DXT).
    std::uint64_t textures = 0;
    std::uint64_t texture_bytes = 0;
    // Texture binds a backend issues if it walks the draws in this order
    // and binds only on a change, counting the first drawable draw and
    // treating every unresolved or unusable group as one fallback binding.
    std::uint64_t texture_binds = 0;
    // Distinct hashes no source resolved.
    std::uint64_t unresolved_hashes = 0;
};

// A self-contained, validated draw list. It owns every byte a backend
// reads, so the TrackRegionReport and RenderTextureSet it was prepared
// from may change or be destroyed afterwards.
struct PreparedTrackRegion {
    // The region the meshes came from, copied from the report.
    TrackRegionSelector region;
    // TrackRegionReport::meshes.size() of the source.
    std::size_t source_mesh_count = 0;
    // Positions of the source meshes that were rejected, ascending.
    std::vector<std::size_t> skipped_invalid_meshes;
    // Positions of the source meshes that were sound but empty, ascending.
    // They are not rejections; see TrackMesh::empty.
    std::vector<std::size_t> skipped_empty_meshes;
    // Ascending source_index.
    std::vector<PreparedTrackMesh> meshes;
    // Mesh order, then material record order.
    std::vector<TrackDraw> draws;
    // Copies of the resolved textures, usable, in order of first use.
    std::vector<RenderTexture> textures;
    // Distinct hashes of unresolved groups, ascending.
    std::vector<std::uint32_t> unresolved_hashes;
    TrackDrawCost cost;
};

// Copies every valid mesh of the region, resolves every material group
// once against the given texture set and copies each resolved texture
// once. Fails, with error set and region left empty, on a limit, an
// allocation failure or a failed self-check; std::bad_alloc is caught.
// An invalid mesh is skipped and recorded, never partially drawn.
bool prepare_track_region(
    const TrackRegionReport& report,
    const RenderTextureSet& textures,
    const TrackSceneLimits& limits,
    PreparedTrackRegion& region,
    std::string& error);

// Consuming form: transfers mesh and texture payloads without a second copy.
// Inputs may be partially consumed on failure; the output is still cleared.
bool prepare_track_region(
    TrackRegionReport&& report,
    RenderTextureSet&& textures,
    const TrackSceneLimits& limits,
    PreparedTrackRegion& region,
    std::string& error);

// Checks every structural invariant a backend relies on: ascending unique
// meshes, one ordered draw per material record, index ranges inside the
// mesh index stream, every triangle index inside the mesh vertex count,
// binding and texture provenance, first-use texture handles, texture
// sizes and the recomputed cost statistics. prepare_track_region runs it
// before returning true.
bool validate_prepared_track_region(
    const PreparedTrackRegion& region,
    std::string& error);

} // namespace nfsu
