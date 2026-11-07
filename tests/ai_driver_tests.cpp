#include "nfsu/ai_path.h"
#include <cstring>
#include <iostream>
#include <limits>
#include "drag_steering_native.inc"

namespace {
unsigned failures=0, checks=0;
void check(bool pass,const char* name) {
    ++checks;
    if(!pass){++failures;std::cerr<<"FAIL: "<<name<<'\n';}
}
std::uint32_t bits(float value) {
    std::uint32_t result;std::memcpy(&result,&value,sizeof(result));return result;
}
struct Output {
    std::uint16_t steer;
    std::uint32_t throttle,brake;
    std::uint32_t speed_error=0,integral=0,derivative=0;
};
struct Sequence {unsigned index;Output frames[16];};
const Sequence original[] = {
#include "ai_driver_native.inc"
};
const Sequence original_air[] = {
#include "ai_driver_air_native.inc"
};
struct PublicationCase {
    unsigned count;float points[20][3];std::uint32_t expected[20][3];
    std::uint32_t length,cursor,cursor_tick,prediction_tick;
};
const PublicationCase publication_cases[]={
#include "ai_publication_native.inc"
};
struct ReverseFrame {
    float speed,desired;std::array<bool,4> contact;bool enabled,reset;
    unsigned steer;std::uint32_t throttle,brake;int gear;bool ready;
    std::uint32_t target_x,target_y,speed_error;
};
struct ReverseSequence {unsigned category;float drive;std::array<float,4> slips;ReverseFrame frames[28];};
#include "ai_reverse_native.inc"
struct ShortPathFrame {
    std::uint32_t x,y;unsigned steer;std::uint32_t throttle,brake;int gear;bool ready;
};
struct ShortPathCase {
    unsigned heading,count,category;float speed;nfsu::AiDriverPoint position,velocity;
    ShortPathFrame frames[3];
};
#include "ai_short_path_native.inc"
bool same(const nfsu::AiDriverForwardState& a,const nfsu::AiDriverForwardState& b) {
    return a.steering.word==b.steering.word && bits(a.pedals.throttle)==bits(b.pedals.throttle) &&
        bits(a.pedals.brake)==bits(b.pedals.brake) && a.target.point==b.target.point;
}
}
int main() {
    using namespace nfsu;
    for(const auto& row:kNativeDragSteering) {
        AiDriverForwardInput in;AiDriverForwardState state;
        in.path.count=2;in.path.points[0]={10,float(row.side)*10,0};
        in.path.points[1]={100,float(row.side)*60,0};in.velocity={12,0,0};
        in.forward_speed=in.absolute_speed=in.desired_speed=12;in.category=3;
        in.contact={{true,true,false,false}};std::memcpy(&in.route_distance,&row.distance,4);
        check(ai_driver_drag_step(in,1.F/60,state)&&state.steering.word==row.word,
              "whole original drag driver actor-distance boundary");
    }
    for(const auto& row:kNativeOrdinarySteering) {
        AiDriverForwardInput in;AiDriverForwardState state;
        in.path.count=2;in.path.points[0]={10,float(row.side)*10,0};
        in.path.points[1]={100,float(row.side)*60,0};in.velocity={12,0,0};
        in.forward_speed=in.absolute_speed=in.desired_speed=12;in.category=3;
        in.contact={{true,true,false,false}};std::memcpy(&in.route_distance,&row.distance,4);
        check(ai_driver_forward_step(in,1.F/60,state)&&state.steering.word==row.word,
              "whole original ordinary driver actor-distance boundary");
    }
    for(const auto& c:publication_cases) {
        AiPlannedPath path;path.path.count=static_cast<std::uint8_t>(c.count);
        path.length=77;path.cursor=2;path.cursor_tick=12345;path.prediction_tick=11111;
        for(unsigned i=0;i<c.count;++i){
            for(unsigned k=0;k<3;++k)path.path.points[i][k]=c.points[i][k];
            path.route[i]=3;path.point[i]=static_cast<std::uint16_t>(100+i);path.category[i]=2;
        }
        check(ai_path_publish(path),"original driver path publication accepted");
        check(bits(path.length)==c.length&&path.cursor==c.cursor&&path.cursor_tick==c.cursor_tick&&path.prediction_tick==c.prediction_tick,
              "original path length and publication cache epochs");
        for(unsigned i=0;i<c.count;++i) {
            for(unsigned k=0;k<3;++k)check(bits(path.path.points[i][k])==c.expected[i][k],"original equal arc-length publication point bits");
            check(path.route[i]==3&&path.point[i]==100+i&&path.category[i]==2,"publication retains original route metadata");
        }
    }
    {
        AiPlannedPath path;path.path.count=3;path.path.points[0]={1,2,3};path.path.points[1]={4,5,6};path.path.points[2]={7,8,9};
        path.length=77;path.cursor_tick=12345;path.prediction_tick=11111;
        auto invalid=path;invalid.path.points[2][1]=std::numeric_limits<float>::quiet_NaN();
        check(!ai_path_publish(invalid)&&invalid.path.points[0]==path.path.points[0]&&
              invalid.length==path.length&&invalid.cursor_tick==path.cursor_tick&&invalid.prediction_tick==path.prediction_tick,
              "invalid path publication refuses before modifying geometry or cache epochs");
    }
    for(unsigned samples:{4U,5U})for(bool reset:{false,true}) {
        AiDriverHistory original_history(static_cast<std::uint8_t>(samples));
        for(unsigned i=0;i<17;++i)original_history.update(float(int(i%7)-3)*.13F,.005F+float(i%3)*.002F);
        if(reset)original_history.reset_error_samples(.35F);
        const auto saved=original_history.snapshot();
        AiDriverHistory restored;
        check(restored.restore(saved),"captured retained history restores");
        auto compare_error=[&] {
            const auto a=original_history.error(),b=restored.error();
            return bits(a.current)==bits(b.current)&&bits(a.integral)==bits(b.integral)&&bits(a.derivative)==bits(b.derivative);
        };
        check(compare_error(),"restore retains reset caches without reconstructing samples");
        for(unsigned bad=0;bad<7;++bad) {
            auto invalid=saved;
            if(bad==0)invalid.samples=0;
            if(bad==1)invalid.samples=6;
            if(bad==2)invalid.time.next=invalid.samples;
            if(bad==3)invalid.integral.count=invalid.samples+1;
            if(bad==4)invalid.derivative.average=std::numeric_limits<float>::infinity();
            if(bad==5)invalid.current=std::numeric_limits<float>::quiet_NaN();
            if(bad==6)invalid.time.values[2]=std::numeric_limits<float>::quiet_NaN();
            check(!restored.restore(invalid)&&compare_error(),"invalid snapshot refuses without partial mutation");
        }
        for(unsigned i=0;i<13;++i) {
            const float value=float(int(i%5)-2)*.017F,dt=.003F+float(i%4)*.001F;
            original_history.update(value,dt,i==3);restored.update(value,dt,i==3);
            check(compare_error(),"restored cursor and rings preserve subsequent driver history");
        }
    }
    for(bool drag:{false,true}) for(bool air:{false,true}) for(const auto& sequence:air?original_air:original) {
        const unsigned id=sequence.index;
        AiDriverForwardState state;
        AiDriverForwardInput in;
        in.path.count=2;
        const float direction=float(int(id%3)-1);
        in.path.points[0]={10,direction*10,0};in.path.points[1]={100,direction*60,0};
        in.absolute_speed=in.forward_speed=float(id%9)*10;
        in.velocity={in.absolute_speed,0,0};
        in.category=int(id%4);in.heading=std::uint16_t(id*83);
        in.contact={true,true,id%2!=0,id%2!=0};
        in.rear_drive_fraction=float(id%3)*.5F;
        for(unsigned step=0;step<16;++step) {
            if(air)in.contact=step>=4&&step<8?std::array<bool,4>{false,false,true,true}:
                step>=8&&step<12?std::array<bool,4>{false,false,false,false}:
                std::array<bool,4>{true,true,id%2!=0,id%2!=0};
            constexpr float desired[]={0,1,2,4.99F,5,10,30,90};
            in.desired_speed=desired[step/2];in.enabled=step%7!=6;
            in.race_seconds=float(double(39999+step*67)*.00025);
            in.wheel_slip={float(double(int(step%5)-2)*.3),0,float(double(int(id%5)-2)*.2),5};
            check(drag?ai_driver_drag_step(in,1.F/60,state):ai_driver_forward_step(in,1.F/60,state),"original context accepted");
            const auto& expected=sequence.frames[step];
            check(state.steering.word==expected.steer &&
                bits(state.pedals.throttle)==expected.throttle && bits(state.pedals.brake)==expected.brake,
                "original retained steering/throttle/brake bits");
            if(air) {
                const auto history=state.speed.error();
                check(bits(history.current)==expected.speed_error &&
                    bits(history.integral)==expected.integral &&
                    bits(history.derivative)==expected.derivative,
                    "original support loss clears samples and retains history cache");
            }
        }
        // Refused input must not corrupt histories or future output.
        auto control=state;
        auto invalid=in;invalid.path.count=0;
        check(!ai_driver_forward_step(invalid,1.F/60,state),"empty action path refused");
        invalid=in;invalid.desired_speed=-1;
        check(!ai_driver_forward_step(invalid,1.F/60,state),"reverse branch not silently emulated");
        invalid=in;invalid.wheel_slip[0]=std::numeric_limits<float>::quiet_NaN();
        check(!ai_driver_forward_step(invalid,1.F/60,state),"invalid slip refused");
        check(!ai_driver_forward_step(in,0,state),"zero time refused");
        for(unsigned step=0;step<12;++step) {
            in.desired_speed=float(step*3);in.enabled=true;
            check(ai_driver_forward_step(in,1.F/60,state) && ai_driver_forward_step(in,1.F/60,control)
                && same(state,control),"refusal preserves future driver state");
        }
    }
    check(!ai_driver_slip_limit(false,2,10,false,0),"ten second gate excludes equality");
    check(ai_driver_slip_limit(false,2,10.001F,false,0),"ten second gate opens after boundary");
    check(!ai_driver_slip_limit(true,2,20,false,0),"held driver excludes slip limit");
    check(!ai_driver_slip_limit(false,3,20,false,0),"role3 excludes slip limit");
    check(!ai_driver_slip_limit(false,2,20,true,1),"finished racer excludes slip limit");
    for(const auto& sequence:reverse_sequences) {
        AiDriverForwardState state;AiDriverForwardInput in;
        const float direction=sequence.category%2?1.F:-1.F;
        in.path.count=2;in.path.points[0]={10,direction*10,0};in.path.points[1]={100,direction*60,0};
        in.category=static_cast<int>(sequence.category);in.heading=static_cast<std::uint16_t>(sequence.category*83);
        in.rear_drive_fraction=sequence.drive;in.wheel_slip=sequence.slips;
        unsigned step=0;
        for(const auto& f:sequence.frames) {
            in.forward_speed=f.speed;in.absolute_speed=std::abs(f.speed);in.velocity={f.speed,0,0};
            in.desired_speed=f.desired;in.contact=f.contact;in.enabled=f.enabled;
            in.race_seconds=float(double(39999+step*67)*.00025);
            const auto before=failures;
            if(f.reset)check(ai_driver_recovery_reset(in,state),"original recovery reset executes");
            check(ai_driver_ordinary_step(in,1.F/60,state),"signed driver executes");
            check(state.steering.word==f.steer,"signed original steering word");
            check(bits(state.pedals.throttle)==f.throttle&&bits(state.pedals.brake)==f.brake,"signed original pedal bits");
            check(state.gear_request==f.gear&&state.direction_ready==f.ready,"original direction latch and gear request");
            check(bits(state.target.point[0])==f.target_x&&bits(state.target.point[1])==f.target_y,"signed original path target");
            check(bits(state.speed.error().current)==f.speed_error,"signed original speed history clear");
            if(before!=failures)std::cerr<<"signed category="<<sequence.category<<" frame="<<step<<" steer="<<state.steering.word<<'/'<<f.steer
                <<" gear="<<state.gear_request<<'/'<<f.gear<<" ready="<<state.direction_ready<<'/'<<f.ready<<'\n';
            ++step;
        }
    }
    for(const auto& c:short_path_cases) {
        AiDriverForwardState state;AiDriverForwardInput in;
        in.path.count=static_cast<std::uint8_t>(c.count);in.path.points[0]={999,-888,7};
        in.heading=static_cast<std::uint16_t>(c.heading);in.category=static_cast<int>(c.category);
        in.forward_speed=c.speed;in.absolute_speed=std::abs(c.speed);in.desired_speed=15;
        in.position=c.position;in.velocity=c.velocity;in.contact={true,true,false,false};
        unsigned step=0;
        for(const auto& f:c.frames) {
            const auto prior=failures;
            in.race_seconds=float(double(39999+step*67)*.00025);
            check(ai_driver_ordinary_step(in,1.F/60,state),"short original path accepted");
            check(bits(state.target.point[0])==f.x&&bits(state.target.point[1])==f.y,"short path original target XY");
            check(state.steering.word==f.steer,"short path original retained steering");
            check(bits(state.pedals.throttle)==f.throttle&&bits(state.pedals.brake)==f.brake,"short path original pedals");
            check(state.gear_request==f.gear&&state.direction_ready==f.ready,"short path original direction command");
            if(failures!=prior)std::cerr<<"short heading="<<c.heading<<" count="<<c.count<<" speed="<<c.speed<<" frame="<<step
                <<" target="<<bits(state.target.point[0])<<'/'<<f.x<<','<<bits(state.target.point[1])<<'/'<<f.y<<'\n';
            ++step;
        }
        auto expected=state;auto invalid=in;invalid.path.count=26;
        check(!ai_driver_ordinary_step(invalid,1.F/60,state),"oversized ordinary path refused");
        check(ai_driver_ordinary_step(in,1.F/60,state)&&ai_driver_ordinary_step(in,1.F/60,expected)&&same(state,expected),
            "short path refusal preserves continuation");
    }
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
