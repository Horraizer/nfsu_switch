// The free-driving Eddie Skyline (explicit owner request; menu -> free
// driving -> menu lifecycle).
//
// Without a retail root this runs the unit half: the dress misuse refusals
// of RaceGpuScene::load, which run before any file is read. With a retail
// root (argv[1], passed by the nfsu_freeride_skyline_retail check) it
// resolves the original FE_SKYLINE preset once and prepares it twice through
// the two real paths that display and drive it - the menu frame the frontend
// loads, and the dressed race frame RaceGpuScene::load records - then
// compares the resolved selections mechanically, adds regressions against
// stock substitution and missing authored parts, and drives the Switch race
// load with the dress before, beside and after a live frontend scene, as the
// menu lifecycle does.
//
// Nothing writes under the retail root; every output the test produces is a
// hash printed on stdout.

#include "nfsu/arg_path.h"
#include "nfsu/car_assets.h"
#include "nfsu/frontend_preset.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/freeride_skyline.h"
#include "nfsu/platform.h"
#include "nfsu/race_events.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/race_run.h"
#include "nfsu/race_session.h"
#include "nfsu/sha256.h"
#include "nfsu/skin_inputs.h"
#include "nfsu/stock_frame.h"
#include "nfsu/texture.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "nfsu/posix_file_system.h"

using namespace nfsu;

