#pragma once
#include "nfsu/car_shading.h"

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

enum class RenderTextureStorage { rgba8, dxt1, dxt3 };

// Top level of one pack texture. Compressed blocks are opt-in for GPU use.
struct RenderTexture {
    // Info +36 of the record.
    std::uint32_t hash = 0;
    // The distinct hashes the record claims (see record_claims_hash), in
    // the order info +36, hash table +0, blob table +0; the first
    // claim_count entries are used. Set by the decode functions. They
    // differ from hash for an invalid record or an explicitly evidenced retail
    // DEFAULTALPHA substitution (bind_track_default_alpha). Hand-built textures may
    // leave claim_count 0; hash always counts as claimed.
    std::array<std::uint32_t, 3> claims{};
    std::size_t claim_count = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<unsigned char> rgba;
    RenderTextureStorage storage = RenderTextureStorage::rgba8;
    std::vector<unsigned char> blocks;
    const std::vector<unsigned char>& payload() const noexcept {
        return storage == RenderTextureStorage::rgba8 ? rgba : blocks;
    }
    bool valid = false;
    std::string problem;
    // Provenance: caller source index, pack index within that source and
    // TextureRecord::index.
    std::size_t source = 0;
    std::size_t pack = 0;
    std::size_t record = 0;
    // The two raw texture-info bytes the retail draw path reads for render
    // state, carried unchanged: +85 selects the alpha test and +86 the
    // blend. The decode functions copy them from the record before it is
    // validated, so an invalid record still reports what it stored. A
    // hand-built texture leaves both 0, which is the opaque state. Never
    // interpret them here; render_alpha_test_enabled and
    // render_blend_mode_of hold the only decoded rules. See
    // docs/MATERIALS.md.
    std::uint8_t alpha_test_mode = 0;
    std::uint8_t alpha_blend_mode = 0;
    // The two raw texture-info bytes the retail draw path turns into the
    // row of the draw queue bucket a material is appended to: +81 counts as
    // a signed byte and +84, when nonzero, adds 8. Carried unchanged and
    // decoded only by render_draw_layer.
    std::uint8_t layer_order = 0;
    std::uint8_t layer_group = 0;

    // True if value is hash or one of the first claim_count claims.
    bool claims_hash(std::uint32_t value) const noexcept;
    // True if valid and exactly one payload holds the expected top level.
    // DXT rows contain ceil(width / 4) blocks, with ceil(height / 4) rows.
    bool usable() const noexcept;
};

// Retail render state decoded from the two raw texture-info bytes above.
// Both rules are traced in the retail draw path; see docs/MATERIALS.md for
// the addresses. They are duplicated from texture.h on purpose: this layer
// must not depend on the pack reader. tests/render_tests.cpp checks the two
// decoders agree for all 256 byte values.

// Texture-info +85 == 1 enables the alpha test; every other value leaves it
// off.
bool render_alpha_test_enabled(std::uint8_t alpha_test_mode) noexcept;

// D3DRS_ALPHAREF written with the test, compared with D3DCMP_GREATER, so a
// texel passes only when its alpha is strictly above this value.
constexpr std::uint8_t kRenderAlphaRef = 0xAA;

// The draw queue row a material with these two bytes is appended to:
// (layer_group != 0 ? 8 : 0) + (signed) layer_order, clamped to
// 0..kRenderDrawLayerCount - 1. The retail code does not clamp; every
// record read from retail data stays inside the range on its own.
constexpr int kRenderDrawLayerCount = 16;

int render_draw_layer(std::uint8_t layer_group, std::uint8_t layer_order) noexcept;

// Texture-info +86: 0 disables blending, 2 selects SRCALPHA/ONE, and every
// other non-zero value selects SRCALPHA/INVSRCALPHA.
enum class RenderBlendMode { disabled, source_over, additive };

RenderBlendMode render_blend_mode_of(std::uint8_t alpha_blend_mode) noexcept;

// "disabled", "source-over" or "additive".
const char* render_blend_mode_name(RenderBlendMode mode) noexcept;

// Which material coefficient groups of the car effect survive the
// flag-controlled zeroing in the retail coefficient upload. The three bits
// are the only proven consumers of the material record flags; no semantic
// name for any flag bit is claimed, and nothing else may be derived from
// the flags. environment_enabled stands for the runtime global the upload
// also tests. See docs/MATERIALS.md.
// The traced branch zeros the diffuse and the specular vectors together,
// so diffuse and specular are always equal; they are kept apart because the
// branch names two coefficient groups, and a test pins the equality.
struct MaterialCoefficientGates {
    bool diffuse = true;
    bool specular = true;
    bool environment = true;
};

