#include "nfsu/race_result.h"
#include "nfsu/frame_spike_trace.h"
#include "nfsu/race_debug_input.h"
#include "nfsu/career.h"
#include "nfsu/career_ride.h"
#include "nfsu/campaign_sweep.h"
#include "nfsu/campaign_finish_save.h"
#include "nfsu/career_garage.h"
// Event 1 on the pad, with the desktop fixed-step simulation and GPU scene.
#include "deko_renderer.h"
#include "lan_menu.h"
#include "nfsu/lan_link.h"
#include "nfsu/ab_route.h"
#include "profile_pages.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/intro_grid.h"
#include "nfsu/race_events.h"
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
#include "drag_shift_trace.h"
#include "race_input.h"
#include "drag_input.h"
#include <switch.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <malloc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <atomic>
#include <stdexcept>

namespace {
const std::filesystem::path presentation_path="sdmc:/switch/nfsu-saves/presentation.txt";

nfsu::RacePresentationOptions read_presentation_options(bool road_default) {
    nfsu::RacePresentationOptions options;
    options.road_reflections=road_default;
    for(const auto& path:{presentation_path,std::filesystem::path(presentation_path.string()+".bak")}) {
        std::ifstream file(path);
        unsigned version,blur,car,road,metric;
        std::string extra;
        if(file>>version>>blur>>car>>road>>metric && !(file>>extra) &&
           version==1 && blur<=1 && car<=1 && road<=1 && metric<=1)
            return {blur!=0,car!=0,road!=0,metric!=0};
    }
    return options;
}

bool write_presentation_options(const nfsu::RacePresentationOptions& options) {
    std::error_code error;
    std::filesystem::create_directories(presentation_path.parent_path(),error);
    if(error)return false;
    auto pending=presentation_path;
    pending+=".tmp";
    std::ofstream file(pending,std::ios::trunc);
    file<<"1 "<<options.motion_blur<<' '<<options.low_car_reflections<<' '
        <<options.road_reflections<<' '<<options.metric_speed<<'\n';
    file.close();
    if(!file)return false;
    if(std::filesystem::exists(presentation_path,error)) {
        const auto backup=presentation_path.string()+".bak";
        std::filesystem::remove(backup,error);
        if(error)return false;
        std::filesystem::rename(presentation_path,backup,error);
    }
    if(error)return false;
    std::filesystem::rename(pending,presentation_path,error);
    return !error;
}

// Original 6FA7F0 first-visit groups. The package-selected tuner
// requests at 50A84E use the active performance category instead.
int career_garage_speech(const nfsu::CareerGarage& garage) {
    using nfsu::GaragePage;
    switch(garage.page()) {
    case GaragePage::root:return 67;
    case GaragePage::body:return 63;
    case GaragePage::accessories:return 62;
    case GaragePage::performance:return 75;
    case GaragePage::packages:{
        constexpr int speech[]={88,79,70,83,73,69,82,80,64};
        return speech[garage.performance_field()];
    }
    case GaragePage::rims:return 77;
    case GaragePage::paint:return 74;
    case GaragePage::vinyl_layers:return 84;
    case GaragePage::decals_zones:return 68;
    case GaragePage::trade_in:return 81;
    case GaragePage::parts:
        // The port opens the part browser directly after selecting a body slot.
        switch(garage.slot()) {
        case 1:case 2:case 6:case 7:case 8:case 10:case 11:return 85;
        default:return -1;
        }
    default:return -1;
    }
}

bool ab_record=false,ab_replay=false,ab_minimal=false;

std::string selected_car_label = "STOCK";
const char* car_shading_label="OFF";
using namespace nfsu;
// One bounded CPU asset job; the main thread keeps presenting and owns all GPU work.
class GarageSceneLoad {
public:
    ~GarageSceneLoad(){join();}
    bool running()const{return running_;}
    bool ready()const{return running_&&ready_.load(std::memory_order_acquire);}
    void join(){if(running_){threadWaitForExit(&thread_);threadClose(&thread_);running_=false;}environment_.reset();car_source_.reset();}
    bool start(std::filesystem::path root,unsigned width,unsigned height,
               std::vector<CareerRideSetup> variants,std::string signature,
               std::string installed,std::string car,std::vector<unsigned> indices,
               std::shared_ptr<const FrontendSceneEnvironment> environment,
               std::shared_ptr<const FrontendSceneCarSource> car_source,std::string& error) {
        if(running_){error="garage asset job already running";return false;}
        root_=std::move(root);width_=width;height_=height;variants_=std::move(variants);
        signature_=std::move(signature);installed_=std::move(installed);car_=std::move(car);indices_=std::move(indices);
        environment_=std::move(environment);car_source_=std::move(car_source);
        error_.clear();scene_.reset();ready_=false;
        // Rendering must not starve asset preparation on the process default core.
        // Audio output retains its higher0x28 priority on the other application core.
        const auto render_core=svcGetCurrentProcessorNumber();
        const int asset_core=render_core==2?1:2;
        started_=armGetSystemTick();
        auto result=threadCreate(&thread_,entry,this,nullptr,1024*1024,0x30,asset_core);
        if(R_FAILED(result)){error="garage asset thread create: "+std::to_string(result);return false;}
        const auto variant_count=variants_.size();
        result=threadStart(&thread_);
        if(R_FAILED(result)){threadClose(&thread_);error="garage asset thread start: "+std::to_string(result);return false;}
        running_=true;
        std::printf("GARAGE_JOB started variants=%lu core=%d priority=48\n",(unsigned long)variant_count,asset_core);
        return true;
    }
    const std::string& signature()const{return signature_;}
    std::string installed_,car_,error_;
    double prepare_ms_=0;
    std::vector<unsigned> indices_;
    std::unique_ptr<FrontendGpuScene> scene_;
private:
    static void entry(void* raw) {
        auto& self=*static_cast<GarageSceneLoad*>(raw);
        try {
            self.scene_=std::make_unique<FrontendGpuScene>();
            if(!self.scene_->load_garage(self.root_,self.width_,self.height_,self.variants_,self.error_,nullptr,self.environment_,self.car_source_))self.scene_.reset();
        }catch(const std::exception& e){self.error_=e.what();self.scene_.reset();}
        catch(...){self.error_="garage asset preparation failed";self.scene_.reset();}
        std::vector<CareerRideSetup>().swap(self.variants_);
        self.prepare_ms_=armTicksToNs(armGetSystemTick()-self.started_)/1000000.0;
        self.ready_.store(true,std::memory_order_release);
    }
    Thread thread_{};std::atomic<bool> ready_{false};bool running_=false;
    std::uint64_t started_=0;
    std::filesystem::path root_;unsigned width_=0,height_=0;
    std::vector<CareerRideSetup> variants_;std::string signature_;
    std::shared_ptr<const FrontendSceneEnvironment> environment_;
    std::shared_ptr<const FrontendSceneCarSource> car_source_;
};
struct GarageFpsDisplay {
    RaceDebugInput chord;
    RaceFpsHistory history;
    std::uint64_t previous_present=0;
    bool visible=false;

