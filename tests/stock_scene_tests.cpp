#include "nfsu/stock_scene.h"
#include "nfsu/texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

// Mesh hashes of the synthetic sources. Every one is arbitrary; only the
// absent pair is deliberately held by no source.
constexpr std::uint32_t kBaseA = 0x00001001U;
constexpr std::uint32_t kBaseB = 0x00001002U;
constexpr std::uint32_t kBodyA = 0x00002001U;
constexpr std::uint32_t kRearBumperA = 0x00003001U;
constexpr std::uint32_t kWheelA = 0x00004001U;
constexpr std::uint32_t kBrokenA = 0x00005001U;
constexpr std::uint32_t kExhaustA = 0x00006001U;
constexpr std::uint32_t kBrakeA = 0x00007001U;
constexpr std::uint32_t kPlateA = 0x00008001U;
constexpr std::uint32_t kMirrorA = 0x0000A001U;
constexpr std::uint32_t kAbsentBase = 0x00009001U;
constexpr std::uint32_t kAbsentSpoiler = 0x00009002U;
// A requested hash and the fallback hash under it, both held by no source.
// The retail pair they stand in for is consecutive the same way: GOLF slot
// 11 SPOILER asks for 0xBE64804B and falls back to 0xBE64804C.
constexpr std::uint32_t kAbsentTrunk = 0x00009003U;
constexpr std::uint32_t kAbsentTrunkFallback = 0x00009004U;

// Source mesh positions of the car file, in the order car_source builds
// them.
constexpr std::size_t kMeshBaseA = 0;
constexpr std::size_t kMeshBodyA = 2;
constexpr std::size_t kMeshRearBumperA = 3;
constexpr std::size_t kMeshBrokenA = 5;
constexpr std::size_t kMeshExhaustA = 6;
constexpr std::size_t kMeshMirrorA = 7;

// Marker name hashes, from the recovered table in part_placement.h.
constexpr std::uint32_t kLeftExhaust = 0xBCF8A18BU;
constexpr std::uint32_t kRightExhaust = 0xBD7CF15EU;
constexpr std::uint32_t kRearLicensePlate = 0xC805F7E0U;

const std::string kCar = "TESTCAR";

nfsu::MeshVertex vertex(float px, float py, float pz) {
    nfsu::MeshVertex out;
    out.px = px;
    out.py = py;
    out.pz = pz;
    return out;
}

void store_u32(unsigned char* out, std::uint32_t value) {
    out[0] = static_cast<unsigned char>(value);
    out[1] = static_cast<unsigned char>(value >> 8U);
    out[2] = static_cast<unsigned char>(value >> 16U);
    out[3] = static_cast<unsigned char>(value >> 24U);
}

// One material group over the whole index stream, with the raw record the
// prepared scene's self-check compares against.
nfsu::MeshMaterial material(std::uint32_t index_count) {
    nfsu::MeshMaterial out;
    out.first_index = 0;
    out.index_count = index_count;
    out.texture_slot = 0;
    store_u32(out.raw + 12, out.index_count);
    store_u32(out.raw + 28, out.texture_slot);
    store_u32(out.raw + 32, out.material_key_slot);
    store_u32(out.raw + 52, out.first_index);
    store_u32(out.raw + 56, out.flags);
    return out;
}

nfsu::Mesh mesh(std::size_t index, const std::string& name, std::uint32_t hash,
                bool valid = true) {
    nfsu::Mesh out;
    out.index = index;
    out.name = name;
    out.directory_hash = hash;
    out.directory_offset = 0x100U + index * 0x80U;
    out.directory_size = 0x40U;
    out.has_directory_entry = true;
    out.valid = valid;
    if (!valid) {
        // parse_geometry leaves a rejected mesh with no decoded records.
        return out;
    }
    out.vertices = {vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F),
                    vertex(0.0F, 1.0F, 0.0F)};
    out.triangles = {{0, 1, 2}};
    out.texture_hashes = {0x11111111U};
    out.materials = {material(3)};
    return out;
}

nfsu::MeshMarker marker(std::uint32_t name_hash, float longitudinal, float lateral,
                        float vertical) {
    nfsu::MeshMarker out;
    out.name_hash = name_hash;
    out.offset = 0x400U;
    out.matrix[0] = out.matrix[5] = out.matrix[10] = out.matrix[15] = 1.0F;
    out.matrix[12] = longitudinal;
    out.matrix[13] = lateral;
    out.matrix[14] = vertical;
    return out;
}

// The car's own geometry file.
nfsu::GeometryReport car_source() {
    nfsu::GeometryReport report;
    report.meshes.push_back(mesh(0, "BASE_A", kBaseA));
    report.meshes.push_back(mesh(1, "BASE_B", kBaseB));
    report.meshes.push_back(mesh(2, "BODY_A", kBodyA));
    nfsu::Mesh rear = mesh(3, "REAR_BUMPER_A", kRearBumperA);
    rear.has_marker_chunk = true;
    rear.markers = {marker(kLeftExhaust, -2.0F, 0.5F, 0.1F),
                    marker(kRearLicensePlate, -2.03F, 0.0F, 0.3F),
                    marker(kRightExhaust, -2.0F, -0.5F, 0.1F)};
    report.meshes.push_back(rear);
    report.meshes.push_back(mesh(4, "WHEEL_A", kWheelA));
    report.meshes.push_back(mesh(5, "BROKEN_A", kBrokenA, false));
    report.meshes.push_back(mesh(6, "EXHAUST_A", kExhaustA));
    report.meshes.push_back(mesh(7, "MIRROR_A", kMirrorA));
    report.mesh_directory_valid = true;
    return report;
}

