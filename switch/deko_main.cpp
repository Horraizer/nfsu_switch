// Stock 240SX through the neutral recorder and the Switch-only executor.
// Same six read-only SD inputs as nfsu_switch_car, no CPU rasterization.
#include "deko_renderer.h"
#include "nfsu/stock_frame.h"
#include "nfsu/screen_text.h"
#include <switch.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <exception>
#include <string>

namespace {
// The diagnostic framebuffer is released before deko3d takes the window,
// and reopened only after the renderer (including its swapchain) dies.
// Draw text ourselves so nxlink redirecting stdout cannot hide the error.
class Report {
    Framebuffer fb_{};
    bool open_ = false;
    char text_[2048] = "nfsu_switch_deko3d: loading the stock 240SX. + exits.";
public:
    ~Report() { close(); }
    void close() { if (open_) framebufferClose(&fb_); open_ = false; }
    void line(const char* message) {
        std::snprintf(text_, sizeof text_, "%s", message);
        std::printf("%s\n", text_); std::fflush(stdout);
        show();
    }
    void show() {
        if (!open_) {
            const auto result = framebufferCreate(&fb_, nwindowGetDefault(), 1280, 720,
                                                   PIXEL_FORMAT_RGBA_8888, 2);
            if (R_FAILED(result)) {
                std::printf("diagnostic framebuffer creation failed: 0x%08lX\n", static_cast<unsigned long>(result));
                return;
            }
            if (R_FAILED(framebufferMakeLinear(&fb_))) { framebufferClose(&fb_); return; }
            open_ = true;
        }
        std::uint32_t stride = 0;
        auto* pixels = static_cast<unsigned char*>(framebufferBegin(&fb_, &stride));
        if (!pixels) return;
        nfsu::ScreenImage image{pixels, 1280, 720, stride};
        nfsu::fill_screen_rect(image, 0, 0, 1280, 720, {20, 24, 30, 255});
        // Fixed byte wrapping avoids allocation in the out-of-memory report.
        const char* cursor = text_;
        for (int row = 0; *cursor && row < 35; ++row) {
            char line[81];
            std::size_t count = 0;
            while (*cursor && *cursor != '\n' && count < 80) line[count++] = *cursor++;
            if (*cursor == '\n') ++cursor;
            line[count] = 0;
            for (std::size_t x = 0; x < count; ++x) {
                const auto* glyph = nfsu::screen_glyph_rows(line[x]);
                for (std::uint32_t y = 0; y < nfsu::kScreenGlyphHeight; ++y)
                    for (std::uint32_t bit = 0; bit < nfsu::kScreenGlyphWidth; ++bit)
                        if (glyph[y] & (1U << (4-bit)))
                            nfsu::fill_screen_rect(image, static_cast<std::int64_t>(x*12+bit*2),
                                row*20+y*2, 2, 2, {230, 230, 230, 255});
            }
        }
        framebufferEnd(&fb_);
    }
};

bool run(int argc, char** argv, PadState& pad, Report& report, std::string& error) {
    const auto mounted = romfsInit();
    if (R_FAILED(mounted)) {
        error = "offline shader RomFS mount failed: " + std::to_string(mounted); return false;
    }
    struct RomfsGuard { ~RomfsGuard() { romfsExit(); } } romfs_guard;
    std::string root = "sdmc:/nfsu";
    bool painted = true;
    bool tuned = false;
    bool retail = false;
    bool have_root = false;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i] || !argv[i][0]) continue;
        const std::string argument = argv[i];
        if (argument == "--unpainted") painted = false;
        else if (argument == "--tuned") tuned = true;
        else if (argument == "--material-state=retail") retail = true;
        else if (!have_root && argument[0] != '-') { root = argument; have_root = true; }
        else { error = "unknown argument: " + argument; return false; }
    }
    if (tuned) painted = true;
    report.line(("inputs: " + root + (tuned ? " (tuned)" :
                 painted ? " (painted)" : " (unpainted)")).c_str());
    nfsu::StockFrame frame;
    {
        nfsu::StockFrameInputs inputs;
        const bool read = nfsu::read_stock_frame_inputs(root, inputs, tuned);
        for (const auto& input : inputs.files) {
            const auto line = root + "/" + input.path + ": " +
                (input.read ? "read " + std::to_string(input.bytes.size()) + " bytes" : input.error);
            report.line(line.c_str());
            if (!input.read) error += line + "\n";
        }
        if (tuned) {
            for (const auto* input : {&inputs.wheel_geometry, &inputs.wheel_textures,
                                      &inputs.vinyls}) {
                const auto line = root + "/" + input->path + ": " +
                    (input->read ? "read " + std::to_string(input->bytes.size()) +
                                       " bytes" : input->error);
                report.line(line.c_str());
                if (!input->read) error += line + "\n";
            }
        }
        if (!read) return false;
        const auto skin = tuned ? nfsu::tuned_240sx_skin() :
            painted ? nfsu::stock_240sx_skin() : nfsu::StockFrameSkin{};
        const auto request = tuned ? nfsu::tuned_240sx_request() :
            nfsu::stock_240sx_request();
        if (!nfsu::prepare_stock_frame(inputs, request, skin, frame, error)) return false;
        if (!frame.inputs_ok) {
            error = "stock input issues:\n";
            for (const auto& issue : frame.issues) error += issue + "\n";
            return false;
        }
    }
    nfsu::RecordedCommands commands;
    nfsu::RenderOptions options;
    if (retail) options.material_state = nfsu::MaterialStateMode::retail;
    if (!nfsu::record_car_commands(frame.scene, options, {},
            painted ? frame.skin_binding.source : nfsu::kNoHandle, commands, error)) return false;
    report.line("deko3d: opening GPU renderer; offline shaders in RomFS. + exits.");
    report.close();
    nfsu::DekoRenderer renderer;
    if (!renderer.open(commands, error)) return false;
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        if (!renderer.present(error)) return false;
    }
    return renderer.close(error);
}
} // namespace

int main(int argc, char** argv) {
    bool sockets = false;
    int link = -1;
    if (__nxlink_host.s_addr && R_SUCCEEDED(socketInitializeDefault())) {
        sockets = true; link = nxlinkStdio();
    }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    {
        Report report;
        bool ready = false;
        std::string error;
        try { ready = run(argc, argv, pad, report, error); }
        catch (const std::exception& caught) { report.line(caught.what()); }
        catch (...) { report.line("unknown exception preparing deko3d stock frame"); }
        if (!ready) {
            if (!error.empty()) report.line(error.c_str());
            std::printf("Press + to exit.\n");
            while (appletMainLoop()) {
                padUpdate(&pad);
                if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
                report.show();
            }
        }
    }
    if (link >= 0) close(link);
    if (sockets) socketExit();
    return 0;
}
