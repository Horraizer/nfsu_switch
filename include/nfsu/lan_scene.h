#pragma once
#include "nfsu/lan_race.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/race_hud.h"

namespace nfsu {
// Same backend-neutral 3D scene and HUD as the ordinary race. The view is
// local; world authority stays in LanRaceWorld.
class LanScene {
public:
    bool load(const std::filesystem::path&,const LanRaceOptions&,const std::array<std::string,2>& models,
              unsigned local_actor,std::string&);
    bool frame(const LanRaceView&,RaceGpuFrame&,RaceHudFrame&,std::string&);
    const RaceHudAtlas& atlas()const{return atlas_;}
    std::size_t draw_capacity()const{return scene_.draw_capacity();}
private:
    RaceGpuScene scene_;RaceHudAtlas atlas_;
};
}
