// Event 1 on the pad, with the desktop fixed-step simulation and GPU scene.
#include "deko_renderer.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/heap_usage.h"
#include "nfsu/frame_profile_stats.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/race_frame_stats.h"
#include "nfsu/screen_text.h"
#include "nfsu/stock_view.h"
#include "nfsu/race_audio.h"
#include <switch.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <malloc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
using namespace nfsu;
constexpr unsigned width = 1280, height = 720;
double ms(std::uint64_t ticks) { return armTicksToNs(ticks)/1000000.0; }
class SwitchTiming final : public TimingService {
public:
    std::uint64_t now_us() override { return armTicksToNs(armGetSystemTick())/1000; }
};
struct ClockReadings {
    std::uint32_t cpu_bus_hz = 0, gpu_hz = 0, emc_hz = 0;
    bool cpu_bus_ok = false, gpu_ok = false, emc_ok = false;
};
ClockReadings read_clocks() {
    ClockReadings out;
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
std::string profile_percentile(const ProfileDistribution& stats) {
    const auto bin = stats.percentile_bin(95);
    return bin == ProfileDistribution::kOverflowBin ? ">=100" :
        format("%.2f",(bin+1)*ProfileDistribution::kBinWidthMs);
}
std::string profile_row(const char* label, const ProfileDistribution& stats) {
    if (!stats.count()) return format("%-16s n0 unavailable",label);
    const auto p95 = profile_percentile(stats);
    return format("%-16s n%llu mean %.2f p95 %s max %.2f ms",label,
        (unsigned long long)stats.count(),stats.mean(),p95.c_str(),stats.max());
}
struct ProfileResultContext {
    const FrameProfileStats& cpu;
    const DekoGpuProfile& gpu;
    const HeapUsage& heap;
    ClockReadings clocks;
    double load_ms = 0, upload_ms = 0;
    std::uint64_t stream_bytes = 0, persistent_bytes = 0, gpu_bytes = 0;
    std::uint64_t storage_bytes = 0;
    DekoMemoryUsage memory;
    DekoGpfifoUsage gpfifo;
};
using ProfilePages = std::vector<std::vector<std::string>>;
ProfilePages profile_pages(const RaceRun& race, const RaceReplay& replay,
                           bool replay_mode, const RaceFrameStats& frames,
                           unsigned unpause_resets, const ProfileResultContext& context,
                           const std::string& failure) {
    ProfilePages pages;
    std::vector<std::string> titles;
    auto add_page = [&](const char* title, std::vector<std::string> rows) {
        if (rows.size() > 8) throw std::runtime_error("profile page exceeds eight data rows");
        for (const auto& row : rows) if (row.size() > 80)
            throw std::runtime_error("profile page exceeds 80 columns");
        titles.emplace_back(title);
        pages.push_back(std::move(rows));
    };
    const auto mode = appletGetOperationMode() == AppletOperationMode_Console ? "DOCKED" : "HANDHELD";
    const auto& clocks = context.clocks;
    add_page("IDENTITY", {
        format("BUILD TAG %.40s",NFSU_RACE_BUILD_TAG),
        std::strlen(NFSU_RACE_BUILD_TAG) >= 40 ? "FULL COMMIT: build tag above" :
            "FULL COMMIT: coordinator build record",
        "NRO SHA-256: coordinator build record (not derivable in NRO)",
        format("%s | %s DIRECT | PROFILE ONE SCENE PAIR",mode,replay_mode?"REPLAY":"LIVE"),
        format("TOOLCHAIN GCC %s | deko3d version unavailable",__VERSION__),
        clocks.cpu_bus_ok ? format("CPU clock %.1f MHz",clocks.cpu_bus_hz/1e6) :
            "CPU clock unavailable",
        clocks.gpu_ok ? format("GPU clock %.1f MHz",clocks.gpu_hz/1e6) : "GPU clock unavailable",
        clocks.emc_ok ? format("MEM/EMC clock %.1f MHz",clocks.emc_hz/1e6) : "MEM/EMC clock unavailable"
    });
    const bool finished = race.session().timing().outcome == RaceOutcome::finished;
    const char* verdict = !failure.empty() || (finished && !(replay.exhausted() && race.matches_event1_replay()))
        ? "DIFFERENT" : finished ? "EQUAL" : "RUNNING";
    std::vector<std::string> replay_rows = {
        format("%s | REPLAY %s | LAST STEP %llu",race_outcome_name(race.session().timing().outcome),
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
    add_page("REPLAY",std::move(replay_rows));
    if (frames.count()) {
        const auto mean = frames.mean();
        const auto worst = context.cpu.worst();
        add_page("ACTIVE FRAMES",{
            format("FRAMES %llu | excludes countdown/pause/resume/finish | resets %u",
                (unsigned long long)frames.count(),unpause_resets),
            format("MEAN %.2f | P50 %s | P95 %s | P99 %s ms",mean.frame_ms,
                percentile_text(frames,50).c_str(),percentile_text(frames,95).c_str(),
                percentile_text(frames,99).c_str()),
            format("MAX %.2f ms | OVER 25.00 %llu/%llu (%.2f%%)",frames.max_ms(),
                (unsigned long long)frames.over_budget(),(unsigned long long)frames.count(),
                100.0*frames.over_budget()/frames.count()),
            format("WORST TICK %llu | TRACK/CAR DRAWS %u/%u",
                (unsigned long long)worst.tick,worst.track_draws,worst.car_draws),
            format("WORST TRIANGLES %llu | DEBT %llu ticks",
                (unsigned long long)worst.triangles,(unsigned long long)worst.debt),
            format("CATCH-UP MAX %u | DEBT MAX %llu ticks",context.cpu.catch_up_max(),
                (unsigned long long)context.cpu.debt_max()),
            "OVER means >25.00 ms (1.5 display periods); percentile bins 0.25 ms"
        });
    } else add_page("ACTIVE FRAMES",{"No active driving frames measured"});
    const auto& cpu = context.cpu;
    auto stage = [&](const char* label, FrameProfileStage id) { return profile_row(label,cpu.stage(id)); };
    add_page("CPU A",{
        stage("SIM/frame",FrameProfileStage::simulation),
        stage("SIM/tick",FrameProfileStage::simulation_tick),
        stage("RES/HASH/LOG",FrameProfileStage::residency),
        stage("CULL/REC parent",FrameProfileStage::record_total),
        stage("CULL+CAMERA",FrameProfileStage::cull),
        stage("LIST",FrameProfileStage::list),
        stage("CAR",FrameProfileStage::car)
    });
    add_page("CPU B",{
        stage("TRACK META",FrameProfileStage::track),
        stage("COMBINE/CHECK",FrameProfileStage::combine),
        stage("UPDATE parent",FrameProfileStage::update_total),
        stage("UPDATE VALID",FrameProfileStage::update_validation),
        stage("UNIFORM WRITE",FrameProfileStage::uniform_write),
        stage("PRESENT parent",FrameProfileStage::present_total),
        stage("ACQUIRE",FrameProfileStage::acquire),
        stage("FENCE WAIT",FrameProfileStage::fence)
    });
    add_page("CPU C",{
        stage("CMD RECORD",FrameProfileStage::command_record),
        stage("FINISH LIST",FrameProfileStage::finish_list),
        stage("SUBMIT/FLUSH",FrameProfileStage::submit),
        stage("PRESENT other",FrameProfileStage::present_other),
        stage("MALLINFO",FrameProfileStage::mallinfo),
        stage("PARENT SUM",FrameProfileStage::parent_sum),
        stage("OTHER",FrameProfileStage::other)
    });
    const auto& gpu = context.gpu;
    add_page("GPU SCENE",{
        format("ONE SCENE PAIR | completed %llu | invalid %llu | dropped %llu",
            (unsigned long long)gpu.scene.count(),(unsigned long long)gpu.invalid,
            (unsigned long long)gpu.dropped),
        profile_row("SCENE",gpu.scene),
        format("WINDOW %llu PAIRS | TICKS %llu-%llu (%llu) | RESUMES %u",
            (unsigned long long)gpu.scene.count(),
            (unsigned long long)gpu.first_tick,(unsigned long long)gpu.last_tick,
            (unsigned long long)gpu.span_ticks(),unpause_resets),
        format("WORST GPU TICK %llu | TRACK/CAR DRAWS %u/%u",
            (unsigned long long)gpu.worst_tick,gpu.worst_track_draws,gpu.worst_car_draws),
        "Pipeline-top begin / timestamp end; elapsed span, not utilization",
        "Query after fence; final pair drained; report flush cost included",
        format("GPFIFO bound last/max %u/%u of %u | track lists %u",
            context.gpfifo.last_entries,context.gpfifo.max_entries,
            kDekoGpfifoRingEntries,context.gpfifo.last_lists),
        format("QUERY/STAT STORAGE %llu bytes",(unsigned long long)context.storage_bytes)
    });
    const auto& heap = context.heap;
    add_page("LOAD/MEMORY",{
        format("LOAD %.0f ms | initial upload %.0f ms",context.load_ms,context.upload_ms),
        format("CPU STREAM %llu / 50331648 | PERSISTENT %llu / 67108864",
            (unsigned long long)context.stream_bytes,(unsigned long long)context.persistent_bytes),
        format("GPU BLOCKS %llu / 268435456 bytes",(unsigned long long)context.gpu_bytes),
        format("FRAME UNIFORMS %llu bytes",(unsigned long long)context.memory.frame_uniform_bytes),
        format("STATIC UNIFORMS %llu bytes",(unsigned long long)context.memory.static_uniform_bytes),
        format("PREPARED COMMANDS %llu bytes",(unsigned long long)context.memory.prepared_command_bytes),
        format("LOAD HEAP OBS HWM %llu | over %llu bytes",(unsigned long long)heap.load_peak,
            (unsigned long long)heap.load_overrun(50331648))
    });
    if (!failure.empty()) {
        const auto wrapped = wrap_screen_text(failure,80);
        for (std::size_t i = 0; i < wrapped.size(); i += 8) {
            std::vector<std::string> rows;
            rows.push_back("ERROR - NO SUCCESS RESULT");
            const auto end = std::min<std::size_t>(i+7,wrapped.size());
            rows.insert(rows.end(),wrapped.begin()+i,wrapped.begin()+end);
            add_page("ERROR",std::move(rows));
        }
    }
    for (std::size_t i = 0; i < pages.size(); ++i) {
        auto& rows = pages[i];
        rows.insert(rows.begin(),format("BUILD %.12s | %s | %s | PAGE %zu/%zu",
            NFSU_RACE_BUILD_TAG,mode,titles[i].c_str(),i+1,pages.size()));
        rows.push_back("D-pad LEFT/RIGHT changes page | + exits");
    }
    return pages;
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
        const auto pages = profile_pages(race,replay,replay_mode,stats,unpause_resets,*profile,failure);
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
    std::vector<std::string> lines = {format("NFSU EVENT 1 RESULT | BUILD %.12s | %s %s",
        NFSU_RACE_BUILD_TAG, replay_mode ? "REPLAY" : "LIVE", panel_mode ? "PANEL" : "DIRECT")};
    lines.push_back(format("OUTCOME %s%s | step %llu", race_outcome_name(race.session().timing().outcome),
        paused && !finished ? " (PAUSED)" : "",
        (unsigned long long)race.session().timing().step));
    if (!failure.empty()) {
        auto wrapped = wrap_screen_text(failure, 105);
        lines.push_back("ERROR - NO SUCCESS RESULT");
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if (replay_mode) {
        const char* verdict = !failure.empty() || (finished && !(replay.exhausted() && race.matches_event1_replay()))
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
void run(const std::string& root, bool replay_mode, bool panel_mode, bool profile_frame,
         Screen& screen, PadState& pad) {
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
            });
        if (!audio_started && audio_note.empty())
            audio_note = "audio disabled: startup failed without diagnostic";
    } else audio_note = "audio disabled: render core pin failed";
    std::printf("AUDIO: %s\n", audio_note.c_str());
    const auto persistent = scene.car_bytes()+race.persistent_bytes()+scene.track().contacts.memory.resident_bytes+scene.cache_bytes()+sound.resident_bytes();
    require(persistent <= kPersistentBudgetPlaceholder, "race persistent payload exceeds 64 MiB");
    RaceGpuFrame frame; require(scene.frame(race.session(),frame,error),error);
    const double load_ms = ms(armGetSystemTick()-load_start);
    const auto after = mallinfo(); heap.finish_load(after.uordblks,after.arena);
    screen.show({"ASSETS PASS. Uploading track and car to deko3d..."});
    const auto upload_start = armGetSystemTick();
    if (!panel_mode) screen.close();
    DekoRenderer renderer;
    require(renderer.open(frame.commands,error,!panel_mode,kTrackSliceDrawLimit,profile_frame,true),error);
    const double initial_upload = ms(armGetSystemTick()-upload_start);
    const auto clocks = profile_frame ? read_clocks() : ClockReadings{};
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
    unsigned unpause_resets = 0;
    std::string failure;
    try {
    while (appletMainLoop()) {
        const auto begin = armGetSystemTick();
        const auto elapsed = armTicksToNs(begin-previous)/1000; previous = begin;
        padUpdate(&pad); const auto down = padGetButtonsDown(&pad);
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
        sound.set_suspended(paused || !running);
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
            auto stage = armGetSystemTick();
            require(race.simulate(input,error),error);
            const double tick_ms = ms(armGetSystemTick()-stage);
            simulation += tick_ms; simulation_ticks[ticks] = tick_ms;
            stage = armGetSystemTick(); require(race.commit(error),error);
            sound.on_tick(audio::race_audio_snapshot(race.session()));
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
        if (finished) sound.set_suspended(true);
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
                replay.exhausted() && race.matches_event1_replay() ? "EQUAL" : "DIFFERENT");
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
            auto hud = race_session_hud(race.session()); hud.resize(3);
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
            lines.push_back(format("DRAWS %zu track + %zu car | TRIANGLES %llu | far %.2f | visible %zu | unplaced %llu",
                frame.track_draws,frame.car_draws,(unsigned long long)frame.triangles,frame.far_plane,frame.visible,(unsigned long long)frame.unplaced));
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
            lines.push_back(format("HEAP LOAD OBS HWM %llu | delta %llu / 50331648 | LOAD HEAP OVER %llu",
                (unsigned long long)heap.load_peak,(unsigned long long)heap.load_delta(),(unsigned long long)heap.load_overrun(50331648)));
            lines.push_back(format("HEAP RUN OBS HWM %llu | ARENA NOW %llu | ARENA OBS HWM %llu (sampled)",
                (unsigned long long)heap.peak,(unsigned long long)heap.extent,(unsigned long long)heap.extent_peak));
            if (replay_mode) {
                const char* verdict = finished ? (replay.exhausted() && race.matches_event1_replay()?"EQUAL":"DIFFERENT") : "RUNNING";
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
        require(renderer.present(error,&present,profile_frame ? &scene_frame : nullptr),error);
        present_cpu = ms(armGetSystemTick()-present_start);
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
            auto hud = race_session_hud(race.session()); hud.resize(3);
            std::printf("PERF BUILD %.12s | %s %s | frame %llu step %llu | %s | %s | %s\n",
                NFSU_RACE_BUILD_TAG,replay_mode?"REPLAY":"LIVE",paused?"PAUSED":finished?"FINISHED":"RUNNING",
                (unsigned long long)presented,(unsigned long long)race.session().timing().step,
                hud[0].c_str(),hud[1].c_str(),hud[2].c_str());
            std::printf("PERF FRAME %.2f ms (>25 ms %s) | SIM %.2f | RES/HASH/LOG %.2f | CULL/RECORD %.2f | UPDATE %.2f | PRESENT CPU %.2f (ACQUIRE %.2f FENCE %.2f CMD %.2f SUBMIT %.2f) | CATCH-UP %u DEBT %llu\n",
                frame_ms,frame_ms>RaceFrameStats::kOverThresholdMs?"OVER":"WITHIN",simulation,residency,record,upload,present_cpu,
                present.acquire_ms,present.fence_wait_ms,measured.command_ms,present.submit_ms,
                ticks,(unsigned long long)clock.debt_ticks());
            std::printf("PERF %.1f KM/H gear %d pad %s | DRAWS %zu track + %zu car | TRIANGLES %llu | far %.2f | visible %zu | unplaced %llu | RESIDENCY %zu | GPU %llu / 268435456 | resources %zu\n",
                speed,int(car.gear),connected?"CONNECTED":"DISCONNECTED - NEUTRAL",frame.track_draws,frame.car_draws,
                (unsigned long long)frame.triangles,frame.far_plane,frame.visible,
                (unsigned long long)frame.unplaced,race.resident_count(),
                (unsigned long long)renderer.gpu_bytes(),frame.commands.resource_table().resources.size());
            std::printf("PERF LOAD %.0f ms initial upload %.0f ms | CPU stream %llu / 50331648 | persistent %llu / 67108864 | CACHE EST %llu / 8388608 | HEAP before %llu after load %llu now %llu load HWM %llu run HWM %llu\n",
                load_ms,initial_upload,(unsigned long long)scene.track().streaming_bytes,(unsigned long long)persistent,
                (unsigned long long)scene.cache_bytes(),
                (unsigned long long)heap.before_load,(unsigned long long)heap.after_load,
                (unsigned long long)heap.current,(unsigned long long)heap.load_peak,(unsigned long long)heap.peak);
            std::printf("PERF HEAP LOAD OBS HWM %llu | delta %llu / 50331648 | LOAD HEAP OVER %llu | HEAP RUN OBS HWM %llu | ARENA NOW %llu | ARENA OBS HWM %llu\n",
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
    // Every way out of the race loop (+, an error or the finish) mutes the
    // audio with the service's fade before the result screen; before this
    // only the finish did, so + left the engine and music playing.
    sound.set_suspended(true);
    std::string close_error;
    if (!renderer.close(close_error)) {
        if (!failure.empty()) failure += " | ";
        failure += "renderer close: " + close_error;
    }
    if (!failure.empty()) { std::printf("ERROR: %s\n",failure.c_str()); std::fflush(stdout); }
    if (appletMainLoop()) {
        if (profile_stats) {
            const ProfileResultContext context{*profile_stats,renderer.gpu_profile(),heap,clocks,
                load_ms,initial_upload,scene.track().streaming_bytes,persistent,renderer.gpu_bytes(),
                sizeof(FrameProfileStats)+renderer.profile_storage_bytes(),
                renderer.memory_usage(),renderer.gpfifo_usage()};
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
        try {
            std::string root = "sdmc:/nfsu";
            bool replay = false, panel = false, profile_frame = false, selected = false, root_seen = false;
            for (int i = 1; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--replay") { replay = true; selected = true; }
                else if (arg == "--live") { replay = false; selected = true; }
                else if (arg == "--panel") panel = true;
                else if (arg == "--profile-frame") profile_frame = true;
                else if (!root_seen && arg.rfind("--",0) != 0) { root = arg; root_seen = true; }
                else throw std::runtime_error("Usage: nfsu_switch_race.nro [sdmc:/nfsu] [--live|--replay] [--panel|--profile-frame]");
            }
            require(!(panel && profile_frame),"--profile-frame requires direct presentation");
            while (!selected && appletMainLoop()) {
                padUpdate(&pad); const auto down = padGetButtonsDown(&pad);
                if (down & HidNpadButton_Plus) throw Cancel{};
                if (down & HidNpadButton_A) selected = true;
                if (down & HidNpadButton_X) { selected = true; replay = true; }
                if ((down & HidNpadButton_Y) && !profile_frame) panel = !panel;
                screen.show({"NFSU #196 - EVENT 1 / STOCK 240SX / TWO LAPS",format("BUILD %.12s",NFSU_RACE_BUILD_TAG),
                    "A: drive with Joy-Con / Pro Controller", "X: replay the pinned desktop event1.txt", "+: exit",
                    profile_frame ? "PROFILE FRAME: direct only; Y panel toggle disabled" :
                        std::string("Y: diagnostic panel ")+(panel?"ON":"OFF")+" (readback and CPU text cost)",
                    "The #47 first-lap check runs before loading. Assets: "+root,
                    "Release all controls during countdown to clear the #59 hot-plug latch.",
                    "Direct mode has no in-race HUD yet; choose PANEL for live driving evidence.",
                    "During the race: Y pauses, + opens results with driving-frame statistics.",
                    "No game data is bundled or written. Replay input is bundled in RomFS."});
            }
            if (selected) run(root,replay,panel,profile_frame,screen,pad);
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
