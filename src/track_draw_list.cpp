#include "nfsu/track_draw_list.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace nfsu {
namespace {

std::string hex32(std::uint32_t value) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text.push_back(kHex[(value >> shift) & 0x0FU]);
    }
    return text;
}

bool finite3(const float value[3]) noexcept {
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
           std::isfinite(value[2]);
}

bool transform_finite(const TrackTransform& transform) noexcept {
    for (std::size_t i = 0; i < 9; ++i) {
        if (!std::isfinite(transform.linear[i])) {
            return false;
        }
    }
    return finite3(transform.translation);
}

// Match resolve_track_objects: retain first-root precedence, but use the
// last loaded valid payload for equal hashes inside one verified root.
// Native 0x540201 writes every such payload into the first equal-key entry.
std::map<std::uint32_t, std::size_t> mesh_index_by_hash(
    const PreparedTrackRegion& region) {
    std::map<std::uint32_t, std::size_t> meshes;
    for (std::size_t i = 0; i < region.meshes.size(); ++i) {
        const auto& mesh = region.meshes[i];
        const auto bound = meshes.emplace(mesh.mesh_hash, i);
        if (!bound.second) {
            const auto& previous = region.meshes[bound.first->second];
            if (mesh.has_hash_record && previous.has_hash_record &&
                mesh.root_index != kUnknownTrackGeometryRoot &&
                mesh.root_index == previous.root_index)
                bound.first->second = i;
        }
    }
    return meshes;
}

// One slot's attempt at a mesh.
struct SlotLookup {
    bool resolved = false;
    std::size_t mesh = kNoHandle;
    std::uint32_t hash = 0;
    TrackUnplacedReason reason = TrackUnplacedReason::empty_slot;
};

SlotLookup look_up_slot(
    const std::map<std::uint32_t, std::size_t>& meshes,
    const TrackObject& object,
    std::size_t slot) {
    SlotLookup result;
    result.hash = object.mesh_hash[slot];
    if (result.hash == 0) {
        result.reason = TrackUnplacedReason::empty_slot;
        return result;
    }
    // speed.exe 0x0044590B and 0x00445917 compare against these two and
    // skip the lookup, so neither ever resolves a solid.
    if (result.hash == kSkydomeMeshHash || result.hash == kReservedMeshHash) {
        result.reason = TrackUnplacedReason::skipped_hash;
        return result;
    }
    const std::map<std::uint32_t, std::size_t>::const_iterator found =
        meshes.find(result.hash);
    if (found == meshes.end()) {
        result.reason = TrackUnplacedReason::unresolved_hash;
        return result;
    }
    result.resolved = true;
    result.mesh = found->second;
    return result;
}

// Drawable groups of each prepared mesh, in prepared order, as a flat
// table: mesh m owns order[first[m] .. first[m + 1]).
struct MeshDraws {
    std::vector<std::size_t> first;
    std::vector<std::uint32_t> order;
};

bool build_mesh_draws(const PreparedTrackRegion& region, MeshDraws& out,
                      std::string& error) {
    if (region.draws.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "the prepared region holds more draws than a u32 can name";
        return false;
    }
    std::vector<std::size_t> counts(region.meshes.size(), 0);
    for (const TrackDraw& draw : region.draws) {
        if (draw.mesh >= region.meshes.size()) {
            error = "a prepared draw names mesh " + std::to_string(draw.mesh) +
                    " of " + std::to_string(region.meshes.size());
            return false;
        }
        if (draw.drawable) {
            ++counts[draw.mesh];
        }
    }
    out.first.assign(region.meshes.size() + 1, 0);
    for (std::size_t m = 0; m < region.meshes.size(); ++m) {
        out.first[m + 1] = out.first[m] + counts[m];
    }
    out.order.assign(out.first.back(), 0);
    std::vector<std::size_t> next(out.first.begin(), out.first.end() - 1);
    for (std::size_t d = 0; d < region.draws.size(); ++d) {
        const TrackDraw& draw = region.draws[d];
        if (draw.drawable) {
            out.order[next[draw.mesh]++] = static_cast<std::uint32_t>(d);
        }
    }
    return true;
}

// The position of one source instance in PreparedTrackScene::instance_handles,
// or kNoHandle when the scene holds no such section or instance.
std::size_t handle_position(
    const PreparedTrackScene& scene,
    std::size_t section_index,
    std::size_t instance_index) noexcept {
    const std::vector<TrackSceneSection>::const_iterator found = std::lower_bound(
        scene.sections.begin(), scene.sections.end(), section_index,
        [](const TrackSceneSection& section, std::size_t wanted) {
            return section.section_index < wanted;
        });
    if (found == scene.sections.end() || found->section_index != section_index) {
        return kNoHandle;
    }
    if (instance_index >= found->source_instances) {
        return kNoHandle;
    }
    const std::size_t position = found->first_handle + instance_index;
    if (position >= scene.instance_handles.size()) {
        return kNoHandle;
    }
    return position;
}

