#pragma once
#include "nfsu/crash_camera.h"
#include <filesystem>
#include <memory>
namespace nfsu {
class IntroAiPlanner;
struct VehicleTrackCourse;
// Decoded once. Impacts only copy bounded chains; no file I/O or heap allocation.
class RaceCrashRoad final : public CrashCameraRoadSource {
public:
    RaceCrashRoad();
    ~RaceCrashRoad();
    bool load(const std::filesystem::path&,unsigned track,int direction,
              const VehicleTrackCourse&,const VehicleHandling&,const IntroAiPlanner*,std::string&);
    bool prepare(const VehicleState&,float,AiRoadChain&,int&)const override;
    bool progress_at(const AiDriverPoint&,float&)const override;
private:
    class State;
    std::unique_ptr<State> state_;
};
}
