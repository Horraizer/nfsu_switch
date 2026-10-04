// Data-driven car selection over the retail car type table (issue #315).
//
// Without a retail root this runs the unit half: what resolve_car_assets
// derives and refuses on a hand-built table, and the reader's misuse guard.
// With a retail root (argv[1], passed by the nfsu_car_selection_retail
// check) it runs the whole playable inventory through the real reader,
// preparation, skin, render and encoding paths the applications use, plus
// the per-car handling record and a bounded deterministic drive of every
// racer and of the Switch race's own load path.
//
// Nothing writes under the retail root; every output the test produces is a
// hash printed on stdout.

#include "nfsu/arg_path.h"
#include "nfsu/car_assets.h"
#include "nfsu/car_shadow.h"
#include "nfsu/part_placement.h"
#include "nfsu/platform.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/race_run.h"
#include "nfsu/race_session.h"
#include "nfsu/race_setup.h"
#include "nfsu/stock_frame.h"
#include "nfsu/stock_view.h"
#include "nfsu/texture.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "nfsu/posix_file_system.h"

using namespace nfsu;

namespace {

void require(bool pass, const std::string& label) {
    if (!pass) throw std::runtime_error("FAIL: " + label);
}

// Field-by-field placement comparison: the aggregates carry no operator==.
bool same_placements(const std::array<PartPlacement, kCullableCarPartCount>& a,
                     const std::array<PartPlacement, kCullableCarPartCount>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].position[0] != b[i].position[0] || a[i].position[1] != b[i].position[1] ||
            a[i].position[2] != b[i].position[2] || a[i].source != b[i].source ||
            a[i].index_space != b[i].index_space || a[i].sources != b[i].sources ||
            a[i].source_count != b[i].source_count ||
            a[i].degenerate_plane != b[i].degenerate_plane) {
            return false;
        }
    }
    return true;
}

// The retail roster of the pinned installation, in car type record order.
// This is a pin of the installed table, not an assumption: the test derives
// the list from GLOBALB.BUN and compares it with these names.
const char* const kRetailPlayables[20] = {
    "GOLF",    "FOCUS",   "MIATA",     "NEON",    "CIVIC",
    "PEUGOT",  "CELICA",  "ECLIPSE",   "RX7",     "SUPRA",
    "S2000",   "RSX",     "IMPREZA",   "LANCER",  "INTEGRA",
    "TIBURON", "350Z",    "SENTRA",    "240SX",   "SKYLINE"};

