#include "track_animation_cost.h"
#include "track_world_model_cost.h"
// The six further circuit tracks (owner task 2026-10-10): every supported
// single-race circuit event beyond events 1 and 8 - tracks 1001, 1002,
// 1004, 1006, 1007 and 1008, both directions - loads, resolves its placed
// materials the way its track requires, starts a race on its own course and
// drives it to completion through controller input. Events 1 and 8 keep
// their own tests (nfsu_race_run_tests, nfsu_circuit1005_tests).
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

// One row per supported circuit track beyond 1003/1005: the representative
// event the retail checks drive, and the per-track facts the load pins.
// The pins move with the loader; the test is their record. `drive` marks
// the tracks whose derived lane completes the race today, with the reset
// budget the lane measured (track 1001's climbs take one a lap); the others
// load, render and stay hand-drivable, and their row pins that state until
// their lane is finished (see the handoff).
struct CircuitTrackCase {
    std::uint32_t event;
    std::uint32_t track;
    int laps;
    int direction;
    std::size_t sections;
    std::size_t spillover_sections;
    std::uint64_t texture_bytes;
    std::uint64_t streaming_bytes;
    std::vector<std::uint32_t> unresolved;
    bool drive;
    std::size_t max_resets;
};

const CircuitTrackCase kCases[] = {
    {19, 1001, 3, 0, 130, 17, 16012320, 31689480, {}, true, 3},
    {59, 1002, 4, 1, 109, 16, 15193120, 28443586, {}, false, 0},
    {89, 1004, 3, 0, 81, 17, 13948288, 25841518, {}, false, 0},
    {122, 1006, 3, 0, 96, 17, 15332640, 28897350, {}, false, 0},
    {99, 1007, 6, 0, 96, 17, 14961216, 31091964, {0x74CFB820U}, false, 0},
    {32, 1008, 3, 0, 102, 17, 15368736, 29261298, {0x74CFB820U}, false, 0},
};