MaterialCoefficientGates material_coefficient_gates(
    std::uint32_t flags, bool environment_enabled) noexcept;

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
    // As the decode leaves it: in source, pack and record order, then claim
    // order (RenderTexture::claims), at most two per shadowed record. A
    // later pass may rewrite it - bind_runtime_skin removes every entry
    // whose hash it binds and appends the unbound claims of the textures it
    // removes - so a set that has been bound into is missing the bound
    // hashes and holds the preserved ones at the end, out of that order.
    // Nothing reads the order; resolve_material scans the whole list.
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

// Retail brake substitution: speed.exe 0x56A76B and 0x56A80A.
inline constexpr std::uint32_t kBrakeGlobalHash = 0x17F9F794U;
inline constexpr std::uint32_t kBrakeGlobalLeftHash = 0x85E9C79EU;

// Empty stock sponsor slots resolve to DEFAULTALPHA in retail and are skipped.
bool is_stock_dummy_decal_hash(std::uint32_t hash) noexcept;

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
// subset. Neither is a stock-car assembly: they name meshes directly and
// read no car state.
struct MeshSelection {
    std::vector<MeshSelector> selectors;
};

// all_meshes and explicit_subset are the two profiles resolve_mesh_selection
// produces from one geometry source. stock_car is built by a layer above
// this one, include/nfsu/stock_scene.h, out of the recovered stock part set,
// its per-LOD mesh hashes and the recovered placement; it is the only
// profile that names several geometry sources and the only one that carries
// instances.
enum class SelectionProfile { all_meshes, explicit_subset, stock_car };

// "all meshes", "explicit subset" or "stock car".
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
    // Payload bytes (RGBA8 or native DXT) of the textures copied into the scene.
    std::uint64_t max_texture_bytes = 128ULL * 1024ULL * 1024ULL;
    // Geometry sources a scene may be built from. The single-source
    // profiles always use one.
    std::size_t max_sources = 64;
    // Drawn copies of the selected meshes (prepared instances).
    std::uint64_t max_instances = 65536;
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
    // Which geometry source the mesh was copied from: a position in the
    // source list the profile was prepared from, and an index into
    // PreparedCarScene::source_mesh_counts. Always 0 for the two
    // single-source profiles.
    std::size_t source = 0;
    // Position in that source's GeometryReport::meshes.
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
    std::uint32_t material_key_hash = 0;
    CarShadingMaterial shading;
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

// One drawn copy of a prepared mesh, with the transform that places it.
// Only the stock-car profile produces instances; the two single-source
// profiles name meshes and leave placement to the backend, so their
// instance list is empty and each of their meshes is drawn once in its own
// space, exactly as before this type existed.
//
// This layer places nothing and interprets nothing: the transform is
// carried as given and the four provenance fields are opaque numbers here,
// the way texture source, pack and record are. What they mean, and which
// retail rule produced each transform, is in include/nfsu/stock_scene.h.
struct PreparedInstance {
    // Index into PreparedCarScene::meshes.
    std::size_t mesh = 0;
    // Row-major 4x4, the row-vector convention v' = v * transform that
    // nfsu::multiply_placement_matrix uses, in the geometry file's raw slot
    // frame: component 0 is longitudinal (MeshVertex::px), 1 is lateral
    // (MeshVertex::pz) and 2 is vertical (MeshVertex::py). The identity
    // means the mesh is drawn at the car's own matrix.
    float transform[16] = {1.0F, 0.0F, 0.0F, 0.0F,
                           0.0F, 1.0F, 0.0F, 0.0F,
                           0.0F, 0.0F, 1.0F, 0.0F,
                           0.0F, 0.0F, 0.0F, 1.0F};
    // False when the profile could not derive the transform the retail draw
    // path applies and placed the instance from a documented stand-in
    // instead. A backend that must not show an unverified position skips
    // these; nothing else in this layer reads the flag.
    bool placement_exact = true;
    // Profile provenance: the car part slot, the LOD index of the model,
    // the 0-based copy within the slot and the mesh hash the slot resolved
    // to.
    std::uint32_t part_slot = 0;
    std::uint32_t part_lod = 0;
    std::uint32_t copy = 0;
    std::uint32_t mesh_hash = 0;
    // Retail left brake substitutes BRAKE_GLOBAL with BRAKE_GLOBAL_LEFT.
    // Handle of that prepared texture for stock brake copies 0 and 3.
    std::size_t brake_left_texture = kNoHandle;
    // Wheel/brake rotation pivot in car space. Zero for other parts.
    float animation_pivot[3] = {};
    // Race-only visual lamp treatment: 0 off, 1 head, 2 tail, 3 brake.
    // Stock scenes leave it zero; it is not vehicle or replay state.
    std::uint8_t lamp_mode = 0;
};