// The painted stock frame of every racer through the per-car paths at the
// stock view's default camera, and the 1200-step proving-ground drive of its
// own handling record. Pinned at issue #315's base for the retail
// installation the fixture verifies; a change that moves any one of them
// fails its row. The 240SX frame row is kStock240sxPaintedFrameSha256 and is
// additionally compared with that constant below.
//
// Round 3 re-pinned fourteen frame rows (FOCUS, MIATA, CIVIC, PEUGOT,
// CELICA, ECLIPSE, RX7, SUPRA, S2000, RSX, IMPREZA, LANCER, INTEGRA, 350Z):
// their lamp and window material groups had drawn the diagnostic checker
// since the first pin, and the runtime lamp/window name resolution
// (alias_stock_lamp_textures, alias_stock_window_textures) now binds their
// real textures. The six cars whose packs ship position-name lamp records -
// GOLF, NEON, TIBURON, SENTRA, 240SX, SKYLINE - and every drive row are
// byte-identical to the first pin.
const char* const kRetailFramePins[20] = {
    "54E769A9AA2D44C97470F3A6A9C41A01CA93BBC65121D75AECB8504F0ADA1204",
    "51CD015A670CE0DB1A48025E0C5A48CABA8B9972679498BDA77EE6336BB6A463",
    "818A71F95DA92E819431AD2C425C8574A94556DA474EA24961C3296DF137AE78",
    "FAF13BE7AA5E7295EB00458B306984904C03014AE9500FBE24C0285320282FBD",
    "91F751EE952BCE8C02F6E90DAD4F2B70659AE93A758890626946376BF6DE2445",
    "155B76F2BEB01295EE115ADA43BE0EAAB00EAE36F43AA950732DB89F0D2F6F60",
    "47184762A747E80FD3C24E12865A56CEE3E4A9B9E7F7769DC658FEC207A6C577",
    "8C118F9E4E0524FAFC953CAB7EAD3F30629E10FA17A97F5FEBAB46FBA2BF36A8",
    "E73DFBDF2F64D749AEC6B9DCF17274C3ED103D932F3FD2A08EF85AD131306C72",
    "FA7E650CD19136031B9FCEE5FCCD7C4C5F998F5F507422A0BFF7E2133F51BB0E",
    "C634998DD6DCFE97AAFE3C0DEAD0F24685032692CD94CD0DA2DBD52FBA4831AA",
    "7CBD1B76768889090FBB5E7E490876F1828C1E9C874AC748A54F40A61125456D",
    "91DE7A8DA265B0E93961EBC33C7E712D8AA70136C2C87646C90191E31AE93E02",
    "7B7FA7D3657E5AE26F03E1AD785D09F0EA98B91A46F18E8472AD9DA89D4057FB",
    "804488AAF847A5167EF9D1965B44560E90A6EEF4FDC3C73CD54A03EAD0BA9515",
    "DE1A9248FDF1F586A5888026A7A5A9AF0840642C6192788092BE6CF4D504B3B4",
    "315562DE7E65378AA9A702A5D5562B59898DE92D72E73A8AE66E9846866D3533",
    "5BBB4ACDED09D854F58B4CF37801703F283443613FDCDF1F51B76F75EF76C36B",
    "FE7506F173A871A208F94451DFC71A3F3AEBF1EBE677C28AF2B73AD9CEA3AEE8",
    "AF86D7FCA20E6DE052BA19EE9AB4B6A1C968B9532EDBFE224966DF6726C839A1",
};

const char* const kRetailDrivePins[20] = {
    "FE6D3BDAD76BDCB359C3DB199821AC38DE062A223BE7F0F1869801861E0378A2",
    "E22C0DAED1FFBD23C6D337427132BA0F3FC5594C67B6705342ECAC6DD988A778",
    "6BF43C7EE4D40EC9181F8DC92881FBC23DCD30EE5B46CCCCA77A80E154366FE5",
    "C81A9E38C8F40AED800558E76086432DC1FEDC0ADF5783B9F8841EEAC8C3DCCD",
    "3727B430C4B81C1FA77E4450B2922D789F6936BB29AFA47D9AE9E77EBB3462CB",
    "73F12044752014926BBEDBABBB1CDD3B579CAF1D0E6E37655B155C41677F4F5A",
    "50B125EDE628310236C42DB53EB896AFAE6CB0A497163AD5CDCBE2D84CC1D90B",
    "5D33C9FE72A5AFC87A41A10AC3861E75014EB3B237849954FEB194BACC8BAE64",
    "59341D484D7AD72C5D3EFC57A103A0C4457B014AE75529F84C6F9ED2A56E49DA",
    "5AD910958328F4A6D94885613C66BEB92C32EBA325DFDBD31D246C930B94FA73",
    "BCFA2D72AF0F81BDCAD93A4E7952D6026BFA455CA2AD1BE7B8F6758BE5E24F5A",
    "ACF0D248C54E43FA868086D41698FDD0F2BAEAD778E3F947CA9F3B08EFCB06A5",
    "89A803E65641587E63C050D03F3FAA2163F2A25248357270F8B668721C199CBF",
    "8618B863EFA6F2F07D3F2946F47EA5D209BBDF2095181E4C015F81421F8DFEC6",
    "85C4B14B5E74CABE4064C013AE111C42010AEF466993929C7AABA1E3272AE7A9",
    "DE38C10E66512598BE938A9F5B87546B8918A364338F9971D450D294E9262002",
    "7F4D8C4C8A45DD4BCF0181D2EEEC67A9F474E373793A1D0648D9A1D50CBF21DA",
    "0121CFFB4FBC8F0AEABF91A47EC9EB0F3EABF3654B782DF8F92D505D53F13377",
    "ACCCDF91F45A3EDCA11758FD173CB0F52AA2BA73A8076D495C528525C054A323",
    "E62CEA9DBC240B5AACB312D523CA7C7B27B9706286AF8FF4F250A9C4C1054F09",
};

