// Event 1 on the pad, with the desktop fixed-step simulation and GPU scene.
#include "deko_renderer.h"
#include "profile_pages.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/heap_usage.h"
#include "nfsu/handling_telemetry.h"
#include "nfsu/frame_profile_stats.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/race_frame_stats.h"
#include "nfsu/screen_text.h"
#include "nfsu/stock_view.h"
#include "nfsu/race_audio.h"
#include "nfsu/frontend.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/freeride_skyline.h"
#include "nfsu/frontend_music.h"
#include "nfsu/intro_movie.h"
#include "frontend_startup_trace.h"
#include <switch.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <malloc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
std::string selected_car_label = "STOCK";
const char* car_shading_label="OFF";
using namespace nfsu;
class FrontendAppletClockHook {
public:
    explicit FrontendAppletClockHook(FrontendClock& clock,FrontendPresentationStats& stats):clock_(clock),stats_(stats) {
        appletHook(&cookie_,[](AppletHookType event,void* state) {
            if(event==AppletHookType_OnResume || event==AppletHookType_OnFocusState) {
                auto& self=*static_cast<FrontendAppletClockHook*>(state);
                self.clock_.resume();self.stats_.reset();
            }
        },this);
    }
    ~FrontendAppletClockHook() {appletUnhook(&cookie_);}
private:
    FrontendClock& clock_;FrontendPresentationStats& stats_;AppletHookCookie cookie_{};
};
constexpr unsigned width = 1280, height = 720;
double ms(std::uint64_t ticks) { return armTicksToNs(ticks)/1000000.0; }
class SwitchTiming final : public TimingService {
public:
    std::uint64_t now_us() override { return armTicksToNs(armGetSystemTick())/1000; }
};
ClockReadings read_clocks() {
    ClockReadings out;
    out.configuration_ok = R_SUCCEEDED(appletGetCurrentPerformanceConfiguration(&out.configuration));
    if (hosversionAtLeast(8,0,0)) {
        if (R_FAILED(clkrstInitialize())) return out;
        const auto read = [](PcvModuleId module, std::uint32_t& hz) {
            ClkrstSession session{};
            if (R_FAILED(clkrstOpenSession(&session, module, 3))) return false;
            const bool ok = R_SUCCEEDED(clkrstGetClockRate(&session, &hz));
            clkrstCloseSession(&session);
            return ok;
        };
        out.cpu_bus_ok = read(PcvModuleId_CpuBus, out.cpu_bus_hz);
        out.gpu_ok = read(PcvModuleId_GPU, out.gpu_hz);
        out.emc_ok = read(PcvModuleId_EMC, out.emc_hz);
        clkrstExit();
    } else if (R_SUCCEEDED(pcvInitialize())) {
        out.cpu_bus_ok = R_SUCCEEDED(pcvGetClockRate(PcvModule_CpuBus,&out.cpu_bus_hz));
        out.gpu_ok = R_SUCCEEDED(pcvGetClockRate(PcvModule_GPU,&out.gpu_hz));
        out.emc_ok = R_SUCCEEDED(pcvGetClockRate(PcvModule_EMC,&out.emc_hz));
        pcvExit();
    }
    return out;
}
class HandheldGpuProfile {
    u32 original_ = 0;
    bool active_ = false;
public:
    explicit HandheldGpuProfile(bool enabled) {
        if (!enabled) return;
        if (R_FAILED(apmInitialize()))
            throw std::runtime_error("cannot initialize apm for GPU boost");
        if (R_FAILED(apmGetPerformanceConfiguration(ApmPerformanceMode_Normal,&original_))) {
            apmExit();
            throw std::runtime_error("cannot read original handheld GPU configuration");
        }
        active_ = true;
        // Official Normal/handheld profile: 1020 CPU, 460.8 GPU, 1331.2 EMC.
        // apm:am may reject it on some launch contexts; report that failure.
        if (R_FAILED(apmSetPerformanceConfiguration(ApmPerformanceMode_Normal,0x92220008))) {
            apmSetPerformanceConfiguration(ApmPerformanceMode_Normal,original_);
            apmExit();
            active_ = false;
            throw std::runtime_error("apm rejected official handheld GPU configuration 0x92220008");
        }
    }
    ~HandheldGpuProfile() {
        if (active_) {
            const auto result = apmSetPerformanceConfiguration(ApmPerformanceMode_Normal,original_);
            if (R_FAILED(result)) std::printf("ERROR: could not restore handheld GPU configuration: 0x%08lX\n",
                                              static_cast<unsigned long>(result));
            apmExit();
        }
    }
};
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
    void movie(const MovieFrame& frame) {
        if (!opened_ && !open()) throw std::runtime_error("framebuffer unavailable");
        u32 stride=0; auto* pixels=static_cast<unsigned char*>(framebufferBegin(&fb_,&stride));
        if (!pixels) throw std::runtime_error("framebuffer unavailable");
        blit_movie(frame,pixels,width,height,stride);
        framebufferEnd(&fb_);
    }
    void menu(const FrontendArt& art, const FrontendMenu& menu) {
        if (!opened_ && !open()) throw std::runtime_error("framebuffer unavailable");
        u32 stride=0; auto* pixels=static_cast<unsigned char*>(framebufferBegin(&fb_,&stride));
        if (!pixels) throw std::runtime_error("framebuffer unavailable");
        art.draw({pixels,width,height,stride},menu);
        framebufferEnd(&fb_);
    }
    void show(const std::vector<std::string>& lines, const std::vector<unsigned char>* rgba = nullptr) {
        if (!opened_ && !open()) throw std::runtime_error("framebuffer unavailable");
        u32 stride = 0; auto* pixels = static_cast<unsigned char*>(framebufferBegin(&fb_, &stride));
        if (!pixels) throw std::runtime_error("framebuffer unavailable");
        ScreenImage image{pixels, width, height, stride};
        if (rgba && rgba->size() == width*height*4) {
            for (unsigned y = 0; y < height; ++y)
                std::memcpy(pixels+y*stride, rgba->data()+y*width*4, width*4);
        } else fill_screen_rect(image, 0, 0, width, height, {12,16,22,255});
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
    Screen& screen_; PadState& pad_; HeapUsage& heap_; bool frontend_;
    std::string file_; std::uint64_t previous_ = 0, last_screen_ = 0;
    void poll() {
        const auto info = mallinfo();
        heap_.observe(info.uordblks, info.arena);
        padUpdate(&pad_);
        if (!appletMainLoop() || (padGetButtonsDown(&pad_) & HidNpadButton_Plus)) throw Cancel{};
    }
public:
    Progress(Screen& screen, PadState& pad, HeapUsage& heap, bool frontend = false) : screen_(screen), pad_(pad), heap_(heap), frontend_(frontend) {}
    void checkpoint(const char* phase) override {
        poll();
        const auto now = armGetSystemTick();
        if (ms(now-last_screen_) >= 100) {
            last_screen_ = now;
            if (frontend_) {
                screen_.show({"Loading free driving...", "+ cancels loading"});
                return;
            }
            screen_.show({std::string("Loading: ")+phase,
                format("HEAP LOAD OBS HWM delta %llu / 50331648 | over %llu",
                    (unsigned long long)heap_.load_delta(), (unsigned long long)heap_.load_overrun(50331648)),
                "HOME serviced; + cancels."});
        }
    }
    void file_started(std::size_t index, std::size_t total, const AssetFile& file) override {
        poll(); previous_ = 0; file_ = file.path;
        if (frontend_) screen_.show({format("Loading free driving... %zu / %zu",index+1,total),"+ cancels loading"});
        else screen_.show({format("NFSU #196 / #47 first-lap check %zu/%zu",index+1,total),file_,"All game files read-only. + cancels."});
    }
    void file_progress(std::size_t, std::uint64_t hashed, std::uint64_t bytes) override {
        poll();
        if (hashed == bytes || hashed-previous_ >= 8*1024*1024) {
            previous_ = hashed;
            if (frontend_) {
                screen_.show({"Loading free driving...", "+ cancels loading"});
                return;
            }
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
std::string percentile_text(const RaceFrameStats& stats, unsigned percent) {
    const unsigned bin = stats.percentile_bin(percent);
    return bin == RaceFrameStats::kOverflowBin ? ">=100" :
        format("%.2f", (bin+1)*RaceFrameStats::kBinWidthMs);
}
void append_frame_stats(std::vector<std::string>& lines, const RaceFrameStats& stats,
                        bool panel_mode, unsigned unpause_resets) {
    lines.push_back(format("DRIVING FRAMES ONLY: no countdown, pause or finish; reset on unpause (%u)",
        unpause_resets));
    if (!stats.count()) {
        lines.push_back("FRAMES 0 | MEAN/P50/P95/P99/MAX -- | OVER 25.00 ms -- | CATCH-UP MAX --");
        lines.push_back("MEAN STAGES -- (no measured driving frame yet)");
        return;
    }
    const auto mean = stats.mean();
    const auto p50 = percentile_text(stats,50), p95 = percentile_text(stats,95),
        p99 = percentile_text(stats,99);
    lines.push_back(format("FRAMES %llu | MEAN %.2f | P50 %s | P95 %s | P99 %s | MAX %.2f ms",
        (unsigned long long)stats.count(),mean.frame_ms,p50.c_str(),p95.c_str(),p99.c_str(),stats.max_ms()));
    lines.push_back(format("OVER 25.00 ms %llu/%llu (%.2f%%) | CATCH-UP MAX %u | percentile bins 0.25 ms",
        (unsigned long long)stats.over_budget(),(unsigned long long)stats.count(),
        100.0*stats.over_budget()/stats.count(),stats.catch_up_max()));
    lines.push_back(format("MEAN ms SIM %.2f | RES/HASH/LOG %.2f | CULL/RECORD %.2f | UPDATE %.2f",
        mean.simulation_ms,mean.residency_ms,mean.record_ms,mean.update_ms));
    if (panel_mode) {
        lines.push_back(format("MEAN ms DRAW %.2f | TRANSFER/COPY %.2f | HUD/PANEL %.2f",
            mean.draw_ms,mean.transfer_ms,mean.panel_ms));
        lines.push_back("PRESENT CPU/ACQUIRE/FENCE/CMD/SUBMIT n/a (panel readback path)");
    } else {
        lines.push_back(format("MEAN ms PRESENT CPU %.2f | ACQUIRE %.2f | FENCE %.2f",
            mean.present_cpu_ms,mean.acquire_ms,mean.fence_ms));
        lines.push_back(format("MEAN ms CMD %.2f | SUBMIT %.2f",mean.command_ms,mean.submit_ms));
    }
}
void append_audio_status(std::vector<std::string>& lines,
                         const audio::RaceAudioService& sound,
                         const std::string& startup_note) {
    const auto stats = sound.stats();
    lines.push_back(format("AUDIO U %llu | RING %zu/%zu | LATE %llu | QUEUE %u | THREAD %u us | CLIP %llu",
        (unsigned long long)stats.ring_underruns,
        stats.ring_fill, stats.ring_capacity,
        (unsigned long long)stats.output_late,
        stats.queued_frames, stats.thread_us,
        (unsigned long long)stats.mix_clips));
    lines.push_back(format("AUDIO OVER %llu samples | LIMIT %llu frames",
        (unsigned long long)stats.mix_overloads,
        (unsigned long long)stats.mix_limited_frames));
    const auto diagnostic = sound.diagnostic();
    lines.push_back("AUDIO STATUS " +
                    (diagnostic.empty() ? startup_note : diagnostic));
}
void show_result(Screen& screen, PadState& pad, const RaceRun& race, const RaceReplay& replay,
                 bool replay_mode, bool panel_mode, bool paused,
                 const RaceFrameStats& stats, unsigned unpause_resets,
                 const std::vector<std::string>& last_perf, const std::string& failure,
                 const ProfileResultContext* profile = nullptr) {
    if (profile) {
        const auto mode = appletGetOperationMode() == AppletOperationMode_Console ? "DOCKED" : "HANDHELD";
        const bool finished = race.session().timing().outcome == RaceOutcome::finished;
        const char* verdict = !failure.empty() ||
            (finished && !(replay.exhausted() && race.matches_configured_replay()))
            ? "DIFFERENT" : finished ? "EQUAL" : "RUNNING";
        std::vector<std::string> replay_rows = {
            format("%s | REPLAY %s | LAST STEP %llu",
                race_outcome_name(race.session().timing().outcome),
                replay_mode?verdict:"n/a",(unsigned long long)race.session().timing().step)
        };
        for (const auto& row : race.result_lines())
            if (row.rfind("lap ",0) == 0 || row.rfind("total ticks ",0) == 0)
                replay_rows.push_back(row);
        if (replay_mode) {
            const auto state = race.sequence_hash(), steps = race.steps_hash();
            replay_rows.push_back("STATE SHA A " + state.substr(0,32));
            replay_rows.push_back("STATE SHA B " + state.substr(32));
            replay_rows.push_back("STEPS SHA A " + steps.substr(0,32));
            replay_rows.push_back("STEPS SHA B " + steps.substr(32));
        }
        const auto pages = build_profile_pages(std::move(replay_rows),replay_mode,stats,
            unpause_resets,*profile,failure,mode);
        std::size_t page = 0;
        while (appletMainLoop()) {
            screen.show(pages[page]);
            padUpdate(&pad);
            const auto down = padGetButtonsDown(&pad);
            if (down & HidNpadButton_Plus) break;
            if (down & HidNpadButton_Right) page = (page+1)%pages.size();
            if (down & HidNpadButton_Left) page = (page+pages.size()-1)%pages.size();
        }
        return;
    }
    const bool finished = race.session().timing().outcome == RaceOutcome::finished;
    std::vector<std::string> lines = {format("NFSU EVENT %u RESULT | BUILD %.12s | %s %s",
        race.event_number(), NFSU_RACE_BUILD_TAG, replay_mode ? "REPLAY" : "LIVE", panel_mode ? "PANEL" : "DIRECT")};
    lines.push_back(format("OUTCOME %s%s | step %llu", race_outcome_name(race.session().timing().outcome),
        paused && !finished ? " (PAUSED)" : "",
        (unsigned long long)race.session().timing().step));
    if (!failure.empty()) {
        auto wrapped = wrap_screen_text(failure, 105);
        lines.push_back("ERROR - NO SUCCESS RESULT");
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if (replay_mode) {
        const char* verdict = !failure.empty() || (finished && !(replay.exhausted() && race.matches_configured_replay()))
            ? "DIFFERENT" : finished ? "EQUAL" : "RUNNING";
        lines.push_back(std::string("REPLAY ") + verdict + " desktop per-step stream and result");
    }
    const auto result = race.result_lines();
    for (const auto& row : result) {
        if (!finished && row.rfind("total ticks ",0) == 0) {
            const int completed = race.session().timing().racers[0].highest_lap;
            for (int lap = completed+1; lap <= 2; ++lap)
                lines.push_back(format("lap %d time --:--.-- (unfinished)", lap));
            lines.push_back("total ticks -- time --:--.-- (unfinished)");
        } else lines.push_back(row);
    }
    if (replay_mode) {
        auto last = race.step_line();
        if (!last.empty() && last.back() == '\n') last.pop_back();
        lines.push_back("last step " + (last.empty() ? std::string("none") : last));
    }
    append_frame_stats(lines,stats,panel_mode,unpause_resets);
    lines.insert(lines.end(), last_perf.begin(), last_perf.end());
    lines.push_back("+ exits to hbmenu");
    while (appletMainLoop()) {
        screen.show(lines);
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    }
}
// --sky on draws the sky after the opaque world; off omits it; early is the
// issue 227 diagnostic that submits it with the opaque pass, as before.
void run(const std::string& root, bool replay_mode, bool panel_mode, bool profile_frame,
          bool particles_enabled, const std::string& sky, bool tuned, bool glows,
          bool car_shading, bool wet_road_reflections, TextureFilterMode texture_filter,
          std::uint32_t requested_height, bool gpu_boost, bool hud_mode, bool hud_trace,
          const VehicleUpgradeLevels* performance, bool handling_telemetry, RaceSteering steering,
          const nfsu::CarAssets& selected_car, Screen& screen, PadState& pad, bool free_drive = false,
          const nfsu::RaceCarDress* dress = nullptr, std::uint32_t event_number = 1) {
    selected_car_label = dress!=nullptr ? "EDDIE "+selected_car.car_type_name :
        (tuned ? "TUNED " : "STOCK ") + selected_car.car_type_name;
    car_shading_label=car_shading ? "ON static" : "OFF";
    HandheldGpuProfile gpu_profile(gpu_boost);
    bool docked = appletGetOperationMode() == AppletOperationMode_Console;
    auto scene_height = deko_effective_scene_height(requested_height,docked,!panel_mode);
    DekoSceneTarget scene_target;
    require(plan_deko_scene_target(width,height,!panel_mode,scene_height,scene_target),
            "--scene-height below 720 requires direct presentation (no --panel)");
    std::string error;
    RaceReplay replay;
    if (replay_mode) {
        require(event_number==1,"event 8 currently requires --live");
        const bool upgraded=performance && !performance_levels_equal(*performance,{});
        require(!upgraded || performance_levels_equal(*performance,performance1_levels()),
                "replay supports zero levels or the documented performance1 setup; use --live for other levels");
        std::ifstream in(upgraded ? "romfs:/performance1.txt" : "romfs:/event1.txt", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        require(bool(in) && replay.load(text,error,upgraded), "bundled replay: "+error);
    }
    RaceGpuScene scene;
    require(scene.set_texture_filter(texture_filter), "invalid texture filter policy");
    std::printf("TEXTURE FILTER %s (world/car diffuse; sky/effects unchanged)\n",
                texture_filter == TextureFilterMode::linear ? "linear" : "legacy");
    scene.set_particles_enabled(particles_enabled);
    RaceRun race; HeapUsage heap;
    require(race.set_event(event_number,error),error);
    require(race.set_free_drive(free_drive,error),error);
    require(!replay_mode || steering==RaceSteering::baseline,"recovered steering requires --live; pinned replay uses baseline");
    require(race.set_steering(steering,error),error);
    require(race.set_car(selected_car.car_type_name,error),error);
    std::printf("steering %s\n",race_steering_name(steering));
    if (performance) {
        require(race.set_performance_levels(*performance,error),error);
        std::printf("performance-levels %s\n",performance_levels_text(*performance).c_str());
    } else if (handling_telemetry) std::puts("performance-levels legacy-stock");
    HandlingTelemetryLog handling_log(handling_telemetry ? stdout : nullptr);
    std::printf("PARTICLES %s\n",particles_enabled?"ON":"OFF");
    require(scene.set_sky(sky != "off"), "sky mode set after scene load");
    scene.set_wet_road_reflections(wet_road_reflections);
    const auto before = mallinfo(); heap.begin(before.uordblks,before.arena);
    Progress progress(screen,pad,heap,free_drive);
    const auto load_start = armGetSystemTick();
    require(race.prepare_metadata(root,error,&progress),error);
    require(scene.load(posix_file_system(), root, error, &progress, &progress,
                       tuned, glows, car_shading, &selected_car, dress, event_number), error);
    screen.show({(free_drive ? std::string("Preparing free driving, ") :
                 "ASSETS PASS. Preparing event " + std::to_string(event_number) + ", ") +
                 selected_car_label + " and race course..."});
    if (!free_drive) screen.show({car_shading ? "CAR SHADING ON: static environment approximation" : "CAR SHADING OFF"});
    progress.checkpoint("race course");
    require(race.start(root,scene.track(),error,&progress),error);
    progress.checkpoint("race course prepared");
    audio::RaceAudioService sound;
    std::string audio_note;
    const auto render_core = svcGetCurrentProcessorNumber();
    const auto audio_core = render_core == 2 ? 1 : 2;
    if (R_SUCCEEDED(svcSetThreadCoreMask(threadGetCurHandle(),
            static_cast<s32>(render_core), 1U << render_core))) {
        const bool audio_started = sound.start(std::filesystem::path(root), audio_note,
            [&](const char* phase) { progress.checkpoint(phase); },
            [audio_core] {
                return R_SUCCEEDED(svcSetThreadCoreMask(threadGetCurHandle(),
                           audio_core, 1U << audio_core)) &&
                       R_SUCCEEDED(svcSetThreadPriority(threadGetCurHandle(), 0x30));
            },free_drive);
        if (!audio_started && audio_note.empty())
            audio_note = "audio disabled: startup failed without diagnostic";
    } else audio_note = "audio disabled: render core pin failed";
    std::printf("AUDIO: %s\n", audio_note.c_str());
    RaceHudAtlas hud_atlas;
    if (hud_mode && !panel_mode)
        require(load_race_hud_atlas(root,hud_atlas,error),error);
    const auto persistent = scene.car_bytes()+race.persistent_bytes()+
        scene.track().contacts.memory.resident_bytes+scene.cache_bytes()+
        sound.resident_bytes()+hud_atlas.rgba.size();
    require(persistent <= kPersistentBudgetPlaceholder, "race persistent payload exceeds 64 MiB");
    RaceGpuFrame frame; require(scene.frame(race.session(),frame,error),error);
    const double load_ms = ms(armGetSystemTick()-load_start);
    const auto after = mallinfo(); heap.finish_load(after.uordblks,after.arena);
    screen.show({free_drive ? "Starting free driving..." : "ASSETS PASS. Uploading track and car to deko3d..."});
    const auto upload_start = armGetSystemTick();
    if (!panel_mode) screen.close();
    docked = appletGetOperationMode() == AppletOperationMode_Console;
    scene_height = deko_effective_scene_height(requested_height,docked,!panel_mode);
    require(plan_deko_scene_target(width,height,!panel_mode,scene_height,scene_target),
            "invalid scene target after operation mode change");
    DekoRenderer renderer;
    require(renderer.open(frame.commands,error,!panel_mode,kTrackSliceDrawLimit,profile_frame,true,
                          sky == "early",scene_height,
                          hud_mode && !panel_mode ? &hud_atlas : nullptr),error);
    const double initial_upload = ms(armGetSystemTick()-upload_start);
    std::unique_ptr<FrameProfileStats> profile_stats;
    if (profile_frame) profile_stats = std::make_unique<FrameProfileStats>();
    SwitchTiming profile_clock;
    std::vector<unsigned char> rgba;
    RaceTickDebt clock;
    auto previous = armGetSystemTick();
    bool paused = false, printed_result = false, printed_final_perf = false;
    std::uint64_t presented = 0;
    double previous_frame = 0, previous_hud = 0;
    std::vector<std::string> last_perf;
    RaceFrameStats stats;
    RaceHudFrame hud_frame;
    unsigned unpause_resets = 0;
    std::string failure;
    std::uint64_t display_frame = 0;
    try {
    while (appletMainLoop()) {
        const bool now_docked = appletGetOperationMode() == AppletOperationMode_Console;
        if (now_docked != docked) {
            docked = now_docked;
            const auto next_height = deko_effective_scene_height(requested_height,docked,!panel_mode);
            if (next_height != scene_height) {
                require(renderer.reconfigure_target(frame.commands,error,kTrackSliceDrawLimit,
                        profile_frame,sky == "early",next_height,
                        hud_mode && !panel_mode ? &hud_atlas : nullptr),error);
                scene_height = next_height;
                require(plan_deko_scene_target(width,height,!panel_mode,scene_height,scene_target),
                        "invalid scene target on operation mode change");
                stats.reset(); last_perf.clear();
                if (profile_stats) profile_stats->reset();
            }
            clock.reset(); previous = armGetSystemTick();
            continue;
        }
        const auto begin = armGetSystemTick();
        const auto elapsed = armTicksToNs(begin-previous)/1000; previous = begin;
        padUpdate(&pad); const auto down = padGetButtonsDown(&pad);
        const auto sampled_us = armTicksToNs(armGetSystemTick())/1000;
        ++display_frame;
        if (down & HidNpadButton_Plus) break;
        bool just_unpaused = false;
        if (down & HidNpadButton_Y) {
            just_unpaused = paused;
            paused = !paused;
            clock.reset();
            if (just_unpaused) {
                stats.reset(); last_perf.clear(); ++unpause_resets;
                if (profile_stats) { profile_stats->reset(); renderer.reset_gpu_profile(); }
            }
        }
        // A long OS suspend is an explicit pause, not thousands of live
        // control ticks using a newly sampled pad. Ordinary slow frames keep debt.
        if (elapsed > 1000000) { paused = true; clock.reset(); }
        const bool connected = padIsConnected(&pad);
        const bool running = race.session().timing().outcome == RaceOutcome::running;
        sound.set_suspended(paused);
        const bool driving_before = !paused && !just_unpaused && running &&
            race.session().timing().stage == RaceStage::racing;
        if (!paused && running) clock.add(elapsed); else clock.reset();
        double simulation = 0, residency = 0, record = 0, upload = 0;
        std::array<double,8> simulation_ticks{};
        RaceFrameProfile record_profile;
        DekoUpdateTiming update_timing;
        unsigned ticks = 0;
        while (!paused && race.session().timing().outcome == RaceOutcome::running && ticks < 8 && clock.take()) {
            GamepadFrame input;
            if (replay_mode) require(replay.next(race.session().timing().step,input,error),error);
            else input = sample(pad,race.session().timing().step);
            if (!replay_mode) {
                input.sample_clock_available = true;
                input.sample_time_us = sampled_us;
                input.display_frame = display_frame;
            }
            auto stage = armGetSystemTick();
            const auto consumed_us = armTicksToNs(stage)/1000;
            require(race.simulate(input,error),error);
            require(handling_log.write(race.session(),input,consumed_us,!replay_mode),
                    "handling telemetry write failed");
            const double tick_ms = ms(armGetSystemTick()-stage);
            simulation += tick_ms; simulation_ticks[ticks] = tick_ms;
            stage = armGetSystemTick(); require(race.commit(error),error);
            scene.advance_particles(race.session());
            auto audio_snapshot=audio::race_audio_snapshot(race.session());
            if (free_drive) { audio_snapshot.completed_laps=0; audio_snapshot.finished=false; }
            sound.on_tick(audio_snapshot);
            if (replay_mode) std::fputs(race.step_line().c_str(),stdout);
            residency += ms(armGetSystemTick()-stage);
            ++ticks;
        }
        if (ticks) {
            auto stage = armGetSystemTick();
            RaceGpuFrame next;
            require(scene.frame(race.session(),next,error,
                profile_frame ? &record_profile : nullptr,false,
                profile_frame ? &profile_clock : nullptr),error);
            frame = std::move(next); record = ms(armGetSystemTick()-stage);
            stage = armGetSystemTick();
            require(renderer.update(frame.commands,error,profile_frame ? &update_timing : nullptr),error);
            upload = ms(armGetSystemTick()-stage);
        }
        const bool finished = race.session().timing().outcome == RaceOutcome::finished;
        RaceFrameSample measured;
        measured.simulation_ms = simulation;
        measured.residency_ms = residency;
        measured.record_ms = record;
        measured.update_ms = upload;
        measured.catch_up = ticks;
        double measured_frame_ms = 0;
        if (finished && !printed_result) {
            for (const auto& line : race.result_lines()) std::printf("%s\n",line.c_str());
            if (replay_mode) std::printf("REPLAY %s desktop per-step stream and result\n",
                replay.exhausted() && race.matches_configured_replay() ? "EQUAL" : "DIFFERENT");
            std::fflush(stdout); printed_result = true;
        }
        DekoPresentTiming present;
        double present_cpu = 0, mallinfo_ms = 0, frame_ms = 0;
        if (panel_mode) {
            double draw = 0, readback = 0;
            const auto capture_start = armGetSystemTick();
            require(renderer.capture(rgba,draw,readback,error),error);
            const double transfer = std::max(0.0,ms(armGetSystemTick()-capture_start)-draw);
            const auto hud_start = armGetSystemTick();
            const auto info = mallinfo(); heap.observe(info.uordblks,info.arena);
            const auto& car = race.session().car();
            const double speed = vehicle_sqrt(car.velocity.x*car.velocity.x+car.velocity.y*car.velocity.y)*3.6;
            auto hud = race_session_hud(race.session(),event_number); hud.resize(3);
            std::vector<std::string> lines = {format("NFSU #196 | PANEL | %s | %s | BUILD %.12s",
                replay_mode?"REPLAY":"LIVE",paused?"PAUSED":finished?"FINISHED":"RUNNING",NFSU_RACE_BUILD_TAG)};
            lines.push_back("DIAGNOSTIC PANEL: frame time includes GPU readback and CPU text copy");
            lines.push_back(hud[0]+" | "+hud[1]+" | "+hud[2]);
            lines.push_back(format("%.1f KM/H | gear %d | pad %s | step %llu | physics 120 Hz / pad 60 Hz | catch-up %u",
                speed,int(car.gear),connected?"CONNECTED":"DISCONNECTED - NEUTRAL",(unsigned long long)race.session().timing().step,ticks));
            lines.push_back(format("SIM %.2f ms | RES/HASH/LOG %.2f ms | CULL/RECORD %.2f ms | UPLOAD/CONSTANTS %.2f ms",
                simulation,residency,record,upload));
            lines.push_back(format("DRAW %.2f ms | TRANSFER/COPY %.2f ms (GPU %.2f) | previous HUD/present %.2f ms",
                draw,transfer,readback,previous_hud));
            lines.push_back(format("Previous PANEL FRAME %.2f ms (>25 ms %s) | debt %llu ticks | load %.0f ms | initial upload %.0f ms",
                previous_frame,previous_frame>RaceFrameStats::kOverThresholdMs?"OVER":"WITHIN",(unsigned long long)clock.debt_ticks(),load_ms,initial_upload));
            lines.push_back(format("DRAWS %zu track + %zu car + %zu glow | TRIANGLES %llu | far %.2f | visible %zu | unplaced %llu",
                frame.track_draws,frame.car_draws,frame.glow_draws,(unsigned long long)frame.triangles,frame.far_plane,frame.visible,(unsigned long long)frame.unplaced));
            lines.push_back(format("RESIDENCY %zu sections | CPU stream %llu / 50331648 | persistent payload %llu / 67108864",
                race.resident_count(),(unsigned long long)scene.track().streaming_bytes,(unsigned long long)persistent));
            lines.push_back(format("Command cache metadata EST %llu / 8388608 bytes (immutable track templates)",
                (unsigned long long)scene.cache_bytes()));
            const auto memory = renderer.memory_usage();
            lines.push_back(format("GPU blocks %llu / 268435456 | frame uniforms %llu | resources %zu",
                (unsigned long long)renderer.gpu_bytes(),
                (unsigned long long)memory.frame_uniform_bytes,frame.commands.resource_table().resources.size()));
            lines.push_back(format("Static uniforms %llu | prepared commands %llu bytes",
                (unsigned long long)memory.static_uniform_bytes,
                (unsigned long long)memory.prepared_command_bytes));
            lines.push_back(format("MALLOC IN-USE before %llu | after load %llu | NOW %llu (includes GPU BLOCKS)",
                (unsigned long long)heap.before_load,(unsigned long long)heap.after_load,(unsigned long long)heap.current));
            lines.push_back(format("HEAP LOAD OBS HWM ABS %llu | delta %llu / 50331648 | LOAD HEAP OVER %llu",
                (unsigned long long)heap.load_peak,(unsigned long long)heap.load_delta(),(unsigned long long)heap.load_overrun(50331648)));
            lines.push_back(format("HEAP RUN OBS HWM %llu | ARENA NOW %llu | ARENA OBS HWM %llu (sampled)",
                (unsigned long long)heap.peak,(unsigned long long)heap.extent,(unsigned long long)heap.extent_peak));
            if (replay_mode) {
                const char* verdict = finished ? (replay.exhausted() && race.matches_configured_replay()?"EQUAL":"DIFFERENT") : "RUNNING";
                lines.push_back(std::string("REPLAY ")+verdict+" | state-sha256 "+race.sequence_hash());
                lines.push_back("steps-sha256 "+race.steps_hash());
                auto last = race.step_line(); if (!last.empty()) last.pop_back();
                lines.push_back("last step "+last);
            }
            append_audio_status(lines,sound,audio_note);
            lines.push_back("ZR gas | ZL brake/reverse | stick/D-pad steer | B handbrake | R/L shift | X camera | - reset");
            lines.push_back("Y pause/photo | + result | panel includes readback and CPU text cost");
            screen.show(lines,&rgba);
            previous_hud = ms(armGetSystemTick()-hud_start);
            previous_frame = ms(armGetSystemTick()-begin);
            measured_frame_ms = previous_frame;
            measured.draw_ms = draw;
            measured.transfer_ms = transfer;
            measured.panel_ms = previous_hud;
            ++presented;
            if (presented == 1 || presented % 120 == 0 || (finished && !printed_final_perf)) {
                std::printf("PERF PANEL BUILD %.12s | %s %s | frame %llu step %llu\n",
                    NFSU_RACE_BUILD_TAG,replay_mode?"REPLAY":"LIVE",
                    paused?"PAUSED":finished?"FINISHED":"RUNNING",
                    (unsigned long long)presented,(unsigned long long)race.session().timing().step);
                std::printf("PERF PANEL FRAME %.2f / 16.67 ms includes readback and panel | SIM %.2f | RES/HASH/LOG %.2f | CULL/RECORD %.2f | UPDATE %.2f | DRAW %.2f | TRANSFER/COPY %.2f (GPU %.2f) | HUD/PRESENT %.2f\n",
                    previous_frame,simulation,residency,record,upload,draw,transfer,readback,previous_hud);
                std::fflush(stdout);
                last_perf = {format("Last PANEL FRAME %.2f ms includes readback and panel | CULL/RECORD %.2f ms",previous_frame,record),
                    format("DRAW %.2f ms | TRANSFER/COPY %.2f ms (GPU %.2f) | HUD/PRESENT %.2f ms",draw,transfer,readback,previous_hud)};
                if (finished) printed_final_perf = true;
            }
        } else {
        const DekoSceneFrame scene_frame{race.session().timing().step,
            static_cast<std::uint32_t>(frame.track_draws),
            static_cast<std::uint32_t>(frame.car_draws),driving_before && !finished};
        const auto present_start = armGetSystemTick();
        if (hud_mode) build_race_hud_frame(race.session(),frame.commands.width,
                                           frame.commands.height,hud_frame);
        require(renderer.present(error,&present,profile_frame ? &scene_frame : nullptr,
                                 hud_mode ? &hud_frame : nullptr),error);
        present_cpu = ms(armGetSystemTick()-present_start);
        if (hud_trace) {
            const auto& r=hud_frame.readout;
            std::printf("HUD_FRAME present %llu tick %llu physics %llu arm-tick %llu rpm %.9f wheel-rpm %.9f gear %d rotation %.9f max %.0f redline %.0f\n",
                (unsigned long long)presented,
                (unsigned long long)race.session().timing().step,
                (unsigned long long)r.physics_step,(unsigned long long)present_start,
                r.engine_rpm,r.wheel_rpm,r.gear,r.rotation,r.max_rpm,r.redline_rpm);
        }
        const auto mall_start = armGetSystemTick();
        const auto info = mallinfo();
        mallinfo_ms = ms(armGetSystemTick()-mall_start);
        heap.observe(info.uordblks,info.arena);
        frame_ms = ms(armGetSystemTick()-begin);
        measured_frame_ms = frame_ms;
        measured.present_cpu_ms = present_cpu;
        measured.acquire_ms = present.acquire_ms;
        measured.fence_ms = present.fence_wait_ms;
        measured.command_ms = present.uniform_write_ms + present.record_ms + present.finish_ms;
        measured.submit_ms = present.submit_ms;
        ++presented;
        if (!profile_frame && (presented == 1 || presented % 120 == 0 ||
                               (finished && !printed_final_perf))) {
            const auto& car = race.session().car();
            const double speed = vehicle_sqrt(car.velocity.x*car.velocity.x+car.velocity.y*car.velocity.y)*3.6;
            auto hud = race_session_hud(race.session(),event_number); hud.resize(3);
            std::printf("PERF BUILD %.12s | %s %s | frame %llu step %llu | %s | %s | %s\n",
                NFSU_RACE_BUILD_TAG,replay_mode?"REPLAY":"LIVE",paused?"PAUSED":finished?"FINISHED":"RUNNING",
                (unsigned long long)presented,(unsigned long long)race.session().timing().step,
                hud[0].c_str(),hud[1].c_str(),hud[2].c_str());
            std::printf("PERF FRAME %.2f ms (>25 ms %s) | SIM %.2f | RES/HASH/LOG %.2f | CULL/RECORD %.2f | UPDATE %.2f | PRESENT CPU %.2f (ACQUIRE %.2f FENCE %.2f CMD %.2f SUBMIT %.2f) | CATCH-UP %u DEBT %llu\n",
                frame_ms,frame_ms>RaceFrameStats::kOverThresholdMs?"OVER":"WITHIN",simulation,residency,record,upload,present_cpu,
                present.acquire_ms,present.fence_wait_ms,measured.command_ms,present.submit_ms,
                ticks,(unsigned long long)clock.debt_ticks());
            std::printf("PERF %.1f KM/H gear %d pad %s | DRAWS %zu track + %zu car + %zu glow | TRIANGLES %llu | far %.2f | visible %zu | unplaced %llu | RESIDENCY %zu | GPU %llu / 268435456 | resources %zu\n",
                speed,int(car.gear),connected?"CONNECTED":"DISCONNECTED - NEUTRAL",frame.track_draws,frame.car_draws,frame.glow_draws,
                (unsigned long long)frame.triangles,frame.far_plane,frame.visible,
                (unsigned long long)frame.unplaced,race.resident_count(),
                (unsigned long long)renderer.gpu_bytes(),frame.commands.resource_table().resources.size());
            std::printf("PERF LOAD %.0f ms initial upload %.0f ms | CPU stream %llu / 50331648 | persistent %llu / 67108864 | CACHE EST %llu / 8388608 | HEAP before %llu after load %llu now %llu load HWM %llu run HWM %llu\n",
                load_ms,initial_upload,(unsigned long long)scene.track().streaming_bytes,(unsigned long long)persistent,
                (unsigned long long)scene.cache_bytes(),
                (unsigned long long)heap.before_load,(unsigned long long)heap.after_load,
                (unsigned long long)heap.current,(unsigned long long)heap.load_peak,(unsigned long long)heap.peak);
            std::printf("PERF HEAP LOAD OBS HWM ABS %llu | delta %llu / 50331648 | LOAD HEAP OVER %llu | HEAP RUN OBS HWM %llu | ARENA NOW %llu | ARENA OBS HWM %llu\n",
                (unsigned long long)heap.load_peak,(unsigned long long)heap.load_delta(),
                (unsigned long long)heap.load_overrun(50331648),(unsigned long long)heap.peak,
                (unsigned long long)heap.extent,(unsigned long long)heap.extent_peak);
            std::vector<std::string> audio_lines;
            append_audio_status(audio_lines,sound,audio_note);
            for (const auto& line : audio_lines) std::printf("%s\n",line.c_str());
            std::fflush(stdout);
            last_perf = {format("Last DIRECT FRAME %.2f ms | SIM %.2f | CULL/RECORD %.2f | UPDATE %.2f",
                frame_ms,simulation,record,upload),
                format("PRESENT CPU %.2f ms | ACQUIRE %.2f | FENCE %.2f | CMD %.2f | SUBMIT %.2f",
                present_cpu,present.acquire_ms,present.fence_wait_ms,measured.command_ms,present.submit_ms)};
            last_perf.insert(last_perf.end(),audio_lines.begin(),audio_lines.end());
            if (finished) printed_final_perf = true;
        }
        }
        if (driving_before && !finished) {
            measured.frame_ms = measured_frame_ms;
            stats.add(measured);
            if (profile_stats) {
                for (unsigned i = 0; i < ticks; ++i)
                    profile_stats->add_simulation_tick(simulation_ticks[i]);
                FrameProfileStats::Values values{};
                const auto set = [&](FrameProfileStage stage, double value) {
                    values[static_cast<std::size_t>(stage)] = value;
                };
                set(FrameProfileStage::simulation,simulation);
                set(FrameProfileStage::residency,residency);
                set(FrameProfileStage::record_total,record);
                set(FrameProfileStage::cull,record_profile.cull_ms);
                set(FrameProfileStage::list,record_profile.list_ms);
                set(FrameProfileStage::car,record_profile.car_ms);
                set(FrameProfileStage::track,record_profile.track_ms);
                set(FrameProfileStage::combine,record_profile.combine_ms);
                set(FrameProfileStage::update_total,upload);
                set(FrameProfileStage::update_validation,update_timing.validation_ms);
                set(FrameProfileStage::uniform_write,present.uniform_write_ms);
                set(FrameProfileStage::present_total,measured.present_cpu_ms);
                set(FrameProfileStage::acquire,present.acquire_ms);
                set(FrameProfileStage::fence,present.fence_wait_ms);
                set(FrameProfileStage::command_record,present.record_ms);
                set(FrameProfileStage::finish_list,present.finish_ms);
                set(FrameProfileStage::submit,present.submit_ms);
                const double present_parts = present.acquire_ms+present.fence_wait_ms+
                    present.uniform_write_ms+present.record_ms+present.finish_ms+present.submit_ms;
                set(FrameProfileStage::present_other,std::max(0.0,present_cpu-present_parts));
                set(FrameProfileStage::mallinfo,mallinfo_ms);
                const double parent_sum = simulation+residency+record+upload+present_cpu+mallinfo_ms;
                set(FrameProfileStage::parent_sum,parent_sum);
                set(FrameProfileStage::other,std::max(0.0,frame_ms-parent_sum));
                const FrameProfileIdentity identity{race.session().timing().step,clock.debt_ticks(),
                    frame.triangles,static_cast<std::uint32_t>(frame.track_draws),
                    static_cast<std::uint32_t>(frame.car_draws)};
                profile_stats->add_frame(values,frame_ms,identity,ticks);
            }
        }
        if (finished) break;
    }
    } catch (const std::exception& e) { failure = e.what(); }
    // Leave the finish cue audible on the result screen. Other exits mute
    // the still-running engine and music before showing that screen.
    if (!failure.empty() || race.session().timing().outcome != RaceOutcome::finished)
        sound.set_suspended(true);
    std::string close_error;
    if (!renderer.close(close_error)) {
        if (!failure.empty()) failure += " | ";
        failure += "renderer close: " + close_error;
    }
    if (!failure.empty()) { std::printf("ERROR: %s\n",failure.c_str()); std::fflush(stdout); }
    if (free_drive && !failure.empty()) throw std::runtime_error(failure);
    if (appletMainLoop() && !free_drive) {
        if (profile_stats) {
            const ProfileResultContext context{*profile_stats,renderer.gpu_profile(),heap,read_clocks(),
                load_ms,initial_upload,scene.track().streaming_bytes,persistent,renderer.gpu_bytes(),
                sizeof(FrameProfileStats)+renderer.profile_storage_bytes(),
                renderer.memory_usage(),renderer.gpfifo_usage(),
                sky == "off" ? "OFF" : sky == "early" ? "EARLY" : "ON",scene_target,
                tuned,particles_enabled,glows,car_shading,wet_road_reflections};
            show_result(screen,pad,race,replay,replay_mode,panel_mode,paused,
                        stats,unpause_resets,last_perf,failure,&context);
        } else show_result(screen,pad,race,replay,replay_mode,panel_mode,paused,
                           stats,unpause_resets,last_perf,failure);
    }
}
} // namespace
int main(int argc, char** argv) {
    bool sockets = false; int link = -1;
    if (__nxlink_host.s_addr && R_SUCCEEDED(socketInitializeDefault())) { sockets = true; link = nxlinkStdio(); }
    padConfigureInput(1,HidNpadStyleSet_NpadStandard); PadState pad; padInitializeDefault(&pad);
    {
        Screen screen; if (!screen.open()) return 1;
        StartupJournal startup_journal;StartupJournal* active_trace=nullptr;
        try {
            // Shaders belong to both frontend and race. Keep this mount
            // alive through menu -> race -> menu and until all renderers close.
            require(R_SUCCEEDED(romfsInit()), "cannot mount application shader/replay RomFS");
            struct RomfsGuard { ~RomfsGuard() { romfsExit(); } } romfs_guard;
            std::string root = "sdmc:/nfsu";
            bool tuned = false;
            bool replay = false, panel = false, profile_frame = false;
            bool particles_enabled = true, selected = false, root_seen = false;
            bool hud = true;
            TextureFilterMode texture_filter=TextureFilterMode::legacy;
            bool hud_trace = false;
            bool car_shading=false;
            std::string sky = "on";
            std::uint32_t scene_height = 0;
            bool gpu_boost = true;
            bool startup_trace=false;
            bool glows = false, glows_seen = false;
            bool wet_road_reflections = false, wet_road_seen = false, car_shading_seen = false;
            bool explicit_performance = false, handling_telemetry = false;
            VehicleUpgradeLevels performance;
            std::uint32_t event_number=1; bool event_seen=false;
            RaceSteering steering=RaceSteering::baseline; bool steering_seen=false;
            std::string car_name = "240SX"; bool car_seen = false;
            for (int i = 1; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--replay") { replay = true; selected = true; }
                else if (arg == "--live") { replay = false; selected = true; }
                else if (arg == "--tuned") tuned = true;
                else if (arg == "--handling-telemetry") handling_telemetry = true;
                else if (arg == "--car" && i+1 < argc) {
                    require(!car_seen,"duplicate --car");
                    car_name = argv[++i]; car_seen = true;
                }
                else if (arg == "--steering" && i+1<argc) {
                    require(!steering_seen && parse_race_steering(argv[++i],steering),"--steering needs one baseline, recovered0 or recovered1");
                    steering_seen=true;
                }
                else if (arg == "--performance-levels" && i+1 < argc) {
                    require(!explicit_performance,"duplicate --performance-levels");
                    std::string setup_error;
                    require(parse_performance_levels(argv[++i],performance,setup_error),setup_error);
                    explicit_performance = true;
                }
                else if (arg == "--panel") panel = true;
                else if (arg == "--profile-frame") profile_frame = true;
                else if (arg == "--frontend-startup-trace") startup_trace=true;
                else if (arg == "--hud-trace") hud_trace = true;
                else if (arg == "--event" && i+1<argc) {
                const std::string value=argv[++i];
                require(!event_seen && (value=="1" || value=="8"),"--event needs one 1 or 8");
                event_seen=true; event_number=value=="1" ? 1 : 8;
            }
                else if (arg == "--no-hud") hud = false;
                else if (arg == "--scene-height" && i+1 < argc &&
                         deko_parse_scene_height(argv[i+1],scene_height)) ++i;
                else if (arg == "--gpu-boost" && i+1 < argc &&
                         (std::strcmp(argv[i+1],"on") == 0 ||
                          std::strcmp(argv[i+1],"off") == 0))
                    gpu_boost = std::strcmp(argv[++i],"on") == 0;
                else if (arg == "--particles=on") particles_enabled = true;
                else if (arg == "--particles=off") particles_enabled = false;
                else if (arg=="--texture-filter" && i+1<argc) {
                    require(parse_texture_filter(argv[++i],texture_filter),"--texture-filter needs legacy or linear");
                }
                else if (arg=="--car-shading" && i+1<argc) {
                    const std::string mode=argv[++i];
                    require(mode=="on" || mode=="off","--car-shading needs on or off");
                    car_shading=mode=="on";car_shading_seen=true;
                }
                else if (arg == "--sky" && i+1 < argc &&
                         (std::strcmp(argv[i+1],"on") == 0 || std::strcmp(argv[i+1],"off") == 0 ||
                          std::strcmp(argv[i+1],"early") == 0)) sky = argv[++i];
                else if (arg == "--glows" || arg == "--glows=on" || arg == "--glows=off") {
                    glows = arg != "--glows=off";glows_seen=true;
                }
                else if (arg == "--wet-road-reflections") {
                    if (i + 1 >= argc) throw std::runtime_error("--wet-road-reflections needs on or off");
                    const std::string value = argv[++i];
                    if (value == "on") wet_road_reflections = true;
                    else if (value == "off") wet_road_reflections = false;
                    else throw std::runtime_error("--wet-road-reflections must be on or off");
                    wet_road_seen=true;
                }
                else if (!root_seen && arg.rfind("--",0) != 0) { root = arg; root_seen = true; }
                else throw std::runtime_error("Usage: nfsu_switch_race.nro [sdmc:/nfsu] [--live|--replay] [--event 1|8] [--panel|--profile-frame] [--hud-trace] [--no-hud] [--sky on|off|early] [--scene-height 720|540|450|360] [--gpu-boost on|off] [--particles=on|--particles=off] [--tuned] [--glows|--glows=off] [--car NAME] [--car-shading on|off] [--texture-filter legacy|linear] [--wet-road-reflections on|off]");
            }
            require(event_number == 1 || selected, "--event 8 requires --live");
            require(!(panel && profile_frame),"--profile-frame requires direct presentation");
            require(!startup_trace || !selected,"--frontend-startup-trace requires menu startup");
            if(startup_trace) {
                const auto retail_directory=std::filesystem::path(root).lexically_normal().generic_string();
                require(retail_directory!="sdmc:" && retail_directory!="sdmc:/",
                    "startup journal must stay outside the retail root");
                bool opened=false;char path[96];
                for(unsigned attempt=0;attempt<64;++attempt) {
                    std::snprintf(path,sizeof path,"sdmc:/nfsu-menu-startup-%04u.log",attempt);
                    if(startup_journal.open(path)) {opened=true;break;}
                    if(errno!=EEXIST)break;
                }
                require(opened,startup_journal.error());active_trace=&startup_journal;
                char identity[256];
                std::snprintf(identity,sizeof identity,"baseline=d00d897 diagnostic=r18 requested_height=%lu boost=%s profile=%s journal=%s timing_perturbed=1",
                    (unsigned long)scene_height,gpu_boost?"on":"off",profile_frame?"on":"off",path);
                if(!startup_journal.mark("enabled",identity))throw std::runtime_error(startup_journal.error());
            }
            require(!hud_trace || (hud && !panel),"--hud-trace requires HUD and direct presentation");
            require(!(tuned && car_name!="240SX"),
                    "the --tuned preset is the 240SX's own part set; it cannot dress car type "+car_name);
            nfsu::CarAssets selected_car;
            {
                std::string car_error;
                if (!nfsu::load_car_assets(std::filesystem::path(root)/"Global/GLOBALB.BUN",
                                           car_name,selected_car,car_error))
                    throw std::runtime_error("car selection: "+car_error);
            }
            // Ordinary live/menu driving uses the recovered steering path.
            // Explicit replay keeps the historical baseline simulation pins.
            if (!steering_seen) steering = replay ? RaceSteering::baseline :
                                                   RaceSteering::recovered_predicate1;
            if (!replay) {
                if (!glows_seen) glows=true;
                if (!wet_road_seen) wet_road_reflections=true;
                if (!car_shading_seen) car_shading=true;
            }
            if (selected) {
                run(root,replay,panel,profile_frame,particles_enabled,sky,
                    tuned,glows,car_shading,wet_road_reflections,texture_filter,scene_height,
                    gpu_boost,hud,hud_trace,explicit_performance ? &performance : nullptr,
                    handling_telemetry,steering,selected_car,screen,pad,false,nullptr,event_number);
            } else {
                std::string trace_error;
                const auto trace=[&](const char* stage) {require(trace_frontend_startup(active_trace,stage,trace_error),trace_error);};
                trace("intro.before");
                const auto intro=play_original_intro(root,[&] {
                    if (!appletMainLoop()) return IntroControl::quit;
                    padUpdate(&pad); const auto down=padGetButtonsDown(&pad);
                    if (down&HidNpadButton_Plus) return IntroControl::quit;
                    if (down&(HidNpadButton_A|HidNpadButton_B)) return IntroControl::skip;
                    return IntroControl::continue_playback;
                },[&](const MovieFrame& frame) { screen.movie(frame); });
                trace("intro.after");
                if (intro.outcome==IntroOutcome::quit) throw Cancel{};
                FrontendArt art; std::string frontend_error;
                screen.show({"Loading original frontend..."});
                trace("art.load.before");
                require(art.load(root,frontend_error,false),frontend_error);
                trace("art.load.after");
                FrontendGpuScene frontend_scene;
                trace("scene.load.before");
                require(frontend_scene.load(root,width,height,frontend_error),frontend_error);
                trace("scene.load.after");
                FrontendUiFrame frontend_ui;
                // Match the launch power/scene policy while the menu is live.
                // Restore it after renderer shutdown before handing race its
                // own guard. Declaration order also preserves error unwinding.
                std::unique_ptr<HandheldGpuProfile> frontend_gpu_profile;
                std::unique_ptr<DekoRenderer> frontend_renderer;
                FrontendPresentationStats frontend_presentation_stats;
                const auto open_frontend=[&] {
                    frontend_presentation_stats.reset();
                    trace("power.before");
                    frontend_gpu_profile=std::make_unique<HandheldGpuProfile>(gpu_boost);
                    trace("power.after");
                    if(active_trace) {
                        const auto clocks=read_clocks();char detail[256];
                        std::snprintf(detail,sizeof detail,"read_only=1 configuration_ok=%u configuration=%lu cpu_ok=%u cpu_hz=%lu gpu_ok=%u gpu_hz=%lu emc_ok=%u emc_hz=%lu",
                            (unsigned)clocks.configuration_ok,(unsigned long)clocks.configuration,(unsigned)clocks.cpu_bus_ok,(unsigned long)clocks.cpu_bus_hz,
                            (unsigned)clocks.gpu_ok,(unsigned long)clocks.gpu_hz,(unsigned)clocks.emc_ok,(unsigned long)clocks.emc_hz);
                        if(!active_trace->mark("clocks.actual",detail))throw std::runtime_error(active_trace->error());
                    }
                    const bool menu_docked=appletGetOperationMode()==AppletOperationMode_Console;
                    const auto menu_height=deko_effective_scene_height(scene_height,menu_docked,true);
                    std::printf("MENU POLICY gpu_boost=%s requested_height=%lu effective_height=%lu (configuration, not measured clocks)\n",
                        gpu_boost?"on":"off",static_cast<unsigned long>(scene_height),static_cast<unsigned long>(menu_height));
                    trace("screen.close.before");screen.close();trace("screen.close.after");
                    frontend_renderer=std::make_unique<DekoRenderer>();
                    trace("renderer.open.before");
                    require(frontend_renderer->open_frontend(frontend_scene.commands(),art.atlas(),frontend_error,menu_height,profile_frame,active_trace),frontend_error);
                    trace("renderer.ready");
                };
                open_frontend();
                FrontendClock frontend_clock;
                FrontendAppletClockHook frontend_clock_hook(frontend_clock,frontend_presentation_stats);
                FrontendMenu menu;
                audio::FrontendMusic music;
                trace("music.start.before");
                if(!music.start(root,frontend_error)) menu.notice("MUSIC: "+frontend_error);
                trace("music.start.after");
                if (intro.outcome==IntroOutcome::error) {
                    std::printf("INTRO ERROR: %s\n",intro.error.c_str());
                    menu.notice(std::string("INTRO: ")+intro.error);
                }
                bool stick_up=false,stick_down=false;
                // Free driving drives the Eddie Skyline the menu displays,
                // resolved once and reused across free-driving re-entries.
                // An explicit --car/--tuned diagnostic selection keeps that
                // car instead of the preset.
                const bool free_drive_diagnostic = car_seen || tuned;
                nfsu::FreeDriveSkyline free_drive_skyline;
                bool free_drive_resolved = false;
                FrontendAction pending=FrontendAction::none;
                while (appletMainLoop()) {
                    if(appletGetFocusState()!=AppletFocusState_InFocus) {frontend_clock.resume();frontend_presentation_stats.reset();continue;}
                    const auto poll_begin=armGetSystemTick();
                    padUpdate(&pad); const auto down=padGetButtonsDown(&pad);
                    const auto stick=padGetStickPos(&pad,0);
                    const bool up=stick.y>16000, below=stick.y<-16000;
                    FrontendInput input;
                    input.up=(down&HidNpadButton_Up) || (up&&!stick_up);
                    input.down=(down&HidNpadButton_Down) || (below&&!stick_down);
                    input.accept=down&HidNpadButton_A;
                    input.back=down&(HidNpadButton_B|HidNpadButton_Plus);
                    stick_up=up; stick_down=below;
                    const auto previous_selection=menu.selected();
                    auto action=pending==FrontendAction::none?menu.update(input):FrontendAction::none;
                    const auto selection_done=armGetSystemTick();
                    if(pending==FrontendAction::none && input.back) music.sound(audio::FrontendSound::back);
                    else if(pending==FrontendAction::none && input.accept) music.sound(audio::FrontendSound::select);
                    else if(menu.selected()!=previous_selection)
                        music.sound(input.up?audio::FrontendSound::up:audio::FrontendSound::down);
                    if(action==FrontendAction::exit || action==FrontendAction::free_drive) {
                        pending=action;music.begin_stop();action=FrontendAction::none;
                    }
                    if(pending!=FrontendAction::none && music.stop_ready()) {
                        action=pending;pending=FrontendAction::none;
                    }
                    if (action==FrontendAction::exit) {music.stop();break;}
                    if (action==FrontendAction::underground) {
                        menu.notice("INTRODUCTORY RACE IS NOT AVAILABLE IN THIS BUILD");
                    } else if (action==FrontendAction::free_drive) {
                        music.stop();
                        require(frontend_renderer->close(frontend_error),frontend_error);
                        frontend_renderer.reset();
                        frontend_gpu_profile.reset();
                        try {
                            if (!free_drive_diagnostic && !free_drive_resolved) {
                                std::string dress_error;
                                if (!resolve_free_drive_skyline(root,free_drive_skyline,dress_error))
                                    throw std::runtime_error(dress_error);
                                free_drive_resolved = true;
                            }
                            // The Eddie dress carries the menu's decoded
                            // GLOBALB materials; free driving renders them,
                            // so the driven car keeps its authored paint
                            // response. Explicit --car/--tuned diagnostics
                            // keep the --car-shading argument's control.
                            run(root,false,false,false,particles_enabled,sky,
                                tuned,glows,car_shading||(free_drive_resolved&&!car_shading_seen),wet_road_reflections,texture_filter,scene_height,
                                gpu_boost,hud,false,explicit_performance ? &performance : nullptr,
                                handling_telemetry,steering,
                                free_drive_resolved ? free_drive_skyline.car : selected_car,
                                screen,pad,true,
                                free_drive_resolved ? &free_drive_skyline.dress : nullptr);
                            menu.notice("");
                        } catch (const Cancel&) {
                            menu.notice("LOADING CANCELLED");
                        } catch (const std::exception& error) {
                            std::printf("FREE DRIVE ERROR: %s\n",error.what());
                            menu.notice(std::string("FREE DRIVING: ")+error.what());
                        }
                        if(!music.start(root,frontend_error)) menu.notice("MUSIC: "+frontend_error);
                        else menu.announce_song();
                        stick_up=stick_down=false;
                        open_frontend();
                        frontend_clock.resume();
                    }
                    const auto music_error=music.error();
                    if(!music_error.empty()) { music.stop(); menu.notice("MUSIC: "+music_error); }
                    const auto song=music.now_playing();menu.song(song.artist,song.title,song.album);
                    const auto scene_begin=armGetSystemTick();
                    const auto elapsed_ns=frontend_clock.sample(armTicksToNs(scene_begin));
                    const auto original_ticks=frontend_clock.original_ticks();
                    trace("frame.scene.before");
                    require(frontend_scene.frame(static_cast<float>(elapsed_ns/1000000000.0),original_ticks,frontend_error),frontend_error);
                    trace("frame.scene.after");
                    const auto scene_done=armGetSystemTick();
                    art.advance(static_cast<float>(elapsed_ns/1000000000.0));
                    trace("frame.ui.before");art.record_ui(width,height,menu,frontend_ui);trace("frame.ui.after");
                    const auto ui_done=armGetSystemTick();
                    DekoUpdateTiming update_timing;
                    trace("frame.update.before");
                    require(frontend_renderer->update(frontend_scene.commands(),frontend_error,&update_timing),frontend_error);
                    trace("frame.update.after");
                    DekoPresentTiming present_timing;
                    require(frontend_renderer->present_frontend(frontend_ui,frontend_error,&present_timing),frontend_error);
                    trace("frame.present.returned");
                    if(active_trace) {
                        if(!startup_journal.close())throw std::runtime_error(startup_journal.error());
                        active_trace=nullptr;
                    }
                    const auto presented=armGetSystemTick();
                    FrontendPresentationWindow presentation_window;
                    if(!profile_frame && frontend_presentation_stats.sample(armTicksToNs(presented),presentation_window)) {
                        std::printf("MENU PRESENT measured_intervals=%llu elapsed_ms=%.3f mean_ms=%.3f max_ms=%.3f measured_fps=%.3f (normal presentation return intervals)\n",
                            static_cast<unsigned long long>(presentation_window.intervals),presentation_window.elapsed_ns/1000000.0,
                            presentation_window.mean_ms(),presentation_window.max_ns/1000000.0,presentation_window.fps());
                    }
                    if(menu.selected()!=previous_selection) {
                        std::printf("MENU INPUT selected=%u selection_ms=%.3f scene_ms=%.3f ui_ms=%.3f present_ms=%.3f total_ms=%.3f acquire_ms=%.3f fence_ms=%.3f record_ms=%.3f quads=%zu\n",
                            static_cast<unsigned>(menu.selected()),ms(selection_done-poll_begin),ms(scene_done-scene_begin),
                            ms(ui_done-scene_done),ms(presented-ui_done),ms(presented-poll_begin),
                            present_timing.acquire_ms,present_timing.fence_wait_ms,present_timing.record_ms,frontend_ui.additive.size());
                    }
                }
                require(frontend_renderer->close(frontend_error),frontend_error);
            }
        } catch (const Cancel&) {
            if(active_trace && !startup_journal.close()) {std::fprintf(stderr,"ERROR: %s\n",startup_journal.error());std::fflush(stderr);}
        } catch (const std::exception& e) {
            if(active_trace && !startup_journal.mark("exception.caught",e.what())) {
                std::fprintf(stderr,"ERROR: %s\n",startup_journal.error());std::fflush(stderr);
            }
            if(active_trace && !startup_journal.close()) {std::fprintf(stderr,"ERROR: %s\n",startup_journal.error());std::fflush(stderr);}
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
