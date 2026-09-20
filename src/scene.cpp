#include "nfsu/scene.h"

#include "nfsu/geometry.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace nfsu {

namespace {

// Printable ASCII with quote and backslash escaped as \xHH.
std::string quoted(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte >= 0x20U && byte <= 0x7EU && ch != '"' && ch != '\\') {
            out += ch;
        } else {
            out += "\\x";
            out += kHex[byte >> 4U];
            out += kHex[byte & 0x0FU];
        }
    }
    return out + "\"";
}

std::string describe(const MeshSelector& selector, std::size_t position) {
    std::string text = "selector " + std::to_string(position) + " (";
    if (selector.kind == MeshSelectorKind::index) {
        text += "index " + std::to_string(selector.index);
    } else if (selector.kind == MeshSelectorKind::name) {
        text += "name " + quoted(selector.name);
    } else if (selector.kind == MeshSelectorKind::hash) {
        static const char kHex[] = "0123456789ABCDEF";
        text += "hash 0x";
        for (int shift = 28; shift >= 0; shift -= 4) {
            text += kHex[(selector.hash >> shift) & 0x0FU];
        }
    } else {
        text += "unknown";
    }
    return text + ")";
}

bool printable_name(const std::string& name) {
    for (const char ch : name) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U || byte > 0x7EU) {
            return false;
        }
    }
    return true;
}

// a + b into sum unless it would exceed limit.
bool add_within(std::uint64_t a, std::uint64_t b, std::uint64_t limit,
                std::uint64_t& sum) noexcept {
    if (a > limit || b > limit - a) {
        return false;
    }
    sum = a + b;
    return true;
}

// True if the range [first, first + count) is a multiple of 3 inside an
// index stream of triangles * 3 entries.
bool range_ok(std::uint64_t first, std::uint64_t count,
              std::uint64_t triangles) noexcept {
    const std::uint64_t total = triangles * 3U;  // triangles < 2^62 here
    return first % 3 == 0 && count % 3 == 0 && first <= total &&
           count <= total - first;
}

