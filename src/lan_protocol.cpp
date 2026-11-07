#include "nfsu/lan_protocol.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace nfsu {
namespace {
struct Writer {
    std::vector<std::uint8_t> b;
    void u8(unsigned v){b.push_back(static_cast<std::uint8_t>(v));}
    void u32(std::uint32_t v){for(unsigned i=0;i<4;++i)u8(v>>(8*i));}
    void i32(std::int32_t v){u32(static_cast<std::uint32_t>(v));}
    void real(float v){std::uint32_t bits;std::memcpy(&bits,&v,4);u32(bits);}
};
struct Reader {
    const std::uint8_t* p;std::size_t size,at=0;bool good=true;
    unsigned u8(){if(at>=size){good=false;return 0;}return p[at++];}
    std::uint32_t u32(){std::uint32_t v=0;
    for(unsigned i=0;i<4;++i)v|=std::uint32_t(u8())<<(8*i);
        return v;}
    std::int32_t i32(){auto v=u32();std::int32_t s;std::memcpy(&s,&v,4);return s;}
    float real(){auto bits=u32();float f;std::memcpy(&f,&bits,4);return f;}
};
bool valid_input(const GamepadState& p){
    if(p.buttons&~kGamepadButtonMask)return false;
    for(auto a:p.axes)if(a<kAxisMin||a>kAxisMax)return false;
    return true;
}
bool valid_frame(const LanRaceFrame& f){
    if(f.stage>4||f.outcome>3||f.countdown>4||f.step>8000000||
       f.world_ticks<0||f.world_ticks>536870912||f.race_ticks<0||f.race_ticks>536870912||
       f.start_ticks<0||f.start_ticks>536870912||f.countdown_start<0||f.countdown_start>536870912)return false;
    for(const auto& c:f.cars){
        float q=0;
        for(float x:c.position)if(!std::isfinite(x)||std::abs(x)>1000000.F)return false;
        for(float x:c.velocity)if(!std::isfinite(x)||std::abs(x)>10000.F)return false;
        for(float x:c.angular_velocity)if(!std::isfinite(x)||std::abs(x)>10000.F)return false;
        for(float x:c.orientation){if(!std::isfinite(x))return false;q+=x*x;}
        if(q<.8F||q>1.2F)return false;
        for(float x:c.compression)if(!std::isfinite(x)||std::abs(x)>10.F)return false;
        for(float x:c.wheel_phase)if(!std::isfinite(x)||std::abs(x)>1e12F)return false;
        if(!std::isfinite(c.steering)||std::abs(c.steering)>4.F||!std::isfinite(c.rpm)||c.rpm<0||c.rpm>100000||
           !std::isfinite(c.turbo)||std::abs(c.turbo)>1000.F||
           !std::isfinite(c.progress)||std::abs(c.progress)>100||!std::isfinite(c.parameter)||std::abs(c.parameter)>100000||!std::isfinite(c.throttle)||c.throttle<0||c.throttle>1||!std::isfinite(c.drive_load)||std::abs(c.drive_load)>100000||c.gear<0||c.gear>10||
           c.lap< -1||c.lap>20||c.highest_lap<0||c.highest_lap>20||c.checkpoint< -1||c.checkpoint>1000||
           c.highest_checkpoint< -1||c.highest_checkpoint>1000||c.place>2||c.status>8||
           c.lap_start<0||c.lap_start>536870912||c.best_lap<0||c.best_lap>536870912||
           c.finish_ticks<0||c.finish_ticks>536870912||c.nitrous_capacity>536870912||
           c.nitrous_remaining>c.nitrous_capacity)return false;
    }
    return true;
}
void write_car(Writer& w,const LanCarFrame& c){
    for(auto x:c.position)w.real(x);
    for(auto x:c.velocity)w.real(x);
    for(auto x:c.angular_velocity)w.real(x);
    for(auto x:c.orientation)w.real(x);
    for(auto x:c.compression)w.real(x);
    for(auto x:c.wheel_phase)w.real(x);
    w.real(c.steering);w.real(c.rpm);w.real(c.progress);w.real(c.parameter);w.real(c.throttle);w.real(c.drive_load);w.real(c.turbo);
    for(auto x:{c.gear,c.lap,c.highest_lap,c.checkpoint,c.highest_checkpoint,c.lap_start,c.best_lap,c.finish_ticks})w.i32(x);
    w.u32(c.nitrous_capacity);w.u32(c.nitrous_remaining);
    w.u8(c.place);w.u8(c.status);w.u8(c.braking);w.u8(c.finished);w.u8(c.nitrous_active);
}
void read_car(Reader& r,LanCarFrame& c){
    for(auto& x:c.position)x=r.real();
    for(auto& x:c.velocity)x=r.real();
    for(auto& x:c.angular_velocity)x=r.real();
    for(auto& x:c.orientation)x=r.real();
    for(auto& x:c.compression)x=r.real();
    for(auto& x:c.wheel_phase)x=r.real();
    c.steering=r.real();c.rpm=r.real();c.progress=r.real();c.parameter=r.real();c.throttle=r.real();c.drive_load=r.real();c.turbo=r.real();
    c.gear=r.i32();c.lap=r.i32();c.highest_lap=r.i32();c.checkpoint=r.i32();c.highest_checkpoint=r.i32();
    c.lap_start=r.i32();c.best_lap=r.i32();c.finish_ticks=r.i32();
    c.nitrous_capacity=r.u32();c.nitrous_remaining=r.u32();
    c.place=static_cast<std::uint8_t>(r.u8());c.status=static_cast<std::uint8_t>(r.u8());
    const auto b=r.u8(),f=r.u8(),nitrous=r.u8();if(b>1||f>1||nitrous>1)r.good=false;
    c.braking=b!=0;c.finished=f!=0;c.nitrous_active=nitrous!=0;
}
void write_ride(Writer& w,const LanRide& ride){
    w.u32(ride.car);
    for(auto part:ride.installed)w.u32(part);
}
void read_ride(Reader& r,LanRide& ride){
    ride.car=r.u32();
    for(auto& part:ride.installed)part=r.u32();
}
}
bool lan_newer(std::uint32_t a,std::uint32_t b) noexcept {return a!=b&&(a-b)<0x80000000U;}
bool lan_encode(const LanMessage& m,std::vector<std::uint8_t>& out,std::string& error){
    if(unsigned(m.kind)<1||unsigned(m.kind)>6||m.config!=kLanConfig||
       (m.kind==LanMessageKind::welcome&&!valid_lan_options(m.race))||
       (m.kind==LanMessageKind::input&&!valid_input(m.input))||
       (m.kind==LanMessageKind::snapshot&&!valid_frame(m.frame))){error="invalid LAN message";return false;}
    Writer w;
    for(auto x:{'N','F','S','L'})w.u8(unsigned(x));
    w.u8(1);w.u8(unsigned(m.kind));
    w.u32(m.session);w.u32(m.sequence);w.u32(m.ack);w.u32(m.config);
    if(m.kind==LanMessageKind::hello){w.u8(m.guest_ready);if(m.guest_ready)write_ride(w,m.guest_ride);}
    if(m.kind==LanMessageKind::welcome){
        w.u8(m.race.course);w.u8(m.race.laps);w.u8(m.race.tier);w.u8(m.launch);
        for(const auto& ride:m.race.rides)write_ride(w,ride);
    }
    if(m.kind==LanMessageKind::input){w.u8(m.input.connected);w.u32(m.input.buttons);
    for(auto x:m.input.axes)w.i32(x);}
    if(m.kind==LanMessageKind::snapshot){
        const auto& f=m.frame;w.u32(f.step);w.i32(f.world_ticks);w.i32(f.race_ticks);w.i32(f.start_ticks);w.i32(f.countdown_start);
        w.u8(f.stage);w.u8(f.outcome);w.u8(f.countdown);w.u8(f.go);
    for(const auto& c:f.cars)write_car(w,c);
    }
    if(w.b.size()>kLanDatagramLimit){error="LAN datagram too large";return false;}
    out=std::move(w.b);error.clear();return true;
}
bool lan_decode(const std::uint8_t* bytes,std::size_t size,LanMessage& out,std::string& error){
    if(!bytes||size<22||size>kLanDatagramLimit){error="LAN datagram extent";return false;}
    Reader r{bytes,size};
    for(auto x:{'N','F','S','L'})if(r.u8()!=unsigned(x)){error="LAN magic mismatch";return false;}
    if(r.u8()!=1){error="LAN version mismatch";return false;}
    LanMessage m;m.kind=static_cast<LanMessageKind>(r.u8());m.session=r.u32();m.sequence=r.u32();m.ack=r.u32();m.config=r.u32();
    if(unsigned(m.kind)<1||unsigned(m.kind)>6||m.config!=kLanConfig){error="LAN room configuration mismatch";return false;}
    if(m.kind==LanMessageKind::hello){
        const auto ready=r.u8();if(ready>1)r.good=false;m.guest_ready=ready!=0;
        if(m.guest_ready)read_ride(r,m.guest_ride);
    }
    if(m.kind==LanMessageKind::welcome){
        m.race.course=static_cast<std::uint8_t>(r.u8());m.race.laps=static_cast<std::uint8_t>(r.u8());
        m.race.tier=static_cast<std::uint8_t>(r.u8());const auto launch=r.u8();
        if(launch>1||!valid_lan_options(m.race))r.good=false;
        m.launch=launch!=0;
        for(auto& ride:m.race.rides)read_ride(r,ride);
    }
    if(m.kind==LanMessageKind::input){auto c=r.u8();if(c>1)r.good=false;m.input.connected=c!=0;m.input.buttons=r.u32();
    for(auto& a:m.input.axes)a=r.i32();}
    if(m.kind==LanMessageKind::snapshot){auto& f=m.frame;f.step=r.u32();f.world_ticks=r.i32();f.race_ticks=r.i32();f.start_ticks=r.i32();f.countdown_start=r.i32();
        f.stage=static_cast<std::uint8_t>(r.u8());f.outcome=static_cast<std::uint8_t>(r.u8());f.countdown=static_cast<std::uint8_t>(r.u8());
        auto go=r.u8();if(go>1)r.good=false;f.go=go!=0;
    for(auto& c:f.cars)read_car(r,c);
    }
    if(!r.good||r.at!=size||(m.kind==LanMessageKind::input&&!valid_input(m.input))||
       (m.kind==LanMessageKind::snapshot&&!valid_frame(m.frame))){error="invalid LAN payload";return false;}
    out=m;error.clear();return true;
}
}
