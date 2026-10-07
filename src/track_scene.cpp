#include "nfsu/track_scene.h"

// For kFaceStride: the track index buffer has the car index layout.
#include "nfsu/geometry.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace nfsu {
namespace {

// value + more, refused instead of wrapped, and refused above limit.
bool add_within(
    std::uint64_t value,
    std::uint64_t more,
    std::uint64_t limit,
    std::uint64_t& out) {
    if (more > limit || value > limit - more) {
        return false;
    }
    out = value + more;
    return true;
}

// An index run is drawable only when it is a whole number of triangles
// inside the mesh index stream.
bool range_ok(
    std::uint32_t first_index,
    std::uint32_t index_count,
    std::size_t triangles) {
    if (index_count == 0 || index_count % 3U != 0 || first_index % 3U != 0) {
        return false;
    }
    const std::uint64_t total = static_cast<std::uint64_t>(triangles) * 3U;
    return static_cast<std::uint64_t>(first_index) + index_count <= total;
}

std::string hex32(std::uint32_t value) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text.push_back(kHex[(value >> shift) & 0x0FU]);
    }
    return text;
}

std::uint32_t read_u32_le(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint32_t float_bits(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// TrackDraw::raw is the untouched 0x00134B02 record and exists so that a
// backend can read the fields this project has not decoded. It must agree
// with every field that was decoded out of it, or the two disagree with no
// diagnostic. Mirrors raw_matches_draw in src/scene.cpp.
bool raw_matches_draw(const TrackDraw& draw) noexcept {
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

bool prepare(
    const TrackRegionReport& report,
    const RenderTextureSet& textures,
    const TrackSceneLimits& limits,
    PreparedTrackRegion& out,
    std::string& error,
    TrackRegionReport* owned_report = nullptr,
    RenderTextureSet* owned_textures = nullptr) {
    PreparedTrackRegion region;
    region.region = report.region;
    region.source_mesh_count = report.meshes.size();

    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    std::uint64_t draws = 0;
    std::uint64_t drawn_triangles = 0;
    std::set<std::uint32_t> unresolved;
    // Source texture index -> handle in region.textures, in first-use order.
    std::vector<std::size_t> handle_of(textures.textures.size(), kNoHandle);

    for (const TrackMesh& mesh : report.meshes) {
        if (!mesh.valid) {
            if (mesh.empty) {
                region.skipped_empty_meshes.push_back(mesh.index);
            } else {
                region.skipped_invalid_meshes.push_back(mesh.index);
            }
            continue;
        }
        if (!add_within(vertices, mesh.vertices.size(), limits.max_vertices,
                        vertices)) {
            error = "the region's meshes hold more than " +
                    std::to_string(limits.max_vertices) + " vertices";
            return false;
        }
        if (!add_within(triangles, mesh.triangles.size(), limits.max_triangles,
                        triangles)) {
            error = "the region's meshes hold more than " +
                    std::to_string(limits.max_triangles) + " triangles";
            return false;
        }
        if (!add_within(draws, mesh.materials.size(), limits.max_draws, draws)) {
            error = "the region's meshes hold more than " +
                    std::to_string(limits.max_draws) + " material groups";
            return false;
        }

        const std::size_t mesh_handle = region.meshes.size();
        region.meshes.emplace_back();
        PreparedTrackMesh& prepared = region.meshes.back();
        prepared.source_index = mesh.index;
        prepared.vertex_format_flags = mesh.vertex_format_flags;
        prepared.name = mesh.name;
        prepared.mesh_hash = mesh.mesh_hash;
        prepared.hash_record_offset = mesh.hash_record_offset;
        prepared.has_hash_record = mesh.has_hash_record;
        prepared.material_count = mesh.materials.size();
        if (!owned_report) {
            prepared.vertices = mesh.vertices;
            prepared.triangles = mesh.triangles;
        }

        for (std::size_t m = 0; m < mesh.materials.size(); ++m) {
            const MeshMaterial& material = mesh.materials[m];
            const MaterialBinding binding =
                resolve_material(mesh.texture_hashes, material, textures);
            TrackDraw draw;
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
            std::copy(std::begin(material.bounds_min),
                      std::end(material.bounds_min), draw.bounds_min);
            std::copy(std::begin(material.bounds_max),
                      std::end(material.bounds_max), draw.bounds_max);
            std::memcpy(draw.raw.data(), material.raw, kMaterialRecordSize);
            draw.drawable = binding.kind != BindingKind::invalid_slot &&
                            range_ok(material.first_index, material.index_count,
                                     mesh.triangles.size());
            if (binding.kind == BindingKind::unresolved) {
                unresolved.insert(binding.hash);
            }
            if (draw.drawable &&
                !add_within(drawn_triangles, material.index_count / 3U,
                            limits.max_draw_triangles, drawn_triangles)) {
                error = "the region's material groups draw more than " +
                        std::to_string(limits.max_draw_triangles) + " triangles";
                return false;
            }
            if (binding.kind == BindingKind::resolved) {
                if (binding.texture_index >= textures.textures.size()) {
                    error = "resolved binding names texture " +
                            std::to_string(binding.texture_index) +
                            " of a set with " +
                            std::to_string(textures.textures.size());
                    return false;
                }
                std::size_t handle = handle_of[binding.texture_index];
                if (handle == kNoHandle) {
                    const RenderTexture& source =
                        textures.textures[binding.texture_index];
                    const std::uint64_t bytes = source.payload().size();
                    std::uint64_t total = region.cost.texture_bytes;
                    if (!add_within(total, bytes, limits.max_texture_bytes,
                                    total)) {
                        error = "the region's textures need more than " +
                                std::to_string(limits.max_texture_bytes) +
                                " bytes";
                        return false;
                    }
                    handle = region.textures.size();
                    if (owned_textures) region.textures.emplace_back();
                    else region.textures.push_back(source);
                    handle_of[binding.texture_index] = handle;
                    region.cost.texture_bytes = total;
                }
                draw.texture = handle;
                const auto& source = textures.textures[binding.texture_index];
                draw.texture_source = source.source;
                draw.texture_pack = source.pack;
                draw.texture_record = source.record;
            }
            region.draws.push_back(std::move(draw));
        }
    }

    // Resolve every material before moving texture payloads: usable() must
    // still see the original bytes when a later mesh uses the same texture.
    if (owned_report) {
        std::size_t handle = 0;
        for (auto& mesh : owned_report->meshes) {
            if (!mesh.valid) continue;
            region.meshes[handle].vertices = std::move(mesh.vertices);
            region.meshes[handle].triangles = std::move(mesh.triangles);
            ++handle;
        }
    }
    if (owned_textures) {
        for (std::size_t i = 0; i < handle_of.size(); ++i) {
            if (handle_of[i] != kNoHandle) {
                region.textures[handle_of[i]] = std::move(owned_textures->textures[i]);
            }
        }
    }
    region.unresolved_hashes.assign(unresolved.begin(), unresolved.end());

    TrackDrawCost& cost = region.cost;
    cost.meshes = region.meshes.size();
    cost.vertices = vertices;
    cost.triangles = triangles;
    cost.vertex_buffer_bytes = vertices * kTrackVertexStride;
    cost.index_buffer_bytes = triangles * kFaceStride;
    cost.draws = region.draws.size();
    cost.drawn_triangles = drawn_triangles;
    cost.textures = region.textures.size();
    cost.unresolved_hashes = region.unresolved_hashes.size();

    // A backend that binds only on a change: kNoHandle - 1 is a sentinel
    // that no handle and no fallback can equal, so the first drawable draw
    // always counts as a bind.
    std::size_t bound = kNoHandle - 1;
    for (const TrackDraw& draw : region.draws) {
        switch (draw.binding) {
        case BindingKind::resolved: ++cost.resolved_draws; break;
        case BindingKind::unresolved: ++cost.unresolved_draws; break;
        case BindingKind::unusable: ++cost.unusable_draws; break;
        case BindingKind::invalid_slot: ++cost.invalid_slot_draws; break;
        }
        if (!draw.drawable) {
            continue;
        }
        ++cost.drawable_draws;
        const std::uint64_t group = draw.index_count / 3U;
        if (group > cost.largest_draw_triangles) {
            cost.largest_draw_triangles = group;
        }
        // Everything that is not resolved shares one fallback binding.
        const std::size_t wanted =
            draw.binding == BindingKind::resolved ? draw.texture : kNoHandle;
        if (wanted != bound) {
            ++cost.texture_binds;
            bound = wanted;
        }
    }

    if (!validate_prepared_track_region(region, error)) {
        return false;
    }
    out = std::move(region);
    return true;
}

} // namespace

bool prepare_track_region(
    const TrackRegionReport& report,
    const RenderTextureSet& textures,
    const TrackSceneLimits& limits,
    PreparedTrackRegion& region,
    std::string& error) {
    region = PreparedTrackRegion{};
    error.clear();
    bool ok = false;
    try {
        ok = prepare(report, textures, limits, region, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory while preparing the track region";
        ok = false;
    }
    if (!ok) {
        region = PreparedTrackRegion{};
        if (error.empty()) {
            error = "the track region could not be prepared";
        }
    }
    return ok;
}

bool prepare_track_region(
    TrackRegionReport&& report,
    RenderTextureSet&& textures,
    const TrackSceneLimits& limits,
    PreparedTrackRegion& region,
    std::string& error) {
    region = PreparedTrackRegion{};
    error.clear();
    bool ok = false;
    try {
        ok = prepare(report, textures, limits, region, error, &report, &textures);
    } catch (const std::bad_alloc&) {
        error = "out of memory while preparing the track region";
        ok = false;
    }
    if (!ok) {
        region = PreparedTrackRegion{};
        if (error.empty()) {
            error = "the track region could not be prepared";
        }
    }
    return ok;
}

bool validate_prepared_track_region(
    const PreparedTrackRegion& region,
    std::string& error) {
    error.clear();

    if (region.meshes.size() + region.skipped_invalid_meshes.size() +
            region.skipped_empty_meshes.size() !=
        region.source_mesh_count) {
        error = "prepared " + std::to_string(region.meshes.size()) +
                " meshes, skipped " +
                std::to_string(region.skipped_invalid_meshes.size()) +
                " invalid and " +
                std::to_string(region.skipped_empty_meshes.size()) +
                " empty of " + std::to_string(region.source_mesh_count);
        return false;
    }
    for (std::size_t i = 1; i < region.skipped_invalid_meshes.size(); ++i) {
        if (region.skipped_invalid_meshes[i - 1] >=
            region.skipped_invalid_meshes[i]) {
            error = "skipped invalid mesh positions are not ascending at " +
                    std::to_string(i);
            return false;
        }
    }
    for (std::size_t i = 1; i < region.skipped_empty_meshes.size(); ++i) {
        if (region.skipped_empty_meshes[i - 1] >=
            region.skipped_empty_meshes[i]) {
            error = "skipped empty mesh positions are not ascending at " +
                    std::to_string(i);
            return false;
        }
    }
    for (const std::size_t index : region.skipped_invalid_meshes) {
        if (index >= region.source_mesh_count) {
            error = "skipped invalid mesh " + std::to_string(index) +
                    " is outside the " +
                    std::to_string(region.source_mesh_count) +
                    " source meshes";
            return false;
        }
    }
    for (const std::size_t index : region.skipped_empty_meshes) {
        if (index >= region.source_mesh_count) {
            error = "skipped empty mesh " + std::to_string(index) +
                    " is outside the " +
                    std::to_string(region.source_mesh_count) +
                    " source meshes";
            return false;
        }
    }

    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    for (std::size_t i = 0; i < region.meshes.size(); ++i) {
        const PreparedTrackMesh& mesh = region.meshes[i];
        if (i != 0 && region.meshes[i - 1].source_index >= mesh.source_index) {
            error = "prepared mesh source positions are not ascending at " +
                    std::to_string(i);
            return false;
        }
        if (mesh.source_index >= region.source_mesh_count) {
            error = "prepared mesh " + std::to_string(i) +
                    " names source mesh " + std::to_string(mesh.source_index) +
                    " of " + std::to_string(region.source_mesh_count);
            return false;
        }
        if (std::binary_search(region.skipped_invalid_meshes.begin(),
                               region.skipped_invalid_meshes.end(),
                               mesh.source_index) ||
            std::binary_search(region.skipped_empty_meshes.begin(),
                               region.skipped_empty_meshes.end(),
                               mesh.source_index)) {
            error = "prepared mesh " + std::to_string(i) +
                    " is also listed as skipped";
            return false;
        }
        if (mesh.vertices.empty() || mesh.triangles.empty()) {
            error = "prepared mesh " + std::to_string(i) +
                    " has no vertices or no triangles";
            return false;
        }
        if (mesh.vertices.size() > 65536U) {
            error = "prepared mesh " + std::to_string(i) + " holds " +
                    std::to_string(mesh.vertices.size()) +
                    " vertices, more than a u16 index can address";
            return false;
        }
        const std::size_t count = mesh.vertices.size();
        for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
            const MeshTriangle& triangle = mesh.triangles[t];
            if (triangle.a >= count || triangle.b >= count ||
                triangle.c >= count) {
                error = "prepared mesh " + std::to_string(i) + " triangle " +
                        std::to_string(t) + " references a vertex outside 0.." +
                        std::to_string(count - 1);
                return false;
            }
        }
        vertices += mesh.vertices.size();
        triangles += mesh.triangles.size();
    }

    // Every hash the prepared textures answer to: an unresolved draw may
    // not name one of them.
    std::set<std::uint32_t> texture_claims;
    for (const RenderTexture& texture : region.textures) {
        texture_claims.insert(texture.hash);
        const std::size_t used =
            std::min(texture.claim_count, texture.claims.size());
        texture_claims.insert(texture.claims.begin(),
                              texture.claims.begin() +
                                  static_cast<std::ptrdiff_t>(used));
    }
    // One binding per texture hash: two draws that name the same hash must
    // have been resolved the same way, or a backend that caches by hash
    // gets two answers.
    struct CanonicalBinding {
        BindingKind kind = BindingKind::unresolved;
        std::size_t texture = kNoHandle;
        std::size_t source = 0;
        std::size_t pack = 0;
        std::size_t record = 0;
    };
    std::map<std::uint32_t, CanonicalBinding> bindings;
    std::set<std::uint32_t> unresolved_hashes;

    std::size_t draw = 0;
    std::uint64_t drawn_triangles = 0;
    std::uint64_t drawable = 0;
    std::uint64_t resolved = 0;
    std::uint64_t unresolved = 0;
    std::uint64_t unusable = 0;
    std::uint64_t invalid_slot = 0;
    std::uint64_t largest = 0;
    std::uint64_t binds = 0;
    std::size_t bound = kNoHandle - 1;
    std::size_t next_texture = 0;
    for (std::size_t i = 0; i < region.meshes.size(); ++i) {
        const PreparedTrackMesh& mesh = region.meshes[i];
        for (std::size_t m = 0; m < mesh.material_count; ++m) {
            if (draw >= region.draws.size()) {
                error = "prepared mesh " + std::to_string(i) +
                        " is missing draws for its " +
                        std::to_string(mesh.material_count) +
                        " material records";
                return false;
            }
            const TrackDraw& entry = region.draws[draw];
            if (entry.mesh != i || entry.material_index != m) {
                error = "draw " + std::to_string(draw) +
                        " is not mesh " + std::to_string(i) + " material " +
                        std::to_string(m);
                return false;
            }
            const bool drawable_now =
                entry.binding != BindingKind::invalid_slot &&
                range_ok(entry.first_index, entry.index_count,
                         mesh.triangles.size());
            if (entry.drawable != drawable_now) {
                error = "draw " + std::to_string(draw) +
                        " disagrees with its own index range";
                return false;
            }
            if (!raw_matches_draw(entry)) {
                error = "draw " + std::to_string(draw) +
                        " does not match its raw material record";
                return false;
            }
            if (entry.binding == BindingKind::resolved) {
                if (entry.texture >= region.textures.size()) {
                    error = "draw " + std::to_string(draw) +
                            " names texture " + std::to_string(entry.texture) +
                            " of " + std::to_string(region.textures.size());
                    return false;
                }
                if (entry.texture > next_texture) {
                    error = "draw " + std::to_string(draw) +
                            " uses texture " + std::to_string(entry.texture) +
                            " before texture " + std::to_string(next_texture) +
                            " was first used";
                    return false;
                }
                if (entry.texture == next_texture) {
                    ++next_texture;
                }
                const RenderTexture& texture = region.textures[entry.texture];
                if (!texture.usable() || !texture.claims_hash(entry.texture_hash)) {
                    error = "draw " + std::to_string(draw) +
                            " is bound to a texture that is unusable or does"
                            " not claim " + hex32(entry.texture_hash);
                    return false;
                }
                if (entry.texture_source != texture.source ||
                    entry.texture_pack != texture.pack ||
                    entry.texture_record != texture.record) {
                    error = "draw " + std::to_string(draw) +
                            " carries texture provenance its texture does not";
                    return false;
                }
                ++resolved;
            } else {
                if (entry.texture != kNoHandle || entry.texture_source != 0 ||
                    entry.texture_pack != 0 || entry.texture_record != 0) {
                    error = "draw " + std::to_string(draw) +
                            " carries texture provenance but is not resolved";
                    return false;
                }
                if (entry.binding == BindingKind::unresolved) {
                    if (texture_claims.count(entry.texture_hash) != 0) {
                        error = "draw " + std::to_string(draw) +
                                " is unresolved but a prepared texture claims " +
                                hex32(entry.texture_hash);
                        return false;
                    }
                    unresolved_hashes.insert(entry.texture_hash);
                    ++unresolved;
                } else if (entry.binding == BindingKind::unusable) {
                    ++unusable;
                } else {
                    if (entry.texture_hash != 0) {
                        error = "draw " + std::to_string(draw) +
                                " has an invalid slot but a nonzero hash";
                        return false;
                    }
                    ++invalid_slot;
                }
            }
            if (entry.binding != BindingKind::invalid_slot) {
                const CanonicalBinding current{entry.binding, entry.texture,
                                               entry.texture_source,
                                               entry.texture_pack,
                                               entry.texture_record};
                const auto inserted =
                    bindings.emplace(entry.texture_hash, current);
                const CanonicalBinding& canonical = inserted.first->second;
                if (!inserted.second &&
                    (canonical.kind != current.kind ||
                     (current.kind == BindingKind::resolved &&
                      (canonical.texture != current.texture ||
                       canonical.source != current.source ||
                       canonical.pack != current.pack ||
                       canonical.record != current.record)))) {
                    error = "draw " + std::to_string(draw) +
                            " disagrees with another draw about " +
                            hex32(entry.texture_hash);
                    return false;
                }
            }
            if (entry.drawable) {
                ++drawable;
                const std::uint64_t group = entry.index_count / 3U;
                drawn_triangles += group;
                if (group > largest) {
                    largest = group;
                }
                const std::size_t wanted = entry.binding == BindingKind::resolved
                                               ? entry.texture
                                               : kNoHandle;
                if (wanted != bound) {
                    ++binds;
                    bound = wanted;
                }
            }
            ++draw;
        }
    }
    if (draw != region.draws.size()) {
        error = "the region holds " + std::to_string(region.draws.size()) +
                " draws but its meshes account for " + std::to_string(draw);
        return false;
    }
    if (next_texture != region.textures.size()) {
        error = std::to_string(region.textures.size() - next_texture) +
                " prepared textures are never drawn";
        return false;
    }
    if (region.unresolved_hashes.size() != unresolved_hashes.size() ||
        !std::equal(region.unresolved_hashes.begin(),
                    region.unresolved_hashes.end(),
                    unresolved_hashes.begin())) {
        error = "the unresolved hash list does not match the prepared draws";
        return false;
    }

    std::uint64_t texture_bytes = 0;
    for (std::size_t i = 0; i < region.textures.size(); ++i) {
        const RenderTexture& texture = region.textures[i];
        if (!texture.usable()) {
            error = "prepared texture " + std::to_string(i) + " is unusable";
            return false;
        }
        texture_bytes += texture.payload().size();
    }

    const TrackDrawCost& cost = region.cost;
    if (cost.meshes != region.meshes.size() || cost.vertices != vertices ||
        cost.triangles != triangles ||
        cost.vertex_buffer_bytes != vertices * kTrackVertexStride ||
        cost.index_buffer_bytes != triangles * kFaceStride ||
        cost.draws != region.draws.size() || cost.drawable_draws != drawable ||
        cost.resolved_draws != resolved || cost.unresolved_draws != unresolved ||
        cost.unusable_draws != unusable ||
        cost.invalid_slot_draws != invalid_slot ||
        cost.drawn_triangles != drawn_triangles ||
        cost.largest_draw_triangles != largest ||
        cost.textures != region.textures.size() ||
        cost.texture_bytes != texture_bytes || cost.texture_binds != binds ||
        cost.unresolved_hashes != region.unresolved_hashes.size()) {
        error = "the cost statistics do not match the prepared draw list";
        return false;
    }
    return true;
}

bool bind_track_default_alpha(RenderTextureSet& textures, std::uint32_t missing_hash,
                              std::string& error) {
    error.clear();
    MeshMaterial material;
    material.texture_slot = 0;
    const auto binding = resolve_material(std::vector<std::uint32_t>{missing_hash}, material, textures);
    if (binding.kind != BindingKind::unresolved) return true;
    const auto fallback = resolve_material(std::vector<std::uint32_t>{kTrackDefaultAlphaHash}, material, textures);
    if (fallback.kind != BindingKind::resolved) {
        error = "retail DEFAULTALPHA is unavailable for an absent track texture";
        return false;
    }
    auto& texture = textures.textures[fallback.texture_index];
    if (texture.claim_count >= texture.claims.size()) {
        error = "retail DEFAULTALPHA alias capacity exceeded";
        return false;
    }
    texture.claims[texture.claim_count++] = missing_hash;
    return true;
}

} // namespace nfsu