nfsu::GeometryReport brake_source() {
    nfsu::GeometryReport report;
    report.meshes.push_back(mesh(0, "BRAKE_A", kBrakeA));
    report.mesh_directory_valid = true;
    return report;
}

nfsu::GeometryReport plate_source() {
    nfsu::GeometryReport report;
    report.meshes.push_back(mesh(0, "PLATE_A", kPlateA));
    report.mesh_directory_valid = true;
    return report;
}

struct RecordSpec {
    const char* car = "";
    int slot = 0;
    std::uint8_t level = 0;
    std::string part_name;
    std::array<std::uint32_t, nfsu::kCarPartLodCount> lods{};
    std::int16_t attribute_count = 0;
    std::uint32_t attribute_index = 0;
};

// Records must be sorted by car type hash, which is what the game's binary
// search needs and what parse_car_part_database checks.
nfsu::CarPartDatabase build_database(const std::vector<RecordSpec>& specs) {
    nfsu::CarPartDatabase database;
    std::vector<RecordSpec> sorted = specs;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const RecordSpec& a, const RecordSpec& b) {
                         return nfsu::bin_hash(a.car) < nfsu::bin_hash(b.car);
                     });
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        nfsu::CarPartRecord record;
        record.index = i;
        record.offset = 0x1000U + i * nfsu::kCarPartRecordSize;
        record.name = sorted[i].part_name.empty() ? "PART" : sorted[i].part_name;
        record.car_type_hash = nfsu::bin_hash(sorted[i].car);
        record.part_name_hash = nfsu::bin_hash(record.name);
        record.slot = sorted[i].slot;
        record.upgrade_level = sorted[i].level;
        record.attribute_count = sorted[i].attribute_count;
        record.attribute_index = sorted[i].attribute_index;
        record.lod_mesh_hashes = sorted[i].lods;
        database.records.push_back(record);
    }
    database.attributes.push_back(
        nfsu::CarPartAttribute{nfsu::kDisableSlotAttributeKey,
                               nfsu::bin_hash("DECAL_HOOD")});
    database.version = nfsu::kCarPartDatabaseVersion;
    database.valid = true;
    return database;
}

// BASE falls back at LOD B, the spoiler names a hash no source holds, the
// hood resolves to the mesh that failed validation and disables the hood
// decal, and BODY and ROOF name the same mesh.
std::vector<RecordSpec> stock_records() {
    return {
        {kCar.c_str(), 0, 0, "TESTCAR_BASE", {kBaseA, kAbsentBase, 0, 0}, 0, 0},
        {kCar.c_str(), 2, 0, "TESTCAR_REAR_BUMPER", {kRearBumperA, 0, 0, 0}, 0, 0},
        {kCar.c_str(), 3, 0, "TESTCAR_LEFT_SIDE_MIRROR", {kMirrorA, kMirrorA, 0, 0}, 0, 0},
        {kCar.c_str(), 5, 0, "TESTCAR_BODY", {kBodyA, 0, 0, 0}, 0, 0},
        {kCar.c_str(), 7, 0, "TESTCAR_ROOF", {kBodyA, 0, 0, 0}, 0, 0},
        {kCar.c_str(), 8, 0, "TESTCAR_HOOD", {kBrokenA, 0, 0, 0}, 1, 0},
        {kCar.c_str(), 11, 0, "TESTCAR_SPOILER", {kAbsentSpoiler, 0, 0, 0}, 0, 0},
        {kCar.c_str(), 15, 0, "TESTCAR_EXHAUST", {kExhaustA, 0, 0, 0}, 0, 0},
        {kCar.c_str(), 17, 0, "TESTCAR_WHEEL", {kWheelA, 0, 0, 0}, 0, 0},
        {kCar.c_str(), 19, 0, kCar + "_DECAL_HOOD_RECT_MEDIUM", {kBodyA, 0, 0, 0}, 0, 0},
        {kCar.c_str(), 119, 0, "TESTCAR_CV", {kBodyA, 0, 0, 0}, 0, 0},
        {"BRAKES", 16, 0, "BRAKE_KIT00", {kBrakeA, kBrakeA, 0, 0}, 0, 0},
        {"PLATES", 18, 3, "LICENSE_PLATE_STYLE01", {kPlateA, 0, 0, 0}, 0, 0},
    };
}

nfsu::CarWheelPlacement wheel(std::size_t index, float longitudinal, float lateral) {
    nfsu::CarWheelPlacement out;
    out.index = static_cast<std::uint32_t>(index);
    out.offset = 0x2000U + index * nfsu::kCarWheelRecordSize;
    out.longitudinal = longitudinal;
    out.lateral = lateral;
    out.vertical = 0.3F;
    out.rolling_radius = 0.3F;
    out.value_14 = 0.184F;
    out.value_1c = lateral + (lateral > 0.0F ? 0.09F : -0.09F);
    return out;
}