// The event0 player car's own 900-step sequence through the Switch race's
// load path and RaceRun, pinned with the two arrays above.
constexpr const char* kIntegraRaceSequence =
    "BDDEC0ACDC7AE21041831FDCDCF20C2677ECC27CC121D4862B9F861DF8153A18";

// ---------------------------------------------------------------------------
// The unit half
// ---------------------------------------------------------------------------

// A minimal valid table for the unit half: three records, one per class.
CarTypeTable unit_table() {
    CarTypeTable table;
    table.valid = true;
    const auto make = [&](std::size_t index, const std::string& name,
                          const std::string& geometry, std::uint32_t cls) {
        CarTypeRecord record;
        record.index = index;
        record.offset = 100 + index * 0xC90;
        record.name = name;
        record.lookup_name = name;
        record.geometry_path = geometry;
        record.compressed_path = geometry;
        record.brand_name = "UNIT";
        record.class_id = cls;
        table.records.push_back(record);
    };
    make(0, "CARA", "CARS\\CARA\\GEOMETRY.BIN", 0);
    make(1, "CARB", "CARS\\CARB\\GEOMETRY.BIN", 0);
    make(2, "VAN", "CARS\\VAN\\GEOMETRY.BIN", 2);
    return table;
}

void unit_checks() {
    const CarTypeTable table = unit_table();

    // What a playable car resolves to: the record's directory with the
    // installation's spelling, one path per pack.
    CarAssets car;
    std::string error;
    require(resolve_car_assets(table, "CARA", car, error), "unit resolve CARA: " + error);
    require(car.car_type_name == "CARA" && car.car_type == 0 && car.class_id == 0,
            "unit resolve identity");
    require(car.car_type_hash == bin_hash("CARA"), "unit resolve hash");
    require(car.geometry_path == "Cars/CARA/GEOMETRY.BIN" &&
            car.textures_path == "Cars/CARA/TEXTURES.BIN" &&
            car.vinyls_path == "Cars/CARA/VINYLS.BIN",
            "unit resolve paths: " + car.geometry_path);

    // A traffic-only record is refused with its class named, not resolved.
    CarAssets traffic;
    require(!resolve_car_assets(table, "VAN", traffic, error), "unit traffic resolved");
    require(error.find("class 2") != std::string::npos,
            "unit traffic refusal names the class: " + error);

    // An unknown name lists the playable cars.
    CarAssets absent;
    require(!resolve_car_assets(table, "MISSING", absent, error), "unit unknown resolved");
    require(error.find("CARA") != std::string::npos && error.find("CARB") != std::string::npos &&
            error.find("VAN") == std::string::npos,
            "unit unknown refusal lists playables only: " + error);

    // An invalid table refuses everything.
    CarTypeTable broken = unit_table();
    broken.valid = false;
    require(!resolve_car_assets(broken, "CARA", car, error), "unit invalid table resolved");

    // The playable list, in record order, playables only.
    const std::vector<std::string> names = playable_car_names(table);
    require(names.size() == 2 && names[0] == "CARA" && names[1] == "CARB",
            "unit playable list");
    require(playable_car_names(broken).empty(), "unit playable list of invalid table");

    // The tuned preset rule: the 240SX's own part set dresses the 240SX.
    CarAssets baseline = car;
    baseline.car_type_name = "240SX";
    require(tuned_preset_available_for(baseline), "tuned preset available for 240SX");
    require(!tuned_preset_available_for(car), "tuned preset refused for another car");

    // The reader refuses an unresolved selection without touching the inputs.
    StockFrameInputs inputs;
    require(!read_car_frame_inputs(std::filesystem::path("."), CarAssets{}, inputs, false,
                                   error) &&
            error.find("resolve_car_assets") != std::string::npos &&
            inputs.files[0].path.empty(),
            "unit reader misuse guard: " + error);
}

