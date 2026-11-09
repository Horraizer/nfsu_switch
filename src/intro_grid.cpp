#include "nfsu/intro_grid.h"
#include "nfsu/car_collision_hull.h"
#include "nfsu/frontend_preset.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/ai_action.h"
#include "nfsu/quick_rival.h"
#include "nfsu/ai_powertrain.h"
#include "nfsu/traffic_model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
namespace nfsu {
namespace {
bool load_traffic(const std::filesystem::path& root,const std::vector<unsigned char>& globals,
                  unsigned track,unsigned count,std::vector<IntroGridActor>& out,std::string& error) {
    if(!count)return true;
    const auto types=parse_car_type_table(globals.data(),globals.size());
    if(!types.ok()){error="traffic car table unavailable";return false;}
    // The current frontend supplies an explicit port seed. This restores the
    // original weighted draw/retry/skin boundary, not all earlier global RNG
    // consumers or the original traffic actor construction scheduler.
    std::array<TrafficModelRecord,35> models{};
    if(types.records.size()!=models.size()){error="traffic model table extent";return false;}
    for(const auto& type:types.records) {
        if(type.index>=models.size()||type.offset>globals.size()||
           0xc90>globals.size()-type.offset||
           !decode_traffic_model_record(globals.data()+type.offset,0xc90,models[type.index])||
           models[type.index].id!=int(type.index)) {error="traffic model record invalid";return false;}
    }
    if(count>kRaceTrafficCapacity){error="authored traffic pool exceeds supported capacity";return false;}
    std::array<int,kRaceTrafficCapacity> used{};
    std::uint32_t random=0x60000000U;out.reserve(count);
    for(unsigned i=0;i<count;++i) {
        const auto choice=choose_traffic_model(models.data(),models.size(),track,used.data(),i,random);
        if(choice.id<0||choice.id>=int(types.records.size())){error="intro traffic pool unavailable";return false;}
        used[i]=choice.id;
        // 4B3B7D consumes the skin RNG even though57D160 does not store its
        // first stack argument in this PC executable. Do not invent a skin alias.
        const auto& name=types.records[unsigned(choice.id)].name;
        IntroGridActor actor;actor.request=stock_car_request(name);
        if(!resolve_traffic_assets(types,name,actor.assets,error))return false;
        StockFrameInputs inputs;
        if(!read_car_frame_inputs(root,actor.assets,inputs,false,error))return false;
        if(!decode_car_collision_hull(inputs.file(StockFrameFile::car_geometry).bytes,bin_hash(name+"_CV"),actor.hull)) {
            error="traffic collision solid unavailable: "+name;return false;
        }
        actor.frame=std::make_unique<StockFrame>();auto& frame=*actor.frame;
        if(!prepare_stock_frame(inputs,actor.request,frame,error)||

           !load_race_vehicle_spec(root/"Global/GLOBALB.BUN",name,{},actor.placement_spec,error))return false;
        // 56A130 uses the loaded texture registry. Most traffic SHADOWIG
        // records are in Cars/TEXTURES.BIN, even though the body pack is local.
        const auto shadow_hash=bin_hash(name+"_SHADOWIG");
        const auto has_shadow=[shadow_hash](const TexturePack& pack) {
            return std::any_of(pack.textures.begin(),pack.textures.end(),
                [shadow_hash](const auto& texture){return texture.hash==shadow_hash;});
        };
        const TexturePack* shadow_pack=&frame.car_pack;
        auto shadow_file=StockFrameFile::car_textures;
        if(!has_shadow(*shadow_pack)) {
            const auto& shared=frame.shared_sets.at(1).packs;
            const auto found=std::find_if(shared.begin(),shared.end(),has_shadow);
            shadow_pack=found==shared.end()?nullptr:&*found;
            shadow_file=StockFrameFile::shared_car_textures;
        }
        // 56F22F skips the texture pass when the lookup returned null.
        if(shadow_pack &&
           (!prepare_car_shadow(name,inputs,*shadow_pack,actor.shadow,error,shadow_file)||
            !prepare_car_shadow_bounds(inputs,frame,actor.shadow,error)))return false;
        actor.glow_anchors=collect_car_glow_anchors(frame);
        actor.base_instances=frame.scene.instances;out.push_back(std::move(actor));
    }
    return true;
}

// 4B3C60 permutes the retail stock roster and finds an opponent within
// the player's performance ceiling. 4B4590 samples the cumulative context
// table in GLOBALB (0x34B00 + 0x788).
bool configure_quick_grid(Event0SetupDescriptor& grid,const CarTypeTable& types,
    const std::vector<unsigned char>& globals,const unsigned char* ai,const QuickRaceOptions& options,
    const VehicleSpec& player,const VehicleUpgradeLevels& player_levels,const CarPartDatabase& db,
    std::array<InstalledPartSelection,4>& dresses,std::string& error) {
    constexpr std::array<unsigned,20> cars{{1,3,6,9,7,10,11,12,13,14,4,15,16,8,5,0,17,2,18,19}};
    if(!ai||types.records.size()<cars.size()||options.opponents>3||options.difficulty>2) {
        error="quick race rival inputs invalid";return false;
    }
    float player_rating=0,slip=0;
    if(!ai_setup_performance(player.handling,player_rating,slip)){error="quick race player rating refused";return false;}
    std::array<float,20> rating{};
    for(const auto id:cars) {
        const auto offset=types.records[id].offset;
        VehicleSpec stock;
        if(offset>globals.size()||kVehicleRecordSize>globals.size()-offset||
           !make_race_vehicle_spec(globals.data()+offset,kVehicleRecordSize,{},stock,error)||
           !ai_setup_performance(stock.handling,rating[id],slip)) {
            if(error.empty())error="quick race stock rival rating refused";
            return false;
        }
    }
    std::uint32_t rng=options.seed;
    const auto random=[&](unsigned bound) {
        return quick_rival_random(rng,bound);
    };
    grid.racer_count=options.opponents+1;
    bool player_model_used=false; //4B4040 permits at most one player-model rival.
    for(unsigned i=1;i<grid.racer_count;++i) {
        std::array<int,20> order;order.fill(-1);
        for(const auto car:cars) {
            auto at=random(order.size());
            while(order[at]>=0)at=(at+(random(100)>50?1U:19U))%order.size();
            order[at]=static_cast<int>(car);
        }
        unsigned selected=0;
        bool found=false;
        for(float allowance=15.F;!found;allowance*=2)for(const auto car:order) {
            if((!player_model_used||types.records[car].name!=grid.actors[0].visual.car)&&
               rating[car]<=player_rating+allowance) {selected=unsigned(car);found=true;break;}
        }
        player_model_used|=types.records[selected].name==grid.actors[0].visual.car;
        auto& actor=grid.actors[i];actor={};
        actor.config_id=static_cast<std::int8_t>(99+i);actor.role=Event0Role::opponent;
        actor.visual.car=types.records[selected].name;actor.event_label=actor.visual.car;
        actor.selected_context=static_cast<std::int16_t>(quick_rival_context(ai,options.difficulty,rng));
    }
    //4B4380: performance matching and appearance run after driver selection,
    //in actor configuration order, before the human grid-slot swap. The port
    //seed remains explicit, not the original global RNG's pre-menu history.
    for(unsigned i=1;i<grid.racer_count;++i) {
        auto& actor=grid.actors[i];
        const auto id=find_car_type(types,actor.visual.car);
        if(id==kNoCarType){error="quick rival selected model missing";return false;}
        const auto* record=globals.data()+types.records[id].offset;
        VehicleUpgradeLevels levels;VehicleSpec spec;
        if(!adaptive_rival_setup(record,kVehicleRecordSize,player,player_levels,levels,spec,error)||
           !quick_rival_visual(db,actor.visual.car,record,kVehicleRecordSize,levels,rng,dresses[i],error))return false;
        actor.performance=levels;
    }
    // 4B4610 swaps the human with a random initial grid slot.
    grid.desired_player_grid=static_cast<std::uint8_t>(random(grid.racer_count)+1);
    for(unsigned i=0;i<grid.racer_count;++i) {
        auto& actor=grid.actors[i];
        actor.initial_grid=static_cast<std::uint8_t>(i?i:grid.racer_count);
        actor.final_grid=static_cast<std::uint8_t>(i?(i==grid.desired_player_grid?grid.racer_count:i):grid.desired_player_grid);
        grid.grid_config_ids[actor.final_grid-1]=actor.config_id;
    }
    const auto count=race_traffic_pool_size(grid.track,options.traffic,unsigned(grid.race.direction));
    if(count<0||unsigned(count)>kRaceTrafficCapacity){error="quick race traffic pool invalid";return false;}
    grid.traffic.selector=options.traffic;
    grid.traffic.requested_configs=static_cast<std::uint8_t>(count);
    return true;
}
}
bool load_quick_race_traffic(const std::filesystem::path& root,const SupportedRaceSelection& race,
    unsigned density,std::vector<IntroGridActor>& out,std::string& error) {
    if(density>3){error="invalid traffic density";return false;}
    std::vector<IntroGridActor> next;
    if(density&&race.type!=RaceType::drift) {
        std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
        if(!load_race_image(root/"Global/GLOBALB.BUN",globals,issues)) {
            error="traffic catalog unavailable";return false;
        }
        const auto table=parse_track_info_table(globals.data(),globals.size());
        const auto* track=find_track_info(table,race.slot.track);
        const auto count=track?race_traffic_pool_size(*track,density,unsigned(race.slot.direction)):-1;
        if(!table.ok()||count<0){error="traffic track selection invalid";return false;}
        if(!load_traffic(root,globals,race.slot.track,unsigned(count),next,error))return false;
    }
    out=std::move(next);error.clear();return true;
}
bool load_race_grid(const std::filesystem::path& root,unsigned event,const std::string& car,
                     unsigned difficulty,IntroGrid& out,std::string& error,const VehicleUpgradeLevels* owned_levels,
                     const RaceCarDress* owned_dress,unsigned race_index,GridPlayerVisuals player_visuals,
                     const QuickRaceOptions* quick) {
    IntroGrid next;
    SupportedRace selection;
    if(!load_supported_race(root/"FrontEnd/FrontB.lzc",root/"Global/GLOBALB.BUN",event,selection,error,race_index))return false;
    const auto track=root/"Tracks"/track_bundle_file_name(selection.slot.track);
    const auto setup=load_race_grid_setup(root/"FrontEnd/FrontB.lzc",root/"Global/GLOBALB.BUN",
                                     track,event,car,difficulty,race_index);
    if(!setup.ok()) {error=setup.issues.empty()?"intro setup unavailable":setup.issues.front().message;return false;}
    next.setup=setup.descriptor;
    if(!load_vehicle_track_course(track,selection.slot.laps,selection.slot.direction,selection.track.lap_skip,0,next.course,error))return false;
    std::vector<unsigned char> front,globals;
    std::vector<RaceTableIssue> issues;
    RaceTableLimits limits;limits.max_file_size=32*1024*1024;limits.max_image_size=64*1024*1024;
    if(!load_race_image(root/"FrontEnd/FrontB.lzc",front,issues,limits) ||
       !load_race_image(root/"Global/GLOBALB.BUN",globals,issues,limits)) {
        error="intro preset images unreadable";return false;
    }
    const auto db=parse_car_part_database(globals.data(),globals.size());
    const unsigned char* ai_configuration=nullptr;std::size_t ai_configuration_size=0;
    if(quick||selection.type==unsigned(RaceType::drag)) {
        const auto word=[](const unsigned char* p) {
            return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);
        };
        for(std::size_t p=0;p+8<=globals.size();) {
            const auto tag=word(globals.data()+p),size=word(globals.data()+p+4);p+=8;
            if(size>globals.size()-p){error="drag AI configuration extent";return false;}
            if(tag==0x34b00) {
                if(ai_configuration){error="duplicate drag AI configuration";return false;}
                ai_configuration=globals.data()+p;ai_configuration_size=size;
            }
            p+=size;
        }
        if(!ai_configuration||ai_configuration_size!=0x82C){error="race AI configuration absent or invalid";return false;}
    }
    const auto reference=std::find_if(next.setup->actors.begin(),next.setup->actors.end(),
        [](const auto& actor){return actor.role==Event0Role::human;});
    if(reference==next.setup->actors.end()||!reference->performance){error="adaptive human setup absent";return false;}
    const auto reference_levels=event!=0&&owned_levels?*owned_levels:*reference->performance;
    VehicleSpec reference_spec;
    if(!load_race_vehicle_spec(root/"Global/GLOBALB.BUN",reference->visual.car,reference_levels,reference_spec,error))return false;
    std::array<InstalledPartSelection,4> quick_dresses{};
    if(quick) {
        auto grid=std::make_shared<Event0SetupDescriptor>(*next.setup);
        const auto types=parse_car_type_table(globals.data(),globals.size());
        if(!types.ok()){error="quick race car table invalid";return false;}
        if(!configure_quick_grid(*grid,types,globals,ai_configuration,*quick,reference_spec,reference_levels,db,quick_dresses,error))return false;
        next.setup=std::move(grid);
    }
    next.actors.resize(next.setup->racer_count);
    for(std::size_t slot=0;slot<next.actors.size();++slot) {
        auto& actor=next.actors[slot];
        const auto id=next.setup->grid_config_ids[slot];
        const auto found=std::find_if(next.setup->actors.begin(),next.setup->actors.end(),
            [id](const auto& config){return config.config_id==id;});
        if(found==next.setup->actors.end()) {error="intro grid actor absent";return false;}
        actor.config=*found;const auto& preset=actor.config.visual;
        actor.request=stock_car_request(preset.car);
        const bool random_dress=quick&&actor.config.role==Event0Role::opponent;
        if(random_dress) {
            const auto index=static_cast<std::size_t>(found-next.setup->actors.begin());
            actor.request.installed_parts=quick_dresses[index];
        }
        if(!preset.name.empty() && !resolve_frontend_preset_parts(front,static_cast<std::size_t>(preset.offset),preset.car,
            preset.name,db,actor.request.installed_parts,error))return false;
        actor.skin=stock_car_skin(preset.car);
        if(((!preset.name.empty()||random_dress) && !resolve_frontend_preset_skin(globals,preset.car,db,actor.request.installed_parts,
            actor.skin.selection,error)) ||
           !load_car_assets(root/"Global/GLOBALB.BUN",preset.car,actor.assets,error))return false;
        const bool owned=event!=0&&actor.config.role==Event0Role::human&&owned_dress;
        if(owned) {
            if(!owned_dress->request||!owned_dress->skin||owned_dress->request->car_type_name!=preset.car) {
                error="owned grid dress incompatible with human car";return false;
            }
            actor.request=*owned_dress->request;actor.skin=*owned_dress->skin;
        }
        const auto hull_hash=bin_hash(preset.car+"_CV");
        if(actor.config.role==Event0Role::human && player_visuals==GridPlayerVisuals::load_with_scene) {
            // The scene will prepare this dress once. The grid only needs the
            // authored collision hull for the initial pose and race physics.
            std::vector<unsigned char> geometry;
            std::vector<RaceTableIssue> geometry_issues;
            if(!load_race_image(root/actor.assets.geometry_path,geometry,geometry_issues) ||
               !decode_car_collision_hull(geometry,hull_hash,actor.hull)) {
                error="intro selected collision solid unavailable: "+preset.car;return false;
            }
        } else {
            StockFrameInputs inputs;
            if(!read_car_frame_inputs(root,actor.assets,inputs,true,error)) {
                error=car_frame_inputs_error(inputs,error,root);return false;
            }
            if(!read_exhaust_family_inputs(root,inputs)) {error="intro shared exhaust unreadable";return false;}
            if(!decode_car_collision_hull(inputs.file(StockFrameFile::car_geometry).bytes,hull_hash,actor.hull)) {
                error="intro selected collision solid unavailable: "+preset.car;return false;
            }
            actor.frame=std::make_unique<StockFrame>();
            auto& frame=*actor.frame;
            if(!prepare_stock_frame(inputs,actor.request,actor.skin,frame,error,true) ||
               (owned&&owned_dress->bind&&!owned_dress->bind(frame,inputs,actor.request,error,owned_dress->user)) ||
               (!owned&&(!preset.name.empty()||random_dress) && !bind_frontend_runtime_skin(frame,actor.request,error)) ||
               (!owned&&(!preset.name.empty()||random_dress) && !bind_frontend_sponsors(inputs,actor.request.installed_parts,frame,error)) ||
               !prepare_car_shadow(preset.car,inputs,frame.car_pack,actor.shadow,error) ||
               !prepare_car_shadow_bounds(inputs,frame,actor.shadow,error))return false;
            if(random_dress) {
                actor.neon=std::make_unique<CarNeonAsset>();
                if(!prepare_car_neon(inputs,frame,actor.shadow,*actor.neon,error))return false;
                if(!actor.neon->enabled)actor.neon.reset();
            }
        }
        auto levels=actor.config.role==Event0Role::human?reference_levels:actor.config.performance.value_or(VehicleUpgradeLevels{});
        if(actor.config.role==Event0Role::opponent) {
            if(actor.assets.record_offset>globals.size()||kVehicleRecordSize>globals.size()-actor.assets.record_offset) {
                error="adaptive selected record outside GLOBALB";return false;
            }
            const auto* record=globals.data()+actor.assets.record_offset;
            if(quick) {
                if(!make_race_vehicle_spec(record,kVehicleRecordSize,levels,actor.placement_spec,error))return false;
            }else if(!adaptive_rival_setup(record,kVehicleRecordSize,reference_spec,reference_levels,
                levels,actor.placement_spec,error,actor.config.tuning_selector))return false;
            actor.config.performance=levels;
            if(selection.type==unsigned(RaceType::drag)) {
                //476DC4 skips the quick-race difficulty override in Underground
                //mode1. Its gearbox retains constructor clutch scaleDC=1.
                constexpr float quick_clutch[]={.4F,.8F,1.F};
                actor.placement_spec.drag_clutch_scale=quick?quick_clutch[difficulty]:1.F;
                //476EC0 maps context0 back to row0; only negative or >9
                //contexts retain the constructor's default bounds.
                if(actor.config.selected_context>=0 && actor.config.selected_context<=9) {
                    AiSpeedConfiguration configuration;
                    if(!ai_speed_configuration(ai_configuration,ai_configuration_size,
                        actor.config.selected_context,configuration)) {error="drag shift context invalid";return false;}
                    actor.placement_spec.drag_shift_offsets=configuration.drag_shift_offsets;
                }
            }
        } else {
            actor.config.performance=levels;
            if(!load_race_vehicle_spec(root/"Global/GLOBALB.BUN",preset.car,levels,actor.placement_spec,error))return false;
        }
        int marker=-1;
        if(!vehicle_track_grid_pose(next.course,static_cast<int>(slot),actor.pose,marker,error))return false;
        const auto& actual_marker=next.course.track.markers.at(static_cast<std::size_t>(marker));
        if(!quick&&(actual_marker.param!=actor.config.marker.index || actual_marker.angle!=actor.config.marker.raw_angle ||
           !std::equal(actor.config.marker.raw_position.begin(),actor.config.marker.raw_position.end(),actual_marker.position))) {
            error="intro grid marker mismatch";return false;
        }
        VehicleModel model(actor.placement_spec);
        if(!model.reset(actor.state,actor.pose)) {error="intro placement refused: "+model.error();return false;}
        if(actor.frame) {
            auto& frame=*actor.frame;
            actor.glow_anchors=collect_car_glow_anchors(frame);
            actor.base_instances=frame.scene.instances;
            if(!drive_place_instances(actor.base_instances,actor.state,actor.placement_spec,frame.scene.instances)) {
                error="intro body placement refused";return false;
            }
        }
    }
    if(!load_traffic(root,globals,next.setup->race.track,next.setup->traffic.requested_configs,
        next.traffic,error))return false;
    out=std::move(next);error.clear();return true;
}
bool load_intro_grid(const std::filesystem::path& root,IntroGrid& out,std::string& error) {
    return load_race_grid(root,0,"",1,out,error);
}
bool prepare_intro_race(IntroGrid& grid,RaceSession& session,std::string& error) {
    std::vector<RaceRivalSetup> setup;
    unsigned player_grid=1;
    for(auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent) {
        // Traffic keeps its selected stock setup; adaptive tuning applies to racers.
        auto spec=actor.placement_spec;
        spec.nitrous={};
        setup.push_back({spec,actor.pose,actor.hull,actor.config.event_label,actor.config.visual.car,actor.config.ai_mode});
    } else if(actor.config.role==Event0Role::human)player_grid=actor.config.final_grid;
    return session.set_rivals(setup,error,RacePresentation::none,player_grid);
}
}
