// Real planner/driver/physics integration on retail UG0 and event8.
// Host pilot input is synthetic; this does not assert original race times.
#include "nfsu/intro_grid.h"
#include "nfsu/race_run.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include <iostream>
#include <stdexcept>
using namespace nfsu;
void need(bool okay,const std::string& error){if(!okay)throw std::runtime_error(error);}
int main(int argc,char** argv){try {
    if(argc!=3)return 2;
    const auto root=argument_path(argv[1]);const unsigned event=unsigned(std::stoul(argv[2]));
    IntroGrid grid;std::string error;VehicleUpgradeLevels levels;
    if(event==8){levels.engine_a=levels.gearbox=levels.tyres=1;levels.drive_split_item=true;}
    need(load_race_grid(root,event,event?"CIVIC":"INTEGRA",1,grid,error,event==8?&levels:nullptr),error);
    const IntroGridActor* player=nullptr;std::vector<RaceRivalSetup> rivals;std::array<int,3> contexts{};
    for(const auto& a:grid.actors) {
        if(a.config.role==Event0Role::opponent) {
            contexts[rivals.size()]=a.config.selected_context;
            rivals.push_back({a.placement_spec,a.pose,a.hull,a.config.event_label,a.assets.car_type_name,a.config.ai_mode,a.config.config_id});
        }else player=&a;
    }
    need(player&&rivals.size()==3,"three rivals absent");
    TrackSlice track;need(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,event),error);
    RaceRun run;need(run.set_event(event,error)&&run.set_car(event?"CIVIC":"INTEGRA",error),error);
    if(player->config.performance)need(run.set_performance_levels(*player->config.performance,error),error);
    need(run.set_intro_grid(player->pose,player->hull,rivals,contexts,error,player->config.final_grid),error);
    std::vector<RaceTrafficSetup> traffic;
    for(const auto& a:grid.traffic)traffic.push_back({a.placement_spec,a.hull,a.assets.car_type_name,unsigned(a.assets.car_type)});
    need(run.set_intro_traffic(traffic,error),error);
    need(run.set_steering(RaceSteering::recovered_predicate1,error),error);
    need(run.start(root,track,error),error);
    if(event==8) {
        const std::array<std::int16_t,3> retail_roads{{1,25,24}};
        unsigned rival=0;
        for(const auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent) {
            need(run.session().rival_actor_id(rival)==actor.config.config_id,"rival recovery uses authored actor identity");
            need(run.session().rival_start_road(rival)==retail_roads[rival],
                "rival starts on its authored road");
            ++rival;
        }
    }
    // A separate read-only planner probes the published driver contract on
    // moving production actors. It never feeds controls back into RaceRun.
    IntroAiPlanner publication;
    std::array<const VehicleHull*,4> hulls{{&rivals[0].hull,&rivals[1].hull,&rivals[2].hull,&player->hull}};
    need(publication.load(root,contexts,hulls,true,run.track_number()),"publication planner load");
    unsigned moving_publications=0;
    RaceRouteDriver driver(event==8?RaceDrivingLine::event8_forward:RaceDrivingLine::event1_forward);
    for(unsigned frame=0;frame<36000;++frame) {
        if(frame<1800) {
            need(publication.observe_human(run.session()),"publication human observation");
            RaceRivalPlans planned;
            need(publication.plan(run.session(),planned),"publication plan refused");
            for(unsigned i=0;i<3;++i) {
                const float distance=run.session().rival(i).travelled_distance();
                need(planned.cars[i].route_distance==distance,
                     "ordinary driver publication must use actor odometer, not local road coordinate; frame="+
                     std::to_string(frame)+" actor="+std::to_string(i)+" got="+
                     std::to_string(planned.cars[i].route_distance)+" expected="+std::to_string(distance));
                if(distance>2.F)++moving_publications;
            }
        }
        if(!run.simulate(driver.sample(run.session()),error)||!run.commit(error)) {
            std::cerr<<"FAIL frame="<<frame<<" "<<error<<'\n';return 1;
        }
        if(frame%6000==0) {
            std::cout<<"FRAME "<<frame<<" human_lap="<<run.session().progress().lap;
            for(unsigned i=0;i<3;++i)std::cout<<" rival"<<i<<"="<<run.session().rival(i).progress().coordinate.distance<<":"<<run.session().rival(i).car().owner_speeds.magnitude;
            std::cout<<'\n';
        }
        bool done=true;
        for(unsigned i=0;i<3;++i)done=done&&run.session().timing().racers[i+1].status!=RacerStatus::not_finished;
        if(done){
            for(unsigned extra=0;extra<180;++extra)need(run.simulate(driver.sample(run.session()),error)&&run.commit(error),error);
            for(unsigned i=0;i<3;++i)need(run.session().rival(i).car().owner_speeds.magnitude<1.F,"finished rival did not stop");
            need(moving_publications>0,"moving actor publication coverage");
            std::cout<<"PASS three rivals finished and stopped, frame="<<frame<<'\n'<<run.difficulty_line()<<'\n';return 0;}
    }
    std::cerr<<"FAIL race not complete\n";return 1;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
