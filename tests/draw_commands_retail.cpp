#include "draw_command_checks.h"
#include "nfsu/arg_path.h"
#include "nfsu/stock_frame.h"
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
        for (auto mode : {nfsu::MaterialStateMode::off, nfsu::MaterialStateMode::retail}) {
            nfsu::RenderOptions options;
            options.material_state = mode;
            nfsu::RecordedCommands commands;
            if (!nfsu::record_car_commands(stock.scene, options, {},
                    painted ? stock.skin_binding.source : nfsu::kNoHandle, commands, error)) {
                std::cerr << "FAIL: record: " << error << '\n';
                return 1;
            }
            std::vector<nfsu::ReferenceDraw> trace;
            if (!nfsu::render_scene(stock.scene, options, {}, stock.frame, stock.stats, error,
                [&](const auto& d) { trace.push_back(d); })) {
                std::cerr << "FAIL: render: " << error << '\n';
                return 1;
            }
            const auto mismatch = compare_draws(commands, trace, false);
            if (!mismatch.empty() || trace.empty()) {
                std::cerr << "FAIL: " << (trace.empty() ? "empty CPU trace" : mismatch) << '\n';
                return 1;
            }
            for (const auto& draw : commands.draws) {
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
                 image.tga.size() != nfsu::kStock240sxFrameBytes)) {
                std::cerr << "FAIL: pinned stock frame changed: " << hash << '\n';
                return 1;
            }
            std::cout << "PASS: stock 240SX " << (painted ? "painted " : "unpainted ") << nfsu::material_state_mode_name(mode)
                      << ": " << trace.size() << " draws, " << commands.uploads.resources.size()
                      << " resources, " << stock.stats.triangles_submitted << " triangles, sha256 " << hash << '\n';
        }
    }
    return 0;
}
