#include "nfsu/race_gpu_scene.h"
#include "nfsu/race_events.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "nfsu/draw_execution.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool okay, const std::string& message) {
    if (!okay) throw std::runtime_error(message);
}
}
int main(int argc, char** argv) {
    using namespace nfsu;
    try {
        std::string error;
        RaceRun refused;
        require(!refused.set_event(85,error), "missing original event must be refused");
        if (argc==1) { std::cout << "PASS: supported circuit selection guards\n"; return 0; }
        require(check_arguments(argc,argv,1,error),error);
        require(argc <= 3 && (argc != 3 || std::string(argv[2]) == "--measure" ||
                std::string(argv[2]) == "--measure-recovered"), "invalid Atlantic test mode");
        const auto steering = argc == 3 && std::string(argv[2]) == "--measure-recovered"
            ? RaceSteering::recovered_predicate1 : RaceSteering::baseline;
        const auto root=argument_path(argv[1]);
        SupportedRace selection;
        require(load_supported_race(root/"Frontend/FRONTB.LZC",root/"Global/GLOBALB.BUN",8,selection,error),error);
        require(selection.slot.track==1005 && selection.slot.laps==3 && selection.slot.direction==0,
                "event8 retail definition changed");
        RaceGpuScene scene;
        scene.set_wet_road_reflections(true);
        require(scene.load(posix_file_system(),root,error,nullptr,nullptr,false,true,true,nullptr,nullptr,8),error);
        const auto& track=scene.track();
        std::cout << "track " << track.track_number << " sections " << track.sections
                  << " textures " << track.texture_bytes << " unresolved "
                  << track.scene.region.unresolved_hashes.size() << " streaming " << track.streaming_bytes << '\n';
        require(track.event_number==8 && track.track_number==1005 && track.sections==90,
                "uploaded event8 selection differs");
        require(track.scene.region.unresolved_hashes.empty(), "event8 has an unresolved placed material");
        bool original_fallback = false;
        for (const auto& draw : track.scene.region.draws) if (draw.texture_hash == 0x828DCF0BU) {
            require(draw.binding == BindingKind::resolved, "power tower must use retail DEFAULTALPHA");
            const auto& texture = track.scene.region.textures.at(draw.texture);
            require(texture.hash == kTrackDefaultAlphaHash && texture.alpha_blend_mode == 2 &&
                    texture.width == 32 && texture.height == 32,
                    "power tower fallback must retain the original GLOBALB payload and state");
            original_fallback = true;
        }
        require(original_fallback, "power tower fallback draw absent");
        for (auto hash : track.scene.region.unresolved_hashes)
            std::cout << "unresolved hash " << std::hex << hash << std::dec << '\n';
        for (const auto& draw : track.scene.region.draws) if (draw.binding==BindingKind::unresolved)
            std::cout << "unresolved use " << std::hex << draw.texture_hash << std::dec
                      << " mesh " << track.scene.region.meshes.at(draw.mesh).name
                      << " triangles " << draw.index_count/3 << " flags " << std::hex << draw.flags
                      << std::dec << " material " << draw.material_index << " slot " << draw.texture_slot << '\n';
        for (const auto& instance : track.scene.instances) {
            const auto& mesh=track.scene.region.meshes[instance.mesh];
            if (mesh.name.find("POWERTOWER")!=std::string::npos)
                std::cout << "power instance " << instance.section_number << ' ' << instance.instance_index
                          << " mesh " << mesh.name << " xyz " << instance.transform.translation[0] << ' '
                          << instance.transform.translation[1] << ' ' << instance.transform.translation[2] << '\n';
        }
        RaceRun wrong;
        require(!wrong.start(root,track,error),"event1 cannot use event8 uploaded resources");
        RaceRun run;
        require(run.set_event(8,error) && run.set_steering(steering,error) && run.prepare_metadata(root,error) && run.start(root,track,error),error);
        require(!run.set_event(1,error),"active race selection cannot change");
        RaceGpuFrame frame;
        require(scene.frame(run.session(),frame,error),error);
        require(frame.commands.draw_count()!=0,"event8 GPU frame must draw");
        require(!run.matches_configured_replay(),"event8 cannot match event1 replay");
        if (argc == 3) {
            std::cout << "steering " << race_steering_name(steering) << '\n';
            const UploadTable uploaded = frame.commands.resource_table();
            RaceRouteDriver driver(RaceDrivingLine::event8_forward);
            std::vector<RaceInputRun> recording;
            while (run.session().timing().outcome == RaceOutcome::running &&
                   run.session().timing().step < kRaceInputMaxTicks) {
                const auto pad = driver.sample(run.session());
                append_race_input(recording, pad);
                require(run.simulate(pad, error) && run.commit(error), error);
                require(run.session().telemetry().resets == 0, "event8 drive reset");
                if (run.session().timing().step % 6000 == 0) {
                    require(scene.frame(run.session(), frame, error), error);
                    require(frame.commands.draw_count() != 0, "later event8 GPU frame empty");
                    require(validate_draw_update(uploaded, frame.commands, kTrackSliceDrawLimit, error), error);
                    std::cout << "shared tick " << run.session().timing().step
                              << " lap " << run.session().timing().racers[0].highest_lap
                              << " draws " << frame.commands.draw_count() << '\n';
                }
            }
            require(run.session().timing().outcome == RaceOutcome::finished,
                    "shared event8 drive did not finish");
            std::vector<RaceInputRun> decoded;
            require(parse_race_input(format_race_input(recording), decoded, error), error);
            RaceRun replay;
            require(replay.set_event(8, error) && replay.set_steering(steering, error) && replay.start(root, track, error), error);
            for (const auto& input : decoded) for (std::uint32_t i = 0; i < input.ticks; ++i) {
                const auto pad = race_input_frame(input, replay.session().timing().step);
                require(replay.session().timing().outcome == RaceOutcome::running,
                        "shared recording has trailing input");
                require(replay.simulate(pad, error) && replay.commit(error), error);
            }
            require(replay.session().timing().outcome == RaceOutcome::finished &&
                    replay.sequence_hash() == run.sequence_hash() && replay.steps_hash() == run.steps_hash(),
                    "shared event8 recording/state fidelity differs");
            require(scene.frame(replay.session(), frame, error) && frame.commands.draw_count() != 0,
                    "shared event8 finish frame empty");
            require(validate_draw_update(uploaded, frame.commands, kTrackSliceDrawLimit, error), error);
            std::cout << "shared finish steering " << race_steering_name(steering)
                      << " steps " << run.session().timing().step
                      << " laps " << run.session().timing().racers[0].highest_lap
                      << " resets " << run.session().telemetry().resets
                      << " sequence " << run.sequence_hash() << " steps-hash " << run.steps_hash() << '\n';
            std::cout << "PASS: event8 full RaceRun three-lap drive, residency, later frames and input/state replay\n";
        }
        std::cout << "PASS: event8 retail selection, GPU inputs, collision/start and residency\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
