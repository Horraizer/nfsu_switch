#define NFSU_RACE_BUILD_TAG "a261ee4ca3ca"
#include "../switch/profile_pages.h"
#include "nfsu/deko_scene_target.h"
#include <iostream>

int main() {
    using namespace nfsu;
    FrameProfileStats cpu;
    FrameProfileStats::Values values{};
    values.fill(1.25);
    cpu.add_simulation_tick(1.25);
    cpu.add_frame(values, 18.5, {1200, 2, 1234567, 430, 12}, 2);
    DekoGpuProfile gpu;
    gpu.add_scene(14.25, {1200, 430, 12, true});
    gpu.invalid = 2;
    gpu.dropped = 3;
    HeapUsage heap;
    ProfileResultContext context{cpu, gpu, heap};
    context.storage_bytes = 65536;
    context.gpfifo = {1024, 1280, 430};
    RaceFrameStats frames;
    frames.add({18.5});
    const std::vector<std::string> replay_rows = {
        "RUNNING | REPLAY RUNNING | LAST STEP 1200", "lap 1 time 01:00.00",
        "lap 2 time --:--.--", "total ticks 1200", "STATE SHA A 0123456789ABCDEF",
        "STATE SHA B 0123456789ABCDEF", "STEPS SHA A 0123456789ABCDEF",
        "STEPS SHA B 0123456789ABCDEF"
    };
    for (const auto height : {720U, 540U, 450U, 360U}) {
        if (!plan_deko_scene_target(1280, 720, true, height, context.scene_target)) return 1;
        for (bool active : {false, true}) {
            try {
                const auto pages = build_profile_pages(replay_rows, true, active ? frames : RaceFrameStats{},
                    2, context, "", "HANDHELD");
                if (pages.size() != 8) {
                    std::cerr << "FAIL: expected eight profile pages\n";
                    return 1;
                }
                for (std::size_t page = 0; page < pages.size(); ++page) {
                    if (pages[page].size() < 2 || pages[page].size() - 2 > 8) {
                        std::cerr << "FAIL: page " << page + 1 << " exceeds eight data rows\n";
                        return 1;
                    }
                    for (const auto& row : pages[page]) {
                        if (row.size() > 80) {
                            std::cerr << "FAIL: page " << page + 1 << " exceeds 80 columns\n";
                            return 1;
                        }
                    }
                }
                const auto& gpu_page = pages[6];
                const auto contains = [&](const char* needle) {
                    for (const auto& row : gpu_page)
                        if (row.find(needle) != std::string::npos) return true;
                    return false;
                };
                if (!contains(height == 720 ? "NATIVE DIRECT" : "LINEAR UPSCALE INCLUDED")) {
                    std::cerr << "FAIL: missing target mode\n";
                    return 1;
                }
                for (const char* field : {"TARGET ", "completed", "invalid", "dropped",
                                          "mean", "p95", "max", "WINDOW", "TICKS", "RESUMES",
                                          "WORST GPU TICK", "TRACK/CAR DRAWS", "GPFIFO bound"}) {
                    if (!contains(field)) {
                        std::cerr << "FAIL: missing GPU field " << field << '\n';
                        return 1;
                    }
                }
            } catch (const std::exception& error) {
                std::cerr << "FAIL: scene height " << height << ": " << error.what() << '\n';
                return 1;
            }
        }
    }
    std::cout << "PASS: every profile page fits at all scene heights\n";
}