nfsu::CarTypeTable build_car_types() {
    nfsu::CarTypeTable table;
    nfsu::CarTypeRecord record;
    record.index = 0;
    record.offset = 0x2000;
    record.name = kCar;
    record.lookup_name = kCar;
    record.geometry_path = "CARS\\TESTCAR\\GEOMETRY.BIN";
    record.compressed_path = "CARS\\TESTCAR\\GEOMETRY.LZC";
    record.brand_name = "TEST";
    record.wheels = {wheel(0, 1.0F, 0.7F), wheel(1, 1.0F, -0.7F),
                     wheel(2, -1.2F, -0.7F), wheel(3, -1.2F, 0.7F)};
    table.records.push_back(record);
    table.valid = true;
    return table;
}

const nfsu::StockSlotReport& slot_of(const nfsu::StockSceneReport& report, int slot) {
    return report.slots[static_cast<std::size_t>(slot)];
}

bool prepare(const std::vector<const nfsu::GeometryReport*>& sources,
             const nfsu::StockSceneRequest& request,
             nfsu::PreparedCarScene& scene,
             nfsu::StockSceneReport& report,
             std::string& error,
             const nfsu::SceneLimits& limits = nfsu::SceneLimits{}) {
    return nfsu::prepare_stock_car_scene(sources, nfsu::RenderTextureSet{},
                                         build_database(stock_records()),
                                         build_car_types(), request, limits, scene,
                                         report, error);
}

// prepare() with a part database of the caller's own, for a case that
// needs a record stock_records() does not carry.
bool prepare_records(const std::vector<RecordSpec>& records,
                     const std::vector<const nfsu::GeometryReport*>& sources,
                     const nfsu::StockSceneRequest& request,
                     nfsu::PreparedCarScene& scene,
                     nfsu::StockSceneReport& report,
                     std::string& error) {
    return nfsu::prepare_stock_car_scene(sources, nfsu::RenderTextureSet{},
                                         build_database(records), build_car_types(),
                                         request, nfsu::SceneLimits{}, scene, report,
                                         error);
}

nfsu::StockSceneRequest request_at(std::size_t lod) {
    nfsu::StockSceneRequest request;
    request.car_type_name = kCar;
    request.lod = lod;
    return request;
}

bool empty_outputs(const nfsu::PreparedCarScene& scene,
                   const nfsu::StockSceneReport& report) {
    return scene.meshes.empty() && scene.draws.empty() && scene.instances.empty() &&
           scene.source_mesh_counts.empty() &&
           scene.profile == nfsu::SelectionProfile::all_meshes &&
           report.car_type_name.empty() && report.markers.empty() &&
           report.instances == 0 && report.drawn == 0;
}

// The slot ids this layer hard-codes, against the retail slot table.
void test_slot_ids() {
    check(std::string(nfsu::car_slot_name(3)) == "LEFT_SIDE_MIRROR" &&
              std::string(nfsu::car_slot_name(4)) == "RIGHT_SIDE_MIRROR" &&
              std::string(nfsu::car_slot_name(15)) == "EXHAUST" &&
              std::string(nfsu::car_slot_name(nfsu::kSlotBrake)) == "BRAKE" &&
              std::string(nfsu::car_slot_name(17)) == "WHEEL" &&
              std::string(nfsu::car_slot_name(nfsu::kSlotLicensePlate)) ==
                  "LICENSE_PLATE",
          "the slot ids the draw rules name");
    check(std::string(nfsu::car_draw_pass_name(nfsu::CarDrawPass::world)) == "world" &&
              std::string(nfsu::car_draw_pass_name(nfsu::CarDrawPass::front_end)) ==
                  "front end" &&
              std::string(nfsu::stock_placement_name(nfsu::StockPlacement::marker)) ==
                  "marker" &&
              std::string(nfsu::stock_slot_outcome_name(
                  nfsu::StockSlotOutcome::placement_unreproduced)) ==
                  "placement unreproduced" &&
              std::string(nfsu::selection_profile_name(
                  nfsu::SelectionProfile::stock_car)) == "stock car",
          "profile and outcome names");
}

