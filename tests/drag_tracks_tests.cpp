#include "track_animation_cost.h"
#include "track_world_model_cost.h"
// The drag tracks (owner task 2026-10-15): every single-race drag career
// event on the six open point-to-point tracks 1201, 1202, 1206, 1207, 1210
// and 1214 - loads through the retail open-spline route the task taught
// the port, resolves its placed materials (every verified-absent hash
// substituted with retail DEFAULTALPHA except 0x74CFB820 on 1207, which
// ships unresolved exactly as tracks 1007 and 1008 do) and starts a race
// on its own course. Tournaments9/20 use the separate round-aware selector;
// tournament80 is connected through its round rows. The auto-driver lanes are follow-up work: the drag splines run
// beside or across the highway and no lane completes a race yet, so every
// row is pinned load-only (see docs/checkpoints/drag-glm-20261015.md).
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
struct DragTrackCase {
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

const DragTrackCase kCases[] = {
    {4, 1210, 2, 1, "L1RB", 35, 0, 8442368, 13820954, {}, false, 0},
    {51, 1210, 2, 0, "L1RB", 35, 0, 8442368, 13820954, {}, false, 0},
    {11, 1202, 2, 0, "L1RC", 33, 0, 11392512, 19401984, {}, false, 0},
    {108, 1202, 2, 1, "L1RC", 33, 0, 11392512, 19401984, {}, false, 0},
    {36, 1206, 2, 0, "L1RB", 35, 0, 5724672, 8994102, {}, false, 0},
    {55, 1206, 2, 1, "L1RB", 35, 0, 5724672, 8994102, {}, false, 0},
    {83, 1207, 2, 0, "L1RE", 45, 0, 5167040, 11383844, {0x74CFB820U}, false, 0},
    {98, 1207, 2, 1, "L1RE", 45, 0, 5167040, 11383844, {0x74CFB820U}, false, 0},
    {21, 1201, 2, 0, "L1RB", 36, 0, 8545792, 14139562, {}, false, 0},
    {17, 1201, 2, 1, "L1RB", 36, 0, 8545792, 14139562, {}, false, 0},
    {70, 1214, 2, 0, "L1RB", 35, 0, 5904896, 9328442, {}, false, 0},
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
    for (const nfsu::SupportedDragRow& row : nfsu::kSupportedDragRows) {
        const nfsu::SupportedDragRow* found = nfsu::supported_drag_row(row.event);
        require(found && found->track == row.track && found->laps == row.laps &&
                found->direction == row.direction && found->lap_skip == 1,
                "supported drag row lookup failed");
        nfsu::RaceRun run;
        require(run.set_event(row.event, error), error);
        std::string name;
        require(nfsu::first_lap_asset_set_name(static_cast<std::uint32_t>(row.track), name),
                "drag track has no asset set");
        nfsu::AssetManifest manifest;
        require(nfsu::retail_asset_manifest(name, manifest), "drag asset set missing");
        // The forward events take their derived lane; the reverse events
        // stay on the progress spline until the reverse derivation is
        // fixed (its output came out byte-identical to the forward table).
        if (row.direction == 0) {
            require(nfsu::race_event_driving_line(row.event) !=
                        nfsu::RaceDrivingLine::progress_spline,
                    "forward drag event has no lane");
        } else {
            require(nfsu::race_event_driving_line(row.event) ==
                        nfsu::RaceDrivingLine::progress_spline,
                    "reverse drag event received the unproven lane");
        }
    }
    // Tournaments have no single-event row or single-event driving lane.
    require(nfsu::supported_drag_row(9) == nullptr &&
            nfsu::supported_drag_row(20) == nullptr &&
            nfsu::supported_drag_row(80) == nullptr,
            "drag tournament listed as supported");
    require(nfsu::race_event_driving_line(9) == nfsu::RaceDrivingLine::progress_spline,
            "drag tournament received a lane");
    std::puts("PASS: drag selection, manifest and lane guards");
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
        require(argc <= 2 || measure, "invalid drag tracks test mode");
        const auto root = argument_path(argv[1]);
        for (const DragTrackCase& drag : kCases) {
            std::printf("== event %u track %u laps %d direction %d\n",
                        drag.event, drag.track, drag.laps, drag.direction);
            std::string error;
            SupportedRace selection;
            require(load_supported_race(root/"Frontend/FRONTB.LZC",
                                        root/"Global/GLOBALB.BUN", drag.event,
                                        selection, error), error);
            require(selection.slot.track == static_cast<std::int16_t>(drag.track) &&
                    selection.slot.laps == drag.laps &&
                    selection.slot.direction == drag.direction &&
                    selection.track.region == drag.region &&
                    selection.track.lap_skip == 1,
                    "retail drag selection changed");
            RaceGpuScene scene;
            scene.set_wet_road_reflections(true);
            require(scene.load(posix_file_system(), root, error, nullptr, nullptr,
                               false, true, true, nullptr, nullptr, drag.event), error);
            const auto& track = scene.track();
            const auto world_cost=explicit_world_model_cost(track);
            std::cout << "track " << track.track_number << " sections " << track.sections
                      << " textures " << track.texture_bytes << " streaming "
                      << track.streaming_bytes << " spillover "
                      << track.texture_spillover_sections << " unresolved "
                      << track.scene.region.unresolved_hashes.size() << '\n';
            require(track.event_number == drag.event &&
                    track.track_number == drag.track &&
                    track.sections == drag.sections &&
                    track.texture_spillover_sections == drag.spillover_sections &&
                    track.texture_bytes == drag.texture_bytes + explicit_animation_bytes(track.scene.region) + world_cost.textures &&
                    track.streaming_bytes == drag.streaming_bytes + explicit_animation_bytes(track.scene.region) + world_cost.total,
                    "uploaded drag selection differs from its pin");
            require(track.scene.region.unresolved_hashes.size() == drag.unresolved.size(),
                    "unresolved placed material set differs");
            for (std::size_t i = 0; i < drag.unresolved.size(); ++i)
                require(track.scene.region.unresolved_hashes[i] == drag.unresolved[i],
                        "unresolved placed material hash differs");
            require(track.streaming_bytes + track.texture_bytes <=
                        kResidentRegionsPlaceholder * kStreamingRegionBudgetPlaceholder,
                    "drag streaming payload exceeds the plan");
            RaceRun run;
            require(run.set_event(drag.event, error) &&
                    run.prepare_metadata(root, error) &&
                    run.start(root, track, error), error);
            RaceGpuFrame frame;
            require(scene.frame(run.session(), frame, error), error);
            require(frame.commands.draw_count() != 0, "drag GPU frame must draw");
            std::printf("PASS: drag %u loaded and started, laps %d\n",
                        drag.track, run.session().progress().max_lap);
        }
        std::puts("PASS: all six drag tracks load and start");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