    void reset() noexcept {history={};previous_present=0;}
};
class FrontendAppletClockHook {
public:
    FrontendAppletClockHook(FrontendClock& clock,FrontendPresentationStats& stats,GarageFpsDisplay& garage_fps)
        :clock_(clock),stats_(stats),garage_fps_(garage_fps) {
        appletHook(&cookie_,[](AppletHookType event,void* state) {
            if(event==AppletHookType_OnResume || event==AppletHookType_OnFocusState) {
                auto& self=*static_cast<FrontendAppletClockHook*>(state);
                self.clock_.resume();self.stats_.reset();self.garage_fps_.reset();
            }
        },this);
    }
    ~FrontendAppletClockHook() {appletUnhook(&cookie_);}
private:
    FrontendClock& clock_;FrontendPresentationStats& stats_;GarageFpsDisplay& garage_fps_;
    AppletHookCookie cookie_{};
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
    std::unique_ptr<RaceLoadingArt> loading_;
    std::uint64_t loading_start_=0;
    Framebuffer fb_{};
    bool opened_ = false;
public:
    RaceLoadingTips tips;
    void begin_loading(const std::filesystem::path& root,unsigned tip=0) {
        // A career load can begin before run() resolves its cars/parts. Reuse
        // that presentation and clock rather than decode the artwork twice.
        if(loading_)return;
        auto art=std::make_unique<RaceLoadingArt>();std::string error;
        if(!art->load(root,error,tip)) {
            if(!tip)throw std::runtime_error(error);
            std::fprintf(stderr,"LOADING tip %u unavailable: %s\n",tip,error.c_str());
            tip=0;require(art->load(root,error),error);
        }
        loading_=std::move(art);loading_start_=armGetSystemTick();
        try {show({});tips.shown(tip);} catch(...) {end_loading();throw;}
    }
    void end_loading() {loading_.reset();}
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
    void startup_splash(const FrontendArt& art) {
        if (!opened_ && !open()) throw std::runtime_error("framebuffer unavailable");
        u32 stride=0; auto* pixels=static_cast<unsigned char*>(framebufferBegin(&fb_,&stride));
        if (!pixels) throw std::runtime_error("framebuffer unavailable");
        art.draw_startup_splash({pixels,width,height,stride});
        framebufferEnd(&fb_);
    }
    void show(const std::vector<std::string>& lines, const std::vector<unsigned char>* rgba = nullptr) {
        if(loading_) {
            if(!opened_ && !open())throw std::runtime_error("framebuffer unavailable");
            u32 stride=0;auto* pixels=static_cast<unsigned char*>(framebufferBegin(&fb_,&stride));
            if(!pixels)throw std::runtime_error("framebuffer unavailable");
            loading_->draw({pixels,width,height,stride},ms(armGetSystemTick()-loading_start_)/1000);
            framebufferEnd(&fb_);return;
        }
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
// The software framebuffer owns loading presentation only while no deko3d
// renderer is live. Clear cached pixels before GPU upload, and on exceptions.
class LoadingScreen {
    Screen& screen_;bool active_;
public:
    LoadingScreen(Screen& screen,const std::filesystem::path& root,bool enabled=true,unsigned tip=0)
        :screen_(screen),active_(enabled) {if(active_)screen_.begin_loading(root,tip);}
    ~LoadingScreen(){finish();}
    void finish(){if(active_){screen_.end_loading();active_=false;}}
    LoadingScreen(const LoadingScreen&)=delete;
    LoadingScreen& operator=(const LoadingScreen&)=delete;
};
class Progress final : public AssetCheckObserver, public TrackSliceLoadObserver {
    Screen& screen_; PadState& pad_; HeapUsage& heap_; bool frontend_;
    std::string file_; std::uint64_t previous_ = 0, last_screen_ = 0, last_log_ = 0;
    std::uint64_t load_started_ = armGetSystemTick();
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
        if (ms(now-last_log_) >= 1000) {
            last_log_=now;
            std::printf("LOAD phase=%s elapsed_ms=%.0f heap_delta=%llu\n",phase,
                ms(now-load_started_),(unsigned long long)heap_.load_delta());
            std::fflush(stdout);
        }
        if (ms(now-last_screen_) >= 100) {
            if (frontend_) {
                screen_.show({"Loading free driving...", "+ cancels loading"});
                last_screen_=armGetSystemTick();
                return;
            }
            screen_.show({std::string("Loading: ")+phase,
                format("HEAP LOAD OBS HWM delta %llu / 75497600 | over %llu",
                    (unsigned long long)heap_.load_delta(), (unsigned long long)heap_.load_overrun(75497600)),
                "HOME serviced; + cancels."});
            // Time spent drawing must not immediately schedule another draw.
            last_screen_=armGetSystemTick();
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
static_assert(kNpadButtonA == HidNpadButton_A && kNpadButtonB == HidNpadButton_B && kNpadButtonX == HidNpadButton_X, "driving face buttons");
static_assert(kNpadButtonL == HidNpadButton_L && kNpadButtonR == HidNpadButton_R &&
              kNpadButtonMinus == HidNpadButton_Minus, "shift/reset mapping");
GamepadFrame sample(const PadState& pad, std::uint64_t tick) {
    GamepadFrame frame; frame.time_us = tick*1000000/60;
    auto& state = frame.pads[0]; state.connected = padIsConnected(&pad);
    state.buttons = switch_race_buttons(padGetButtons(&pad));
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
void print_run_measurement(const char* kind, const RaceFrameStats& stats) {
    if (!stats.count()) { std::printf("RUN_STATS %s samples=0\n",kind); return; }
    const auto p50=percentile_text(stats,50), p95=percentile_text(stats,95),
        p99=percentile_text(stats,99);
    const auto mean=stats.mean();
    std::printf("RUN_STATS %s samples=%llu mean_ms=%.6f p50_ms=%s p95_ms=%s p99_ms=%s max_ms=%.6f over_16_666667=%llu over_25=%llu over_33_333333=%llu histogram_bin_ms=0.25\n",
        kind,(unsigned long long)stats.count(),mean.frame_ms,p50.c_str(),p95.c_str(),p99.c_str(),stats.max_ms(),
        (unsigned long long)stats.over_one_refresh(),(unsigned long long)stats.over_budget(),
        (unsigned long long)stats.over_two_refreshes());
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
    lines.push_back(race.event_number()==0?"+ returns to menu":"+ exits to hbmenu");
    while (appletMainLoop()) {
        screen.show(lines);
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    }
}
// --sky on draws the sky after the opaque world; off omits it; early is the
// issue 227 diagnostic that submits it with the opaque pass, as before.
enum class RaceReturn { menu, restart, intro_won, quit_tournament, restart_tournament, sweep_complete };
RaceReturn run(const std::string& root, bool replay_mode, bool panel_mode, bool profile_frame,
          bool particles_enabled, const std::string& sky, bool tuned, bool glows,
          bool car_shading, const RacePresentationOptions& presentation, TextureFilterMode texture_filter,
          std::uint32_t requested_height, bool gpu_boost, bool hud_mode, bool hud_trace, bool fps_overlay,
          const VehicleUpgradeLevels* performance, bool handling_telemetry, RaceSteering steering,
          const nfsu::CarAssets& selected_car, Screen& screen, PadState& pad, bool free_drive = false,
          const nfsu::RaceCarDress* dress = nullptr, std::uint32_t event_number = 1,
          const FrontendArt* post_art = nullptr, unsigned difficulty=1, unsigned* completed_place=nullptr, unsigned event_prize=0, unsigned previous_style=0, int* completed_style=nullptr, const CareerEventMedia* event_media=nullptr,float visual_reputation=0,const CareerCatalog* career_catalog=nullptr,unsigned previous_high_water=0,unsigned race_index=0,std::array<unsigned,4>* completed_places=nullptr,const CareerTournament* tournament=nullptr,bool nos_self_check=false,const std::string& player_name="YOU",CareerRaceReport* career_report=nullptr,const QuickRaceOptions* quick=nullptr,StartupJournal* sweep=nullptr,bool finish_sweep=false,const CareerProfile* career_profile=nullptr) {
    const bool wet_road_reflections=presentation.road_reflections;
    if(career_report)*career_report={};
    if(completed_place)*completed_place=0;
    if(completed_style)*completed_style=0;
    if(completed_places)*completed_places={};
    SupportedRaceSelection selected_race;
    require(supported_race_selection(event_number,race_index,selected_race),"unsupported selected race round");
    if(quick)selected_race.type=quick_race_type(quick->mode,selected_race.type);
    const bool drift_mode=selected_race.type==RaceType::drift,drag_mode=selected_race.type==RaceType::drag;

    // Boundary-only SD evidence survives a lost nxlink stream. No frame writes.
    StartupJournal flow_journal;bool flow_journal_open=false;
    if(post_art && !free_drive && !quick && !sweep) {
        for(unsigned attempt=0;attempt<10000;++attempt) {
            char path[96];std::snprintf(path,sizeof path,"sdmc:/nfsu-career-flow-%04u.log",attempt);
            if(flow_journal.open(path)){flow_journal_open=true;break;}
            if(errno!=EEXIST)break;
        }
    }
    const auto flow_mark=[&](const char* stage,const char* detail) {
        std::printf("CAREER_FLOW build=%s event=%lu stage=%s %s\n",NFSU_RACE_BUILD_TAG,
            (unsigned long)event_number,stage,detail);std::fflush(stdout);
        if(flow_journal_open && !flow_journal.mark(stage,detail))
            std::fprintf(stderr,"CAREER_FLOW journal: %s\n",flow_journal.error());
    };
    const auto sweep_mark=[&](const char* stage,const char* detail="") {
        if(sweep&&!sweep->mark(stage,detail,false))throw std::runtime_error(sweep->error());
        if(finish_sweep) {
            std::printf("FINISH_SWEEP stage=%s %s\n",stage,detail);std::fflush(stdout);
        }
    };
    char flow_detail[384];
    std::snprintf(flow_detail,sizeof flow_detail,"build=%s event=%lu profile=%u panel=%u free=%u",
        NFSU_RACE_BUILD_TAG,(unsigned long)event_number,unsigned(profile_frame),unsigned(panel_mode),unsigned(free_drive));
    if(post_art)flow_mark("race.enter",flow_detail);
    selected_car_label = (dress?"CUSTOM ":tuned?"TUNED ":"STOCK ") + selected_car.car_type_name;
    car_shading_label=car_shading ? "ON static" : "OFF";
    HandheldGpuProfile gpu_profile(gpu_boost);
    bool docked = appletGetOperationMode() == AppletOperationMode_Console;
    auto scene_height = deko_effective_scene_height(requested_height,docked,!panel_mode);
    DekoSceneTarget scene_target;
    require(plan_deko_scene_target(width,height,!panel_mode,scene_height,scene_target),
            "--scene-height below 720 requires direct presentation (no --panel)");
    std::string error;
    StartupJournal drag_journal;
    const bool drag_trace=event_number==4&&std::strstr(NFSU_RACE_BUILD_TAG,"drag-start-");
    if(drag_trace) {
        bool opened=false;
        for(unsigned attempt=0;attempt<10000;++attempt) {
            char path[96];std::snprintf(path,sizeof path,"sdmc:/nfsu-drag-startup-%04u.log",attempt);
            if(drag_journal.open(path)){opened=true;break;}
            if(errno!=EEXIST)break;
        }
        require(opened,drag_journal.error());
    }
    const auto drag_mark=[&](const char* stage) {
        if(!drag_trace)return;
        const auto memory=mallinfo();char detail[256];
        std::snprintf(detail,sizeof detail,"tag=%s event=%lu car=%s used=%lu arena=%lu",
            NFSU_RACE_BUILD_TAG,(unsigned long)event_number,selected_car.car_type_name.c_str(),
            (unsigned long)memory.uordblks,(unsigned long)memory.arena);
        require(drag_journal.mark(stage,detail),drag_journal.error());
    };
    drag_mark("drag.load.begin");
    sweep_mark("race.loading_art");
    LoadingScreen loading_screen(screen,root,!profile_frame&&!panel_mode&&!replay_mode,
        post_art&&!sweep?screen.tips.current():0U);
    std::unique_ptr<IntroGrid> intro_grid;
    std::vector<IntroGridActor> solo_traffic;
    CarAssets race_car=selected_car;
    RaceCarDress intro_dress;
    if(event_number==0 || (post_art && !free_drive && event_number<=kCareerLastEvent && !drift_mode)) {
        sweep_mark("race.grid");
        require(!free_drive && !replay_mode,"intro requires live race mode");
        screen.show({"Preparing race grid..."});
        intro_grid=std::make_unique<IntroGrid>();
        std::puts("UG0_LOAD grid begin");std::fflush(stdout);
        drag_mark("drag.grid.before");
        require(load_race_grid(root,event_number,selected_car.car_type_name,difficulty,*intro_grid,error,
            performance,dress,race_index,GridPlayerVisuals::load_with_scene,quick),error);
        drag_mark("drag.grid.ready");
        std::puts("UG0_LOAD grid ready");std::fflush(stdout);
        auto human=std::find_if(intro_grid->actors.begin(),intro_grid->actors.end(),[](const auto& a){return a.config.role==Event0Role::human;});
        require(human!=intro_grid->actors.end(),"race grid has no human");
        auto& player=*human;
        race_car=player.assets;performance=&*player.config.performance;
        intro_dress.request=&player.request;intro_dress.skin=&player.skin;
        intro_dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,
            const StockSceneRequest& request,std::string& bind_error,void*) {
            return bind_frontend_runtime_skin(frame,request,bind_error) &&
                   bind_frontend_sponsors(inputs,request.installed_parts,frame,bind_error);
        };
        if(event_number==0||dress)dress=&intro_dress;
        tuned=false;car_shading=true;selected_car_label=event_number==0?"DDAY INTEGRA":race_car.car_type_name;
        std::puts("UG0: recovered rival path choice/driver/selector3; stock setup, ordinary strategy, SAT contacts");
    }
    RaceReplay replay;
    if (replay_mode) {
        require(event_number==1,"replay is pinned to event 1; other circuit events require --live");
        const bool upgraded=performance && !performance_levels_equal(*performance,{});
        require(!upgraded || performance_levels_equal(*performance,performance1_levels()),
                "replay supports zero levels or the documented performance1 setup; use --live for other levels");
        std::ifstream in(upgraded ? "romfs:/performance1.txt" : "romfs:/event1.txt", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        require(bool(in) && replay.load(text,error,upgraded), "bundled replay: "+error);
    }
    const bool ab_enabled=ab_record || ab_replay;
    const bool detailed=profile_frame && !ab_minimal;
    require(!ab_enabled || (!panel_mode && !replay_mode && !handling_telemetry && !hud_trace),
            "AB needs direct live mode without per-tick logs");
    AbRoute ab_route;
    ab_route.context=std::to_string(event_number)+":"+race_car.car_type_name+":"+
        race_steering_name(steering)+":"+std::to_string(free_drive)+":"+
        (performance?performance_levels_text(*performance):"legacy")+":"+
        std::to_string(scene_height)+":"+sky+":"+std::to_string(hud_mode)+":"+
        std::to_string(particles_enabled)+":"+std::to_string(tuned)+":"+std::to_string(glows)+":"+
        std::to_string(car_shading)+":"+std::to_string(wet_road_reflections)+":"+
        std::to_string(static_cast<unsigned>(texture_filter))+":"+std::to_string(dress!=nullptr);
    if(ab_replay) {
        std::ifstream route("sdmc:/switch/nfsu-ab-route.txt",std::ios::binary);
        require(ab_route.load(route,ab_route.context),"AB route missing, malformed or different setup");
    } else if(ab_record) ab_route.frames.reserve(AbRoute::limit);
    std::vector<AbCpuRow> ab_cpu;
    if(ab_enabled)ab_cpu.reserve(AbRoute::limit);
    std::uint64_t ab_previous_present=0;
    bool ab_cancelled=false;
    RaceGpuScene scene;
    require(scene.set_texture_filter(texture_filter), "invalid texture filter policy");
    std::printf("TEXTURE FILTER %s (world/car diffuse; sky/effects unchanged)\n",
                texture_filter == TextureFilterMode::linear ? "linear" : "legacy");
    scene.set_particles_enabled(particles_enabled);
    scene.set_start_scenes_enabled(!free_drive);
    RaceRun race; HeapUsage heap;
    race.set_recovery_trace(std::getenv("NFSU_TRACE_RECOVERY")!=nullptr);
    require(race.set_event(event_number,error,race_index),error);
    require(race.set_crash_camera(kCrashCameraDefaultIntensity,error),error);
    require(race.set_difficulty(difficulty,error),error);
    require(quick?race.set_quick_race(*quick,error):race.set_free_drive(free_drive,error),error);
    require(!replay_mode || steering==RaceSteering::baseline,"recovered steering requires --live; pinned replay uses baseline");
    require(race.set_steering(steering,error),error);
    require(race.set_car(race_car.car_type_name,error),error);
    if(intro_grid) {
        std::vector<RaceRivalSetup> rivals;
        std::array<int,3> contexts{};
        for(const auto& actor:intro_grid->actors)if(actor.config.role==Event0Role::opponent) {
            contexts[rivals.size()]=actor.config.selected_context;
            rivals.push_back({actor.placement_spec,actor.pose,actor.hull,actor.config.event_label,actor.config.visual.car,actor.config.ai_mode});
        }
        const auto& player=*std::find_if(intro_grid->actors.begin(),intro_grid->actors.end(),[](const auto& a){return a.config.role==Event0Role::human;});
        require(race.set_intro_grid(player.pose,player.hull,rivals,contexts,error,player.config.final_grid),error);
        // The live RaceRun owns the shared course. Do not keep a second world.
        intro_grid->course={};
    }else if(quick) {
        require(load_quick_race_traffic(root,selected_race,quick->traffic,solo_traffic,error),error);
    }
    auto& traffic_actors=intro_grid?intro_grid->traffic:solo_traffic;
    if(intro_grid||!traffic_actors.empty()) {
        std::vector<RaceTrafficSetup> traffic_setup;
        for(const auto& car:traffic_actors)
            traffic_setup.push_back({car.placement_spec,car.hull,car.assets.car_type_name,unsigned(car.assets.car_type)});
        require(race.set_intro_traffic(traffic_setup,error),error);
    }
    std::printf("steering %s\n",race_steering_name(steering));
    if (performance) {
        require(race.set_performance_levels(*performance,error),error);
        std::printf("performance-levels %s\n",performance_levels_text(*performance).c_str());
        const auto& items=*performance;
        std::printf("performance-items mass=%u torque-a=%u torque-b=%u torque-c=%u nitrous=%u drive-split=%u tyre-friction=%u grip=%u\n",
            unsigned(items.mass_item),unsigned(items.torque_item_a),unsigned(items.torque_item_b),
            unsigned(items.torque_item_c),unsigned(items.nitrous_item),unsigned(items.drive_split_item),
            unsigned(items.tyre_friction_item),unsigned(items.grip_item));
    } else if (handling_telemetry) std::puts("performance-levels legacy-stock");
    HandlingTelemetryLog handling_log(handling_telemetry ? stdout : nullptr);
    std::printf("PARTICLES %s\n",particles_enabled?"ON":"OFF");
    require(scene.set_sky(sky != "off"), "sky mode set after scene load");
    scene.set_wet_road_reflections(wet_road_reflections);
    bool low_reflections_pending=presentation.low_car_reflections;
    scene.set_race_visuals(true);
    scene.set_silhouette_shadows(true);
    require(scene.set_original_draw_distance(true),"draw distance setup failed");
    // Point-to-point finishes can coast beyond the permanent race footprint.
    // Back every authored zone before uploading; live requests/culling still
    // use the original zone policy and native256-record request limit.
    require(scene.set_complete_zone_backing(free_drive||selected_race.type==RaceType::sprint||
        selected_race.type==RaceType::drag||selected_race.type==RaceType::time_trial),"zone backing setup failed");
    const auto before = mallinfo(); heap.begin(before.uordblks,before.arena);
    Progress progress(screen,pad,heap,free_drive);
    const auto load_start = armGetSystemTick();
    progress.checkpoint("race metadata begin");
    sweep_mark("race.metadata");
    drag_mark("drag.metadata.before");
    require(race.prepare_metadata(root,error,&progress),error);
    drag_mark("drag.scene.before");
    const auto scene_variant=[] {
        // 4216B0 advances the NIS playlist stream independently of driving.
        static audio::TrafficAudioRandom random{std::uint32_t(armGetSystemTick()),0};
        const auto value=random.state;
        (void)random.integer(1);
        return value;
    };
    unsigned start_scene=0;
    if(intro_grid && event_number>0 && !drag_mode && !drift_mode) {
        require(select_road_start_scene(posix_file_system(),root,unsigned(intro_grid->actors.size()),
            unsigned(intro_grid->setup->desired_player_grid),scene_variant(),start_scene,error),error);
        require(scene.set_start_scene(start_scene,selected_race.region),"start crowd selection refused");
    }
    sweep_mark("race.scene");
    require(scene.load(posix_file_system(), root, error, &progress, &progress,
                       tuned, glows, car_shading, &race_car, dress, event_number, true,race_index), error);
    drag_mark("drag.scene.ready");
    if(intro_grid && event_number>0 && !drag_mode && !drift_mode) {
        sweep_mark("race.start_camera");
        RaceStartClock clock;
        require(scene.load_start_camera(posix_file_system(),root,start_scene,selected_race.region,clock,error),error);
        require(race.set_start_clock(clock.release,clock.go,error),error);
        std::printf("RACE_START scene=%u release=%u go=%u end=%u grid=%u round=%u\n",start_scene,clock.release,clock.go,clock.end,
            unsigned(intro_grid->setup->desired_player_grid),race_index);
    }
    sweep_mark("race.actors");
    if(intro_grid) {
        for(auto& actor:intro_grid->actors) {
            if(actor.config.role==Event0Role::opponent)
                require(scene.add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),
                    actor.frame->skin_binding.source,std::move(actor.shadow),error,false,actor.glow_anchors,&actor.hull,std::move(actor.neon)),error);
            actor.frame.reset();
        }
    }
    for(auto& actor:traffic_actors) {
        require(scene.add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),
            kNoHandle,std::move(actor.shadow),error,true,actor.glow_anchors,&actor.hull),error);
        actor.frame.reset();
    }
    std::printf("WORLD TEXTURES authored_mips=on filter=linear_min_mag_mip\n");
    screen.show({(free_drive ? std::string("Preparing free driving, ") :
                 "ASSETS PASS. Preparing event " + std::to_string(event_number) + ", ") +
                 selected_car_label + " and race course..."});
    if (!free_drive) screen.show({car_shading ? "CAR SHADING ON: static environment approximation" : "CAR SHADING OFF"});
    progress.checkpoint("race course");
    sweep_mark("race.session");
    drag_mark("drag.session.before");
    require(race.start(root,scene.track(),error,&progress,player_name),error);
    drag_mark("drag.session.ready");
    if (race.session().spec().nitrous.installed) {
        const auto& nos=race.session().spec().nitrous;
        std::printf("BOOST A=nitrous capacity_seconds=%.3f count=%ld rate=%.3f\n",
            double(nos.capacity_seconds),static_cast<long>(nos.count),double(nos.rate));
    }
    progress.checkpoint("race course prepared");
    sweep_mark("race.audio");
    drag_mark("drag.audio.before");
    std::vector<audio::RaceRemoteEngineBinding> rival_audio;
    if(intro_grid)for(const auto& actor:intro_grid->actors)if(actor.config.role==Event0Role::opponent)
        rival_audio.push_back({actor.config.visual.car,actor.config.performance?actor.config.performance->engine_a:0,actor.config.performance?actor.config.performance->induction:0,std::any_of(actor.placement_spec.induction.begin(),actor.placement_spec.induction.end(),[](float value){return value>0.F;})});
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
            },!replay_mode,race_car.car_type_name,performance?performance->engine_a:0,
            performance?performance->nitrous:0,performance?performance->tyres:0,rival_audio,performance?performance->induction:0,
            unsigned(traffic_actors.size()),
            race.session().props()?race.session().props()->sound_materials():0U);
        if (!audio_started && audio_note.empty())
            audio_note = "audio disabled: startup failed without diagnostic";
    } else audio_note = "audio disabled: render core pin failed";
    std::printf("AUDIO: %s\n", audio_note.c_str());
    drag_mark("drag.audio.ready");
    sweep_mark("race.hud");
    RaceHudAtlas hud_atlas;
    if (hud_mode && !panel_mode)
        require(load_race_hud_atlas(root,hud_atlas,error,!drift_mode&&!drag_mode,
            race.session().spec().nitrous.installed,drift_mode,race.track_number(),
            selected_race.type==RaceType::lap_knockout),error);
    if(drag_mode) {
        drag_mark("drag.hud.before");
        require(load_race_drag_hud(root,race_car.car_type_name,race.session().spec().handling,hud_atlas,error),error);
        drag_mark("drag.hud.ready");
        screen.show({"DRAG: ZR gas / ZL brake / right stick up/down shifts / R/L also shift / A nitrous"});
        std::puts("DRAG CONTROLS ZR=gas ZL=brake right_stick_up=up right_stick_down=down R=up L=down A=nitrous");std::fflush(stdout);
    }
    const RacePersistentPayload payload{scene.car_bytes(),race.persistent_bytes(),
        scene.track().contacts.memory.resident_bytes,scene.cache_bytes(),
        sound.resident_bytes(),hud_atlas.rgba.size()};
    std::printf("RACE_PAYLOAD %s\n",payload.description().c_str());std::fflush(stdout);
    require(check_race_persistent_payload(payload,error),error);
    const auto persistent=payload.total();
    if(sweep)sweep_mark("race.frame_build",payload.description().c_str());
    RaceResultStatistics result_stats;
    drag_mark("drag.frame.before");
    RaceGpuFrame frame; require(scene.frame(race.session(),frame,error),error);
    drag_mark("drag.frame.ready");
    const double load_ms = ms(armGetSystemTick()-load_start);
    const auto after = mallinfo(); heap.finish_load(after.uordblks,after.arena);
    screen.show({free_drive ? "Starting free driving..." : "ASSETS PASS. Uploading track and car to deko3d..."});
    const auto upload_start = armGetSystemTick();
    loading_screen.finish();
    if (!panel_mode) screen.close();
    docked = appletGetOperationMode() == AppletOperationMode_Console;
    scene_height = deko_effective_scene_height(requested_height,docked,!panel_mode);
    require(plan_deko_scene_target(width,height,!panel_mode,scene_height,scene_target),
            "invalid scene target after operation mode change");
    DekoRenderer renderer;
    sweep_mark("race.gpu_upload");
    drag_mark("drag.gpu.before");
    require(renderer.open(frame.commands,error,!panel_mode,scene.draw_capacity(),profile_frame || !panel_mode,true,
                          sky == "early",scene_height,
                          hud_mode && !panel_mode ? &hud_atlas : nullptr),error);
    renderer.enable_speed_blur_probe(nos_self_check);
    drag_mark("drag.gpu.ready");
    if(drag_trace)require(drag_journal.close(),drag_journal.error());
    if(ab_enabled)renderer.prepare_ab_trace(!ab_minimal);
    const double initial_upload = ms(armGetSystemTick()-upload_start);
    if(sweep)sweep_mark("race.gpu_ready",format("load_ms=%.0f upload_ms=%.0f",load_ms,initial_upload).c_str());
    std::unique_ptr<FrameProfileStats> profile_stats;
    if (detailed) profile_stats = std::make_unique<FrameProfileStats>();
    SwitchTiming profile_clock;
    const bool spike_probe=!panel_mode&&!ab_enabled&&std::strstr(NFSU_RACE_BUILD_TAG,"spike-");
    auto spike_trace=spike_probe?std::make_unique<FrameSpikeTrace<>>():nullptr;
    double previous_tail_ms=0;
    std::uint64_t diagnostic_present_end=0;
    const auto join_spike_gpu=[&](){if(spike_trace)for(const auto& r:renderer.gpu_profile().recent)
        spike_trace->gpu(r.frame,r.ms);};
    // Low-frequency CPU-only probes: six simulation stages plus existing
    // renderer stages. No GPU queries, file I/O, or per-sample log lines.
    std::array<double,6> sim_probe_sum{},sim_probe_max{};
    std::array<double,3> contact_probe_sum{},contact_probe_max{};
    std::array<double,7> render_probe_sum{},render_probe_max{};
    unsigned sim_probe_count=0,render_probe_count=0;
    const auto reset_cpu_probes=[&]() {
        sim_probe_sum={};sim_probe_max={};render_probe_sum={};render_probe_max={};
        sim_probe_count=render_probe_count=0;contact_probe_sum={};contact_probe_max={};
        renderer.reset_gpu_profile();
        if(spike_trace)*spike_trace={};
        previous_tail_ms=0;diagnostic_present_end=0;
    };
    std::vector<unsigned char> rgba;
    RaceTickDebt clock;
    auto previous = armGetSystemTick();
    FrontendArt pause_art;
    const bool ordinary_ui=post_art&&!panel_mode&&!ab_enabled&&!replay_mode;
    if(ordinary_ui)require(post_art->prepare_post_race(pause_art,error),error);
    RacePauseMenu pause_menu(free_drive,tournament&&tournament->event);RaceReturn pause_return=RaceReturn::menu;bool pause_exit=false;
    bool paused = false, printed_result = false, printed_final_perf = false;
    std::uint64_t presented = 0;
    double previous_frame = 0, previous_hud = 0;
    std::vector<std::string> last_perf;
    RaceFrameStats stats, presentation_stats;
    std::uint64_t previous_driving_present = 0;
    std::uint64_t last_heap_sample = 0, timed_heap_samples = 0;
    RaceHudFrame hud_frame;
    double finish_seconds=0,finish_delay=3;bool finish_seen=false;
    RaceFpsHistory fps_history;
    RaceDebugInput debug_input;
    RaceMinimapState minimap_state;
    SpeedBlur speed_blur;
    std::int32_t nos_self_check_boost_tick=-1;
    std::size_t speed_blur_resets=0;
    std::uint64_t fps_previous_present=0;
    unsigned unpause_resets = 0;
    std::string failure;
    std::uint64_t display_frame = 0;
    bool sweep_go=false;
    bool finish_probe_armed=false;
    const auto sweep_started=sweep?armGetSystemTick():0;
    const auto sweep_target=[&] {
        const auto& timing=race.session().timing();
        return timing.go_valid&&timing.step_ticks-timing.go_ticks>=2*kRaceTicksPerSecond;
    };
    std::unique_ptr<DragShiftTrace> drag_shift_trace;
    DragPadInput drag_pad_input;
    if(drag_mode)drag_shift_trace=std::make_unique<DragShiftTrace>();
    try {
    bool boost_log_pressed=false,boost_log_active=false,suppress_nitrous=false;
    std::uint64_t boost_log_step=0;
    while (appletMainLoop()) {
        if(ab_enabled && presented >= (ab_replay?ab_route.frames.size():AbRoute::limit))break;
        AbFrame ab_frame;
        const AbFrame* ab_expected=ab_replay?&ab_route.frames[presented]:nullptr;

        const bool now_docked = appletGetOperationMode() == AppletOperationMode_Console;
        if (now_docked != docked) {
            fps_history={}; fps_previous_present=0;
            require(!ab_enabled,"AB invalid: operation mode changed");
            docked = now_docked;
            const auto next_height = deko_effective_scene_height(requested_height,docked,!panel_mode);
            if (next_height != scene_height) {
                if(presentation.low_car_reflections) {
                    scene.set_low_car_reflections(false);
                    low_reflections_pending=true;
                    require(scene.frame(race.session(),frame,error),error);
                }
                require(renderer.reconfigure_target(frame.commands,error,scene.draw_capacity(),
                        profile_frame || !panel_mode,sky == "early",next_height,
                        hud_mode && !panel_mode ? &hud_atlas : nullptr),error);
                scene_height = next_height;
                require(plan_deko_scene_target(width,height,!panel_mode,scene_height,scene_target),
                        "invalid scene target on operation mode change");
                stats.reset(); presentation_stats.reset(); previous_driving_present=0; last_perf.clear();
                if (profile_stats) profile_stats->reset();
                reset_cpu_probes();
            }
            clock.reset(); previous = armGetSystemTick();
            continue;
        }
        const auto begin = armGetSystemTick();
        const auto elapsed = armTicksToNs(begin-previous)/1000; previous = begin;
        padUpdate(&pad); const auto down = padGetButtonsDown(&pad);
        if(sweep) {
            if(down&HidNpadButton_Plus)throw Cancel{};
            require(ms(begin-sweep_started)<45000,"campaign sweep timed out before two seconds after GO");
        }
        if(!(padGetButtons(&pad)&HidNpadButton_A))suppress_nitrous=false;
        if(!sweep && !panel_mode && !replay_mode && !ab_enabled && !paused &&
           (down&HidNpadButton_A) && race.skip_start_cinematic()) {
            suppress_nitrous=true;
            std::puts("RACE_START skipped to countdown");
        }
        if (!sweep && hud_mode && !panel_mode && !ab_enabled) {
            const auto held = padGetButtons(&pad);
            if (debug_input.update(padIsConnected(&pad),
                    (held & HidNpadButton_StickL) != 0, (held & HidNpadButton_StickR) != 0)) {
                fps_overlay = !fps_overlay;
                fps_history = {}; fps_previous_present = 0;
                std::printf("FPS_OVERLAY %s via L3+R3\n", fps_overlay ? "ON" : "OFF");
            }
        }
        const auto sampled_us = armTicksToNs(armGetSystemTick())/1000;
        ++display_frame;
        bool just_unpaused = false;
        const bool suspend=elapsed>1000000;
        const bool pause_requested=!sweep&&((down&(HidNpadButton_Plus|HidNpadButton_Y))||suspend);
        if(ordinary_ui&&pause_requested&&race.session().timing().outcome==RaceOutcome::running) {
            paused=true;pause_menu.open();clock.reset();sound.set_suspended(true);
            sound.frontend_sound(audio::FrontendSound::select);
            require(renderer.set_frontend_overlay(pause_art.atlas(),error,false),error);
            FrontendUiFrame pause_ui;FrontendClock pause_clock;
            RacePauseAction closing=RacePauseAction::none;double closing_seconds=0;
            padUpdate(&pad); // The opening edge must not activate the first row.
            while(appletMainLoop()) {
                if(appletGetFocusState()!=AppletFocusState_InFocus){pause_clock.resume();continue;}
                padUpdate(&pad);const auto buttons=padGetButtonsDown(&pad);
                RacePauseInput input;
                input.up=buttons&(HidNpadButton_Up|HidNpadButton_StickLUp);
                input.down=buttons&(HidNpadButton_Down|HidNpadButton_StickLDown);
                input.left=buttons&(HidNpadButton_Left|HidNpadButton_StickLLeft);
                input.right=buttons&(HidNpadButton_Right|HidNpadButton_StickLRight);
                input.accept=buttons&HidNpadButton_A;
                input.back=buttons&(HidNpadButton_B|HidNpadButton_Plus);
                const auto before=pause_menu.selected();const auto page=pause_menu.page();
                const auto action=closing==RacePauseAction::none?pause_menu.update(input):RacePauseAction::none;
                sound.set_ui_mix_percent(pause_menu.music(),pause_menu.effects());
                if(input.back)sound.frontend_sound(audio::FrontendSound::back);
                else if(input.accept)sound.frontend_sound(audio::FrontendSound::select);
                else if(before!=pause_menu.selected()||page!=pause_menu.page()||input.left||input.right||input.up||input.down)
                    sound.frontend_sound(input.up||input.left?audio::FrontendSound::up:audio::FrontendSound::down);
                if(action!=RacePauseAction::none) {
                    closing=action;closing_seconds=0;
                }
                const auto delta=pause_clock.sample(armTicksToNs(armGetSystemTick()));
                const double seconds=delta/1000000000.0;
                pause_art.advance(float(seconds));
                if(closing!=RacePauseAction::none) {
                    closing_seconds+=seconds;
                    if(closing_seconds>=200.0/1200.0) {
                        pause_exit=closing!=RacePauseAction::resume;
                        pause_return=closing==RacePauseAction::restart?RaceReturn::restart:RaceReturn::menu;break;
                    }
                }
                pause_art.record_pause(frame.commands.width,frame.commands.height,pause_menu,pause_ui,
                    closing==RacePauseAction::none?~0U:unsigned(closing_seconds*1200));
                require(renderer.present_frontend(pause_ui,error),error);
            }
            if(pause_exit||!appletMainLoop())break;
            suppress_nitrous=(padGetButtons(&pad)&HidNpadButton_A)!=0;
            require(renderer.restore_race_overlay(hud_atlas,error),error);
            paused=false;just_unpaused=true;previous=armGetSystemTick();clock.reset();
            stats.reset();presentation_stats.reset();previous_driving_present=0;fps_previous_present=0;last_perf.clear();++unpause_resets;
            if(profile_stats){profile_stats->reset();renderer.reset_gpu_profile();}reset_cpu_probes();
        }else if(ordinary_ui&&suspend) {
            clock.reset();previous=armGetSystemTick();continue;
        }else if(!ordinary_ui) {
            if(down&HidNpadButton_Plus){ab_cancelled=ab_replay;break;}
            if((down&HidNpadButton_Y)&&!ab_replay){require(!ab_enabled,"AB invalid: pause requested");just_unpaused=paused;paused=!paused;clock.reset();}
            if(suspend){require(!ab_enabled,"AB invalid: OS suspend");paused=true;clock.reset();}
        }
        const bool connected = padIsConnected(&pad);
        const bool running = race.session().timing().outcome == RaceOutcome::running;
        if(drag_mode&&!replay_mode&&!ab_replay)
            drag_pad_input.poll(pad,paused||just_unpaused||!running);
        sound.set_suspended(paused);
        const bool driving_before = !paused && !just_unpaused && running &&
            race.session().timing().stage == RaceStage::racing;
        const bool finish_driving=!replay_mode&&!ab_enabled&&(race.session().rival_count()>0||race.session().is_drift())&&
            race.session().timing().outcome==RaceOutcome::finished;
        if (!paused && !just_unpaused && (running||finish_driving)) clock.add(elapsed); else clock.reset();
        double simulation = 0, residency = 0, record = 0, upload = 0;
        std::array<double,8> simulation_ticks{};
        RaceFrameProfile record_profile;
        std::array<double,9> spatial_probe{};unsigned spatial_probe_ticks=0;
        const bool cpu_probe=!ab_minimal && (spike_probe || presented%60==0);
        DekoUpdateTiming update_timing;
        unsigned ticks = 0;
        while (!paused && (race.session().timing().outcome == RaceOutcome::running||finish_driving) && ticks < 8 && (ab_replay ? ticks<ab_expected->count : clock.take())) {
            GamepadFrame input;
            if(sweep) {
                const auto& timing=race.session().timing();
                const auto buttons=timing.go_valid?gamepad_button_bit(GamepadButton::trigger_right):0;
                input=race_input_frame({1,buttons,0},timing.step);
            } else if (ab_replay) input=ab_expected->inputs[ticks].unpack();
            else if (replay_mode) require(replay.next(race.session().timing().step,input,error),error);
            else input = drag_mode?drag_pad_input.take(race.session().timing().step):
                sample(pad,race.session().timing().step);
            if(nos_self_check) {
                input.pads[0].connected=true;for(auto& axis:input.pads[0].axes)axis=0;
                const auto& timing=race.session().timing();
                const auto& velocity=race.session().car().velocity;
                const double speed2=velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z;
                if(nos_self_check_boost_tick<0 && timing.stage==RaceStage::racing &&
                   timing.race_ticks>=24000 && speed2>=(80.0/3.6)*(80.0/3.6)) {
                    nos_self_check_boost_tick=timing.race_ticks;
                    std::printf("NOS_SELF_CHECK phase=boost_start race_ms=%d speed_kmh=%.3f\n",
                        int(timing.race_ticks/4),std::sqrt(speed2)*3.6);
                }
                const bool boost=nos_self_check_boost_tick>=0;
                // Match a real connection: release first, then press. A button
                // already held on the connecting frame remains latched.
                const bool gas=timing.step>0;
                input.pads[0].buttons=switch_race_buttons((gas?kNpadButtonZR:0)|(boost?kNpadButtonA:0));
            }
            if (!replay_mode && !ab_replay) {
                if(suppress_nitrous)input.pads[0].buttons&=~gamepad_button_bit(GamepadButton::shoulder_left);
                debug_input.filter(input);
            }
            if (!replay_mode && !ab_replay) {
                input.sample_clock_available = true;
                input.sample_time_us = sampled_us;
                input.display_frame = display_frame;
            }
            if(ab_record)ab_frame.inputs[ticks]=AbInput::pack(input);
            auto stage = ab_minimal ? 0 : armGetSystemTick();
            const auto consumed_us = armTicksToNs(stage)/1000;
            RaceSimulationProfile sim_probe;
            const bool probe_tick=cpu_probe && !race_car_held(race.session().timing());
            if(drag_shift_trace)drag_shift_trace->before(race.session());
            const bool crashing=race.session().crash_camera_event()!=0;
            require(race.simulate(input,error,probe_tick?&sim_probe:nullptr,probe_tick?&profile_clock:nullptr),error);
            if(!finish_seen&&race.session().timing().outcome==RaceOutcome::finished)
                finish_delay=crashing?6.:3.; // 42C2FB checks active stunt/crash modes6/7.
            if(drag_shift_trace)drag_shift_trace->after(race.session(),input.pads[0].buttons);
            result_stats.observe(race.session());
            if(probe_tick) {
                const auto values=sim_probe.values();++sim_probe_count;
                ++spatial_probe_ticks;
                for(unsigned i=0;i<6;++i)spatial_probe[i]+=values[i];
                spatial_probe[6]+=sim_probe.traffic_motion;
                spatial_probe[7]+=sim_probe.prop_motion;
                spatial_probe[8]+=sim_probe.prop_contacts;
                for(unsigned i=0;i<6;++i){sim_probe_sum[i]+=values[i];sim_probe_max[i]=std::max(sim_probe_max[i],values[i]);}
                const double children[]={sim_probe.traffic_motion,sim_probe.prop_motion,sim_probe.prop_contacts};
                for(unsigned i=0;i<3;++i){contact_probe_sum[i]+=children[i];contact_probe_max[i]=std::max(contact_probe_max[i],children[i]);}
            }
            const bool boost_pressed=input.pads[0].connected &&
                (input.pads[0].buttons & gamepad_button_bit(GamepadButton::shoulder_left));
            const auto boost= race.session().nitrous();
            const auto boost_step=race.session().timing().step;
            if(boost_pressed!=boost_log_pressed || boost.active!=boost_log_active ||
               (boost.active && boost_step-boost_log_step>=60)) {
                const auto& model=race.session().physics_telemetry().back().model;
                double wheel=0;for(const auto& w:model.wheels)wheel+=w.drive_torque;
                std::printf("BOOST_STATE step=%llu A=%u active=%u charge=%u/%u speed_kmh=%.3f rpm=%.1f gear=%d crank_Nm=%.3f wheel_Nm=%.3f\n",
                    (unsigned long long)boost_step,unsigned(boost_pressed),unsigned(boost.active),
                    unsigned(boost.remaining),unsigned(boost.capacity),model.forward_speed*3.6,
                    race.session().car().engine.angular_speed*9.54930400848388671875,
                    int(race.session().car().gear),double(model.engine_torque),wheel);
                boost_log_step=boost_step;
            }
            boost_log_pressed=boost_pressed;boost_log_active=boost.active;
            require(handling_log.write(race.session(),input,consumed_us,!replay_mode),
                    "handling telemetry write failed");
            const double tick_ms = ab_minimal ? 0 : ms(armGetSystemTick()-stage);
            simulation += tick_ms; simulation_ticks[ticks] = tick_ms;
            stage = ab_minimal ? 0 : armGetSystemTick(); require(race.commit(error),error);
            scene.advance_particles(race.session());
            auto audio_snapshot=audio::race_audio_snapshot(race.session());
            if (free_drive) { audio_snapshot.completed_laps=0; audio_snapshot.finished=false; }
            sound.on_tick(audio_snapshot,race.audio_world_random());
            if (replay_mode) std::fputs(race.step_line().c_str(),stdout);
            if(!ab_minimal)residency += ms(armGetSystemTick()-stage);
            ++ticks;
            if(sweep&&!finish_probe_armed&&sweep_target())break;
        }
        if (ticks) {
            auto stage = ab_minimal ? 0 : armGetSystemTick();
            require(scene.frame(race.session(),frame,error,
                detailed || cpu_probe ? &record_profile : nullptr,false,
                detailed || cpu_probe ? &profile_clock : nullptr),error);
            if(cpu_probe && !race_car_held(race.session().timing())) {
                ++render_probe_count;
                const double values[]={record_profile.cull_ms,record_profile.list_ms,record_profile.car_ms,
                    record_profile.track_ms,record_profile.combine_ms,record_profile.road_reflections_ms,
                    record_profile.race_visuals_ms};
                for(unsigned i=0;i<7;++i){render_probe_sum[i]+=values[i];render_probe_max[i]=std::max(render_probe_max[i],values[i]);}
            }
            record = ab_minimal ? 0 : ms(armGetSystemTick()-stage);
            stage = ab_minimal ? 0 : armGetSystemTick();
            require(renderer.update(frame.commands,error,detailed ? &update_timing : nullptr),error);
            upload = ab_minimal ? 0 : ms(armGetSystemTick()-stage);
        }
        if(ab_enabled) {
            ab_frame.count=ticks;ab_frame.state=race.step_line();
            const auto& c=frame.commands.frame;
            auto hash=ab_hash(c.eye,sizeof c.eye);
            hash=ab_hash(c.right,sizeof c.right,hash);hash=ab_hash(c.up,sizeof c.up,hash);
            hash=ab_hash(c.forward,sizeof c.forward,hash);hash=ab_hash(c.projection,sizeof c.projection,hash);
            ab_frame.camera=hash;
            if(ab_replay)require(ticks==ab_expected->count && ab_frame.state==ab_expected->state &&
                hash==ab_expected->camera,"AB state/camera mismatch at frame "+std::to_string(presented+1));
        }
        const bool finished = race.session().timing().outcome == RaceOutcome::finished;
        if(finished) {
            if(!finish_seen){finish_seen=true;finish_seconds=0;std::puts("HUD RACE_OVER begin");std::fflush(stdout);}
            else if(!paused && elapsed<=1000000)finish_seconds+=double(elapsed)/1000000;
        }
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
        // Original blur reads view-space speed and the active NOS resource,
        // not the button. Freeze the envelope in pause and clear it on reset.
        const auto blur_resets=race.session().timing().restarts+
            (race.session().timing().racers.empty()?0:race.session().timing().racers[0].resets);
        if(speed_blur_resets!=blur_resets) {
            speed_blur.reset();speed_blur_resets=blur_resets;
        }
        const auto& blur_view=race.session().camera();
        const auto& velocity=race.session().camera_velocity();
        const VehicleVec3 right{blur_view.forward.y*blur_view.up.z-blur_view.forward.z*blur_view.up.y,
            blur_view.forward.z*blur_view.up.x-blur_view.forward.x*blur_view.up.z,
            blur_view.forward.x*blur_view.up.y-blur_view.forward.y*blur_view.up.x};
        const auto local=camera_motion_to_view(velocity,
            {{float(right.x),float(right.y),float(right.z)}},
            {{float(blur_view.up.x),float(blur_view.up.y),float(blur_view.up.z)}},
            {{float(blur_view.forward.x),float(blur_view.forward.y),float(blur_view.forward.z)}});
        if(presentation.motion_blur && race.session().timing().stage==RaceStage::racing && !finished)
            renderer.set_speed_blur(speed_blur.frame(race.session().nitrous_boosting(),
                local[0],local[1],local[2],!paused));
        else {speed_blur.reset();renderer.set_speed_blur({});}
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
            lines.push_back(format("%.1f KM/H | gear %d | pad %s | step %llu | physics 240 Hz / pad 60 Hz | catch-up %u",
                speed,int(car.gear),connected?"CONNECTED":"DISCONNECTED - NEUTRAL",(unsigned long long)race.session().timing().step,ticks));
            lines.push_back(format("SIM %.2f ms | RES/HASH/LOG %.2f ms | CULL/RECORD %.2f ms | UPLOAD/CONSTANTS %.2f ms",
                simulation,residency,record,upload));
            lines.push_back(format("DRAW %.2f ms | TRANSFER/COPY %.2f ms (GPU %.2f) | previous HUD/present %.2f ms",
                draw,transfer,readback,previous_hud));
            lines.push_back(format("Previous PANEL FRAME %.2f ms (>25 ms %s) | debt %llu ticks | load %.0f ms | initial upload %.0f ms",
                previous_frame,previous_frame>RaceFrameStats::kOverThresholdMs?"OVER":"WITHIN",(unsigned long long)clock.debt_ticks(),load_ms,initial_upload));
            lines.push_back(format("DRAWS %zu track + %zu car + %zu glow | TRIANGLES %llu | far %.2f | visible %zu | unplaced %llu",
                frame.track_draws,frame.car_draws,frame.glow_draws,(unsigned long long)frame.triangles,frame.far_plane,frame.visible,(unsigned long long)frame.unplaced));
            lines.push_back(format("RESIDENCY %zu sections | CPU stream %llu / 75497600 | persistent payload %llu / %llu",
                race.resident_count(),(unsigned long long)scene.track().streaming_bytes,(unsigned long long)persistent,
                (unsigned long long)kPersistentBudgetPlaceholder));
            lines.push_back(format("Command cache metadata EST %llu / 16777216 bytes (immutable track templates)",
                (unsigned long long)scene.cache_bytes()));
            const auto memory = renderer.memory_usage();
            lines.push_back(format("GPU blocks %llu / base %llu | frame uniforms %llu | resources %zu",
                (unsigned long long)renderer.gpu_bytes(),(unsigned long long)kGpuBudgetPlaceholder,
                (unsigned long long)memory.frame_uniform_bytes,frame.commands.resource_table().resources.size()));
            lines.push_back(format("Static uniforms %llu | prepared commands %llu bytes",
                (unsigned long long)memory.static_uniform_bytes,
                (unsigned long long)memory.prepared_command_bytes));
            lines.push_back(format("MALLOC IN-USE before %llu | after load %llu | NOW %llu (includes GPU BLOCKS)",
                (unsigned long long)heap.before_load,(unsigned long long)heap.after_load,(unsigned long long)heap.current));
            lines.push_back(format("HEAP LOAD OBS HWM ABS %llu | delta %llu / 75497600 | LOAD HEAP OVER %llu",
                (unsigned long long)heap.load_peak,(unsigned long long)heap.load_delta(),(unsigned long long)heap.load_overrun(75497600)));
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
            static_cast<std::uint32_t>(frame.car_draws),driving_before && !finished,ab_enabled?display_frame:0,profile_frame || cpu_probe,spike_probe?display_frame:0};
        const auto present_start = armGetSystemTick();
        if(hud_mode&&!pause_menu.hud())hud_frame={};
        if (hud_mode && pause_menu.hud()) build_race_hud_frame(race.session(),hud_atlas,frame.commands.width,
                                           frame.commands.height,hud_frame,presentation.metric_speed);
        if (hud_mode && pause_menu.hud() && hud_atlas.map.valid)
            require(append_race_minimap(race.session(),hud_atlas,frame.commands.width,
                    frame.commands.height,minimap_state,hud_frame),"minimap geometry invalid or HUD capacity exceeded");
        if (fps_overlay && hud_mode)
            append_race_fps_overlay(hud_atlas,fps_history,frame.commands.width,
                                    frame.commands.height,hud_frame);
        if(hud_mode && finished) {
            std::string_view message="Race Over";
            const auto status=race.session().timing().racers[0].status;
            if(status==RacerStatus::knocked_out)message="You were knocked out!"; // English C246A543, 4A7EF9.
            else if(race.session().is_drag()) {
                if(status==RacerStatus::blown_engine)message=hud_atlas.drag_failure_labels[0];
                else if(status==RacerStatus::totalled)message=hud_atlas.drag_failure_labels[1];
            }
            append_race_over(hud_atlas,finish_seconds,frame.commands.width,frame.commands.height,hud_frame,message);
        }
        const bool nos_probe_was_complete=nos_self_check && renderer.speed_blur_probe_complete();
        if(sweep&&!presented)sweep_mark("race.first_frame_submit");
        require(renderer.present(error,ab_minimal?nullptr:&present,&scene_frame,
                                 hud_mode ? &hud_frame : nullptr),error);
        if(hud_mode&&hud_frame.shown_drift_notice)race.consume_drift_notice();
        // The first complete capture must be submitted before recording partial
        // frames. A simulation tick may replace the frame prepared at load.
        if(low_reflections_pending) {
            scene.set_low_car_reflections(true);
            low_reflections_pending=false;
        }
        if(sweep) {
            if(!presented)sweep_mark("race.first_frame");
            const auto& timing=race.session().timing();
            require(finish_probe_armed||timing.outcome==RaceOutcome::running,"race ended before the campaign sweep target");
            if(timing.go_valid&&!sweep_go) {
                sweep_mark("race.go");sweep_go=true;
            }
            if(!finish_probe_armed&&sweep_target()) {
                require(frame.track_draws&&frame.car_draws,"campaign sweep target frame is empty");
                sweep_mark(finish_sweep?"race.before_finish_probe":"race.target",format("after_go_ms=%d frames=%llu track_draws=%zu car_draws=%zu",
                    (timing.step_ticks-timing.go_ticks)/4,(unsigned long long)(presented+1),frame.track_draws,frame.car_draws).c_str());
                if(!finish_sweep) {
                    require(renderer.close(error),error);
                    return RaceReturn::sweep_complete;
                }
                require(race.arm_finish_probe(error),error);
                finish_probe_armed=true;
                sweep_mark("race.finish_probe_armed");
            }
        }
        if(nos_self_check && !nos_probe_was_complete && renderer.speed_blur_probe_complete()) {
            // CPU reference and pixel export are intentional diagnostic stalls,
            // not an OS suspend. Resume the automatic run without timing debt.
            clock.reset();previous=armGetSystemTick();
        }
        const bool nos_self_check_done=nos_self_check && renderer.speed_blur_probe_complete() &&
            nos_self_check_boost_tick>=0 && race.session().timing().race_ticks>=nos_self_check_boost_tick+8000;
        if(nos_self_check&&(nos_self_check_done||race.session().timing().race_ticks>=96000)) {
            std::printf("NOS_SELF_CHECK completed=%u nitrous_remaining=%u; inspect BLUR_PASS/BLUR_PROBE_END, timing perturbed\n",
                renderer.speed_blur_probe_complete()?1U:0U,race.session().nitrous().remaining);
            std::fflush(stdout);return RaceReturn::menu;
        }
        const auto present_end = armGetSystemTick();
        if (fps_overlay) {
            if (fps_previous_present) fps_history.add(ms(present_end-fps_previous_present));
            fps_previous_present=present_end;
        }
        const double driving_interval=previous_driving_present&&driving_before&&!finished?ms(present_end-previous_driving_present):0;
        diagnostic_present_end=present_end;
        const double ab_interval=ab_previous_present?ms(present_end-ab_previous_present):0;
        ab_previous_present=present_end;
        present_cpu = ms(present_end-present_start);
        if (driving_before && !finished) {
            if (previous_driving_present) {
                RaceFrameSample interval;
                interval.frame_ms=ms(present_end-previous_driving_present);
                presentation_stats.add(interval);
            }
            previous_driving_present=present_end;
        } else previous_driving_present=0;
        if (hud_trace) {
            const auto& r=hud_frame.readout;
            std::printf("HUD_FRAME present %llu tick %llu physics %llu arm-tick %llu rpm %.9f wheel-rpm %.9f gear %d rotation %.9f max %.0f redline %.0f\n",
                (unsigned long long)presented,
                (unsigned long long)race.session().timing().step,
                (unsigned long long)r.physics_step,(unsigned long long)present_start,
                r.engine_rpm,r.wheel_rpm,r.gear,r.rotation,r.max_rpm,r.redline_rpm);
        }
        const auto mall_start = ab_minimal ? 0 : armGetSystemTick();
        // Allocator traversal is diagnostic, not frame work. Preserve real
        // snapshots every two seconds without walking the heap on every frame.
        // These remain sampled peaks, not an allocation-exact high-water mark.
        if (!ab_minimal && (!last_heap_sample || armTicksToNs(mall_start-last_heap_sample) >= 2000000000ULL)) {
            const auto info = mallinfo();
            mallinfo_ms = ms(armGetSystemTick()-mall_start);
            heap.observe(info.uordblks,info.arena);
            last_heap_sample = mall_start;
            ++timed_heap_samples;
        }
        frame_ms = ms(armGetSystemTick()-begin);
        measured_frame_ms = frame_ms;
        measured.present_cpu_ms = present_cpu;
        measured.acquire_ms = present.acquire_ms;
        measured.fence_ms = present.fence_wait_ms;
        measured.command_ms = present.uniform_write_ms + present.record_ms + present.finish_ms;
        measured.submit_ms = present.submit_ms;
        if(ab_enabled) {
            AbCpuRow row{display_frame,race.session().timing().step,frame.triangles,ticks,
                unsigned(frame.track_draws),unsigned(frame.car_draws),unsigned(frame.glow_draws),
                unsigned(driving_before&&!finished),{}};
            row.ms={frame_ms,ab_interval,simulation,residency,record,upload,present_cpu,
                present.acquire_ms,present.fence_wait_ms,present.uniform_write_ms,present.record_ms,
                present.finish_ms,present.submit_ms,mallinfo_ms};
            ab_cpu.push_back(row);
            if(ab_record)ab_route.frames.push_back(std::move(ab_frame));
        }
        if(spike_trace && driving_before && !finished) {
            FrameSpikeRow row;const auto& session=race.session();const auto& p=session.car().position;
            row.frame=display_frame;row.tick=session.timing().step;row.debt=clock.debt_ticks();
            row.node=session.progress().coordinate.node;row.lap=session.progress().lap;
            row.x=p.x;row.y=p.y;row.z=p.z;row.fraction=session.progress().coordinate.fraction;
            row.steps=ticks;row.draws=unsigned(frame.track_draws+frame.car_draws+frame.glow_draws);
            if(session.props())for(const auto& prop:session.props()->objects())if(prop.awake&&!prop.expired){
                ++row.awake;row.vertices+=unsigned(prop.points.size());}
            row.times={driving_interval,frame_ms,simulation,residency,record,upload,present_cpu,
                present.acquire_ms,present.fence_wait_ms,present.uniform_write_ms,present.record_ms,
                present.finish_ms,present.submit_ms,mallinfo_ms,
                driving_interval?std::max(0.0,driving_interval-ms(present_end-begin)):0,
                previous_tail_ms,std::max(0.0,frame_ms-simulation-residency-record-upload-present_cpu-mallinfo_ms)};
            row.simulation=spatial_probe;
            row.render={record_profile.cull_ms,record_profile.list_ms,record_profile.car_ms,
                record_profile.track_ms,record_profile.combine_ms,record_profile.road_reflections_ms,record_profile.race_visuals_ms};
            spike_trace->add(row);join_spike_gpu();
        }
        ++presented;
        // One spatial sample per existing CPU-probe frame. Print after timed
        // simulation/render/presentation, with the same frame's stage sums.
        // Distance is diagnostic only: it never changes prop simulation.
        if(spatial_probe_ticks && !spike_probe) {
            const auto& session=race.session();const auto& at=session.car().position;
            const auto& route=session.progress().coordinate;
            unsigned awake=0,far=0,vertices=0;float oldest=0;double farthest=0;
            if(session.props())for(const auto& prop:session.props()->objects())if(prop.awake&&!prop.expired) {
                ++awake;vertices+=unsigned(prop.points.size());oldest=std::max(oldest,prop.age);
                const auto& p=prop.state.position;
                const double dx=p.x-at.x,dy=p.y-at.y,distance=std::sqrt(dx*dx+dy*dy);
                farthest=std::max(farthest,distance);if(distance>200)++far;
            }
            std::printf("CPU_SPATIAL frame=%llu tick=%llu lap=%d x=%.3f y=%.3f z=%.3f node=%d fraction=%.6f steps=%u sim_ms=%.3f player_ms=%.3f rivals_ms=%.3f pair_ms=%.3f traffic_ms=%.3f props_ms=%.3f prop_contacts_ms=%.3f awake=%u vertices=%u far200=%u farthest=%.1f oldest=%.2f record_ms=%.3f command_ms=%.3f frame_ms=%.3f\n",
                (unsigned long long)presented,(unsigned long long)session.timing().step,session.progress().lap,
                at.x,at.y,at.z,route.node,double(route.fraction),spatial_probe_ticks,simulation,
                spatial_probe[3],spatial_probe[2],spatial_probe[4],spatial_probe[6],spatial_probe[7],spatial_probe[8],
                awake,vertices,far,farthest,double(oldest),record,measured.command_ms,frame_ms);
        }
        if (!profile_frame && (presented == 1 || presented % 120 == 0 ||
                               (finished && !printed_final_perf))) {
            const auto& sampled_gpu=renderer.gpu_profile();
            std::printf("GPU_PROBE_PROGRESS samples=%llu mean_ms=%.6f max_ms=%.6f invalid=%llu dropped=%llu last_tick=%llu\n",
                (unsigned long long)sampled_gpu.scene.count(),sampled_gpu.scene.mean(),sampled_gpu.scene.max(),
                (unsigned long long)sampled_gpu.invalid,(unsigned long long)sampled_gpu.dropped,
                (unsigned long long)sampled_gpu.last_tick);
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
            std::printf("PERF %.1f KM/H gear %d pad %s | DRAWS %zu track + %zu car + %zu glow | TRIANGLES %llu | far %.2f | visible %zu | unplaced %llu | RESIDENCY %zu | GPU %llu / base %llu | resources %zu\n",
                speed,int(car.gear),connected?"CONNECTED":"DISCONNECTED - NEUTRAL",frame.track_draws,frame.car_draws,frame.glow_draws,
                (unsigned long long)frame.triangles,frame.far_plane,frame.visible,
                (unsigned long long)frame.unplaced,race.resident_count(),
                (unsigned long long)renderer.gpu_bytes(),(unsigned long long)kGpuBudgetPlaceholder,frame.commands.resource_table().resources.size());
            std::printf("PERF LOAD %.0f ms initial upload %.0f ms | CPU stream %llu / 75497600 | persistent %llu / %llu | CACHE EST %llu / 16777216 | HEAP before %llu after load %llu now %llu load HWM %llu run HWM %llu\n",
                load_ms,initial_upload,(unsigned long long)scene.track().streaming_bytes,(unsigned long long)persistent,
                (unsigned long long)kPersistentBudgetPlaceholder,
                (unsigned long long)scene.cache_bytes(),
                (unsigned long long)heap.before_load,(unsigned long long)heap.after_load,
                (unsigned long long)heap.current,(unsigned long long)heap.load_peak,(unsigned long long)heap.peak);
            std::printf("PERF HEAP LOAD OBS HWM ABS %llu | delta %llu / 75497600 | LOAD HEAP OVER %llu | HEAP RUN OBS HWM %llu | ARENA NOW %llu | ARENA OBS HWM %llu\n",
                (unsigned long long)heap.load_peak,(unsigned long long)heap.load_delta(),
                (unsigned long long)heap.load_overrun(75497600),(unsigned long long)heap.peak,
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
                set(FrameProfileStage::road_reflections,record_profile.road_reflections_ms);
                set(FrameProfileStage::race_visuals,record_profile.race_visuals_ms);
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
        if(spike_probe&&diagnostic_present_end)previous_tail_ms=ms(armGetSystemTick()-diagnostic_present_end);
        // Allow the authored finish banner to become visible before results.
        // This three-second port dwell is not an original results-camera port.
        if (finished && (!hud_mode || panel_mode || replay_mode || ab_enabled || profile_stats || finish_seconds>finish_delay)) break;
    }
    } catch (const std::exception& e) {
        if(sweep)throw;
        failure = e.what();
        // Preserve the actual exception on SD when nxlink is disconnected.
        // A terminal result alone cannot identify which continuation failed.
        if(post_art)flow_mark("race.failed",failure.c_str());
    }
    if(sweep&&!appletMainLoop())throw Cancel{};
    if(sweep&&!finish_sweep)throw Cancel{};
    if(finish_sweep) {
        require(race.session().timing().outcome==RaceOutcome::finished&&race.session().player_won(),
            "finish probe did not produce a timed player win");
        sweep_mark("race.finished",format("place=%u finish_ticks=%d",race.session().player_position(),
            race.session().timing().racers[0].finish_ticks).c_str());
    }
    if(drag_shift_trace)drag_shift_trace->print();
    if(drag_mode&&!replay_mode&&!ab_replay)drag_pad_input.print();
    // Free drive returns directly to the menu, so preserve its complete
    // measured window before the local accumulators go out of scope.
    // Final boundary snapshot is outside both driving timing windows.
    const auto final_heap = mallinfo();
    heap.observe(final_heap.uordblks,final_heap.arena);
    std::printf("RUN_STATS heap_sampling=2s timed_heap_samples=%llu final_snapshot=1 peaks=sampled_not_exact\n",
        (unsigned long long)timed_heap_samples);
    const auto audio_stats=sound.stats();
    std::printf("SWITCH_MUSIC mode=race missing_frames=%llu buffered_frames=%zu\n",
        static_cast<unsigned long long>(audio_stats.ring_underruns),audio_stats.ring_fill);
    const auto measured_clocks=read_clocks();
    std::printf("RUN_STATS build=%s event=%lu height=%lu unpause_resets=%u CPU_HZ=%lu CPU_OK=%u GPU_HZ=%lu GPU_OK=%u EMC_HZ=%lu EMC_OK=%u\n",
        NFSU_RACE_BUILD_TAG,(unsigned long)event_number,(unsigned long)scene_height,unpause_resets,
        (unsigned long)measured_clocks.cpu_bus_hz,unsigned(measured_clocks.cpu_bus_ok),
        (unsigned long)measured_clocks.gpu_hz,unsigned(measured_clocks.gpu_ok),
        (unsigned long)measured_clocks.emc_hz,unsigned(measured_clocks.emc_ok));
    std::printf("RUN_STATS scope=driving_only reset_on_unpause_and_resolution_change; presentation_return_intervals_are_not_GPU_timestamps_or_display_scanout\n");
    print_run_measurement("frame_work",stats);
    print_run_measurement("presentation_interval",presentation_stats);
    const auto stage_mean=stats.mean();
    std::printf("RUN_STATS mean_sim_ms=%.6f mean_residency_ms=%.6f mean_record_ms=%.6f mean_update_ms=%.6f mean_present_cpu_ms=%.6f\n",
        stage_mean.simulation_ms,stage_mean.residency_ms,stage_mean.record_ms,
        stage_mean.update_ms,stage_mean.present_cpu_ms);
    std::fflush(stdout);
    if(sim_probe_count) {
        const char* names[]={"planner","drivers","rival_physics","player_physics","pair_contacts","timing_camera"};
        for(unsigned i=0;i<6;++i)std::printf("CPU_PROBE_SIM stage=%s samples=%u mean_ms=%.6f max_ms=%.6f cadence=%u_presentations\n",
            names[i],sim_probe_count,sim_probe_sum[i]/sim_probe_count,sim_probe_max[i],spike_probe?1U:60U);
        const char* children[]={"traffic_motion","prop_motion","prop_contacts"};
        for(unsigned i=0;i<3;++i)std::printf("CPU_PROBE_CONTACT stage=%s samples=%u mean_ms=%.6f max_ms=%.6f parent=pair_contacts cadence=%u_presentations\n",
            children[i],sim_probe_count,contact_probe_sum[i]/sim_probe_count,contact_probe_max[i],spike_probe?1U:60U);
    }
    if(render_probe_count) {
        const char* names[]={"cull","list","car","track","combine","road_reflections_child","visuals_child"};
        for(unsigned i=0;i<7;++i)std::printf("CPU_PROBE_RENDER stage=%s samples=%u mean_ms=%.6f max_ms=%.6f cadence=%u_presentations\n",
            names[i],render_probe_count,render_probe_sum[i]/render_probe_count,render_probe_max[i],spike_probe?1U:60U);
    }
    std::fflush(stdout);
    // Leave the finish cue audible on the result screen. Other exits mute
    // the still-running engine and music before showing that screen.
    if (!failure.empty() || race.session().timing().outcome != RaceOutcome::finished)
        sound.set_suspended(true);
    const auto& player_result=race.session().timing().racers[0];
    const auto actor_places=race.session().result_positions();
    std::array<unsigned,4> configuration_places{};
    std::array<unsigned,4> configuration_to_actor{{0,1,2,3}};
    if(intro_grid) {
        unsigned actor=1;
        for(const auto& entry:intro_grid->actors)if(entry.config.role==Event0Role::opponent) {
            const auto index=unsigned(entry.config.config_id-99);
            require(index>=1&&index<4,"tournament actor identity outside roster");
            configuration_places[index]=actor_places[actor];
            configuration_to_actor[index]=actor++;
        }
        configuration_places[0]=actor_places[0];
    }
    if(drift_mode)for(unsigned rank=0;rank<4;++rank)
        configuration_places[race.session().drift_competition().standings()[rank].actor]=rank+1;
    CareerTournament resolved_tournament;
    if(tournament&&tournament->event==event_number&&tournament->round==race_index&&race_index<tournament->places.size()) {
        resolved_tournament=*tournament;resolved_tournament.places[resolved_tournament.round++]=configuration_places;
    }
    const bool intro_won=event_number==0 && failure.empty() && player_result.finish_valid && player_result.position==1;
    RaceReturn result=pause_exit?pause_return:intro_won?RaceReturn::intro_won:RaceReturn::menu;
    bool post_shown=false;
    std::snprintf(flow_detail,sizeof flow_detail,"finished=%u status=%u place=%u won=%u failure=%u post=%u profile=%u panel=%u free=%u intro=%u",
        unsigned(player_result.finish_valid),unsigned(player_result.status),race.session().player_position(),unsigned(race.session().player_won()),unsigned(!failure.empty()),
        unsigned(post_art!=nullptr),unsigned(bool(profile_stats)),unsigned(panel_mode),unsigned(free_drive),unsigned(intro_won));
    if(post_art)flow_mark("race.terminal",flow_detail);
    if(post_art && !pause_exit && !profile_stats && !panel_mode && !free_drive && !intro_won &&
       failure.empty() && post_race_result_ready(player_result)) {
        // Keep the live song through style counting and rewards.
        sound.set_suspended(false);
        try {
            FrontendArt results_art;
            require(post_art->prepare_post_race(results_art,error),error);
            std::snprintf(flow_detail,sizeof flow_detail,"atlas=%ux%u bytes=%lu",
                results_art.atlas().width,results_art.atlas().height,(unsigned long)results_art.atlas().rgba.size());
            flow_mark("post.atlas",flow_detail);
            require(renderer.set_frontend_overlay(results_art.atlas(),error),error);
            std::string finish_scene_error;
            bool staged_finish=scene.load_finish_scene(posix_file_system(),root,race.session(),
                selected_race.region,scene_variant(),finish_scene_error);
            flow_mark(staged_finish?"post.scene":"post.scene_unavailable",finish_scene_error.c_str());
            double finish_scene_seconds=0;
            std::array<std::string,4> models{{race.car(),race.car(),race.car(),race.car()}},labels;
            if(intro_grid) {
                unsigned actor=1;
                for(const auto& entry:intro_grid->actors)models[entry.config.role==Event0Role::human?0:actor++]=entry.assets.car_type_name;
            }
            require(load_race_car_labels(root,models,labels,error),error);
            const bool tournament_win=resolved_tournament.event&&resolved_tournament.round==supported_tournament_row(event_number)->count&&resolved_tournament.order()[0]==0;
            const bool event_won=resolved_tournament.event?tournament_win:race.session().player_won();
            const unsigned prize=event_won?event_prize:0;
            auto values=post_race_report(race.session(),result_stats.values(race.session(),labels[0]),labels,prize,previous_style,visual_reputation);
            if(resolved_tournament.event)bind_post_race_tournament(values,race.session(),resolved_tournament,
                configuration_places,configuration_to_actor,labels);
            const auto career_rewards=career_catalog&&career_profile?
                career_catalog->finish_rewards(*career_profile,event_won):CareerFinishRewards{};
            if(career_rewards.collect)values["CollectReward"]="1";
            // Native 496E00 changes the confirmation only for unique challenges.
            if(career_rewards.collect&&event_media&&!event_media->unique_categories.empty())
                values["ConfirmCollectReward"]="1";
            PostRaceStyleAnimator style_animator(values);FrontendClock post_clock;
            std::vector<std::pair<std::string,std::string>> rewards;
            if(career_catalog) {
                const auto earned=std::uint32_t(std::clamp<std::int64_t>(std::int64_t(previous_style)+
                    post_race_style_subtotal(race.session())*post_race_style_multiplier(visual_reputation),0,0xffffffffLL));
                const auto unlocked=career_catalog->style_messages(previous_high_water,std::max(previous_high_water,earned));
                //49E9D0 uses language6A765215 for style unlocks, including
                //losses. These are not mislabeled as a victory cash reward.
                for(const auto& message:unlocked)rewards.emplace_back(message,"Style Reward");
            }
            // Native 49ED00 queues style, magazine tasks, then victory and
            // the authored event rewards. Cover notifications live in this queue.
            for(const auto& message:career_rewards.magazine_tasks)rewards.emplace_back(message,"Style Reward");
            if(prize)rewards.emplace_back(values.at("RewardBlurbString"),"Race Victory Reward");
            if(event_won&&event_media)for(const auto& message:event_media->unlock_messages)
                rewards.emplace_back(message,"Race Victory Reward");
            std::printf("RACE_STYLE previous=%u high_water=%u subtotal=%lld multiplier=%u rewards=%lu\n",previous_style,previous_high_water,
                (long long)post_race_style_subtotal(race.session()),post_race_style_multiplier(visual_reputation),(unsigned long)rewards.size());
            std::vector<unsigned> statistic_actors;unsigned human_statistic=0;
            for(const auto& row:race.session().result_rows()) {
                if(!row.racer)human_statistic=unsigned(statistic_actors.size());
                statistic_actors.push_back(unsigned(row.racer));
            }
            PostRaceMenu menu(event_number!=0,event_number!=0 && race.session().style().total()!=0,
                unsigned(rewards.size()),unsigned(statistic_actors.size()),human_statistic);
            if(resolved_tournament.event)menu.tournament(resolved_tournament.round<supported_tournament_row(event_number)->count);
            menu.animate();
            const auto play_post_cue=[&](PostRaceCue cue) {
                using audio::FrontendSound;
                switch(cue) {
                case PostRaceCue::up:sound.frontend_sound(FrontendSound::up);break;
                case PostRaceCue::down:sound.frontend_sound(FrontendSound::down);break;
                case PostRaceCue::left:sound.frontend_sound(FrontendSound::left);break;
                case PostRaceCue::right:sound.frontend_sound(FrontendSound::right);break;
                case PostRaceCue::select:sound.frontend_sound(FrontendSound::select);break;
                case PostRaceCue::back:sound.frontend_sound(FrontendSound::back);break;
                case PostRaceCue::dialog:sound.frontend_sound(FrontendSound::dialog);break;
                default:break;
                }
            };
            FrontendUiFrame ui;
            bool sweep_results_presented=false,sweep_results_continued=false;
            const auto post_started=finish_sweep?armGetSystemTick():0;
            auto previous_post_page=PostRacePage::menu;bool first_post_frame=true;
            flow_mark("post.begin","original results flow");
            padUpdate(&pad); // Consume finish-frame button edges before menu input.
            post_shown=true;
            std::printf("RACE_POST menu=original stats=%s restart=full_session\n",race.session().is_drag()?"drag":race.session().is_drift()?"drift":"circuit");
            while(appletMainLoop()) {
                padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
                FrontendInput input;
                input.up=down&(HidNpadButton_Up|HidNpadButton_StickLUp);
                input.down=down&(HidNpadButton_Down|HidNpadButton_StickLDown);
                input.accept=down&HidNpadButton_A;
                input.back=down&(HidNpadButton_B|HidNpadButton_Plus);
                input.left=down&(HidNpadButton_Left|HidNpadButton_StickLLeft);
                input.right=down&(HidNpadButton_Right|HidNpadButton_StickLRight);
                if(finish_sweep) {
                    if(down&HidNpadButton_Plus)throw Cancel{};
                    require(ms(armGetSystemTick()-post_started)<30000,"finish sweep results timed out");
                    input=sweep_results_presented?campaign_finish_post_input(menu):FrontendInput{};
                }
                const double seconds=post_clock.sample(armTicksToNs(armGetSystemTick()))/1000000000.0;
                const auto previous_page=menu.page();const auto previous_racer=menu.racer();
                auto action=menu.advance(seconds);
                bool style_skip=false;
                if(menu.page()==PostRacePage::style) {
                    style_animator.advance(seconds);
                    if(input.accept&&!style_animator.done()) {style_animator.finish();input.accept=false;style_skip=true;}
                    style_animator.bind(values);
                }
                results_art.advance(float(seconds));
                if(action==PostRaceAction::none) {
                    action=menu.update(input);
                    play_post_cue(style_skip?PostRaceCue::select:menu.cue());
                    if(menu.dialog_sound())play_post_cue(PostRaceCue::dialog);
                }
                if(menu.statistics()&&(previous_page!=menu.page()||previous_racer!=menu.racer())) {
                    const auto actor=statistic_actors.at(menu.racer());
                    auto details=result_stats.values(race.session(),labels[actor],actor);
                    if(!race.session().is_drift())details["RaceFinishedPosition"]="0"+std::to_string(menu.racer()+1);
                    for(auto it=values.begin();it!=values.end();) {
                        const auto& key=it->first;
                        if(key.rfind("Data",0)==0||key.rfind("LapNumber_",0)==0||key.rfind("AllLapTimes",0)==0||
                           key.rfind("PositionAfterEachLap",0)==0)it=values.erase(it);else ++it;
                    }
                    for(const auto& value:details)values[value.first]=value.second;
                }
                if(menu.page()==PostRacePage::style)style_animator.bind(values);
                if(action!=PostRaceAction::none) {
                    if(finish_sweep) {
                        require(sweep_results_presented&&action==(menu.next_race()?PostRaceAction::next_race:PostRaceAction::leave),
                            "finish sweep selected an unexpected result action");
                        sweep_mark("post.continued",menu.next_race()?"action=next_race":"action=leave");
                        sweep_results_continued=true;
                    }
                    if(action==PostRaceAction::restart)result=RaceReturn::restart;
                    else if(action==PostRaceAction::restart_tournament)result=RaceReturn::restart_tournament;
                    else if(action==PostRaceAction::quit_tournament)result=RaceReturn::quit_tournament;
                    std::snprintf(flow_detail,sizeof flow_detail,"action=%u page=%u",unsigned(action),unsigned(menu.page()));
                    flow_mark("post.action",flow_detail);
                    // The original FE device survives panel changes. Our race
                    // device is about to close: preserve the final click tail
                    // while presenting the already resident last UI frame.
                    const auto cue_deadline=armGetSystemTick()+armNsToTicks(1000000000ULL);
                    while(sound.frontend_sound_active()&&armGetSystemTick()<cue_deadline&&appletMainLoop())
                        require(renderer.present_frontend(ui,error),error);
                    break;
                }
                if(menu.page()==PostRacePage::reward) {
                    const auto& reward=rewards.at(menu.reward_index());values["RewardBlurbString"]=reward.first;values["Victory_Title"]=reward.second;
                }
                else if(menu.page()==PostRacePage::style && career_catalog) {
                    const auto displayed=std::uint32_t(std::stoull(values.at("CareerStyleDisplayed")));
                    const auto unlocked=career_catalog->style_messages(previous_high_water,std::max(previous_high_water,displayed));
                    values["RewardBlurbString"]=unlocked.empty()?"":unlocked.back();
                }
                results_art.record_post_race(frame.commands.width,frame.commands.height,menu,values,ui);
                if(staged_finish) {
                    scene.set_finish_scene_seconds(finish_scene_seconds);
                    if(!scene.frame(race.session(),frame,finish_scene_error) ||
                       !renderer.update(frame.commands,finish_scene_error)) {
                        // Presentation assets must not discard a completed race.
                        flow_mark("post.scene_unavailable",finish_scene_error.c_str());
                        scene.clear_finish_scene();
                        staged_finish=false;
                        require(scene.frame(race.session(),frame,error),error);
                        require(renderer.update(frame.commands,error),error);
                    }
                    finish_scene_seconds+=seconds;
                }
                require(renderer.present_frontend(ui,error),error);
                if(finish_sweep&&!sweep_results_presented&&menu.page()==PostRacePage::results) {
                    require(!ui.vertices.empty()&&values.at("PlayerRow")=="1"&&values.at("Name1")==player_name,
                        "finish sweep results lack the first-place player row");
                    sweep_mark("post.results",format("player_row=1 vertices=%zu",ui.vertices.size()).c_str());
                    sweep_results_presented=true;
                }
                if(first_post_frame&&menu.page()==PostRacePage::results) {
                    const int voice=audio::post_race_speech(event_number,event_won);
                    if(voice>=0) {
                        std::string voice_error;
                        const bool played=sound.post_race_voice(root,event_number,event_won,voice_error);
                        std::printf("RACE_POST_VOICE event=%u request=%d played=%u error=%s\n",event_number,voice,unsigned(played),voice_error.c_str());
                    }
                }
                if(first_post_frame || previous_post_page!=menu.page()) {
                    std::snprintf(flow_detail,sizeof flow_detail,"page=%u quads=%lu",unsigned(menu.page()),(unsigned long)ui.additive.size());
                    flow_mark("post.presented",flow_detail);previous_post_page=menu.page();first_post_frame=false;
                }
            }
            if(finish_sweep&&!sweep_results_continued)throw Cancel{};
        }catch(const std::exception& e){if(finish_sweep)throw;failure=std::string("post-race: ")+e.what();flow_mark("post.failed",e.what());}
    }
    std::string close_error;
    if (!renderer.close(close_error)) {
        if (!failure.empty()) failure += " | ";
        failure += "renderer close: " + close_error;
    }
    if(spike_trace) {
        join_spike_gpu();
        std::printf("SPIKE_TRACE build=%s events=%zu dropped=%zu threshold_ms=20 capacity=64 bytes=%zu cpu_cadence=1 gpu_cadence=1 gpu_missing=-1 context=previous_current_next deferred_until_exit=1 extra_gpu_barriers=0\n",
            NFSU_RACE_BUILD_TAG,spike_trace->count,spike_trace->dropped,sizeof(*spike_trace));
        const char* time_names[]={"interval","work","sim","residency","record","update","present","acquire","fence","uniform","command","finish","submit","heap","gap","prior_tail","other"};
        const char* sim_names[]={"planner","drivers","rivals","player","pair","timing","traffic","props","prop_contacts"};
        const char* render_names[]={"cull","list","car","track","combine","road","visuals"};
        for(std::size_t i=0;i<spike_trace->count;++i)for(unsigned c=0;c<3;++c) {
            const auto& r=spike_trace->events[i].rows[c];if(!r.frame)continue;
            std::printf("SPIKE_FRAME event=%zu context=%u frame=%llu tick=%llu x=%.3f y=%.3f z=%.3f node=%d fraction=%.6f lap=%d steps=%u debt=%llu draws=%u awake=%u vertices=%u gpu_ms=%.6f",
                i,c,(unsigned long long)r.frame,(unsigned long long)r.tick,r.x,r.y,r.z,r.node,r.fraction,r.lap,r.steps,
                (unsigned long long)r.debt,r.draws,r.awake,r.vertices,r.gpu_ms);
            for(unsigned k=0;k<r.times.size();++k)std::printf(" %s_ms=%.6f",time_names[k],r.times[k]);
            for(unsigned k=0;k<r.simulation.size();++k)std::printf(" sim_%s_ms=%.6f",sim_names[k],r.simulation[k]);
            for(unsigned k=0;k<r.render.size();++k)std::printf(" render_%s_ms=%.6f",render_names[k],r.render[k]);
            std::puts("");
        }
    }
    if(!panel_mode && !profile_stats && !ab_enabled) {
        const auto& gpu=renderer.gpu_profile();
        const auto percentile=[&](unsigned n){const auto bin=gpu.scene.percentile_bin(n);
            return bin==ProfileDistribution::kOverflowBin?-1.0:(bin+1)*ProfileDistribution::kBinWidthMs;};
        std::printf("GPU_PROBE scene samples=%llu mean_ms=%.6f p95_bin_upper_ms=%.2f p99_bin_upper_ms=%.2f max_ms=%.6f first_tick=%llu last_tick=%llu worst_tick=%llu invalid=%llu dropped=%llu cadence=%u_presentations asynchronous=1 extra_pass_barriers=0 percentile_minus1=empty_or_overflow\n",
            (unsigned long long)gpu.scene.count(),gpu.scene.mean(),percentile(95),percentile(99),gpu.scene.max(),
            (unsigned long long)gpu.first_tick,(unsigned long long)gpu.last_tick,(unsigned long long)gpu.worst_tick,
            (unsigned long long)gpu.invalid,(unsigned long long)gpu.dropped,spike_probe?1U:60U);
    }
    if(ab_enabled) {
        std::ostringstream ab_serialized;ab_route.save(ab_serialized);
        const auto ab_text=ab_serialized.str();
        std::printf("AB_ROUTE identity=%016llx frames=%llu context=%s\n",
            (unsigned long long)ab_hash(ab_text.data(),ab_text.size()),
            (unsigned long long)ab_route.frames.size(),ab_route.context.c_str());
        bool saved=false;
        if(ab_record && failure.empty() && !ab_route.frames.empty()) {
            // Replace only the dedicated diagnostic recording, never retail data.
            std::ofstream out("sdmc:/switch/nfsu-ab-route.txt",std::ios::binary|std::ios::trunc);
            out<<ab_text;out.flush();saved=bool(out);
            if(!saved)failure="AB route write failed";
        }
        std::printf("AB_RESULT mode=%s frames=%llu expected=%llu valid=%u saved=%u cancelled=%u error=%s\n",
            ab_record?"record":"replay",(unsigned long long)presented,
            (unsigned long long)ab_route.frames.size(),unsigned(!ab_route.frames.empty() && failure.empty()&&!ab_cancelled &&
                (!ab_replay || presented==ab_route.frames.size())),unsigned(saved),unsigned(ab_cancelled),failure.c_str());
        std::printf("AB_INSTRUMENTATION mode=%s state_camera_check=on route_replay=on total_gpu_pair=on\n",ab_minimal?"minimal":"detailed");
        std::puts(ab_minimal?"AB_CPU_COLUMNS frame,tick,triangles,ticks,track,car,glow,active,work_ms,interval_ms":"AB_CPU_COLUMNS frame,tick,triangles,ticks,track,car,glow,active,work_ms,interval_ms,sim_ms,residency_ms,record_ms,update_ms,present_ms,acquire_ms,fence_ms,uniform_ms,command_ms,finish_ms,submit_ms,heap_ms");
        for(const auto& r:ab_cpu) {
            std::printf("AB_CPU %llu,%llu,%llu,%u,%u,%u,%u,%u",(unsigned long long)r.frame,
                (unsigned long long)r.tick,(unsigned long long)r.triangles,r.ticks,r.track,r.car,r.glow,r.active);
            for(std::size_t i=0;i<(ab_minimal?2:r.ms.size());++i)std::printf(",%.6f",r.ms[i]);
            std::putchar('\n');
        }
        renderer.dump_ab_trace();std::fflush(stdout);
    }
    if (profile_stats) {
        // close() drains the last asynchronous GPU reports before logging.
        static constexpr const char* names[] = {
            "simulation", "simulation_tick", "residency", "record_total",
            "cull_camera", "list", "car", "track", "combine",
            "update_total", "update_validation", "uniform_write",
            "present_total", "acquire", "fence", "command_record", "finish_list", "submit", "present_other",
            "mallinfo", "parent_sum", "other", "road_reflections", "race_visuals"
        };
        static_assert(sizeof(names)/sizeof(names[0])==FrameProfileStats::kStages);
        std::printf("RUN_PROFILE build=%s event=%lu free_drive=%u frames=%llu CPU_us_timers=on GPU_scene_pair=on; child_stages_overlap_parents\n",
            NFSU_RACE_BUILD_TAG,(unsigned long)event_number,unsigned(free_drive),
            (unsigned long long)profile_stats->count());
        const auto distribution=[](const char* kind,const char* name,const ProfileDistribution& d) {
            const auto percentile=[&](unsigned n) {
                const auto bin=d.percentile_bin(n);
                return bin==ProfileDistribution::kOverflowBin ? std::string(">=100") :
                    format("%.2f",(bin+1)*ProfileDistribution::kBinWidthMs);
            };
            std::printf("RUN_PROFILE %s %s samples=%llu mean_ms=%.6f p95_ms=%s p99_ms=%s max_ms=%.6f\n",
                kind,name,(unsigned long long)d.count(),d.mean(),percentile(95).c_str(),percentile(99).c_str(),d.max());
        };
        for(std::size_t i=0;i<FrameProfileStats::kStages;++i)
            distribution("CPU",names[i],profile_stats->stage(static_cast<FrameProfileStage>(i)));
        const auto& gpu=renderer.gpu_profile();
        distribution("GPU","scene",gpu.scene);
        const auto worst=profile_stats->worst();
        std::printf("RUN_PROFILE CPU_worst_tick=%llu CPU_worst_track=%u CPU_worst_car=%u GPU_first_tick=%llu GPU_last_tick=%llu GPU_worst_tick=%llu GPU_worst_track=%u GPU_worst_car=%u GPU_invalid=%llu GPU_dropped=%llu\n",
            (unsigned long long)worst.tick,worst.track_draws,worst.car_draws,
            (unsigned long long)gpu.first_tick,(unsigned long long)gpu.last_tick,
            (unsigned long long)gpu.worst_tick,gpu.worst_track_draws,gpu.worst_car_draws,
            (unsigned long long)gpu.invalid,(unsigned long long)gpu.dropped);
        std::fflush(stdout);
    }
    std::puts(race.difficulty_line().c_str());
    if (!failure.empty()) { std::printf("ERROR: %s\n",failure.c_str()); std::fflush(stdout); }
    if (free_drive && !failure.empty()) throw std::runtime_error(failure);

    // The original introductory victory does not open the ordinary results UI.
    // Explicit profiling still owns its diagnostic result page.
    if (appletMainLoop() && profile_stats && !free_drive && (!post_shown || !failure.empty())) {
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
    if(career_report&&(failure.empty()||post_race_result_ready(player_result))) {
        career_report->started=true;
        career_report->won=!pause_exit&&post_race_result_ready(player_result)&&race.session().player_won();
        career_report->totalled=player_result.status==RacerStatus::totalled;
        career_report->metres=result_stats.player_metres();
        if(!pause_exit&&post_race_result_ready(player_result))
            career_report->style=post_race_style_subtotal(race.session())*post_race_style_multiplier(visual_reputation);
    }
    if(!pause_exit && post_race_result_ready(player_result) && result!=RaceReturn::restart &&
       result!=RaceReturn::restart_tournament && result!=RaceReturn::quit_tournament) {
        // A terminal knockout/blown-engine result cannot award first-place
        // career cash even if its last live progress rank was still1.
        if(completed_places)*completed_places=configuration_places;
        //The career completion seam uses2 for a failed solo challenge. Keep
        //that failure separate from the original single racer's actual place.
        if(completed_place)*completed_place=race.session().player_won()?1U:std::max(race.session().player_position(),2U);
        if(completed_style)*completed_style=post_race_style_subtotal(race.session())*post_race_style_multiplier(visual_reputation);
    }
    std::snprintf(flow_detail,sizeof flow_detail,"result=%u place=%u style=%d post_shown=%u failure=%u",
        unsigned(result),completed_place?*completed_place:0,completed_style?*completed_style:0,unsigned(post_shown),unsigned(!failure.empty()));
    if(post_art)flow_mark("race.return",flow_detail);
    if(flow_journal_open && !flow_journal.close())std::fprintf(stderr,"CAREER_FLOW journal close: %s\n",flow_journal.error());
    // A failed post-race screen is an error, not a cancelled career race.
    if(post_art && !failure.empty())throw std::runtime_error(failure);
    return failure.empty()?result:RaceReturn::menu;
}
#ifdef NFSU_SWITCH_CAMPAIGN_SWEEP
#include "campaign_sweep.inc"
#endif
} // namespace
int main(int argc, char** argv) {
    LanSocket nxlink_socket;
    std::string socket_error;
    const int link=__nxlink_host.s_addr&&nxlink_socket.open(0,socket_error)?nxlinkStdio():-1;
    std::printf("NFSU_BUILD tag=%s nxlink=%u\n",NFSU_RACE_BUILD_TAG,unsigned(link>=0));std::fflush(stdout);
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
            bool hud_trace = false, nos_self_check=false;
#ifdef NFSU_RACE_FPS_OVERLAY
            bool fps_overlay = true;
#else
            bool fps_overlay = false;
#endif
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
#ifdef NFSU_SWITCH_CAMPAIGN_SWEEP
            unsigned sweep_event=~0U;
#endif
            RaceSteering steering=RaceSteering::baseline; bool steering_seen=false;
            std::string car_name = "240SX"; bool car_seen = false;
            for (int i = 1; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--replay") { replay = true; selected = true; }
                else if (arg == "--live") { replay = false; selected = true; }
#ifdef NFSU_SPEED_BLUR_PROBE
                else if(arg=="--nos-self-check") {nos_self_check=true;replay=false;selected=true;}
#endif
#ifdef NFSU_SWITCH_CAMPAIGN_SWEEP
                else if(arg=="--sweep-event"&&i+1<argc) {
                    char* end=nullptr;const char* input=argv[++i];
                    const auto number=std::strtol(input,&end,10);
                    require(sweep_event==~0U&&end!=input&&!*end&&number>=0&&number<=kCareerLastEvent,
                            "--sweep-event needs one campaign event number");
                    sweep_event=unsigned(number);
                }
#endif
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
                else if (arg == "--ab-record") { ab_record=true;profile_frame=true; }
                else if (arg == "--ab-replay") { ab_replay=true;profile_frame=true; }
                else if (arg == "--ab-minimal") { ab_replay=true;ab_minimal=true;profile_frame=true; }
                else if (arg == "--frontend-startup-trace") startup_trace=true;
                else if (arg == "--fps-overlay") fps_overlay=true;
                else if (arg == "--no-fps-overlay") fps_overlay=false;
                else if (arg == "--hud-trace") hud_trace = true;
                else if (arg == "--event" && i+1<argc) {
                const std::string value=argv[++i];
                require(!event_seen,"duplicate --event");
                char* end=nullptr;
                const long parsed=std::strtol(value.c_str(),&end,10);
                require(end && *end=='\0' && parsed>0 && parsed<1000 &&
                       nfsu::is_supported_event(static_cast<std::uint32_t>(parsed)),
                       "--event needs one supported circuit event: 1, 8, 16, 19, 32, 35, 37, 45, 52, 53, 59, 71, 75, 81, 87, 89, 96, 99, 104, 107, 111, 113, 120, 121, 122 - or one of the drift events 3, 13, 18, 25, 28, 33, 40, 44, 50, 56, 64, 65, 73, 86, 90, 93, 105, 109 - or one of the drag events 4, 11, 17, 21, 29, 36, 42, 46, 51, 55, 62, 63, 70, 76, 79, 83, 88, 98, 106, 108 - or one of the sprint events 2, 12, 22, 23, 27, 34, 41, 43, 54, 57, 61, 67, 74, 78, 82, 94, 95, 100, 101, 110 - or one of the lap-knockout events 5, 10, 24, 31, 39, 49, 58, 60, 72, 84, 91, 103 - or one of the time-trial events 7, 15, 26, 47, 68, 77, 97");
                event_seen=true; event_number=static_cast<std::uint32_t>(parsed);
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
                else throw std::runtime_error("Usage: nfsu_switch_race.nro [sdmc:/nfsu] [--live|--replay] [--event N] [--panel|--profile-frame] [--hud-trace] [--fps-overlay|--no-fps-overlay] [--no-hud] [--sky on|off|early] [--scene-height 720|540|450|360] [--gpu-boost on|off] [--particles=on|--particles=off] [--tuned] [--glows|--glows=off] [--car NAME] [--car-shading on|off] [--texture-filter legacy|linear] [--wet-road-reflections on|off]");
            }
            require(event_number == 1 || selected, "--event other than 1 requires --live");
            require(!(ab_record && ab_replay),"choose one AB mode");
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
            require(!fps_overlay || (hud && !panel && !ab_record && !ab_replay),
                    "FPS overlay requires direct HUD and no AB measurement");
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
            if(nos_self_check) {
                require(event_number==1&&!panel&&!replay&&!ab_record&&!ab_replay,"NOS self-check requires live event1 presentation");
                performance.nitrous=3;explicit_performance=true;
            }
#ifdef NFSU_SWITCH_CAMPAIGN_SWEEP
            require(sweep_event==~0U||!selected,"--sweep-event requires campaign sweep mode");
            if(!selected) {
                require(!panel&&!profile_frame&&!ab_record&&!ab_replay&&hud,
                        "campaign sweep requires ordinary race presentation and HUD");
                run_campaign_sweep(root,screen,pad,particles_enabled,sky,glows,wet_road_reflections,
                    texture_filter,scene_height,gpu_boost,steering,sweep_event);
            } else
#endif
            if (selected) {
                run(root,replay,panel,profile_frame,particles_enabled,sky,
                    tuned,glows,car_shading,{true,false,wet_road_reflections,false},texture_filter,scene_height,
                    gpu_boost,hud,hud_trace,fps_overlay,explicit_performance ? &performance : nullptr,
                    handling_telemetry,steering,selected_car,screen,pad,false,nullptr,event_number,
                    nullptr,1,nullptr,0,0,nullptr,nullptr,0,nullptr,0,0,nullptr,nullptr,nos_self_check);
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
                LoadingScreen frontend_loading(screen,root,!profile_frame);
                FrontendArt art; std::string frontend_error;
                screen.show({"Loading original frontend..."});
                trace("art.load.before");
                require(art.load_startup(root,frontend_error,true),frontend_error);
                frontend_loading.finish();
                const auto show_startup_splash=[&] {
                    trace("splash.before");
                    screen.startup_splash(art);
                    padUpdate(&pad);
                    while(appletMainLoop()) {
                        if(appletGetFocusState()==AppletFocusState_InFocus) {
                            padUpdate(&pad);
                            const auto pressed=padGetButtonsDown(&pad);
                            if(pressed&HidNpadButton_B)throw Cancel{};
                            if(pressed&(HidNpadButton_A|HidNpadButton_Plus)) {
                                trace("splash.after");
                                return;
                            }
                        }
                        svcSleepThread(16666667);
                    }
                    throw Cancel{};
                };
                show_startup_splash();
                bool post_race_loaded=false;
                const auto load_post_race=[&] {
                    if(post_race_loaded)return;
                    require(art.load_post_race(root,frontend_error),frontend_error);
                    post_race_loaded=true;
                };
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
                    // Garage/menu geometry uses the native portable target.
                    // Race keeps its independent scene_height policy below.
                    constexpr std::uint32_t menu_height=720;
                    std::printf("MENU POLICY gpu_boost=%s requested_height=%lu effective_height=%lu (configuration, not measured clocks)\n",
                        gpu_boost?"on":"off",static_cast<unsigned long>(scene_height),static_cast<unsigned long>(menu_height));
                    trace("screen.close.before");screen.close();trace("screen.close.after");
                    frontend_renderer=std::make_unique<DekoRenderer>();
                    trace("renderer.open.before");
                    require(frontend_renderer->open_frontend(frontend_scene.commands(),art.atlas(),frontend_error,menu_height,profile_frame && !ab_minimal,active_trace,frontend_scene.draw_capacity()),frontend_error);
                    trace("renderer.ready");
                };
                open_frontend();
                FrontendClock frontend_clock;
                GarageFpsDisplay garage_fps;garage_fps.visible=fps_overlay;
                FrontendAppletClockHook frontend_clock_hook(frontend_clock,frontend_presentation_stats,garage_fps);
                const auto garage_fps_chord=[&] {
                    const auto held=padGetButtons(&pad);
                    return garage_fps.chord.update(padIsConnected(&pad),
                        (held&HidNpadButton_StickL)!=0,(held&HidNpadButton_StickR)!=0);
                };
                FrontendMenu menu(true,true,true);
                CareerInfoMenu statistics;
                std::optional<CareerCatalog> statistics_catalog;
                CareerProfile statistics_profile;
                auto presentation=read_presentation_options(wet_road_reflections);
                if(wet_road_seen)presentation.road_reflections=wet_road_reflections;
                bool options_open=false;
                unsigned option_row=0;
                std::string option_notice;
                audio::FrontendMusic music;
                std::uint32_t career_hints=0;
                const auto play_career_hint=[&](int group) {
                    const auto bit=1U<<unsigned(group-62);
                    if(career_hints&bit)return;
                    career_hints|=bit;
                    std::string speech_error;
                    if(!music.speech(group,speech_error))
                        std::fprintf(stderr,"CAREER hint %d failed: %s\n",group,speech_error.c_str());
                };
                trace("music.start.before");
                if(!music.start(root,frontend_error)) menu.notice("MUSIC: "+frontend_error);
                trace("music.start.after");
                if (intro.outcome==IntroOutcome::error) {
                    std::printf("INTRO ERROR: %s\n",intro.error.c_str());
                    menu.notice(std::string("INTRO: ")+intro.error);
                }
                CareerProfiles profiles("sdmc:/switch/nfsu-saves");
                std::optional<CareerProfileEntry> active_profile;
                const auto choose_profile=[&](bool startup=false) {
                    CareerCatalog catalog;require(catalog.load(root,frontend_error),frontend_error);
                    std::vector<CareerProfileEntry> entries;unsigned selected=0;
                    require(profiles.list(entries,selected,frontend_error),frontend_error);
                    std::string notice;
                    padUpdate(&pad);
                    while(appletMainLoop()) {
                        if(appletGetFocusState()!=AppletFocusState_InFocus){frontend_clock.resume();continue;}
                        const auto elapsed=frontend_clock.sample(armTicksToNs(armGetSystemTick()));
                        require(frontend_scene.frame(float(elapsed/1000000000.0),frontend_clock.original_ticks(),frontend_error),frontend_error);
                        art.advance(float(elapsed/1000000000.0));
                        require(frontend_renderer->update(frontend_scene.commands(),frontend_error),frontend_error);
                        art.record_profiles(width,height,entries,selected,notice,frontend_ui);
                        require(frontend_renderer->present_frontend(frontend_ui,frontend_error),frontend_error);
                        padUpdate(&pad);const auto pressed=padGetButtonsDown(&pad);
                        if(startup&&(pressed&HidNpadButton_Plus))throw Cancel{};
                        if(pressed&(HidNpadButton_B|HidNpadButton_Plus)){music.sound(audio::FrontendSound::back);return false;}
                        if(!entries.empty()) {
                            if(pressed&(HidNpadButton_Up|HidNpadButton_StickLUp)){selected=(selected+unsigned(entries.size())-1)%unsigned(entries.size());music.sound(audio::FrontendSound::up);}
                            if(pressed&(HidNpadButton_Down|HidNpadButton_StickLDown)){selected=(selected+1)%unsigned(entries.size());music.sound(audio::FrontendSound::down);}
                        }
                        if((pressed&HidNpadButton_X)||((pressed&HidNpadButton_A)&&entries.empty())) {
                            SwkbdConfig keyboard{};const auto created=swkbdCreate(&keyboard,0);
                            if(R_FAILED(created)){notice="Keyboard unavailable. Launch through title override.";continue;}
                            swkbdConfigMakePresetDefault(&keyboard);
                            swkbdConfigSetHeaderText(&keyboard,"Create Profile");
                            swkbdConfigSetSubText(&keyboard,"Start a separate Underground career.");
                            swkbdConfigSetGuideText(&keyboard,"Enter a name (1-12 characters)");
                            swkbdConfigSetOkButtonText(&keyboard,"Create");
                            swkbdConfigSetStringLenMin(&keyboard,1);swkbdConfigSetStringLenMax(&keyboard,12);
                            swkbdConfigSetTextCheckCallback(&keyboard,[](char* text,std::size_t bytes) {
                                if(CareerProfiles::valid_name(text))return SwkbdTextCheckResult_OK;
                                std::snprintf(text,bytes,"Use 1-12 ASCII characters without outer spaces.");return SwkbdTextCheckResult_Bad;
                            });
                            char name[64]{};const auto result=swkbdShow(&keyboard,name,sizeof name);swkbdClose(&keyboard);
                            frontend_clock.resume();frontend_presentation_stats.reset();padUpdate(&pad);
                            if(R_SUCCEEDED(result)) {
                                CareerProfileEntry entry;
                                if(profiles.create(name,catalog,entry,frontend_error)) {
                                    active_profile=entry;music.sound(audio::FrontendSound::select);return true;
                                }
                                notice=frontend_error;
                            }
                            continue;
                        }
                        if((pressed&HidNpadButton_A)&&!entries.empty()) {
                            CareerProfile probe;
                            if(!load_career(entries[selected].base,catalog,probe,frontend_error)||!profiles.select(entries[selected].id,frontend_error)){notice=frontend_error;continue;}
                            active_profile=entries[selected];music.sound(audio::FrontendSound::select);return true;
                        }
                    }
                    return false;
                };
                trace("profile.before");
                while(!choose_profile(true)) {
                    if(!appletMainLoop())throw Cancel{};
                    require(frontend_renderer->close(frontend_error),frontend_error);
                    frontend_renderer.reset();frontend_gpu_profile.reset();
                    show_startup_splash();
                    open_frontend();
                    frontend_clock.resume();
                }
                art.release_startup_splash();
                trace("profile.after");
                frontend_clock.resume();
                frontend_presentation_stats.reset();
                bool stick_up=false,stick_down=false;
                std::optional<CareerProfile> free_profile;
                std::map<std::string,CareerRide> free_rides;
                unsigned free_course=0;
                QuickRaceOptions quick_options;
                FrontendAction pending=FrontendAction::none;
                const auto run_career_action=[&](FrontendAction action,const std::string* garage_car,
                        unsigned garage_tier,const std::function<void()>& lobby_poll) {
                        // The welcome is an overlay on the resident main menu.
                        // Keep the GPU/atlas alive until accepting the career transition.
                        bool career_scene_changed=false;
                        try {
                            {
                                const bool sandbox=action==FrontendAction::free_drive;
                                CareerCatalog career;CareerProfile profile;
                                require(sandbox?career.load_free_drive(root,frontend_error):career.load(root,frontend_error),frontend_error);
                                // Unrestricted ordinary practice cars do not unlock bonus presets.
                                // Read the active real profile, never the sandbox's all-parts marker.
                                std::uint32_t earned_style=0;
                                if(sandbox&&!garage_car&&active_profile) {
                                    CareerCatalog saved_catalog;CareerProfile saved;
                                    require(saved_catalog.load(root,frontend_error),frontend_error);
                                    require(load_career(active_profile->base,saved_catalog,saved,frontend_error),frontend_error);
                                    earned_style=saved.style_high_water;
                                }
                                // Port profiles are separate from retail assets and PC saves.
                                const std::filesystem::path save_base=sandbox?std::filesystem::path{}:active_profile->base;
                                const auto select_quick_car=[&](const std::string& identity,CareerProfile& next) {
                                    const auto saved=free_rides.find(identity);
                                    require(career.quick_profile(identity,earned_style,next,frontend_error,
                                        saved==free_rides.end()?nullptr:&saved->second),frontend_error);
                                };
                                if(sandbox) {
                                    if(garage_car){
                                        require(career.free_profile(*garage_car,profile,frontend_error),frontend_error);
                                        const auto saved=free_rides.find(*garage_car);
                                        if(saved!=free_rides.end())profile.ride=saved->second;
                                        profile.ride.performance.fill(garage_tier);
                                        profile.ride.unique.fill(false);
                                        profile.page=CareerPage::garage;
                                    }else{
                                        if(free_profile) {
                                            profile=*free_profile;
                                            if(const auto* bonus=career.bonus_car(profile.bonus_preset);bonus&&bonus->points>earned_style)
                                                select_quick_car(profile.car,profile);
                                        }
                                        else require(career.free_profile("SKYLINE",profile,frontend_error),frontend_error);
                                        profile.page=CareerPage::menu;
                                    }
                                }else {
                                    require(load_career(save_base,career,profile,frontend_error),frontend_error);
                                    profile.name=active_profile->name;
                                }
                                const auto store=[&](const CareerProfile& next) {
                                    if(!sandbox)require(save_career(save_base,career,next,frontend_error),frontend_error);
                                    profile=next;
                                    if(sandbox){
                                        auto ride=profile.ride;
                                        if(garage_car){
                                            // Performance is a host lobby rule, not a garage edit.
                                            const auto saved=free_rides.find(profile.car);
                                            ride.performance=saved==free_rides.end()?CareerRide{}.performance:saved->second.performance;
                                            ride.unique=saved==free_rides.end()?CareerRide{}.unique:saved->second.unique;
                                        }else free_profile=profile;
                                        if(profile.bonus_preset.empty())free_rides[profile.car]=ride;
                                    }
                                    std::printf("CAREER saved page=%u car=%s bank=%lu event=%u\n",unsigned(profile.page),
                                        profile.car.c_str(),static_cast<unsigned long>(profile.bank),profile.selected_event);
                                    std::fflush(stdout);
                                };
                                const auto movie_poll=[&] {
                                    if(!appletMainLoop())return IntroControl::quit;
                                    padUpdate(&pad);const auto pressed=padGetButtonsDown(&pad);
                                    if(pressed&HidNpadButton_Plus)return IntroControl::quit;
                                    if(pressed&(HidNpadButton_A|HidNpadButton_B))return IntroControl::skip;
                                    return IntroControl::continue_playback;
                                };
                                const auto movie_present=[&](const MovieFrame& frame){screen.movie(frame);};
                                bool intro_accepted=profile.page!=CareerPage::intro;
                                if(profile.page==CareerPage::intro) {
                                    frontend_clock.resume();double welcome_seconds=0;bool exit_selected=false,stick_was_horizontal=false;
                                    try {
                                        while(appletMainLoop()) {
                                            if(appletGetFocusState()!=AppletFocusState_InFocus){frontend_clock.resume();continue;}
                                            padUpdate(&pad);const auto pressed=padGetButtonsDown(&pad);
                                            if(pressed&(HidNpadButton_B|HidNpadButton_Plus))break;
                                            const auto stick=padGetStickPos(&pad,0);const bool horizontal=stick.x>16000||stick.x<-16000;
                                            if((pressed&(HidNpadButton_Left|HidNpadButton_Right)) || (horizontal&&!stick_was_horizontal))exit_selected=!exit_selected;
                                            stick_was_horizontal=horizontal;
                                            if(pressed&HidNpadButton_A){intro_accepted=!exit_selected;break;}
                                            const auto elapsed=frontend_clock.sample(armTicksToNs(armGetSystemTick()));
                                            require(frontend_scene.frame(static_cast<float>(elapsed/1000000000.0),frontend_clock.original_ticks(),frontend_error),frontend_error);
                                            welcome_seconds+=elapsed/1000000000.0;
                                            art.record_underground_welcome(width,height,exit_selected,frontend_ui,static_cast<unsigned>(std::min(600.0,welcome_seconds*1200)));
                                            require(frontend_renderer->update(frontend_scene.commands(),frontend_error),frontend_error);
                                            require(frontend_renderer->present_frontend(frontend_ui,frontend_error),frontend_error);
                                        }
                                    }catch(...) {
                                        if(frontend_renderer){std::string cleanup;frontend_renderer->close(cleanup);frontend_renderer.reset();frontend_gpu_profile.reset();}
                                        throw;
                                    }
                                    std::printf("CAREER welcome accepted=%u\n",unsigned(intro_accepted));std::fflush(stdout);
                                }
                                if(profile.page!=CareerPage::intro || intro_accepted) {
                                    if(intro_accepted && profile.page==CareerPage::intro)music.stop();
                                    if(frontend_renderer)require(frontend_renderer->close(frontend_error),frontend_error);
                                    frontend_renderer.reset();frontend_gpu_profile.reset();
                                }
                                if(profile.page==CareerPage::intro && intro_accepted) {
                                    const auto prologue=play_underground_prologue(root,movie_poll,movie_present);
                                    if(prologue.outcome==IntroOutcome::quit)throw Cancel{};
                                    require(prologue.outcome!=IntroOutcome::error,prologue.error);
                                    LoadingScreen intro_loading(screen,root,!profile_frame,screen.tips.prologue());
                                    load_post_race();
                                    RaceReturn intro_result;
                                    do { intro_result=run(root,false,false,profile_frame,particles_enabled,sky,
                                        false,glows,true,presentation,texture_filter,scene_height,
                                        gpu_boost,hud,false,fps_overlay,nullptr,handling_telemetry,steering,
                                        selected_car,screen,pad,false,nullptr,0,&art,1,nullptr,0,0,nullptr,nullptr,
                                        0,nullptr,0,0,nullptr,nullptr,false,profile.name);
                                    } while(intro_result==RaceReturn::restart && appletMainLoop());
                                    if(intro_result==RaceReturn::intro_won) {
                                        auto next=profile;require(career.intro_won(next,frontend_error),frontend_error);store(next);
                                    }
                                }
                                if(profile.page!=CareerPage::intro) {
                                    require(art.load_career_pages(root,career,frontend_error),frontend_error);
                                    bool leave=false,career_music=music.playing(),browser_loaded=false;
                                    unsigned selection=0,map_prepared_event=0;std::string resident_car;
                                    bool choose_quick_mode=sandbox;
                                    std::vector<unsigned> quick_courses;
                                    CareerInfoMenu career_info;
                                    CareerGarage garage;garage.reset(career,profile,garage_car!=nullptr);
                                    std::vector<unsigned char> garage_globals;std::vector<RaceTableIssue> garage_issues;
                                    require(load_race_image(std::filesystem::path(root)/"Global/GLOBALB.BUN",garage_globals,garage_issues),"garage GLOBALB unavailable");
                                    std::string garage_signature,garage_resident_base;
                                    std::vector<unsigned> garage_preview_indices;
                                    GarageSceneLoad garage_load;std::unique_ptr<FrontendGpuScene> retired_garage;
                                    const auto close_career=[&] {
                                        garage_load.join();garage_load.scene_.reset();
                                        frontend_scene.release_preparation_cache();
                                        if(frontend_renderer) {
                                            require(frontend_renderer->close(frontend_error),frontend_error);
                                            frontend_renderer.reset();frontend_gpu_profile.reset();
                                        }
                                        retired_garage.reset();
                                    };
                                    const auto prepare_garage=[&] {
                                        // A bounded resident component bank. No race loading screen or
                                        // music stop is entered for a garage selection.
                                        const auto signature=garage.preview_signature(profile);
                                        const auto installed_base=CareerGarage::installed_signature(profile);
                                        const bool reuse_base=!garage.previewing()&&!garage_signature.empty()&&
                                            installed_base==garage_resident_base&&resident_car==profile.car;
                                        frontend_scene.focus_part(garage.focus_slot());
                                        frontend_scene.road_target(garage.road_target());
                                        if(garage_load.ready()) {
                                            garage_load.join();
                                            require(garage_load.error_.empty(),garage_load.error_);
                                            if(garage_load.signature()==signature) {
                                                const auto publish_started=armGetSystemTick();
                                                retired_garage=std::move(garage_load.scene_);
                                                const auto selected=garage.previewing()&&garage.preview_index()<garage_load.indices_.size()?
                                                    garage_load.indices_[garage.preview_index()]:0;
                                                require(frontend_scene.adopt_garage(*retired_garage,frontend_error,garage.page()==GaragePage::trade_in?0:selected),frontend_error);
                                                if(garage.page()==GaragePage::trade_in)require(frontend_scene.select_car(selected,frontend_error),frontend_error);
                                                if(frontend_renderer)require(frontend_renderer->replace_frontend(frontend_scene.commands(),frontend_scene.draw_capacity(),frontend_error),frontend_error);
                                                retired_garage.reset();
                                                garage_signature=signature;garage_resident_base=garage_load.installed_;
                                                garage_preview_indices=std::move(garage_load.indices_);resident_car=garage_load.car_;
                                                browser_loaded=false;career_scene_changed=true;
                                                std::printf("GARAGE_BANK ready entries=%lu selected=%u prepare_ms=%.3f GPU_bytes=%llu publish_ms=%.3f\n",(unsigned long)garage_preview_indices.size(),selected,garage_load.prepare_ms_,
                                                    (unsigned long long)(frontend_renderer?frontend_renderer->gpu_bytes():0),
                                                    armTicksToNs(armGetSystemTick()-publish_started)/1000000.0);
                                            }else garage_load.scene_.reset();
                                        }
                                        if(!reuse_base&&signature!=garage_signature&&!garage_load.running()) {
                                            std::vector<CareerRideSetup> variants;std::vector<unsigned> indices;
                                            require(garage.prepare_previews(garage_globals,career,profile,variants,indices,frontend_error),frontend_error);
                                            require(garage_load.start(root,width,height,std::move(variants),signature,installed_base,profile.car,std::move(indices),frontend_scene.environment(),resident_car==profile.car?frontend_scene.car_source():nullptr,frontend_error),frontend_error);
                                        }
                                        if(signature==garage_signature||reuse_base) {
                                            if(garage.previewing()&&garage.preview_index()<garage_preview_indices.size()) {
                                                const auto variant=garage_preview_indices[garage.preview_index()];
                                                require(garage.page()==GaragePage::trade_in?frontend_scene.select_car(variant,frontend_error):frontend_scene.select_part(variant,frontend_error),frontend_error);
                                            }
                                            else require(frontend_scene.select_part(0,frontend_error),frontend_error);
                                        }
                                        if(!frontend_renderer)open_frontend();
                                    };
                                    try {
                                        CareerMagazineRecipe magazine_recipe;
                                        while(!leave && appletMainLoop()) {
                                            const auto movie=career.pending_movie(profile);
                                            if(!movie.empty()) {
                                                close_career();music.stop();career_music=false;
                                                const auto result=play_career_movie(root,movie,movie_poll,movie_present);
                                                require(result.outcome!=IntroOutcome::error,result.error);
                                                if(result.outcome==IntroOutcome::quit)break;
                                                auto next=profile;require(career.movie_finished(next,frontend_error),frontend_error);store(next);
                                                selection=0;continue;
                                            }
                                            if(profile.page==CareerPage::magazines){
                                                require(!profile.magazine_covers.empty(),"magazine collection is empty");
                                                selection%=unsigned(profile.magazine_covers.size());const auto& cover=profile.magazine_covers[selection];
                                                auto seed=cover.seed;require(career.magazines().make(CareerMagazineCatalog::key(cover.index),seed,magazine_recipe,frontend_error),frontend_error);
                                                auto photo=profile;photo.car=cover.car;photo.ride.installed=cover.installed;photo.ride.performance=cover.performance;photo.ride.unique=cover.unique;
                                                CareerRideSetup setup;require(resolve_career_ride(garage_globals,career,photo,setup,frontend_error),frontend_error);
                                                close_career();
                                                require(frontend_scene.load_garage(root,width,height,{setup},frontend_error,nullptr,frontend_scene.environment()),frontend_error);
                                                frontend_scene.magazine_camera(magazine_recipe.camera);
                                                require(art.load_magazine(root,magazine_recipe,frontend_error),frontend_error);
                                                map_prepared_event=0;
                                                resident_car=profile.car;career_scene_changed=true;
                                            }
                                            const auto car=profile.page==CareerPage::choose_car?
                                                career.starters().at(selection).name:profile.car;
                                            // A confirmed Trade In already displays the incoming car.
                                            // Keep that live scene/music while its durable base is prepared.
                                            const bool live_trade=profile.page==CareerPage::garage&&frontend_renderer&&!garage_signature.empty();
                                            if(!sandbox && profile.page!=CareerPage::choose_car && resident_car!=car && !car.empty()&&!live_trade) {
                                                //The first Underground frame must already own the saved
                                                //ride, including paints and installed parts. Do not show
                                                //a stock-car placeholder while the garage job prepares.
                                                close_career();
                                                CareerRideSetup installed;
                                                require(resolve_career_ride(garage_globals,career,profile,installed,frontend_error),frontend_error);
                                                require(frontend_scene.load_garage(root,width,height,{installed},frontend_error,nullptr,frontend_scene.environment()),frontend_error);
                                                resident_car=car;browser_loaded=false;
                                                garage_resident_base=CareerGarage::installed_signature(profile);
                                                garage_signature=garage.previewing()?std::string{}:garage.preview_signature(profile);
                                                career_scene_changed=true;
                                            }
                                            if(profile.page==CareerPage::unique_reward)garage.reset_unique(career,profile);
                                            if(sandbox||(profile.page==CareerPage::garage||profile.page==CareerPage::unique_reward))prepare_garage();
                                            else if(resident_car!=car && !browser_loaded) {
                                                close_career();
                                                if(profile.page==CareerPage::choose_car) {
                                                    std::vector<std::string> roster;
                                                    for(const auto& entry:career.starters())roster.push_back(entry.name);
                                                    require(frontend_scene.load_browser(root,width,height,roster,frontend_error),frontend_error);
                                                    browser_loaded=true;
                                                } else require(frontend_scene.load(root,width,height,frontend_error,car),frontend_error);
                                                resident_car=car;career_scene_changed=true;
                                            }
                                            if(profile.page==CareerPage::map&&map_prepared_event!=profile.selected_event){
                                                require(art.load_career_event(root,profile.selected_event,frontend_error),frontend_error);
                                                if(frontend_renderer)require(frontend_renderer->set_frontend_overlay(art.atlas(),frontend_error),frontend_error);
                                                map_prepared_event=profile.selected_event;
                                            }
                                            if(!frontend_renderer)open_frontend();
                                            frontend_clock.resume();
                                            if(!career_music){require(music.start(root,frontend_error),frontend_error);career_music=true;}
                                            if(profile.page==CareerPage::choose_car)
                                                play_career_hint(audio::kStarterCarSelectSpeech);
                                            if(profile.page==CareerPage::unique_reward){
                                                if(const auto* reward=career.media(profile.pending_unique);reward&&reward->reward_speech>=0)
                                                    if(!music.speech(reward->reward_speech,frontend_error))
                                                        std::fprintf(stderr,"CAREER reward speech failed: %s\n",frontend_error.c_str());
                                            }
                                            int magazine_voice=-1;
                                            if(profile.page==CareerPage::briefing) {
                                                music.stop_speech();
                                                if(const auto* message=career.media(profile.selected_event)) {
                                                    if(message->speech>=0&&!music.speech(message->speech,frontend_error))
                                                        std::fprintf(stderr,"CAREER speech failed: %s\n",frontend_error.c_str());
                                                    if(!message->magazine_rewards.empty())
                                                        magazine_voice=audio::magazine_briefing_variant(profile.selected_event);
                                                }
                                            }
                                            std::string notice;bool change=false,confirm=false,previous_negative=false,previous_positive=false,previous_color_up=false,previous_color_down=false;
                                            std::optional<CareerProfile> starter_purchase;
                                            bool starter_purchase_spoken=false;
                                            garage_fps.reset();
                                            while(!change && !leave && appletMainLoop()) {
                                                if(lobby_poll)lobby_poll();
                                                if(appletGetFocusState()!=AppletFocusState_InFocus) {
                                                    frontend_clock.resume();
                                                    garage_fps.reset();
                                                    continue;
                                                }
                                                if(magazine_voice>=0&&music.speech_drained()) {
                                                    if(!music.speech(audio::kMagazineBriefingSpeech,frontend_error,unsigned(magazine_voice)))
                                                        std::fprintf(stderr,"CAREER magazine briefing failed: %s\n",frontend_error.c_str());
                                                    magazine_voice=-1;
                                                }
                                                if(starter_purchase&&music.speech_drained()) {
                                                    if(starter_purchase_spoken) {
                                                        store(*starter_purchase);change=true;continue;
                                                    }
                                                    starter_purchase_spoken=true;
                                                    play_career_hint(audio::kStarterCarPurchaseSpeech);
                                                }
                                                padUpdate(&pad);
                                                const bool in_garage=profile.page==CareerPage::garage||profile.page==CareerPage::unique_reward;
                                                if(garage_fps_chord()&&in_garage) {
                                                    garage_fps.visible=!garage_fps.visible;
                                                    garage_fps.reset();
                                                }
                                                const auto pressed=starter_purchase?0:padGetButtonsDown(&pad);
                                                const auto stick=starter_purchase?decltype(padGetStickPos(&pad,0)){}:padGetStickPos(&pad,0);
                                                const bool vertical=profile.page==CareerPage::menu&&!career_info.active();
                                                const bool negative=vertical?stick.y>16000:stick.x<-16000;
                                                const bool positive=vertical?stick.y<-16000:stick.x>16000;
                                                const bool prev=(pressed&(vertical?HidNpadButton_Up:HidNpadButton_Left))||(negative&&!previous_negative);
                                                const bool next_key=(pressed&(vertical?HidNpadButton_Down:HidNpadButton_Right))||(positive&&!previous_positive);
                                                previous_negative=negative;previous_positive=positive;
                                                if((profile.page==CareerPage::garage||profile.page==CareerPage::unique_reward)&&garage.page()==GaragePage::colors) {
                                                    const bool color_up=(pressed&HidNpadButton_Up)||(stick.y>16000&&!previous_color_up);
                                                    const bool color_down=(pressed&HidNpadButton_Down)||(stick.y<-16000&&!previous_color_down);
                                                    previous_color_up=stick.y>16000;previous_color_down=stick.y<-16000;
                                                    if(color_up!=color_down){garage.move_color(0,color_up?-1:1);prepare_garage();}
                                                    if(pressed&(HidNpadButton_L|HidNpadButton_R)){garage.change_finish(pressed&HidNpadButton_L?-1:1,career,profile);prepare_garage();}
                                                }
                                                if(pressed&HidNpadButton_Plus){if(career_info.active()){career_info.close();music.sound(audio::FrontendSound::back);}else leave=true;continue;}
                                                if(career_info.active()) {
                                                    if(pressed&HidNpadButton_B){career_info.close();music.sound(audio::FrontendSound::back);}
                                                    else if(prev!=next_key&&career_info.move(prev?-1:1))music.sound(prev?audio::FrontendSound::left:audio::FrontendSound::right);
                                                }else if(pressed&HidNpadButton_B) {
                                                    if((profile.page==CareerPage::garage||profile.page==CareerPage::unique_reward)) {
                                                        notice.clear();
                                                        if(profile.page==CareerPage::unique_reward){garage.back(career,profile);prepare_garage();}
                                                        else if(garage.back(career,profile)){
                                                            auto next=profile;next.page=CareerPage::menu;store(next);
                                                            selection=0;change=true;leave=garage_car!=nullptr;
                                                        }
                                                        else prepare_garage();
                                                    }else if(profile.page==CareerPage::magazines){auto next=profile;require(career.dismiss_magazines(next,frontend_error),frontend_error);store(next);resident_car.clear();selection=0;change=true;}
                                                    else if(confirm){confirm=false;notice.clear();}
                                                    else if(sandbox&&profile.page==CareerPage::menu&&!choose_quick_mode) {
                                                        selection=unsigned(quick_options.mode);choose_quick_mode=true;notice.clear();
                                                    }else if(profile.page==CareerPage::choose_car || profile.page==CareerPage::menu)leave=true;
                                                    else {if(profile.page==CareerPage::briefing)music.stop_speech();auto next=profile;next.movie_event=next.movie_phase=0;next.page=profile.page==CareerPage::briefing?CareerPage::map:CareerPage::menu;store(next);selection=0;change=true;}
                                                    music.sound(audio::FrontendSound::back);
                                                }else if(sandbox&&profile.page==CareerPage::menu) {
                                                    const bool left=(pressed&HidNpadButton_Left)||(stick.x<-16000&&!previous_color_up);
                                                    const bool right=(pressed&HidNpadButton_Right)||(stick.x>16000&&!previous_color_down);
                                                    previous_color_up=stick.x<-16000;previous_color_down=stick.x>16000;
                                                    const bool accept=pressed&HidNpadButton_A;
                                                    if(choose_quick_mode) {
                                                        if(prev!=next_key) {
                                                            selection=(selection+(prev?kQuickRaceModeCount-1:1))%kQuickRaceModeCount;
                                                            music.sound(prev?audio::FrontendSound::up:audio::FrontendSound::down);
                                                        }
                                                        if(accept) {
                                                            quick_options.mode=static_cast<QuickRaceMode>(selection);
                                                            quick_courses=career.quick_courses(quick_options.mode);
                                                            require(!quick_courses.empty(),"quick race mode has no courses");
                                                            if(std::find(quick_courses.begin(),quick_courses.end(),free_course)==quick_courses.end())
                                                                free_course=quick_courses.front();
                                                            if(quick_options.mode==QuickRaceMode::lap_knockout&&quick_options.opponents==0)
                                                                quick_options.opponents=3;
                                                            selection=0;choose_quick_mode=false;
                                                            music.sound(audio::FrontendSound::select);
                                                        }
                                                    }else {
                                                        using Row=QuickRaceRow;
                                                        const auto rows=quick_race_rows(quick_options.mode,career.free_courses().at(free_course).type,!profile.bonus_preset.empty());
                                                        const auto cycle=[](unsigned value,unsigned count,int delta) {
                                                            return (value+count+delta)%count;
                                                        };
                                                        if(prev!=next_key) {
                                                            selection=cycle(selection,unsigned(rows.size()),prev?-1:1);
                                                            notice.clear();music.sound(prev?audio::FrontendSound::up:audio::FrontendSound::down);
                                                        }
                                                        const auto row=rows.at(selection);
                                                        if((pressed&HidNpadButton_Y)&&profile.bonus_preset.empty()) {
                                                            auto next=profile;next.ride={};store(next);garage.reset(career,profile);
                                                            garage_signature.clear();notice="Stock car restored";prepare_garage();
                                                        }
                                                        if((left!=right)||(accept&&row!=Row::customize&&row!=Row::start)) {
                                                            const int direction=left?-1:1;
                                                            auto next=profile;bool ride_changed=false;
                                                            switch(row) {
                                                            case Row::track: {
                                                                const auto at=unsigned(std::find(quick_courses.begin(),quick_courses.end(),free_course)-quick_courses.begin());
                                                                free_course=quick_courses.at(cycle(at,unsigned(quick_courses.size()),direction));
                                                                break;
                                                            }
                                                            case Row::car: {
                                                                const auto cars=career.quick_cars(earned_style);
                                                                const auto identity=profile.bonus_preset.empty()?profile.car:profile.bonus_preset;
                                                                const auto at=unsigned(std::find(cars.begin(),cars.end(),identity)-cars.begin());
                                                                const auto& car=cars.at(cycle(at,unsigned(cars.size()),direction));
                                                                select_quick_car(car,next);
                                                                garage.reset(career,next);garage_signature.clear();selection=1;ride_changed=true;break;
                                                            }
                                                            case Row::performance:next.ride.performance.fill(cycle(next.ride.performance[2],4,direction));ride_changed=true;break;
                                                            case Row::unique:next.ride.unique.fill(!std::all_of(next.ride.unique.begin(),next.ride.unique.end(),[](bool v){return v;}));ride_changed=true;break;
                                                            case Row::traffic:quick_options.traffic=cycle(quick_options.traffic,4,direction);break;
                                                            case Row::difficulty:quick_options.difficulty=cycle(quick_options.difficulty,3,direction);break;
                                                            case Row::opponents: {
                                                                const unsigned minimum=quick_options.mode==QuickRaceMode::lap_knockout?1U:0U;
                                                                quick_options.opponents=minimum+cycle(quick_options.opponents-minimum,4-minimum,direction);
                                                                break;
                                                            }
                                                            case Row::laps:quick_options.laps=1+cycle(quick_options.laps-1,10,direction);break;
                                                            case Row::catch_up:quick_options.catch_up=!quick_options.catch_up;break;
                                                            case Row::customize:case Row::start:break;
                                                            }
                                                            if(ride_changed){store(next);prepare_garage();}
                                                            notice.clear();music.sound(audio::FrontendSound::select);
                                                        }else if(accept&&row==Row::customize) {
                                                            auto next=profile;next.page=CareerPage::garage;store(next);garage.reset(career,profile);change=true;
                                                        }else if(accept&&row==Row::start) {
                                                            close_career();music.stop();career_music=false;
                                                            const auto nitrous_level=career_performance_levels(career,profile).nitrous;
                                                            const auto tip=profile_frame?0U:screen.tips.quick(quick_options.mode,unsigned(nitrous_level));
                                                            LoadingScreen free_loading(screen,root,!profile_frame,tip);
                                                            CarAssets chosen;require(load_car_assets(std::filesystem::path(root)/"Global/GLOBALB.BUN",profile.car,chosen,frontend_error),frontend_error);
                                                            CareerRideSetup setup;require(resolve_career_ride(garage_globals,career,profile,setup,frontend_error),frontend_error);
                                                            RaceCarDress dress;dress.request=&setup.request;dress.skin=&setup.skin;
                                                            dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,std::string& error,void*) {
                                                                return bind_frontend_runtime_skin(frame,request,error)&&bind_frontend_sponsors(inputs,request.installed_parts,frame,error);
                                                            };
                                                            const auto& course=career.free_courses().at(free_course);
                                                            const bool practice=quick_options.mode==QuickRaceMode::free_run;
                                                            auto launch=quick_options;
                                                            launch.seed=static_cast<std::uint32_t>(armGetSystemTick());
                                                            std::printf("QUICK_RACE mode=%s car=%s track=%d direction=%d traffic=%u opponents=%u laps=%u difficulty=%u\n",
                                                                quick_race_mode_name(launch.mode),profile.car.c_str(),course.track,course.direction,
                                                                launch.traffic,practice?0U:launch.opponents,quick_race_laps(launch),launch.difficulty);
                                                            std::fflush(stdout);
                                                            if(!practice)load_post_race();
                                                            RaceReturn result;
                                                            do { result=run(root,false,false,profile_frame,particles_enabled,sky,false,glows,true,presentation,texture_filter,scene_height,
                                                                gpu_boost,hud,false,fps_overlay,&setup.performance,handling_telemetry,steering,chosen,screen,pad,practice,&dress,course.event,
                                                                &art,launch.difficulty,nullptr,0,0,nullptr,nullptr,setup.reputation,nullptr,0,course.round,
                                                                nullptr,nullptr,false,active_profile?active_profile->name:"YOU",nullptr,&launch);
                                                            }while(result==RaceReturn::restart&&appletMainLoop());
                                                            change=true;
                                                        }
                                                    }
                                                }else if(prev!=next_key) {
                                                    confirm=false;notice.clear();
                                                    if(profile.page!=CareerPage::map)music.sound(prev?audio::FrontendSound::up:audio::FrontendSound::down);
                                                    if((profile.page==CareerPage::garage||profile.page==CareerPage::unique_reward)){
                                                        if(garage.page()==GaragePage::colors)garage.move_color(prev?-1:1,0);
                                                        else garage.move(prev?-1:1);
                                                        prepare_garage();
                                                    }
                                                    else if(profile.page==CareerPage::choose_car) {
                                                        const auto count=static_cast<unsigned>(career.starters().size());
                                                        if(prev) {if(selection) --selection;}
                                                        else if(selection+1<count) ++selection;
                                                        require(frontend_scene.select_car(selection,frontend_error),frontend_error);
                                                    }else if(profile.page==CareerPage::briefing) {
                                                        auto next=profile;next.difficulty=(profile.difficulty+(prev?2:1))%3;store(next);
                                                    }else if(profile.page==CareerPage::menu)selection=(selection+(prev?5:1))%6;
                                                    else if(profile.page==CareerPage::magazines){selection=(selection+(prev?unsigned(profile.magazine_covers.size())-1:1))%unsigned(profile.magazine_covers.size());change=true;}
                                                    else if(profile.page==CareerPage::map) {
                                                        const auto events=career.available(profile);
                                                        const auto found=std::find(events.begin(),events.end(),profile.selected_event);
                                                        const auto at=static_cast<std::size_t>(found-events.begin());
                                                        const bool can_move=at<events.size()&&(prev?at>0:at+1<events.size());
                                                        if(can_move) {
                                                            auto next=profile;next.tournament={};next.selected_event=events[prev?at-1:at+1];
                                                            store(next);change=true;
                                                            music.sound(prev?audio::FrontendSound::left:audio::FrontendSound::right);
                                                        }
                                                    }
                                                }else if((pressed&(HidNpadButton_ZL|HidNpadButton_ZR))&&
                                                         profile.page==CareerPage::garage&&garage.page()==GaragePage::vinyl_layers) {
                                                    auto next=profile;
                                                    if(garage.reorder_vinyl(pressed&HidNpadButton_ZL?-1:1,career,next)) {
                                                        store(next);prepare_garage();
                                                    }
                                                }else if((pressed&HidNpadButton_X)&&profile.page==CareerPage::garage&&garage.page()==GaragePage::decals_brands) {
                                                    garage.toggle_decal_color(career,profile);prepare_garage();
                                                }else if((pressed&HidNpadButton_X)&&profile.page==CareerPage::garage&&garage.page()==GaragePage::vinyl_layers) {
                                                    if(!garage.edit_vinyl_colors(career,profile,frontend_error))notice=frontend_error;
                                                    else {notice.clear();prepare_garage();}
                                                }else if(pressed&HidNpadButton_A) {
                                                    music.sound(audio::FrontendSound::select);
                                                    if((profile.page==CareerPage::garage||profile.page==CareerPage::unique_reward)) {
                                                        const auto previous_page=profile.page;auto next=profile;bool purchased=false;
                                                        if(!garage.accept(career,next,purchased,frontend_error))notice=frontend_error;
                                                        else {
                                                            if(purchased)store(next);notice.clear();
                                                            if(next.page!=previous_page){
                                                                if(next.page==CareerPage::magazines){const auto first=std::find_if(next.magazine_covers.begin(),next.magazine_covers.end(),[](const auto& cover){return !cover.seen;});selection=first==next.magazine_covers.end()?0:unsigned(first-next.magazine_covers.begin());}
                                                                change=true;
                                                            }else prepare_garage();
                                                        }
                                                    }else if(profile.page==CareerPage::magazines){
                                                        auto next=profile;next.magazine_covers[selection].seen=true;next.page=CareerPage::menu;store(next);resident_car.clear();selection=0;change=true;
                                                    }else if(profile.page==CareerPage::choose_car) {
                                                        if(frontend_scene.car_transitioning())continue;
                                                        if(!confirm){confirm=true;notice="Buy "+career.starters().at(selection).name+"? A: confirm / B: cancel";}
                                                        else {
                                                            auto next=profile;
                                                            if(career.buy(next,selection,frontend_error)) {
                                                                starter_purchase=std::move(next);notice.clear();
                                                            }else notice=frontend_error;
                                                        }
                                                    }else if(profile.page==CareerPage::map) {
                                                        if(profile.selected_event>kCareerLastEvent)notice="This event is outside the current campaign block.";
                                                        else {auto next=profile;require(career.begin_event(next,frontend_error),frontend_error);store(next);change=true;
                                                        }
                                                    }else if(profile.page==CareerPage::briefing) {
                                                        close_career();music.stop();career_music=false;
                                                        //4C5DF0: ordinary intro movies follow accepted
                                                        //difficulty;7/15/92 already ran through4C4EE0.
                                                        if(const auto movie=career.pending_movie(profile,true);!movie.empty()) {
                                                            const auto played=play_career_movie(root,movie,movie_poll,movie_present);
                                                            require(played.outcome!=IntroOutcome::error,played.error);
                                                            if(played.outcome==IntroOutcome::quit)break;
                                                            auto next=profile;require(career.movie_finished(next,frontend_error),frontend_error);store(next);
                                                        }
                                                        const auto* event=find_race_event(career.events(),profile.selected_event);
                                                        require(event!=nullptr,"missing career event loading context");
                                                        const auto tip=profile_frame?0U:screen.tips.career(*event,profile);
                                                        LoadingScreen race_loading(screen,root,!profile_frame,tip);
                                                        CarAssets owned;require(load_car_assets(std::filesystem::path(root)/"Global/GLOBALB.BUN",profile.car,owned,frontend_error),frontend_error);
                                                        load_post_race();
                                                        std::vector<unsigned char> ride_globals;std::vector<RaceTableIssue> ride_issues;
                                                        require(load_race_image(std::filesystem::path(root)/"Global/GLOBALB.BUN",ride_globals,ride_issues),"career ride global input");
                                                        CareerRideSetup owned_setup;require(resolve_career_ride(ride_globals,career,profile,owned_setup,frontend_error),frontend_error);
                                                        ride_globals.clear();ride_globals.shrink_to_fit();
                                                        RaceCarDress owned_dress;owned_dress.request=&owned_setup.request;owned_dress.skin=&owned_setup.skin;
                                                        owned_dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,std::string& error,void*) {
                                                            return bind_frontend_runtime_skin(frame,request,error)&&bind_frontend_sponsors(inputs,request.installed_parts,frame,error);
                                                        };
                                                        bool next_round=false;
                                                        do {
                                                            unsigned completed=0;int style_earned=0;RaceReturn outcome;
                                                            std::array<unsigned,4> places{};
                                                            std::exception_ptr terminal_failure;
                                                            const auto round=profile.tournament.event?profile.tournament.round:0;
                                                            do {
                                                                CareerRaceReport career_report;
                                                                try {outcome=run(root,false,false,profile_frame,particles_enabled,sky,
                                                                    false,glows,true,presentation,texture_filter,scene_height,
                                                                    gpu_boost,hud,false,fps_overlay,&owned_setup.performance,handling_telemetry,steering,
                                                                    owned,screen,pad,false,&owned_dress,profile.selected_event,&art,profile.difficulty,&completed,
                                                                    career.media(profile.selected_event)->reward[profile.difficulty],profile.style_points,&style_earned,
                                                                    career.media(profile.selected_event),owned_setup.reputation,&career,profile.style_high_water,
                                                                    round,&places,profile.tournament.event?&profile.tournament:nullptr,false,profile.name,&career_report,nullptr,nullptr,false,&profile);
                                                                }catch(...) {
                                                                    // A renderer/finish-continuation failure does not
                                                                    // cancel a result already fixed by race timing.
                                                                    // Keep the error visible after committing it.
                                                                    if(!completed)throw;
                                                                    terminal_failure=std::current_exception();outcome=RaceReturn::menu;
                                                                }
                                                                auto recorded=profile;
                                                                record_career_statistics(recorded.statistics,career.statistics_track_mask(profile.selected_event,round),career_report);store(recorded);
                                                                if(outcome==RaceReturn::restart){auto next=profile;++next.restarts;store(next);}
                                                            }while(outcome==RaceReturn::restart&&appletMainLoop());
                                                            auto next=profile;
                                                            if(outcome==RaceReturn::restart_tournament) {
                                                                next.tournament={};next.tournament.event=profile.selected_event;++next.restarts;
                                                                store(next);next_round=true;continue;
                                                            }
                                                            require(apply_career_race_result(career,next,completed,places,style_earned,frontend_error),frontend_error);
                                                            next_round=completed&&next.tournament.event&&next.page==CareerPage::briefing;
                                                            if(next.page==CareerPage::magazines){const auto first=std::find_if(next.magazine_covers.begin(),next.magazine_covers.end(),[](const auto& cover){return !cover.seen;});selection=unsigned(first-next.magazine_covers.begin());}
                                                            store(next);
                                                            if(terminal_failure)std::rethrow_exception(terminal_failure);
                                                        }while(next_round&&appletMainLoop());
                                                        change=true;
                                                    }else if(profile.page==CareerPage::menu) {
                                                        auto next=profile;
                                                        if(selection==0){next.page=CareerPage::map;store(next);change=true;}
                                                        else if(selection==1){require(career.enter_customization(next,frontend_error),frontend_error);store(next);garage.reset(career,profile);garage_signature.clear();change=true;}
                                                        else if(selection==3){if(next.magazine_covers.empty())notice="No magazine covers earned yet.";else {next.page=CareerPage::magazines;store(next);selection=0;change=true;}}
                                                        else if(selection==5){store(next);notice="Career saved";}
                                                        else if(selection==2||selection==4){career_info.open(selection==2?CareerInfoScreen::rankings:CareerInfoScreen::statistics);notice.clear();}
                                                    }
                                                }
                                                if(change||leave)continue;
                                                if(sandbox||in_garage)prepare_garage();
                                                else if(profile.page!=CareerPage::magazines)frontend_scene.road_target(50.F);
                                                const auto elapsed=frontend_clock.sample(armTicksToNs(armGetSystemTick()));
                                                if(in_garage) {
                                                    const auto orbit=padGetStickPos(&pad,1);
                                                    const auto axis=[](int value){return std::abs(value)>6000?float(value)/32767.F:0.F;};
                                                    frontend_scene.orbit(axis(orbit.x)*float(elapsed/1000000000.0)*90,
                                                        -axis(orbit.y)*float(elapsed/1000000000.0)*45);
                                                }
                                                require(frontend_scene.frame(static_cast<float>(elapsed/1000000000.0),frontend_clock.original_ticks(),frontend_error),frontend_error);
                                                art.advance(static_cast<float>(elapsed/1000000000.0));
                                                if(!sandbox&&!change&&!leave&&music.speech_drained()) {
                                                    int hint=-1;
                                                    if(career_info.active())
                                                        hint=career_info.screen()==CareerInfoScreen::rankings?76:78;
                                                    else if(profile.page==CareerPage::map)hint=72;
                                                    else if(profile.page==CareerPage::magazines)hint=71;
                                                    else if(profile.page==CareerPage::garage)hint=career_garage_speech(garage);
                                                    if(hint>=0)play_career_hint(hint);
                                                }
                                                if(career_info.active())art.record_career_info(width,height,career,profile,career_info,frontend_ui);
                                                else if(profile.page==CareerPage::magazines)art.record_magazine(width,height,magazine_recipe,profile.magazine_covers[selection].car,frontend_ui);
                                                else if(sandbox&&profile.page==CareerPage::menu)art.record_free_drive(width,height,career,profile,selection,free_course,notice,frontend_ui,&quick_options,choose_quick_mode);
                                                else art.record_career(width,height,career,profile,selection,notice,frontend_ui,in_garage?&garage:nullptr);
                                                const bool show_garage_fps=in_garage&&garage_fps.visible;
                                                if(show_garage_fps)art.append_fps(width,height,garage_fps.history,frontend_ui);
                                                require(frontend_renderer->update(frontend_scene.commands(),frontend_error),frontend_error);
                                                require(frontend_renderer->present_frontend(frontend_ui,frontend_error),frontend_error);
                                                if(show_garage_fps) {
                                                    const auto garage_presented=armGetSystemTick();
                                                    if(garage_fps.previous_present)
                                                        garage_fps.history.add(ms(garage_presented-garage_fps.previous_present));
                                                    garage_fps.previous_present=garage_presented;
                                                }
                                            }
                                        }
                                        music.stop();close_career();
                                    }catch(...) {
                                        music.stop();
                                        if(frontend_renderer){std::string cleanup_error;frontend_renderer->close(cleanup_error);frontend_renderer.reset();frontend_gpu_profile.reset();}
                                        throw;
                                    }
                                }
                            }
                            menu.notice("");
                        } catch (const Cancel&) {
                            std::puts("RACE_LOAD cancelled by input or applet exit");std::fflush(stdout);
                            menu.notice("LOADING CANCELLED");
                        } catch (const std::exception& error) {
                            std::printf("RACE MENU ERROR: %s\n",error.what());
                            menu.notice(std::string(action==FrontendAction::free_drive?"QUICK RACE: ":"UNDERGROUND: ")+error.what());
                        }
                        // Restore the main-menu preset after success, cancellation or
                        // a career error. No renderer may borrow the old resources.
                        if(career_scene_changed)require(frontend_scene.load(root,width,height,frontend_error),frontend_error);
                        if(garage_car)return;
                        if(!music.playing()) {
                            if(!music.start(root,frontend_error)) menu.notice("MUSIC: "+frontend_error);
                            else menu.announce_song();
                        }
                        stick_up=stick_down=false;
                        if(!frontend_renderer)open_frontend();
                        frontend_clock.resume();
                };
                while (appletMainLoop()) {
                    if(appletGetFocusState()!=AppletFocusState_InFocus) {frontend_clock.resume();frontend_presentation_stats.reset();continue;}
                    const auto poll_begin=armGetSystemTick();
                    padUpdate(&pad); const auto down=padGetButtonsDown(&pad);
                    garage_fps_chord();
                    const auto stick=padGetStickPos(&pad,0);
                    const bool up=stick.y>16000, below=stick.y<-16000;
                    FrontendInput input;
                    input.up=(down&HidNpadButton_Up) || (up&&!stick_up);
                    input.down=(down&HidNpadButton_Down) || (below&&!stick_down);
                    input.accept=down&HidNpadButton_A;
                    input.profiles=down&HidNpadButton_X;
                    input.back=down&(HidNpadButton_B|HidNpadButton_Plus);
                    stick_up=up; stick_down=below;
                    const auto previous_selection=menu.selected();
                    auto action=FrontendAction::none;
                    const bool handling_options=options_open;
                    const bool handling_statistics=statistics.active();
                    if(handling_statistics) {
                        const bool left=down&(HidNpadButton_Left|HidNpadButton_StickLLeft);
                        const bool right=down&(HidNpadButton_Right|HidNpadButton_StickLRight);
                        if(input.back) {
                            statistics.close();
                            statistics_catalog.reset();
                            music.sound(audio::FrontendSound::back);
                        }else if(left!=right&&statistics.move(left?-1:1)) {
                            music.sound(left?audio::FrontendSound::left:audio::FrontendSound::right);
                        }
                    }else if(handling_options) {
                        if(input.back) {
                            options_open=false;
                            music.sound(audio::FrontendSound::back);
                        }else if(input.up!=input.down) {
                            option_row=(option_row+(input.up?3U:1U))%4U;
                            music.sound(input.up?audio::FrontendSound::up:audio::FrontendSound::down);
                        }else {
                            const bool left=down&(HidNpadButton_Left|HidNpadButton_StickLLeft);
                            const bool right=down&(HidNpadButton_Right|HidNpadButton_StickLRight);
                            if(left!=right || input.accept) {
                                bool* values[]={&presentation.motion_blur,&presentation.low_car_reflections,
                                                &presentation.road_reflections,&presentation.metric_speed};
                                *values[option_row]=!*values[option_row];
                                option_notice=write_presentation_options(presentation)?"":"Could not save options to SD.";
                                music.sound(left?audio::FrontendSound::left:audio::FrontendSound::right);
                            }
                        }
                    }else if(pending==FrontendAction::none)action=menu.update(input);
                    const auto selection_done=armGetSystemTick();
                    if(!handling_options&&!handling_statistics) {
                        if(pending==FrontendAction::none && input.back) music.sound(audio::FrontendSound::back);
                        else if(pending==FrontendAction::none && input.accept) music.sound(audio::FrontendSound::select);
                        else if(menu.selected()!=previous_selection)
                            music.sound(input.up?audio::FrontendSound::up:audio::FrontendSound::down);
                    }
                    if(action==FrontendAction::exit) {
                        pending=action;music.begin_stop();action=FrontendAction::none;
                    }
                    if(pending!=FrontendAction::none && music.stop_ready()) {
                        action=pending;pending=FrontendAction::none;
                    }
                    if (action==FrontendAction::exit) {music.stop();break;}
                    if(action==FrontendAction::statistics&&active_profile) {
                        statistics_catalog.emplace();
                        bool atlas_changed=false;
                        if(statistics_catalog->load(root,frontend_error)&&
                           load_career(active_profile->base,*statistics_catalog,statistics_profile,frontend_error)&&
                           art.load_statistics(root,atlas_changed,frontend_error)) {
                            if(atlas_changed)require(frontend_renderer->set_frontend_overlay(art.atlas(),frontend_error,false),frontend_error);
                            statistics.open(CareerInfoScreen::statistics);
                        }else {
                            menu.notice("STATISTICS: "+frontend_error);
                            statistics_catalog.reset();
                        }
                        frontend_clock.resume();
                    }
                    if(action==FrontendAction::options) {
                        load_post_race();
                        require(frontend_renderer->set_frontend_overlay(art.atlas(),frontend_error),frontend_error);
                        options_open=true;
                        option_row=0;
                        option_notice.clear();
                    }
                    if(action==FrontendAction::profiles) {
                        choose_profile();frontend_clock.resume();menu.notice(active_profile?"PROFILE: "+active_profile->name:"");continue;
                    }
                    if(action==FrontendAction::multiplayer) {
                        music.stop();
                        require(frontend_renderer->close(frontend_error),frontend_error);
                        frontend_renderer.reset();frontend_gpu_profile.reset();
                        try {
                            run_switch_lan(root,pad,free_rides,[&](const std::string& car,unsigned tier,
                                                                 const std::function<void()>& poll) {
                                run_career_action(FrontendAction::free_drive,&car,tier,poll);
                            });
                            menu.notice("");
                        }
                        catch(const std::exception& e){menu.notice(std::string("MULTIPLAYER: ")+e.what());}
                        if(!appletMainLoop())break;
                        if(music.start(root,frontend_error))menu.announce_song();
                        else menu.notice("MUSIC: "+frontend_error);
                        open_frontend();frontend_clock.resume();
                        stick_up=stick_down=false;
                        continue;
                    }
                    if (action==FrontendAction::underground || action==FrontendAction::free_drive) {
                        run_career_action(action,nullptr,0,{});
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
                    trace("frame.ui.before");
                    if(options_open)art.record_options(width,height,presentation,option_row,option_notice,frontend_ui);
                    else if(statistics.active())art.record_career_info(width,height,*statistics_catalog,statistics_profile,statistics,frontend_ui);
                    else art.record_ui(width,height,menu,frontend_ui);
                    trace("frame.ui.after");
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
                if(frontend_renderer)require(frontend_renderer->close(frontend_error),frontend_error);
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
    return 0;
}