void test_lod_a() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare(sources, request_at(0), scene, report, error), "lod A: " + error);

    check(scene.profile == nfsu::SelectionProfile::stock_car && scene.selectors.empty() &&
              scene.skipped_invalid_meshes.empty(),
          "stock profile shape");
    check(scene.source_mesh_counts == std::vector<std::size_t>({8, 1, 1}),
          "one mesh count per geometry source");
    check(report.car_type_name == kCar && report.car_type == 0 &&
              report.car_type_hash == nfsu::bin_hash(kCar),
          "car identity");

    // Six meshes: five of the car file and the plate.
    check(scene.meshes.size() == 6, "prepared mesh count");
    if (scene.meshes.size() == 6) {
        const std::vector<std::size_t> expected_sources{0, 0, 0, 0, 0, 2};
        const std::vector<std::size_t> expected_indices{
            kMeshBaseA, kMeshBodyA, kMeshRearBumperA, kMeshExhaustA, kMeshMirrorA, 0};
        for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
            check(scene.meshes[i].source == expected_sources[i] &&
                      scene.meshes[i].source_index == expected_indices[i],
                  "prepared mesh " + std::to_string(i) + " provenance");
            check(scene.meshes[i].selector == nfsu::kNoHandle,
                  "the stock profile has no selectors");
        }
    }

    // One instance per drawn copy, in slot order, and two of them for the
    // exhaust because the host mesh carries two exhaust markers.
    check(scene.instances.size() == 8 && report.instances == 8, "instance count");
    const std::vector<std::uint32_t> slots_in_order{0, 2, 3, 5, 7, 15, 15, 18};
    for (std::size_t i = 0; i < scene.instances.size() && i < slots_in_order.size();
         ++i) {
        check(scene.instances[i].part_slot == slots_in_order[i],
              "instance " + std::to_string(i) + " slot order");
    }

    check(slot_of(report, 0).outcome == nfsu::StockSlotOutcome::drawn &&
              slot_of(report, 0).hash == kBaseA &&
              slot_of(report, 0).model == nfsu::PartModelSource::requested &&
              slot_of(report, 0).placement == nfsu::StockPlacement::car_matrix,
          "BASE draws its own LOD A mesh at the car matrix");
    check(slot_of(report, 8).outcome == nfsu::StockSlotOutcome::mesh_invalid &&
              slot_of(report, 8).source_mesh == kMeshBrokenA,
          "a part whose mesh failed validation is reported, not drawn");
    check(slot_of(report, 11).outcome == nfsu::StockSlotOutcome::mesh_absent &&
              report.absent_hashes == std::vector<std::uint32_t>({kAbsentSpoiler}),
          "a part whose mesh no source holds is reported absent");
    check(slot_of(report, 19).outcome == nfsu::StockSlotOutcome::disabled &&
              !slot_of(report, 19).enabled,
          "the hood's attribute disables the hood decal");
    check(slot_of(report, 119).outcome == nfsu::StockSlotOutcome::no_model,
          "a slot outside 0..29 gets no model");
    check(slot_of(report, 16).outcome ==
                nfsu::StockSlotOutcome::placement_unreproduced &&
            slot_of(report, 17).outcome ==
                nfsu::StockSlotOutcome::placement_unreproduced,
          "the wheel and the brake are not placed by default");
    check(slot_of(report, 16).hash == kBrakeA && slot_of(report, 17).hash == kWheelA,
          "a slot that is not drawn still reports the mesh the rule names");
    check(report.installed == 13 && report.with_model == 12 && report.drawn == 7 &&
              report.fell_back == 0,
          "slot counts");
    check(report.ambiguous_hashes.empty(), "no hash is held twice");
    check(!report.degenerate_wheel_plane, "the four wheels span a plane");

    std::string validation_error;
    check(nfsu::validate_prepared_scene(scene, validation_error),
          "prepared stock scene validates: " + validation_error);
}

// Two slots name one mesh: it is prepared once and drawn twice.
void test_duplicate_part() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare(sources, request_at(0), scene, report, error),
          "duplicate part: " + error);
    const nfsu::StockSlotReport& body = slot_of(report, 5);
    const nfsu::StockSlotReport& roof = slot_of(report, 7);
    check(body.outcome == nfsu::StockSlotOutcome::drawn &&
              roof.outcome == nfsu::StockSlotOutcome::drawn &&
              body.hash == kBodyA && roof.hash == kBodyA,
          "both slots resolve to the same mesh");
    check(body.mesh == roof.mesh && body.mesh != nfsu::kNoHandle,
          "one prepared mesh serves both slots");
    check(body.instance_count == 1 && roof.instance_count == 1 &&
              body.first_instance != roof.first_instance,
          "each slot contributes its own instance");
    std::size_t copies = 0;
    for (const nfsu::PreparedInstance& instance : scene.instances) {
        copies += instance.mesh == body.mesh ? 1U : 0U;
    }
    check(copies == 2, "the shared mesh is drawn twice");
}

// LOD B: the base's own B hash is absent, so the constructor's fallback
// keeps the A mesh.
void test_fallback_lod() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare(sources, request_at(1), scene, report, error), "lod B: " + error);
    const nfsu::StockSlotReport& base = slot_of(report, 0);
    check(base.outcome == nfsu::StockSlotOutcome::drawn &&
              base.requested_hash == kAbsentBase && base.hash == kBaseA &&
              base.model == nfsu::PartModelSource::fallback && base.lod == 1,
          "the base falls back to its LOD A mesh");
    check(report.fell_back == 1, "one slot drew a fallback mesh");
    check(report.absent_hashes == std::vector<std::uint32_t>({kAbsentBase}),
          "the requested LOD B hash is reported absent");
    // The mirror draws at LOD A and B only, and the exhaust and the plate
    // stay at LOD A whatever the car's LOD is.
    check(slot_of(report, 3).outcome == nfsu::StockSlotOutcome::drawn &&
              slot_of(report, 3).lod == 1,
          "the mirror still draws at LOD B");
    check(slot_of(report, 15).lod == 0 && slot_of(report, 18).lod == 0,
          "the exhaust and the plate always take LOD A");
}

