#include "nfsu/memory_budget.h"
// Bounded event-1 track diagnostic. All SD inputs are read-only; RomFS
// contains checked shaders only. The scene is drawn by deko3d, then read
// back for a libnx framebuffer with a photographable measurement panel.
#include "deko_renderer.h"
#include "nfsu/track_slice.h"
#include "nfsu/heap_usage.h"
#include <malloc.h>
#include "nfsu/posix_file_system.h"
#include "nfsu/screen_text.h"
#include <switch.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace {
constexpr unsigned kWidth = 1280, kHeight = 720;
double milliseconds(std::uint64_t ticks) { return armTicksToNs(ticks) / 1000000.0; }
struct Cancel {};

class Screen {
    Framebuffer fb_{};
    bool opened_ = false;
public:
    ~Screen() { if (opened_) framebufferClose(&fb_); }
    bool open() {
        if (R_FAILED(framebufferCreate(&fb_, nwindowGetDefault(), kWidth, kHeight,
                                       PIXEL_FORMAT_RGBA_8888, 2))) return false;
        if (R_FAILED(framebufferMakeLinear(&fb_))) { framebufferClose(&fb_); return false; }
        opened_ = true; return true;
    }
    void show(const char* message, const std::vector<unsigned char>* frame = nullptr) {
        if (!opened_) return;
        std::uint32_t stride = 0;
        auto* dst = static_cast<unsigned char*>(framebufferBegin(&fb_, &stride));
        if (!dst) return;
        nfsu::ScreenImage image{dst, kWidth, kHeight, stride};
        if (frame && frame->size() == kWidth * kHeight * 4) {
            for (unsigned y = 0; y < kHeight; ++y)
                std::memcpy(dst + y*stride, frame->data() + y*kWidth*4, kWidth*4);
        } else nfsu::fill_screen_rect(image, 0, 0, kWidth, kHeight, {20, 24, 30, 255});
        nfsu::fill_screen_rect(image, 0, 0, kWidth, frame ? 232 : kHeight, {12, 16, 22, 255});
        const char* cursor = message;
        for (unsigned row = 0; *cursor && row < (frame ? 12U : 35U); ++row) {
            unsigned column = 0;
            while (*cursor && *cursor != '\n' && column < 104) {
                const auto* glyph = nfsu::screen_glyph_rows(*cursor++);
                for (unsigned y = 0; y < nfsu::kScreenGlyphHeight; ++y)
                    for (unsigned bit = 0; bit < nfsu::kScreenGlyphWidth; ++bit)
                        if (glyph[y] & (1U << (4-bit)))
                            nfsu::fill_screen_rect(image, 8 + column*12 + bit*2, 4 + row*19 + y*2,
                                                  2, 2, {235, 235, 235, 255});
                ++column;
            }
            if (*cursor == '\n') ++cursor;
        }
        framebufferEnd(&fb_);
    }
};

void sample_heap(nfsu::HeapUsage& heap) {
    const auto info = mallinfo();
    heap.observe(info.uordblks, info.arena);
}

class Progress : public nfsu::AssetCheckObserver, public nfsu::TrackSliceLoadObserver {
    Screen& screen_;
    PadState& pad_;
    nfsu::HeapUsage& heap_;
    std::string file_;
    std::uint64_t last_ = 0;
    void poll() {
        sample_heap(heap_);
        padUpdate(&pad_);
        if (!appletMainLoop() || (padGetButtonsDown(&pad_) & HidNpadButton_Plus)) throw Cancel{};
    }
public:
    Progress(Screen& screen, PadState& pad, nfsu::HeapUsage& heap)
        : screen_(screen), pad_(pad), heap_(heap) {}
    void checkpoint(const char* phase) override {
        poll();
        const auto now = armGetSystemTick();
        if (milliseconds(now - last_) >= 100) {
            last_ = now;
            char text[512];
            std::snprintf(text, sizeof text,
                "Loading: %s\nHeap in-use before %llu now %llu observed HWM %llu\n"
                "Load delta HWM %llu / 75497600 | over %llu\nHOME serviced; + cancels.", phase,
                static_cast<unsigned long long>(heap_.before_load),
                static_cast<unsigned long long>(heap_.current),
                static_cast<unsigned long long>(heap_.load_peak),
                static_cast<unsigned long long>(heap_.load_delta()),
                static_cast<unsigned long long>(heap_.load_overrun(75497600)));
            screen_.show(text);
        }
    }
    void file_started(std::size_t index, std::size_t total, const nfsu::AssetFile& file) override {
        poll(); file_ = file.path; last_ = 0;
        const auto text = "#47 first-lap asset check " + std::to_string(index+1) + "/" +
            std::to_string(total) + "\n" + file_ + "\nInputs read-only. + cancels.";
        screen_.show(text.c_str());
    }
    void file_progress(std::size_t, std::uint64_t hashed, std::uint64_t size) override {
        poll();
        if (hashed == size || hashed - last_ >= 8*1024*1024) {
            last_ = hashed;
            const auto text = "#47 first-lap asset check\n" + file_ + "\n" +
                std::to_string(hashed) + " / " + std::to_string(size) + " bytes\n+ cancels.";
            screen_.show(text.c_str());
        }
    }
};

