#include "nfsu/stock_scene.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace nfsu {

void alias_stock_window_textures(GeometryReport& geometry) noexcept {
    constexpr std::uint32_t kWindow = 0x834B8017U;
    for (Mesh& mesh : geometry.meshes) {
        for (std::uint32_t& hash : mesh.texture_hashes) {
            switch (hash) {
            case 0x7B220DDFU: // WINDOW_FRONT
            case 0x1B0763A0U: // WINDOW_REAR
            case 0xE7E4EF49U: // WINDOW_LEFT_FRONT
            case 0x4CDEBFCAU: // WINDOW_LEFT_REAR
            case 0x60F8B13CU: // WINDOW_RIGHT_FRONT
            case 0x0AB88F5DU: // WINDOW_RIGHT_REAR
                hash = kWindow;
                break;
            default:
                break;
            }
        }
    }
}

namespace {

// Slot ids the draw code treats specially. car_slot_name is the authority
// for the table these come from; nfsu_stock_scene_tests pins each id
// against it.
constexpr int kSlotLeftSideMirror = 3;
constexpr int kSlotRightSideMirror = 4;
constexpr int kSlotExhaust = 15;
constexpr int kSlotWheel = 17;

// The highest LOD index the wheel draw takes: CarRenderInfo::Render reads
// the wheel model at min(LOD, 2) (docs/CAR_SCENE.md).
constexpr std::size_t kWheelMaximumLod = 2;

// How the draw loop at 0x0056F4B0 and the four blocks after it treat a
// slot.
enum class SlotClass { generic, mirror, exhaust, brake, wheel, plate };

SlotClass slot_class(int slot) noexcept {
    if (slot == kSlotLeftSideMirror || slot == kSlotRightSideMirror) {
        return SlotClass::mirror;
    }
    if (slot == kSlotExhaust) {
        return SlotClass::exhaust;
    }
    if (slot == kSlotBrake) {
        return SlotClass::brake;
    }
    if (slot == kSlotWheel) {
        return SlotClass::wheel;
    }
    if (slot == kSlotLicensePlate) {
        return SlotClass::plate;
    }
    return SlotClass::generic;
}

// The placement entries the wheel and the brake draws use, in draw order.
// derive_cullable_part_placements writes 0..3 from the four wheel records
// and 6..9 from the same four positions again.
constexpr std::array<std::size_t, kCarWheelCount> kWheelPlacements{
    static_cast<std::size_t>(CullableCarPart::tire_fl),
    static_cast<std::size_t>(CullableCarPart::tire_fr),
    static_cast<std::size_t>(CullableCarPart::tire_rr),
    static_cast<std::size_t>(CullableCarPart::tire_rl)};
constexpr std::array<std::size_t, kCarWheelCount> kBrakePlacements{
    static_cast<std::size_t>(CullableCarPart::brake_fl),
    static_cast<std::size_t>(CullableCarPart::brake_fr),
    static_cast<std::size_t>(CullableCarPart::brake_rr),
    static_cast<std::size_t>(CullableCarPart::brake_rl)};

void identity_matrix(float out[16]) noexcept {
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = 0.0F;
    }
    out[0] = out[5] = out[10] = out[15] = 1.0F;
}

// Row-major, row-vector: row 3 is the translation.
void translation_matrix(const float position[3], float out[16]) noexcept {
    identity_matrix(out);
    for (std::size_t i = 0; i < 3; ++i) {
        out[12 + i] = position[i];
    }
}

std::string hex32(std::uint32_t value) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text += kHex[(value >> shift) & 0x0FU];
    }
    return text;
}

// Where one mesh hash lives among the loaded sources.
struct MeshHome {
    std::size_t source = 0;
    std::size_t index = 0;
    // A later source, or another mesh of the same source, holds it too.
    bool ambiguous = false;
};

// One drawn copy before the mesh handles are known.
struct InstanceDraft {
    SceneMeshRef mesh;
    float transform[16] = {};
    float animation_pivot[3] = {};
    bool placement_exact = true;
    int slot = 0;
    std::size_t lod = 0;
    std::size_t copy = 0;
    std::uint32_t hash = 0;
};

} // namespace