// A record whose requested hash is absent and whose fallback hash is absent
// too. This is a live retail path, not a contrived one: eleven of the twenty
// retail racers reach it, among them GOLF slot 11 SPOILER, which asks for
// 0xBE64804B, falls back to 0xBE64804C and finds neither in any loaded
// source, and SKYLINE slot 9 TRUNK with 0xABE49618 and 0xABE49619.
//
// It is also the only shape that reports the fallback hash as absent. A
// fallback that resolves never reaches that branch, and a record that names
// nothing at LOD B leaves the fallback at zero, which is what the spoiler of
// stock_records() does - so every other case in this file would keep
// passing if the fallback were dropped from the absent list.
void test_absent_fallback() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    std::vector<RecordSpec> records = stock_records();
    records.push_back({kCar.c_str(), 9, 0, "TESTCAR_TRUNK",
                       {kAbsentTrunk, kAbsentTrunkFallback, 0, 0}, 0, 0});
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare_records(records, sources, request_at(0), scene, report, error),
          "absent fallback: " + error);
    const nfsu::StockSlotReport& trunk = slot_of(report, 9);
    check(trunk.outcome == nfsu::StockSlotOutcome::mesh_absent,
          "a slot whose fallback is absent too draws nothing");
    check(trunk.requested_hash == kAbsentTrunk && trunk.hash == kAbsentTrunkFallback &&
              trunk.model == nfsu::PartModelSource::fallback,
          "the slot still reports the fallback it could not resolve");
    check(trunk.instance_count == 0, "an absent part places no instance");
    // Both hashes. The requested one is what the record named and the
    // fallback is what the game looks for next, so a caller tracking down
    // which mesh is missing from a source needs the pair, not the half of
    // it the record happens to spell out.
    check(report.absent_hashes == std::vector<std::uint32_t>(
                                      {kAbsentSpoiler, kAbsentTrunk,
                                       kAbsentTrunkFallback}),
          "the requested hash and the fallback hash are both reported absent");
}

void test_mirror_and_pass_lod_rules() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare(sources, request_at(2), scene, report, error), "lod C: " + error);
    check(slot_of(report, 3).outcome == nfsu::StockSlotOutcome::lod_skipped,
          "the mirror is skipped below LOD B");

    nfsu::StockSceneRequest request = request_at(3);
    request.place_wheels_from_culling_table = true;
    check(prepare(sources, request, scene, report, error), "lod D world: " + error);
    check(slot_of(report, 16).lod == 1, "the brake takes LOD B in the world");
    check(slot_of(report, 17).lod == 2, "the wheel is clamped to LOD C");

    request.pass = nfsu::CarDrawPass::front_end;
    check(prepare(sources, request, scene, report, error), "lod D front end: " + error);
    check(slot_of(report, 16).lod == 0, "the brake takes LOD A in the front end");
}

// The upper 3x3 block a wheel or brake copy should carry: the half turn
// for wheel positions 0 and 3, the lateral reflection for their brakes, and
// the identity for positions 1 and 2 (issue #161).
std::array<float, 9> expected_orientation(bool brake, std::size_t position) {
    if (position == 1 || position == 2) {
        return {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    }
    if (brake) {
        return {1.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    }
    return {-1.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
}

bool has_orientation(const nfsu::PreparedInstance& instance,
                     const std::array<float, 9>& expected) {
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            if (instance.transform[row * 4 + column] != expected[row * 3 + column]) {
                return false;
            }
        }
    }
    return true;
}

// The stock offset and lateral scale from the wheel records reach the four
// wheel and brake copies. The unselected wide-body and marker branches
// leave them marked as approximations.
void test_wheel_placement() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::StockSceneRequest request = request_at(0);
    request.place_wheels_from_culling_table = true;
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare(sources, request, scene, report, error), "placed wheels: " + error);
    check(slot_of(report, 17).outcome == nfsu::StockSlotOutcome::drawn &&
              slot_of(report, 17).instance_count == nfsu::kCarWheelCount &&
              slot_of(report, 17).placement == nfsu::StockPlacement::wheel_table,
          "the wheel draws four copies");
    check(slot_of(report, 16).outcome == nfsu::StockSlotOutcome::drawn &&
              slot_of(report, 16).instance_count == nfsu::kCarWheelCount,
          "the brake draws four copies");
    check(scene.instances.size() == 16, "eight body copies and eight wheel copies");

    const nfsu::StockSlotReport& wheels = slot_of(report, 17);
    for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
        const nfsu::PreparedInstance& instance =
            scene.instances[wheels.first_instance + w];
        const float* position = report.placements[w].position;
        check(!instance.placement_exact,
              "wheel copy " + std::to_string(w) + " is not an exact placement");
        const auto& wheel = build_car_types().records[0].wheels[w];
        check(instance.transform[12] == position[0] &&
                  std::abs(instance.transform[13] - wheel.value_1c) < 1e-6F &&
                  instance.transform[14] == position[2] &&
                  instance.transform[15] == 1.0F,
              "wheel copy " + std::to_string(w) + " carries the drawn lateral offset");
        // The wheel model is authored for the right-hand side. The draw
        // half-turns it at positions 0 and 3, the two left wheels, so that
        // the rim faces out there too; without that a camera on the left
        // sees the inboard backing quad (issue #161).
        check(has_orientation(instance, expected_orientation(false, w)),
              "wheel copy " + std::to_string(w) +
                  (w == 0 || w == 3 ? " is half-turned" : " is not turned"));
        check(instance.copy == w && instance.part_slot == 17,
              "wheel copy " + std::to_string(w) + " provenance");
    }
    const nfsu::StockSlotReport& brake = slot_of(report, 16);
    for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
        const nfsu::PreparedInstance& instance = scene.instances[brake.first_instance + w];
        const float* position = report.placements[6 + w].position;
        check(!instance.placement_exact,
              "brake copy " + std::to_string(w) + " is not an exact placement");
        const auto& wheel = build_car_types().records[0].wheels[w];
        const float half = wheel.value_14 * 0.5F;
        const float pivot = wheel.value_1c - (wheel.value_1c > 0.0F ? half : -half);
        check(instance.transform[12] == position[0] &&
                  std::abs(instance.transform[13] - pivot) < 1e-6F &&
                  instance.transform[14] == position[2] &&
                  instance.transform[15] == 1.0F,
              "brake copy " + std::to_string(w) + " carries the drawn lateral offset");
        check(has_orientation(instance, expected_orientation(true, w)),
              "brake copy " + std::to_string(w) +
                  (w == 0 || w == 3 ? " is reflected" : " is not reflected"));
        check(instance.copy == w && instance.part_slot == 16,
              "brake copy " + std::to_string(w) + " provenance");
    }
    std::string validation_error;
    check(nfsu::validate_prepared_scene(scene, validation_error),
          "placed stock scene validates: " + validation_error);
}

