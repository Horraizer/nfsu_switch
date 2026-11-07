// PC original-code comparison seam. Inputs and outputs are component states;
// this does not impersonate the original world/query/physics scheduler.
#include "nfsu/ai_powertrain.h"
#include "nfsu/player_powertrain.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace nfsu;
static void out(float x){std::uint32_t b;std::memcpy(&b,&x,4);std::cout<<' '<<b;}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
    VehicleHandling h;std::vector<VehicleHandlingIssue> issues;
    if(!decode_vehicle_handling(bytes.data(),bytes.size(),h,issues))return 3;
    unsigned mode;
    while(std::cin>>mode) {
        AiPowertrainState s;PlayerRigidBodyState b;AiPowertrainTables tables;
        float dt;std::cin>>h.rear_drive_fraction>>s.rpm>>s.throttle>>s.brake>>s.steering>>s.gear>>s.gear_request
            >>s.boost>>s.grip>>s.low_slip>>s.extra_torque>>s.extra_torque_scale>>s.traction_until>>s.world_time>>dt;
        for(auto& column:b.axes)for(auto& v:column)std::cin>>v;
        for(auto& v:b.velocity)std::cin>>v;
        for(auto& v:b.angular_velocity)std::cin>>v;
        for(auto& w:s.wheels) {
            std::cin>>w.spin>>w.compression>>w.load>>w.contact>>w.locked>>w.preload;
            for(auto& v:w.normal)std::cin>>v;
        }
        for(auto& v:tables.up)std::cin>>v;
        for(auto& v:tables.down)std::cin>>v;
        std::array<AiWheelGround,4> g;
        for(auto& v:g) {std::cin>>v.height;for(auto& x:v.hub)std::cin>>x;for(auto& x:v.normal)std::cin>>x;}
        if(!std::cin)return 4;
        std::array<float,4> rates{};
        if(mode==0) {
            ai_powertrain_feedback(h,b,s);
            rates=ai_powertrain_derivative(h,b,s);
            for(unsigned k=0;k<4;++k)s.wheels[k].spin+=rates[k]*dt;
            ai_powertrain_complete(h,tables,s);
        } else ai_powertrain_struts(h,b,g,dt,s);
        std::cout<<"RESULT";
        for(const auto& w:s.wheels) {
            out(w.spin);out(w.compression);out(w.compression_rate);out(w.load);
            for(auto v:w.feedback)out(v);
            for(auto v:w.slip)out(v);
            std::cout<<' '<<w.contact<<' '<<w.sliding<<' '<<w.locked;out(w.excess);
        }
        for(auto x:b.force)out(x);
        for(auto x:b.torque)out(x);
        for(auto x:rates)out(x);
        out(s.rpm);std::cout<<' '<<s.gear<<std::endl;
    }
    return 0;
}
