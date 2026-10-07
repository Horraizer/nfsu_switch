// Issue #147: CPU pin + same-scene deko3d readback, reported on screen.
#include "deko_renderer.h"
#include "nfsu/frame_compare.h"
#include "nfsu/stock_frame.h"
#include "nfsu/stock_view.h"
#include "nfsu/screen_text.h"
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

namespace {
class Report {
    Framebuffer fb_{};
    bool open_ = false;
    char text_[2048] = "NFSU #147: loading the stock car. Results appear on this screen.";
public:
    ~Report() { if (open_) framebufferClose(&fb_); }
    void line(const char* message) {
        std::snprintf(text_, sizeof text_, "%s", message);
        std::printf("%s\n", text_); std::fflush(stdout);
        show();
    }
    void show(const std::vector<unsigned char>* frame = nullptr, bool overlay = true) {
        if (!open_) {
            const auto result = framebufferCreate(&fb_, nwindowGetDefault(), 1280, 720,
                                                   PIXEL_FORMAT_RGBA_8888, 2);
            if (R_FAILED(result)) {
                std::printf("framebufferCreate failed: 0x%08lX\n", static_cast<unsigned long>(result));
                return;
            }
            if (R_FAILED(framebufferMakeLinear(&fb_))) { framebufferClose(&fb_); return; }
            open_ = true;
        }
        std::uint32_t stride = 0;
        auto* pixels = static_cast<unsigned char*>(framebufferBegin(&fb_, &stride));
        if (!pixels) return;
        nfsu::ScreenImage image{pixels, 1280, 720, stride};
        if (frame && frame->size() == 1280*720*4) {
            for (std::size_t y=0; y<720; ++y)
                std::memcpy(pixels + y*stride, frame->data() + y*1280*4, 1280*4);
        } else nfsu::fill_screen_rect(image, 0, 0, 1280, 720, {20, 24, 30, 255});
        if (overlay) {
            const char* cursor = text_;
            for (int row=0; *cursor && row<35; ++row) {
                char line[81]; std::size_t count = 0;
                while (*cursor && *cursor != '\n' && count<80) line[count++] = *cursor++;
                if (*cursor == '\n') ++cursor;
                line[count] = 0;
                nfsu::fill_screen_rect(image, 10, 10+row*20, 980, 20, {20, 24, 30, 255});
                for (std::size_t x=0; x<count; ++x) {
                    const auto* glyph = nfsu::screen_glyph_rows(line[x]);
                    for (std::uint32_t y=0; y<nfsu::kScreenGlyphHeight; ++y)
                        for (std::uint32_t bit=0; bit<nfsu::kScreenGlyphWidth; ++bit)
                            if (glyph[y] & (1U << (4-bit)))
                                nfsu::fill_screen_rect(image, 14+static_cast<std::int64_t>(x*12+bit*2),
                                    12+row*20+y*2, 2, 2, {230, 230, 230, 255});
                }
            }
        }
        framebufferEnd(&fb_);
    }
};

bool run(int argc, char** argv, PadState& pad, Report& report, std::string& error) {
    const auto mounted = romfsInit();
    if (R_FAILED(mounted)) { error = "offline shader RomFS mount failed: " + std::to_string(mounted); return false; }
    struct RomfsGuard { ~RomfsGuard() { romfsExit(); } } romfs_guard;
    bool painted = true, tuned = false, hud = false, have_root = false;
    std::string root = "sdmc:/nfsu";
    std::string car_name = "240SX";
    for (int i=1; i<argc; ++i) {
        if (!argv[i] || !argv[i][0]) continue;
        const std::string argument = argv[i];
        if (argument == "--unpainted") painted = false;
        else if (argument == "--tuned") tuned = true;
        else if (argument == "--hud") hud = true;
        else if (argument == "--car" && i+1 < argc && argv[i+1]) car_name = argv[++i];
        else if (!have_root && argument[0] != '-') { root = argument; have_root = true; }
        else { error = "unknown argument: " + argument; return false; }
    }
    if (tuned) painted = true;
    nfsu::CarAssets selected_car;
    if (!nfsu::load_car_assets(std::filesystem::path(root) / "Global/GLOBALB.BUN",
                               car_name, selected_car, error)) {
        error = "car selection: " + error; return false;
    }
    if (tuned && !nfsu::tuned_preset_available_for(selected_car)) {
        error = "the --tuned preset is the 240SX's own part set; it cannot dress car type " +
                selected_car.car_type_name;
        return false;
    }
    nfsu::StockFrame stock;
    {
        report.line(("NFSU #147: reading the inputs of " + selected_car.car_type_name +
                     " under " + root).c_str());
        nfsu::StockFrameInputs inputs;
        std::string read_error;
        const bool read = nfsu::read_car_frame_inputs(root, selected_car, inputs, tuned,
                                                      read_error);
        if (!read_error.empty()) { error = read_error; return false; }
        if (!read) {
            for (const auto& f : inputs.files) if (!f.read) error += f.path + ": " + f.error + "\n";
            if (tuned && !inputs.wheel_geometry.read)
                error += inputs.wheel_geometry.path + ": " + inputs.wheel_geometry.error + "\n";
            if (tuned && !inputs.wheel_textures.read)
                error += inputs.wheel_textures.path + ": " + inputs.wheel_textures.error + "\n";
            if (tuned && !inputs.vinyls.read)
                error += inputs.vinyls.path + ": " + inputs.vinyls.error + "\n";
            return false;
        }
        if (!nfsu::prepare_stock_frame(inputs,
                tuned ? nfsu::tuned_240sx_request() : nfsu::stock_car_request(selected_car.car_type_name),
                tuned ? nfsu::tuned_240sx_skin() :
                painted ? nfsu::stock_car_skin(selected_car.car_type_name) : nfsu::StockFrameSkin{}, stock, error)) return false;
        if (!stock.inputs_ok) {
            error = "stock input issues:\n";
            for (const auto& issue : stock.issues) error += issue + "\n";
            return false;
        }
    }
    const nfsu::RenderOptions options = nfsu::stock_view_default_options();
    report.line("NFSU #147: rendering CPU reference (same prepared scene).");
    const auto cpu_start = armGetSystemTick();
    if (!nfsu::render_stock_frame(stock, options, error)) return false;
    const double cpu_ms = armTicksToNs(armGetSystemTick()-cpu_start)/1000000.0;
    std::string cpu_hash;
    {
        nfsu::StockFrameImage encoded;
        if (!nfsu::encode_stock_frame(stock.frame, encoded, error)) return false;
        cpu_hash = nfsu::sha256_hex(encoded.sha256);
    }
    const bool pin_ok = cpu_hash == (tuned ? nfsu::kTuned240sxFrameSha256 :
        painted ? nfsu::kStock240sxPaintedFrameSha256 : nfsu::kStock240sxFrameSha256);
    nfsu::RecordedCommands commands;
    if (!nfsu::record_car_commands(stock.scene, options, {},
            painted ? stock.skin_binding.source : nfsu::kNoHandle, commands, error)) return false;
    std::vector<unsigned char> gpu, difference;
    nfsu::RaceHudAtlas hud_atlas;
    nfsu::RaceHudFrame hud_frame;
    if (hud) {
        if (!nfsu::load_race_hud_atlas(root,hud_atlas,error)) return false;
        nfsu::VehicleState car{};
        car.gear = nfsu::kVehicleGearFirst;
        car.engine_rpm = 7500.0;
        car.velocity.x = 22.0;
        // This synthetic HUD uses engine B level 3's +1000 RPM for --tuned.
        nfsu::build_race_hud_frame_for_car(car,tuned ? 9000 : 8000,
                                            tuned ? 8500 : 7500,commands.width,
                                            commands.height,hud_frame);
    }
    double gpu_ms=0, readback_ms=0;
    report.line("NFSU #147: rendering GPU and reading back RGBA8 (no swapchain).");
    {
        nfsu::DekoRenderer renderer;
        if (!renderer.open(commands, error, false, 0, false, false,
                           false, 0, hud ? &hud_atlas : nullptr) ||
            !renderer.capture(gpu, gpu_ms, readback_ms, error,
                              hud ? &hud_frame : nullptr) || !renderer.close(error)) return false;
    } // Device and every GPU allocation die before displaying the captured bytes.
    std::vector<unsigned char> cpu = stock.frame.rgba();
    if (hud) nfsu::composite_race_hud(hud_atlas,hud_frame,cpu.data(),
                                       commands.width,commands.height,commands.width*4);
    nfsu::RgbaFrameView cpu_view{cpu.data(), cpu.size(), stock.frame.width(), stock.frame.height(), stock.frame.width()*4};
    nfsu::RgbaFrameView gpu_view{gpu.data(), gpu.size(), commands.width, commands.height, commands.width*4};
    nfsu::FrameComparison metrics;
    if (!nfsu::compare_frames(cpu_view, gpu_view, metrics, error) ||
        !nfsu::amplify_frame_difference(cpu_view, gpu_view, difference, error)) return false;
    const auto gpu_hash = nfsu::sha256_hex(nfsu::sha256(gpu.data(), gpu.size()));
    char text[2048];
    std::snprintf(text, sizeof text,
        "NFSU #147 v1: %s, viewer default camera and retail material state%s\n"
        "%s (requires CPU pin, within-2 >=99.0%% AND RGB PSNR >=35 dB)\n"
        "Exact RGBA: %.6f%% (%llu/%llu pixels)\n"
        "Within +/-2 RGBA: %.6f%% (%llu/%llu pixels)\n"
        "Maximum difference, every channel: %u / 255\n"
        "PSNR RGB, whole frame: %.6f dB\n"
        "CPU render: %.3f ms   GPU render+wait: %.3f ms\n"
        "GPU readback+wait: %.3f ms (separate from GPU render)\n"
        "CPU TGA pin: %s\n%s\n"
        "GPU raw RGBA SHA-256:\n%s\n"
        "A=GPU  B=CPU  X=DIFF x16  Y=toggle report  +=exit\n"
        "Photograph this report; coordinator records numbers on issue #147.",
        tuned ? "tuned" : painted ? "painted" : "unpainted", hud ? ", HUD" : "",
        pin_ok && metrics.passed ? "PASS" : "FAIL",
        100.0*metrics.exact_pixels/metrics.pixels,
        static_cast<unsigned long long>(metrics.exact_pixels), static_cast<unsigned long long>(metrics.pixels),
        100.0*metrics.within_two_pixels/metrics.pixels,
        static_cast<unsigned long long>(metrics.within_two_pixels), static_cast<unsigned long long>(metrics.pixels),
        metrics.max_channel_difference, metrics.psnr_db, cpu_ms, gpu_ms, readback_ms,
        pin_ok ? "MATCH" : "MISMATCH (overall FAIL)", cpu_hash.c_str(), gpu_hash.c_str());
    report.line(text);
    const std::vector<unsigned char>* selected = &gpu;
    bool overlay = true;
    while (appletMainLoop()) {
        padUpdate(&pad);
        const auto down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_A) selected = &gpu;
        if (down & HidNpadButton_B) selected = &cpu;
        if (down & HidNpadButton_X) selected = &difference;
        if (down & HidNpadButton_Y) overlay = !overlay;
        report.show(selected, overlay);
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    Report report;
    bool complete = false;
    std::string error;
    try { complete = run(argc, argv, pad, report, error); }
    catch (const std::exception& caught) { report.line(caught.what()); }
    catch (...) { report.line("unknown exception comparing frames"); }
    if (!complete) {
        if (!error.empty()) report.line(error.c_str());
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            report.show();
        }
    }
    return 0;
}
