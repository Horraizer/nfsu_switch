#include "track_animation_cost.h"
#include "track_world_model_cost.h"
// The time-trial events (owner task 2026-10-18): the seven kind-2 and
// kind-3 single-race time-trial career events, the unlockable races the
// career's availability rule treats specially. Each is one
// point-to-point pass on a sprint track whose manifest and loader the
// sprint family carries; the lanes are the sprint follow-up's, so every
// row is pinned load-only (see docs/checkpoints/timetrial-glm-20261018.md).
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
struct TimetrialTrackCase {
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

const TimetrialTrackCase kCases[] = {
    {7, 1102, 2, 1, "L1RA", 105, 15, 14351488, 29719156, {}, false, 0},
    {15, 1104, 2, 0, "L1RA", 195, 12, 19122592, 43747900, {}, false, 0},
    {26, 1105, 2, 0, "L1RA", 208, 10, 18817184, 45396032, {}, false, 0},
    {47, 1103, 2, 1, "L1RA", 169, 12, 18263584, 40710532, {}, false, 0},
    {68, 1106, 2, 0, "L1RA", 197, 9, 19064896, 48663340, {0x74CFB820U}, false, 0},
    {77, 1107, 2, 1, "L1RA", 199, 9, 19209280, 49078744, {0x74CFB820U}, false, 0},
    {97, 1109, 2, 0, "L1RA", 161, 12, 18980928, 42647232, {}, false, 0},
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
    for (const nfsu::SupportedTimetrialRow& row : nfsu::kSupportedTimetrialRows) {
        const nfsu::SupportedTimetrialRow* found = nfsu::supported_timetrial_row(row.event);
        require(found && found->track == row.track && found->laps == row.laps &&
                found->direction == row.direction && found->lap_skip == 1,
                "supported time-trial row lookup failed");
        nfsu::RaceRun run;
        require(run.set_event(row.event, error), error);
        std::string name;
        require(nfsu::first_lap_asset_set_name(static_cast<std::uint32_t>(row.track), name),
                "time-trial track has no asset set");
        nfsu::AssetManifest manifest;
        require(nfsu::retail_asset_manifest(name, manifest), "time-trial asset set missing");
        // No sprint-track lane exists yet (the sprint follow-up); every
        // time-trial event stays on the progress spline.
        require(nfsu::race_event_driving_line(row.event) ==
                    nfsu::RaceDrivingLine::progress_spline,
                "time-trial event received an unproven lane");
    }
    std::puts("PASS: time-trial selection, manifest and lane guards");
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
        require(argc <= 2 || measure, "invalid timetrial tracks test mode");
        const auto root = argument_path(argv[1]);
        for (const TimetrialTrackCase& timetrial : kCases) {
            std::printf("== event %u track %u laps %d direction %d\n",
                        timetrial.event, timetrial.track, timetrial.laps, timetrial.direction);
            std::string error;
            SupportedRace selection;
            require(load_supported_race(root/"Frontend/FRONTB.LZC",
                                        root/"Global/GLOBALB.BUN", timetrial.event,
                                        selection, error), error);
            require(selection.slot.track == static_cast<std::int16_t>(timetrial.track) &&
                    selection.slot.laps == timetrial.laps &&
                    selection.slot.direction == timetrial.direction &&
                    selection.track.region == timetrial.region &&
                    selection.track.lap_skip == 1,
                    "retail time-trial selection changed");
            RaceGpuScene scene;
            scene.set_wet_road_reflections(true);
            require(scene.load(posix_file_system(), root, error, nullptr, nullptr,
                               false, true, true, nullptr, nullptr, timetrial.event), error);
            const auto& track = scene.track();
            const auto world_cost=explicit_world_model_cost(track);
            std::cout << "track " << track.track_number << " sections " << track.sections
                      << " textures " << track.texture_bytes << " streaming "
                      << track.streaming_bytes << " spillover "
                      << track.texture_spillover_sections << " unresolved "
                      << track.scene.region.unresolved_hashes.size() << '\n';
            require(track.event_number == timetrial.event &&
                    track.track_number == timetrial.track &&
                    track.sections == timetrial.sections &&
                    track.texture_spillover_sections == timetrial.spillover_sections &&
                    track.texture_bytes == timetrial.texture_bytes + explicit_animation_bytes(track.scene.region) + world_cost.textures &&
                    track.streaming_bytes == timetrial.streaming_bytes + explicit_animation_bytes(track.scene.region) + world_cost.total,
                    "uploaded time-trial selection differs from its pin");
            require(track.scene.region.unresolved_hashes.size() == timetrial.unresolved.size(),
                    "unresolved placed material set differs");
            for (std::size_t i = 0; i < timetrial.unresolved.size(); ++i)
                require(track.scene.region.unresolved_hashes[i] == timetrial.unresolved[i],
                        "unresolved placed material hash differs");
            require(track.streaming_bytes + track.texture_bytes <=
                        kResidentRegionsPlaceholder * kStreamingRegionBudgetPlaceholder,
                    "time-trial streaming payload exceeds the plan");
            RaceRun run;
            require(run.set_event(timetrial.event, error) &&
                    run.prepare_metadata(root, error) &&
                    run.start(root, track, error), error);
            RaceGpuFrame frame;
            require(scene.frame(run.session(), frame, error), error);
            require(frame.commands.draw_count() != 0, "time-trial GPU frame must draw");
            const UploadTable uploaded = frame.commands.resource_table();
            if (!measure || !timetrial.drive) {
                std::printf("PASS: knockout %u loaded and started\n", timetrial.track);
                continue;
            }
            // The Atlantic standard: the event's lane drives the race to
            // completion through the shared simulate/commit path within
            // the reset budget, and every later frame stays a valid draw
            // update. The rows without a completing lane (the circuits'
            // documented stall gap) stay load-only.
            RaceRouteDriver driver(race_event_driving_line(timetrial.event));
            while (run.session().timing().outcome == RaceOutcome::running &&
                   run.session().timing().step < kRaceInputMaxTicks) {
                const auto pad = driver.sample(run.session());
                require(run.simulate(pad, error) && run.commit(error), error);
                require(run.session().telemetry().resets <= timetrial.max_resets,
                        "time-trial drive reset");
                if (run.session().timing().step % 6000 == 0) {
                    require(scene.frame(run.session(), frame, error), error);
                    require(frame.commands.draw_count() != 0, "later timetrial frame empty");
                    require(validate_draw_update(uploaded, frame.commands,
                                                  kTrackSliceDrawLimit, error), error);
                    std::printf("shared tick %zu lap %d\n",
                                run.session().timing().step,
                                run.session().progress().max_lap);
                }
            }
            require(run.session().timing().outcome != RaceOutcome::running,
                    "time-trial drive did not finish within the input budget");
            require(run.session().timing().racers[0].status ==
                        RacerStatus::normal_completion,
                    "time-trial drive did not finish normally");
            require(run.session().telemetry().resets <= timetrial.max_resets,
                    "time-trial drive reset");
            std::printf("PASS: timetrial %u driven, laps %d, resets %zu\n",
                        timetrial.track, run.session().progress().max_lap,
                        run.session().telemetry().resets);
        }
        std::puts(measure ? "PASS: the time-trial events load and start"
                          : "PASS: all seven time-trial events load and start");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
