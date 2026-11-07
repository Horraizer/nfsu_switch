#include "nfsu/ai_powertrain.h"
#include "nfsu/player_powertrain.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <fstream>
#include <iterator>
using namespace nfsu;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* message) {
    ++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
}
std::uint32_t bits(float v){std::uint32_t b;std::memcpy(&b,&v,4);return b;}
struct Case {unsigned id;std::array<float,111> input;std::array<std::uint32_t,60> expected;};
const Case cases[]={
#include "ai_powertrain_native.inc"
};
struct Reader {
    const std::array<float,111>& values;unsigned index=0;
    template<class T> Reader& operator>>(T& v){v=static_cast<T>(values[index++]);return *this;}
};
void collision_outer() {
    const float cases[][29]={
#include "ai_collision_native.inc"
    };
    for(const auto& row:cases) {
        unsigned at=0;AiCollisionContext c;PlayerRigidBodyState b;PlayerAeroCache aero;
        c.state=unsigned(row[at++]);c.support=unsigned(row[at++]);const float mass=row[at++];
        for(auto& axis:c.actor_axes)for(auto& x:axis)x=row[at++];
        for(auto& x:c.actor_velocity)x=row[at++];
        for(auto& x:b.angular_velocity)x=row[at++];
        for(auto& x:b.torque)x=row[at++];
        ai_collision_policy(c,b,aero);ai_collision_stabilize(c,mass,b);
        for(float x:b.torque) {
            const float expected=row[at++];
            if(bits(x)!=bits(expected))std::cerr<<"case "<<(&row-cases)<<" bits "<<bits(expected)<<"/"<<bits(x)<<" torque "<<at<<" expected "<<expected<<" got "<<x<<"\n";
            check(bits(x)==bits(expected),"original474DD0 complete stabilization torque");
        }
        check(b.linear_damping==bool(row[at++]),"original474A90 linear damping");
        check(b.angular_damping==bool(row[at++]),"original474A90 angular damping");
        check(b.alternate_yaw==bool(row[at++]),"original474A90 alternate tensor");
        check(aero.enabled==bool(row[at++]),"original474A90 aero policy");
    }
}
void primitives() {
    for(const auto& c:cases) {
        auto h=stock_240sx_handling();Reader input{c.input};unsigned mode;input>>mode;
        AiPowertrainState s;PlayerRigidBodyState b;AiPowertrainTables tables;
        float dt;input>>h.rear_drive_fraction>>s.rpm>>s.throttle>>s.brake>>s.steering>>s.gear>>s.gear_request
            >>s.boost>>s.grip>>s.low_slip>>s.extra_torque>>s.extra_torque_scale>>s.traction_until>>s.world_time>>dt;
        for(auto& column:b.axes)for(auto& v:column)input>>v;
        for(auto& v:b.velocity)input>>v;
        for(auto& v:b.angular_velocity)input>>v;
        for(auto& w:s.wheels) {
            input>>w.spin>>w.compression>>w.load>>w.contact>>w.locked>>w.preload;
            for(auto& v:w.normal)input>>v;
        }
        for(auto& v:tables.up)input>>v;
        for(auto& v:tables.down)input>>v;
        std::array<AiWheelGround,4> g;
        for(auto& v:g) {input>>v.height;for(auto& x:v.hub)input>>x;for(auto& x:v.normal)input>>x;}
        std::array<float,4> rates{};
        if(mode==0) {
            ai_powertrain_feedback(h,b,s);
            rates=ai_powertrain_derivative(h,b,s);
            for(unsigned k=0;k<4;++k)s.wheels[k].spin+=rates[k]*dt;
            ai_powertrain_complete(h,tables,s);
        } else ai_powertrain_struts(h,b,g,dt,s);

        std::array<std::uint32_t,60> actual{};unsigned at=0;
        const auto add=[&](float v){actual[at++]=bits(v);};
        for(const auto& w:s.wheels) {
            add(w.spin);add(w.compression);add(w.compression_rate);add(w.load);
            for(float x:w.feedback)add(x);
            for(float x:w.slip)add(x);
            actual[at++]=w.contact;actual[at++]=w.sliding;actual[at++]=w.locked;add(w.excess);
        }
        for(float x:b.force)add(x);
        for(float x:b.torque)add(x);
        for(float x:rates)add(x);
        add(s.rpm);actual[at++]=static_cast<std::uint32_t>(s.gear);
        check(input.index==111&&at==60,"native case schema");
        for(unsigned k=0;k<60;++k) {
            const auto a=actual[k],e=c.expected[k];
            const bool equal=a==e||((a&0x7fffffffU)==0&&(e&0x7fffffffU)==0);
            if(!equal)std::cerr<<"case "<<c.id<<" field "<<k<<" expected "<<e<<" actual "<<a<<'\n';
            check(equal,"authentic original primitive bits");
        }
    }
}
void ground_clear() {
    {
        auto handling=stock_240sx_handling();
        for(auto& wheel:handling.wheels)wheel.radius=.5F;
        for(auto& strut:handling.struts)strut.droop_limit=-.25F;
        PlayerRigidBodyState body;body.axes[2]={{0,0,1}};
        AiPowertrainState state;
        std::array<AiWheelGround,4> ground{};
        for(auto& g:ground)g.normal={{0,0,1}};
        ground[0].height=-.75F;
        ground[1].height=std::nextafter(-.75F,-1.F);
        ground[2].normal[2]=.001F;
        ground[3].normal[2]=std::nextafter(.001F,0.F);
        ai_powertrain_struts(handling,body,ground,1.F/120.F,state);
        check(state.wheels[0].contact&&!state.wheels[1].contact,
              "475A9B retains support at exact droop, releases just below");
        check(state.wheels[2].contact&&!state.wheels[3].contact,
              "475A5A projects ground at exact minimum alignment");
    }
    for(const auto hub:{PlayerRigidVector{{0,0,.25F}},
                        PlayerRigidVector{{983.049011F,-243.833969F,59.559338F}},
                        PlayerRigidVector{{1,2,-7}}}) {
        AiWheelGround g;g.hub=hub;g.height=100;g.normal={{1,0,0}};
        ai_ground_clear(g);
        check(g.hub==hub&&bits(g.height)==bits(hub[2])&&g.normal==PlayerRigidVector{{0,0,-1}},
              "original440A40 clear height and normal, hub retained");
    }
    struct Transition {unsigned mask;std::array<std::uint32_t,20> expected;};
    const Transition sequence[]={
#include "ai_ground_clear_native.inc"
    };
    const auto h=stock_240sx_handling();PlayerRigidBodyState body;
    body.axes={{{{1,0,0}},{{0,1,0}},{{0,0,1}}}};
    AiPowertrainState state;
    for(const auto& c:sequence) {
        std::array<AiWheelGround,4> ground;
        for(unsigned k=0;k<4;++k) {
            ground[k].hub={{0,0,.25F}};
            if(c.mask&(1U<<k))ai_ground_clear(ground[k]);
        }
        ai_powertrain_struts(h,body,ground,1.F/120,state);
        unsigned at=0;
        for(unsigned k=0;k<4;++k) {
            const auto& w=state.wheels[k];
            for(const auto value:{bits(w.compression),bits(w.compression_rate),bits(w.load),
                                 std::uint32_t(w.contact),bits(w.excess)}) {
                const auto expected=c.expected[at++];
                check(value==expected||((value&0x7fffffffU)==0&&(expected&0x7fffffffU)==0),
                      "original475A00 hit-clear-recontact transition bits");
            }
            if(c.mask&(1U<<k))check(!w.contact&&w.load==0,"clear wheel has no support");
            else check(w.contact,"available ground restores contact");
        }
    }
}
class Flat final:public AiPhysicsGround {
public:
    bool fail=false;
    bool query(const VehicleHandling& h,const PlayerRigidBodyState& b,
               std::array<AiWheelGround,4>& out) const override {
        for(unsigned k=0;k<4;++k){out[k].hub=player_strut_attachment(h,k,b);out[k].normal={{0,0,1}};out[k].height=0;}
        return !fail;
    }
};
void collision_rest() {
    const auto h=stock_240sx_handling();AiPowertrainTables tables;Flat ground;
    struct Case {float speed;unsigned support,prior;float throttle;unsigned state;};
    // Original47AC00 selector3/EAX0 cases in contact_lifecycle_native.py.
    for(const auto c:std::array<Case,6>{{{0,4,0,0,2},{.4F,4,0,0,1},{1,4,0,0,0},
                                      {0,4,0,.5F,0},{0,0,0,0,0},{0,0,2,0,2}}}) {
        AiPhysicsState state;state.body.position={0,0,.5F};
        state.body.momentum={c.speed*h.mass,0,0};state.prepared=true;
        state.collision_context_valid=true;state.collision_context.support=c.support;
        state.collision_context.throttle=c.throttle;state.rest_state=c.prior;
        state.engine.throttle=c.throttle;for(auto& w:state.engine.wheels)w.spin=10;
        state.aero.enabled=false;const auto p=state.body.momentum;
        check(ai_physics_advance(h,tables,{},0,ground,state),"original zero-time AI rest completion");
        check(state.rest_state==c.state&&state.body.gravity==(c.state!=2),"original AI rest selection");
        check(state.body.momentum[0]==(c.state==2?0.F:c.state==1?p[0]*.95F:p[0]),
              "original AI rest momentum change");
        check(state.engine.wheels[0].spin==10,"selector3 rest leaves engine wheel spin intact");
        check(state.engine.brake==(c.state==2?1.F:0.F),"474CF5 applies the brake at full rest");
        check(state.ground_queries==(c.state?8U:4U),"rest mutation refreshes components exactly once");
    }
}
void bump_stop() {
    const auto h=stock_240sx_handling();AiPowertrainTables tables;Flat ground;
    for(float speed:{-3.F,3.F}) {
        AiPhysicsState state;state.body.position={0,0,.15F};
        state.body.momentum={0,0,speed*h.mass};state.collision_context_valid=true;
        state.aero.enabled=false;
        check(ai_physics_advance(h,tables,{},0,ground,state,true),"selector3 fully compressed struts");
        check(state.engine.wheels[0].excess>0.F,"bump-stop scenario has excess travel");
        if(speed<0.F) {
            check(state.body.momentum[2]>speed*h.mass,"476240 opposes inward motion");
            check(state.ground_queries==8,"474CE8 refreshes once after bump-stop impulses");
        } else {
            check(state.body.momentum[2]==speed*h.mass,"476240 leaves outward motion alone");
            check(state.ground_queries==4,"no bump-stop refresh without impulse");
        }
    }
}
void system() {
    const auto h=stock_240sx_handling();AiPhysicsState s;AiPowertrainTables tables;Flat ground;
    s.aero.enabled=false;s.body.position={{0,0,.5F}};s.body.alternate_yaw=true;
    s.body.momentum={{20.F*h.mass,0,0}};s.engine.rpm=2500;s.engine.throttle=.5F;s.engine.gear_request=2;
    for(auto& w:s.engine.wheels){w.spin=60;w.preload=3;w.contact=true;}
    for(unsigned k=0;k<8;++k){tables.up[k]=1000.F+800.F*float(k);tables.down[k]=500.F+500.F*float(k);}
    const auto original=s;
    ground.fail=true;check(!ai_physics_advance(h,tables,{},1.F/60,ground,s),"failed external ground refused");
    check(s.slices==0&&!s.prepared&&s.body.position==original.body.position&&s.engine.rpm==original.engine.rpm,
          "ground failure retains state");ground.fail=false;
    auto invalid=s;invalid.engine.gear=99;
    check(!ai_physics_advance(h,tables,{},1.F/60,ground,invalid),"invalid gear refused before table access");
    invalid=s;invalid.engine.rpm=std::numeric_limits<float>::quiet_NaN();
    check(!ai_physics_advance(h,tables,{},1.F/60,ground,invalid),"nonfinite curve operand refused");
    check(!ai_physics_advance(h,tables,{},-1,ground,s),"negative interval refused");
    for(unsigned frame=0;frame<24;++frame) {
        check(ai_physics_advance(h,tables,{},1.F/60,ground,s),"ordinary component system advances");
        // Pins from actual original two-component46AA40 list, full strut
        // transform, supplied flat external queries, aero/rest/collision off.
        if(frame==0||frame==23) {
            const std::array<std::uint32_t,73> expected=frame==0?
                std::array<std::uint32_t,73>{{0x3E613C66u,0x00000000u,0x3F135631u,0x41033DADu,0x2D8A3B05u,0x4130F0A0u,0xB30879F9u,0x3F9DE4C2u,0xB30879F9u,0x40D72628u,0x2D629B81u,0x41110861u,0xB3941A2Au,0x3F0C9528u,0xB1E2134Cu,0x00000000u,0x00000000u,0xC13F4BC7u,0x00000000u,0x00000000u,0x00000000u,0x3F7FFF40u,0xAE760F2Au,0xBB9CB0A7u,0x2E69F069u,0x3F800000u,0xB01E731Bu,0x3B9CB0A7u,0x301E60BFu,0x3F7FFF40u,0x00000000u,0x4233BFB8u,0xBDA81AF0u,0xC1068F00u,0x00000000u,0x00000000u,0x00000000u,0xC0C6FDDCu,0xB20088B5u,0x00000001u,0x00000000u,0x4233BFB8u,0xBDA81AF0u,0xC1068F00u,0x00000000u,0x00000000u,0x00000000u,0xC0C6FDDCu,0xB20088B5u,0x00000001u,0x00000000u,0x427F2199u,0xBD8BC3C8u,0xC11CF30Du,0x00000000u,0x00000000u,0x00000000u,0xC1545C13u,0x32132419u,0x00000001u,0x00000000u,0x427F2199u,0xBD8BC3C8u,0xC11CF30Du,0x00000000u,0x00000000u,0x00000000u,0xC1545C13u,0x32132419u,0x00000001u,0x00000000u,0x45FA0000u,0x00000002u}}:
                std::array<std::uint32_t,73>{{0x40330659u,0x2CADBB84u,0x40562344u,0x41033DADu,0x2D8A3B05u,0x40CF3844u,0xB305E291u,0x3F9AE53Eu,0xB305E291u,0x40D72628u,0x2D629B81u,0x40A9DA2Fu,0xB386971Cu,0x3F09E9CAu,0xB0F39CD8u,0x00000000u,0x00000000u,0xC13F4BC7u,0x00000000u,0x00000000u,0x00000000u,0x3F7A326Fu,0xB192B859u,0xBE58CB26u,0xB0A38AE4u,0x3F800001u,0xB2DC7076u,0x3E58CB26u,0x32D99B3Bu,0x3F7A326Fu,0x00000000u,0x4233BFB8u,0xBE051EB8u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0xC0C6FDDCu,0xB20088B5u,0x00000000u,0x00000000u,0x4233BFB8u,0xBE051EB8u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0xC0C6FDDCu,0xB20088B5u,0x00000000u,0x00000000u,0x4284705Cu,0xBE051EB8u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0xC1545C13u,0x32132419u,0x00000000u,0x00000000u,0x4284705Cu,0xBE051EB8u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0xC1545C13u,0x32132419u,0x00000000u,0x00000000u,0x45FA0000u,0x00000002u}};
            std::array<std::uint32_t,73> actual{};unsigned at=0;
            const auto add=[&](float x){actual[at++]=bits(x);};
            for(const auto& v:{s.body.position,s.body.momentum,s.body.angular_momentum,s.body.velocity,
                              s.body.angular_velocity,s.body.force,s.body.torque})for(float x:v)add(x);
            for(const auto& axis:s.body.axes)for(float x:axis)add(x);
            add(s.body.adaptive_damping);
            for(const auto& w:s.engine.wheels) {
                add(w.spin);add(w.compression);add(w.compression_rate);add(w.load);
                for(float x:w.feedback)add(x);
                for(float x:w.slip)add(x);
                actual[at++]=w.contact;actual[at++]=w.locked;
            }
            add(s.engine.rpm);actual[at++]=static_cast<std::uint32_t>(s.engine.gear);
            check(at==73,"coupled system schema");
            for(unsigned k=0;k<73;++k)check(actual[k]==expected[k]||
                ((actual[k]&0x7fffffffU)==0&&(expected[k]&0x7fffffffU)==0),"original coupled system bits");
        }
    }
    check(s.slices==48&&s.ground_queries==196,"source cadence and ground query count");
    // Actual original46ABD0/475A00 after a supplied external separation and
    // impulse. tools/ai_physics_vectors.py --external-contact; no contact
    // solver equivalence claimed. This catches constructor-zero reuse.
    s=original;
    for(unsigned frame=0;frame<24;++frame) {
        if(frame==1) {
            s.body.position[2]-=.277F;
            s.body.momentum[2]-=.3F*h.mass;s.prepared=false;
        }
        check(ai_physics_advance(h,tables,{},1.F/60,ground,s),"external contact continuation");
    }
    const std::array<std::uint32_t,6> contact_expected{{0x41011DD0u,0x31187681u,0x408CCABCu,0x41CA9D9Au,0x31F8A1B6u,0x412D2548u}};
    for(unsigned k=0;k<3;++k) {
        check(bits(s.body.position[k])==contact_expected[k],"original post-contact position");
        check(bits(s.body.momentum[k])==contact_expected[k+3],"original post-contact momentum");
    }
    check(s.component_interval==1.F/120,"contact retains completed slice interval");
    s.component_interval=-1;
    check(!ai_physics_advance(h,tables,{},1.F/60,ground,s),"invalid refresh interval refused");
    // No camera, telemetry encode, heap allocation or selector1 constraint loop.
}
}
int main(int argc,char** argv){
    if(argc>1) {
        for(int i=1;i<argc;++i) {
            std::ifstream f(argv[i],std::ios::binary);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
            VehicleHandling h;std::vector<VehicleHandlingIssue> issues;float rating=0,slip=0;
            if(!decode_vehicle_handling(bytes.data(),bytes.size(),h,issues)||!ai_setup_performance(h,rating,slip))return 2;
            std::cout<<bits(rating)<<' '<<bits(slip)<<'\n';
        }
        return 0;
    }
    // Original581B30/57F61D/474203, ai_setup_performance_vectors.py.
    const std::uint32_t expected[4][2]={{1129217790U,1049460009U},{1129408361U,1049498666U},
        {1129658641U,1049549436U},{1133918826U,1050709131U}};
    for(unsigned i=0;i<4;++i) {
        auto h=stock_240sx_handling();
        if(i==1)h.rear_drive_fraction=.5F;
        if(i==2)h.rear_drive_fraction=0;
        if(i==3)for(float& x:h.torque)x=float(double(x)*1.4);
        float rating=-1,slip=-1;
        check(ai_setup_performance(h,rating,slip),"selected setup performance");
        check(bits(rating)==expected[i][0]&&bits(slip)==expected[i][1],"original rating and low-slip bits");
    }
    float predicted_speed=-1;
    check(ai_setup_predicted_speed(stock_240sx_handling(),predicted_speed)&&
          bits(predicted_speed)==0x4280BE72U,"580240 original stock 240SX steering speed");
    auto invalid=stock_240sx_handling();invalid.mass=0;float rating=-1,slip=-2;
    check(!ai_setup_performance(invalid,rating,slip)&&rating==-1&&slip==-2,"performance refusal retains outputs");
    primitives();collision_outer();collision_rest();bump_stop();ground_clear();system();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