bool place(
    const TrackRegionReport& geometry,
    const TrackObjectReport& objects,
    const RenderTextureSet& textures,
    const TrackSceneOptions& options,
    PreparedTrackScene& out,
    std::string& error,
    TrackRegionReport* owned_geometry = nullptr,
    RenderTextureSet* owned_textures = nullptr) {
    if (options.lod_slot >= kSceneryObjectMeshSlots) {
        error = "mesh slot " + std::to_string(options.lod_slot) +
                " is outside the " + std::to_string(kSceneryObjectMeshSlots) +
                " an object record holds";
        return false;
    }

    PreparedTrackScene scene;
    const bool prepared = owned_geometry && owned_textures ?
        prepare_track_region(std::move(*owned_geometry), std::move(*owned_textures),
                             options.region, scene.region, error) :
        prepare_track_region(geometry, textures, options.region, scene.region, error);
    if (!prepared) {
        return false;
    }
    scene.object_region = objects.region;
    scene.lod_slot = options.lod_slot;
    scene.first_instance_of_mesh.assign(scene.region.meshes.size(), kNoHandle);

    const std::map<std::uint32_t, std::size_t> meshes =
        mesh_index_by_hash(scene.region);
    MeshDraws mesh_draws;
    if (!build_mesh_draws(scene.region, mesh_draws, error)) {
        return false;
    }

    TrackScenePlacementCost& cost = scene.cost;
    cost.source_sections = objects.sections.size();
    std::set<std::uint32_t> placed_hashes;
    std::set<std::uint32_t> unplaced_hashes;

    for (const TrackSection& section : objects.sections) {
        if (!section.valid) {
            continue;
        }
        if (scene.sections.size() >= options.placement.max_sections) {
            error = "the object region holds more than " +
                    std::to_string(options.placement.max_sections) +
                    " valid sections";
            return false;
        }
        const std::uint64_t handles = scene.instance_handles.size();
        const std::uint64_t more = section.instances.size();
        if (more > options.placement.max_instances ||
            handles > options.placement.max_instances - more) {
            error = "the object region places more than " +
                    std::to_string(options.placement.max_instances) +
                    " instances";
            return false;
        }

        TrackSceneSection entry;
        entry.section_index = section.index;
        entry.number = section.number;
        entry.letter = section.letter;
        entry.has_letter = section.has_letter;
        entry.source_instances = section.instances.size();
        entry.first_handle = scene.instance_handles.size();

        for (const TrackInstance& instance : section.instances) {
            ++cost.source_instances;
            if (instance.object_index >= section.objects.size()) {
                error = "section " + std::to_string(section.index) +
                        " instance " + std::to_string(instance.index) +
                        " names object " +
                        std::to_string(instance.object_index) + " of " +
                        std::to_string(section.objects.size());
                return false;
            }
            const TrackObject& object = section.objects[instance.object_index];
            if (scene.sky_mesh == kNoHandle && object.mesh_hash[0] == kSkydomeMeshHash) {
                const auto sky = meshes.find(kSkydomeMeshHash);
                if (sky != meshes.end()) {
                    scene.sky_mesh = sky->second;
                    scene.sky_transform = track_instance_transform(instance);
                }
            }

            const SlotLookup requested =
                look_up_slot(meshes, object, options.lod_slot);
            SlotLookup lookup = requested;
            std::size_t slot = options.lod_slot;
            bool fallback = false;
            // 0x00445A81 and 0x00445A8F: when slot 5 or slot 4 resolved
            // nothing, the loader fills it with the first solid slots 0 to 3
            // resolved. It is the only recovered fallback and it covers
            // those two slots alone.
            if (!lookup.resolved &&
                (options.lod_slot == 4 || options.lod_slot == 5)) {
                for (std::size_t s = 0; s < kSceneryObjectChainSlots; ++s) {
                    const SlotLookup chain = look_up_slot(meshes, object, s);
                    if (chain.resolved) {
                        lookup = chain;
                        slot = s;
                        fallback = true;
                        break;
                    }
                }
            }

            if (!lookup.resolved) {
                TrackUnplacedInstance miss;
                miss.section_index = section.index;
                miss.instance_index = instance.index;
                miss.object_index = instance.object_index;
                miss.slot = options.lod_slot;
                miss.mesh_hash = requested.hash;
                miss.reason = requested.reason;
                switch (miss.reason) {
                case TrackUnplacedReason::empty_slot:
                    ++cost.unplaced_empty_slot;
                    break;
                case TrackUnplacedReason::skipped_hash:
                    ++cost.unplaced_skipped_hash;
                    break;
                case TrackUnplacedReason::unresolved_hash:
                    ++cost.unplaced_unresolved;
                    break;
                }
                if (miss.mesh_hash != 0) {
                    unplaced_hashes.insert(miss.mesh_hash);
                }
                scene.unplaced.push_back(miss);
                scene.instance_handles.push_back(kNoHandle);
                continue;
            }

            PreparedTrackInstance placed;
            placed.mesh = lookup.mesh;
            SlotLookup reflected = look_up_slot(meshes, object, 4);
            // speed.exe 0x445A66..0x445A9A retains the last non-empty,
            // non-skipped node from slots 0..3 and copies it into slot 4
            // only when slot 4 has no node. An unresolved hash still owns
            // a node, so it must not trigger or be bypassed by fallback.
            if (!reflected.resolved &&
                reflected.reason != TrackUnplacedReason::unresolved_hash) {
                for (std::size_t s = 0; s < kSceneryObjectChainSlots; ++s) {
                    const SlotLookup candidate = look_up_slot(meshes, object, s);
                    if (candidate.resolved ||
                        candidate.reason == TrackUnplacedReason::unresolved_hash)
                        reflected = candidate;
                }
            }
            if (reflected.resolved) {
                if (reflected.mesh >= kNoReflectionMesh) { error = "reflection mesh index exceeds u32"; return false; }
                placed.reflection_mesh = static_cast<std::uint32_t>(reflected.mesh);
            }
            placed.transform = track_instance_transform(instance);
            placed.section_index = section.index;
            placed.section_number = section.number;
            placed.section_letter = section.letter;
            placed.has_section_letter = section.has_letter;
            placed.instance_index = instance.index;
            placed.object_index = instance.object_index;
            placed.slot = slot;
            placed.requested_slot = options.lod_slot;
            placed.slot_fallback_used = fallback;
            placed.mesh_hash = lookup.hash;
            placed.instance_flags = instance.flags;
            placed.radius = object.radius;
            track_box_to_float(instance.bounds_min, instance.bounds_max,
                               placed.bounds_min, placed.bounds_max);

            if (fallback) {
                ++cost.slot_fallbacks;
            }
            placed_hashes.insert(lookup.hash);
            if (scene.first_instance_of_mesh[lookup.mesh] == kNoHandle) {
                scene.first_instance_of_mesh[lookup.mesh] = scene.instances.size();
            }
            if (!cost.has_bounds) {
                cost.has_bounds = true;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    cost.bounds_min[axis] = placed.bounds_min[axis];
                    cost.bounds_max[axis] = placed.bounds_max[axis];
                }
            } else {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    cost.bounds_min[axis] =
                        std::min(cost.bounds_min[axis], placed.bounds_min[axis]);
                    cost.bounds_max[axis] =
                        std::max(cost.bounds_max[axis], placed.bounds_max[axis]);
                }
            }
            cost.resident_entries +=
                mesh_draws.first[lookup.mesh + 1] - mesh_draws.first[lookup.mesh];
            for (std::size_t k = mesh_draws.first[lookup.mesh];
                 k < mesh_draws.first[lookup.mesh + 1]; ++k) {
                cost.resident_triangles +=
                    scene.region.draws[mesh_draws.order[k]].index_count / 3U;
            }

            scene.instance_handles.push_back(scene.instances.size());
            ++entry.placed;
            scene.instances.push_back(std::move(placed));
        }

        scene.sections.push_back(entry);
    }

    cost.valid_sections = scene.sections.size();
    cost.placed_instances = scene.instances.size();
    cost.placed_hashes = placed_hashes.size();
    cost.unplaced_hashes = unplaced_hashes.size();
    scene.unplaced_hashes.assign(unplaced_hashes.begin(), unplaced_hashes.end());
    for (const std::size_t witness : scene.first_instance_of_mesh) {
        if (witness == kNoHandle) {
            ++cost.undrawn_meshes;
        } else {
            ++cost.drawn_meshes;
        }
    }

    if (!validate_prepared_track_scene(scene, error)) {
        return false;
    }
    out = std::move(scene);
    return true;
}

