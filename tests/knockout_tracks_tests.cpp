#include "track_animation_cost.h"
#include "track_world_model_cost.h"
// The lap-knockout events (owner task 2026-10-18): the twelve kind-0
// single-race lap-knockout career events on the closed circuit tracks,
// whose manifests and lanes the circuit families already carry (plus
// track 1003's reverse lane, derived for events 5 and 10 - no circuit
// event races that direction). The elimination rule needs opponents the
// solo port does not simulate, so the port runs each race as its three
// laps; the drive pins stay load-only until the circuit lanes' documented
// stall gap closes (see docs/checkpoints/knockout-glm-20261018.md).
#include "nfsu/race_gpu_scene.h"
#include "nfsu/race_events.h"
#include "nfsu/memory_budget.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "nfsu/draw_execution.h"
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool okay, const std::string& message) {
    if (!okay) throw std::runtime_error(message);
}

// One row per drag track: the representative events the retail checks load
// (both directions where the career races them), and the per-track facts
// the load pins. `drive` stays false until a lane finishes a race.
struct KnockoutTrackCase {
    std::uint32_t event;
    std::uint32_t track;
    int laps;
    int direction;
    const char* region;
    std::size_t sections;
    std::size_t spillover_sections;
    std::uint64_t texture_bytes;
    std::uint64_t streaming_bytes;
    std::vector<std::uint32_t> unresolved;
    bool drive;
    std::size_t max_resets;
};

const KnockoutTrackCase kCases[] = {
    {5, 1003, 3, 1, "L1RA", 78, 19, 14703808, 28378198, {0x2D1A7A07U, 0x74CFB820U}, false, 0},
    {10, 1003, 3, 1, "L1RA", 78, 19, 14703808, 28378198, {0x2D1A7A07U, 0x74CFB820U}, false, 0},
    {24, 1006, 3, 1, "L1RA", 96, 17, 15332640, 28897350, {}, false, 0},
    {39, 1006, 3, 1, "L1RA", 96, 17, 15332640, 28897350, {}, false, 0},
    {31, 1005, 3, 0, "L1RA", 90, 18, 14514080, 27916010, {}, true, 0},
    {49, 1008, 3, 0, "L1RA", 102, 17, 15368736, 29261298, {0x74CFB820U}, false, 0},
    {58, 1008, 3, 1, "L1RA", 102, 17, 15368736, 29261298, {0x74CFB820U}, false, 0},
    {60, 1006, 3, 0, "L1RA", 96, 17, 15332640, 28897350, {}, false, 0},
    {72, 1001, 3, 0, "L1RA", 130, 17, 16012320, 31689480, {}, true, 3},
    {84, 1001, 3, 1, "L1RA", 130, 17, 16012320, 31689480, {}, false, 0},
    {103, 1001, 3, 0, "L1RA", 130, 17, 16012320, 31689480, {}, true, 3},
    {91, 1007, 3, 0, "L1RA", 96, 17, 14961216, 31091964, {0x74CFB820U}, false, 0},
};

void guard_tests() {
    std::string error;
    // Missing and unknown events are refused before reading retail files.
    // Authored tournaments are now connected by the full campaign.
    for (const std::uint32_t refused : {85U, 118U, 123U, 125U, 126U, 127U}) {
        nfsu::RaceRun run;
        require(!run.set_event(refused, error), "unsupported event was accepted");
    }
    // The supported drag row is accepted and names its track.
    for (const nfsu::SupportedKnockoutRow& row : nfsu::kSupportedKnockoutRows) {
        const nfsu::SupportedKnockoutRow* found = nfsu::supported_knockout_row(row.event);
        require(found && found->track == row.track && found->laps == row.laps &&
                found->direction == row.direction,
                "supported knockout row lookup failed");
        nfsu::RaceRun run;
        require(run.set_event(row.event, error), error);
        std::string name;
        require(nfsu::first_lap_asset_set_name(static_cast<std::uint32_t>(row.track), name),
                "knockout track has no asset set");
        nfsu::AssetManifest manifest;
        require(nfsu::retail_asset_manifest(name, manifest), "knockout asset set missing");
        // Every knockout event rides the lane its track and direction
        // already carry (1003's reverse derived for events 5 and 10).
        require(nfsu::race_event_driving_line(row.event) !=
                    nfsu::RaceDrivingLine::progress_spline,
                "knockout event has no lane");
    }
    // The knockout events that are not single races stay refused; every
    // lap-knockout event is, so spot pins suffice.
    require(nfsu::race_event_driving_line(5) == nfsu::RaceDrivingLine::circuit1003_reverse &&
            nfsu::race_event_driving_line(31) == nfsu::RaceDrivingLine::event8_forward &&
            nfsu::race_event_driving_line(103) == nfsu::RaceDrivingLine::circuit1001_forward,
            "knockout lane mapping broke");
    std::puts("PASS: knockout selection, manifest and lane guards");
}
} // namespace

