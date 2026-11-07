// Bounded selector3 system seam; flat external ground, no actor/world services.
#include "nfsu/ai_powertrain.h"
#include "nfsu/player_powertrain.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace nfsu;
class Flat final:public AiPhysicsGround {
public:
    bool query(const VehicleHandling& h,const PlayerRigidBodyState& b,
               std::array<AiWheelGround,4>& out) const override {
        for(unsigned k=0;k<4;++k) {
            out[k].hub=player_strut_attachment(h,k,b);
            out[k].height=0;out[k].normal={{0,0,1}};
        }
        return true;
    }
};
static void emit(float x) {std::uint32_t b;std::memcpy(&b,&x,4);std::cout<<' '<<b;}
int main(int argc,char** argv) {
    if(argc!=2&&argc!=3)return 2;
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
    VehicleHandling h;std::vector<VehicleHandlingIssue> issues;
    if(!decode_vehicle_handling(bytes.data(),bytes.size(),h,issues))return 3;
    AiPhysicsState s;AiPowertrainTables tables;Flat ground;
    s.aero.enabled=false;s.body.position={{0,0,.5F}};s.body.alternate_yaw=true;
    s.body.momentum={{20.F*h.mass,0,0}};s.engine.rpm=2500;s.engine.throttle=.5F;
    s.engine.gear_request=2;
    for(auto& w:s.engine.wheels){w.spin=60;w.preload=3;w.contact=true;}
    for(unsigned k=0;k<8;++k){tables.up[k]=1000.F+800.F*float(k);tables.down[k]=500.F+500.F*float(k);}
    unsigned steps;float dt;
    if(!(std::cin>>steps>>dt))return 2;
    for(unsigned i=0;i<steps;++i) {
        if(argc==3&&i==1) {
            s.body.position[2]-=.277F;
            s.body.momentum[2]-=.3F*h.mass;
            s.prepared=false;
        }
        if(!ai_physics_advance(h,tables,{},dt,ground,s))return 4;
        std::cout<<"STATE";
        for(const auto& v:{s.body.position,s.body.momentum,s.body.angular_momentum,
                          s.body.velocity,s.body.angular_velocity,s.body.force,s.body.torque})
            for(float x:v)emit(x);
        for(const auto& axis:s.body.axes)for(float x:axis)emit(x);
        emit(s.body.adaptive_damping);
        for(const auto& w:s.engine.wheels) {
            emit(w.spin);emit(w.compression);emit(w.compression_rate);emit(w.load);
            for(float x:w.feedback)emit(x);
            for(float x:w.slip)emit(x);
            std::cout<<' '<<w.contact<<' '<<w.locked;
        }
        emit(s.engine.rpm);std::cout<<' '<<s.engine.gear<<'\n';
    }
}