bool build_list(
    const PreparedTrackScene& scene,
    const std::vector<TrackVisibleInstance>& visible,
    const TrackDrawListOptions& options,
    TrackDrawList& out,
    std::string& error) {
    if (scene.instances.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "the scene holds more instances than a u32 can name";
        return false;
    }
    MeshDraws mesh_draws;
    if (!build_mesh_draws(scene.region, mesh_draws, error)) {
        return false;
    }

    TrackDrawList list;
    list.order = options.order;
    list.cost.visible = visible.size();

    std::vector<char> seen(scene.instance_handles.size(), 0);
    for (const TrackVisibleInstance& item : visible) {
        const std::size_t position =
            handle_position(scene, item.section_index, item.instance_index);
        if (position == kNoHandle) {
            error = "visible section " + std::to_string(item.section_index) +
                    " instance " + std::to_string(item.instance_index) +
                    " is not one the scene holds";
            return false;
        }
        if (seen[position] != 0) {
            error = "visible section " + std::to_string(item.section_index) +
                    " instance " + std::to_string(item.instance_index) +
                    " appears twice in the visible set";
            return false;
        }
        seen[position] = 1;

        const std::size_t handle = scene.instance_handles[position];
        if (handle == kNoHandle) {
            ++list.cost.visible_unplaced;
            continue;
        }
        ++list.cost.visible_placed;
        if (handle >= scene.instances.size()) {
            error = "the scene's handle table names instance " +
                    std::to_string(handle) + " of " +
                    std::to_string(scene.instances.size());
            return false;
        }
        const std::size_t mesh = scene.instances[handle].mesh;
        if (mesh + 1 >= mesh_draws.first.size()) {
            error = "scene instance " + std::to_string(handle) +
                    " names mesh " + std::to_string(mesh) + " of " +
                    std::to_string(scene.region.meshes.size());
            return false;
        }
        const std::size_t first = mesh_draws.first[mesh];
        const std::size_t last = mesh_draws.first[mesh + 1];
        if (first == last) {
            ++list.cost.instances_without_entries;
            continue;
        }
        if (list.entries.size() + (last - first) >
            options.limits.max_entries) {
            error = "the draw list would hold more than " +
                    std::to_string(options.limits.max_entries) + " entries";
            return false;
        }
        for (std::size_t k = first; k < last; ++k) {
            const std::uint32_t draw = mesh_draws.order[k];
            const std::uint64_t triangles =
                scene.region.draws[draw].index_count / 3U;
            if (triangles > options.limits.max_triangles - list.cost.triangles) {
                error = "the draw list would draw more than " +
                        std::to_string(options.limits.max_triangles) +
                        " triangles";
                return false;
            }
            list.cost.triangles += triangles;
            TrackDrawListEntry entry;
            entry.instance = static_cast<std::uint32_t>(handle);
            entry.draw = draw;
            list.entries.push_back(entry);
        }
    }

    if (options.order == TrackDrawOrder::material_group) {
        std::stable_sort(list.entries.begin(), list.entries.end(),
                         [](const TrackDrawListEntry& a,
                            const TrackDrawListEntry& b) {
                             return a.draw < b.draw;
                         });
    } else if (options.order == TrackDrawOrder::retail_bucket) {
        std::vector<unsigned char> layers(list.entries.size(), 0);
        std::vector<std::size_t> groups(list.entries.size(), 0);
        std::map<std::pair<std::size_t, std::uint32_t>, std::size_t> first_use;
        std::size_t next_group = 0;
        for (std::size_t i = 0; i < list.entries.size(); ++i) {
            const TrackDraw& draw = scene.region.draws[list.entries[i].draw];
            std::pair<std::size_t, std::uint32_t> key{kNoHandle, draw.texture_hash};
            if (draw.binding == BindingKind::resolved) {
                const RenderTexture& texture = scene.region.textures[draw.texture];
                layers[i] = static_cast<unsigned char>(
                    render_draw_layer(texture.layer_group, texture.layer_order));
                key = {draw.texture, 0};
            }
            const auto inserted = first_use.emplace(key, next_group);
            if (inserted.second) ++next_group;
            groups[i] = inserted.first->second;
        }
        std::vector<std::size_t> order(list.entries.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&layers, &groups](std::size_t a, std::size_t b) {
                             if (layers[a] != layers[b]) return layers[a] < layers[b];
                             return groups[a] < groups[b];
                         });
        std::vector<TrackDrawListEntry> sorted;
        sorted.reserve(list.entries.size());
        for (const std::size_t index : order) sorted.push_back(list.entries[index]);
        list.entries.swap(sorted);
    }

    std::set<std::uint32_t> groups;
    std::set<std::size_t> used_meshes;
    std::size_t bound = kNoHandle - 1;
    for (const TrackDrawListEntry& entry : list.entries) {
        groups.insert(entry.draw);
        used_meshes.insert(scene.instances[entry.instance].mesh);
        const TrackDraw& draw = scene.region.draws[entry.draw];
        const std::size_t wanted =
            draw.binding == BindingKind::resolved ? draw.texture : kNoHandle;
        if (wanted != bound) {
            ++list.cost.texture_binds;
            bound = wanted;
        }
    }
    list.cost.entries = list.entries.size();
    list.cost.groups = groups.size();
    list.cost.meshes = used_meshes.size();

    if (!validate_track_draw_list(scene, list, error)) {
        return false;
    }
    out = std::move(list);
    return true;
}

} // namespace