int main(int argc, char** argv) {
    using namespace nfsu;
    try {
        guard_tests();
        if (argc == 1) return 0;
        std::string error;
        require(check_arguments(argc, argv, 1, error), error);
        const bool measure = argc == 3 && std::string(argv[2]) == "--measure";
        require(argc <= 2 || measure, "invalid knockout tracks test mode");
        const auto root = argument_path(argv[1]);
        for (const KnockoutTrackCase& knockout : kCases) {
            std::printf("== event %u track %u laps %d direction %d\n",
                        knockout.event, knockout.track, knockout.laps, knockout.direction);
            std::string error;
            SupportedRace selection;
            require(load_supported_race(root/"Frontend/FRONTB.LZC",
                                        root/"Global/GLOBALB.BUN", knockout.event,
                                        selection, error), error);
            require(selection.slot.track == static_cast<std::int16_t>(knockout.track) &&
                    selection.slot.laps == knockout.laps &&
                    selection.slot.direction == knockout.direction &&
                    selection.track.region == knockout.region &&
                    selection.track.lap_skip == 0,
                    "retail knockout selection changed");
            RaceGpuScene scene;
            scene.set_wet_road_reflections(true);
            require(scene.load(posix_file_system(), root, error, nullptr, nullptr,
                               false, true, true, nullptr, nullptr, knockout.event), error);
            const auto& track = scene.track();
            const auto world_cost=explicit_world_model_cost(track);
            std::cout << "track " << track.track_number << " sections " << track.sections
                      << " textures " << track.texture_bytes << " streaming "
                      << track.streaming_bytes << " spillover "
                      << track.texture_spillover_sections << " unresolved "
                      << track.scene.region.unresolved_hashes.size() << '\n';
            require(track.event_number == knockout.event &&
                    track.track_number == knockout.track &&
                    track.sections == knockout.sections &&
                    track.texture_spillover_sections == knockout.spillover_sections &&
                    track.texture_bytes == knockout.texture_bytes + explicit_animation_bytes(track.scene.region) + world_cost.textures &&
                    track.streaming_bytes == knockout.streaming_bytes + explicit_animation_bytes(track.scene.region) + world_cost.total,
                    "uploaded knockout selection differs from its pin");
            require(track.scene.region.unresolved_hashes.size() == knockout.unresolved.size(),
                    "unresolved placed material set differs");
            for (std::size_t i = 0; i < knockout.unresolved.size(); ++i)
                require(track.scene.region.unresolved_hashes[i] == knockout.unresolved[i],
                        "unresolved placed material hash differs");
            require(track.streaming_bytes + track.texture_bytes <=
                        kResidentRegionsPlaceholder * kStreamingRegionBudgetPlaceholder,
                    "knockout streaming payload exceeds the plan");
            RaceRun run;
            require(run.set_event(knockout.event, error) &&
                    run.prepare_metadata(root, error) &&
                    run.start(root, track, error), error);
            RaceGpuFrame frame;
            require(scene.frame(run.session(), frame, error), error);
            require(frame.commands.draw_count() != 0, "knockout GPU frame must draw");
            const UploadTable uploaded = frame.commands.resource_table();
            if (!measure || !knockout.drive) {
                std::printf("PASS: knockout %u loaded and started\n", knockout.track);
                continue;
            }
            // The Atlantic standard: the event's lane drives the race to
            // completion through the shared simulate/commit path within
            // the reset budget, and every later frame stays a valid draw
            // update. The rows without a completing lane (the circuits'
            // documented stall gap) stay load-only.
            RaceRouteDriver driver(race_event_driving_line(knockout.event));
            while (run.session().timing().outcome == RaceOutcome::running &&
                   run.session().timing().step < kRaceInputMaxTicks) {
                const auto pad = driver.sample(run.session());
                require(run.simulate(pad, error) && run.commit(error), error);
                require(run.session().telemetry().resets <= knockout.max_resets,
                        "knockout drive reset");
                if (run.session().timing().step % 6000 == 0) {
                    require(scene.frame(run.session(), frame, error), error);
                    require(frame.commands.draw_count() != 0, "later knockout frame empty");
                    require(validate_draw_update(uploaded, frame.commands,
                                                  kTrackSliceDrawLimit, error), error);
                    std::printf("shared tick %zu lap %d\n",
                                run.session().timing().step,
                                run.session().progress().max_lap);
                }
            }
            require(run.session().timing().outcome != RaceOutcome::running,
                    "knockout drive did not finish within the input budget");
            require(run.session().timing().racers[0].status ==
                        RacerStatus::normal_completion,
                    "knockout drive did not finish normally");
            require(run.session().telemetry().resets <= knockout.max_resets,
                    "knockout drive reset");
            std::printf("PASS: knockout %u driven, laps %d, resets %zu\n",
                        knockout.track, run.session().progress().max_lap,
                        run.session().telemetry().resets);
        }
        std::puts(measure ? "PASS: the knockout events load, and the completing lanes drive"
                          : "PASS: all twelve knockout events load and start");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
