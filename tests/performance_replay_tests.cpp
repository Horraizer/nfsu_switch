#include "nfsu/race_gpu_scene.h"
#include "nfsu/handling_telemetry.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace nfsu;
namespace {
void require(bool okay,const std::string& message) {
    if (!okay) throw std::runtime_error("FAIL: "+message);
}
std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); require(bool(in),"recording read");
    return std::string((std::istreambuf_iterator<char>(in)),{});
}
}
int main(int argc,char** argv) {
    try {
        std::string error;
        require(argc==4 && check_arguments(argc,argv,1,error),"root, stock recording, upgraded recording required");
        const auto root=argument_path(argv[1]);
        const auto stock=read_text(argument_path(argv[2])),upgraded=read_text(argument_path(argv[3]));
        RaceReplay refused;
        require(!refused.load(stock,error,true) && !refused.load(upgraded,error),"cross-setup replay refused");
        RaceGpuScene scene;
        require(scene.load(posix_file_system(),root,error),error);
        std::FILE* stream=std::tmpfile(); require(stream!=nullptr,"temporary telemetry stream");
        struct Close { std::FILE* f; ~Close() { std::fclose(f); } } close{stream};
        for (int pass=0;pass<3;++pass) {
            RaceRun race;
            const auto levels=pass==0 ? VehicleUpgradeLevels{} : performance1_levels();
            require(race.set_performance_levels(levels,error),error);
            require(race.prepare_metadata(root,error),error);
            require(!race.set_performance_levels(levels,error),"late setup refused");
            require(race.start(root,scene.track(),error),error);
            require(compare_vehicle_handling(race.session().spec().handling,
                    [&]() {
                        VehicleSpec spec; require(load_race_vehicle_spec(root/"Global/GLOBALB.BUN","240SX",levels,spec,error),error);
                        return spec.handling;
                    }())==kVehicleRecordSize,"RaceRun uses selected spec");
            RaceReplay replay; require(replay.load(pass==0 ? stock : upgraded,error,pass!=0),error);
            HandlingTelemetryLog log(pass==2 ? stream : nullptr);
            while (race.session().timing().outcome==RaceOutcome::running && race.session().timing().step<kRaceInputMaxTicks) {
                GamepadFrame pad; require(replay.next(race.session().timing().step,pad,error),error);
                require(race.simulate(pad,error),error);
                require(log.write(race.session(),pad),"telemetry write");
                require(race.commit(error),error);
            }
            require(replay.exhausted() && race.matches_configured_replay(),"configured full retail replay pins");
            require(pass==0 ? race.matches_event1_replay() : !race.matches_event1_replay(),"legacy stock pin stays separate");
            require(pass!=2 || log.rows()==HandlingTelemetryLog::kMaxRows,"bounded replay telemetry");
            for (const auto& line : race.result_lines()) std::cout << line << '\n';
        }
        std::rewind(stream); char line[4096];
        require(std::fgets(line,sizeof line,stream) && std::fgets(line,sizeof line,stream),"replay telemetry rows");
        require(std::string(line).find("unavailable")!=std::string::npos,"replay wall-clock age unavailable");
        std::cout << "PASS: zero levels preserve stock pins; upgraded replay twice, logging on/off, matches separate pins\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