// A self-contained, validated draw list. It owns every byte a backend
// reads, so the GeometryReport and RenderTextureSet it was prepared from
// may change or be destroyed afterwards without affecting it. Fields are
// public; backends call validate_prepared_scene (render_scene does) and
// refuse a scene that was changed into an inconsistent state.
struct PreparedCarScene {
    SelectionProfile profile = SelectionProfile::all_meshes;
    // The selectors as given; empty for all meshes and for the stock car.
    std::vector<MeshSelector> selectors;
    // GeometryReport::meshes.size() of each geometry source, in source
    // order. The two single-source profiles hold exactly one entry.
    std::vector<std::size_t> source_mesh_counts;
    // All meshes only: positions of the invalid meshes, ascending.
    std::vector<std::size_t> skipped_invalid_meshes;
    // Ascending (source, source_index).
    std::vector<PreparedMesh> meshes;
    // Mesh order, then record order.
    std::vector<PreparedDraw> draws;
    // Copies of the resolved textures, usable, in order of first use.
    std::vector<RenderTexture> textures;
    // Distinct hashes of unresolved groups (drawable or not), ascending.
    std::vector<std::uint32_t> unresolved_hashes;
    // Stock car only: the drawn copies, in the order the profile produced
    // them. Empty for every other profile, and every prepared mesh of a
    // stock car is named by at least one of them.
    std::vector<PreparedInstance> instances;
};

// One selected mesh of a scene that may name several geometry sources.
struct SceneMeshRef {
    // Position in the source list the scene is prepared from.
    std::size_t source = 0;
    // Position in that source's GeometryReport::meshes.
    std::size_t index = 0;
};

// The profile-independent half of preparing a scene: checks the counts
// against the limits, copies the named meshes in the order given, resolves
// every material group once and copies each resolved texture once.
//
// `sources` holds the loaded geometry sources in the order the profile
// numbers them, and none of its entries may be null. `meshes` must be
// strictly ascending in (source, index), must stay inside `sources` and
// must name only valid meshes. `selected_by` is either empty or holds one
// selector position per entry of `meshes`.
//
// On success the result carries the meshes, draws, textures, unresolved
// hashes and source mesh counts, and nothing else: the profile is left at
// all_meshes with no selectors, no skipped meshes and no instances, because
// only the caller knows which profile it is building. A caller therefore
// fills those fields and calls validate_prepared_scene itself; that is what
// prepare_car_scene and prepare_stock_car_scene do. On failure `scene` is
// left empty. std::bad_alloc is caught.
bool prepare_scene_meshes(
    const std::vector<const GeometryReport*>& sources,
    const std::vector<SceneMeshRef>& meshes,
    const std::vector<std::size_t>& selected_by,
    const RenderTextureSet& textures,
    const SceneLimits& limits,
    PreparedCarScene& scene,
    std::string& error);

// Prepares a single-source scene: resolves the selection, copies the
// selected meshes, resolves every material group once and copies each
// resolved texture once. Fails, with error set and scene left empty, on a
// selection error, a limit, an allocation failure or a failed self-check;
// std::bad_alloc is caught.
bool prepare_car_scene(
    const GeometryReport& geometry,
    const RenderTextureSet& textures,
    const MeshSelection& selection,
    const SceneLimits& limits,
    PreparedCarScene& scene,
    std::string& error);

// Checks every structural invariant a backend relies on: profile coverage
// and semantic selectors, ascending unique meshes inside their sources, one
// ordered draw per material, raw-record agreement, derived flags and hashes,
// binding and texture provenance, first-use texture handles, texture sizes,
// a drawn triangle total below 2^32 - 1 both per group and over every
// instance copy of it, and instances that name a prepared mesh with a
// finite transform and leave no prepared mesh undrawn. Vertex indices
// inside triangles are checked by backends per triangle.
bool validate_prepared_scene(const PreparedCarScene& scene, std::string& error);

// The drawn copies of every prepared mesh, in one table. The copies of
// prepared mesh m are order[k] for k in [first[m], first[m + 1]), each an
// index into PreparedCarScene::instances, ascending. first holds
// meshes.size() + 1 entries and order holds one entry per instance.
//
// A profile that carries no instances gets an empty list for every mesh,
// which a backend reads as the rule PreparedInstance documents: the mesh is
// drawn once, in its own space, exactly as it was before instances existed.
// That is not the same as the identity transform reaching the vertex
// arithmetic, and a backend must keep the two apart to stay byte-identical
// with builds that predate instances.
struct PreparedMeshInstances {
    std::vector<std::uint32_t> first;
    std::vector<std::uint32_t> order;
    // False when the scene carried no instance at all, which is what makes
    // copies() answer 1 rather than 0 below.
    bool instanced = false;

