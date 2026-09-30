#pragma once
#include "deko_renderer.h"
#include "nfsu/heap_usage.h"
#include "nfsu/frame_profile_stats.h"
#include "nfsu/race_frame_stats.h"
#include "nfsu/screen_text.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nfsu {
inline std::string profile_format(const char* pattern, ...) {
    char text[512];
    va_list args; va_start(args, pattern);
    std::vsnprintf(text, sizeof text, pattern, args); va_end(args);
    return text;
}
inline std::string profile_frame_percentile(const RaceFrameStats& stats, unsigned percent) {
    const unsigned bin = stats.percentile_bin(percent);
    return bin == RaceFrameStats::kOverflowBin ? ">=100" :
        profile_format("%.2f", (bin+1)*RaceFrameStats::kBinWidthMs);
}
struct ClockReadings {
    std::uint32_t cpu_bus_hz = 0, gpu_hz = 0, emc_hz = 0;
    bool cpu_bus_ok = false, gpu_ok = false, emc_ok = false;
};
inline std::string profile_percentile(const ProfileDistribution& stats) {
    const auto bin = stats.percentile_bin(95);
    return bin == ProfileDistribution::kOverflowBin ? ">=100" :
        profile_format("%.2f",(bin+1)*ProfileDistribution::kBinWidthMs);
}
inline std::string profile_row(const char* label, const ProfileDistribution& stats) {
    if (!stats.count()) return profile_format("%-16s n0 unavailable",label);
    const auto p95 = profile_percentile(stats);
    return profile_format("%-16s n%llu mean %.2f p95 %s max %.2f ms",label,
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
    const char* sky = "ON";
    DekoSceneTarget scene_target{1280, 720, false};
};
using ProfilePages = std::vector<std::vector<std::string>>;
inline ProfilePages build_profile_pages(std::vector<std::string> replay_rows,
                                        bool replay_mode, const RaceFrameStats& frames,
                                        unsigned unpause_resets, const ProfileResultContext& context,
                                        const std::string& failure, const char* mode) {
    ProfilePages pages;
    std::vector<std::string> titles;
    auto add_page = [&](const char* title, std::vector<std::string> rows) {
        if (rows.size() > 8) throw std::runtime_error("profile page exceeds eight data rows");
        for (const auto& row : rows) if (row.size() > 80)
            throw std::runtime_error("profile page exceeds 80 columns");
        titles.emplace_back(title);
        pages.push_back(std::move(rows));
    };
    const auto& clocks = context.clocks;
    add_page("IDENTITY", {
        profile_format("BUILD TAG %.40s",NFSU_RACE_BUILD_TAG),
        std::strlen(NFSU_RACE_BUILD_TAG) >= 40 ? "FULL COMMIT: build tag above" :
            "FULL COMMIT: coordinator build record",
        "NRO SHA-256: coordinator build record (not derivable in NRO)",
        profile_format("%s | %s DIRECT | PROFILE ONE SCENE PAIR | SKY %s",mode,
            replay_mode?"REPLAY":"LIVE",context.sky),
        profile_format("TOOLCHAIN GCC %s | deko3d version unavailable",__VERSION__),
        clocks.cpu_bus_ok ? profile_format("CPU clock %.1f MHz",clocks.cpu_bus_hz/1e6) :
            "CPU clock unavailable",
        clocks.gpu_ok ? profile_format("GPU clock %.1f MHz",clocks.gpu_hz/1e6) : "GPU clock unavailable",
        clocks.emc_ok ? profile_format("MEM/EMC clock %.1f MHz",clocks.emc_hz/1e6) : "MEM/EMC clock unavailable"
    });
    add_page("REPLAY",std::move(replay_rows));
    if (frames.count()) {
        const auto mean = frames.mean();
        const auto worst = context.cpu.worst();
        add_page("ACTIVE FRAMES",{
            profile_format("FRAMES %llu | excludes countdown/pause/resume/finish | resets %u",
                (unsigned long long)frames.count(),unpause_resets),
            profile_format("MEAN %.2f | P50 %s | P95 %s | P99 %s ms",mean.frame_ms,
                profile_frame_percentile(frames,50).c_str(),profile_frame_percentile(frames,95).c_str(),
                profile_frame_percentile(frames,99).c_str()),
            profile_format("MAX %.2f ms | OVER 25.00 %llu/%llu (%.2f%%)",frames.max_ms(),
                (unsigned long long)frames.over_budget(),(unsigned long long)frames.count(),
                100.0*frames.over_budget()/frames.count()),
            profile_format("WORST TICK %llu | TRACK/CAR DRAWS %u/%u",
                (unsigned long long)worst.tick,worst.track_draws,worst.car_draws),
            profile_format("WORST TRIANGLES %llu | DEBT %llu ticks",
                (unsigned long long)worst.triangles,(unsigned long long)worst.debt),
            profile_format("CATCH-UP MAX %u | DEBT MAX %llu ticks",context.cpu.catch_up_max(),
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
        profile_format("TARGET %ux%u -> %ux%u | %s",context.scene_target.width,context.scene_target.height,
            1280,720,context.scene_target.upscale ? "LINEAR UPSCALE INCLUDED" : "NATIVE DIRECT"),
        profile_format("ONE SCENE PAIR | completed %llu | invalid %llu | dropped %llu",
            (unsigned long long)gpu.scene.count(),(unsigned long long)gpu.invalid,
            (unsigned long long)gpu.dropped),
        profile_row("SCENE",gpu.scene),
        profile_format("WINDOW %llu PAIRS | TICKS %llu-%llu (%llu) | RESUMES %u",
            (unsigned long long)gpu.scene.count(),
            (unsigned long long)gpu.first_tick,(unsigned long long)gpu.last_tick,
            (unsigned long long)gpu.span_ticks(),unpause_resets),
        profile_format("WORST GPU TICK %llu | TRACK/CAR DRAWS %u/%u",
            (unsigned long long)gpu.worst_tick,gpu.worst_track_draws,gpu.worst_car_draws),
        "Top-begin/end; fence-drained; flush included; span, not utilization",
        profile_format("GPFIFO bound last/max %u/%u of %u | track lists %u",
            context.gpfifo.last_entries,context.gpfifo.max_entries,
            kDekoGpfifoRingEntries,context.gpfifo.last_lists),
        profile_format("QUERY/STAT STORAGE %llu bytes",(unsigned long long)context.storage_bytes)
    });
    const auto& heap = context.heap;
    add_page("LOAD/MEMORY",{
        profile_format("LOAD %.0f ms | initial upload %.0f ms",context.load_ms,context.upload_ms),
        profile_format("CPU STREAM %llu / 50331648 | PERSISTENT %llu / 67108864",
            (unsigned long long)context.stream_bytes,(unsigned long long)context.persistent_bytes),
        profile_format("GPU BLOCKS %llu / 268435456 bytes",(unsigned long long)context.gpu_bytes),
        profile_format("FRAME UNIFORMS %llu bytes",(unsigned long long)context.memory.frame_uniform_bytes),
        profile_format("STATIC UNIFORMS %llu bytes",(unsigned long long)context.memory.static_uniform_bytes),
        profile_format("PREPARED COMMANDS %llu bytes",(unsigned long long)context.memory.prepared_command_bytes),
        profile_format("LOAD HEAP ABS %llu | DELTA %llu / 50331648 | OVER %llu",
            (unsigned long long)heap.load_peak,(unsigned long long)heap.load_delta(),
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
        rows.insert(rows.begin(),profile_format("BUILD %.12s | %s | %s | SKY %s | PAGE %zu/%zu",
            NFSU_RACE_BUILD_TAG,mode,titles[i].c_str(),context.sky,i+1,pages.size()));
        rows.push_back("D-pad LEFT/RIGHT changes page | + exits");
    }
    return pages;
}
} // namespace nfsu