bool run(const std::string& root, Screen& screen, PadState& pad, std::string& error) {
    if (R_FAILED(romfsInit())) { error = "Cannot mount offline shader RomFS"; return false; }
    struct RomfsGuard { ~RomfsGuard() { romfsExit(); } } romfs_guard;
    nfsu::TrackSlice slice;
    nfsu::HeapUsage heap;
    const auto before = mallinfo();
    heap.begin(before.uordblks, before.arena);
    Progress progress(screen, pad, heap);
    screen.show("Event 1 / track 1003: checking first-lap assets, then preparing streamed sections.");
    const auto load_start = armGetSystemTick();
    if (!nfsu::load_track_slice(nfsu::posix_file_system(), root, slice, error, &progress, &progress)) return false;
    const auto after = mallinfo();
    heap.finish_load(after.uordblks, after.arena);
    const double load_ms = milliseconds(armGetSystemTick() - load_start);
    nfsu::TrackSliceFrame current;
    if (!nfsu::make_track_slice_frame(slice, 0, 0, current, error)) return false;
    screen.show("Assets PASS. Opening deko3d track renderer.\nA: play/pause hand path; left/right: jump 10 seconds; stick: look; B: reset; +: exit.");
    nfsu::DekoRenderer renderer;
    if (!renderer.open(current.commands, error, false, nfsu::kTrackSliceDrawLimit)) return false;
    std::vector<unsigned char> rgba;
    bool playing = false;
    double sample_cursor = 0, yaw = 0, drawn_yaw = 0;
    double frame_ms = 0, prepare_ms = 0;
    std::uint64_t last_tick = armGetSystemTick();
    std::uint64_t frames = 0;
    while (appletMainLoop()) {
        const auto start = armGetSystemTick();
        const double seconds = std::min(milliseconds(start - last_tick) / 1000.0, 0.25);
        last_tick = start;
        padUpdate(&pad);
        const auto down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_A) playing = !playing;
        if (down & HidNpadButton_B) { sample_cursor = 0; yaw = 0; playing = false; }
        if (down & HidNpadButton_Left) { sample_cursor = std::max(0.0, sample_cursor - 600); playing = false; }
        if (down & HidNpadButton_Right) { sample_cursor += 600; playing = false; }
        const auto stick = padGetStickPos(&pad, 0);
        if (stick.x > 6000 || stick.x < -6000) yaw += stick.x / 32767.0 * 90 * seconds;
        if (yaw > 180) yaw -= 360;
        if (yaw < -180) yaw += 360;
        if (playing) sample_cursor += seconds * 60;
        if (sample_cursor >= slice.path.size()) { sample_cursor = slice.path.size()-1; playing = false; }
        const auto sample = static_cast<std::size_t>(sample_cursor);
        if (sample != current.sample || yaw != drawn_yaw) {
            const auto prepare_start = armGetSystemTick();
            nfsu::TrackSliceFrame next;
            if (!nfsu::make_track_slice_frame(slice, sample, yaw, next, error)) return false;
            current = std::move(next);
            if (!renderer.update(current.commands, error)) return false;
            drawn_yaw = yaw;
            prepare_ms = milliseconds(armGetSystemTick() - prepare_start);
        }
        double render_ms = 0, readback_ms = 0;
        if (!renderer.capture(rgba, render_ms, readback_ms, error)) return false;
        sample_heap(heap);
        char contact[160];
        if (current.contact_status == nfsu::TrackContactStatus::hit)
            std::snprintf(contact, sizeof contact, "Contact HIT polygon %zu height %.2f (raw slot vertical)",
                          current.contact.polygon, current.contact.point[2]);
        else std::snprintf(contact, sizeof contact, "Contact %s: no surface in the query column (rise 2, drop 20)",
                           nfsu::track_contact_status_name(current.contact_status));
        const auto fallback = std::count_if(current.commands.owning_draws().begin(), current.commands.owning_draws().end(),
            [](const auto& draw) { return draw.texture_resource == nfsu::kNoHandle; });
        char text[2048];
        std::snprintf(text, sizeof text,
            "NFSU #54 Event 1 / track 1003 | ASSETS PASS | deko3d DXT | %s | %.12s\n"
            "Hand path %zu/%zu | %zu resident sections | far %.2f | visible %zu | unplaced %llu\n"
            "Draws %zu (%zu fallback) | triangles %llu | resources %zu | load %.0f ms\n"
            "GPU record/submit/wait %.2f ms | readback %.2f ms | last cull/record %.2f ms\n"
            "Previous full frame %.2f ms (includes readback, text and vsync)\n"
            "CPU payload EST %llu / 75497600 | textures %llu | collision %llu\n"
            "GPU blocks %llu / base %llu | LOAD HEAP OVER %llu bytes\n"
            "HEAP IN USE before %llu | after load %llu | now %llu\n"
            "HEAP LOAD OBS HWM %llu | delta %llu / 75497600\n"
            "HEAP RUN OBS HWM %llu | arena now %llu | arena OBS HWM %llu\n"
            "%s\n"
            "A play/pause | left/right +/-10s | left stick look | B reset | + exit | photograph this panel",
            playing ? "PLAY" : "PAUSED", NFSU_TRACK_BUILD_TAG, current.sample, slice.path.size()-1, slice.sections,
            current.far_plane, current.visible, static_cast<unsigned long long>(current.unplaced),
            current.commands.owning_draws().size(), static_cast<std::size_t>(fallback), static_cast<unsigned long long>(current.triangles),
            current.commands.resource_table().resources.size(), load_ms, render_ms, readback_ms, prepare_ms, frame_ms,
            static_cast<unsigned long long>(slice.streaming_bytes), static_cast<unsigned long long>(slice.texture_bytes),
            static_cast<unsigned long long>(slice.contacts.memory.resident_bytes),
            static_cast<unsigned long long>(renderer.gpu_bytes()),
            static_cast<unsigned long long>(nfsu::kGpuBudgetPlaceholder),
            static_cast<unsigned long long>(heap.load_overrun(75497600)),
            static_cast<unsigned long long>(heap.before_load),
            static_cast<unsigned long long>(heap.after_load),
            static_cast<unsigned long long>(heap.current),
            static_cast<unsigned long long>(heap.load_peak),
            static_cast<unsigned long long>(heap.load_delta()),
            static_cast<unsigned long long>(heap.peak),
            static_cast<unsigned long long>(heap.extent),
            static_cast<unsigned long long>(heap.extent_peak),
            contact);
        screen.show(text, &rgba);
        frame_ms = milliseconds(armGetSystemTick() - start);
        if ((frames++ % 120) == 0) { std::printf("%s\n", text); std::fflush(stdout); }
    }
    return renderer.close(error);
}
} // namespace

int main(int argc, char** argv) {
    bool sockets = false; int link = -1;
    if (__nxlink_host.s_addr && R_SUCCEEDED(socketInitializeDefault())) { sockets = true; link = nxlinkStdio(); }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    {
        Screen screen;
        if (!screen.open()) return 1;
        std::string error;
        bool ok = false;
        try {
            if (argc > 2) error = "Usage: nfsu_switch_track.nro [sdmc:/nfsu]";
            else ok = run(argc == 2 ? argv[1] : "sdmc:/nfsu", screen, pad, error);
        } catch (const Cancel&) { ok = true; }
          catch (const std::exception& e) { error = e.what(); }
          catch (...) { error = "Unknown track diagnostic failure"; }
        if (!ok) {
            error += "\nNo hardware result recorded. Press + to exit.";
            std::printf("%s\n", error.c_str()); std::fflush(stdout);
            while (appletMainLoop()) {
                padUpdate(&pad);
                if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
                screen.show(error.c_str());
            }
        }
    }
    if (link >= 0) close(link);
    if (sockets) socketExit();
    return 0;
}
