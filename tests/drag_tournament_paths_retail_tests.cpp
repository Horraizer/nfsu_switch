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
    require(argc==3||argc==4,"expected retail root, tournament round and optional traffic");
    const auto root=argument_path(argv[1]);const unsigned round=unsigned(std::stoul(argv[2]));
    const bool traffic_enabled=argc==4&&std::string(argv[3])=="traffic";
    require(argc==3||traffic_enabled,"unknown traffic option");
    require(round<3,"invalid tournament round");
    IntroGrid grid;std::string error;
    require(load_race_grid(root,9,"CIVIC",2,grid,error,nullptr,nullptr,round),error);
    const IntroGridActor* human=nullptr;std::vector<RaceRivalSetup> rivals;std::array<int,3> contexts{};
    for(const auto& actor:grid.actors) {
        if(actor.config.role==Event0Role::opponent) {
            require(rivals.size()<3,"unexpected rival count");
            contexts[rivals.size()]=actor.config.selected_context;
            rivals.push_back({actor.placement_spec,actor.pose,actor.hull,actor.config.event_label,
                actor.assets.car_type_name,actor.config.ai_mode,actor.config.config_id});
        }else human=&actor;
    }
    require(human&&rivals.size()==3,"missing authored drag grid");
    RaceRun run;require(run.set_event(9,error,round)&&run.set_car("CIVIC",error)&&run.set_difficulty(2,error),error);
    require(run.set_intro_grid(human->pose,human->hull,rivals,contexts,error),error);
    // Human is deliberately idle. Empty-pool cases isolate closures;
    // pool cases also exercise real spawn admission and neighbor metadata.
    std::vector<RaceTrafficSetup> traffic;
    if(traffic_enabled)for(const auto& actor:grid.traffic)
        traffic.push_back({actor.placement_spec,actor.hull,actor.assets.car_type_name,unsigned(actor.assets.car_type)});
    require(run.set_intro_traffic(traffic,error),error);
    TrackSlice track;
    require(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,9,false,round),error);
    require(run.start(root,track,error),error);
    const bool check_grid=traffic_enabled&&round==0;
    const auto active_count=[](const RaceTraffic& owner) {
        const auto& cars=owner.actors();
        return std::count_if(cars.begin(),cars.end(),[](const RaceTrafficActor& car){return car.active;});
    };
    const auto initial_count=check_grid?active_count(*run.session().traffic()):0;
    if(check_grid)require(initial_count>1,"drag traffic must already be placed before the first countdown frame");
    bool checked_go=false;
    std::vector<unsigned char> road_image;RacingLineTrack roads;
    const auto raw_road=load_race_route_track(root/"Tracks"/grid.course.name,road_image);
    require(raw_road.ok()&&parse_racing_line_image(road_image.data(),road_image.size(),{},roads),
            "raw traffic spawn admission input unavailable");
    std::vector<std::uint64_t> admitted_epochs(grid.traffic.size());
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
