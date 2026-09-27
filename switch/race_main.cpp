// Event 1 on the pad, with the desktop fixed-step simulation and GPU scene.
#include "deko_renderer.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/heap_usage.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/screen_text.h"
#include "nfsu/stock_view.h"
#include <switch.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <malloc.h>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <stdexcept>

namespace {
using namespace nfsu;
constexpr unsigned width = 1280, height = 720;
double ms(std::uint64_t ticks) { return armTicksToNs(ticks)/1000000.0; }
std::string format(const char* pattern, ...) {
    char text[512]; va_list args; va_start(args, pattern);
    std::vsnprintf(text, sizeof text, pattern, args); va_end(args); return text;
}
void require(bool okay, const std::string& error) { if (!okay) throw std::runtime_error(error); }
struct Cancel {};
class Screen {
    Framebuffer fb_{};
    bool opened_ = false;
public:
    ~Screen() { close(); }
    bool open() {
        if (opened_) return true;
        if (R_FAILED(framebufferCreate(&fb_, nwindowGetDefault(), width, height, PIXEL_FORMAT_RGBA_8888, 2))) return false;
        if (R_FAILED(framebufferMakeLinear(&fb_))) { framebufferClose(&fb_); return false; }
        opened_ = true; return true;
    }
    void close() {
        if (opened_) { framebufferClose(&fb_); opened_ = false; }
    }
    void show(const std::vector<std::string>& lines) {
        if (!opened_ && !open()) throw std::runtime_error("framebuffer unavailable");
        u32 stride = 0; auto* pixels = static_cast<unsigned char*>(framebufferBegin(&fb_, &stride));
        if (!pixels) throw std::runtime_error("framebuffer unavailable");
        ScreenImage image{pixels, width, height, stride};
        fill_screen_rect(image, 0, 0, width, height, {12,16,22,255});
        const auto count = std::min<std::size_t>(lines.size(), 38);
        fill_screen_rect(image, 0, 0, width, 8+count*18, {12,16,22,255});
        // One bounded line per row. Long diagnostics cannot wrap into and
        // overwrite the following measurement, as in the #54 photo.
        for (std::size_t row = 0; row < count; ++row)
            draw_screen_text(image, 8, 4+18*row, lines[row].substr(0,105), 2,
                             {235,235,235,255}, {12,16,22,255});
        framebufferEnd(&fb_);
    }
};
class Progress final : public AssetCheckObserver, public TrackSliceLoadObserver {
    Screen& screen_; PadState& pad_; HeapUsage& heap_;
    std::string file_; std::uint64_t previous_ = 0, last_screen_ = 0;
    void poll() {
        const auto info = mallinfo();
        heap_.observe(info.uordblks, info.arena);
        padUpdate(&pad_);
        if (!appletMainLoop() || (padGetButtonsDown(&pad_) & HidNpadButton_Plus)) throw Cancel{};
    }
public:
    Progress(Screen& screen, PadState& pad, HeapUsage& heap) : screen_(screen), pad_(pad), heap_(heap) {}
    void checkpoint(const char* phase) override {
        poll();
        const auto now = armGetSystemTick();
        if (ms(now-last_screen_) >= 100) {
            last_screen_ = now;
            screen_.show({std::string("Loading: ")+phase,
                format("HEAP LOAD OBS HWM delta %llu / 50331648 | over %llu",
                    (unsigned long long)heap_.load_delta(), (unsigned long long)heap_.load_overrun(50331648)),
                "HOME serviced; + cancels."});
        }
    }
    void file_started(std::size_t index, std::size_t total, const AssetFile& file) override {
        poll(); previous_ = 0; file_ = file.path;
        screen_.show({format("NFSU #196 / #47 first-lap check %zu/%zu",index+1,total),file_,"All game files read-only. + cancels."});
    }
    void file_progress(std::size_t, std::uint64_t hashed, std::uint64_t bytes) override {
        poll();
        if (hashed == bytes || hashed-previous_ >= 8*1024*1024) {
            previous_ = hashed;
            screen_.show({"#47 FIRST-LAP ASSET CHECK",file_,format("%llu / %llu bytes",
                         (unsigned long long)hashed,(unsigned long long)bytes),"+ cancels"});
        }
    }
};
static_assert(JOYSTICK_MAX == kAxisMax, "controller axis range");
static_assert(kNpadButtonZR == HidNpadButton_ZR && kNpadButtonZL == HidNpadButton_ZL, "trigger mapping");
static_assert(kNpadButtonB == HidNpadButton_B && kNpadButtonX == HidNpadButton_X, "driving face buttons");
static_assert(kNpadButtonL == HidNpadButton_L && kNpadButtonR == HidNpadButton_R &&
              kNpadButtonMinus == HidNpadButton_Minus, "shift/reset mapping");
GamepadFrame sample(const PadState& pad, std::uint64_t tick) {
    GamepadFrame frame; frame.time_us = tick*1000000/60;
    auto& state = frame.pads[0]; state.connected = padIsConnected(&pad);
    state.buttons = gamepad_buttons_from_npad(padGetButtons(&pad));
    const auto left = padGetStickPos(&pad,0), right = padGetStickPos(&pad,1);
    state.axes[0] = left.x; state.axes[1] = left.y; state.axes[2] = right.x; state.axes[3] = right.y;
    return frame;
}
void run(const std::string& root, bool replay_mode, Screen& screen, PadState& pad) {
    require(R_SUCCEEDED(romfsInit()), "cannot mount race shader/replay RomFS");
    struct Guard { ~Guard() { romfsExit(); } } guard;
    std::string error;
    RaceReplay replay;
    if (replay_mode) {
        std::ifstream in("romfs:/event1.txt", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        require(bool(in) && replay.load(text,error), "bundled replay: "+error);
    }
    RaceGpuScene scene; HeapUsage heap;
    const auto before = mallinfo(); heap.begin(before.uordblks,before.arena);
    Progress progress(screen,pad,heap);
    const auto load_start = armGetSystemTick();
    require(scene.load(posix_file_system(), root, error, &progress, &progress), error);
    screen.show({"ASSETS PASS. Preparing event 1, stock 240SX and race course..."});
    progress.checkpoint("race course");
    RaceRun race; require(race.start(root,scene.track(),error),error);
    progress.checkpoint("race course prepared");
    const auto persistent = scene.car_bytes()+race.persistent_bytes()+scene.track().contacts.memory.resident_bytes+scene.cache_bytes();
    require(persistent <= kPersistentBudgetPlaceholder, "race persistent payload exceeds 64 MiB");
    RaceGpuFrame frame; require(scene.frame(race.session(),frame,error),error);
    const double load_ms = ms(armGetSystemTick()-load_start);
    const auto after = mallinfo(); heap.finish_load(after.uordblks,after.arena);
    screen.show({"ASSETS PASS. Uploading track and car to deko3d..."});
    const auto upload_start = armGetSystemTick();
    screen.close();
    DekoRenderer renderer;
    require(renderer.open(frame.commands,error,true,kTrackSliceDrawLimit),error);
    const double initial_upload = ms(armGetSystemTick()-upload_start);
    RaceTickDebt clock;
    auto previous = armGetSystemTick();
    bool paused = false, printed_result = false, printed_final_perf = false;
    std::uint64_t presented = 0;
    while (appletMainLoop()) {
        const auto begin = armGetSystemTick();
        const auto elapsed = armTicksToNs(begin-previous)/1000; previous = begin;
        padUpdate(&pad); const auto down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_Y) { paused = !paused; clock.reset(); }
        // A long OS suspend is an explicit pause, not thousands of live
        // control ticks using a newly sampled pad. Ordinary slow frames keep debt.
        if (elapsed > 1000000) { paused = true; clock.reset(); }
        const bool connected = padIsConnected(&pad);
        const bool running = race.session().timing().outcome == RaceOutcome::running;
        if (!paused && running) clock.add(elapsed); else clock.reset();
        double simulation = 0, residency = 0, record = 0, upload = 0;
        unsigned ticks = 0;
        while (!paused && race.session().timing().outcome == RaceOutcome::running && ticks < 8 && clock.take()) {
            GamepadFrame input;
            if (replay_mode) require(replay.next(race.session().timing().step,input,error),error);
            else input = sample(pad,race.session().timing().step);
            auto stage = armGetSystemTick();
            require(race.simulate(input,error),error); simulation += ms(armGetSystemTick()-stage);
            stage = armGetSystemTick(); require(race.commit(error),error);
            if (replay_mode) std::fputs(race.step_line().c_str(),stdout);
            residency += ms(armGetSystemTick()-stage);
            ++ticks;
        }
        if (ticks) {
            auto stage = armGetSystemTick();
            RaceGpuFrame next; require(scene.frame(race.session(),next,error),error);
            frame = std::move(next); record = ms(armGetSystemTick()-stage);
            stage = armGetSystemTick(); require(renderer.update(frame.commands,error),error);
            upload = ms(armGetSystemTick()-stage);
        }
        const bool finished = race.session().timing().outcome == RaceOutcome::finished;
        if (finished && !printed_result) {
            for (const auto& line : race.result_lines()) std::printf("%s\n",line.c_str());
            if (replay_mode) std::printf("REPLAY %s desktop per-step stream and result\n",
                replay.exhausted() && race.matches_event1_replay() ? "EQUAL" : "DIFFERENT");
            std::fflush(stdout); printed_result = true;
        }
        DekoPresentTiming present;
        const auto present_start = armGetSystemTick();
        require(renderer.present(error,&present),error);
        const double present_cpu = ms(armGetSystemTick()-present_start);
        const auto info = mallinfo(); heap.observe(info.uordblks,info.arena);
        const double frame_ms = ms(armGetSystemTick()-begin);
        ++presented;
        if (presented == 1 || presented % 120 == 0 || (finished && !printed_final_perf)) {
            const auto& car = race.session().car();
            const double speed = vehicle_sqrt(car.velocity.x*car.velocity.x+car.velocity.y*car.velocity.y)*3.6;
            auto hud = race_session_hud(race.session()); hud.resize(3);
            std::printf("PERF BUILD %.12s | %s %s | frame %llu step %llu | %s | %s | %s\n",
                NFSU_RACE_BUILD_TAG,replay_mode?"REPLAY":"LIVE",paused?"PAUSED":finished?"FINISHED":"RUNNING",
                (unsigned long long)presented,(unsigned long long)race.session().timing().step,
                hud[0].c_str(),hud[1].c_str(),hud[2].c_str());
            std::printf("PERF FRAME %.2f / 16.67 ms %s | SIM %.2f | RES/HASH/LOG %.2f | CULL/RECORD %.2f | UPDATE %.2f | PRESENT CPU %.2f (ACQUIRE %.2f FENCE %.2f CMD %.2f SUBMIT %.2f) | CATCH-UP %u DEBT %llu\n",
                frame_ms,frame_ms>16.6667?"OVER":"WITHIN",simulation,residency,record,upload,present_cpu,
                present.acquire_ms,present.fence_wait_ms,present.record_ms,present.submit_ms,
                ticks,(unsigned long long)clock.debt_ticks());
            std::printf("PERF %.1f KM/H gear %d pad %s | DRAWS %zu track + %zu car | TRIANGLES %llu | far %.2f | visible %zu | unplaced %llu | RESIDENCY %zu | GPU %llu / 268435456 | resources %zu\n",
                speed,int(car.gear),connected?"CONNECTED":"DISCONNECTED - NEUTRAL",frame.track_draws,frame.car_draws,
                (unsigned long long)frame.triangles,frame.far_plane,frame.visible,
                (unsigned long long)frame.unplaced,race.resident_count(),
                (unsigned long long)renderer.gpu_bytes(),frame.commands.uploads.resources.size());
            std::printf("PERF LOAD %.0f ms initial upload %.0f ms | CPU stream %llu / 50331648 | persistent %llu / 67108864 | CACHE EST %llu / 8388608 | HEAP before %llu after load %llu now %llu load HWM %llu run HWM %llu\n",
                load_ms,initial_upload,(unsigned long long)scene.track().streaming_bytes,(unsigned long long)persistent,
                (unsigned long long)scene.cache_bytes(),
                (unsigned long long)heap.before_load,(unsigned long long)heap.after_load,
                (unsigned long long)heap.current,(unsigned long long)heap.load_peak,(unsigned long long)heap.peak);
            std::fflush(stdout);
            if (finished) printed_final_perf = true;
        }
    }
    require(renderer.close(error),error);
}
} // namespace
int main(int argc, char** argv) {
    bool sockets = false; int link = -1;
    if (__nxlink_host.s_addr && R_SUCCEEDED(socketInitializeDefault())) { sockets = true; link = nxlinkStdio(); }
    padConfigureInput(1,HidNpadStyleSet_NpadStandard); PadState pad; padInitializeDefault(&pad);
    {
        Screen screen; if (!screen.open()) return 1;
        try {
            std::string root = "sdmc:/nfsu"; bool replay = false, selected = false, root_seen = false;
            for (int i = 1; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--replay") { replay = true; selected = true; }
                else if (arg == "--live") { replay = false; selected = true; }
                else if (!root_seen && arg.rfind("--",0) != 0) { root = arg; root_seen = true; }
                else throw std::runtime_error("Usage: nfsu_switch_race.nro [sdmc:/nfsu] [--live|--replay]");
            }
            while (!selected && appletMainLoop()) {
                padUpdate(&pad); const auto down = padGetButtonsDown(&pad);
                if (down & HidNpadButton_Plus) throw Cancel{};
                if (down & HidNpadButton_A) selected = true;
                if (down & HidNpadButton_X) { selected = true; replay = true; }
                screen.show({"NFSU #196 - EVENT 1 / STOCK 240SX / TWO LAPS",std::string("BUILD ")+NFSU_RACE_BUILD_TAG,
                    "A: drive with Joy-Con / Pro Controller", "X: replay the pinned desktop event1.txt", "+: exit",
                    "The #47 first-lap check runs before loading. Assets: "+root,
                    "Release all controls during countdown to clear the #59 hot-plug latch.",
                    "During the race: Y pauses, + exits; performance and replay results print to nxlink.",
                    "No game data is bundled or written. Replay input is bundled in RomFS."});
            }
            if (selected) run(root,replay,screen,pad);
        } catch (const Cancel&) {
        } catch (const std::exception& e) {
            std::printf("ERROR: %s\n",e.what()); std::fflush(stdout);
            auto lines = wrap_screen_text(e.what(),105); lines.insert(lines.begin(),"NFSU RACE STOPPED - NO SUCCESS RESULT");
            lines.push_back("Photograph this error. + exits.");
            while (appletMainLoop()) { padUpdate(&pad); if (padGetButtonsDown(&pad)&HidNpadButton_Plus) break; screen.show(lines); }
        }
    }
    if (link >= 0) close(link);
    if (sockets) socketExit();
    return 0;
}
