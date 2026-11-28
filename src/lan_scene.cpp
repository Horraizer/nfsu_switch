#include "nfsu/lan_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/race_events.h"
#include "nfsu/race_setup.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/career_ride.h"
#include "nfsu/memory_budget.h"

namespace nfsu {
bool LocalRaceTraffic::load(const std::filesystem::path& root,unsigned event,unsigned round,
                            unsigned selected_density,std::string& error) {
    SupportedRaceSelection selection;
    if(!supported_race_selection(event,round,selection)) {error="local traffic course unavailable";return false;}
    LocalRaceTraffic next;
    next.density=selected_density;
    if(!load_quick_race_traffic(root,selection,selected_density,next.visuals,error))return false;
    for(const auto& car:next.visuals)
        next.physics.push_back({car.placement_spec,car.hull,car.assets.car_type_name,unsigned(car.assets.car_type)});
    *this=std::move(next);
    return true;
}
bool LanScene::load(const std::filesystem::path& root,const LanRaceOptions& options,
                    const std::array<std::string,2>& models,unsigned local,std::string& error){
    if(!valid_lan_options(options)||local>1){error="invalid LAN scene settings";return false;}
    const auto& course=kLanCourses[options.course];
    return load_course(root,course.event,course.round,options,models,local,false,error);
}
bool LanScene::load_local(const std::filesystem::path& root,unsigned event,unsigned round,QuickRaceMode mode,
                          const LanRaceOptions& options,const std::array<std::string,2>& models,std::string& error,
                          std::vector<IntroGridActor> traffic,SplitEffects effects){
    return load_course(root,event,round,options,models,0,true,error,mode,std::move(traffic),effects);
}
bool LanScene::load_course(const std::filesystem::path& root,unsigned event,unsigned round,
                           const LanRaceOptions& options,const std::array<std::string,2>& models,
                           unsigned local,bool split,std::string& error,QuickRaceMode mode,
                           std::vector<IntroGridActor> traffic,SplitEffects effects){
    SupportedRaceSelection race;
    if(!supported_race_selection(event,round,race)){error="two-player course unavailable";return false;}
    finish_phases_={};region_=race.region;
    const auto global=root/"Global/GLOBALB.BUN";
    auto catalog=std::make_unique<CareerCatalog>();
    if(!catalog->load_free_drive(root,error))return false;
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
    if(!load_race_image(global,globals,issues)){error="LAN tuning table unavailable";return false;}
    auto storage=std::make_unique<std::array<CareerRideSetup,2>>();
    auto& rides=*storage;
    std::array<CarAssets,2> cars;
    for(unsigned actor=0;actor<2;++actor){
        CareerProfile profile;
        if(!catalog->free_profile(models[actor],profile,error)||!load_car_assets(global,models[actor],cars[actor],error))return false;
        profile.ride.installed=options.rides[actor].installed;
        profile.ride.performance.fill(options.tier);
        profile.ride.turbo_brand=options.rides[actor].turbo_brand;
        if(!resolve_career_ride(globals,*catalog,profile,rides[actor],error))return false;
    }
    const auto bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,std::string& error,void*){
        return bind_frontend_runtime_skin(frame,request,error)&&bind_frontend_sponsors(inputs,request.installed_parts,frame,error);
    };
    RaceCarDress dress{&rides[local].request,&rides[local].skin,bind,nullptr};
    scene_.set_particles_enabled(split&&effects.particles);
    scene_.set_wet_road_reflections(split&&effects.reflections);
    scene_.set_race_visuals(split);scene_.set_start_scenes_enabled(false);
    scene_.set_widescreen_camera(true);
    scene_.set_local_second_player(split);
    if(!scene_.set_original_draw_distance(true)||!scene_.set_texture_filter(TextureFilterMode::linear)){
        error="LAN scene already loaded";return false;
    }
    if(!scene_.load(posix_file_system(),root,error,nullptr,nullptr,false,split,true,&cars[local],&dress,event,true,round))return false;
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
    auto glows=split?collect_car_glow_anchors(other):CarGlowAnchors{};
    if(split&&!scene_.set_local_second_car(cars[remote],other,error))return false;
    if(!scene_.add_rival(std::move(other.scene),std::move(base),other.skin_binding.source,std::move(shadow),error,
        false,std::move(glows),nullptr,std::move(neon)))return false;
    for(auto& actor:traffic) {
        if(!scene_.add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),kNoHandle,
            std::move(actor.shadow),error,true,actor.glow_anchors,&actor.hull))return false;
        actor.frame.reset();
    }
    const bool drag=mode==QuickRaceMode::drag;
    const bool drift=mode==QuickRaceMode::drift;
    if(!load_race_hud_atlas(root,atlas_,error,!drag&&!drift,options.tier!=0,drift,race.slot.track))return false;
    if(!drag)return true;
    std::array<VehicleSpec,2> specs;
    const auto levels=lan_performance_levels(options.tier);
    for(unsigned i=0;i<2;++i)
        if(!load_race_vehicle_spec(global,models[i],levels,specs[i],error))return false;
    return load_race_drag_hud(root,models[0],specs[0].handling,atlas_,error,&models[1],&specs[1].handling);
}
bool LanScene::load_finish(const std::filesystem::path& root,const RaceSession& world,unsigned variant,std::string& error) {
    return scene_.load_finish_scene(posix_file_system(),root,world,region_,variant,error);
}
bool LanScene::finish_frame(const RaceSession& world,double seconds,RaceGpuFrame& frame,std::string& error) {
    scene_.set_finish_scene_seconds(seconds);return scene_.frame(world,frame,error);
}
bool LanScene::check_local_budget(const RaceSession& world,std::uint64_t audio_bytes,std::string& error)const {
    const RacePersistentPayload payload{scene_.car_bytes(),world.course().world.memory.resident_bytes,
        scene_.track().contacts.memory.resident_bytes,scene_.cache_bytes(),audio_bytes,atlas_.rgba.size()};
    return check_race_persistent_payload(payload,error);
}
bool LanScene::frame(const LanRaceView& view,RaceGpuFrame& frame,RaceHudFrame& hud,std::string& error){
    if(!scene_.frame(view.session(),frame,error))return false;
    const auto& local=view.session().timing().racers[0];
    const bool terminal=local.finish_valid||local.status!=RacerStatus::not_finished;
    const auto seconds=finish_phases_[0].observe(terminal);
    if(terminal)build_race_over_frame(atlas_,seconds,1280,720,hud,race_over_message(local,view.session().is_drag(),atlas_));
    else build_race_hud_frame(view.session(),atlas_,1280,720,hud);
    return true;
}
bool LanScene::split_frame(const RaceSession& world,unsigned player,RaceGpuFrame& frame,
                           RaceHudFrame& hud,std::string& error,bool metric,SplitLayout layout,RaceFrameProfile* profile,
                           TimingService* timer){
    if(!scene_.frame_split_view(world,player,frame,error,layout,profile,timer))return false;
    const auto view=split_viewport(frame.commands.width,frame.commands.height,layout,player);
    const auto& local=world.timing().racers[player];
    const bool terminal=local.finish_valid||local.status!=RacerStatus::not_finished;
    const auto seconds=finish_phases_.at(player).observe(terminal);
    if(terminal) {
        const auto finish_layout=layout==SplitLayout::stacked?(player?RaceOverLayout::split_bottom:RaceOverLayout::split_top):RaceOverLayout::split_side;
        build_race_over_frame(atlas_,seconds,view.width,view.height,hud,race_over_message(local,world.is_drag(),atlas_),finish_layout);
    } else {
        build_race_hud_frame(world,atlas_,view.width,view.height,hud,metric,player);
        if(atlas_.map.valid&&!append_race_minimap(world,atlas_,view.width,view.height,minimaps_.at(player),hud,player)) {
            error="split-screen minimap geometry refused";return false;
        }
    }
    return true;
}
}