namespace {

void require(bool pass, const std::string& label) {
    if (!pass) throw std::runtime_error("FAIL: " + label);
}

// The menu preparation below is the menu loader's own sequence; the race one
// goes through resolve_free_drive_skyline's dress, whose binder is the same
// function pointer the console passes, so the sequences compared are the
// sequences the applications run.
bool prepare_menu_frame(const std::filesystem::path& root,
                        const InstalledPartSelection& installed,
                        const StockFrameSkin& skin, StockFrame& menu,
                        StockFrameInputs& inputs, std::string& error) {
    // The exact menu sequence of src/frontend_scene.cpp load_scene_assets:
    // tuned extras, front-end pass, runtime skin, sponsors.
    CarAssets skyline;
    if (!load_car_assets(root/"Global/GLOBALB.BUN","SKYLINE",skyline,error)) return false;
    if (!read_car_frame_inputs(root,skyline,inputs,true,error)) {
        error = car_frame_inputs_error(inputs,error,root);return false;
    }
    auto request=stock_car_request("SKYLINE");
    request.installed_parts=installed;request.pass=CarDrawPass::front_end;
    return prepare_stock_frame(inputs,request,skin,menu,error,true) &&
           bind_frontend_skyline_runtime_skin(menu,request,error) &&
           bind_frontend_sponsors(inputs,installed,menu,error);
}

bool prepare_race_frame(const std::filesystem::path& root, const FreeDriveSkyline& dress,
                        StockFrame& race, std::string& error) {
    // The exact dressed sequence of RaceGpuScene::load: tuned extras, the
    // EXHAUST family pair, world pass through the dress's own request, then
    // its bind.
    StockFrameInputs inputs;
    if (!read_car_frame_inputs(root,dress.car,inputs,true,error)) {
        error = car_frame_inputs_error(inputs,error,root);return false;
    }
    if (!read_exhaust_family_inputs(root,inputs)) {
        error="the dress needs the shared exhaust family";return false;
    }
    if (!prepare_race_car_frame(inputs,root,dress.request,dress.skin,race,error,true)) return false;
    return dress.dress.bind!=nullptr && dress.dress.bind(race,inputs,dress.request,error,nullptr);
}

std::string slot_field_mismatch(int slot, const char* field) {
    return "menu and race slot " + std::to_string(slot) + " differ in " + field;
}

void compare_menu_and_race(const StockFrame& menu, const StockFrame& race) {
    const auto& menu_slots = menu.report.slots;
    const auto& race_slots = race.report.slots;
    // Part slot IDs and record identities: one installed record per slot,
    // identical in both preparations.
    for (int slot = 0; slot < static_cast<int>(kCarSlotCount); ++slot) {
        require(menu_slots[slot].record==race_slots[slot].record,
            slot_field_mismatch(slot,"record"));
        require(menu_slots[slot].enabled==race_slots[slot].enabled,
            slot_field_mismatch(slot,"enable byte"));
        // Mesh/LOD family mapping: the resolved model hash and where it came
        // from. Two slots legitimately differ. The brake is the one
        // pass-dependent LOD (world takes LOD B, the front end LOD A). The
        // exhaust is the one authored part the menu's own source set cannot
        // close: the FE preset installs the ROCKET exhaust, whose only solid
        // (EXHAUST_STYLE07_LEVEL1_A) lives in Cars/EXHAUST/GEOMETRY.BIN,
        // which the menu loader does not read; the dress reads the family
        // pair, so free driving draws the pipe the menu still cannot.
        if (slot==kSlotExhaust) {
            require(menu_slots[slot].outcome==StockSlotOutcome::mesh_absent &&
                race_slots[slot].outcome==StockSlotOutcome::drawn &&
                race_slots[slot].model==PartModelSource::requested &&
                race_slots[slot].requested_hash==race_slots[slot].hash &&
                bin_hash("EXHAUST_STYLE07_LEVEL1_A")==race_slots[slot].hash,
                "the race draws the authored ROCKET exhaust solid the menu has no family file for");
        } else {
            if (slot!=kSlotBrake) {
                require(menu_slots[slot].requested_hash==race_slots[slot].requested_hash,
                    slot_field_mismatch(slot,"requested LOD hash"));
                require(menu_slots[slot].hash==race_slots[slot].hash,
                    slot_field_mismatch(slot,"resolved model hash"));
                require(menu_slots[slot].model==race_slots[slot].model,
                    slot_field_mismatch(slot,"model source"));
            }
            require(menu_slots[slot].outcome==race_slots[slot].outcome,
                slot_field_mismatch(slot,"outcome"));
            require(menu_slots[slot].instance_count==race_slots[slot].instance_count,
                slot_field_mismatch(slot,"instance count"));
        }
        require(menu_slots[slot].placement==race_slots[slot].placement,
            slot_field_mismatch(slot,"placement"));
    }
    // The one legitimate FE/race pass difference, and only that one.
    require(menu_slots[kSlotBrake].lod==0 && race_slots[kSlotBrake].lod==1,
        "brake LOD is A in the menu and B in the race");
    require(menu.report.car_type==race.report.car_type &&
        menu.report.car_type_hash==race.report.car_type_hash,
        "menu and race resolve the same Skyline car type");
    require(menu.report.drawn+1==race.report.drawn && menu.report.fell_back==race.report.fell_back,
        "the race draws exactly the menu's slots plus the exhaust");
    require(menu.report.ambiguous_hashes==race.report.ambiguous_hashes,
        "menu and race share the ambiguous mesh set");
    // The race's absent set is the menu's minus exactly the exhaust family
    // hash the dress loaded; what stays absent is absent for both (the
    // Skyline's KIT00 trunk solids ship in no file either path reads, and
    // the game's own loaded-solids lookup would not find them either).
    {
        std::vector<std::uint32_t> only_menu;
        std::set_difference(menu.report.absent_hashes.begin(),menu.report.absent_hashes.end(),
            race.report.absent_hashes.begin(),race.report.absent_hashes.end(),
            std::back_inserter(only_menu));
        std::vector<std::uint32_t> only_race;
        std::set_difference(race.report.absent_hashes.begin(),race.report.absent_hashes.end(),
            menu.report.absent_hashes.begin(),menu.report.absent_hashes.end(),
            std::back_inserter(only_race));
        require(only_menu.size()==1 && only_menu[0]==bin_hash("EXHAUST_STYLE07_LEVEL1_A") &&
            only_race.empty(),
            "the dress removes exactly the exhaust hash from the absent set");
    }
    // Skin selection and composed surface: the same Eddie paint, vinyls and
    // doorline composed to the same pixels.
    require(menu.skin.target_hash==race.skin.target_hash &&
        menu.skin.width==race.skin.width && menu.skin.height==race.skin.height &&
        menu.skin.layers==race.skin.layers,
        "menu and race compose the same skin target and layers");
    require(menu.skin.rgba.size()==race.skin.rgba.size(),
        "menu and race skin surfaces have the same size");
    const auto menu_skin = sha256_hex(sha256(menu.skin.rgba.data(),menu.skin.rgba.size()));
    const auto race_skin = sha256_hex(sha256(race.skin.rgba.data(),race.skin.rgba.size()));
    std::printf("freeride-skin sha256 %s (menu and race)\n",menu_skin.c_str());
    require(menu_skin==race_skin,"menu and race compose byte-identical Eddie skin surfaces");
    require(menu.skin_bound_names==race.skin_bound_names,
        "menu and race bind the same skin hash names");
    // Sponsor/decal bindings: the widebody door panels draw the same four
    // native sponsor textures, and the body keeps its GLOBAL_SKIN1 material.
    const std::uint32_t native_doors[]={0x8A571203U,0xB015F6F2U,0xB891CDB4U,0x0F4108B2U};
    for (const StockFrame* frame : {&menu,&race}) {
        const char* where = frame==&menu ? "menu" : "race";
        unsigned global_draws=0;
        for (const auto& draw:frame->scene.draws) if (draw.texture_hash==0xDDA11455U) {
            require(draw.binding==BindingKind::resolved &&
                frame->scene.textures.at(draw.texture).source==frame->skin_binding.source,
                std::string(where)+" GLOBAL_SKIN1 uses the generated body surface");
            ++global_draws;
        }
        require(global_draws!=0,std::string(where)+" retains the GLOBAL_SKIN1 material");
        for (unsigned slot:{26U,27U}) {
            unsigned bound=0;
            for (const auto& instance:frame->scene.instances) if (instance.part_slot==slot)
                for (const auto& draw:frame->scene.draws) if (draw.mesh==instance.mesh && draw.drawable)
                    for (auto hash:native_doors) if (draw.texture_hash==hash) {
                        require(draw.binding==BindingKind::resolved &&
                            frame->scene.textures.at(draw.texture).claims_hash(hash),
                            std::string(where)+" widebody door uses native sponsor pixels");
                        ++bound;
                    }
            require(bound==4,std::string(where)+" widebody door renders four selected sponsors");
        }
    }
}

void retail_checks(const std::filesystem::path& root) {
    std::string error;
    FreeDriveSkyline dress;
    require(resolve_free_drive_skyline(root,dress,error),
             "resolve the free-driving Skyline: " + error);
    require(dress.car.car_type_name=="SKYLINE","the dress selects the Skyline");
    require(dress.request.car_type_name=="SKYLINE" &&
        dress.request.pass==CarDrawPass::world &&
        dress.request.place_wheels_from_culling_table,
        "the dress keeps the race world pass and wheel placement");
    require(dress.dress.request==&dress.request && dress.dress.skin==&dress.skin &&
        dress.dress.bind!=nullptr,"the dress is internally wired");

    // Regressions against stock substitution: the installed wheel and paint
    // are the FE_SKYLINE ones, not the stock Skyline's.
    StockParts stock_parts;
    CarPartDatabase database;
    {
        std::vector<unsigned char> globals;
        std::vector<RaceTableIssue> issues;
        require(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),
                "read GLOBALB for the stock comparison");
        database=parse_car_part_database(globals.data(),globals.size());
    }
    require(select_stock_parts(database,"SKYLINE",stock_parts,error),
             "select stock Skyline parts: " + error);
    require(dress.request.installed_parts.records[kSlotWheel].has_value() &&
        *dress.request.installed_parts.records[kSlotWheel]!=
            stock_parts.slots[kSlotWheel].record,
        "the installed wheel is the Eddie wheel, not the stock one");
    require(dress.skin.selection.base_color==
        skin_pack_paint(145,63,16,128),
        "the base paint is the Eddie paint");
    require(dress.skin.selection.base_color!=stock_car_skin("SKYLINE").selection.base_color,
        "the base paint is not the stock paint");
    require(dress.skin.selection.vinyl_layers[3] &&
        dress.skin.selection.vinyl_layers[3]->texture=="EDDIE",
        "the top vinyl layer is the EDDIE vinyl");

