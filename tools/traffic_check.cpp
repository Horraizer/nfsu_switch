#include "nfsu/ai_driver.h"
#include "nfsu/traffic_motion.h"
#include "nfsu/race_setup.h"
#include "nfsu/car_assets.h"
#include "nfsu/intro_grid.h"
#include "nfsu/race_traffic.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>
int main(int argc,char** argv){
    if(argc==3 && std::string(argv[2])=="world") {
        using namespace nfsu;
        const auto root=std::filesystem::path(argv[1]);IntroGrid grid;std::string error;
        if(!load_intro_grid(root,grid,error)){std::cerr<<"FAIL "<<error;return 1;}
        RaceTraffic traffic;std::vector<RaceTrafficSetup> setups;
        for(const auto& a:grid.traffic)setups.push_back({a.placement_spec,a.hull,a.assets.car_type_name});
        if(!traffic.load(root,grid.course,setups,error)){std::cerr<<"FAIL "<<error;return 1;}
        std::array<VehicleState,4> observers;
        std::array<const VehicleState*,4> bodies;
        for(unsigned i=0;i<4;++i){observers[i].position=grid.actors[1].state.position;bodies[i]=&observers[i];}
        const auto observer_box=race_contact_box(grid.actors[1].hull);
        std::array<const RaceContactBox*,4> shapes;shapes.fill(&observer_box);
        std::cout<<std::setprecision(9);
        for(unsigned tick=0;tick<7200;++tick) {
            if(!traffic.advance(bodies,tick,error,-1,1.F/60.F,{},shapes)){std::cerr<<"FAIL "<<error;return 1;}
            if(tick%60==0)for(unsigned i=0;i<traffic.actors().size();++i){
                const auto& a=traffic.actors()[i];if(!a.active)continue;
                std::cout<<"CAR "<<tick<<' '<<i<<' '<<a.setup.name<<' '<<a.activations<<' '
                    <<a.cursor.route<<' '<<a.cursor.point<<' '<<a.lane<<' '
                    <<a.motion.x<<' '<<a.motion.y<<' '<<a.motion.heading<<' '<<a.motion.speed<<' '
                    <<a.target_speed<<' '<<a.ended<<' '<<a.driver.steering.word;
                for(unsigned j=0;j<std::min<unsigned>(a.path.count,4);++j)
                    std::cout<<' '<<a.path.points[j][0]<<' '<<a.path.points[j][1];
                std::cout<<'\n';
            }
        }
        std::cout<<"PASS isolated traffic world, no racer contacts\n";return 0;
    }
    if(argc==2) {
        const auto root=std::filesystem::path(argv[1]);
        const auto table=nfsu::load_car_type_table(root/"Global/GLOBALB.BUN");
        if(!table.ok())return 2;
        bool all=true;
        for(const auto& r:table.records)if(r.class_id==2) {
            nfsu::VehicleSpec spec;std::string error;
            bool ok=nfsu::load_race_vehicle_spec(root/"Global/GLOBALB.BUN",r.name,{},spec,error);
            all=all&&ok;
            std::cout<<r.name<<" "<<ok<<" "<<error<<"\n";
        }
        return all?0:1;
    }
    float speed,target,lateral;unsigned frames;
    while(std::cin>>speed>>target>>lateral>>frames) {
        nfsu::AiDriverForwardInput in;in.path.count=2;
        in.path.points[0]={10,lateral,0};in.path.points[1]={100,lateral*10,0};
        in.velocity={speed,0,0};in.forward_speed=in.absolute_speed=speed;
        in.desired_speed=target;in.category=2;
        nfsu::AiDriverForwardState state;
        for(unsigned k=0;k<frames;++k) {
            if(!nfsu::ai_driver_traffic_step(in,1.F/60,state))return 3;
            std::cout<<state.steering.word<<" "<<std::setprecision(9)<<state.pedals.throttle<<" "<<state.pedals.brake<<"\n";
        }
    }
}