const char* car_draw_pass_name(CarDrawPass pass) noexcept {
    switch (pass) {
    case CarDrawPass::world: return "world";
    case CarDrawPass::front_end: return "front end";
    }
    return "unknown";
}

const char* stock_slot_outcome_name(StockSlotOutcome outcome) noexcept {
    switch (outcome) {
    case StockSlotOutcome::not_installed: return "not installed";
    case StockSlotOutcome::skipped: return "skipped";
    case StockSlotOutcome::no_model: return "no model";
    case StockSlotOutcome::disabled: return "disabled";
    case StockSlotOutcome::lod_skipped: return "lod skipped";
    case StockSlotOutcome::no_mesh_named: return "no mesh named";
    case StockSlotOutcome::mesh_absent: return "mesh absent";
    case StockSlotOutcome::mesh_invalid: return "mesh invalid";
    case StockSlotOutcome::no_marker: return "no marker";
    case StockSlotOutcome::placement_unreproduced: return "placement unreproduced";
    case StockSlotOutcome::drawn: return "drawn";
    }
    return "unknown";
}

const char* stock_placement_name(StockPlacement placement) noexcept {
    switch (placement) {
    case StockPlacement::car_matrix: return "car matrix";
    case StockPlacement::wheel_table: return "wheel table";
    case StockPlacement::marker: return "marker";
    }
    return "unknown";
}

