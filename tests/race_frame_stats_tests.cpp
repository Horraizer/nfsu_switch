#include "nfsu/race_frame_stats.h"
#include "nfsu/frame_profile_stats.h"
#include "nfsu/deko_memory_usage.h"
#include "nfsu/gpu_scene_profile.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

int main() {
    struct Block {
        std::uint64_t bytes = 0;
        explicit operator bool() const { return bytes != 0; }
        std::uint64_t getSize() {
            if (!bytes) std::abort();
            return bytes;
        }
    };
    Block empty;
    Block allocated{4096};
    if (nfsu::deko_allocated_block_size(empty) != 0 ||
        nfsu::deko_allocated_block_size(allocated) != 4096) {
        std::cerr << "empty deko memory block was queried\n";
        return 1;
    }
    nfsu::RaceFrameStats stats;
    if (stats.count() != 0 || stats.mean().frame_ms != 0) return 1;
    for (const double frame : std::array<double,4>{10.0, 16.67, 16.68, 101.0}) {
        nfsu::RaceFrameSample sample;
        sample.frame_ms = frame;
        sample.simulation_ms = 2.0;
        sample.residency_ms = 3.0;
        sample.record_ms = 4.0;
        sample.update_ms = 5.0;
        sample.present_cpu_ms = 6.0;
        sample.acquire_ms = 1.0;
        sample.fence_ms = 0.5;
        sample.command_ms = 2.0;
        sample.submit_ms = 2.5;
        sample.draw_ms = 7.0;
        sample.transfer_ms = 8.0;
        sample.panel_ms = 9.0;
        sample.catch_up = static_cast<unsigned>(frame);
        stats.add(sample);
    }
    const auto mean = stats.mean();
    if (stats.count() != 4 || stats.over_budget() != 1 || stats.max_ms() != 101.0 ||
        stats.catch_up_max() != 101 || std::abs(mean.frame_ms-36.0875) > 0.00001 ||
        mean.simulation_ms != 2.0 || mean.residency_ms != 3.0 ||
        mean.record_ms != 4.0 || mean.update_ms != 5.0 ||
        mean.present_cpu_ms != 6.0 || mean.acquire_ms != 1.0 ||
        mean.fence_ms != 0.5 || mean.command_ms != 2.0 || mean.submit_ms != 2.5 ||
        mean.draw_ms != 7.0 || mean.transfer_ms != 8.0 || mean.panel_ms != 9.0 ||
        stats.percentile_bin(50) != 66 || stats.percentile_bin(95) != nfsu::RaceFrameStats::kOverflowBin) {
        std::cerr << "race frame statistics mismatch\n";
        return 1;
    }
    stats.reset();
    if (stats.count() != 0 || stats.over_budget() != 0 || stats.max_ms() != 0 ||
        stats.catch_up_max() != 0 || stats.percentile_bin(50) != nfsu::RaceFrameStats::kOverflowBin) {
        std::cerr << "race frame statistics reset mismatch\n";
        return 1;
    }
    nfsu::RaceFrameStats threshold;
    for (double ms : {16.68, 17.31, 25.0, 25.01}) {
        nfsu::RaceFrameSample sample;
        sample.frame_ms = ms;
        threshold.add(sample);
    }
    if (threshold.count() != 4 || threshold.over_budget() != 1) {
        std::cerr << "normal refresh jitter counted as a slow frame\n";
        return 1;
    }
    nfsu::RaceFrameSample first_after_pause;
    first_after_pause.frame_ms = 1.0;
    stats.add(first_after_pause);
    if (stats.count() != 1 || stats.percentile_bin(50) != 4 || stats.over_budget() != 0) {
        std::cerr << "race frame statistics resume mismatch\n";
        return 1;
    }
    nfsu::FrameProfileStats profile;
    nfsu::FrameProfileStats::Values values{};
    values[static_cast<std::size_t>(nfsu::FrameProfileStage::cull)] = 1.25;
    profile.add_simulation_tick(2.0);
    profile.add_frame(values,10.0,{42,1,500,120,20},2);
    values[static_cast<std::size_t>(nfsu::FrameProfileStage::cull)] = 101.0;
    profile.add_frame(values,20.0,{43,3,600,130,21},4);
    const auto& cull = profile.stage(nfsu::FrameProfileStage::cull);
    const auto& sim_tick = profile.stage(nfsu::FrameProfileStage::simulation_tick);
    if (profile.count() != 2 || cull.count() != 2 || cull.mean() != 51.125 ||
        cull.max() != 101.0 || cull.percentile_bin(95) != nfsu::ProfileDistribution::kOverflowBin ||
        sim_tick.count() != 1 || sim_tick.mean() != 2.0 || profile.worst().tick != 43 ||
        profile.worst().track_draws != 130 || profile.catch_up_max() != 4 ||
        profile.debt_max() != 3) {
        std::cerr << "frame profile statistics mismatch\n";
        return 1;
    }
    profile.reset();
    if (profile.count() || profile.stage(nfsu::FrameProfileStage::cull).count() ||
        profile.stage(nfsu::FrameProfileStage::simulation_tick).count() || profile.worst().tick) {
        std::cerr << "frame profile reset mismatch\n";
        return 1;
    }
    nfsu::DekoGpuProfile gpu;
    for (std::uint64_t tick = 1; tick <= 18584; ++tick)
        gpu.add_scene(20.0, {tick, 756, 94, true});
    if (gpu.scene.count() != 18584 || gpu.first_tick != 1 ||
        gpu.last_tick != 18584 || gpu.span_ticks() != 18584) {
        std::cerr << "long GPU profile window mismatch\n";
        return 1;
    }
    gpu = {};
    for (std::uint64_t tick = 18540; tick <= 18584; ++tick)
        gpu.add_scene(21.0, {tick, 756, 94, true});
    gpu.add_scene(-1.0, {18585, 0, 0, true});
    if (gpu.scene.count() != 45 || gpu.first_tick != 18540 ||
        gpu.last_tick != 18584 || gpu.span_ticks() != 45 ||
        gpu.worst_tick != 18540 || gpu.worst_track_draws != 756 ||
        gpu.worst_car_draws != 94) {
        std::cerr << "resumed GPU profile window mismatch\n";
        return 1;
    }
    return 0;
}