    // Drawn copies of prepared mesh `mesh`: the instances that name it, or
    // 1 for a scene with none. 0 for a handle outside the prepared meshes.
    //
    // This is the single rule for how many times a mesh is drawn. Nothing
    // may restate it: render_scene submits this many copies of each group,
    // prepared_triangle_ranges sizes each id range by it and
    // prepared_draw_triangles totals it, so two of them disagreeing would
    // put a pixel's id on the wrong material group.
    std::uint64_t copies(std::size_t mesh) const noexcept;
};

// Builds that table. Fails on a scene with 2^32 - 1 or more instances, on
// an instance whose mesh handle is outside the prepared meshes, and on an
// allocation failure; both members are left empty then. It does not
// validate the rest of the scene: validate_prepared_scene belongs before
// it.
bool prepared_mesh_instances(
    const PreparedCarScene& scene,
    PreparedMeshInstances& instances,
    std::string& error);

// Triangles over all drawable ranges, counting one copy per instance of
// the drawing mesh: a group of a mesh with four instances submits its
// triangles four times. A profile with no instances draws each mesh once,
// so the total is unchanged there. Only meaningful for a valid scene.
//
// Returns UINT64_MAX when the total overflows and when the per-mesh
// instance counts could not be allocated. Both answers are above every
// limit a caller checks it against, so a caller refuses in either case.
std::uint64_t prepared_draw_triangles(const PreparedCarScene& scene) noexcept;

// The order a backend submits the prepared material groups in, as indices
// into PreparedCarScene::draws.
//
// With retail_order false it is the prepared order 0, 1, 2, ...: mesh order
// and then material record order. With retail_order true it is the order
// the retail draw queue produces.
//
// The retail queue is a fixed table of buckets indexed by a row taken from
// the bound texture and a column taken from the effect. Every material of
// one car uses the same effect, so the column is constant and the row alone
// selects the bucket. Inside a bucket the queue holds one entry per
// texture, placed where that texture was first used, and each entry holds
// its materials in record order; the table is then walked by ascending row
// and drained in place. Ordering the prepared draws by (row, first use of
// the texture) with a stable sort reproduces that exactly. A group with no
// resolved texture has no row of its own and takes row 0, the row a record
// with both bytes zero selects.
//
// layers, when not null, receives the draw queue row of every draw, indexed
// by prepared position and not by position in order. Every row is 0 with
// retail_order false, which assigns no rows.
//
// The scene is read as given, so validate_prepared_scene belongs before it.
// It fails only on a scene with 2^32 - 1 or more groups and on an
// allocation failure, and leaves both outputs empty then.
bool prepared_draw_order(
    const PreparedCarScene& scene,
    bool retail_order,
    std::vector<std::uint32_t>& order,
    std::vector<unsigned char>* layers,
    std::string& error);

// The triangle ids of one drawable group. A backend that submits the groups
// in the order above, numbering every triangle it submits from 0, gives the
// triangles of the group at prepared position draw the ids first_id ..
// first_id + count - 1. The CPU renderer does exactly that and writes the
// id of the owning triangle into Frame::ids(), so a pixel's id locates the
// group that drew it.
//
// One group submits one copy per instance of its mesh, the copies in
// ascending instance order and the group's triangles in index order inside
// each copy, so count is the group's triangles times that instance count
// and the whole group still occupies one contiguous id range. A pixel's id
// therefore locates the group directly and the copy as
// (id - first_id) / (count / instances).
struct PreparedTriangleRange {
    std::uint32_t first_id = 0;
    std::uint32_t count = 0;
    std::uint32_t draw = 0;
};

// One range per drawable group, in submission order, so first_id ascends
// and the ranges are contiguous from 0. A group that is not drawable
// submits nothing and gets no range, and neither does a group whose mesh
// has no instance to draw it. Fails when order is not a permutation of the
// scene's groups, when the instance table cannot be built, when the
// drawable groups hold 2^32 - 1 or more triangles over all their copies,
// and on an allocation failure; ranges is left empty then.
bool prepared_triangle_ranges(
    const PreparedCarScene& scene,
    const std::vector<std::uint32_t>& order,
    std::vector<PreparedTriangleRange>& ranges,
    std::string& error);

// The prepared position of the group that submitted triangle id, or
// kNoHandle when no range holds it. Binary search over ranges exactly as
// prepared_triangle_ranges leaves them; any other order gives a wrong
// answer rather than an error.
std::size_t find_prepared_triangle(
    const std::vector<PreparedTriangleRange>& ranges,
    std::uint32_t id) noexcept;

} // namespace nfsu