namespace {

bool build_stock_scene(
    const std::vector<const GeometryReport*>& sources,
    const RenderTextureSet& textures,
    const CarPartDatabase& parts,
    const CarTypeTable& car_types,
    const StockSceneRequest& request,
    const SceneLimits& limits,
    PreparedCarScene& scene,
    StockSceneReport& report,
    std::string& error) {
    if (request.lod >= kCarPartLodCount) {
        error = "LOD index " + std::to_string(request.lod) + " is outside 0.." +
                std::to_string(kCarPartLodCount - 1);
        return false;
    }
    if (request.pass != CarDrawPass::world && request.pass != CarDrawPass::front_end) {
        error = "unknown car draw pass";
        return false;
    }
    if (sources.empty()) {
        error = "a stock car scene needs at least one geometry source";
        return false;
    }
    // Before the source list is walked, not after. prepare_scene_meshes
    // checks the same limit, but it is called at the end, and everything
    // between here and there is work proportional to the source list: this
    // function validates every source and then builds the loaded-solid map
    // over every mesh of every one of them. Refusing a list that is already
    // over the limit costs one comparison; refusing it down there costs all
    // of that first.
    if (sources.size() > limits.max_sources) {
        error = std::to_string(sources.size()) + " geometry sources; the limit is " +
                std::to_string(limits.max_sources);
        return false;
    }
    for (std::size_t s = 0; s < sources.size(); ++s) {
        if (sources[s] == nullptr) {
            error = "geometry source " + std::to_string(s) + " is null";
            return false;
        }
        if (!sources[s]->mesh_directory_valid) {
            error = "geometry source " + std::to_string(s) +
                    " has no verified mesh directory, so a missing part mesh would "
                    "prove nothing";
            return false;
        }
    }
    if (!car_types.ok()) {
        error = "car type table is not valid";
        return false;
    }

    StockParts stock;
    if (!select_stock_parts(parts, request.car_type_name, stock, error)) {
        return false;
    }
    const std::size_t car_type = find_car_type(car_types, request.car_type_name);
    if (car_type == kNoCarType) {
        error = "no car type record is named '" + request.car_type_name + "'";
        return false;
    }
    std::array<PartPlacement, kCullableCarPartCount> placements{};
    if (!derive_cullable_part_placements(car_types.records[car_type].wheels,
                                         placements, error)) {
        error = "car type '" + request.car_type_name + "': " + error;
        return false;
    }

    StockSceneReport result;
    result.car_type_name = stock.car_type_name;
    result.car_type_hash = stock.car_type_hash;
    result.car_type = car_type;
    result.lod = request.lod;
    result.pass = request.pass;
    result.placements = placements;
    result.degenerate_wheel_plane =
        placements[static_cast<std::size_t>(CullableCarPart::shadow)].degenerate_plane;

    // The loaded solid lookup (0x540630) binary-searches each loaded file's
    // 0x00134003 directory in load order and takes the first hit, so the
    // map is filled in that order and an existing entry is never replaced.
    // Meshes that failed validation stay in it: the game would find their
    // solid, and reporting mesh_invalid is more honest than pretending the
    // hash is absent.
    std::map<std::uint32_t, MeshHome> homes;
    for (std::size_t s = 0; s < sources.size(); ++s) {
        const std::vector<Mesh>& meshes = sources[s]->meshes;
        for (std::size_t m = 0; m < meshes.size(); ++m) {
            if (!meshes[m].has_directory_entry) {
                continue;
            }
            const auto inserted =
                homes.emplace(meshes[m].directory_hash, MeshHome{s, m, false});
            if (!inserted.second) {
                inserted.first->second.ambiguous = true;
            }
        }
    }
    std::vector<std::uint32_t> loaded;
    loaded.reserve(homes.size());
    for (const auto& entry : homes) {
        loaded.push_back(entry.first);
    }

    // Pass 1: the four models of every installed part that has any.
    std::array<std::array<PartModel, kCarPartLodCount>, kCarModelSlotCount> models{};
    for (int slot = 0; slot < static_cast<int>(kCarSlotCount); ++slot) {
        const std::size_t index = static_cast<std::size_t>(slot);
        StockSlotReport& entry = result.slots[index];
        const StockPartSlot& installed = stock.slots[index];
        entry.slot = slot;
        entry.rule = installed.rule;
        entry.record = installed.record;
        entry.enabled = installed.enabled;
        if (installed.record == kNoCarPart) {
            entry.outcome = installed.rule == StockPartRule::skipped
                                ? StockSlotOutcome::skipped
                                : StockSlotOutcome::not_installed;
            continue;
        }
        ++result.installed;
        if (index >= kCarModelSlotCount) {
            entry.outcome = StockSlotOutcome::no_model;
            continue;
        }
        ++result.with_model;
        if (installed.record >= parts.records.size()) {
            error = "stock part slot " + std::to_string(slot) +
                    " names record " + std::to_string(installed.record) +
                    " outside the database";
            return false;
        }
        if (!resolve_part_models(parts.records[installed.record], loaded, models[index],
                                 error)) {
            error = "stock part slot " + std::to_string(slot) + ": " + error;
            return false;
        }
    }

    // Pass 2: the collector at 0x0056BDB0. Slots 0..29, a disabled slot
    // contributes nothing, a slot whose LOD A model has no solid
    // contributes nothing, and the markers come from that model's mesh.
    for (std::size_t index = 0; index < kCarModelSlotCount; ++index) {
        const StockSlotReport& entry = result.slots[index];
        if (entry.record == kNoCarPart || !entry.enabled) {
            continue;
        }
        const PartModel& model = models[index][0];
        if (!model.resolved) {
            continue;
        }
        const MeshHome& home = homes.find(model.hash)->second;
        const Mesh& mesh = sources[home.source]->meshes[home.index];
        if (!mesh.valid) {
            // The parser withdraws the markers of a mesh it rejected, so
            // there is nothing to collect. The game would collect them:
            // a known divergence, listed under "Still open after this
            // slice" in docs/CAR_SCENE.md.
            continue;
        }
        for (std::size_t m = 0; m < mesh.markers.size(); ++m) {
            StockMarker marker;
            marker.slot = static_cast<int>(index);
            marker.name_hash = mesh.markers[m].name_hash;
            marker.role = car_marker_role(marker.name_hash);
            marker.geometry_source = home.source;
            marker.source_mesh = home.index;
            marker.index = m;
            result.markers.push_back(marker);
        }
    }

    // Pass 3: which model each slot draws, and where.
    std::set<std::uint32_t> absent;
    std::set<std::uint32_t> ambiguous;
    std::vector<InstanceDraft> drafts;
    std::set<std::pair<std::size_t, std::size_t>> selected;
    for (std::size_t index = 0; index < kCarModelSlotCount; ++index) {
        StockSlotReport& entry = result.slots[index];
        if (entry.record == kNoCarPart) {
            continue;
        }
        const int slot = entry.slot;
        const SlotClass kind = slot_class(slot);

        // The LOD every class takes, then the reasons a class draws
        // nothing at all.
        switch (kind) {
        case SlotClass::generic:
        case SlotClass::mirror:
            entry.lod = request.lod;
            break;
        case SlotClass::exhaust:
        case SlotClass::plate:
            entry.lod = 0;
            break;
        case SlotClass::brake:
            entry.lod = request.pass == CarDrawPass::world ? 1 : 0;
            break;
        case SlotClass::wheel:
            entry.lod = std::min(request.lod, kWheelMaximumLod);
            break;
        }
        entry.placement = kind == SlotClass::exhaust || kind == SlotClass::plate
                              ? StockPlacement::marker
                              : (kind == SlotClass::wheel || kind == SlotClass::brake
                                     ? StockPlacement::wheel_table
                                     : StockPlacement::car_matrix);
        // The model the slot would take is reported whether or not the
        // slot goes on to draw it, so a gated slot still says which mesh
        // the retail rule names for it.
        const PartModel& model = models[index][entry.lod];
        entry.requested_hash = model.requested_hash;
        entry.hash = model.hash;
        entry.model = model.source;

        if ((kind == SlotClass::generic || kind == SlotClass::wheel) && !entry.enabled) {
            entry.outcome = StockSlotOutcome::disabled;
            continue;
        }
        if (kind == SlotClass::mirror && request.lod > 1) {
            entry.outcome = StockSlotOutcome::lod_skipped;
            continue;
        }
        if ((kind == SlotClass::wheel || kind == SlotClass::brake) &&
            !request.place_wheels_from_culling_table) {
            entry.outcome = StockSlotOutcome::placement_unreproduced;
            continue;
        }

        // Only a slot that got this far asked for its mesh, so only such a
        // slot can make a hash absent or ambiguous.
        if (entry.requested_hash != 0 && homes.count(entry.requested_hash) == 0) {
            absent.insert(entry.requested_hash);
        }
        if (!model.resolved) {
            if (model.hash != 0) {
                absent.insert(model.hash);
            }
            // The record named something at this LOD, or the fallback did,
            // and no loaded source holds it; only a slot that named nothing
            // at all reports no_mesh_named.
            entry.outcome = entry.requested_hash != 0 || model.hash != 0
                                ? StockSlotOutcome::mesh_absent
                                : StockSlotOutcome::no_mesh_named;
            continue;
        }
        const MeshHome& home = homes.find(model.hash)->second;
        if (home.ambiguous) {
            ambiguous.insert(model.hash);
        }
        entry.geometry_source = home.source;
        entry.source_mesh = home.index;
        if (!sources[home.source]->meshes[home.index].valid) {
            entry.outcome = StockSlotOutcome::mesh_invalid;
            continue;
        }

        entry.first_instance = drafts.size();
        InstanceDraft draft;
        draft.mesh = SceneMeshRef{home.source, home.index};
        draft.slot = slot;
        draft.lod = entry.lod;
        draft.hash = model.hash;
        if (entry.placement == StockPlacement::marker) {
            const CarMarkerRole wanted = kind == SlotClass::exhaust
                                             ? CarMarkerRole::exhaust
                                             : CarMarkerRole::license_plate;
            for (const StockMarker& marker : result.markers) {
                if (marker.role != wanted) {
                    continue;
                }
                const MeshMarker& record =
                    sources[marker.geometry_source]->meshes[marker.source_mesh]
                        .markers[marker.index];
                draft.copy = entry.instance_count;
                if (!car_marker_placement(record, draft.transform)) {
                    error = "stock part slot " + std::to_string(slot) + ": marker " +
                            hex32(marker.name_hash) + " of mesh " +
                            std::to_string(marker.source_mesh) + " in geometry source " +
                            std::to_string(marker.geometry_source) +
                            " has a matrix that is not finite";
                    return false;
                }
                drafts.push_back(draft);
                ++entry.instance_count;
            }
            if (entry.instance_count == 0) {
                entry.outcome = StockSlotOutcome::no_marker;
                continue;
            }
        } else if (entry.placement == StockPlacement::wheel_table) {
            const std::array<std::size_t, kCarWheelCount>& table =
                kind == SlotClass::wheel ? kWheelPlacements : kBrakePlacements;
            // 0x0056F9B2..0x0056FBAC builds a lateral scale and offset
            // from wheel +0x14 and the stock (RideInfo +0x464 == 0) choice
            // of +0x1C. The alternative +0x20 and the wheel mesh's
            // MARKER_BRAKE branch are not selected by this stock profile.
            // X * H * W * Q and R * B * P are the draw chains (see
            // docs/PART_PLACEMENT.md). Spin and steering in W/B are added
            // by drive_place_instances for a moving race car.
            float turn[16];
            if (kind == SlotClass::wheel) {
                car_wheel_half_turn(turn);
            } else {
                car_brake_reflection(turn);
            }
            draft.placement_exact = false;
            for (std::size_t w = 0; w < table.size(); ++w) {
                draft.copy = w;
                const CarWheelPlacement& wheel = car_types.records[car_type].wheels[w];
                const float half_width = wheel.value_14 * 0.5F;
                const float side = wheel.value_1c > 0.0F ? 1.0F : -1.0F;
                float centre[3] = {placements[table[w]].position[0],
                                   wheel.value_1c - side * half_width,
                                   placements[table[w]].position[2]};
                std::copy(std::begin(centre), std::end(centre),
                          std::begin(draft.animation_pivot));
                float position[16];
                translation_matrix(centre, position);
                float model[16];
                identity_matrix(model);
                if (kind == SlotClass::wheel) {
                    model[5] = wheel.value_14 / 0.184F;
                    model[13] = -half_width;
                }
                if (car_wheel_turned_by_draw(w)) {
                    float reflected[16];
                    multiply_placement_matrix(model, turn, reflected);
                    std::copy(std::begin(reflected), std::end(reflected), model);
                }
                multiply_placement_matrix(model, position, draft.transform);
                drafts.push_back(draft);
                ++entry.instance_count;
            }
        } else {
            draft.copy = 0;
            identity_matrix(draft.transform);
            drafts.push_back(draft);
            entry.instance_count = 1;
        }
        entry.outcome = StockSlotOutcome::drawn;
        ++result.drawn;
        if (model.source == PartModelSource::fallback) {
            ++result.fell_back;
        }
        selected.insert(std::make_pair(home.source, home.index));
    }

    if (drafts.size() > limits.max_instances) {
        error = "the stock car draws " + std::to_string(drafts.size()) +
                " instances; the limit is " + std::to_string(limits.max_instances);
        return false;
    }
    result.absent_hashes.assign(absent.begin(), absent.end());
    result.ambiguous_hashes.assign(ambiguous.begin(), ambiguous.end());
    result.instances = drafts.size();

    // Ascending (source, index), which is the order prepared scenes keep.
    std::vector<SceneMeshRef> refs;
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> handles;
    refs.reserve(selected.size());
    for (const auto& entry : selected) {
        handles.emplace(entry, refs.size());
        refs.push_back(SceneMeshRef{entry.first, entry.second});
    }

    PreparedCarScene prepared;
    if (!prepare_scene_meshes(sources, refs, std::vector<std::size_t>{}, textures,
                              limits, prepared, error)) {
        return false;
    }
    prepared.profile = SelectionProfile::stock_car;
    for (PreparedDraw& draw : prepared.draws) {
        // A stock car has no selected sponsor decal. Retail maps its
        // DUMMY_DECAL1..8 slots to DEFAULTALPHA, which the car draw skips.
        if (draw.binding == BindingKind::unresolved &&
            is_stock_dummy_decal_hash(draw.texture_hash)) {
            draw.drawable = false;
        }
    }
    const auto uses_global_brake = [&](std::size_t mesh) {
        return std::any_of(prepared.draws.begin(), prepared.draws.end(),
            [mesh](const PreparedDraw& draw) {
                return draw.mesh == mesh && draw.drawable &&
                       draw.texture_hash == kBrakeGlobalHash;
            });
    };
    std::size_t brake_left_texture = kNoHandle;
    const bool has_left_brake = std::any_of(drafts.begin(), drafts.end(),
        [&](const InstanceDraft& draft) {
            return draft.slot == kSlotBrake && (draft.copy == 0 || draft.copy == 3) &&
                uses_global_brake(handles.at({draft.mesh.source, draft.mesh.index}));
        });
    if (has_left_brake) {
        for (std::size_t i = 0; i < prepared.textures.size(); ++i) {
            if (prepared.textures[i].claims_hash(kBrakeGlobalLeftHash)) {
                brake_left_texture = i;
                break;
            }
        }
        for (const RenderTexture& texture : textures.textures) {
            if (brake_left_texture == kNoHandle && texture.usable() &&
                texture.claims_hash(kBrakeGlobalLeftHash)) {
                std::uint64_t remaining = limits.max_texture_bytes;
                if (texture.payload().size() > remaining) {
                    error = "prepared textures would exceed " +
                            std::to_string(limits.max_texture_bytes) + " bytes";
                    return false;
                }
                remaining -= texture.payload().size();
                for (const RenderTexture& existing : prepared.textures) {
                    if (existing.payload().size() > remaining) {
                        error = "prepared textures would exceed " +
                                std::to_string(limits.max_texture_bytes) + " bytes";
                        return false;
                    }
                    remaining -= existing.payload().size();
                }
                brake_left_texture = prepared.textures.size();
                prepared.textures.push_back(texture);
                break;
            }
        }
    }
    prepared.instances.reserve(drafts.size());
    for (const InstanceDraft& draft : drafts) {
        PreparedInstance instance;
        instance.mesh =
            handles.find(std::make_pair(draft.mesh.source, draft.mesh.index))->second;
        std::copy(std::begin(draft.transform), std::end(draft.transform),
                  std::begin(instance.transform));
        std::copy(std::begin(draft.animation_pivot), std::end(draft.animation_pivot),
                  std::begin(instance.animation_pivot));
        instance.placement_exact = draft.placement_exact;
        instance.part_slot = static_cast<std::uint32_t>(draft.slot);
        instance.part_lod = static_cast<std::uint32_t>(draft.lod);
        instance.copy = static_cast<std::uint32_t>(draft.copy);
        instance.mesh_hash = draft.hash;
        if (draft.slot == kSlotBrake && (draft.copy == 0 || draft.copy == 3) &&
            uses_global_brake(instance.mesh)) {
            instance.brake_left_texture = brake_left_texture;
        }
        prepared.instances.push_back(instance);
    }
    for (StockSlotReport& entry : result.slots) {
        if (entry.outcome == StockSlotOutcome::drawn) {
            entry.mesh = handles.find(std::make_pair(entry.geometry_source,
                                                     entry.source_mesh))->second;
        }
    }
    if (!validate_prepared_scene(prepared, error)) {
        error = "prepared stock car scene failed its self-check: " + error;
        return false;
    }
    scene = std::move(prepared);
    report = std::move(result);
    return true;
}

} // namespace

bool prepare_stock_car_scene(
    const std::vector<const GeometryReport*>& sources,
    const RenderTextureSet& textures,
    const CarPartDatabase& parts,
    const CarTypeTable& car_types,
    const StockSceneRequest& request,
    const SceneLimits& limits,
    PreparedCarScene& scene,
    StockSceneReport& report,
    std::string& error) {
    scene = PreparedCarScene{};
    report = StockSceneReport{};
    try {
        if (build_stock_scene(sources, textures, parts, car_types, request, limits,
                              scene, report, error)) {
            return true;
        }
    } catch (const std::bad_alloc&) {
        error = "out of memory while preparing the stock car scene";
    }
    scene = PreparedCarScene{};
    report = StockSceneReport{};
    return false;
}

} // namespace nfsu