    // The two real preparations.
    StockFrame menu;StockFrameInputs menu_inputs;
    require(prepare_menu_frame(root,dress.request.installed_parts,dress.skin,menu,menu_inputs,error),
        "prepare the menu frame: " + error);
    StockFrame race;
    require(prepare_race_frame(root,dress,race,error),"prepare the race frame: " + error);
    require(race.report.pass==CarDrawPass::world && menu.report.pass==CarDrawPass::front_end,
        "each frame keeps its own pass");
    compare_menu_and_race(menu,race);

    // No unresolved drawable material in the dressed race scene: no checker
    // textures, no silent part fallback.
    for (const auto& draw:race.scene.draws) if (draw.drawable && draw.index_count>0)
        require(draw.binding==BindingKind::resolved,
            "the dressed race scene resolves every drawable material: group "+
            std::to_string(draw.mesh));
    // Absent authored meshes: reported, printed, and compared above to the
    // menu's set minus the exhaust hash - never a new race-side gap.
    std::printf("freeride-absent-meshes %zu (race; menu adds the exhaust hash)\n",
        race.report.absent_hashes.size());
    for (const auto& slot:race.report.slots)
        if (slot.outcome==StockSlotOutcome::mesh_absent)
            std::printf("freeride-absent-mesh slot %d (%s) hash 0x%08X\n",slot.slot,
                car_slot_name(slot.slot),slot.hash);

