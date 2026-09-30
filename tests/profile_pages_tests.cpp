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
    context.clocks.configuration = 0x92220008;
    context.clocks.configuration_ok = true;
    context.clocks.gpu_hz = 460800000;
    context.clocks.gpu_ok = true;
    context.storage_bytes = 65536;
    context.gpfifo = {1024, 1280, 430};
    context.tuned = true;
    context.particles = true;
    context.glows = true;
    context.car_shading = true;
    context.wet_road_reflections = true;
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
                bool clock_row = false;
                for (const auto& row : pages[0])
                    if (row.find("0x92220008") != std::string::npos &&
                        row.find("460.8 MHz") != std::string::npos) clock_row = true;
                if (!clock_row) {
                    std::cerr << "FAIL: active GPU configuration or measured clock missing\n";
                    return 1;
                }
                for (std::size_t page = 0; page < pages.size(); ++page) {
                    if (pages[page].size() < 3 || pages[page].size() - 3 > 8) {
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
                for (const char* effect : {"CAR TUNED", "PART ON", "GLOW ON",
                                           "SHADE ON", "WET ON", "SKY ON", "SHADOW ON"}) {
                    for (const auto& page : pages)
                        if (page[page.size()-2].find(effect) == std::string::npos) {
                            std::cerr << "FAIL: missing effect label " << effect << '\n';
                            return 1;
                        }
                }
                const std::string failure(800, 'X');
                const auto wrapped = wrap_screen_text(failure, 80);
                const auto errors = build_profile_pages(replay_rows, true, active ? frames : RaceFrameStats{},
                    2, context, failure, "HANDHELD");
                std::vector<std::string> actual;
                for (std::size_t page = 8; page < errors.size(); ++page) {
                    if (errors[page].size() - 3 > 8) {
                        std::cerr << "FAIL: error page exceeds eight data rows\n";
                        return 1;
                    }
                    actual.insert(actual.end(), errors[page].begin()+2, errors[page].end()-2);
                }
                if (actual != wrapped) {
                    std::cerr << "FAIL: error page dropped wrapped text\n";
                    return 1;
                }
            } catch (const std::exception& error) {
                std::cerr << "FAIL: scene height " << height << ": " << error.what() << '\n';
                return 1;
            }
        }
    }
    std::cout << "PASS: every profile page fits at all scene heights\n";
}
