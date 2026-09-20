#pragma once

#include "nfsu/mesh.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace nfsu {

struct GeometryReport;

// Backend-neutral prepared car scene. Mesh selection and material lookup
// happen once, in prepare_car_scene; a render backend consumes only the
// validated PreparedCarScene and never looks up a texture hash again. This
// layer has no file, path, TexturePack, archive or command-line API; numeric
// source/pack/record provenance is carried as opaque metadata. See
// docs/RENDERER.md.

// Decoded top level of one pack texture, rows top to bottom.
struct RenderTexture {
    // Info +36 of the record.
    std::uint32_t hash = 0;
    // The distinct hashes the record claims (see record_claims_hash), in
    // the order info +36, hash table +0, blob table +0; the first
    // claim_count entries are used. Set by the decode functions. They
    // differ from hash only for an invalid record. Hand-built textures may
    // leave claim_count 0; hash always counts as claimed.
    std::array<std::uint32_t, 3> claims{};
    std::size_t claim_count = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<unsigned char> rgba;
    bool valid = false;
    std::string problem;
    // Provenance: caller source index, pack index within that source and
    // TextureRecord::index.
    std::size_t source = 0;
    std::size_t pack = 0;
    std::size_t record = 0;

    // True if value is hash or one of the first claim_count claims.
    bool claims_hash(std::uint32_t value) const noexcept;
    // True if valid, width and height are positive and rgba holds exactly
    // width * height * 4 bytes.
    bool usable() const noexcept;
};

// A record left out of the set because an earlier source already claims
// one of its hashes (see record_claims_hash).
struct ShadowedTexture {
    std::uint32_t hash = 0;
    std::size_t source = 0;
    std::size_t pack = 0;
    std::size_t record = 0;
    // The earliest source that claims the hash.
    std::size_t by_source = 0;
};

// A hash claimed by a shadowed record that no earlier source claims. The
// record is not in the set, but its claim still blocks later sources, so
// resolve_material reports the hash as unusable.
struct UnusableClaim {
    std::uint32_t hash = 0;
    std::size_t source = 0;
    std::size_t pack = 0;
    std::size_t record = 0;
};

// Entries in source, pack and record order. A single pack gives one entry
// per texture, in pack order.
struct RenderTextureSet {
    std::vector<RenderTexture> textures;
    // Sum of RenderTexture::rgba sizes.
    std::uint64_t decoded_bytes = 0;
    // In source, pack and record order.
    std::vector<ShadowedTexture> shadowed;
    // In source, pack and record order, then claim order
    // (RenderTexture::claims). At most two per shadowed record.
    std::vector<UnusableClaim> unusable_claims;
    // Sources not used at all because an earlier source is incomplete.
    std::vector<std::size_t> blocked_sources;
    // Records skipped because none of their claimed hashes is in the
    // source's wanted list.
    std::uint64_t not_wanted = 0;
};

enum class BindingKind {
    // A texture matches when it claims the hash (RenderTexture::claims_hash).
    resolved,      // exactly one usable pack texture matches
    unresolved,    // no pack texture or unusable claim matches
    unusable,      // several or only invalid pack textures match, or an
                   // entry of RenderTextureSet::unusable_claims does
    invalid_slot   // the texture slot is outside the mesh hash list
};

const char* binding_kind_name(BindingKind kind) noexcept;

struct MaterialBinding {
    BindingKind kind = BindingKind::unresolved;
    std::uint32_t hash = 0;
    std::size_t texture_index = 0;  // valid for resolved only
};

// Used by prepare_car_scene only; render backends read the prepared binding.
MaterialBinding resolve_material(
    const Mesh& mesh,
    const MeshMaterial& material,
    const RenderTextureSet& textures) noexcept;

// The same resolution against an explicit texture-hash list. The Mesh
// overload calls this one with Mesh::texture_hashes; decoders that build no
// Mesh (the bounded track region, include/nfsu/track.h) pass their own list.
// Behaviour is identical for identical lists.
MaterialBinding resolve_material(
    const std::vector<std::uint32_t>& texture_hashes,
    const MeshMaterial& material,
    const RenderTextureSet& textures) noexcept;

// One mesh selector. index is the position in GeometryReport::meshes
// (parse_geometry sets Mesh::index to the same value). name is compared
// byte for byte, case-sensitively, with Mesh::name. hash is matched against
// the verified 0x00134003/0x00134004 mesh directory.
enum class MeshSelectorKind { index, name, hash };

const char* mesh_selector_kind_name(MeshSelectorKind kind) noexcept;

struct MeshSelector {
    MeshSelectorKind kind = MeshSelectorKind::index;
    std::uint64_t index = 0;
    std::string name;
    std::uint32_t hash = 0;
};

// No selectors: the "all meshes" profile, every valid mesh in source
// order, invalid meshes skipped and counted. Any selector: an explicit
// subset. Neither is a stock-car assembly; which meshes the game draws
// together is not decoded.
struct MeshSelection {
    std::vector<MeshSelector> selectors;
};

enum class SelectionProfile { all_meshes, explicit_subset };

// "all meshes" or "explicit subset".
const char* selection_profile_name(SelectionProfile profile) noexcept;

