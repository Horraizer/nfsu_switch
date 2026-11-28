#include "nfsu/intro_grid.h"
#include "nfsu/race_run.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace nfsu;
namespace {
void require(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
struct Counts {
    std::uint64_t samples=0,throttle=0,brake=0,strong_brake=0,weak_light=0,contacts=0;
    std::uint64_t light_switches=0,boost_switches=0,pedal_switches=0,quick_switches=0,target_changes=0;
    double min_speed=1000,max_speed=0,max_acceleration=0,max_deceleration=0;
};
struct Previous {
    bool active=false,light=false,strong=false;int pedal=0,action=0;
    std::uint64_t registration=0;unsigned pedal_frame=0;
    float target=0,speed=0,motion_speed=0;
};
}
int main(int argc,char** argv) {try {
    require(argc>=2&&argc<=5,"expected retail root, optional event, frames and trace CSV");
    const auto root=argument_path(argv[1]);
    const unsigned event=argc>2?unsigned(std::stoul(argv[2])):1;
    const unsigned frames=argc>3?unsigned(std::stoul(argv[3])):9000;
    SupportedRaceSelection selection;
    require(supported_race_selection(event,0,selection),"unsupported event");
    if(selection.type==RaceType::drift||selection.type==RaceType::drag) {
        std::cout<<"SKIP traffic pedals event="<<event<<" type="<<race_type_name(unsigned(selection.type))
            <<" reason=no_ordinary_road_traffic\n";return 0;
    }
    IntroGrid grid;std::string error;
    require(load_race_grid(root,event,"CIVIC",1,grid,error),error);
    const IntroGridActor* human=nullptr;std::vector<RaceRivalSetup> rivals;
    std::array<int,3> contexts{};
    for(const auto& actor:grid.actors) {
        if(actor.config.role==Event0Role::opponent) {
            contexts[rivals.size()]=actor.config.selected_context;
            rivals.push_back({actor.placement_spec,actor.pose,actor.hull,actor.config.event_label,
                actor.assets.car_type_name,actor.config.ai_mode,actor.config.config_id});
        }else human=&actor;
    }
    require(human!=nullptr,"missing player grid");
    if(grid.traffic.empty()) {
        std::cout<<"SKIP traffic pedals event="<<event<<" reason=empty_authored_traffic_pool\n";return 0;
    }
    RaceRun run;
    require(run.set_event(event,error)&&run.set_car("CIVIC",error),error);
    require(run.set_intro_grid(human->pose,human->hull,rivals,contexts,error,human->config.final_grid),error);
    std::vector<RaceTrafficSetup> setup;
    for(const auto& a:grid.traffic)setup.push_back({a.placement_spec,a.hull,a.assets.car_type_name,unsigned(a.assets.car_type)});
    require(run.set_intro_traffic(setup,error),error);
    TrackSlice track;
    require(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,event),error);
    require(run.start(root,track,error),error);
    std::ofstream trace,roads;
    if(argc>4) {
        const auto path=argument_path(argv[4]);require(!std::filesystem::exists(path),"trace already exists");
        trace.open(path);require(bool(trace),"cannot open trace");trace<<std::setprecision(9);
        auto road_path=path;road_path.replace_extension(".roads.csv");
        require(!std::filesystem::exists(road_path),"road trace already exists");
        roads.open(road_path);require(bool(roads),"cannot open road trace");roads<<std::setprecision(9);
        roads<<"event,frame,actor,category,first,distance,speed,cap,stopped,route_speed,action_speed,duration,auxiliary,target,count,cell,cell_distance,slope,speed_byte\n";
        trace<<"event,frame,actor,model,registration,rigid,action,contacts,crash_stop,speed,input_speed,target,throttle,brake,light,cap,route_speed,error,integral,derivative,x,y,z\n";
    }
    std::array<Counts,3> counts{};std::array<Counts,32> actions{};
    std::vector<Previous> previous(setup.size());
    for(unsigned i=0;i<previous.size();++i) {
        const auto& a=run.session().traffic()->actors()[i];
        previous[i].motion_speed=a.motion.speed;
    }
    RaceRouteDriver driver(race_event_driving_line(event));
    for(unsigned frame=0;frame<frames;++frame) {
        const auto pad=driver.sample(run.session());
        require(run.simulate(pad,error)&&run.commit(error),error);
        const auto* traffic=run.session().traffic();require(traffic!=nullptr,"missing traffic owner");
        for(unsigned i=0;i<traffic->actors().size();++i) {
            const auto& a=traffic->actors()[i];auto& p=previous[i];
            if(!a.active){p.active=false;continue;}
            const float throttle=a.driver.pedals.throttle,brake=a.driver.pedals.brake;
            const int pedal=brake>0?-1:throttle>0?1:0;
            const bool continuing=p.active&&p.registration==a.registration;
            const bool light=a.animation.braking;
            const auto action=unsigned(a.action_owner.current.kind);
            require(action<actions.size(),"unknown traffic action");
            const bool pedal_change=continuing&&pedal!=p.pedal;
            const bool light_change=continuing&&light!=p.light;
            const bool target_change=continuing&&std::abs(a.target_speed-p.target)>.01F;
            if(roads&&target_change&&!a.impacted&&!a.crash_stop&&a.neighbors.self_index>=0) {
                const auto& self=a.neighbors.entries[unsigned(a.neighbors.self_index)];
                for(unsigned cell=0;cell<a.road.chain.count;++cell) {
                    const auto& point=a.road.chain.cells[cell];
                    roads<<event<<','<<frame<<','<<i<<','<<a.road.category<<','<<self.cell<<','<<self.distance<<','
                        <<p.speed<<','<<a.speed_cap<<','<<a.crash_stop<<','<<a.action_speed.route_speed<<','
                        <<a.action_speed.speed<<','<<a.action_speed.duration<<','<<a.action_speed.auxiliary<<','
                        <<a.target_speed<<','<<a.road.chain.count<<','<<cell<<','<<point.distance<<','
                        <<point.point.unknown_30<<','<<unsigned(point.point.speed[unsigned(a.road.category)])<<'\n';
                }
            }
            const double acceleration=continuing?(a.car.owner_speeds.magnitude-p.speed)*60:0;
            for(auto* c:{&counts[0],&counts[a.impacted?2:1],&actions[action]}) {
                ++c->samples;c->throttle+=throttle>0;c->brake+=brake>0;c->strong_brake+=brake>.05000000074505806F;
                c->weak_light+=light&&brake<=.05000000074505806F;c->contacts+=a.car.collision_body_flag>0;
                c->light_switches+=light_change;c->pedal_switches+=pedal_change;c->target_changes+=target_change;
                c->boost_switches+=continuing&&((brake>.05000000074505806F)!=p.strong);
                c->quick_switches+=pedal_change&&frame-p.pedal_frame<=10;
                c->min_speed=std::min(c->min_speed,double(a.car.owner_speeds.magnitude));
                c->max_speed=std::max(c->max_speed,double(a.car.owner_speeds.magnitude));
                c->max_acceleration=std::max(c->max_acceleration,acceleration);
                c->max_deceleration=std::max(c->max_deceleration,-acceleration);
            }
            if(trace) {
                const auto e=a.driver.speed.error();
                const float input_speed=continuing&&!a.impacted?p.motion_speed:
                    (a.crash_stop?0:a.target_speed)-e.current;
                trace<<event<<','<<frame<<','<<i<<','<<a.setup.name<<','<<a.registration<<','<<a.impacted<<','<<action
                    <<','<<a.contacts<<','<<a.crash_stop<<','<<a.car.owner_speeds.magnitude<<','<<input_speed<<','<<a.target_speed<<','
                    <<throttle<<','<<brake<<','<<light<<','<<a.speed_cap<<','<<a.action_speed.route_speed<<','
                    <<e.current<<','<<e.integral<<','<<e.derivative<<','<<a.car.position.x<<','<<a.car.position.y<<','<<a.car.position.z<<'\n';
            }
            if(!continuing||pedal_change)p.pedal_frame=frame;
            p.active=true;p.registration=a.registration;p.pedal=pedal;p.light=light;p.action=int(action);
            p.strong=brake>.05000000074505806F;
            p.target=a.target_speed;p.speed=a.car.owner_speeds.magnitude;p.motion_speed=a.motion.speed;
        }
    }
    require(counts[0].samples>0,"no active ordinary road traffic sampled");
    std::cout<<std::setprecision(9);
    const auto print=[&](const char* group,unsigned id,const Counts& c) {
        if(!c.samples)return;
        std::cout<<"TRAFFIC_PEDALS event="<<event<<" track="<<run.track_number()<<" frames="<<frames<<" group="<<group<<" id="<<id
            <<" samples="<<c.samples<<" throttle="<<c.throttle<<" brake="<<c.brake<<" strong_brake="<<c.strong_brake
            <<" weak_command="<<c.weak_light<<" brake_switches="<<c.light_switches<<" boost_switches="<<c.boost_switches<<" pedal_switches="<<c.pedal_switches
            <<" quick_switches="<<c.quick_switches<<" target_changes="<<c.target_changes<<" contact_samples="<<c.contacts
            <<" speed_min="<<c.min_speed<<" speed_max="<<c.max_speed<<" max_acceleration="<<c.max_acceleration
            <<" max_deceleration="<<c.max_deceleration<<'\n';
    };
    for(unsigned i=0;i<counts.size();++i)print("mover",i,counts[i]);
    for(unsigned i=0;i<actions.size();++i)print("action",i,actions[i]);
    std::cout<<"PASS traffic pedal measurement event="<<event<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL traffic pedals: "<<e.what()<<'\n';return 1;}}
