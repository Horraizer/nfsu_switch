#include "nfsu/intro_grid.h"
#include "nfsu/race_run.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "nfsu/ai_traffic_action.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace nfsu;
namespace {
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
}
int main(int argc,char** argv) {try {
    require(argc>=3&&argc<=5,"expected retail root, case and optional traffic mode and round");
    const auto root=argument_path(argv[1]);
    const unsigned selection=unsigned(std::stoul(argv[2]));
    const std::string option=argc>=4?argv[3]:"";
    const bool fallback_start=option=="fallback-start";
    const bool ordinary_start=option=="ordinary-start";
    const bool start_only=fallback_start||ordinary_start;
    const bool traffic_enabled=start_only||option=="traffic";
    require(argc==3||traffic_enabled,"unknown traffic option");
    require(ordinary_start||(fallback_start?(selection==70||selection==80||selection==98):selection<3),
            "invalid traffic case");
    const unsigned event=start_only?selection:9;
    const unsigned round=start_only?(argc==5?unsigned(std::stoul(argv[4])):0):selection;
    require(argc<5||ordinary_start||(fallback_start&&event==80&&(round==1||round==3)),
            "invalid startup round");
    IntroGrid grid;std::string error;
    require(load_race_grid(root,event,"CIVIC",2,grid,error,nullptr,nullptr,round),error);
    const IntroGridActor* human=nullptr;std::vector<RaceRivalSetup> rivals;std::array<int,3> contexts{};
    for(const auto& actor:grid.actors) {
        if(actor.config.role==Event0Role::opponent) {
            require(rivals.size()<3,"unexpected rival count");
            contexts[rivals.size()]=actor.config.selected_context;
            rivals.push_back({actor.placement_spec,actor.pose,actor.hull,actor.config.event_label,
                actor.assets.car_type_name,actor.config.ai_mode,actor.config.config_id});
        }else human=&actor;
    }
    require(human&&(start_only?rivals.size()+1==grid.setup->racer_count:rivals.size()==3),
            "missing authored start grid");
    RaceRun run;require(run.set_event(event,error,round)&&run.set_car("CIVIC",error)&&run.set_difficulty(2,error),error);
    require(run.set_intro_grid(human->pose,human->hull,rivals,contexts,error,human->config.final_grid),error);
    // Human is deliberately idle. Empty-pool cases isolate closures;
    // pool cases also exercise real spawn admission and neighbor metadata.
    std::vector<RaceTrafficSetup> traffic;
    if(traffic_enabled)for(const auto& actor:grid.traffic)
        traffic.push_back({actor.placement_spec,actor.hull,actor.assets.car_type_name,unsigned(actor.assets.car_type)});
    require(run.set_intro_traffic(traffic,error),error);
    TrackSlice track;
    require(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,event,false,round),error);
    require(run.start(root,track,error),error);
    const bool check_grid=traffic_enabled;
    const auto active_count=[](const RaceTraffic& owner) {
        const auto& cars=owner.actors();
        return std::count_if(cars.begin(),cars.end(),[](const RaceTrafficActor& car){return car.active;});
    };
    if(check_grid)require(run.session().traffic()!=nullptr,"authored traffic pool unavailable");
    const auto initial_count=check_grid?active_count(*run.session().traffic()):0;
    if(check_grid)require(initial_count>=(start_only?1:2),"traffic must already be placed before countdown");
    if(fallback_start) {
        const auto& session=run.session();
        const auto& cars=session.traffic()->actors();
        const float spacing=grid.setup->track.traffic_start_spacing[unsigned(grid.setup->race.direction)];
        const auto separated=[&](const VehicleState& a,const VehicleState& b) {
            return std::hypot(a.position.x-b.position.x,a.position.y-b.position.y)>=spacing;
        };
        for(unsigned i=0;i<cars.size();++i)if(cars[i].active) {
            require(separated(cars[i].car,session.car()),"fallback traffic overlaps the player");
            for(unsigned j=0;j<session.rival_count();++j)
                require(separated(cars[i].car,session.rival(j).car()),"fallback traffic overlaps a rival");
            for(unsigned j=0;j<i;++j)if(cars[j].active)
                require(separated(cars[i].car,cars[j].car),"fallback traffic violates initial spacing");
        }
    }
    if(start_only) {
        const auto& session=run.session();
        for(unsigned frame=0;frame<420;++frame) {
            GamepadFrame pad;pad.pads[0].connected=true;
            require(run.simulate(pad,error)&&run.commit(error),error);
            if(session.timing().stage!=RaceStage::racing)continue;
            const auto& traffic_owner=*session.traffic();
            const auto at_go=active_count(traffic_owner);
            require(at_go>0,"no staged traffic survived to GO");
            if(fallback_start)require(at_go==initial_count,"drag fallback field changed before GO");
            require(std::uint64_t(initial_count)==traffic_owner.spawns,"recycler ran during the start grid");
            std::cout<<"PASS start event "<<event<<" round "<<round<<" with "<<initial_count
                     <<" staged and "<<at_go<<" active at GO"<<'\n';
            return 0;
        }
        throw std::runtime_error("traffic startup check never reached GO");
    }
    bool checked_go=false;
    std::vector<unsigned char> road_image;RacingLineTrack roads;
    const auto raw_road=load_race_route_track(root/"Tracks"/grid.course.name,road_image);
    require(raw_road.ok()&&parse_racing_line_image(road_image.data(),road_image.size(),{},roads),
            "raw traffic spawn admission input unavailable");
    std::vector<std::uint64_t> admitted_epochs(grid.traffic.size());
    if(traffic_enabled) {
        const auto read_word=[](const unsigned char* bytes) {
            return unsigned(bytes[0])|(unsigned(bytes[1])<<8);
        };
        const auto read_dword=[&](const unsigned char* bytes) {
            return read_word(bytes)|(read_word(bytes+2)<<16);
        };
        std::array<bool,kRouteMaxIndex+1> fallback_roads{};
        for(std::size_t offset=0;offset+8<=road_image.size();) {
            const auto id=read_dword(road_image.data()+offset);
            const auto size=read_dword(road_image.data()+offset+4);
            offset+=8;
            require(size<=road_image.size()-offset,"invalid traffic road chunk length");
            if(id==0x34124) {
                require(size%0x108==0,"invalid authored fallback road chunk");
                for(std::size_t pos=offset;pos<offset+size;pos+=0x108) {
                    const auto road=read_dword(road_image.data()+pos);
                    if(road<fallback_roads.size())fallback_roads[road]=true;
                }
            }
            offset+=size;
        }
        const auto& cars=run.session().traffic()->actors();
        for(std::size_t i=0;i<cars.size();++i)if(cars[i].active&&cars[i].cursor.route>=99) {
            const auto& car=cars[i];
            require(unsigned(car.cursor.route)<fallback_roads.size()&&fallback_roads[car.cursor.route],
                    "staged fallback traffic must use an authored fallback road");
            const auto* road=roads.find(car.cursor.route);
            require(road&&car.cursor.point<road->points.size()&&
                    ai_traffic_spawn_point_allowed(road->points[car.cursor.point].unknown_84,car.lane<6),
                    "staged fallback traffic must use an allowed authored point");
            admitted_epochs[i]=car.activations;
        }
    }
    bool completed=false,staged_traffic=false,staged_motion=false;
    std::vector<VehicleVec3> held_positions;
    std::vector<std::uint64_t> held_epochs;
    for(unsigned frame=0;frame<4200;++frame) {
        GamepadFrame pad;pad.pads[0].connected=true;
        require(run.simulate(pad,error),error);
        const auto& race=run.session();completed=true;
        if(check_grid&&!checked_go&&race.timing().stage==RaceStage::racing) {
            require(active_count(*race.traffic())==initial_count,"drag grid traffic was removed during countdown");
            checked_go=true;
        }
        if(traffic_enabled&&race.traffic()) {
            const auto& cars=race.traffic()->actors();
            for(std::size_t i=0;i<cars.size();++i)if(cars[i].active&&admitted_epochs[i]!=cars[i].activations) {
                const auto& car=cars[i];const auto* route=roads.find(car.cursor.route);
                require(route&&car.cursor.point<route->points.size(),"spawn point exists in authored road");
                require(ai_traffic_spawn_point_allowed(route->points[car.cursor.point].unknown_84,car.lane<6),
                        "header-enabled race-only lane must not admit a traffic activation");
                require(route->index>=0&&route->index<99,"drag spawns use original599B20 table range");
                require(ai_traffic_drag_spawn_margin(car.cursor.point,unsigned(route->points.size()),car.lane<6),
                        "drag spawns retain native authored endpoint margin");
                admitted_epochs[i]=car.activations;
            }
        }
        if(traffic_enabled&&race_car_held(race.timing())&&race.traffic()) {
            const auto& cars=race.traffic()->actors();
            held_positions.resize(cars.size());held_epochs.resize(cars.size());
            for(std::size_t i=0;i<cars.size();++i)if(cars[i].active) {
                const auto& car=cars[i];staged_traffic=true;
                const auto& old=held_positions[i];const auto& now=car.car.position;
                if(held_epochs[i]==car.activations&&
                   (std::abs(old.x-now.x)>.001||std::abs(old.y-now.y)>.001))staged_motion=true;
                held_positions[i]=now;held_epochs[i]=car.activations;
            }
        }
        for(unsigned i=0;i<3;++i) {
            const auto status=race.timing().racers[i+1].status;
            //465D00 can legitimately total a selector1 drag rival too;
            //the original recording confirms all three use selector1.
            //No-traffic cases retain the strict three-finish path witness.
            const bool native_crash=traffic_enabled&&status==RacerStatus::totalled&&
                race.rival(i).car().drag_totalled;
            if(status!=RacerStatus::not_finished&&status!=RacerStatus::normal_completion&&!native_crash) {
                const auto& p=race.rival(i).car().position;
                std::cerr<<"FAIL rival="<<i<<" status="<<unsigned(status)<<" frame="<<frame
                    <<" position="<<p.x<<','<<p.y<<','<<p.z<<'\n';
                throw std::runtime_error("rival failed in fixed tournament path regression");
            }
            completed&=status==RacerStatus::normal_completion||native_crash;
        }
        require(run.commit(error),error);
        if(completed)break;
    }
    require(completed,"not all rivals reached the finish");
    require(!check_grid||checked_go,"drag traffic grid check never reached GO");
    if(traffic_enabled) {
        require(run.session().traffic()&&run.session().traffic()->spawns>0,"traffic regression spawned no cars");
        require(staged_traffic&&staged_motion,"native traffic must spawn and move before GO");
        unsigned finishers=0;
        for(unsigned i=0;i<3;++i)finishers+=run.session().timing().racers[i+1].status==RacerStatus::normal_completion?1U:0U;
        require(finishers>0,"traffic witness has no completed driving path");
    }
    std::cout<<"PASS tournament9 round "<<round<<": rival paths terminate with authored props, traffic="<<traffic_enabled<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