struct SceneLimits {
    std::size_t max_selectors = 256;
    // Sum of the selector name sizes.
    std::size_t max_selector_text_bytes = 16384;
    // Material groups over the selected meshes (prepared draws).
    std::uint64_t max_draws = 65536;
    // Vertices and triangles copied from the selected meshes.
    std::uint64_t max_vertices = 1U << 22;
    std::uint64_t max_triangles = 1U << 22;
    // Triangles over all drawable ranges; ranges may overlap.
    std::uint64_t max_draw_triangles = 1U << 24;
    // RGBA8 bytes of the textures copied into the scene.
    std::uint64_t max_texture_bytes = 128ULL * 1024ULL * 1024ULL;
};

// Resolves a selection to mesh positions in ascending source order. The
// all-meshes profile gives every valid mesh. An explicit subset fails on
// too many selectors or too much name text (checked before anything is
// allocated), an index outside the report, an empty name, a name with a
// byte outside 0x20..0x7E or longer than kMeshNameSize, a name no mesh
// has, a name more than one mesh has, an unavailable, unknown or ambiguous
// verified directory hash, a selected mesh that is invalid, and the same
// mesh selected twice by any combination. Selectors are checked
// in the order given and the first problem is reported. On success
// selected_by (if not null) receives, per result entry, the index of the
// selector that chose it; it is empty for the all-meshes profile.
bool resolve_mesh_selection(
    const GeometryReport& geometry,
    const MeshSelection& selection,
    const SceneLimits& limits,
    std::vector<std::size_t>& meshes,
    std::vector<std::size_t>* selected_by,
    std::string& error);

constexpr std::size_t kNoHandle = std::numeric_limits<std::size_t>::max();

// One selected mesh. The scene owns copies of the source vertices,
// triangles and verified directory provenance; nothing refers back to the
// GeometryReport.
struct PreparedMesh {
    std::size_t source_index = 0;
    std::string name;
    std::uint32_t directory_hash = 0;
    std::uint64_t directory_offset = 0;
    std::uint64_t directory_size = 0;
    bool has_directory_entry = false;
    // Index into PreparedCarScene::selectors, kNoHandle for all meshes.
    std::size_t selector = kNoHandle;
    // Number of source material records. Validation requires exactly this
    // many consecutive draws with material indices 0..material_count-1.
    std::size_t material_count = 0;
    std::vector<MeshVertex> vertices;
    std::vector<MeshTriangle> triangles;
};

// One material group of a selected mesh, in record order. A GPU backend
// uploads PreparedMesh::vertices, the index stream PreparedMesh::triangles
// (three u16 per triangle) and PreparedCarScene::textures, then issues
// index_count indices from first_index with the texture at handle
// texture, or the fallback for other bindings.
struct PreparedDraw {
    // Index into PreparedCarScene::meshes.
    std::size_t mesh = 0;
    // Index into the source Mesh::materials.
    std::size_t material_index = 0;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    std::uint32_t texture_slot = 0;
    // The hash the slot names; 0 for invalid_slot.
    std::uint32_t texture_hash = 0;
    BindingKind binding = BindingKind::unresolved;
    // Resolved only: index into PreparedCarScene::textures and the source,
    // pack and record of that texture. Otherwise kNoHandle and 0.
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
    // of 3 inside the mesh index stream; such a group is counted as invalid
    // and never drawn.
    bool drawable = false;
};

// A self-contained, validated draw list. It owns every byte a backend
// reads, so the GeometryReport and RenderTextureSet it was prepared from
// may change or be destroyed afterwards without affecting it. Fields are
// public; backends call validate_prepared_scene (render_scene does) and
// refuse a scene that was changed into an inconsistent state.
struct PreparedCarScene {
    SelectionProfile profile = SelectionProfile::all_meshes;
    // The selectors as given; empty for all meshes.
    std::vector<MeshSelector> selectors;
    // GeometryReport::meshes.size() of the source.
    std::size_t source_mesh_count = 0;
    // All meshes only: positions of the invalid meshes, ascending.
    std::vector<std::size_t> skipped_invalid_meshes;
    // Ascending source_index.
    std::vector<PreparedMesh> meshes;
    // Mesh order, then record order.
    std::vector<PreparedDraw> draws;
    // Copies of the resolved textures, usable, in order of first use.
    std::vector<RenderTexture> textures;
    // Distinct hashes of unresolved groups (drawable or not), ascending.
    std::vector<std::uint32_t> unresolved_hashes;
};

// Prepares the scene: resolves the selection, copies the selected meshes,
// resolves every material group once and copies each resolved texture
// once. Fails, with error set and scene left empty, on a selection error,
// a limit, an allocation failure or a failed self-check; std::bad_alloc is
// caught.
bool prepare_car_scene(
    const GeometryReport& geometry,
    const RenderTextureSet& textures,
    const MeshSelection& selection,
    const SceneLimits& limits,
    PreparedCarScene& scene,
    std::string& error);

// Checks every structural invariant a backend relies on: profile coverage
// and semantic selectors, ascending unique meshes, one ordered draw per
// material, raw-record agreement, derived flags and hashes, binding and
// texture provenance, first-use texture handles, texture sizes, and a drawn
// triangle total below 2^32 - 1. Vertex indices inside triangles are checked
// by backends per triangle.
bool validate_prepared_scene(const PreparedCarScene& scene, std::string& error);

// Triangles over all drawable ranges; only meaningful for a valid scene.
std::uint64_t prepared_draw_triangles(const PreparedCarScene& scene) noexcept;

} // namespace nfsu
