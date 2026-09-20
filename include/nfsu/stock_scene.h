#pragma once

#include "nfsu/car_parts.h"
#include "nfsu/geometry.h"
#include "nfsu/part_placement.h"
#include "nfsu/scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// The stock-car profile of PreparedCarScene: the recovered stock part set
// (include/nfsu/car_parts.h), the per-LOD mesh hash of every installed part
// with the fallback the game applies for a missing mesh, and the recovered
// placement (include/nfsu/part_placement.h), turned into one validated draw
// list with one instance per drawn copy.
//
// Every rule below is a rule this project traced in the retail speed.exe;
// addresses are its virtual addresses and the evidence is in
// docs/CAR_SCENE.md and docs/PART_PLACEMENT.md. Nothing here invents a
// rule: where the executable does something this project did not
// reproduce, the slot is reported rather than placed, and the one
// stand-in this layer can produce is opt-in and marked.
//
// This layer reads no files. It takes parsed databases and parsed geometry
// and produces a PreparedCarScene, which is why it sits above the
// backend-neutral scene layer rather than inside it.

// Which pass the car is drawn in. The only difference this layer
// reproduces is the brake model's LOD: CarRenderInfo::Render takes the
// brake at LOD A in the front end and at LOD B in the world
// (docs/CAR_SCENE.md, "LOD models, choice and fallback").
enum class CarDrawPass { world, front_end };

// "world" or "front end".
const char* car_draw_pass_name(CarDrawPass pass) noexcept;

struct StockSceneRequest {
    // Hashed for the part lookup and formatted into the stock decal names,
    // and matched against CarTypeRecord::name for the wheel table. The two
    // retail name fields are equal in all 35 records, so one name serves
    // both; see docs/CAR_SCENE.md.
    std::string car_type_name;
    // The car-wide LOD index, 0 for A to kCarPartLodCount - 1 for D.
    // CarRenderInfo::Render picks it once per car per frame from the car's
    // pixel size; a caller that has one uses car_lod_for_pixel_size. Four
    // slots override it, see StockSlotReport::lod.
    std::size_t lod = 0;
    CarDrawPass pass = CarDrawPass::world;
    // The drawn wheel and brake matrices are not reproduced by this
    // project: docs/PART_PLACEMENT.md traces the lateral component of row 3
    // to an expression whose floating-point order was not verified, and
    // Render discards the component it was given and rebuilds it
    // (0x0056FBA5 for the wheel, 0x0056FBB3 for the brake).
    //
    // False, the default, reports both slots as
    // StockSlotOutcome::placement_unreproduced and draws neither. True
    // places them at the culling placements of
    // derive_cullable_part_placements, whose three floats are copied
    // verbatim out of the wheel record, and marks every instance it
    // produces with PreparedInstance::placement_exact false: the
    // longitudinal and vertical components are the record's, the lateral
    // one is a stand-in for a term the executable computes differently,
    // and the spin, steering, mirroring and half turns the draw applies are
    // not there at all.
    bool place_wheels_from_culling_table = false;
};

// What became of one car part slot. Only `drawn` puts geometry in the
// scene.
enum class StockSlotOutcome {
    not_installed,  // the stock-part routine filled no record for the slot
    skipped,        // the stock-part routine does not fill this slot
    no_model,       // the slot is outside 0..kCarModelSlotCount-1, so
                    // CarRenderInfo builds it no model (0x56ab87)
    disabled,       // the enable byte 0x57ea80 derived is 0
    lod_skipped,    // the mirrors, which draw at LOD A and B only
    no_mesh_named,  // the record names no mesh at this LOD and the
                    // fallback had none to offer either
    mesh_absent,    // a hash was named, by the record or by the fallback,
                    // and no loaded geometry source holds it
    mesh_invalid,   // the source holds it but the mesh did not validate
    no_marker,      // exhaust or licence plate: no collected marker names it
    placement_unreproduced,  // see StockSceneRequest above
    drawn
};

const char* stock_slot_outcome_name(StockSlotOutcome outcome) noexcept;

// Which of the three placement mechanisms of docs/PART_PLACEMENT.md the
// slot uses.
enum class StockPlacement {
    // Drawn at the car's own matrix, that is at the identity relative to
    // the car. Every body part.
    car_matrix,
    // The four wheel positions of the car type table, through the culling
    // placements. Only with StockSceneRequest::place_wheels_from_culling_table.
    wheel_table,
    // One copy per collected attachment marker, at
    // car_marker_quarter_turn() * marker matrix.
    marker
};

const char* stock_placement_name(StockPlacement placement) noexcept;