    // The Switch race load with the dress: the application boundary the menu
    // enters, next to a live frontend scene, and usable again after the race
    // scene is gone - the menu -> free driving -> menu lifecycle.
    {
        FrontendGpuScene frontend;
        require(frontend.load(root,1280,720,error),"load the frontend scene: " + error);
        require(frontend.frame(1.F,60,error),"advance the frontend scene before the race");
        {
            RaceGpuScene scene;
            require(scene.load(posix_file_system(),root,error,nullptr,nullptr,false,false,
                false,&dress.car,&dress.dress),
                "switch race scene with the Eddie dress: " + error);
            RaceRun run;
            require(run.set_free_drive(true,error),error);
            require(run.set_car("SKYLINE",error),error);
            require(run.prepare_metadata(root,error),"skyline metadata: " + error);
            require(run.start(root,scene.track(),error),"skyline free-drive start: " + error);
            RaceGpuFrame frame;
            for (unsigned tick=0;tick<240;++tick) {
                const auto pad=race_input_frame({1,gamepad_button_bit(GamepadButton::trigger_right),0},tick);
                require(run.simulate(pad,error) && run.commit(error),"skyline free-drive step: " + error);
                if (tick%60==0)
                    require(scene.frame(run.session(),frame,error),"skyline dressed frame: " + error);
            }
            require(scene.frame(run.session(),frame,error),"final dressed frame: " + error);
            require(frame.car_draws!=0,"the dressed car contributes draws to the race frame");
            std::printf("freeride-car-payload SKYLINE %llu\n",
                static_cast<unsigned long long>(scene.car_bytes()));
        }
        // The race scene is destroyed; the resident menu scene still frames.
        require(frontend.frame(2.F,120,error),"advance the frontend scene after the race");
        // A second race scene enters free driving again from scratch.
        RaceGpuScene again;
        require(again.load(posix_file_system(),root,error,nullptr,nullptr,false,false,
            false,&dress.car,&dress.dress),
            "switch race scene with the Eddie dress on re-entry: " + error);
        require(again.car_bytes()!=0,"the re-entered dress loads its car payload");
    }

    // A dress authored for the Skyline refuses to dress another body.
    {
        CarAssets integra;
        require(load_car_assets(root/"Global/GLOBALB.BUN","INTEGRA",integra,error),error);
        RaceGpuScene mismatch;
        require(!mismatch.load(posix_file_system(),root,error,nullptr,nullptr,false,false,
            false,&integra,&dress.dress),
            "a Skyline dress dressed the INTEGRA");
        require(error.find("authored for car type SKYLINE")!=std::string::npos,
                "the mismatch refusal names the dress car type: " + error);
    }
}

void unit_checks() {
    // The misuse refusals of the dress input run before any file is read.
    // Each call's outcome is captured first: a require condition and its
    // label are separate function arguments, and their evaluation order is
    // unspecified.
    RaceGpuScene scene;std::string error;
    const auto refused=[&](bool tuned,const RaceCarDress* which,const char* why) {
        error.clear();
        const bool loaded = scene.load(posix_file_system(),
            std::filesystem::path("freeride-unit-absent-root"),error,nullptr,nullptr,
            tuned,false,false,nullptr,which);
        require(!loaded && error==why,std::string("dress refusal, got: ")+error);
    };
    StockSceneRequest request;request.car_type_name="SKYLINE";
    StockFrameSkin skin;
    RaceCarDress dress;dress.request=&request;dress.skin=&skin;
    refused(true,&dress,"the tuned preset and an explicit dress are exclusive; pass one");
    RaceCarDress skinless;skinless.request=&request;
    refused(false,&skinless,"a dress needs both a request and a skin");
    StockSceneRequest nameless;
    RaceCarDress anonymous;anonymous.request=&nameless;anonymous.skin=&skin;
    refused(false,&anonymous,"a dress request needs the car type it was authored for");
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
        std::cout << "free-driving Skyline tests: 0 failures\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
