#include "draw_command_checks.h"
#include "nfsu/arg_path.h"
#include "nfsu/stock_frame.h"
#include "nfsu/draw_execution.h"
#include <iostream>

// Explicit opt-in only: never part of CTest, which must need no retail files.
// The operator takes before/after manifests; this program writes no files.
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
            if (mode == nfsu::MaterialStateMode::off &&
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
                    std::cerr << "FAIL: pinned detail view changed: " << view.name << '\n';
                    return 1;
                }
                std::cout << "PASS: painted retail " << view.name << " sha256 " << view.sha256 << '\n';
            }
        }
    }
    return 0;
}
