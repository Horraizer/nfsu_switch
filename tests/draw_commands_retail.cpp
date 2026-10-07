#include "draw_command_checks.h"
#include "nfsu/arg_path.h"
#include "nfsu/stock_frame.h"
#include "nfsu/draw_execution.h"
#include <iostream>

// A retail check: CTest registers it as nfsu_draw_commands_retail only when
// NFSU_RETAIL_ROOT is configured, inside the retail manifest fixture (issue
// #233), so a build without retail files never runs it. It writes no files.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: nfsu_draw_commands_retail ROOT\n";
        return 2;
    }
    nfsu::StockFrameInputs inputs;
    if (!nfsu::read_stock_frame_inputs(nfsu::argument_path(argv[1]), inputs)) {
        std::cerr << "FAIL: could not read the six stock inputs\n";
        return 1;
    }
    std::string error;
    for (bool painted : {false, true}) {
        nfsu::StockFrame stock;
        if (!nfsu::prepare_stock_frame(inputs, nfsu::stock_240sx_request(),
                painted ? nfsu::stock_240sx_skin() : nfsu::StockFrameSkin{}, stock, error)) {
            std::cerr << "FAIL: prepare: " << error << '\n';
            return 1;
        }
        for (const auto& draw : stock.scene.draws) {
            if (draw.binding != nfsu::BindingKind::unresolved) continue;
            const bool empty_decal = nfsu::is_stock_dummy_decal_hash(draw.texture_hash);
            const bool unpainted_body = !painted && draw.texture_hash == 0x77A20245U;
            if ((!empty_decal && !unpainted_body) || (empty_decal && draw.drawable)) {
                std::cerr << "FAIL: unexpected stock fallback: " << std::hex
                          << draw.texture_hash << std::dec << '\n';
                return 1;
            }
        }
        std::size_t left_brakes = 0;
        for (const auto& instance : stock.scene.instances) {
            if (instance.part_slot != nfsu::kSlotBrake) continue;
            const bool left = instance.copy == 0 || instance.copy == 3;
            if (left) {
                ++left_brakes;
                if (instance.brake_left_texture >= stock.scene.textures.size() ||
                    !stock.scene.textures[instance.brake_left_texture].claims_hash(nfsu::kBrakeGlobalLeftHash)) {
                    std::cerr << "FAIL: left brake has no BRAKE_GLOBAL_LEFT binding\n";
                    return 1;
                }
            } else if (instance.brake_left_texture != nfsu::kNoHandle) {
                std::cerr << "FAIL: right brake has a left texture binding\n";
                return 1;
            }
        }
        if (left_brakes != 2) {
            std::cerr << "FAIL: expected two left brakes\n";
            return 1;
        }
        for (auto mode : {nfsu::MaterialStateMode::off, nfsu::MaterialStateMode::retail}) {
            nfsu::RenderOptions options;
            options.material_state = mode;
            nfsu::RecordedCommands commands;
            if (!nfsu::record_car_commands(stock.scene, options, {},
                    painted ? stock.skin_binding.source : nfsu::kNoHandle, commands, error)) {
                std::cerr << "FAIL: record: " << error << '\n';
                return 1;
            }
            nfsu::DrawExecutionPlan plan;
            if (!nfsu::plan_draw_execution(commands, plan, error)) {
                std::cerr << "FAIL: execution plan: " << error << '\n';
                return 1;
            }
            std::vector<nfsu::ReferenceDraw> trace;
            if (!nfsu::render_scene(stock.scene, options, {}, stock.frame, stock.stats, error,
                [&](const auto& d) { trace.push_back(d); })) {
                std::cerr << "FAIL: render: " << error << '\n';
                return 1;
            }
            if (painted && (stock.stats.triangles_fallback != 0 ||
                            stock.stats.pixels_fallback != 0)) {
                std::cerr << "FAIL: painted stock car still has fallback pixels\n";
                return 1;
            }
            const auto mismatch = compare_draws(commands, trace, false);
            if (!mismatch.empty() || trace.empty()) {
                std::cerr << "FAIL: " << (trace.empty() ? "empty CPU trace" : mismatch) << '\n';
                return 1;
            }
            for (const auto& draw : commands.owning_draws()) {
                if (draw.instance == nfsu::kNoHandle) {
                    std::cerr << "FAIL: " << draw.name << ": stock placement absent\n";
                    return 1;
                }
                const auto& placement = stock.scene.instances[draw.instance];
                for (std::size_t i = 0; i < 16; ++i) {
                    if (draw.placement.rows[i] != placement.transform[i]) {
                        std::cerr << "FAIL: " << draw.name << ": stock placement differs\n";
                        return 1;
                    }
                }
            }
            nfsu::StockFrameImage image;
            if (!nfsu::encode_stock_frame(stock.frame, image, error)) {
                std::cerr << "FAIL: encode: " << error << '\n';
                return 1;
            }
            const auto hash = nfsu::sha256_hex(image.sha256);
            if (mode == nfsu::MaterialStateMode::retail &&
                (hash != (painted ? nfsu::kStock240sxPaintedFrameSha256 : nfsu::kStock240sxFrameSha256) ||
                 image.tga.size() != (painted ? nfsu::kStock240sxPaintedFrameBytes : nfsu::kStock240sxFrameBytes))) {
                std::cerr << "FAIL: pinned stock frame changed: " << hash << '\n';
                return 1;
            }
            std::cout << "PASS: stock 240SX " << (painted ? "painted " : "unpainted ") << nfsu::material_state_mode_name(mode)
                      << ": " << trace.size() << " draws, " << commands.owning_uploads().resources.size()
                      << " resources, " << stock.stats.triangles_submitted << " triangles, sha256 " << hash << '\n';
        }
        if (painted) {
            struct DetailView {
                const char* name;
                double yaw;
                bool close;
                const char* sha256;
            };
            constexpr DetailView views[] = {
                {"front wheel", -45.0, false, "370A134DC58F2F3FD6FCC50F191AD800F32DA125CB472C5CAA894021D46B9403"},
                {"rear wheel", 145.0, false, "F136A9150C99EE367DA801228D642F7FF4E3DC801C79EC6B2B3DA6A002A2907F"},
                {"badge closeup", 0.0, true, "A98E6D51AE8B5E22B642ABE958B505CDC50B956759F21CE56E33B1B94DFB19EF"},
            };
            for (const auto& view : views) {
                nfsu::RenderOptions options;
                options.material_state = nfsu::MaterialStateMode::retail;
                options.camera.yaw_deg = view.yaw;
                if (view.close) {
                    options.camera.pitch_deg = 0.0;
                    options.camera.distance = 2.0;
                    options.camera.target = {1.7, 0.6, 0.0};
                }
                if (!nfsu::render_scene(stock.scene, options, {}, stock.frame, stock.stats, error)) {
                    std::cerr << "FAIL: detail render: " << error << '\n';
                    return 1;
                }
                nfsu::StockFrameImage image;
                if (!nfsu::encode_stock_frame(stock.frame, image, error) ||
                    nfsu::sha256_hex(image.sha256) != view.sha256) {
                    std::cerr << "FAIL: pinned detail view changed: " << view.name
                              << " got " << nfsu::sha256_hex(image.sha256) << '\n';
                    return 1;
                }
                std::cout << "PASS: painted retail " << view.name << " sha256 " << view.sha256 << '\n';
            }
        }
    }
    // #182: separate opt-in pins; the legacy off-state pins above stay fixed.
    const double yaw[] = {35,-45,145};
    // 35: blue body paint, BASE_A chrome and windshield alpha/reflection.
    // -45: the opposite side changes the normal/view-dependent terms.
    // 145: rear glass and lamp lenses exercise their own material records.
    // Re-pinned for the installed-BASE_PAINT material correction (review
    // F2): the painted stock 240SX now shades with GLOSS_L1_COLOR08's own
    // material instead of the blue swatch surrogate.
    const char* pins[] = {
        "E9393053240398C35B42A9E2F5E2FA253760C1A9F8D3C082E61F763B6FA2688E", // yaw 35
        "D9E5B98EA81BC768F3F3270A889286445513DE67EFF4CC9587053E42BFCF2464", // yaw -45
        "D3722C968311E6B0D14DCEBF07EF6A1396B49B49E370457591EAADB2C5E0EC17"}; // yaw 145
    nfsu::StockFrame shaded;
    if (!nfsu::prepare_stock_frame(inputs,nfsu::stock_240sx_request(),
                                   nfsu::stock_240sx_skin(),shaded,error,true)) {
        std::cerr << "FAIL: shaded preparation: " << error << '\n'; return 1;
    }
    constexpr std::uint32_t kCarSkinHash=0xD6D6080AU;
    std::size_t paint_groups=0;
    for (const auto& draw:shaded.scene.draws) if (draw.material_key_hash==kCarSkinHash) {
        ++paint_groups;
        if (draw.shading.gates[0]!=1.0F) {
            std::cerr << "FAIL: CARSKIN paint group has no shading material\n"; return 1;
        }
    }
    if (paint_groups!=9) {
        std::cerr << "FAIL: expected 9 CARSKIN paint groups, got " << paint_groups << '\n'; return 1;
    }
    const auto installed_paint=nfsu::find_car_part(shaded.parts,nfsu::kAnyCarType,30,
        shaded.car_types.records[shaded.report.car_type].base_paint_part_hash,
        nfsu::kNoCarPart,nfsu::kAnyUpgradeLevel);
    if (installed_paint==nfsu::kNoCarPart ||
        shaded.parts.records[installed_paint].name!="GLOSS_L1_COLOR08") {
        std::cerr << "FAIL: stock 240SX must install GLOSS_L1_COLOR08\n"; return 1;
    }
    // Corrected selection semantics (review F2): an installed BASE_PAINT
    // record keeps its own material whatever the skin color is; only a
    // color-only configuration without an installed record falls back to the
    // nearest GLOSS_L1 swatch as its surrogate.
    const auto installed_hash=nfsu::car_part_material_hash(shaded.parts,installed_paint);
    if (installed_hash==0 ||
        nfsu::select_car_paint_material_hash(shaded.parts,installed_paint,
            nfsu::stock_240sx_skin().selection.base_color)!=installed_hash) {
        std::cerr << "FAIL: installed BASE_PAINT must keep its own material: 0x"
                  << std::hex << installed_hash << std::dec << '\n'; return 1;
    }
    const auto blue_hash=nfsu::select_car_paint_material_hash(shaded.parts,
        nfsu::kNoCarPart,nfsu::stock_240sx_skin().selection.base_color);
    if (blue_hash!=0x827D95A1U) {
        std::cerr << "FAIL: blue skin must select REGPAINTBLUE through BASE_PAINT\n"; return 1;
    }
    const auto red_hash=nfsu::select_car_paint_material_hash(shaded.parts,
        nfsu::kNoCarPart,nfsu::tuned_240sx_skin().selection.base_color);
    if (red_hash!=0xFC329CF4U) {
        std::cerr << "FAIL: red skin must select REGPAINTRED through BASE_PAINT\n"; return 1;
    }
    std::vector<nfsu::CarMaterialRecord> material_records;
    const auto& material_bytes=inputs.files[
        static_cast<std::size_t>(nfsu::StockFrameFile::global_bun)].bytes;
    if (!nfsu::parse_car_materials(material_bytes.data(),material_bytes.size(),material_records,error)) {
        std::cerr << "FAIL: parse paint records: " << error << '\n'; return 1;
    }
    const auto material_power=[&](std::uint32_t hash) {
        for (const auto& record:material_records) if (record.hash==hash)
            return record.values.specular_min[3];
        return -1.0F;
    };
    for (const auto& draw:shaded.scene.draws) if (draw.material_key_hash==kCarSkinHash &&
        draw.shading.specular_min[3]!=material_power(installed_hash)) {
        std::cerr << "FAIL: CARSKIN must receive the installed paint coefficients\n"; return 1;
    }
    auto invalid_materials=inputs;
    auto& global=invalid_materials.files[
        static_cast<std::size_t>(nfsu::StockFrameFile::global_bun)].bytes;
    constexpr std::size_t kRegPaintSpecularScale=0x3528+8+0x58;
    if (global.size()<kRegPaintSpecularScale+4) {
        std::cerr << "FAIL: GLOBALB.BUN is too short for the paint probe\n"; return 1;
    }
    global[kRegPaintSpecularScale+0]=0;
    global[kRegPaintSpecularScale+1]=0;
    global[kRegPaintSpecularScale+2]=0xC0;
    global[kRegPaintSpecularScale+3]=0x7F;
    nfsu::StockFrame legacy_off;
    if (!nfsu::prepare_stock_frame(invalid_materials,nfsu::stock_240sx_request(),
            nfsu::stock_240sx_skin(),legacy_off,error,false)) {
        std::cerr << "FAIL: default shading-off preparation parsed material coefficients: "
                  << error << '\n'; return 1;
    }
    nfsu::StockFrame shaded_invalid;
    if (nfsu::prepare_stock_frame(invalid_materials,nfsu::stock_240sx_request(),
            nfsu::stock_240sx_skin(),shaded_invalid,error,true) ||
        error.find("car materials: coefficient out of range")==std::string::npos) {
        std::cerr << "FAIL: enabled car shading did not reject the invalid material record\n"; return 1;
    }
    for (int i=0;i<3;++i) {
        nfsu::RenderOptions options;
        options.car_shading=true; options.material_state=nfsu::MaterialStateMode::retail;
        options.camera.yaw_deg=yaw[i];
        nfsu::RecordedCommands commands;
        std::vector<nfsu::ReferenceDraw> trace;
        if (!nfsu::render_scene(shaded.scene,options,{},shaded.frame,shaded.stats,error,
                [&](const nfsu::ReferenceDraw& d){trace.push_back(d);}) ||
            !nfsu::record_car_commands(shaded.scene,options,{},shaded.skin_binding.source,commands,error)) {
            std::cerr << "FAIL: shaded render/record: " << error << '\n'; return 1;
        }
        const auto mismatch=compare_draws(commands,trace,false);
        if (!mismatch.empty()) { std::cerr << "FAIL: " << mismatch << '\n'; return 1; }
        std::size_t shaded_draws=0;
        std::size_t shaded_paint_draws=0;
        for (const auto& d:commands.owning_draws()) {
            if (std::string(d.program->name)=="car_shaded_textured") ++shaded_draws;
            if (d.group<shaded.scene.draws.size() &&
                shaded.scene.draws[d.group].material_key_hash==kCarSkinHash) {
                ++shaded_paint_draws;
                if (std::string(d.program->name)!="car_shaded_textured") {
                    std::cerr << "FAIL: CARSKIN paint draw fell back to an unshaded program\n"; return 1;
                }
            }
        }
        if (shaded_paint_draws!=paint_groups) {
            std::cerr << "FAIL: not all CARSKIN paint groups produced shaded draws\n"; return 1;
        }
        if (!shaded_draws || shaded.stats.triangles_fallback || shaded.stats.pixels_fallback) {
            std::cerr << "FAIL: shaded stock coverage or fallback\n"; return 1;
        }
        nfsu::StockFrameImage image;
        if (!nfsu::encode_stock_frame(shaded.frame,image,error)) { std::cerr << "FAIL: " << error << '\n'; return 1; }
        const auto hash=nfsu::sha256_hex(image.sha256);
        if (hash!=pins[i]) { std::cerr << "FAIL: shaded stock pin changed: " << hash << '\n'; return 1; }
        std::cout << "PASS: shaded stock yaw " << yaw[i] << " shaded draws " << shaded_draws << " sha256 " << hash << '\n';
    }
    nfsu::StockFrame red;
    auto red_skin=nfsu::stock_240sx_skin();
    red_skin.selection.base_color=nfsu::tuned_240sx_skin().selection.base_color;
    if (!nfsu::prepare_stock_frame(inputs,nfsu::stock_240sx_request(),
                                   red_skin,red,error,true)) {
        std::cerr << "FAIL: red shaded preparation: " << error << '\n'; return 1;
    }
    // Corrected semantics: with the stock BASE_PAINT installed, the red skin
    // color changes the composed surface, not the paint material.
    for (const auto& draw:red.scene.draws) if (draw.material_key_hash==kCarSkinHash &&
        draw.shading.specular_min[3]!=material_power(installed_hash)) {
        std::cerr << "FAIL: red CARSKIN must keep the installed paint coefficients\n"; return 1;
    }
    auto unmatched_parts=shaded.parts;
    auto& base=unmatched_parts.records[installed_paint];
    bool changed=false;
    for (std::int16_t i=0;i<base.attribute_count;++i) {
        auto& attribute=unmatched_parts.attributes[base.attribute_index+static_cast<std::size_t>(i)];
        if (attribute.key==0x6BA02C05U) { attribute.value=0xD6640DFFU; changed=true; break; }
    }
    const auto unmatched_hash=nfsu::select_car_paint_material_hash(unmatched_parts,
        installed_paint,std::nullopt);
    if (!changed || unmatched_hash!=0xD6640DFFU ||
        !nfsu::apply_car_materials(shaded.scene,material_bytes.data(),material_bytes.size(),
                                   unmatched_hash,0,error)) {
        std::cerr << "FAIL: unmatched BASE_PAINT attribute must use DEFAULT: changed "
                  << changed << " hash " << std::hex << unmatched_hash << std::dec
                  << " error " << error << '\n'; return 1;
    }
    for (const auto& draw:shaded.scene.draws) if (draw.material_key_hash==kCarSkinHash &&
        draw.shading.specular_min[3]!=material_power(0x2C420D64U)) {
        std::cerr << "FAIL: unmatched CARSKIN must receive DEFAULT coefficients\n"; return 1;
    }
    nfsu::StockFrameInputs tuned_inputs;
    if (!nfsu::read_stock_frame_inputs(nfsu::argument_path(argv[1]), tuned_inputs, true)) {
        std::cerr << "FAIL: could not read the tuned inputs\n";
        return 1;
    }
    nfsu::StockFrame tuned;
    if (!nfsu::prepare_stock_frame(tuned_inputs, nfsu::tuned_240sx_request(),
                                   nfsu::tuned_240sx_skin(), tuned, error)) {
        std::cerr << "FAIL: tuned prepare: " << error << '\n';
        return 1;
    }
    nfsu::RenderOptions options;
    options.material_state = nfsu::MaterialStateMode::retail;
    nfsu::RecordedCommands commands;
    if (!nfsu::record_car_commands(tuned.scene, options, {},
                                   tuned.skin_binding.source, commands, error)) {
        std::cerr << "FAIL: tuned record: " << error << '\n';
        return 1;
    }
    nfsu::DrawExecutionPlan plan;
    if (!nfsu::plan_draw_execution(commands, plan, error)) {
        std::cerr << "FAIL: tuned execution plan: " << error << '\n';
        return 1;
    }
    std::vector<nfsu::ReferenceDraw> trace;
    if (!nfsu::render_scene(tuned.scene, options, {}, tuned.frame, tuned.stats, error,
                            [&](const auto& draw) { trace.push_back(draw); })) {
        std::cerr << "FAIL: tuned reference render: " << error << '\n';
        return 1;
    }
    const auto mismatch = compare_draws(commands, trace, false);
    if (!mismatch.empty() || trace.empty()) {
        std::cerr << "FAIL: tuned draw comparison: " <<
            (trace.empty() ? "empty CPU trace" : mismatch) << '\n';
        return 1;
    }
    nfsu::StockFrameImage image;
    if (!nfsu::encode_stock_frame(tuned.frame, image, error) ||
        nfsu::sha256_hex(image.sha256) != nfsu::kTuned240sxFrameSha256 ||
        image.tga.size() != nfsu::kTuned240sxFrameBytes) {
        std::cerr << "FAIL: pinned tuned frame changed: " << error << '\n';
        return 1;
    }
    std::cout << "PASS: tuned 240SX retail: " << trace.size() << " draws, "
              << commands.owning_uploads().resources.size() << " resources, "
              << tuned.stats.triangles_submitted << " triangles, sha256 "
              << nfsu::sha256_hex(image.sha256) << '\n';
    return 0;
}
