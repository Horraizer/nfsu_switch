#include "nfsu/ai_recovery.h"
#include <iostream>
#include <cstring>
#include <limits>
using namespace nfsu;
namespace {
struct RecoveryCase {
    AiRecoveryMotion motion;std::array<float,2> local;std::uint32_t created;
    float minimum_x,maximum_x;bool contact,supported,hold;int contact_class,road_class;bool request;
};
struct CooldownCase {std::uint32_t tick,last;bool available;};
struct LifetimeCase {unsigned snapshot;AiRecoveryLifetime lifetime;unsigned status;};
#include "ai_recovery_native.inc"
struct RecoveryRoad {float values[18];unsigned route,index;float distance,offset[7],lower[7],upper[7];};
struct RecoveryPathCase {
    float ahead[18];unsigned selected[2],category;float clearance,actor[10];int selector;
    unsigned count;RecoveryRoad roads[64];unsigned path_count;std::uint32_t points[20][5],length,speed;unsigned flags[3];
};
#include "ai_recovery_path_native.inc"
#include "recovery_control_native.inc"
struct ContactLocalCase {
    bool invalidate;std::array<float,2> contact,position;AiDriverPoint movement;
    float speed;unsigned heading;std::uint32_t local[2],rows[3][3];
};
#include "ai_contact_local_native.inc"
std::uint32_t bits(float v){std::uint32_t r;std::memcpy(&r,&v,4);return r;}
float real(std::uint32_t word){float v;std::memcpy(&v,&word,4);return v;}
RoutePoint point(const float* p) {
    RoutePoint r;r.position[0]=p[0];r.position[1]=p[1];r.height=p[2];
    r.forward[0]=p[3];r.forward[1]=p[4];r.unknown_2c=p[5];r.unknown_30=p[6];
    for(unsigned j=0;j<5;++j)r.driving_offset[j]=p[7+j];
    for(unsigned j=0;j<6;++j)r.lane_offset[j]=p[12+j];
    return r;
}
unsigned failures=0;
void check(bool ok,const char* what,unsigned index) {
    if(!ok){++failures;std::cerr<<"FAIL: case "<<index<<' '<<what<<'\n';}
}
}
int main() {
    RoutePoint road;road.forward[1]=1;road.unknown_24=5;road.unknown_28=5;
    unsigned index=0;
    AiRecoveryHistory history;unsigned trace=~0U;
    for(const auto& row:kNativeRecoveryHistory) {
        if(trace!=row[0]){history={};trace=row[0];}
        for(unsigned step=0;step<row[1];++step)
            check(ai_recovery_history_step(real(row[2]),real(row[3]),real(row[4]),history),"native recovery history input",index);
        check(history.count==row[5]&&history.cursor==row[6]&&unsigned(history.timer)==row[7]&&
              bits(history.sum)==row[8]&&bits(history.average)==row[9],"native recovery timer/ring fields",index);
        for(unsigned k=0;k<5;++k)check(bits(history.samples[k])==row[10+k],"native recovery ring samples",index);
        ++index;
    }
    for(const auto& row:kNativeRecoveryHold) {
        RoutePoint selected;selected.forward[0]=1;selected.unknown_24=selected.unknown_28=100;
        AiRecoveryHistory sample;sample.count=row[1];sample.average=real(row[2]);
        check(bits(ai_recovery_hold(&selected,{0,-(100.F+real(row[0]))},sample))==row[3],"native recovery hold duration",index++);
    }
    for(const auto& row:kNativeRecoveryRate) {
        float rate=real(row[3]);
        check(ai_recovery_progress_rate(real(row[0]),real(row[1]),real(row[2]),rate)&&bits(rate)==row[4],
              "native filtered fractional race speed",index++);
    }
    AiRecoveryContactBasis basis;
    for(const auto& c:contact_local_cases) {
        if(c.invalidate)basis.valid=false;
        std::array<float,2> local{};
        check(ai_recovery_contact_local(c.contact,c.position,c.movement,c.speed,
            static_cast<std::uint16_t>(c.heading),basis,local),"original contact projection executes",index);
        for(unsigned k=0;k<2;++k) {
            if(bits(local[k])!=c.local[k])std::cerr<<"local axis="<<k<<" actual="<<bits(local[k])<<" expected="<<c.local[k]<<'\n';
            check(bits(local[k])==c.local[k],"original contact projection bits",index);
        }
        for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)
            check(bits(basis.rows[j][k])==c.rows[j][k],"original cached axes bits",index);
        ++index;
    }
    {
        auto prior=basis;std::array<float,2> output={123,456};
        const float bad=std::numeric_limits<float>::quiet_NaN();
        check(!ai_recovery_contact_local({bad,0},{0,0},{0,0,0},0,0,basis,output)&&
            basis.rows==prior.rows&&basis.valid==prior.valid&&output[0]==123&&output[1]==456,
            "bad contact preserves cache and output",index);
        basis.valid=false;prior=basis;
        check(!ai_recovery_contact_local({1,2},{0,0},{bad,0,0},0,0,basis,output)&&
            basis.rows==prior.rows&&!basis.valid&&output[0]==123&&output[1]==456,
            "bad uncached motion preserves cache and output",index);
    }
    for(const auto& c:recovery_cases) {
        int kind=-1;
        check(ai_recovery_road_class(road,c.motion,true,kind)&&kind==c.road_class,"native road/heading classification",index);
        if(c.contact)check(ai_recovery_contact_class(c.local,c.minimum_x,c.maximum_x)==c.contact_class,"native contact classification",index);
        const AiRecoveryContact contact{c.local,c.created};
        AiRecoveryInput in;in.motion=c.motion;in.road=&road;in.tick=500000;in.minimum_x=c.minimum_x;in.maximum_x=c.maximum_x;
        in.contacts=&contact;in.contact_count=c.contact?1:0;in.race_time_hold=c.hold;in.supported=c.supported;
        AiRecoveryRequest request;
        check(ai_recovery_request(in,request)&&request.requested==c.request&&request.clear_road==c.request,"native complete recovery query",index);
        ++index;
    }
    for(const auto& c:cooldown_cases)check(ai_recovery_available(c.tick,c.last)==c.available,"native cooldown",index++);
    {
        const std::uint32_t start=400000;
        check(ai_recovery_race_hold(start+47999,start,100,1003,false)&&
             !ai_recovery_race_hold(start+48000,start,100,1003,false)&&
              ai_recovery_race_hold(start+45999,start,101,1003,false)&&
             !ai_recovery_race_hold(start+46000,start,101,1003,false)&&
             !ai_recovery_race_hold(start+44000,start,102,1003,false)&&
             !ai_recovery_race_hold(start,start,100,1099,false)&&
             !ai_recovery_race_hold(start,start,100,1003,true),"native race-clock recovery gate",index++);
        Route route;route.points.resize(5);
        check(&ai_recovery_lookahead(route,0)==&route.points[3]&&
              &ai_recovery_lookahead(route,2)==&route.points[4]&&
              &ai_recovery_lookahead(route,4)==&route.points[4],"recovery lookahead stays inside the raw road record",index++);
    }
    for(const auto& c:lifetime_cases) {
        const auto& s=recovery_cases[c.snapshot];
        const AiRecoveryContact contact{s.local,s.created};
        AiRecoveryInput in;in.motion=s.motion;in.road=&road;in.tick=500000;in.minimum_x=s.minimum_x;in.maximum_x=s.maximum_x;
        in.contacts=&contact;in.contact_count=s.contact?1:0;in.supported=s.supported;
        RoutePoint ahead;ahead.position[1]=30;ahead.forward[1]=1;
        AiRecoveryExit exit;
        check(ai_recovery_finish(c.lifetime,in,ahead,0,exit)&&exit.status==c.status,"native constructor/complete lifecycle",index);
        check(exit.clear_road==(c.status!=0)&&exit.reset_driver==exit.clear_road&&exit.clear_actor_recovery==exit.clear_road,"terminal cleanup commands",index++);
    }
    for(const auto& c:recovery_paths) {
        AiPathRoadPoint roads[64],selected;
        selected.point.route_index=static_cast<std::uint8_t>(c.selected[0]);selected.point_index=static_cast<std::uint16_t>(c.selected[1]);
        for(unsigned j=0;j<c.count;++j) {
            const auto& s=c.roads[j];auto& r=roads[j];r.point=point(s.values);
            r.point.route_index=static_cast<std::uint8_t>(s.route);r.point_index=static_cast<std::uint16_t>(s.index);r.distance=s.distance;
            for(unsigned k=0;k<7;++k){r.offset[k]=s.offset[k];r.lower[k]=s.lower[k];r.upper[k]=s.upper[k];}
        }
        AiPathExtension in;in.category=static_cast<int>(c.category);in.clearance=c.clearance;in.absolute_speed=c.actor[9];
        for(unsigned j=0;j<3;++j){in.actor_position[j]=c.actor[j];in.actor_velocity[j]=c.actor[3+j];in.actor_forward[j]=c.actor[6+j];}
        AiRecoveryPlan out;
        check(ai_recovery_plan(point(c.ahead),selected,roads,c.count,in,c.selector,out),"native recovery path executes",index);
        check(out.path.path.count==c.path_count&&bits(out.path.length)==c.length,"native path count/length",index);
        check(bits(out.speed)==c.speed&&out.enabled==bool(c.flags[0])&&out.route_speed==bool(c.flags[1])&&out.catchup==bool(c.flags[2]),"negative speed and policy flags",index);
        for(unsigned j=0;j<c.path_count&&j<out.path.path.count;++j) {
            for(unsigned k=0;k<3;++k) {
                const auto actual=bits(out.path.path.points[j][k]);
                if(actual!=c.points[j][k])std::cerr<<"point="<<j<<" axis="<<k<<" actual="<<std::hex<<actual<<" expected="<<c.points[j][k]<<std::dec<<'\n';
                check(actual==c.points[j][k],"native path coordinate bits",index);
            }
            check(out.path.route[j]==c.points[j][3]&&out.path.point[j]==c.points[j][4]&&out.path.category[j]==c.category,"native selected metadata",index);
        }
        ++index;
    }
    check(ai_recovery_contact_class({-.95F,1},-1,1)==2&&
          ai_recovery_contact_class({.95F,1},-1,1)==3&&
          ai_recovery_contact_class({0,1},-1,1)==0,
          "44ECD0 front-contact strip excludes both edges",index++);
    // The native old-contact case must be reevaluated after an earlier young
    // contact; a prior side/rear contact cannot hide a front obstruction.
    AiRecoveryContact contacts[2]={{{3,1},0},{{0,1},0}};
    AiRecoveryInput in;in.road=&road;in.motion.target={0,100};in.motion.body_direction={0,1};in.motion.heading=16384;
    in.tick=500000;in.minimum_x=-1;in.maximum_x=1;in.contacts=contacts;in.contact_count=2;
    AiRecoveryRequest result;
    check(ai_recovery_request(in,result)&&result.requested,"later eligible contact considered",index);
    in.selected_road=false;
    check(ai_recovery_request(in,result)&&!result.requested&&!result.clear_road,"missing road selection suppresses request",index);
    AiRecoveryPlan prior;prior.speed=123;prior.path.path.count=7;
    AiPathExtension bad;bad.category=5;AiPathRoadPoint selected;
    check(!ai_recovery_plan(road,selected,nullptr,0,bad,3,prior)&&prior.speed==123&&prior.path.path.count==7,"unsupported plan preserves output",index);
    AiRecoveryExit prior_exit;prior_exit.status=99;
    check(!ai_recovery_finish({},in,road,5,prior_exit)&&prior_exit.status==99,"unsupported lifecycle preserves output",index);
    std::cout<<"Original recovery: "<<index<<" cases; failures="<<failures<<'\n';return failures?1:0;
}
