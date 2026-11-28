#pragma once
#include "nfsu/lan_race.h"
#include "nfsu/intro_grid.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/race_hud.h"

namespace nfsu {
struct SplitEffects {
    bool particles=true;
    bool reflections=false;
};
struct LocalRaceTraffic {
    unsigned density=0;
    std::vector<IntroGridActor> visuals;
    std::vector<RaceTrafficSetup> physics;
    bool load(const std::filesystem::path&,unsigned event,unsigned round,unsigned density,std::string&);
};
// Same backend-neutral 3D scene and HUD as the ordinary race. The view is
// local; world authority stays in LanRaceWorld.
class LanScene {
public:
    bool load(const std::filesystem::path&,const LanRaceOptions&,const std::array<std::string,2>& models,
              unsigned local_actor,std::string&);
    bool frame(const LanRaceView&,RaceGpuFrame&,RaceHudFrame&,std::string&);
    bool load_local(const std::filesystem::path&,unsigned event,unsigned round,QuickRaceMode,const LanRaceOptions&,
                    const std::array<std::string,2>& models,std::string&,std::vector<IntroGridActor> traffic={},SplitEffects effects={});
    bool check_local_budget(const RaceSession&,std::uint64_t audio_bytes,std::string&)const;
    void advance_local_effects(const RaceSession& world) noexcept {scene_.advance_particles(world);}
    bool split_frame(const RaceSession&,unsigned player,RaceGpuFrame&,RaceHudFrame&,
                     std::string&,bool metric=false,SplitLayout layout=SplitLayout::stacked,
                     RaceFrameProfile* =nullptr,TimingService* =nullptr);
    // Call once per active presentation frame, before recording either view.
    // Re-recording for culling, resize or the second viewport must not tick it.
    void advance_presentation(double seconds) noexcept {for(auto& phase:finish_phases_)phase.advance(seconds);}
    bool load_finish(const std::filesystem::path&,const RaceSession&,unsigned variant,std::string&);
    bool finish_frame(const RaceSession&,double seconds,RaceGpuFrame&,std::string&);
    void clear_finish() noexcept {scene_.clear_finish_scene();}
    unsigned finish_scene_id()const noexcept {return scene_.finish_scene_id();}
    unsigned finish_car_slot(unsigned actor)const {return scene_.finish_car_slot(actor);}
    const RaceHudAtlas& atlas()const{return atlas_;}
    const RaceParticles& particles()const noexcept{return scene_.particles();}
    std::size_t draw_capacity()const{return scene_.draw_capacity();}
private:
    bool load_course(const std::filesystem::path&,unsigned event,unsigned round,const LanRaceOptions&,
                     const std::array<std::string,2>&,unsigned local,bool split,std::string&,QuickRaceMode=QuickRaceMode::free_run,
                     std::vector<IntroGridActor> traffic={},SplitEffects effects={});
    RaceGpuScene scene_;RaceHudAtlas atlas_;std::array<RaceMinimapState,2> minimaps_;
    std::array<RaceOverPhase,2> finish_phases_;
    std::string region_;
};
}
