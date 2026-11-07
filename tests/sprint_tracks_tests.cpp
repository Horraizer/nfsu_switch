#include "track_animation_cost.h"
#include "track_world_model_cost.h"
// The sprint tracks (owner task 2026-10-17): every single-race sprint
// career event of kinds 0 and 6 on the eight open point-to-point tracks
// 1102-1109 - loads through the retail open-spline route, resolves its
// placed materials (the L1RA family's own absent hashes substituted with
// retail DEFAULTALPHA except 0x74CFB820 on 1106-1108, which ships
// unresolved as ever) and starts a race on its own course. The sprint
// tournaments (66 and 102) and kind-5 event112 use the full campaign. The
// auto-driver lanes are follow-up work (the drag task's precedent); every
// row is pinned load-only (see docs/checkpoints/sprint-glm-20261017.md).
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
struct SprintTrackCase {
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

const SprintTrackCase kCases[] = {
    {2, 1102, 2, 0, "L1RA", 105, 15, 14351488, 29719156, {}, false, 0},
    {12, 1103, 2, 0, "L1RA", 169, 12, 18263584, 40710532, {}, false, 0},
    {22, 1103, 2, 1, "L1RA", 169, 12, 18263584, 40710532, {}, false, 0},
    {23, 1104, 2, 0, "L1RA", 195, 12, 19122592, 43747900, {}, false, 0},
    {34, 1105, 2, 1, "L1RA", 208, 10, 18817184, 45396032, {}, false, 0},
    {27, 1106, 2, 0, "L1RA", 197, 9, 19064896, 48663340, {0x74CFB820U}, false, 0},
    {74, 1107, 2, 0, "L1RA", 199, 9, 19209280, 49078744, {0x74CFB820U}, false, 0},
    {95, 1108, 2, 1, "L1RA", 175, 11, 19010112, 46369200, {0x74CFB820U}, false, 0},
    {94, 1109, 2, 0, "L1RA", 161, 12, 18980928, 42647232, {}, false, 0},
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
    for (const nfsu::SupportedSprintRow& row : nfsu::kSupportedSprintRows) {
        const nfsu::SupportedSprintRow* found = nfsu::supported_sprint_row(row.event);
        require(found && found->track == row.track && found->laps == row.laps &&
                found->direction == row.direction && found->lap_skip == 1,
                "supported sprint row lookup failed");
        nfsu::RaceRun run;
        require(run.set_event(row.event, error), error);
        std::string name;
        require(nfsu::first_lap_asset_set_name(static_cast<std::uint32_t>(row.track), name),
                "sprint track has no asset set");
        nfsu::AssetManifest manifest;
        require(nfsu::retail_asset_manifest(name, manifest), "sprint asset set missing");
        // No sprint lane exists yet (the drag task's follow-up class);
        // every sprint event stays on the progress spline.
        require(nfsu::race_event_driving_line(row.event) ==
                    nfsu::RaceDrivingLine::progress_spline,
                "sprint event received an unproven lane");
    }
    // Tournament rows are separate; the Eddie duel is a single sprint.
    require(nfsu::supported_sprint_row(66) == nullptr &&
            nfsu::supported_sprint_row(102) == nullptr &&
            nfsu::supported_sprint_row(112) != nullptr,
            "sprint single-race/tournament classification changed");
    require(nfsu::race_event_driving_line(66) == nfsu::RaceDrivingLine::progress_spline,
            "sprint tournament received a lane");
    std::puts("PASS: sprint selection, manifest and lane guards");
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
        require(argc <= 2 || measure, "invalid sprint tracks test mode");
        const auto root = argument_path(argv[1]);
        for (const SprintTrackCase& sprint : kCases) {
            std::printf("== event %u track %u laps %d direction %d\n",
                        sprint.event, sprint.track, sprint.laps, sprint.direction);
            std::string error;
            SupportedRace selection;
            require(load_supported_race(root/"Frontend/FRONTB.LZC",
                                        root/"Global/GLOBALB.BUN", sprint.event,
                                        selection, error), error);
            require(selection.slot.track == static_cast<std::int16_t>(sprint.track) &&
                    selection.slot.laps == sprint.laps &&
                    selection.slot.direction == sprint.direction &&
                    selection.track.region == sprint.region &&
                    selection.track.lap_skip == 1,
                    "retail sprint selection changed");
            RaceGpuScene scene;
            scene.set_wet_road_reflections(true);
            require(scene.load(posix_file_system(), root, error, nullptr, nullptr,
                               false, true, true, nullptr, nullptr, sprint.event), error);
            const auto& track = scene.track();
            const auto world_cost=explicit_world_model_cost(track);
            std::cout << "track " << track.track_number << " sections " << track.sections
                      << " textures " << track.texture_bytes << " streaming "
                      << track.streaming_bytes << " spillover "
                      << track.texture_spillover_sections << " unresolved "
                      << track.scene.region.unresolved_hashes.size() << '\n';
            require(track.event_number == sprint.event &&
                    track.track_number == sprint.track &&
                    track.sections == sprint.sections &&
                    track.texture_spillover_sections == sprint.spillover_sections &&
                    track.texture_bytes == sprint.texture_bytes + explicit_animation_bytes(track.scene.region) + world_cost.textures &&
                    track.streaming_bytes == sprint.streaming_bytes + explicit_animation_bytes(track.scene.region) + world_cost.total,
                    "uploaded sprint selection differs from its pin");
            require(track.scene.region.unresolved_hashes.size() == sprint.unresolved.size(),
                    "unresolved placed material set differs");
            for (std::size_t i = 0; i < sprint.unresolved.size(); ++i)
                require(track.scene.region.unresolved_hashes[i] == sprint.unresolved[i],
                        "unresolved placed material hash differs");
            require(track.streaming_bytes + track.texture_bytes <=
                        kResidentRegionsPlaceholder * kStreamingRegionBudgetPlaceholder,
                    "sprint streaming payload exceeds the plan");
            RaceRun run;
            require(run.set_event(sprint.event, error) &&
                    run.prepare_metadata(root, error) &&
                    run.start(root, track, error), error);
            RaceGpuFrame frame;
            require(scene.frame(run.session(), frame, error), error);
            require(frame.commands.draw_count() != 0, "sprint GPU frame must draw");
            std::printf("PASS: sprint %u loaded and started, laps %d\n",
                        sprint.track, run.session().progress().max_lap);
        }
        std::puts("PASS: all eight sprint tracks load and start");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
