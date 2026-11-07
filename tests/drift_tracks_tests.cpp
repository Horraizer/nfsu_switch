#include "track_animation_cost.h"
#include "track_world_model_cost.h"
// The drift tracks (owner tasks 2026-10-12 and 2026-10-14): every
// single-race drift event of the career on tracks 1301-1308 - loads,
// resolves its placed materials (every verified-absent hash substituted
// with retail DEFAULTALPHA), starts a race on its own course and drives it
// to completion through controller input. The drift tournament event 38
// is connected separately through its authored round rows.
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

// One row per supported drift track: the representative event the retail
// checks drive, and the per-track facts the load pins. Every derived lane
// drives its race to completion without a reset, so the reset budget is
// zero across the board.
struct DriftTrackCase {
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

const DriftTrackCase kCases[] = {
    // The verified-absent placed hashes are all substituted per region
    // (see absent_placed_hashes), so nothing stays unresolved anywhere.
    {3, 1301, 4, 0, "L1RB", 8, 0, 5399424, 7140366, {}, true, 0},
    {13, 1302, 4, 0, "L1RB", 8, 0, 5399424, 7237386, {}, true, 0},
    {25, 1303, 4, 0, "L1RB", 8, 0, 5403520, 7211086, {}, true, 0},
    {28, 1304, 4, 0, "L1RB", 8, 0, 5403520, 7225180, {}, true, 0},
    {44, 1305, 4, 0, "L1RC", 11, 0, 2977920, 6090780, {}, true, 0},
    {65, 1306, 4, 0, "L1RC", 11, 0, 2977920, 6116250, {}, true, 0},
    {90, 1307, 4, 0, "L1RC", 11, 0, 2977920, 6101478, {}, true, 0},
    {105, 1308, 4, 0, "L1RC", 11, 0, 2977920, 6088140, {}, true, 0},
};

void guard_tests() {
    std::string error;
    // Missing and unknown event numbers are refused before retail reads.
    for (const std::uint32_t refused : {85U, 118U, 123U, 125U, 126U, 127U}) {
        nfsu::RaceRun run;
        require(!run.set_event(refused, error), "unsupported event was accepted");
    }
    // The supported drift row is accepted and names its track.
    for (const nfsu::SupportedDriftRow& row : nfsu::kSupportedDriftRows) {
        const nfsu::SupportedDriftRow* found = nfsu::supported_drift_row(row.event);
        require(found && found->track == row.track && found->laps == row.laps &&
                found->direction == row.direction, "supported drift row lookup failed");
        nfsu::RaceRun run;
        require(run.set_event(row.event, error), error);
        std::string name;
        require(nfsu::first_lap_asset_set_name(static_cast<std::uint32_t>(row.track), name),
                "drift track has no asset set");
        nfsu::AssetManifest manifest;
        require(nfsu::retail_asset_manifest(name, manifest), "drift asset set missing");
        // The driving line factory covers the drift event.
        require(nfsu::race_event_driving_line(row.event) !=
                    nfsu::RaceDrivingLine::progress_spline,
                "supported drift event has no driving lane");
    }
    // Tournament38 uses round rows rather than a single-race row.
    require(nfsu::supported_drift_row(38) == nullptr,
            "drift tournament mixed into the single-race table");
    require(nfsu::race_event_driving_line(38) == nfsu::RaceDrivingLine::progress_spline,
            "drift tournament received a lane");
    // Spot pins of the lane mapping.
    require(nfsu::race_event_driving_line(3) == nfsu::RaceDrivingLine::drift1301_forward &&
            nfsu::race_event_driving_line(13) == nfsu::RaceDrivingLine::drift1302_forward &&
            nfsu::race_event_driving_line(105) == nfsu::RaceDrivingLine::drift1308_forward,
            "drift lane mapping broke");
    require(nfsu::race_event_driving_line(1) == nfsu::RaceDrivingLine::event1_forward &&
            nfsu::race_event_driving_line(8) == nfsu::RaceDrivingLine::event8_forward,
            "events 1 and 8 must keep their authored lanes");
    std::string name;
    require(nfsu::first_lap_asset_set_name(1301, name) && name == "first-lap-1301" &&
            nfsu::first_lap_asset_set_name(1308, name) && name == "first-lap-1308" &&
            !nfsu::first_lap_asset_set_name(1309, name),
            "drift asset set naming broke");
    std::puts("PASS: drift selection, manifest and lane guards");
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
        require(argc <= 2 || measure, "invalid drift tracks test mode");
        const auto root = argument_path(argv[1]);
        for (const DriftTrackCase& drift : kCases) {
            std::printf("== event %u track %u laps %d direction %d\n",
                        drift.event, drift.track, drift.laps, drift.direction);
            std::string error;
            SupportedRace selection;
            require(load_supported_race(root/"Frontend/FRONTB.LZC",
                                        root/"Global/GLOBALB.BUN", drift.event,
                                        selection, error), error);
            require(selection.slot.track == static_cast<std::int16_t>(drift.track) &&
                    selection.slot.laps == drift.laps &&
                    selection.slot.direction == drift.direction &&
                    selection.track.region == drift.region && selection.track.lap_skip == 0,
                    "retail drift selection changed");
            RaceGpuScene scene;
            scene.set_wet_road_reflections(true);
            require(scene.load(posix_file_system(), root, error, nullptr, nullptr,
                               false, true, true, nullptr, nullptr, drift.event), error);
            const auto& track = scene.track();
            const auto world_cost=explicit_world_model_cost(track);
            std::cout << "track " << track.track_number << " sections " << track.sections
                      << " textures " << track.texture_bytes << " streaming "
                      << track.streaming_bytes << " spillover "
                      << track.texture_spillover_sections << " unresolved "
                      << track.scene.region.unresolved_hashes.size() << '\n';
            require(track.event_number == drift.event &&
                    track.track_number == drift.track &&
                    track.sections == drift.sections &&
                    track.texture_spillover_sections == drift.spillover_sections &&
                    track.texture_bytes == drift.texture_bytes + explicit_animation_bytes(track.scene.region) + world_cost.textures &&
                    track.streaming_bytes == drift.streaming_bytes + explicit_animation_bytes(track.scene.region) + world_cost.total,
                    "uploaded drift selection differs from its pin");
            require(track.scene.region.unresolved_hashes.size() == drift.unresolved.size(),
                    "unresolved placed material set differs");
            for (std::size_t i = 0; i < drift.unresolved.size(); ++i)
                require(track.scene.region.unresolved_hashes[i] == drift.unresolved[i],
                        "unresolved placed material hash differs");
            require(track.streaming_bytes + track.texture_bytes <=
                        kResidentRegionsPlaceholder * kStreamingRegionBudgetPlaceholder,
                    "drift streaming payload exceeds the plan");
            RaceRun run;
            require(run.set_event(drift.event, error) &&
                    run.prepare_metadata(root, error) &&
                    run.start(root, track, error), error);
            RaceGpuFrame frame;
            require(scene.frame(run.session(), frame, error), error);
            require(frame.commands.draw_count() != 0, "drift GPU frame must draw");
            const UploadTable uploaded = frame.commands.resource_table();
            if (!measure || !drift.drive) continue;
            // The Atlantic standard the circuits set: the derived lane
            // drives the race to completion through the shared
            // simulate/commit path, without a reset, and every later frame
            // stays a valid draw update.
            RaceRouteDriver driver(race_event_driving_line(drift.event));
            while (run.session().timing().outcome == RaceOutcome::running &&
                   run.session().timing().step < kRaceInputMaxTicks) {
                const auto pad = driver.sample(run.session());
                require(run.simulate(pad, error) && run.commit(error), error);
                require(run.session().telemetry().resets <= drift.max_resets,
                        "drift drive reset");
                if (run.session().timing().step % 6000 == 0) {
                    require(scene.frame(run.session(), frame, error), error);
                    require(frame.commands.draw_count() != 0, "later drift frame empty");
                    require(validate_draw_update(uploaded, frame.commands,
                                                  kTrackSliceDrawLimit, error), error);
                    std::printf("shared tick %zu lap %d\n",
                                run.session().timing().step,
                                run.session().progress().max_lap);
                }
            }
            require(run.session().timing().outcome != RaceOutcome::running,
                    "drift drive did not finish within the input budget");
            require(run.session().timing().racers[0].status ==
                        RacerStatus::normal_completion,
                    "drift drive did not finish normally");
            require(run.session().telemetry().resets <= drift.max_resets,
                    "drift drive reset");
            std::printf("PASS: drift %u driven, laps %d, resets %zu\n",
                        drift.track, run.session().progress().max_lap,
                        run.session().telemetry().resets);
        }
        std::puts(measure ? "PASS: all eight drift tracks load and drive" :
                            "PASS: all eight drift tracks load");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