// The exhaust and the plate are placed by the markers of the meshes the
// collector walks, and a car with no marker of a role draws nothing there.
void test_marker_placement() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare(sources, request_at(0), scene, report, error), "markers: " + error);
    check(report.markers.size() == 3, "every marker of an enabled model is collected");
    if (report.markers.size() == 3) {
        check(report.markers[0].slot == 2 && report.markers[0].name_hash == kLeftExhaust &&
                  report.markers[0].role == nfsu::CarMarkerRole::exhaust &&
                  report.markers[0].geometry_source == 0 &&
                  report.markers[0].source_mesh == kMeshRearBumperA &&
                  report.markers[0].index == 0,
              "marker provenance and order");
        check(report.markers[1].role == nfsu::CarMarkerRole::license_plate &&
                  report.markers[2].name_hash == kRightExhaust,
              "marker roles");
    }
    const nfsu::StockSlotReport& exhaust = slot_of(report, 15);
    check(exhaust.outcome == nfsu::StockSlotOutcome::drawn &&
              exhaust.instance_count == 2 &&
              exhaust.placement == nfsu::StockPlacement::marker,
          "one exhaust copy per exhaust marker");
    check(slot_of(report, 18).instance_count == 1, "one plate copy");

    // The transform is the quarter turn premultiplied onto the marker.
    float expected[16] = {};
    check(nfsu::car_marker_placement(car.meshes[kMeshRearBumperA].markers[0], expected),
          "reference marker placement");
    const nfsu::PreparedInstance& instance = scene.instances[exhaust.first_instance];
    bool same = instance.placement_exact;
    for (std::size_t i = 0; i < 16; ++i) {
        same = same && instance.transform[i] == expected[i];
    }
    check(same, "the exhaust copy uses quarter turn times marker matrix");

    // A car file with no markers at all: both slots report no marker.
    nfsu::GeometryReport bare = car_source();
    bare.meshes[kMeshRearBumperA].markers.clear();
    bare.meshes[kMeshRearBumperA].has_marker_chunk = false;
    const std::vector<const nfsu::GeometryReport*> bare_sources{&bare, &brakes, &plates};
    check(prepare(bare_sources, request_at(0), scene, report, error),
          "no markers: " + error);
    check(report.markers.empty() &&
              slot_of(report, 15).outcome == nfsu::StockSlotOutcome::no_marker &&
              slot_of(report, 18).outcome == nfsu::StockSlotOutcome::no_marker,
          "without a marker neither part is drawn");
    check(scene.instances.size() == 5, "the marker-placed copies are gone");
    for (const nfsu::PreparedMesh& prepared : scene.meshes) {
        check(prepared.directory_hash != kExhaustA && prepared.directory_hash != kPlateA,
              "an undrawn mesh is not prepared");
    }
}

// A disabled slot contributes no markers, which is how the collector
// behaves at 0x0056BD57.
void test_disabled_marker_host() {
    nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    // Move the markers onto the mesh the disabled hood decal draws.
    car.meshes[kMeshBodyA].markers = car.meshes[kMeshRearBumperA].markers;
    car.meshes[kMeshBodyA].has_marker_chunk = true;
    car.meshes[kMeshRearBumperA].markers.clear();
    car.meshes[kMeshRearBumperA].has_marker_chunk = false;
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare(sources, request_at(0), scene, report, error),
          "disabled marker host: " + error);
    // Slots 5 and 7 draw the same mesh and are both enabled, so its three
    // markers are collected twice; slot 19 is disabled and adds none.
    check(report.markers.size() == 6, "each enabled slot contributes its own markers");
    for (const nfsu::StockMarker& collected : report.markers) {
        check(collected.slot == 5 || collected.slot == 7,
              "the disabled slot contributes no marker");
    }
}

// A hash two sources hold resolves to the first source, and says so.
void test_ambiguous_hash() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    nfsu::GeometryReport shadow;
    shadow.meshes.push_back(mesh(0, "BODY_A_COPY", kBodyA));
    shadow.mesh_directory_valid = true;
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates,
                                                           &shadow};
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare(sources, request_at(0), scene, report, error), "ambiguous: " + error);
    check(report.ambiguous_hashes == std::vector<std::uint32_t>({kBodyA}),
          "the repeated hash is reported");
    check(slot_of(report, 5).geometry_source == 0,
          "the first loaded source wins the hash");
    check(scene.source_mesh_counts.size() == 4, "every source is counted");
}

