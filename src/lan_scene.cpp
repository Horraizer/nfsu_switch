#include "nfsu/lan_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/race_events.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/career_ride.h"

namespace nfsu {
bool LanScene::load(const std::filesystem::path& root,const LanRaceOptions& options,
                    const std::array<std::string,2>& models,unsigned local,std::string& error){
    if(!valid_lan_options(options)||local>1){error="invalid LAN scene settings";return false;}
    const auto& course=kLanCourses[options.course];
    return load_course(root,course.event,course.round,options,models,local,error);
}
bool LanScene::load_local(const std::filesystem::path& root,unsigned event,unsigned round,
                          const LanRaceOptions& options,const std::array<std::string,2>& models,std::string& error){
    return load_course(root,event,round,options,models,0,error);
}
bool LanScene::load_course(const std::filesystem::path& root,unsigned event,unsigned round,
                           const LanRaceOptions& options,const std::array<std::string,2>& models,
                           unsigned local,std::string& error){
    SupportedRaceSelection race;
    if(!supported_race_selection(event,round,race)){error="two-player course unavailable";return false;}
    const auto global=root/"Global/GLOBALB.BUN";
    CareerCatalog catalog;if(!catalog.load_free_drive(root,error))return false;
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
    if(!load_race_image(global,globals,issues)){error="LAN tuning table unavailable";return false;}
    std::array<CareerRideSetup,2> rides;
    std::array<CarAssets,2> cars;
    for(unsigned actor=0;actor<2;++actor){
        CareerProfile profile;
        if(!catalog.free_profile(models[actor],profile,error)||!load_car_assets(global,models[actor],cars[actor],error))return false;
        profile.ride.installed=options.rides[actor].installed;
        profile.ride.performance.fill(options.tier);
        if(!resolve_career_ride(globals,catalog,profile,rides[actor],error))return false;
    }
    const auto bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,std::string& error,void*){
        return bind_frontend_runtime_skin(frame,request,error)&&bind_frontend_sponsors(inputs,request.installed_parts,frame,error);
    };
    RaceCarDress dress{&rides[local].request,&rides[local].skin,bind,nullptr};
    scene_.set_particles_enabled(false);scene_.set_race_visuals(false);scene_.set_start_scenes_enabled(false);
    if(!scene_.set_original_draw_distance(true)||!scene_.set_texture_filter(TextureFilterMode::linear)){
        error="LAN scene already loaded";return false;
    }
    if(!scene_.load(posix_file_system(),root,error,nullptr,nullptr,false,false,true,&cars[local],&dress,event,true,round))return false;
    const auto remote=1-local;const auto& ride=rides[remote];
    StockFrameInputs inputs;StockFrame other;
    if(!read_car_frame_inputs(root,cars[remote],inputs,false,error)||
       !prepare_race_car_frame(inputs,root,ride.request,ride.skin,other,error,true)||
       !bind(other,inputs,ride.request,error,nullptr))return false;
    CarShadowAsset shadow;
    if(!prepare_car_shadow(models[remote],inputs,other.car_pack,shadow,error)||!prepare_car_shadow_bounds(inputs,other,shadow,error))return false;
    auto neon=std::make_unique<CarNeonAsset>();
    if(!prepare_car_neon(inputs,other,shadow,*neon,error))return false;
    auto base=other.scene.instances;
    if(!scene_.add_rival(std::move(other.scene),std::move(base),other.skin_binding.source,std::move(shadow),error,
        false,{},nullptr,std::move(neon)))return false;
    return load_race_hud_atlas(root,atlas_,error,true,options.tier!=0,false,race.slot.track);
}
bool LanScene::frame(const LanRaceView& view,RaceGpuFrame& frame,RaceHudFrame& hud,std::string& error){
    if(!scene_.frame(view.session(),frame,error))return false;
    build_race_hud_frame(view.session(),atlas_,1280,720,hud);
    if(view.session().timing().racers[0].finish_valid)
        append_race_over(atlas_,1.,1280,720,hud,view.session().timing().racers[0].position==1?"You Win":"Race Over");
    return true;
}
bool LanScene::split_frame(const RaceSession& world,const LanRaceView& view,unsigned player,RaceGpuFrame& frame,
                           RaceHudFrame& hud,std::string& error,bool metric){
    if(!scene_.frame_split_view(world,view.session().camera(),player,frame,error))return false;
    build_race_hud_frame(view.session(),atlas_,1280,360,hud,metric);
    if(!append_race_minimap(view.session(),atlas_,1280,360,minimaps_.at(player),hud)) {
        error="split-screen minimap geometry refused";return false;
    }
    const auto& local=view.session().timing().racers[0];
    if(local.finish_valid)append_race_over(atlas_,1.,1280,360,hud,local.position==1?"You Win":"Race Over");
    return true;
}
}