struct StockSlotReport {
    int slot = 0;
    StockPartRule rule = StockPartRule::none;
    // Index into CarPartDatabase::records, or kNoCarPart.
    std::size_t record = kNoCarPart;
    // The enable byte 0x57ea80 derived for the slot.
    bool enabled = true;
    // The LOD index this slot draws. It is the request's index for every
    // generic slot and for the mirrors; LOD A for the exhaust and the
    // licence plate, LOD A or B for the brake depending on the pass, and
    // min(lod, 2) for the wheel. Meaningful once the slot has a model.
    std::size_t lod = 0;
    // The record's own hash for that LOD, 0 when it names no mesh.
    std::uint32_t requested_hash = 0;
    // The hash the model ends up with after resolve_part_models, and where
    // it came from.
    std::uint32_t hash = 0;
    PartModelSource model = PartModelSource::none;
    StockSlotOutcome outcome = StockSlotOutcome::not_installed;
    StockPlacement placement = StockPlacement::car_matrix;
    // Where the mesh was found, and its handle in the prepared scene.
    // Valid only for outcome drawn and mesh_invalid; the scene handle is
    // kNoHandle unless the slot is drawn.
    std::size_t geometry_source = 0;
    std::size_t source_mesh = 0;
    std::size_t mesh = kNoHandle;
    // The instances this slot contributed: PreparedCarScene::instances
    // [first_instance, first_instance + instance_count).
    std::size_t first_instance = 0;
    std::size_t instance_count = 0;
};

// One collected attachment marker, in the order the collector at
// 0x0056BDB0 appends it: slot order, then record order within the mesh of
// that slot's LOD A model.
struct StockMarker {
    int slot = 0;
    std::uint32_t name_hash = 0;
    CarMarkerRole role = CarMarkerRole::unknown;
    std::size_t geometry_source = 0;
    std::size_t source_mesh = 0;
    // Position in Mesh::markers.
    std::size_t index = 0;
};

struct StockSceneReport {
    std::string car_type_name;
    std::uint32_t car_type_hash = 0;
    // Index into CarTypeTable::records.
    std::size_t car_type = kNoCarType;
    std::size_t lod = 0;
    CarDrawPass pass = CarDrawPass::world;
    std::array<StockSlotReport, kCarSlotCount> slots{};
    // The 14 culling placements this car's wheel table gives, whether or
    // not the wheels were placed from them.
    std::array<PartPlacement, kCullableCarPartCount> placements{};
    // 0x56b3e2 took the fallback branch: the four wheel centres do not
    // span a plane. Reported, not refused, because the game carries on.
    bool degenerate_wheel_plane = false;
    // Every marker the collector accepts, in its order.
    std::vector<StockMarker> markers;
    std::size_t installed = 0;       // slots with a record
    std::size_t with_model = 0;      // of those, slots 0..29
    std::size_t drawn = 0;           // slots whose outcome is drawn
    std::size_t fell_back = 0;       // drawn slots on a fallback hash
    std::size_t instances = 0;       // == PreparedCarScene::instances.size()
    // Hashes a stock part asked for that no loaded source holds, ascending
    // and unique.
    std::vector<std::uint32_t> absent_hashes;
    // Hashes more than one loaded source holds, ascending and unique. The
    // game's solid lookup searches the loaded lists in order and takes the
    // first hit (0x540680), and so does this layer; the list records that
    // the choice was made.
    std::vector<std::uint32_t> ambiguous_hashes;
};

// Builds the stock-car profile for one car.
//
// `sources` holds the loaded geometry files in load order, none of them
// null, each with a verified mesh directory: the solid lookup the fallback
// rule depends on binary-searches those directories, so without one a
// missing hash would prove nothing. For the retail 240SX the three files
// are Cars/240SX, Cars/BRAKES and Cars/PLATES.
//
// `parts` and `car_types` are the two GLOBALB.BUN tables, both valid.
//
// On success `scene` holds SelectionProfile::stock_car: every mesh a stock
// part draws at the requested LOD, once, in ascending (source, index)
// order, and one instance per drawn copy in ascending slot order. A mesh
// two slots resolve to is prepared once and drawn by both. `report`
// describes all 120 slots whether or not they draw.
//
// Fails, with `error` set and both outputs left empty, for an invalid
// database or table, an unknown car type, a LOD index outside
// 0..kCarPartLodCount-1, a wheel table that does not yield finite
// placements, a marker matrix that is not finite, a source without a
// verified directory, a limit, or a failed self-check. A part that names
// no mesh, names a mesh no source holds, or names a mesh that did not
// validate is reported in `report` and is not an error: the game draws
// nothing there either.
bool prepare_stock_car_scene(
    const std::vector<const GeometryReport*>& sources,
    const RenderTextureSet& textures,
    const CarPartDatabase& parts,
    const CarTypeTable& car_types,
    const StockSceneRequest& request,
    const SceneLimits& limits,
    PreparedCarScene& scene,
    StockSceneReport& report,
    std::string& error);

} // namespace nfsu