void test_invalid_placement() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;

    // A wheel table that does not yield finite placements.
    nfsu::CarTypeTable broken = build_car_types();
    broken.records[0].wheels[2].lateral = std::numeric_limits<float>::infinity();
    check(!nfsu::prepare_stock_car_scene(sources, nfsu::RenderTextureSet{},
                                         build_database(stock_records()), broken,
                                         request_at(0), nfsu::SceneLimits{}, scene,
                                         report, error),
          "a wheel table that is not finite is refused");
    check(error.find("TESTCAR") != std::string::npos, "the error names the car type");
    check(empty_outputs(scene, report), "a refused request leaves both outputs empty");

    // A marker matrix that is not finite. parse_geometry refuses one, so
    // this can only be built by hand; the layer still refuses it rather
    // than placing a part at a position that is not a number.
    nfsu::GeometryReport bad = car_source();
    bad.meshes[kMeshRearBumperA].markers[0].matrix[5] =
        std::numeric_limits<float>::quiet_NaN();
    const std::vector<const nfsu::GeometryReport*> bad_sources{&bad, &brakes, &plates};
    check(!prepare(bad_sources, request_at(0), scene, report, error),
          "a marker matrix that is not finite is refused");
    check(error.find("not finite") != std::string::npos,
          "the marker error says what is wrong: " + error);
    check(empty_outputs(scene, report), "a refused marker leaves both outputs empty");
}

void test_request_and_source_errors() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;

    check(!prepare(sources, request_at(nfsu::kCarPartLodCount), scene, report, error),
          "a LOD index outside the four is refused");
    nfsu::StockSceneRequest unknown = request_at(0);
    unknown.car_type_name = "NOSUCHCAR";
    check(!prepare(sources, unknown, scene, report, error),
          "an unknown car type is refused");
    nfsu::StockSceneRequest nameless = request_at(0);
    nameless.car_type_name.clear();
    check(!prepare(sources, nameless, scene, report, error),
          "an empty car type name is refused");

    check(!prepare({}, request_at(0), scene, report, error),
          "a request with no geometry source is refused");
    check(!prepare({&car, nullptr}, request_at(0), scene, report, error),
          "a null geometry source is refused");
    nfsu::GeometryReport unverified = car_source();
    unverified.mesh_directory_valid = false;
    check(!prepare({&unverified, &brakes, &plates}, request_at(0), scene, report, error),
          "a source without a verified mesh directory is refused");

    nfsu::CarPartDatabase invalid;
    check(!nfsu::prepare_stock_car_scene(sources, nfsu::RenderTextureSet{}, invalid,
                                         build_car_types(), request_at(0),
                                         nfsu::SceneLimits{}, scene, report, error),
          "an invalid part database is refused");
    nfsu::CarTypeTable invalid_types;
    check(!nfsu::prepare_stock_car_scene(sources, nfsu::RenderTextureSet{},
                                         build_database(stock_records()), invalid_types,
                                         request_at(0), nfsu::SceneLimits{}, scene,
                                         report, error),
          "an invalid car type table is refused");

    nfsu::SceneLimits limits;
    limits.max_instances = 4;
    check(!prepare(sources, request_at(0), scene, report, error, limits),
          "the instance limit is enforced");
    check(error.find("instances") != std::string::npos,
          "the limit error names instances: " + error);
    check(empty_outputs(scene, report), "a refused limit leaves both outputs empty");

    // The source limit, and that it is checked before the source list is
    // walked. The second entry here is null, which the per-source loop
    // further down would refuse: naming the limit rather than the null
    // source is what says the count was checked first, so an over-long
    // list costs one comparison and not a pass over every mesh of every
    // source in it.
    nfsu::SceneLimits one_source;
    one_source.max_sources = 1;
    check(!prepare({&car, nullptr}, request_at(0), scene, report, error, one_source),
          "the source limit is enforced");
    check(error.find("geometry sources; the limit is 1") != std::string::npos,
          "the limit is refused before the sources are read: " + error);
    check(empty_outputs(scene, report), "a refused source limit leaves both outputs empty");
}