// ---------------------------------------------------------------------------
// The retail half
// ---------------------------------------------------------------------------

// One rendered car: read, prepare the painted stock frame exactly as the
// applications do, render through the stock view at the default camera and
// encode. The hash is the TGA's SHA-256, the same number the applications
// print.
struct CarFrameResult {
    std::string tga_sha256;
    std::string slots_line;
    std::array<PartPlacement, kCullableCarPartCount> placements{};
    std::size_t drawn = 0;
    std::size_t fell_back = 0;
    std::size_t instances = 0;
    std::size_t unresolved_draws = 0;
    std::size_t lamp_texture_aliases = 0;
    bool inputs_ok = false;
};

CarFrameResult render_car(const std::filesystem::path& root, const CarAssets& car,
                          bool painted = true) {
    StockFrameInputs inputs;
    std::string error;
    require(read_car_frame_inputs(root, car, inputs, false, error), "read: " + error);
    require(inputs.all_read(), "inputs read: " + inputs.read_error(root));
    StockFrame frame;
    const StockFrameSkin skin = painted ? stock_car_skin(car.car_type_name)
                                        : StockFrameSkin{};
    require(prepare_stock_frame(inputs, stock_car_request(car.car_type_name), skin, frame,
                                error),
            "prepare " + car.car_type_name + ": " + error);
    StockView view;
    require(view.start(frame.scene, error), "view " + car.car_type_name + ": " + error);
    require(view.render(frame.frame, frame.stats, error),
            "render " + car.car_type_name + ": " + error);
    StockFrameImage image;
    require(encode_stock_frame(frame.frame, image, error),
            "encode " + car.car_type_name + ": " + error);
    CarFrameResult result;
    result.tga_sha256 = sha256_hex(image.sha256);
    result.slots_line = stock_frame_slots_line(frame.report);
    result.placements = frame.report.placements;
    result.drawn = frame.report.drawn;
    result.fell_back = frame.report.fell_back;
    result.instances = frame.report.instances;
    result.inputs_ok = frame.inputs_ok;
    result.lamp_texture_aliases = frame.lamp_texture_aliases;
    // The reviewer's own probe turned into a gate (issue #315 round 3): a
    // drawable material group with no resolved texture draws the diagnostic
    // checker, and no stock car may ship one.
    for (const PreparedDraw& draw : frame.scene.draws) {
        if (draw.drawable && draw.index_count && draw.texture == kNoHandle) {
            ++result.unresolved_draws;
        }
    }
    // The shadow of the selected car, from its own pack.
    CarShadowAsset shadow;
    require(prepare_car_shadow(car.car_type_name, inputs, frame.car_pack, shadow, error),
            "shadow " + car.car_type_name + ": " + error);
    require(shadow.texture_name == car.car_type_name + "_SHADOWIG" &&
            shadow.texture.hash == bin_hash(car.car_type_name + "_SHADOWIG"),
            "shadow texture identity " + car.car_type_name);
    return result;
}

// A bounded deterministic drive of one car's own handling record over the
// proving ground, as the performance-levels test drives the 240SX.
std::string drive_car(const VehicleSpec& spec) {
    std::string error;
    VehicleTrackCourse course;
    require(build_vehicle_proving_ground(course, error), error);
    RaceSession session(course, spec);
    require(session.start(error), "drive session: " + error);
    Sha256 hash;
    for (unsigned tick = 0; tick < 1200; ++tick) {
        const auto pad = race_input_frame(
            {1, tick < 180 ? 0U : gamepad_button_bit(GamepadButton::trigger_right), 0}, tick);
        require(session.advance(pad, error), "drive step: " + error);
        const auto record = session.record();
        hash.update(record.data(), record.size());
    }
    return sha256_hex(hash.finish());
}