std::uint32_t read_u32_le(const unsigned char* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::uint32_t float_bits(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool raw_matches_draw(const PreparedDraw& draw) noexcept {
    if (read_u32_le(draw.raw.data() + 12) != draw.index_count ||
        read_u32_le(draw.raw.data() + 28) != draw.texture_slot ||
        read_u32_le(draw.raw.data() + 32) != draw.material_key_slot ||
        read_u32_le(draw.raw.data() + 52) != draw.first_index ||
        read_u32_le(draw.raw.data() + 56) != draw.flags) {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (read_u32_le(draw.raw.data() + i * 4U) !=
                float_bits(draw.bounds_min[i]) ||
            read_u32_le(draw.raw.data() + 16U + i * 4U) !=
                float_bits(draw.bounds_max[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool RenderTexture::claims_hash(std::uint32_t value) const noexcept {
    if (hash == value) {
        return true;
    }
    const std::size_t used = std::min(claim_count, claims.size());
    return std::find(claims.begin(), claims.begin() + used, value) !=
           claims.begin() + used;
}

bool RenderTexture::usable() const noexcept {
    // Both factors are below 2^32, so pixels cannot overflow.
    const std::uint64_t pixels = std::uint64_t{width} * height;
    return valid && pixels > 0 && pixels <= UINT64_MAX / 4U &&
           std::uint64_t{rgba.size()} == pixels * 4U;
}

bool render_alpha_test_enabled(std::uint8_t alpha_test_mode) noexcept {
    return alpha_test_mode == 1U;
}

int render_draw_layer(std::uint8_t layer_group, std::uint8_t layer_order) noexcept {
    const int layer = (layer_group != 0U ? 8 : 0) +
                      static_cast<int>(static_cast<std::int8_t>(layer_order));
    if (layer < 0) {
        return 0;
    }
    return layer < kRenderDrawLayerCount ? layer : kRenderDrawLayerCount - 1;
}

RenderBlendMode render_blend_mode_of(std::uint8_t alpha_blend_mode) noexcept {
    if (alpha_blend_mode == 0U) {
        return RenderBlendMode::disabled;
    }
    if (alpha_blend_mode == 2U) {
        return RenderBlendMode::additive;
    }
    return RenderBlendMode::source_over;
}

const char* render_blend_mode_name(RenderBlendMode mode) noexcept {
    switch (mode) {
    case RenderBlendMode::disabled: return "disabled";
    case RenderBlendMode::source_over: return "source-over";
    case RenderBlendMode::additive: return "additive";
    }
    return "unknown";
}

MaterialCoefficientGates material_coefficient_gates(
    std::uint32_t flags, bool environment_enabled) noexcept {
    // The traced upload zeros whole coefficient vectors; it never mixes
    // groups. Flag bits are used only as the branch conditions the retail
    // code tests, and are given no name here.
    constexpr std::uint32_t kEnvironmentBranchBit = 0x00000100U;
    constexpr std::uint32_t kLightingBranchBit = 0x00000080U;
    MaterialCoefficientGates gates;
    if ((flags & kEnvironmentBranchBit) == 0U || !environment_enabled) {
        gates.environment = false;
        return gates;
    }
    if ((flags & kLightingBranchBit) == 0U) {
        gates.diffuse = false;
        gates.specular = false;
    }
    return gates;
}

const char* binding_kind_name(BindingKind kind) noexcept {
    switch (kind) {
    case BindingKind::resolved: return "resolved";
    case BindingKind::unresolved: return "unresolved";
    case BindingKind::unusable: return "unusable";
    case BindingKind::invalid_slot: return "invalid_slot";
    }
    return "unknown";
}

const char* mesh_selector_kind_name(MeshSelectorKind kind) noexcept {
    switch (kind) {
    case MeshSelectorKind::index: return "index";
    case MeshSelectorKind::name: return "name";
    case MeshSelectorKind::hash: return "hash";
    }
    return "unknown";
}

const char* selection_profile_name(SelectionProfile profile) noexcept {
    switch (profile) {
    case SelectionProfile::all_meshes: return "all meshes";
    case SelectionProfile::explicit_subset: return "explicit subset";
    }
    return "unknown";
}

MaterialBinding resolve_material(
    const Mesh& mesh,
    const MeshMaterial& material,
    const RenderTextureSet& textures) noexcept {
    return resolve_material(mesh.texture_hashes, material, textures);
}

MaterialBinding resolve_material(
    const std::vector<std::uint32_t>& texture_hashes,
    const MeshMaterial& material,
    const RenderTextureSet& textures) noexcept {
    MaterialBinding binding;
    if (material.texture_slot >= texture_hashes.size()) {
        binding.kind = BindingKind::invalid_slot;
        return binding;
    }
    binding.hash = texture_hashes[material.texture_slot];
    std::size_t matches = 0;
    std::size_t usable = 0;
    for (std::size_t i = 0; i < textures.textures.size(); ++i) {
        const RenderTexture& texture = textures.textures[i];
        if (!texture.claims_hash(binding.hash)) {
            continue;
        }
        ++matches;
        if (texture.usable()) {
            ++usable;
            binding.texture_index = i;
        }
    }
    for (const UnusableClaim& claim : textures.unusable_claims) {
        if (claim.hash == binding.hash) {
            ++matches;
            break;
        }
    }
    if (matches == 0) {
        binding.kind = BindingKind::unresolved;
    } else if (matches == 1 && usable == 1) {
        binding.kind = BindingKind::resolved;
    } else {
        binding.kind = BindingKind::unusable;
        binding.texture_index = 0;
    }
    return binding;
}

bool resolve_mesh_selection(
    const GeometryReport& geometry,
    const MeshSelection& selection,
    const SceneLimits& limits,
    std::vector<std::size_t>& meshes,
    std::vector<std::size_t>* selected_by,
    std::string& error) {
    meshes.clear();
    if (selected_by != nullptr) {
        selected_by->clear();
    }
    const std::vector<MeshSelector>& selectors = selection.selectors;
    if (selectors.size() > limits.max_selectors) {
        error = std::to_string(selectors.size()) + " mesh selectors; the limit is " +
                std::to_string(limits.max_selectors);
        return false;
    }
    std::uint64_t text = 0;
    for (const MeshSelector& selector : selectors) {
        if (!add_within(text, selector.name.size(), limits.max_selector_text_bytes,
                        text)) {
            error = "mesh selector names exceed " +
                    std::to_string(limits.max_selector_text_bytes) + " bytes";
            return false;
        }
    }
    try {
        std::vector<std::size_t> result;
        std::vector<std::size_t> by;
        if (selectors.empty()) {
            for (std::size_t i = 0; i < geometry.meshes.size(); ++i) {
                if (geometry.meshes[i].valid) {
                    result.push_back(i);
                }
            }
            meshes = std::move(result);
            return true;
        }
        // Mesh position -> selector position; ordered, so iteration gives
        // source order whatever the selector order.
        std::map<std::size_t, std::size_t> chosen;
        for (std::size_t s = 0; s < selectors.size(); ++s) {
            const MeshSelector& selector = selectors[s];
            std::size_t found = 0;
            if (selector.kind == MeshSelectorKind::index) {
                if (!selector.name.empty() || selector.hash != 0) {
                    error = describe(selector, s) +
                            ": an index selector must not carry a name or hash";
                    return false;
                }
                if (selector.index >= geometry.meshes.size()) {
                    error = describe(selector, s) + ": mesh index is out of range (" +
                            std::to_string(geometry.meshes.size()) + " meshes)";
                    return false;
                }
                found = static_cast<std::size_t>(selector.index);
            } else if (selector.kind == MeshSelectorKind::name) {
                if (selector.index != 0 || selector.hash != 0) {
                    error = describe(selector, s) +
                            ": a name selector must not carry an index or hash";
                    return false;
                }
                if (selector.name.empty()) {
                    error = describe(selector, s) + ": mesh name is empty";
                    return false;
                }
                if (selector.name.size() > kMeshNameSize ||
                    !printable_name(selector.name)) {
                    error = describe(selector, s) +
                            ": mesh name must be 1.." + std::to_string(kMeshNameSize) +
                            " printable ASCII bytes";
                    return false;
                }
                std::size_t matches = 0;
                std::size_t second = 0;
                for (std::size_t i = 0; i < geometry.meshes.size(); ++i) {
                    if (geometry.meshes[i].name == selector.name) {
                        if (matches == 0) {
                            found = i;
                        } else if (matches == 1) {
                            second = i;
                        }
                        ++matches;
                    }
                }
                if (matches == 0) {
                    error = describe(selector, s) + ": no mesh has this name";
                    return false;
                }
                if (matches > 1) {
                    error = describe(selector, s) + ": name is ambiguous, " +
                            std::to_string(matches) + " meshes have it (first " +
                            std::to_string(found) + " and " + std::to_string(second) +
                            ")";
                    return false;
                }
            } else if (selector.kind == MeshSelectorKind::hash) {
                if (selector.index != 0 || !selector.name.empty()) {
                    error = describe(selector, s) +
                            ": a hash selector must not carry an index or name";
                    return false;
                }
                if (!geometry.mesh_directory_valid) {
                    error = describe(selector, s) +
                            ": verified mesh hash directory is unavailable";
                    return false;
                }
                std::size_t matches = 0;
                std::size_t second = 0;
                for (std::size_t i = 0; i < geometry.meshes.size(); ++i) {
                    const Mesh& mesh = geometry.meshes[i];
                    if (!mesh.has_directory_entry) {
                        error = describe(selector, s) +
                                ": verified mesh hash directory is incomplete";
                        return false;
                    }
                    if (mesh.directory_hash == selector.hash) {
                        if (matches == 0) {
                            found = i;
                        } else if (matches == 1) {
                            second = i;
                        }
                        ++matches;
                    }
                }
                if (matches == 0) {
                    error = describe(selector, s) +
                            ": no mesh has this directory hash";
                    return false;
                }
                if (matches > 1) {
                    error = describe(selector, s) + ": hash is ambiguous, " +
                            std::to_string(matches) + " meshes have it (first " +
                            std::to_string(found) + " and " +
                            std::to_string(second) + ")";
                    return false;
                }
            } else {
                error = describe(selector, s) + ": unknown selector kind";
                return false;
            }
            if (!geometry.meshes[found].valid) {
                error = describe(selector, s) + ": mesh " + std::to_string(found) +
                        " is invalid and cannot be drawn";
                return false;
            }
            const auto inserted = chosen.emplace(found, s);
            if (!inserted.second) {
                error = describe(selector, s) + ": mesh " + std::to_string(found) +
                        " is already selected by selector " +
                        std::to_string(inserted.first->second);
                return false;
            }
        }
        result.reserve(chosen.size());
        by.reserve(chosen.size());
        for (const auto& entry : chosen) {
            result.push_back(entry.first);
            by.push_back(entry.second);
        }
        meshes = std::move(result);
        if (selected_by != nullptr) {
            *selected_by = std::move(by);
        }
    } catch (const std::bad_alloc&) {
        meshes.clear();
        if (selected_by != nullptr) {
            selected_by->clear();
        }
        error = "out of memory while resolving the mesh selection";
        return false;
    }
    return true;
}

bool prepare_car_scene(
    const GeometryReport& geometry,
    const RenderTextureSet& textures,
    const MeshSelection& selection,
    const SceneLimits& limits,
    PreparedCarScene& scene,
    std::string& error) {
    scene = PreparedCarScene{};
    std::vector<std::size_t> selected;
    std::vector<std::size_t> selected_by;
    if (!resolve_mesh_selection(geometry, selection, limits, selected, &selected_by,
                                error)) {
        return false;
    }
    const bool subset = !selection.selectors.empty();

    // Every count is checked before the copies are allocated.
    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    std::uint64_t draws = 0;
    for (const std::size_t index : selected) {
        const Mesh& mesh = geometry.meshes[index];
        if (!add_within(vertices, mesh.vertices.size(), limits.max_vertices, vertices)) {
            error = "selected meshes have more than " +
                    std::to_string(limits.max_vertices) + " vertices";
            return false;
        }
        if (!add_within(triangles, mesh.triangles.size(), limits.max_triangles,
                        triangles)) {
            error = "selected meshes have more than " +
                    std::to_string(limits.max_triangles) + " triangles";
            return false;
        }
        if (!add_within(draws, mesh.materials.size(), limits.max_draws, draws)) {
            error = "selected meshes have more than " +
                    std::to_string(limits.max_draws) + " material groups";
            return false;
        }
    }

    PreparedCarScene result;
    try {
        result.profile = subset ? SelectionProfile::explicit_subset
                                : SelectionProfile::all_meshes;
        result.selectors = selection.selectors;
        result.source_mesh_count = geometry.meshes.size();
        if (!subset) {
            for (std::size_t i = 0; i < geometry.meshes.size(); ++i) {
                if (!geometry.meshes[i].valid) {
                    result.skipped_invalid_meshes.push_back(i);
                }
            }
        }
        result.meshes.reserve(selected.size());
        result.draws.reserve(static_cast<std::size_t>(draws));
        // RenderTextureSet index -> scene texture handle.
        std::map<std::size_t, std::size_t> handles;
        std::set<std::uint32_t> unresolved;
        std::uint64_t texture_bytes = 0;
        std::uint64_t draw_triangles = 0;
        for (std::size_t k = 0; k < selected.size(); ++k) {
            const Mesh& mesh = geometry.meshes[selected[k]];
            const std::size_t mesh_handle = result.meshes.size();
            result.meshes.emplace_back();
            PreparedMesh& prepared = result.meshes.back();
            prepared.source_index = selected[k];
            prepared.name = mesh.name;
            prepared.directory_hash = mesh.directory_hash;
            prepared.directory_offset = mesh.directory_offset;
            prepared.directory_size = mesh.directory_size;
            prepared.has_directory_entry = mesh.has_directory_entry;
            prepared.selector = subset ? selected_by[k] : kNoHandle;
            prepared.material_count = mesh.materials.size();
            prepared.vertices = mesh.vertices;
            prepared.triangles = mesh.triangles;
            for (std::size_t m = 0; m < mesh.materials.size(); ++m) {
                const MeshMaterial& material = mesh.materials[m];
                const MaterialBinding binding = resolve_material(mesh, material, textures);
                PreparedDraw draw;
                draw.mesh = mesh_handle;
                draw.material_index = m;
                draw.first_index = material.first_index;
                draw.index_count = material.index_count;
                draw.texture_slot = material.texture_slot;
                draw.texture_hash = binding.hash;
                draw.binding = binding.kind;
                draw.material_key_slot = material.material_key_slot;
                draw.flags = material.flags;
                draw.record_offset = material.offset;
                std::copy(std::begin(material.bounds_min), std::end(material.bounds_min),
                          draw.bounds_min);
                std::copy(std::begin(material.bounds_max), std::end(material.bounds_max),
                          draw.bounds_max);
                std::memcpy(draw.raw.data(), material.raw, kMaterialRecordSize);
                draw.drawable = binding.kind != BindingKind::invalid_slot &&
                                range_ok(material.first_index, material.index_count,
                                         mesh.triangles.size());
                if (binding.kind == BindingKind::unresolved) {
                    unresolved.insert(binding.hash);
                }
                if (draw.drawable &&
                    !add_within(draw_triangles, material.index_count / 3U,
                                limits.max_draw_triangles, draw_triangles)) {
                    error = "material groups draw more than " +
                            std::to_string(limits.max_draw_triangles) + " triangles";
                    return false;
                }
                if (binding.kind == BindingKind::resolved) {
                    if (binding.texture_index >= textures.textures.size()) {
                        error = "resolved texture index is outside the texture set";
                        return false;
                    }
                    const RenderTexture& texture = textures.textures[binding.texture_index];
                    auto found = handles.find(binding.texture_index);
                    if (found == handles.end()) {
                        const std::uint64_t bytes = texture.rgba.size();
                        if (!add_within(texture_bytes, bytes, limits.max_texture_bytes,
                                        texture_bytes)) {
                            error = "prepared textures would exceed " +
                                    std::to_string(limits.max_texture_bytes) + " bytes";
                            return false;
                        }
                        found = handles.emplace(binding.texture_index,
                                                result.textures.size()).first;
                        result.textures.push_back(texture);
                    }
                    draw.texture = found->second;
                    draw.texture_source = texture.source;
                    draw.texture_pack = texture.pack;
                    draw.texture_record = texture.record;
                }
                result.draws.push_back(draw);
            }
        }
        result.unresolved_hashes.assign(unresolved.begin(), unresolved.end());
    } catch (const std::bad_alloc&) {
        error = "out of memory while preparing the scene";
        return false;
    }
    if (!validate_prepared_scene(result, error)) {
        error = "prepared scene failed its self-check: " + error;
        return false;
    }
    scene = std::move(result);
    return true;
}

std::uint64_t prepared_draw_triangles(const PreparedCarScene& scene) noexcept {
    std::uint64_t total = 0;
    for (const PreparedDraw& draw : scene.draws) {
        if (draw.drawable) {
            const std::uint64_t add = draw.index_count / 3U;
            total = add > UINT64_MAX - total ? UINT64_MAX : total + add;
        }
    }
    return total;
}

namespace {

bool validate_scene(const PreparedCarScene& scene, std::string& error) {
    const bool subset = scene.profile == SelectionProfile::explicit_subset;
    if (!subset && scene.profile != SelectionProfile::all_meshes) {
        error = "unknown selection profile";
        return false;
    }
    if (subset != !scene.selectors.empty()) {
        error = "selection profile does not match the selector list";
        return false;
    }
    if (subset && !scene.skipped_invalid_meshes.empty()) {
        error = "an explicit subset skips no meshes";
        return false;
    }
    if (scene.meshes.size() > scene.source_mesh_count ||
        scene.skipped_invalid_meshes.size() >
            scene.source_mesh_count - scene.meshes.size()) {
        error = "more prepared meshes than source meshes";
        return false;
    }
    if (!subset && scene.skipped_invalid_meshes.size() !=
                       scene.source_mesh_count - scene.meshes.size()) {
        error = "the all-meshes profile does not cover every source mesh";
        return false;
    }
    // Both lists ascending and disjoint, all inside the source.
    for (std::size_t i = 0; i < scene.skipped_invalid_meshes.size(); ++i) {
        const std::size_t index = scene.skipped_invalid_meshes[i];
        if (index >= scene.source_mesh_count ||
            (i > 0 && index <= scene.skipped_invalid_meshes[i - 1])) {
            error = "skipped mesh list is not ascending inside the source";
            return false;
        }
    }
    std::vector<bool> selector_used(scene.selectors.size(), false);
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        const PreparedMesh& mesh = scene.meshes[i];
        if (mesh.source_index >= scene.source_mesh_count ||
            (i > 0 && mesh.source_index <= scene.meshes[i - 1].source_index)) {
            error = "prepared mesh " + std::to_string(i) +
                    " is not in ascending source order inside the source";
            return false;
        }
        if ((!mesh.has_directory_entry &&
             (mesh.directory_hash != 0 || mesh.directory_offset != 0 ||
              mesh.directory_size != 0)) ||
            (mesh.has_directory_entry &&
             (mesh.directory_size < 8 ||
              mesh.directory_offset > UINT64_MAX - mesh.directory_size))) {
            error = "prepared mesh " + std::to_string(i) +
                    " has invalid mesh directory provenance";
            return false;
        }
        if (std::binary_search(scene.skipped_invalid_meshes.begin(),
                               scene.skipped_invalid_meshes.end(), mesh.source_index)) {
            error = "prepared mesh " + std::to_string(i) + " is also listed as skipped";
            return false;
        }
        if (subset) {
            if (mesh.selector >= scene.selectors.size() ||
                selector_used[mesh.selector]) {
                error = "prepared mesh " + std::to_string(i) +
                        " has a missing or repeated selector";
                return false;
            }
            const MeshSelector& selector = scene.selectors[mesh.selector];
            if (selector.kind == MeshSelectorKind::index) {
                if (selector.index != mesh.source_index || !selector.name.empty() ||
                    selector.hash != 0) {
                    error = "prepared mesh " + std::to_string(i) +
                            " does not match its index selector";
                    return false;
                }
            } else if (selector.kind == MeshSelectorKind::name) {
                if (selector.index != 0 || selector.name != mesh.name ||
                    selector.hash != 0 ||
                    selector.name.empty() || selector.name.size() > kMeshNameSize ||
                    !printable_name(selector.name)) {
                    error = "prepared mesh " + std::to_string(i) +
                            " does not match its name selector";
                    return false;
                }
            } else if (selector.kind == MeshSelectorKind::hash) {
                if (selector.index != 0 || !selector.name.empty() ||
                    !mesh.has_directory_entry ||
                    selector.hash != mesh.directory_hash) {
                    error = "prepared mesh " + std::to_string(i) +
                            " does not match its hash selector";
                    return false;
                }
            } else {
                error = "prepared mesh " + std::to_string(i) +
                        " has an unknown selector kind";
                return false;
            }
            selector_used[mesh.selector] = true;
        } else if (mesh.selector != kNoHandle) {
            error = "prepared mesh " + std::to_string(i) +
                    " names a selector in the all-meshes profile";
            return false;
        }
    }
    for (const bool used : selector_used) {
        if (!used) {
            error = "a selector selects no prepared mesh";
            return false;
        }
    }
    std::set<std::uint32_t> texture_claims;
    for (std::size_t i = 0; i < scene.textures.size(); ++i) {
        const RenderTexture& texture = scene.textures[i];
        if (!texture.usable()) {
            error = "prepared texture " + std::to_string(i) + " is not usable";
            return false;
        }
        texture_claims.insert(texture.hash);
        const std::size_t used = std::min(texture.claim_count, texture.claims.size());
        texture_claims.insert(texture.claims.begin(), texture.claims.begin() + used);
    }
    std::vector<std::size_t> material_counts(scene.meshes.size(), 0);
    std::vector<bool> texture_used(scene.textures.size(), false);
    std::size_t next_texture = 0;
    std::set<std::uint32_t> unresolved;
    struct CanonicalBinding {
        BindingKind kind = BindingKind::unresolved;
        std::size_t texture = kNoHandle;
        std::size_t source = 0;
        std::size_t pack = 0;
        std::size_t record = 0;
    };
    std::map<std::uint32_t, CanonicalBinding> bindings;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < scene.draws.size(); ++i) {
        const PreparedDraw& draw = scene.draws[i];
        const std::string label = "prepared draw " + std::to_string(i);
        if (draw.mesh >= scene.meshes.size()) {
            error = label + " has an invalid mesh handle";
            return false;
        }
        if (i > 0) {
            const PreparedDraw& previous = scene.draws[i - 1];
            if (draw.mesh < previous.mesh ||
                (draw.mesh == previous.mesh &&
                 draw.material_index <= previous.material_index)) {
                error = label + " is out of mesh and record order";
                return false;
            }
        }
        if (draw.material_index != material_counts[draw.mesh] ||
            draw.material_index >= scene.meshes[draw.mesh].material_count) {
            error = label + " is not the next material of its mesh";
            return false;
        }
        ++material_counts[draw.mesh];
        if (!raw_matches_draw(draw)) {
            error = label + " does not match its raw material record";
            return false;
        }
        switch (draw.binding) {
        case BindingKind::resolved:
        case BindingKind::unresolved:
        case BindingKind::unusable:
        case BindingKind::invalid_slot:
            break;
        default:
            error = label + " has an unknown binding kind";
            return false;
        }
        if (draw.binding == BindingKind::resolved) {
            if (draw.texture >= scene.textures.size()) {
                error = label + " has an invalid texture handle";
                return false;
            }
            const RenderTexture& texture = scene.textures[draw.texture];
            if (!texture.claims_hash(draw.texture_hash) ||
                texture.source != draw.texture_source ||
                texture.pack != draw.texture_pack ||
                texture.record != draw.texture_record) {
                error = label + " does not match its texture hash or provenance";
                return false;
            }
            if (!texture_used[draw.texture]) {
                if (draw.texture != next_texture) {
                    error = label + " violates texture first-use order";
                    return false;
                }
                texture_used[draw.texture] = true;
                ++next_texture;
            }
        } else {
            if (draw.texture != kNoHandle || draw.texture_source != 0 ||
                draw.texture_pack != 0 || draw.texture_record != 0) {
                error = label + " has texture provenance but is not resolved";
                return false;
            }
            if (draw.binding == BindingKind::invalid_slot && draw.texture_hash != 0) {
                error = label + " has a hash with an invalid texture slot";
                return false;
            }
        }
        if (draw.binding != BindingKind::invalid_slot) {
            const CanonicalBinding current{draw.binding, draw.texture,
                                           draw.texture_source, draw.texture_pack,
                                           draw.texture_record};
            const auto inserted = bindings.emplace(draw.texture_hash, current);
            const CanonicalBinding& canonical = inserted.first->second;
            if (!inserted.second &&
                (canonical.kind != current.kind ||
                 (current.kind == BindingKind::resolved &&
                  (canonical.texture != current.texture ||
                   canonical.source != current.source ||
                   canonical.pack != current.pack ||
                   canonical.record != current.record)))) {
                error = label + " disagrees with another draw for the same texture hash";
                return false;
            }
        }
        if (draw.binding == BindingKind::unresolved) {
            if (texture_claims.count(draw.texture_hash) != 0) {
                error = label + " is unresolved but a prepared texture claims its hash";
                return false;
            }
            unresolved.insert(draw.texture_hash);
        }
        const bool expected_drawable = draw.binding != BindingKind::invalid_slot &&
                                       range_ok(draw.first_index, draw.index_count,
                                                scene.meshes[draw.mesh].triangles.size());
        if (draw.drawable != expected_drawable) {
            error = label + " has an inconsistent drawable flag";
            return false;
        }
        if (!expected_drawable) {
            continue;
        }
        total += draw.index_count / 3U;  // each term < 2^31, count < 2^32 terms
        if (total >= 0xFFFFFFFFULL) {
            error = "prepared draws hold 2^32 - 1 or more triangles";
            return false;
        }
    }
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        if (material_counts[i] != scene.meshes[i].material_count) {
            error = "prepared mesh " + std::to_string(i) +
                    " does not have one draw per material";
            return false;
        }
    }
    if (next_texture != scene.textures.size()) {
        error = "a prepared texture is never referenced";
        return false;
    }
    if (scene.unresolved_hashes.size() != unresolved.size() ||
        !std::equal(scene.unresolved_hashes.begin(), scene.unresolved_hashes.end(),
                    unresolved.begin())) {
        error = "unresolved hash list does not match the prepared draws";
        return false;
    }
    return true;
}

} // namespace

bool validate_prepared_scene(const PreparedCarScene& scene, std::string& error) {
    try {
        return validate_scene(scene, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory while validating the prepared scene";
        return false;
    }
}

bool prepared_draw_order(
    const PreparedCarScene& scene,
    bool retail_order,
    std::vector<std::uint32_t>& order,
    std::vector<unsigned char>* layers,
    std::string& error) {
    order.clear();
    if (layers != nullptr) {
        layers->clear();
    }
    // order holds one u32 per group, so a scene with more groups than a u32
    // can index could not be ordered at all.
    if (scene.draws.size() >= 0xFFFFFFFFULL) {
        error = "scene has " + std::to_string(scene.draws.size()) +
                " material groups; too many to order";
        return false;
    }
    std::vector<unsigned char> layer;
    try {
        order.resize(scene.draws.size());
        layer.assign(scene.draws.size(), 0);
    } catch (const std::bad_alloc&) {
        error = "not enough memory for the draw order tables";
        order.clear();
        return false;
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<std::uint32_t>(i);
    }
    if (retail_order) {
        // See the declaration for what the retail queue does and why the
        // stable sort below reproduces it.
        std::vector<std::uint32_t> group;
        try {
            group.assign(scene.draws.size(), 0);
            std::map<std::pair<std::size_t, std::uint32_t>, std::uint32_t> first_use;
            std::uint32_t next_group = 0;
            for (std::size_t i = 0; i < scene.draws.size(); ++i) {
                const PreparedDraw& draw = scene.draws[i];
                std::pair<std::size_t, std::uint32_t> key{kNoHandle, draw.texture_hash};
                if (draw.binding == BindingKind::resolved &&
                    draw.texture < scene.textures.size()) {
                    const RenderTexture& texture = scene.textures[draw.texture];
                    layer[i] = static_cast<unsigned char>(
                        render_draw_layer(texture.layer_group, texture.layer_order));
                    key = {draw.texture, 0U};
                }
                const auto inserted = first_use.emplace(key, next_group);
                if (inserted.second) {
                    ++next_group;
                }
                group[i] = inserted.first->second;
            }
        } catch (const std::bad_alloc&) {
            error = "not enough memory for the draw order tables";
            order.clear();
            return false;
        }
        std::stable_sort(order.begin(), order.end(),
                         [&layer, &group](std::uint32_t a, std::uint32_t b) {
                             if (layer[a] != layer[b]) {
                                 return layer[a] < layer[b];
                             }
                             return group[a] < group[b];
                         });
    }
    if (layers != nullptr) {
        try {
            *layers = layer;
        } catch (const std::bad_alloc&) {
            error = "not enough memory for the draw order tables";
            order.clear();
            layers->clear();
            return false;
        }
    }
    return true;
}

bool prepared_triangle_ranges(
    const PreparedCarScene& scene,
    const std::vector<std::uint32_t>& order,
    std::vector<PreparedTriangleRange>& ranges,
    std::string& error) {
    ranges.clear();
    if (order.size() != scene.draws.size()) {
        error = "the draw order holds " + std::to_string(order.size()) +
                " entries for " + std::to_string(scene.draws.size()) +
                " material groups";
        return false;
    }
    std::vector<unsigned char> seen;
    try {
        seen.assign(scene.draws.size(), 0);
    } catch (const std::bad_alloc&) {
        error = "not enough memory for the triangle range table";
        return false;
    }
    std::uint64_t next_id = 0;
    try {
        for (const std::uint32_t d : order) {
            if (d >= scene.draws.size() || seen[d] != 0) {
                error = "the draw order is not a permutation of the material groups";
                ranges.clear();
                return false;
            }
            seen[d] = 1;
            const PreparedDraw& draw = scene.draws[d];
            if (!draw.drawable) {
                continue;
            }
            // A drawable group has an index count that is a multiple of
            // three; validate_prepared_scene has already checked it.
            const std::uint64_t count = draw.index_count / 3U;
            if (count == 0) {
                continue;
            }
            if (next_id + count >= 0xFFFFFFFFULL) {
                error = "prepared draws hold 2^32 - 1 or more triangles";
                ranges.clear();
                return false;
            }
            PreparedTriangleRange range;
            range.first_id = static_cast<std::uint32_t>(next_id);
            range.count = static_cast<std::uint32_t>(count);
            range.draw = d;
            ranges.push_back(range);
            next_id += count;
        }
    } catch (const std::bad_alloc&) {
        error = "not enough memory for the triangle range table";
        ranges.clear();
        return false;
    }
    return true;
}

std::size_t find_prepared_triangle(
    const std::vector<PreparedTriangleRange>& ranges,
    std::uint32_t id) noexcept {
    std::size_t low = 0;
    std::size_t high = ranges.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        const PreparedTriangleRange& range = ranges[middle];
        if (id < range.first_id) {
            high = middle;
        } else if (id - range.first_id >= range.count) {
            low = middle + 1;
        } else {
            return range.draw;
        }
    }
    return kNoHandle;
}

} // namespace nfsu