const char* track_unplaced_reason_name(TrackUnplacedReason reason) noexcept {
    switch (reason) {
    case TrackUnplacedReason::empty_slot: return "empty slot";
    case TrackUnplacedReason::skipped_hash: return "skipped hash";
    case TrackUnplacedReason::unresolved_hash: return "unresolved hash";
    }
    return "unknown";
}

const char* track_draw_order_name(TrackDrawOrder order) noexcept {
    switch (order) {
    case TrackDrawOrder::visible_instance: return "visible instance";
    case TrackDrawOrder::material_group: return "material group";
    case TrackDrawOrder::retail_bucket: return "retail bucket";
    }
    return "unknown";
}

std::size_t track_scene_instance_of(
    const PreparedTrackScene& scene,
    std::size_t section_index,
    std::size_t instance_index) noexcept {
    const std::size_t position =
        handle_position(scene, section_index, instance_index);
    if (position == kNoHandle) {
        return kNoHandle;
    }
    return scene.instance_handles[position];
}

bool build_track_scene(
    const TrackRegionReport& geometry,
    const TrackObjectReport& objects,
    const RenderTextureSet& textures,
    const TrackSceneOptions& options,
    PreparedTrackScene& scene,
    std::string& error) {
    scene = PreparedTrackScene{};
    error.clear();
    bool ok = false;
    try {
        ok = place(geometry, objects, textures, options, scene, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory while placing the track scene";
        ok = false;
    }
    if (!ok) {
        scene = PreparedTrackScene{};
        if (error.empty()) {
            error = "the track scene could not be built";
        }
    }
    return ok;
}

bool build_track_scene(
    TrackRegionReport&& geometry,
    const TrackObjectReport& objects,
    RenderTextureSet&& textures,
    const TrackSceneOptions& options,
    PreparedTrackScene& scene,
    std::string& error) {
    scene = PreparedTrackScene{};
    error.clear();
    bool ok = false;
    try {
        ok = place(geometry, objects, textures, options, scene, error, &geometry, &textures);
    } catch (const std::bad_alloc&) {
        error = "out of memory while placing the track scene";
        ok = false;
    }
    if (!ok) {
        scene = PreparedTrackScene{};
        if (error.empty()) {
            error = "the track scene could not be built";
        }
    }
    return ok;
}

bool validate_prepared_track_scene(
    const PreparedTrackScene& scene,
    std::string& error) {
    error.clear();

    if (!validate_prepared_track_region(scene.region, error)) {
        return false;
    }
    if (scene.sky_mesh != kNoHandle &&
        (scene.sky_mesh >= scene.region.meshes.size() ||
         scene.region.meshes[scene.sky_mesh].mesh_hash != kSkydomeMeshHash ||
         !transform_finite(scene.sky_transform))) {
        error = "invalid separate sky mesh or transform";
        return false;
    }
    if (scene.lod_slot >= kSceneryObjectMeshSlots) {
        error = "the scene names mesh slot " + std::to_string(scene.lod_slot) +
                " of " + std::to_string(kSceneryObjectMeshSlots);
        return false;
    }
    if (scene.first_instance_of_mesh.size() != scene.region.meshes.size()) {
        error = "the scene holds " +
                std::to_string(scene.first_instance_of_mesh.size()) +
                " mesh witnesses for " +
                std::to_string(scene.region.meshes.size()) + " prepared meshes";
        return false;
    }

    std::size_t handles = 0;
    for (std::size_t i = 0; i < scene.sections.size(); ++i) {
        const TrackSceneSection& section = scene.sections[i];
        if (i != 0 &&
            scene.sections[i - 1].section_index >= section.section_index) {
            error = "scene section positions are not ascending at " +
                    std::to_string(i);
            return false;
        }
        if (section.first_handle != handles) {
            error = "scene section " + std::to_string(i) +
                    " starts at handle " + std::to_string(section.first_handle) +
                    " rather than " + std::to_string(handles);
            return false;
        }
        handles += section.source_instances;
    }
    if (handles != scene.instance_handles.size()) {
        error = "the scene's sections cover " + std::to_string(handles) +
                " instances but the handle table holds " +
                std::to_string(scene.instance_handles.size());
        return false;
    }

    // Every handle names an instance that names it back, and every
    // instance is named exactly once. The second half is what stops a
    // scene from carrying an instance no section reaches.
    std::vector<char> named(scene.instances.size(), 0);
    for (std::size_t s = 0; s < scene.sections.size(); ++s) {
        const TrackSceneSection& section = scene.sections[s];
        std::uint64_t placed = 0;
        for (std::size_t k = 0; k < section.source_instances; ++k) {
            const std::size_t handle =
                scene.instance_handles[section.first_handle + k];
            if (handle == kNoHandle) {
                continue;
            }
            if (handle >= scene.instances.size()) {
                error = "handle " + std::to_string(section.first_handle + k) +
                        " names instance " + std::to_string(handle) + " of " +
                        std::to_string(scene.instances.size());
                return false;
            }
            const PreparedTrackInstance& instance = scene.instances[handle];
            if (instance.section_index != section.section_index ||
                instance.instance_index != k) {
                error = "handle " + std::to_string(section.first_handle + k) +
                        " names an instance that belongs to another section or"
                        " position";
                return false;
            }
            // The section provenance an instance carries, against the
            // section it actually belongs to. The builder copies both
            // from the same TrackSection and cannot produce a
            // disagreement, but nothing else in this structure pins
            // them, and a backend reads section_number as provenance.
            if (instance.section_number != section.number ||
                instance.section_letter != section.letter ||
                instance.has_section_letter != section.has_letter) {
                error = "instance " + std::to_string(handle) +
                        " carries section provenance its own section"
                        " contradicts";
                return false;
            }
            if (named[handle] != 0) {
                error = "instance " + std::to_string(handle) +
                        " is named by two handles";
                return false;
            }
            named[handle] = 1;
            ++placed;
        }
        if (section.placed != placed) {
            error = "scene section " + std::to_string(s) + " claims " +
                    std::to_string(section.placed) +
                    " placed instances and holds " + std::to_string(placed);
            return false;
        }
    }
    for (std::size_t i = 0; i < named.size(); ++i) {
        if (named[i] == 0) {
            error = "instance " + std::to_string(i) +
                    " is not named by any section handle";
            return false;
        }
    }

    const std::map<std::uint32_t, std::size_t> meshes =
        mesh_index_by_hash(scene.region);
    MeshDraws mesh_draws;
    if (!build_mesh_draws(scene.region, mesh_draws, error)) {
        return false;
    }

    std::set<std::uint32_t> placed_hashes;
    std::uint64_t fallbacks = 0;
    std::uint64_t resident_entries = 0;
    std::uint64_t resident_triangles = 0;
    bool has_bounds = false;
    float bounds_min[3] = {0.0F, 0.0F, 0.0F};
    float bounds_max[3] = {0.0F, 0.0F, 0.0F};
    for (std::size_t i = 0; i < scene.instances.size(); ++i) {
        const PreparedTrackInstance& instance = scene.instances[i];
        if (i != 0) {
            const PreparedTrackInstance& previous = scene.instances[i - 1];
            const bool ascending =
                previous.section_index < instance.section_index ||
                (previous.section_index == instance.section_index &&
                 previous.instance_index < instance.instance_index);
            if (!ascending) {
                error = "scene instances are not ascending at " +
                        std::to_string(i);
                return false;
            }
        }
        if (instance.reflection_mesh != kNoReflectionMesh && instance.reflection_mesh >= scene.region.meshes.size()) {
            error = "reflection mesh out of range"; return false;
        }
        if (instance.mesh >= scene.region.meshes.size()) {
            error = "scene instance " + std::to_string(i) + " names mesh " +
                    std::to_string(instance.mesh) + " of " +
                    std::to_string(scene.region.meshes.size());
            return false;
        }
        if (!transform_finite(instance.transform)) {
            error = "scene instance " + std::to_string(i) +
                    " carries a transform that is not finite";
            return false;
        }
        if (!finite3(instance.bounds_min) || !finite3(instance.bounds_max) ||
            !std::isfinite(instance.radius)) {
            error = "scene instance " + std::to_string(i) +
                    " carries a bound or radius that is not finite";
            return false;
        }
        if (instance.slot >= kSceneryObjectMeshSlots ||
            instance.requested_slot != scene.lod_slot) {
            error = "scene instance " + std::to_string(i) +
                    " names a slot the scene was not built for";
            return false;
        }
        if (instance.slot_fallback_used) {
            if ((instance.requested_slot != 4 && instance.requested_slot != 5) ||
                instance.slot >= kSceneryObjectChainSlots) {
                error = "scene instance " + std::to_string(i) +
                        " claims the slot fallback outside the slots it covers";
                return false;
            }
            ++fallbacks;
        } else if (instance.slot != instance.requested_slot) {
            error = "scene instance " + std::to_string(i) +
                    " placed a slot it did not request and claims no fallback";
            return false;
        }
        if (instance.mesh_hash == 0 ||
            instance.mesh_hash == kSkydomeMeshHash ||
            instance.mesh_hash == kReservedMeshHash) {
            error = "scene instance " + std::to_string(i) +
                    " placed the hash " + hex32(instance.mesh_hash) +
                    ", which never resolves a mesh";
            return false;
        }
        const std::map<std::uint32_t, std::size_t>::const_iterator found =
            meshes.find(instance.mesh_hash);
        if (found == meshes.end() || found->second != instance.mesh) {
            error = "scene instance " + std::to_string(i) + " binds " +
                    hex32(instance.mesh_hash) +
                    " to a mesh that does not match its root-aware hash binding";
            return false;
        }
        placed_hashes.insert(instance.mesh_hash);
        resident_entries +=
            mesh_draws.first[instance.mesh + 1] - mesh_draws.first[instance.mesh];
        for (std::size_t k = mesh_draws.first[instance.mesh];
             k < mesh_draws.first[instance.mesh + 1]; ++k) {
            resident_triangles +=
                scene.region.draws[mesh_draws.order[k]].index_count / 3U;
        }
        if (!has_bounds) {
            has_bounds = true;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                bounds_min[axis] = instance.bounds_min[axis];
                bounds_max[axis] = instance.bounds_max[axis];
            }
        } else {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                bounds_min[axis] =
                    std::min(bounds_min[axis], instance.bounds_min[axis]);
                bounds_max[axis] =
                    std::max(bounds_max[axis], instance.bounds_max[axis]);
            }
        }
    }

    // The witnesses: the first instance that names each mesh, and nothing
    // else.
    std::vector<std::size_t> witnesses(scene.region.meshes.size(), kNoHandle);
    for (std::size_t i = scene.instances.size(); i > 0; --i) {
        witnesses[scene.instances[i - 1].mesh] = i - 1;
    }
    std::uint64_t drawn = 0;
    for (std::size_t m = 0; m < witnesses.size(); ++m) {
        if (scene.first_instance_of_mesh[m] != witnesses[m]) {
            error = "the first-instance witness of prepared mesh " +
                    std::to_string(m) + " is wrong";
            return false;
        }
        if (witnesses[m] != kNoHandle) {
            ++drawn;
        }
    }

    std::set<std::uint32_t> unplaced_hashes;
    std::uint64_t empty_slot = 0;
    std::uint64_t skipped = 0;
    std::uint64_t unresolved = 0;
    for (std::size_t i = 0; i < scene.unplaced.size(); ++i) {
        const TrackUnplacedInstance& miss = scene.unplaced[i];
        if (i != 0) {
            const TrackUnplacedInstance& previous = scene.unplaced[i - 1];
            const bool ascending =
                previous.section_index < miss.section_index ||
                (previous.section_index == miss.section_index &&
                 previous.instance_index < miss.instance_index);
            if (!ascending) {
                error = "unplaced instances are not ascending at " +
                        std::to_string(i);
                return false;
            }
        }
        if (miss.slot != scene.lod_slot) {
            error = "unplaced instance " + std::to_string(i) +
                    " names a slot the scene was not built for";
            return false;
        }
        const std::size_t position =
            handle_position(scene, miss.section_index, miss.instance_index);
        if (position == kNoHandle ||
            scene.instance_handles[position] != kNoHandle) {
            error = "unplaced instance " + std::to_string(i) +
                    " is not an unplaced entry of the handle table";
            return false;
        }
        switch (miss.reason) {
        case TrackUnplacedReason::empty_slot:
            if (miss.mesh_hash != 0) {
                error = "unplaced instance " + std::to_string(i) +
                        " calls its slot empty but names " +
                        hex32(miss.mesh_hash);
                return false;
            }
            ++empty_slot;
            break;
        case TrackUnplacedReason::skipped_hash:
            if (miss.mesh_hash != kSkydomeMeshHash &&
                miss.mesh_hash != kReservedMeshHash) {
                error = "unplaced instance " + std::to_string(i) +
                        " calls " + hex32(miss.mesh_hash) +
                        " a skipped hash and it is not one";
                return false;
            }
            unplaced_hashes.insert(miss.mesh_hash);
            ++skipped;
            break;
        case TrackUnplacedReason::unresolved_hash:
            // The two hashes the loader skips are not unresolved: nothing
            // ever looks them up. Letting one be labelled unresolved would
            // add it to unplaced_unresolved, which is the count a caller
            // reads as "scenery this region cannot draw".
            if (miss.mesh_hash == 0 || meshes.count(miss.mesh_hash) != 0 ||
                miss.mesh_hash == kSkydomeMeshHash ||
                miss.mesh_hash == kReservedMeshHash) {
                error = "unplaced instance " + std::to_string(i) + " calls " +
                        hex32(miss.mesh_hash) +
                        " unresolved and it is skipped or the region resolves"
                        " it";
                return false;
            }
            unplaced_hashes.insert(miss.mesh_hash);
            ++unresolved;
            break;
        }
    }
    if (scene.unplaced_hashes.size() != unplaced_hashes.size() ||
        !std::equal(scene.unplaced_hashes.begin(), scene.unplaced_hashes.end(),
                    unplaced_hashes.begin())) {
        error = "the unplaced hash list does not match the unplaced instances";
        return false;
    }

    std::uint64_t placed_total = 0;
    for (const TrackSceneSection& section : scene.sections) {
        placed_total += section.placed;
    }
    if (placed_total != scene.instances.size()) {
        error = "the sections claim " + std::to_string(placed_total) +
                " placed instances and the scene holds " +
                std::to_string(scene.instances.size());
        return false;
    }

    const TrackScenePlacementCost& cost = scene.cost;
    if (cost.valid_sections != scene.sections.size() ||
        cost.valid_sections > cost.source_sections ||
        cost.source_instances != scene.instance_handles.size() ||
        cost.placed_instances != scene.instances.size() ||
        cost.unplaced_empty_slot != empty_slot ||
        cost.unplaced_skipped_hash != skipped ||
        cost.unplaced_unresolved != unresolved ||
        cost.unplaced_empty_slot + cost.unplaced_skipped_hash +
                cost.unplaced_unresolved !=
            scene.unplaced.size() ||
        cost.placed_instances + scene.unplaced.size() != cost.source_instances ||
        cost.slot_fallbacks != fallbacks ||
        cost.placed_hashes != placed_hashes.size() ||
        cost.unplaced_hashes != scene.unplaced_hashes.size() ||
        cost.drawn_meshes != drawn ||
        cost.undrawn_meshes != scene.region.meshes.size() - drawn ||
        cost.resident_entries != resident_entries ||
        cost.resident_triangles != resident_triangles ||
        cost.has_bounds != has_bounds) {
        error = "the placement cost statistics do not match the scene";
        return false;
    }
    if (has_bounds) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (cost.bounds_min[axis] != bounds_min[axis] ||
                cost.bounds_max[axis] != bounds_max[axis]) {
                error = "the placement bounds do not match the placed"
                        " instances";
                return false;
            }
        }
    }
    return true;
}