void retail_checks(const std::filesystem::path& root) {
    const std::filesystem::path global = root / "Global/GLOBALB.BUN";
    std::string error;

    // The roster pin: the table's playable class, in record order.
    const CarTypeTable table = load_car_type_table(global);
    require(table.ok(), "retail car type table");
    const std::vector<std::string> names = playable_car_names(table);
    require(names.size() == 20, "the playable roster holds 20 cars, not " +
                                    std::to_string(names.size()));
    for (std::size_t i = 0; i < names.size(); ++i) {
        require(names[i] == kRetailPlayables[i],
                "playable " + std::to_string(i) + " is " + names[i] + ", pinned " +
                    kRetailPlayables[i]);
    }

    // Every racer resolves, reads, prepares, renders and encodes through the
    // real paths, with its own shadow, against its pinned frame and drive,
    // and no two cars produce the same frame: a car that silently rendered
    // another car's data would collide.
    std::map<std::string, CarFrameResult> frames;
    std::map<std::string, std::string> drive_pins;
    for (std::size_t row = 0; row < names.size(); ++row) {
        const std::string& name = names[row];
        CarAssets car;
        require(load_car_assets(global, name, car, error),
                "resolve " + name + ": " + error);
        require(car.geometry_path == "Cars/" + name + "/GEOMETRY.BIN" &&
                car.textures_path == "Cars/" + name + "/TEXTURES.BIN" &&
                car.vinyls_path == "Cars/" + name + "/VINYLS.BIN",
                "paths of " + name + ": " + car.geometry_path);
        const CarFrameResult rendered = render_car(root, car);
        require(rendered.drawn >= 20, name + " drew " + std::to_string(rendered.drawn) +
                                           " slots");
        require(rendered.instances > 0, name + " drew no instances");
        require(rendered.unresolved_draws == 0,
                name + " has " + std::to_string(rendered.unresolved_draws) +
                    " drawable material groups with no texture (checker)");
        frames.emplace(name, rendered);
        std::cout << "car-frame " << name << " drawn " << rendered.drawn << " fallback "
                  << rendered.fell_back << " instances " << rendered.instances
                  << " lamp-aliases " << rendered.lamp_texture_aliases
                  << " unresolved " << rendered.unresolved_draws
                  << " sha256 " << rendered.tga_sha256 << '\n';

        // The handling record of the same car: its own decode, its own drive,
        // against its pin.
        VehicleSpec spec;
        require(load_race_vehicle_spec(global, name, {}, spec, error),
                "spec " + name + ": " + error);
        const VehicleModel model(spec);
        require(model.valid(), "vehicle model " + name + ": " + model.error());
        const std::string drive = drive_car(spec);
        drive_pins.emplace(name, drive);
        std::cout << "car-drive " << name << " sha256 " << drive << '\n';
    }

    // Pin checks after the whole inventory ran, so one failing row does not
    // hide the others.
    for (std::size_t row = 0; row < names.size(); ++row) {
        require(frames[names[row]].tga_sha256 == kRetailFramePins[row],
                "the painted frame pin of " + names[row] +
                    " moved: " + frames[names[row]].tga_sha256);
        require(drive_pins[names[row]] == kRetailDrivePins[row],
                "the proving-ground drive pin of " + names[row] +
                    " moved: " + drive_pins[names[row]]);
    }
    // The presence guard: cars whose packs ship position-name lamp records
    // - the pinned 240SX among them - must pass through the runtime lamp
    // resolution untouched, and the reviewer's three must use it.
    require(frames["240SX"].lamp_texture_aliases == 0,
            "the 240SX lamp resolution rewrote in-pack position records");
    for (const char* name : {"CELICA", "SUPRA", "ECLIPSE"}) {
        require(frames[name].lamp_texture_aliases > 0,
                std::string(name) + " did not resolve its lamp state records");
    }

    // The baseline car is unchanged by the selection layer: the painted 240SX
    // through the per-car reader is the pinned desktop frame.
    const auto baseline = frames.find("240SX");
    require(baseline != frames.end() &&
            baseline->second.tga_sha256 == kStock240sxPaintedFrameSha256,
            "the per-car 240SX painted frame is the pinned one");
    // Four documented cars carry geometry the parser reports issues for; the
    // stock scenes still prepare (docs/CAR_SCENE.md). Their inputs_ok is
    // false and that is recorded, not hidden.
    for (const char* const name : {"CIVIC", "ECLIPSE", "SUPRA", "TIBURON"}) {
        const auto row = frames.find(name);
        require(row != frames.end() && !row->second.inputs_ok,
                std::string(name) + " reports its documented geometry issues");
    }
    // Every frame distinct: no car drew another car's data.
    for (const auto& [name, result] : frames) {
        for (const auto& [other, other_result] : frames) {
            if (name < other) {
                require(result.tga_sha256 != other_result.tga_sha256,
                        name + " and " + other + " rendered the same frame");
                require(drive_pins[name] != drive_pins[other],
                        name + " and " + other + " drove the same replay");
            }
        }
    }

    // The event0 pair the issue prioritizes, with materially different data.
    require(drive_pins["INTEGRA"] != drive_pins["SUPRA"], "event0 pair drives apart");
    require(!same_placements(frames["INTEGRA"].placements, frames["SUPRA"].placements),
            "event0 pair places wheels apart");

    // A/B/A: load INTEGRA, then SUPRA, then INTEGRA again in one process.
    // The first and third frames and placements are identical, and the
    // middle load left nothing behind: no cache, no residual selection.
    CarAssets integra, supra;
    require(load_car_assets(global, "INTEGRA", integra, error), error);
    require(load_car_assets(global, "SUPRA", supra, error), error);
    const CarFrameResult a1 = render_car(root, integra);
    const CarFrameResult b = render_car(root, supra);
    const CarFrameResult a2 = render_car(root, integra);
    require(a1.tga_sha256 == a2.tga_sha256 && same_placements(a1.placements, a2.placements),
            "A/B/A: INTEGRA differs from itself after SUPRA");
    require(a1.tga_sha256 != b.tga_sha256, "A/B/A: INTEGRA and SUPRA agree");
    require(a1.tga_sha256 == frames["INTEGRA"].tga_sha256,
            "A/B/A: the reloaded INTEGRA differs from the inventory pass");
    VehicleSpec integra_spec;
    require(load_race_vehicle_spec(global, "INTEGRA", {}, integra_spec, error), error);
    require(drive_car(integra_spec) == drive_pins["INTEGRA"],
            "the INTEGRA drive is deterministic");

    // A pack that is not there fails with the per-car path named, never by
    // reading another car's directory. The reader returns all_read(), and
    // every file it could not read is in the inputs' own report.
    CarAssets moved = integra;
    moved.geometry_path = "Cars/INTEGRA_X/GEOMETRY.BIN";
    StockFrameInputs inputs;
    const bool moved_read = read_car_frame_inputs(root, moved, inputs, false, error);
    require(error.empty(), "a file problem is not a caller-misuse error: " + error);
    require(!moved_read && !inputs.all_read(), "a moved geometry path still read");
    const std::string missing = inputs.read_error(root);
    require(missing.find("Cars/INTEGRA_X/GEOMETRY.BIN") != std::string::npos,
            "the missing pack is named: " + missing);

    // The application boundary (review F1 of the first round): the Switch
    // race's own load must surface the named missing pack - or the refusal
    // sentence - in the error it returns, never an empty message.
    {
        RaceGpuScene missing_scene;
        require(!missing_scene.load(posix_file_system(), root, error, nullptr, nullptr,
                                    false, false, false, &moved),
                "a scene with a missing pack loaded");
        require(!error.empty() &&
                error.find("Cars/INTEGRA_X/GEOMETRY.BIN") != std::string::npos,
                "the application names the missing pack: '" + error + "'");
        RaceGpuScene unresolved_scene;
        const CarAssets unresolved;
        require(!unresolved_scene.load(posix_file_system(), root, error, nullptr, nullptr,
                                       false, false, false, &unresolved),
                "a scene with an unresolved selection loaded");
        require(error.find("resolve_car_assets") != std::string::npos,
                "the application preserves the resolver refusal: '" + error + "'");
    }

    // The tuned reader route of another car reads its own vinyls.
    CarAssets celica;
    require(load_car_assets(global, "CELICA", celica, error), error);
    StockFrameInputs tuned_inputs;
    require(read_car_frame_inputs(root, celica, tuned_inputs, true, error), error);
    require(tuned_inputs.all_read(), "tuned inputs: " + tuned_inputs.read_error(root));
    require(tuned_inputs.vinyls.path == "Cars/CELICA/VINYLS.BIN" &&
            tuned_inputs.wheel_geometry.path == "Cars/WHEELS/GEOMETRY.BIN",
            "tuned extras name the car's own vinyls and the shared wheels");

    // The Switch race's own load path, with the event0 player car selected:
    // the scene builds its car from the per-car selection and the run decodes
    // the same car's handling record.
    RaceGpuScene scene;
    require(scene.load(posix_file_system(), root, error, nullptr, nullptr, false, false,
                       false, &integra),
            "switch race scene with INTEGRA: " + error);
    RaceRun race;
    require(race.set_car("INTEGRA", error), error);
    require(race.prepare_metadata(root, error), error);
    require(!race.set_car("SUPRA", error) &&
            error == "car selection must precede metadata/start",
            "a prepared run refused a new car");
    require(race.start(root, scene.track(), error), "race start INTEGRA: " + error);
    Sha256 sequence;
    for (unsigned tick = 0; tick < 900; ++tick) {
        const auto pad = race_input_frame(
            {1, tick < 180 ? 0U : gamepad_button_bit(GamepadButton::trigger_right), 0}, tick);
        require(race.simulate(pad, error) && race.commit(error), "race step: " + error);
        const auto line = race.step_line();
        sequence.update(line.data(), line.size());
    }
    const std::string integra_sequence = sha256_hex(sequence.finish());
    std::cout << "car-race-sequence INTEGRA sha256 " << integra_sequence << '\n';
    require(integra_sequence == kIntegraRaceSequence,
            "the INTEGRA race sequence pin moved: " + integra_sequence);
    require(integra_sequence != std::string(kEvent1SequenceHash),
            "the INTEGRA race state is not the 240SX pin");
    require(!race.matches_event1_replay(),
            "a non-240SX car does not claim the event1 replay pin");
    std::cout << "car-payload INTEGRA " << scene.car_bytes() << '\n';

    // The heaviest stock input set of the roster (S2000's geometry is the
    // largest) through the same load, for the one-car side of the budget
    // discussion. The load itself refuses a car that does not fit the
    // persistent payload plan, so its success is the budget check.
    CarAssets heaviest;
    require(load_car_assets(global, "S2000", heaviest, error), error);
    RaceGpuScene heavy_scene;
    require(heavy_scene.load(posix_file_system(), root, error, nullptr, nullptr, false,
                             false, false, &heaviest),
            "switch race scene with S2000: " + error);
    std::cout << "car-payload S2000 " << heavy_scene.car_bytes() << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string error;
        require(check_arguments(argc, argv, 1, error), error);
        unit_checks();
        if (argc == 2) {
            retail_checks(argument_path(argv[1]));
        } else {
            require(argc == 1, "unexpected arguments");
        }
        std::cout << "car selection tests: 0 failures\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