void guard_tests() {
    std::string error;
    // Selection guards: tournaments, other types, the new-career event,
    // unknown numbers and the drift event 3 (supported since the 2026-10-12
    // first-drift task, but not a circuit) are refused before any retail
    // file is read. Event 3 is asserted on the drift side.
    for (const std::uint32_t refused : {118U, 125U, 126U, 127U, 85U, 123U}) {
        nfsu::RaceRun run;
        require(!run.set_event(refused, error), "unsupported event was accepted");
    }
    require(nfsu::supported_circuit_row(3) == nullptr,
            "drift event 3 must not be a circuit row");
    // Every supported row is accepted and names its track.
    for (const nfsu::SupportedCircuitRow& row : nfsu::kSupportedCircuitRows) {
        const nfsu::SupportedCircuitRow* found = nfsu::supported_circuit_row(row.event);
        require(found && found->track == row.track && found->laps == row.laps &&
                found->direction == row.direction, "supported row lookup failed");
        nfsu::RaceRun run;
        require(run.set_event(row.event, error), error);
        error.clear();
        std::string name;
        require(nfsu::first_lap_asset_set_name(static_cast<std::uint32_t>(row.track), name),
                "circuit track has no asset set");
        nfsu::AssetManifest manifest;
        require(nfsu::retail_asset_manifest(name, manifest), "circuit asset set missing");
        // The driving line factory covers every supported circuit.
        require(nfsu::race_event_driving_line(row.event) !=
                    nfsu::RaceDrivingLine::progress_spline,
                "supported circuit has no driving lane");
    }
    for (const std::uint32_t extra : {26U, 12U, 112U, 119U}) {
        require(nfsu::supported_circuit_row(extra) == nullptr,
                "non-circuit event listed as supported");
        require(nfsu::race_event_driving_line(extra) == nfsu::RaceDrivingLine::progress_spline,
                "non-circuit event received a lane");
    }
    require(nfsu::race_event_driving_line(1) == nfsu::RaceDrivingLine::event1_forward &&
            nfsu::race_event_driving_line(8) == nfsu::RaceDrivingLine::event8_forward,
            "events 1 and 8 must keep their authored lanes");
    std::string name;
    require(nfsu::first_lap_asset_set_name(1003, name) && name == "first-lap" &&
            nfsu::first_lap_asset_set_name(1005, name) && name == "first-lap-1005" &&
            nfsu::first_lap_asset_set_name(1001, name) && name == "first-lap-1001" &&
            !nfsu::first_lap_asset_set_name(1101, name),
            "asset set naming broke");
    std::puts("PASS: circuit selection, manifest and lane guards");
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
        require(argc <= 2 || measure, "invalid circuit tracks test mode");
        const auto root = argument_path(argv[1]);
        for (const CircuitTrackCase& circuit : kCases) {
            std::printf("== event %u track %u laps %d direction %d\n",
                        circuit.event, circuit.track, circuit.laps, circuit.direction);
            std::string error;
            SupportedRace selection;
            require(load_supported_race(root/"Frontend/FRONTB.LZC",
                                        root/"Global/GLOBALB.BUN", circuit.event,
                                        selection, error), error);
            require(selection.slot.track == static_cast<std::int16_t>(circuit.track) &&
                    selection.slot.laps == circuit.laps &&
                    selection.slot.direction == circuit.direction &&
                    selection.track.region == "L1RA" && selection.track.lap_skip == 0,
                    "retail circuit selection changed");
            RaceGpuScene scene;
            scene.set_wet_road_reflections(true);
            require(scene.load(posix_file_system(), root, error, nullptr, nullptr,
                               false, true, true, nullptr, nullptr, circuit.event), error);
            const auto& track = scene.track();
            const auto world_cost=explicit_world_model_cost(track);
            std::cout << "track " << track.track_number << " sections " << track.sections
                      << " textures " << track.texture_bytes << " streaming "
                      << track.streaming_bytes << " spillover "
                      << track.texture_spillover_sections << " unresolved "
                      << track.scene.region.unresolved_hashes.size() << '\n';
            require(track.event_number == circuit.event &&
                    track.track_number == circuit.track &&
                    track.sections == circuit.sections &&
                    track.texture_spillover_sections == circuit.spillover_sections &&
                    track.texture_bytes == circuit.texture_bytes + explicit_animation_bytes(track.scene.region) + world_cost.textures &&
                    track.streaming_bytes == circuit.streaming_bytes + explicit_animation_bytes(track.scene.region) + world_cost.total,
                    "uploaded circuit selection differs from its pin");
            require(track.scene.region.unresolved_hashes.size() == circuit.unresolved.size(),
                    "unresolved placed material set differs");
            for (std::size_t i = 0; i < circuit.unresolved.size(); ++i)
                require(track.scene.region.unresolved_hashes[i] == circuit.unresolved[i],
                        "unresolved placed material hash differs");
            require(track.streaming_bytes + track.texture_bytes <=
                        kResidentRegionsPlaceholder * kStreamingRegionBudgetPlaceholder,
                    "circuit streaming payload exceeds the plan");
            RaceRun run;
            require(run.set_event(circuit.event, error) &&
                    run.prepare_metadata(root, error) &&
                    run.start(root, track, error), error);
            RaceGpuFrame frame;
            require(scene.frame(run.session(), frame, error), error);
            require(frame.commands.draw_count() != 0, "circuit GPU frame must draw");
            const UploadTable uploaded = frame.commands.resource_table();
            if (!measure || !circuit.drive) continue;
            // The Atlantic standard for the lanes that reach it: the derived
            // lane drives the race to completion through the shared
            // simulate/commit path, without a reset, and every later frame
            // stays a valid draw update.
            RaceRouteDriver driver(race_event_driving_line(circuit.event));
            while (run.session().timing().outcome == RaceOutcome::running &&
                   run.session().timing().step < kRaceInputMaxTicks) {
                const auto pad = driver.sample(run.session());
                require(run.simulate(pad, error) && run.commit(error), error);
                require(run.session().telemetry().resets <= circuit.max_resets,
                        "circuit drive reset");
                if (run.session().timing().step % 6000 == 0) {
                    require(scene.frame(run.session(), frame, error), error);
                    require(frame.commands.draw_count() != 0, "later circuit frame empty");
                    require(validate_draw_update(uploaded, frame.commands,
                                                  kTrackSliceDrawLimit, error), error);
                    std::printf("shared tick %zu lap %d\n",
                                run.session().timing().step,
                                run.session().progress().max_lap);
                }
            }
            require(run.session().timing().outcome != RaceOutcome::running,
                    "circuit drive did not finish within the input budget");
            require(run.session().timing().racers[0].status ==
                        RacerStatus::normal_completion,
                    "circuit drive did not finish normally");
            require(run.session().telemetry().resets <= circuit.max_resets,
                    "circuit drive reset");
            std::printf("PASS: circuit %u driven, laps %d, resets %zu\n",
                        circuit.track, run.session().progress().max_lap,
                        run.session().telemetry().resets);
        }
        std::puts(measure ? "PASS: all six circuits load and drive" :
                            "PASS: all six circuits load");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