bool build_track_draw_list(
    const PreparedTrackScene& scene,
    const std::vector<TrackVisibleInstance>& visible,
    const TrackDrawListOptions& options,
    TrackDrawList& list,
    std::string& error) {
    list = TrackDrawList{};
    error.clear();
    bool ok = false;
    try {
        ok = build_list(scene, visible, options, list, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory while building the track draw list";
        ok = false;
    }
    if (!ok) {
        list = TrackDrawList{};
        if (error.empty()) {
            error = "the track draw list could not be built";
        }
    }
    return ok;
}

bool build_track_draw_list(
    const PreparedTrackScene& scene,
    const TrackCullReport& cull,
    const TrackDrawListOptions& options,
    TrackDrawList& list,
    std::string& error) {
    if (!build_track_draw_list(scene, cull.visible, options, list, error)) {
        return false;
    }
    list.has_cull_provenance = true;
    list.requested_policy = cull.requested_policy;
    list.policy_held = cull.policy_held;
    list.sections_resident = cull.stats.sections_resident;
    list.sections_degraded = cull.stats.sections_degraded;
    return true;
}

bool validate_track_draw_list(
    const PreparedTrackScene& scene,
    const TrackDrawList& list,
    std::string& error) {
    error.clear();
    if (list.order != TrackDrawOrder::visible_instance &&
        list.order != TrackDrawOrder::material_group &&
        list.order != TrackDrawOrder::retail_bucket) {
        error = "the draw list has an unknown order";
        return false;
    }

    std::set<std::uint32_t> groups;
    std::set<std::size_t> used_meshes;
    // The distinct instances the entries draw. A visible set holds each
    // (section, instance) at most once, so a placed visible instance either
    // appears here or has no drawable group at all; that is what ties
    // cost.visible_placed to the entries below rather than leaving it free.
    std::set<std::uint32_t> drawn_instances;
    // One copy of one group is one entry: the same pair twice would draw a
    // copy twice, and no order this layer produces can do that.
    std::set<std::pair<std::uint32_t, std::uint32_t>> pairs;
    // visible_instance keeps one run per visible instance, so an instance
    // that starts a second run is out of order however its draws ascend.
    std::set<std::uint32_t> closed;
    std::set<std::pair<std::size_t, std::uint32_t>> closed_textures;
    std::pair<std::size_t, std::uint32_t> previous_texture{kNoHandle, 0};
    int previous_layer = -1;
    std::uint64_t triangles = 0;
    std::uint64_t binds = 0;
    std::size_t bound = kNoHandle - 1;
    for (std::size_t i = 0; i < list.entries.size(); ++i) {
        const TrackDrawListEntry& entry = list.entries[i];
        if (entry.instance >= scene.instances.size()) {
            error = "draw list entry " + std::to_string(i) + " names instance " +
                    std::to_string(entry.instance) + " of " +
                    std::to_string(scene.instances.size());
            return false;
        }
        if (entry.draw >= scene.region.draws.size()) {
            error = "draw list entry " + std::to_string(i) + " names draw " +
                    std::to_string(entry.draw) + " of " +
                    std::to_string(scene.region.draws.size());
            return false;
        }
        const TrackDraw& draw = scene.region.draws[entry.draw];
        if (!draw.drawable) {
            error = "draw list entry " + std::to_string(i) + " names draw " +
                    std::to_string(entry.draw) + ", which is not drawable";
            return false;
        }
        if (draw.mesh != scene.instances[entry.instance].mesh) {
            error = "draw list entry " + std::to_string(i) +
                    " draws a group of mesh " + std::to_string(draw.mesh) +
                    " with an instance of mesh " +
                    std::to_string(scene.instances[entry.instance].mesh);
            return false;
        }
        if (!pairs.emplace(entry.instance, entry.draw).second) {
            error = "draw list entry " + std::to_string(i) +
                    " draws instance " + std::to_string(entry.instance) +
                    " group " + std::to_string(entry.draw) + " a second time";
            return false;
        }
        if (list.order == TrackDrawOrder::material_group) {
            if (i != 0 && list.entries[i - 1].draw > entry.draw) {
                error = "draw list entry " + std::to_string(i) +
                        " breaks the material group order";
                return false;
            }
        } else if (list.order == TrackDrawOrder::retail_bucket) {
            std::pair<std::size_t, std::uint32_t> key{kNoHandle, draw.texture_hash};
            int layer = 0;
            if (draw.binding == BindingKind::resolved) {
                const RenderTexture& texture = scene.region.textures[draw.texture];
                key = {draw.texture, 0};
                layer = render_draw_layer(texture.layer_group, texture.layer_order);
            }
            if (layer < previous_layer) {
                error = "draw list entry " + std::to_string(i) +
                        " breaks the retail texture row order";
                return false;
            }
            if (i != 0 && key != previous_texture) {
                closed_textures.insert(previous_texture);
                if (closed_textures.count(key) != 0) {
                    error = "draw list entry " + std::to_string(i) +
                            " reopens a retail texture bucket";
                    return false;
                }
            }
            previous_texture = key;
            previous_layer = layer;
        } else {
            // The visible set arrives in the culler's traversal order, which
            // is not ascending in anything, so what holds is the run shape:
            // one contiguous run per instance, its groups ascending inside.
            if (i != 0 && list.entries[i - 1].instance != entry.instance) {
                closed.insert(list.entries[i - 1].instance);
            }
            if (closed.count(entry.instance) != 0) {
                error = "draw list entry " + std::to_string(i) +
                        " reopens instance " + std::to_string(entry.instance) +
                        ", which the visible instance order does not";
                return false;
            }
            if (i != 0 && list.entries[i - 1].instance == entry.instance &&
                list.entries[i - 1].draw >= entry.draw) {
                error = "draw list entry " + std::to_string(i) +
                        " breaks the visible instance order";
                return false;
            }
        }
        groups.insert(entry.draw);
        used_meshes.insert(draw.mesh);
        drawn_instances.insert(entry.instance);
        triangles += draw.index_count / 3U;
        const std::size_t wanted =
            draw.binding == BindingKind::resolved ? draw.texture : kNoHandle;
        if (wanted != bound) {
            ++binds;
            bound = wanted;
        }
    }

    const TrackDrawListCost& cost = list.cost;
    if (cost.entries != list.entries.size() || cost.triangles != triangles ||
        cost.groups != groups.size() || cost.meshes != used_meshes.size() ||
        cost.texture_binds != binds ||
        cost.visible_placed + cost.visible_unplaced != cost.visible ||
        // A placed visible instance either draws at least one group or has
        // none to draw, so these two account for every one of them. Without
        // this, instances_without_entries is bounded and never pinned, and a
        // list could claim any number at or below visible_placed.
        cost.visible_placed !=
            static_cast<std::uint64_t>(drawn_instances.size()) +
                cost.instances_without_entries) {
        error = "the draw list cost statistics do not match its entries";
        return false;
    }
    // A visible set names each source instance at most once, so it cannot be
    // longer than the scene's handle table. Nothing else bounds these two
    // from above, and both are printed as measurements.
    if (cost.visible > scene.instance_handles.size()) {
        error = "the draw list claims " + std::to_string(cost.visible) +
                " visible instances over a scene holding " +
                std::to_string(scene.instance_handles.size());
        return false;
    }
    if (!list.has_cull_provenance &&
        (list.requested_policy != TrackCullPolicy::retail_tree ||
         list.policy_held != true || list.sections_resident != 0 ||
         list.sections_degraded != 0)) {
        error = "the draw list carries cull provenance it did not come with";
        return false;
    }
    return true;
}

} // namespace nfsu