// validate_prepared_scene is a backend's guard against a scene that was
// changed after it was prepared.
//
// Every case here names the guard it is aiming at and checks the reported
// text, not just that some guard refused the scene. A tamper usually trips
// more than one check, so a bare "it was refused" passes just as happily
// when a later guard catches what an earlier one was meant to: removing the
// instance mesh bound still leaves the no-instance check refusing this
// scene, and the two non-stock-profile guards each refuse what the other
// was written for. Naming the text is what makes one case pin one guard.
void test_validation_rejects_tampering() {
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::PreparedCarScene prepared;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare(sources, request_at(0), prepared, report, error),
          "tamper base: " + error);
    const auto refused = [&error](const nfsu::PreparedCarScene& scene,
                                  const std::string& label,
                                  const std::string& expected) {
        error.clear();
        check(!nfsu::validate_prepared_scene(scene, error), label + " is rejected");
        check(error.find(expected) != std::string::npos,
              label + " names '" + expected + "': " + error);
    };

    // The mesh handle is what indexes the drawn-mesh list a few lines
    // further down, so this bound is the one whose loss is an out-of-bounds
    // write rather than a wrong answer.
    nfsu::PreparedCarScene scene = prepared;
    scene.instances[0].mesh = scene.meshes.size();
    refused(scene, "an instance outside the mesh list", "invalid mesh handle");

    scene = prepared;
    scene.instances[0].transform[3] = std::numeric_limits<float>::quiet_NaN();
    refused(scene, "a transform that is not finite", "not finite");

    scene = prepared;
    scene.instances[0].mesh_hash = 0x12345678U;
    refused(scene, "an instance hash that is not its mesh's",
            "names a hash its prepared mesh does not have");

    scene = prepared;
    scene.instances.erase(scene.instances.begin());
    refused(scene, "a prepared mesh with no instance", "has no instance to draw it");

    // The two non-stock-profile guards, one at a time. A stock scene that
    // only changes profile trips both, so each case here removes what the
    // other guard looks at: no instances for the source-count guard, one
    // source for the instance guard.
    scene = prepared;
    scene.profile = nfsu::SelectionProfile::all_meshes;
    scene.instances.clear();
    refused(scene, "several sources outside the stock profile",
            "names several geometry sources");

    scene = prepared;
    scene.profile = nfsu::SelectionProfile::all_meshes;
    scene.source_mesh_counts.resize(1);
    refused(scene, "instances outside the stock profile", "carries instances");

    scene = prepared;
    scene.source_mesh_counts.resize(1);
    refused(scene, "a mesh outside its source",
            "is not in ascending source order inside the source");

    scene = prepared;
    scene.skipped_invalid_meshes.push_back(1);
    refused(scene, "a stock profile that skips a mesh",
            "the stock car profile skips no meshes");
}

void test_installed_part_overrides() {
    std::vector<RecordSpec> records = stock_records();
    records.push_back({kCar.c_str(), 5, 1, "TESTCAR_KIT01_BODY",
                       {kRearBumperA, 0, 0, 0}, 0, 0});
    records.push_back({kCar.c_str(), 17, 1, "TESTCAR_KIT01_WHEEL",
                       {kBaseA, 0, 0, 0}, 0, 0});
    records.push_back({"WHEELS", 17, 1, "BBS_TEST_WHEEL",
                       {kBaseA, 0, 0, 0}, 0, 0});
    records.push_back({"SKYLINE", 5, 1, "SKYLINE_KIT01_BODY",
                       {kRearBumperA, 0, 0, 0}, 0, 0});
    const nfsu::GeometryReport car = car_source();
    const nfsu::GeometryReport brakes = brake_source();
    const nfsu::GeometryReport plates = plate_source();
    const std::vector<const nfsu::GeometryReport*> sources{&car, &brakes, &plates};
    nfsu::StockSceneRequest request = request_at(0);
    request.place_wheels_from_culling_table = true;
    request.installed_parts.names[5] = "TESTCAR_KIT01_BODY";
    request.installed_parts.names[17] = "TESTCAR_KIT01_WHEEL";
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    check(prepare_records(records, sources, request, scene, report, error),
          "explicit body and wheels prepare: " + error);
    if (report.overridden == 2) {
        check(report.slots[5].overridden && report.slots[17].overridden &&
                  !report.slots[2].overridden && report.slots[5].hash == kRearBumperA &&
                  report.slots[17].hash == kBaseA &&
                  report.slots[17].instance_count == 4,
              "overrides use the selected meshes and stock placement rules");
    } else {
        check(false, "two overridden slots are counted");
    }
    request.installed_parts.names[17] = "BBS_TEST_WHEEL";
    check(prepare_records(records, sources, request, scene, report, error) &&
              report.slots[17].overridden && report.slots[17].instance_count == 4,
          "shared WHEELS type is valid for the wheel slot: " + error);
    request.installed_parts.names[17] = "TESTCAR_KIT01_WHEEL";
    request.installed_parts.names[5] = "SKYLINE_KIT01_BODY";
    check(!prepare_records(records, sources, request, scene, report, error) &&
              error.find("slot 5 BODY") != std::string::npos &&
              error.find("another car type") != std::string::npos &&
              empty_outputs(scene, report),
          "foreign car type is refused by slot: " + error);
    request.installed_parts.names[5] = "NO_SUCH_BODY";
    check(!prepare_records(records, sources, request, scene, report, error) &&
              error.find("slot 5 BODY") != std::string::npos &&
              error.find("NO_SUCH_BODY") != std::string::npos &&
              empty_outputs(scene, report),
          "unknown override names its slot and clears outputs: " + error);
}

} // namespace

int main() {
    test_slot_ids();
    test_lod_a();
    test_duplicate_part();
    test_fallback_lod();
    test_absent_fallback();
    test_mirror_and_pass_lod_rules();
    test_wheel_placement();
    test_marker_placement();
    test_disabled_marker_host();
    test_ambiguous_hash();
    test_invalid_placement();
    test_request_and_source_errors();
    test_validation_rejects_tampering();
    test_installed_part_overrides();
    if (g_failures != 0) {
        std::cerr << g_failures << " test failure(s)\n";
        return 1;
    }
    std::cout << "all stock scene tests passed\n";
    return 0;
}
