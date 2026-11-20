// Real planner/driver/physics integration on retail UG0 and event8.
// Host pilot input is synthetic; this does not assert original race times.
#include "nfsu/intro_grid.h"
#include "nfsu/race_run.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "nfsu/ai_path_choice.h"
#include <iostream>
#include <stdexcept>
using namespace nfsu;
void need(bool okay,const std::string& error){if(!okay)throw std::runtime_error(error);}
bool same_driver_path(const AiDriverPath& a,const AiDriverPath& b) {
    if(a.count!=b.count)return false;
    for(unsigned j=0;j<a.count;++j)if(a.points[j]!=b.points[j])return false;
    return true;
}
struct EpochPaths {
    std::array<AiDriverPath,3> prior{};
    bool ready=true; // Native fresh Driver publications are empty.
    unsigned reads=0,witnesses=0;
    static void observe(void* context,const AiPathCollisionWorld& world) {
        auto& s=*static_cast<EpochPaths*>(context);
        s.reads|=1U<<world.self;
        if(!s.ready)return;
        for(unsigned j=0;j<3;++j)if(j!=world.self)
            need(same_driver_path(s.prior[j],world.actors[j].path.path),
                "neighbor path changed inside controller epoch: reader="+std::to_string(world.self)+
                " target="+std::to_string(j)+" tick="+std::to_string(world.tick));
    }
    void commit(const RaceRivalPlans& planned) {
        if(ready)for(unsigned j=0;j<3;++j)
            if((reads&~((1U<<(j+1))-1U))&&!same_driver_path(prior[j],planned.cars[j].path))++witnesses;
        for(unsigned j=0;j<3;++j)prior[j]=planned.cars[j].path;
        ready=true;reads=0;
    }
};
int main(int argc,char** argv){try {
    if(argc!=3)return 2;
    const auto root=argument_path(argv[1]);const unsigned event=unsigned(std::stoul(argv[2]));
    IntroGrid grid;std::string error;VehicleUpgradeLevels levels;
    if(event==8){levels.engine_a=levels.gearbox=levels.tyres=1;levels.drive_split_item=true;}
    const std::string car=event==19?"TIBURON":event?"CIVIC":"INTEGRA";
    const unsigned difficulty=event==19?2:1;
    if(event==19) {
        levels.weight=levels.suspension=levels.engine_a=levels.induction=levels.nitrous=1;
        levels.engine_b=levels.gearbox=levels.tyres=levels.grip=levels.aero_a=levels.aero_b=1;
    }
    need(load_race_grid(root,event,car,difficulty,grid,error,event==8||event==19?&levels:nullptr),error);
    const IntroGridActor* player=nullptr;std::vector<RaceRivalSetup> rivals;std::array<int,3> contexts{};
    for(const auto& a:grid.actors) {
        if(a.config.role==Event0Role::opponent) {
            contexts[rivals.size()]=a.config.selected_context;
            rivals.push_back({a.placement_spec,a.pose,a.hull,a.config.event_label,a.assets.car_type_name,a.config.ai_mode,a.config.config_id});
        }else player=&a;
    }
    need(player&&rivals.size()==3,"three rivals absent");
    TrackSlice track;need(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,event),error);
    RaceRun run;need(run.set_event(event,error)&&run.set_car(car,error)&&run.set_difficulty(difficulty,error),error);
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
    if(event==19) {
        {
            IntroAiPlanner reset_adapter;
            need(reset_adapter.load(root,contexts,hulls,true,run.track_number()),"reset road adapter load");
            RaceRivalPlans plans;
            need(reset_adapter.inspect_plan(run.session(),plans),"reset road adapter plan");
            for(unsigned i=0;i<3;++i) {
                const auto& source=plans.cars[i].road_reset;
                need(source.resolve&&source.placed,"production rival plan carries road reset owner");
                const auto& rival=run.session().rival(i);auto displaced=rival.car();
                VehicleRoadResetPoint before,after;
                need(source.resolve(source.context,source.slot,displaced,rival.progress(),12345,before),"initial retained road reset resolves");
                displaced.position={10000000,10000000,0};
                need(source.resolve(source.context,source.slot,displaced,rival.progress(),12345,after)&&
                     before.position.x==after.position.x&&before.position.y==after.position.y&&
                     before.position.z==after.position.z&&before.forward==after.forward,
                     "rival reset uses retained road even when body is far from every road");
                displaced.position=before.position;
                source.placed(source.context,source.slot,displaced,rival.progress(),12345);
                need(source.resolve(source.context,source.slot,displaced,rival.progress(),12345,after)&&
                     before.position.x==after.position.x&&before.position.y==after.position.y,
                     "placement publication preserves the selected road and point");
            }
        }
        // Probe the real adapter with a finite pose beyond its spatial search
        // limit. Only this independent planner sees the injected pose/time;
        // restore the race before the normal physics integration below.
        IntroAiPlanner lost;
        need(lost.load(root,contexts,hulls,true,run.track_number()),"no-road planner load");
        auto& session=const_cast<RaceSession&>(run.session());
        auto& pose=const_cast<VehicleState&>(session.rival(0).car());
        auto& timing=const_cast<RaceTimingState&>(session.timing());
        auto& telemetry=const_cast<VehicleTrackTelemetry&>(session.rival(0).telemetry());
        const auto saved_pose=pose;const auto saved_timing=timing;const auto saved_resets=telemetry.resets;
        RaceRivalPlans before,stopped,restored,reset;
        need(lost.inspect_plan(session,before)&&before.cars[0].path.count>0,"no-road witness needs a prior published path");
        pose.position={10000000,10000000,0};timing.now_ticks+=1000;timing.step=(timing.step/8+1)*8;
        need(lost.inspect_plan(session,stopped),"normal no-road result must not abort the race");
        const auto same_path=[](const AiDriverPath& a,const AiDriverPath& b){
            if(a.count!=b.count)return false;
            for(unsigned j=0;j<a.count;++j)if(a.points[j]!=b.points[j])return false;
            return true;
        };
        need(stopped.cars[0].desired_speed==0&&stopped.cars[0].enabled&&!stopped.cars[0].physics_full_update&&
             same_path(before.cars[0].path,stopped.cars[0].path),"no-road stop must preserve Driver path");
        pose=saved_pose;timing.now_ticks+=1000;timing.step+=8;
        need(lost.inspect_plan(session,restored)&&restored.cars[0].desired_speed==0&&
             same_path(before.cars[0].path,restored.cars[0].path),"road return alone must not clear the native stop latch");
        ++telemetry.resets;timing.now_ticks+=1000;timing.step+=8;
        need(lost.inspect_plan(session,reset)&&reset.cars[0].desired_speed>0,
             "completed placement reset must release the no-road stop latch");
        pose=saved_pose;timing=saved_timing;telemetry.resets=saved_resets;
    }
    unsigned moving_publications=0;
    EpochPaths epoch;
    unsigned observed_selectors=0;
    std::array<unsigned,3> last_selector{{1,1,1}};
    unsigned path_exits=0;
    for(unsigned i=0;i<3;++i)
        need(run.session().rival(i).physical_selector()==1,"ordinary rivals start in selector1");
    RaceRouteDriver driver(event?race_event_driving_line(event):RaceDrivingLine::event1_forward);
    for(unsigned frame=0;frame<36000;++frame) {
        if(frame<1800) {
            need(publication.observe_human(run.session()),"publication human observation");
            RaceRivalPlans planned;
            need(publication.inspect_plan(run.session(),planned,0,nullptr,{&epoch,EpochPaths::observe}),"publication plan refused");
            epoch.commit(planned);
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
        for(unsigned i=0;i<3;++i) {
            const auto& world=run.session();
            const auto selector=world.rival(i).physical_selector();
            observed_selectors|=1U<<selector;
            if(last_selector[i]==4&&selector!=4)++path_exits;
            last_selector[i]=selector;
            need(world.rival(i).car().owner_speeds.magnitude<100.F,"rival velocity discontinuity");
            if(selector==4)
                need(world.timing().go_valid&&world.timing().now_ticks-world.timing().go_ticks>=30*kRaceTicksPerSecond,
                    "distant path solver enabled before native race-time gate");
        }
        if(frame%6000==0) {
            std::cout<<"FRAME "<<frame<<" human_lap="<<run.session().progress().lap;
            for(unsigned i=0;i<3;++i)std::cout<<" rival"<<i<<"="<<run.session().rival(i).progress().coordinate.distance<<":"<<run.session().rival(i).car().owner_speeds.magnitude;
            std::cout<<'\n';
        }
        if(event==19&&frame==6000) {
            need(epoch.witnesses>0,"path epoch needs an earlier changed path and a later reader");
            need((observed_selectors&0x1aU)==0x1aU&&path_exits>0,
                 "Stadium selector mask="+std::to_string(observed_selectors)+
                 " path exits="+std::to_string(path_exits));
            std::cout<<"PASS Stadium opening, selector mask="<<observed_selectors
                     <<" path_exits="<<path_exits<<'\n';
            return 0;
        }
        bool done=true;
        for(unsigned i=0;i<3;++i)done=done&&run.session().timing().racers[i+1].status!=RacerStatus::not_finished;
        if(done){
            for(unsigned extra=0;extra<180;++extra)need(run.simulate(driver.sample(run.session()),error)&&run.commit(error),error);
            for(unsigned i=0;i<3;++i) {
                const auto& rival=run.session().rival(i);
                need(rival.car().owner_speeds.magnitude<1.F,"finished rival did not stop: slot="+
                     std::to_string(i)+" speed="+std::to_string(rival.car().owner_speeds.magnitude)+
                     " support="+std::to_string(rival.car().published_support));
            }
            need(moving_publications>0,"moving actor publication coverage");
            need(epoch.witnesses>0,"path epoch needs an earlier changed path and a later reader");
            need((observed_selectors&0x0aU)==0x0aU,"ordinary rival selector1 to selector3 transition absent");
            for(unsigned i=0;i<3;++i)
                need(run.session().rival(i).physical_selector()==1,"finished rival must return to selector1");
            std::cout<<"RIVAL_SELECTORS mask="<<observed_selectors<<'\n';
            std::cout<<"PASS three rivals finished and stopped, frame="<<frame<<'\n'<<run.difficulty_line()<<'\n';return 0;}
    }
    std::cerr<<"FAIL race not complete\n";return 1;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
