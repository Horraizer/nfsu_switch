#include "nfsu/intro_grid.h"
#include "nfsu/race_run.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace nfsu;
namespace {
void require(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
}
int main(int argc,char** argv) {try {
    require(argc>=2&&argc<=4,"expected retail root, optional event and frame count");
    const auto root=argument_path(argv[1]);
    const unsigned event=argc>2?unsigned(std::stoul(argv[2])):12;
    const unsigned frames=argc>3?unsigned(std::stoul(argv[3])):9000;
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
    RaceRun run;
    require(run.set_event(event,error)&&run.set_car("CIVIC",error),error);
    require(run.set_intro_grid(human->pose,human->hull,rivals,contexts,error,human->config.final_grid),error);
    std::vector<RaceTrafficSetup> setup;
    for(const auto& a:grid.traffic)setup.push_back({a.placement_spec,a.hull,a.assets.car_type_name,unsigned(a.assets.car_type)});
    require(!setup.empty(),"no traffic in selected event");
    require(run.set_intro_traffic(setup,error),error);
    TrackSlice track;
    require(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,event),error);
    require(run.start(root,track,error),error);
    std::cout<<std::setprecision(9);
    unsigned measured=0,full=0,promotions=0;double max_error=0,max_full=0,max_kick=0;
    RaceRouteDriver driver(race_event_driving_line(event));
    std::vector<bool> full_before(setup.size());
    std::vector<unsigned> promotion_age(setup.size(),100);
    std::vector<std::uint64_t> registrations(setup.size());
    std::vector<VehicleState> previous;
    for(const auto& a:run.session().traffic()->actors())previous.push_back(a.car);
    for(unsigned frame=0;frame<frames;++frame) {
        GamepadFrame pad=driver.sample(run.session());
        require(run.simulate(pad,error)&&run.commit(error),error);
        const auto* traffic=run.session().traffic();require(traffic!=nullptr,"missing traffic owner");
        for(unsigned i=0;i<traffic->actors().size();++i) {
            const auto& a=traffic->actors()[i];
            if(!a.active){full_before[i]=false;promotion_age[i]=100;continue;}
            if(registrations[i]!=a.registration){full_before[i]=false;promotion_age[i]=100;registrations[i]=a.registration;}
            if(a.impacted&&!full_before[i]) {
                ++promotions;promotion_age[i]=0;
                const auto& human_car=run.session().car();
                const double kick=a.car.velocity.z-previous[i].velocity.z;
                if(a.contacts==0)max_kick=std::max(max_kick,std::abs(kick));
                std::cout<<"PROMOTION frame="<<frame<<" actor="<<i<<" model="<<a.setup.name
                    <<" contacts="<<a.contacts<<" distance="<<std::hypot(a.car.position.x-human_car.position.x,
                        a.car.position.y-human_car.position.y)<<" z="<<a.car.position.z<<" vz="<<a.car.velocity.z
                    <<" prior_z="<<previous[i].position.z<<" prior_vz="<<previous[i].velocity.z
                    <<" action="<<a.action_owner.current.kind<<'\n';
            }
            full_before[i]=a.impacted;
            if(promotion_age[i]<100)++promotion_age[i];
            previous[i]=a.car;
            if(a.impacted)++full;
            auto position=a.car.position;PlayerRigidMatrix axes;
            const auto hubs=vehicle_placement_hubs(a.setup.spec.handling,a.published_heading,position,axes);
            TrackVehicleGround ground(run.session().course().world,true,TrackStrutQuery::cached_height,
                &run.session().course().support_faces);
            std::array<VehicleTrackCast,4> casts;ground.probe_strut_heights(hubs,casts);
            std::array<VehicleGroundHit,4> hits;
            for(unsigned k=0;k<4;++k)hits[k]=casts[k].hit;
            if(!vehicle_placement_ground(a.setup.spec.handling,hubs,hits,position,axes))continue;
            const double gap=a.car.position.z-position.z;
            if(a.impacted) {
                if(std::abs(gap)>max_full){max_full=std::abs(gap);
                    if(max_full>.5)std::cout<<"FULL frame="<<frame<<" actor="<<i<<" model="<<a.setup.name
                        <<" contacts="<<a.contacts<<" car="<<a.car.position.x<<','<<a.car.position.y<<','
                        <<a.car.position.z<<" fresh="<<position.z<<" gap="<<gap<<" vz="<<a.car.velocity.z<<'\n';}
                // These bounded retail driving lines reproduce the false
                // launch immediately after ordinary->rigid activation.
                // Later airborne/impact motion is deliberately unrestricted.
                require(a.contacts>0||promotion_age[i]>4||gap<1,
                    "traffic flies during approach before any contact");
                continue;
            }
            ++measured;
            if(std::abs(gap)>max_error){max_error=std::abs(gap);
                std::cout<<"HEIGHT frame="<<frame<<" actor="<<i<<" model="<<a.setup.name
                    <<" route="<<a.cursor.route<<" point="<<a.cursor.point<<" car="<<a.car.position.x<<','
                    <<a.car.position.y<<','<<a.car.position.z<<" fresh="<<position.z<<" gap="<<gap<<'\n';}
            require(std::abs(gap)<1,"ordinary traffic floats above fresh four-wheel support");
        }
    }
    require(measured>0&&promotions>0,"missing ordinary support or live promotion coverage");
    std::cout<<"PASS traffic height event="<<event<<" track="<<run.track_number()<<" frames="<<frames<<" ordinary="<<measured
        <<" full="<<full<<" promotions="<<promotions<<" max_error="<<max_error<<" max_full="<<max_full
        <<" max_promotion_dvz="<<max_kick<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL traffic height: "<<e.what()<<'\n';return 1;}}
