#include "nfsu/race_gpu_scene.h"
#include "nfsu/lan_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "nfsu/frontend_scene.h"
#include "track_view_mask_native.inc"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace nfsu;
namespace {
void need(bool value,const std::string& error) {if(!value)throw std::runtime_error(error);}
unsigned native_mask(unsigned view,unsigned type,unsigned direction,unsigned supplied) {
    for(const auto& r:kNativeTrackViewMasks)
        if(r.view==view&&r.type==type&&r.direction==direction&&r.supplied==supplied)return r.result&255U;
    throw std::runtime_error("native view context fixture missing");
}
using Draws=std::vector<std::pair<std::size_t,std::size_t>>;
Draws inspect(const RaceGpuFrame& frame,const PreparedTrackScene& track,unsigned mask,
              unsigned reflection_mask,unsigned cube_mask,std::set<unsigned>& retained) {
    Draws result;
    for(std::size_t d=0;d<frame.track_draws;++d) {
        const auto view=frame.commands.draw_view(d);
        if(view.instance==kNoHandle||view.instance>=track.instances.size())continue;
        const auto& inst=track.instances.at(view.instance);
        need(((inst.instance_flags^0x60U)&mask)==0,"extra main-view instance="+std::to_string(view.instance)+
            " flags="+std::to_string(inst.instance_flags)+" mask="+std::to_string(mask));
        if(!frame.commands.movable_track_instance(view.instance))result.emplace_back(view.instance,view.definition.group);
        retained.insert(inst.mesh_hash);
    }
    for(std::size_t d=0;d<frame.commands.reflection_draw_count();++d) {
        const auto& draw=frame.commands.reflection_draw(d);
        if(draw.instance==kNoHandle||draw.instance>=track.instances.size())continue;
        const auto& inst=track.instances[draw.instance];
        need(((inst.instance_flags^0x60U)&reflection_mask)==0,"extra wet-road reflection instance");
    }
    for(const auto& draw:frame.commands.frontend_cube_draws) {
        if(draw.name.rfind("track group ",0)!=0)continue;
        if(draw.instance==kNoHandle||draw.instance>=track.instances.size())continue;
        const auto& inst=track.instances[draw.instance];
        need(((inst.instance_flags^0x60U)&cube_mask)==0,"extra car-environment instance");
    }
    std::sort(result.begin(),result.end());
    return result;
}
QuickRaceMode mode_for(RaceType type) {
    switch(type) {
        case RaceType::drag:return QuickRaceMode::drag;
        case RaceType::drift:return QuickRaceMode::drift;
        case RaceType::sprint:case RaceType::time_trial:return QuickRaceMode::sprint;
        // Local multiplayer offers Circuit/Sprint/Drag/Drift. Cover the
        // knockout's route through its legal Circuit entry, without claiming
        // a local knockout mode exists. Time trial similarly uses Sprint.
        case RaceType::lap_knockout:return QuickRaceMode::circuit;
        default:return QuickRaceMode::circuit;
    }
}
void source_audit(const std::filesystem::path& root,unsigned event,unsigned round,const TrackSlice& track) {
    std::string error;SupportedRace race;
    need(load_supported_race(root/"FrontEnd/FrontB.lzc",root/"Global/GLOBALB.BUN",event,race,error,round),error);
    std::vector<unsigned char> image;
    const auto zones=load_track_zones(root/"Tracks"/track_bundle_file_name(race.slot.track),image);
    const auto table=parse_track_stream_table(image.data(),image.size());
    need(zones.valid&&table.valid,"raw source streaming table");
    std::array<std::ifstream,3> streams;
    streams[1].open(root/"Tracks"/track_stream_file_name(race.track),std::ios::binary);
    streams[2].open(root/"Tracks"/track_stream_pack_file_name(race.track),std::ios::binary);
    std::size_t records=0,reverse=0,identity=0;
    for(const auto& section:track.objects.sections) {
        const TrackStreamRecord* source=nullptr;
        // One streaming range may contain multiple scenery section numbers.
        // Resolve the enclosing file range and authenticate its actual header.
        for(const auto& r:table.records) {
            if(r.file_index<1||r.file_index>2||section.offset<r.data_offset||
               section.header_record_offset+16>std::uint64_t(r.data_offset)+r.size)continue;
            auto& input=streams[r.file_index];unsigned tag=0,number=0;
            input.clear();input.seekg(std::streamoff(section.offset));input.read(reinterpret_cast<char*>(&tag),4);
            input.seekg(std::streamoff(section.header_record_offset+12));input.read(reinterpret_cast<char*>(&number),4);
            if(input.good()&&tag==0x80034100U&&number==section.number){source=&r;break;}
        }
        need(source&&source->file_index>0&&source->file_index<3,"source section/file provenance number="+std::to_string(section.number)+
            " offset="+std::to_string(section.offset));
        for(const auto& instance:section.instances) {
            need(instance.offset>=source->data_offset&&instance.offset+48<=std::uint64_t(source->data_offset)+source->size,
                 "source instance must lie in its original file range");
            unsigned char bytes[48];auto& input=streams[source->file_index];
            input.clear();input.seekg(std::streamoff(instance.offset));input.read(reinterpret_cast<char*>(bytes),48);
            need(input.good(),"read original48-byte instance");
            const auto word=[&](unsigned at){return unsigned(bytes[at])|(unsigned(bytes[at+1])<<8);};
            need(instance.flags==word(14)&&instance.object_index==word(12),"original flags/object binding changed");
            need(std::memcmp(instance.position,bytes+16,12)==0,"original position changed");
            for(unsigned axis=0;axis<3;++axis) {
                short low,high;const auto lo=static_cast<unsigned short>(word(axis*2)),hi=static_cast<unsigned short>(word(6+axis*2));
                std::memcpy(&low,&lo,2);std::memcpy(&high,&hi,2);
                need(instance.bounds_min[axis]==low&&instance.bounds_max[axis]==high,"original instance bounds changed");
            }
            for(unsigned axis=0;axis<9;++axis) {
                auto bits=static_cast<unsigned short>(word(28+axis*2));
                if(race.slot.direction==1&&(instance.flags&0x400U)&&axis<3)bits=static_cast<unsigned short>(0U-bits);
                short expected;std::memcpy(&expected,&bits,2);
                need(instance.matrix[axis]==expected,"original4462A0 direction transform differs");
                if(instance.flags&0x200U)need(expected==(axis%4==0?8192:0),"identity/null-placement source matrix is not identity");
            }
            if(instance.flags&0x200U) {
                for(float position:instance.position)need(position==0,"identity/null-placement source translation is not zero");
                ++identity;
            }
            if(instance.flags&0x400U)++reverse;
            ++records;
        }
    }
    std::cout<<"SOURCE event="<<event<<" round="<<round<<" records="<<records<<" mirror="<<reverse<<" identity="<<identity<<'\n';
}
#include "route_scenery_reference.inc"
void exercise_intro(const std::filesystem::path& root) {
    std::string error;IntroGrid grid;need(load_intro_grid(root,grid,error),error);
    IntroGridActor* player=nullptr;std::vector<RaceRivalSetup> rivals;std::array<int,3> contexts{};
    for(auto& actor:grid.actors) {
        if(actor.config.role==Event0Role::opponent) {
            contexts[rivals.size()]=actor.config.selected_context;
            rivals.push_back({actor.placement_spec,actor.pose,actor.hull,actor.config.event_label,
                actor.assets.car_type_name,actor.config.ai_mode,actor.config.config_id});
        }else player=&actor;
    }
    need(player&&rivals.size()==3,"real UG0 four-car grid");
    TrackSlice track;need(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,0),error);
    auto run=std::make_unique<RaceRun>();need(run->set_event(0,error)&&run->set_car(player->assets.car_type_name,error),error);
    if(player->config.performance)need(run->set_performance_levels(*player->config.performance,error),error);
    need(run->set_intro_grid(player->pose,player->hull,rivals,contexts,error,player->config.final_grid),error);
    std::vector<RaceTrafficSetup> traffic;
    for(const auto& actor:grid.traffic)traffic.push_back({actor.placement_spec,actor.hull,actor.assets.car_type_name});
    need(run->set_intro_traffic(traffic,error),error);
    StartCrowd clock_source;unsigned start_id=0;
    RaceRun::RoadSceneLoader loader{posix_file_system(),[&](unsigned id,RaceStartClock& clock,std::string& problem) {
        start_id=id;return clock_source.load_camera(posix_file_system(),root,id,"L1RA",clock,problem);
    }};
    need(run->start(root,track,error,nullptr,{},&loader),error);
    need(start_id!=0&&run->session().rival_count()==3,"UG0 authored start and three opponents");
    auto scene=std::make_unique<RaceGpuScene>();scene->set_original_draw_distance(true);
    scene->set_race_visuals(true);scene->set_wet_road_reflections(true);
    need(scene->set_start_scene(start_id,"L1RA"),"UG0 selected start scene");
    RaceCarDress dress{&player->request,&player->skin,nullptr,nullptr};
    dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,std::string& problem,void*) {
        return bind_frontend_runtime_skin(frame,request,problem)&&bind_frontend_sponsors(inputs,request.installed_parts,frame,problem);
    };
    need(scene->load(posix_file_system(),root,error,nullptr,nullptr,false,true,true,&player->assets,&dress,0),error);
    source_audit(root,0,0,scene->track());
    const auto add=[&](IntroGridActor& actor,bool traffic) {
        need(actor.frame!=nullptr,"authored grid visual payload");
        need(scene->add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),actor.frame->skin_binding.source,
            std::move(actor.shadow),error,traffic,actor.glow_anchors,&actor.hull,std::move(actor.neon)),error);
    };
    for(auto& actor:grid.actors)if(&actor!=player)add(actor,false);
    for(auto& actor:grid.traffic)add(actor,true);
    RaceGpuFrame frame;need(scene->frame(run->session(),frame,error),error);
    std::set<unsigned> retained;
    inspect(frame,scene->track().scene,native_mask(1,0,0,0x2000),native_mask(4,0,0,0x800),native_mask(10,0,0,0x1000),retained);
    need(!retained.empty()&&frame.car_draws>0,"UG0 first real frame must contain scenery and grid cars");
    std::cout<<"PASS UG0 real four-car first frame, authored start camera loader, selected_models="<<retained.size()<<'\n';
}
void exercise(const std::filesystem::path& root,unsigned event,unsigned round,bool split) {
    SupportedRaceSelection choice;need(supported_race_selection(event,round,choice),"catalog selection");
    std::string error;
    auto scene=std::make_unique<RaceGpuScene>();
    scene->set_original_draw_distance(true);scene->set_start_scenes_enabled(false);
    scene->set_race_visuals(true);scene->set_wet_road_reflections(true);
    need(scene->load(posix_file_system(),root,error,nullptr,nullptr,false,false,true,nullptr,nullptr,event,false,round),error);
    source_audit(root,event,round,scene->track());
    auto run=std::make_unique<RaceRun>();
    need(run->set_event(event,error,round)&&run->start(root,scene->track(),error),error);
    std::set<unsigned> retained;
    const auto direction=unsigned(choice.slot.direction),type=unsigned(choice.type);
    const auto mask=native_mask(1,type,direction,0x2000);
    const auto reflection=native_mask(4,type,direction,0x800);
    const auto cube=native_mask(10,type,direction,0x1000);
    std::size_t rejected=0,legal=0,mirrored=0;
    for(const auto& inst:scene->track().scene.instances) {
        (((inst.instance_flags^0x60U)&mask)!=0?rejected:legal)++;
        if(inst.instance_flags&0x400U)++mirrored;
    }
    RaceRouteDriver driver;
    for(unsigned tick=0;tick<=900;++tick) {
        if(tick==0||tick==300||tick==900) {
            RaceGpuFrame frame;
            need(scene->frame(run->session(),frame,error),error);
            const auto actual=inspect(frame,scene->track().scene,mask,reflection,cube,retained);
            const auto wanted=reference_selection(root,scene->track(),run->session(),frame,mask);
            need(actual==wanted,"cached/reference static instance+group selection differs");
        }
        if(tick<900)need(run->simulate(driver.sample(run->session()),error)&&run->commit(error),error);
    }
    if(split) {
        // The real local race entry, rather than two calls to the solo frame.
        auto world=std::make_unique<LanRaceWorld>();auto local=std::make_unique<LanScene>();
        LanRaceOptions options;options.laps=choice.lap_skip?1:2;
        const auto mode=mode_for(choice.type);
        need(world->load_local(root,event,round,mode,options,error),error);
        need(local->load_local(root,event,round,mode,options,world->models(),error,{},SplitEffects{false,true}),error);
        const auto local_type=world->session().is_drift()?3U:0U;
        for(const auto layout:{SplitLayout::stacked,SplitLayout::side_by_side}) {
            // Reverse submission order catches a view sharing stale selection.
            for(unsigned player:{1U,0U,1U}) {
                RaceGpuFrame frame;RaceHudFrame hud;
                need(local->split_frame(world->session(),player,frame,hud,error,false,layout),error);
                need(frame.commands.static_track_instance_count()>=scene->track().scene.instances.size(),"split source placement prefix missing");
                const auto local_mask=native_mask(player+1,local_type,direction,0x2000);
                const auto actual=inspect(frame,scene->track().scene,local_mask,
                    native_mask(4,local_type,direction,0x800),native_mask(10,local_type,direction,0x1000),retained);
                const auto wanted=reference_selection(root,scene->track(),world->session(),frame,local_mask,true,player,layout);
                need(actual==wanted,"split cached/uncached static instance+group selection differs");
            }
        }
    }
    need(!retained.empty(),"empty render is not a valid scenery comparison");
    std::cout<<"PASS event="<<event<<" round="<<round<<" track="<<choice.slot.track
        <<" direction="<<int(choice.slot.direction)<<" type="<<type<<" legal="<<legal
        <<" rejected="<<rejected<<" reverse_transform_candidates="<<mirrored
        <<" selected_models="<<retained.size()<<" split="<<split
        <<" split_mode="<<unsigned(mode_for(choice.type))<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        need(argc>=2,"usage: nfsu_route_scenery_retail_tests ROOT [EVENT [ROUND]]");
        const auto root=argument_path(argv[1]);
        if(argc>2) {
            if(std::stoul(argv[2])==0)exercise_intro(root);
            else exercise(root,unsigned(std::stoul(argv[2])),argc>3?unsigned(std::stoul(argv[3])):0,true);
        }
        else {
            exercise_intro(root);
            std::map<std::tuple<int,int,unsigned>,std::pair<unsigned,unsigned>> catalogue;
            std::set<int> tracks;
            // Event0 is the introductory four-car grid. Event1 supplies the
            // identical track/direction/type through the ordinary race entry.
            for(unsigned event=1;event<=200;++event)for(unsigned round=0;round<8;++round) {
                SupportedRaceSelection c;if(!supported_race_selection(event,round,c))continue;
                catalogue.emplace(std::make_tuple(c.slot.track,c.slot.direction,unsigned(c.type)),std::make_pair(event,round));
                tracks.insert(c.slot.track);
            }
            need(!catalogue.empty(),"empty event catalogue");
            std::cout<<"CATALOGUE track_files="<<tracks.size()<<" track/direction/type="<<catalogue.size()<<" tracks=";
            for(auto track:tracks)std::cout<<track<<',';
            std::cout<<std::endl;
            for(const auto& entry:catalogue)exercise(root,entry.second.first,entry.second.second,true);
            std::cout<<"PASS complete supported catalogue: track_files="<<tracks.size()<<" track/direction/type="<<catalogue.size()<<'\n';
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
