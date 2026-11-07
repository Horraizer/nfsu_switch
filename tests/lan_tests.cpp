#include "nfsu/lan_link.h"
#include "nfsu/lan_race.h"
#include <iostream>
#include <cmath>
#include <limits>
#include <chrono>
#include <thread>

using namespace nfsu;
namespace {
unsigned failures=0,checks=0;
void check(bool condition,const char* name){++checks;if(!condition){++failures;std::cout<<"FAIL "<<name<<'\n';}}
GamepadFrame pad(bool gas,int steer=0){GamepadFrame f;f.pads[0].connected=true;f.pads[0].buttons=gas?gamepad_button_bit(GamepadButton::trigger_right):0;f.pads[0].axes[0]=steer;return f;}
}
int main(){
    std::string error;std::vector<std::uint8_t> bytes;
    for(unsigned kind=1;kind<=6;++kind){
        LanMessage original;original.kind=static_cast<LanMessageKind>(kind);original.session=0x12345678;original.sequence=0xabcdef01;
        original.race={3,5,1};original.launch=true;original.guest_ready=true;
        original.race.rides[0].car=0x1234;original.race.rides[0].installed[119]=0x4567;
        original.guest_ride.car=0xabcd;original.guest_ride.installed[17]=0x89ab;
        original.input=pad(true,-32768).pads[0];original.frame.step=300;original.frame.cars[1].position[1]=42;
        check(lan_encode(original,bytes,error),"all message kinds encode");LanMessage decoded;
        check(lan_decode(bytes.data(),bytes.size(),decoded,error)&&decoded.kind==original.kind&&decoded.session==original.session&&decoded.sequence==original.sequence,"wire header round trip");
        if(kind==4)check(decoded.input.axes[0]==-32768&&decoded.input.buttons==original.input.buttons,"signed input round trip");
        if(kind==1)check(decoded.guest_ready&&decoded.guest_ride==original.guest_ride,"guest ride travels independently");
        if(kind==2)check(decoded.race==original.race&&decoded.launch,"room setup and launch round trip");
        if(kind==5)check(decoded.frame.step==300&&decoded.frame.cars[1].position[1]==42,"snapshot round trip");
        for(std::size_t cut=0;cut<bytes.size();++cut)check(!lan_decode(bytes.data(),cut,decoded,error),"every truncated prefix rejected");
        auto extra=bytes;extra.push_back(0);check(!lan_decode(extra.data(),extra.size(),decoded,error),"trailing payload rejected");
    }
    LanMessage invalid;invalid.kind=LanMessageKind::snapshot;invalid.frame.cars[0].rpm=std::numeric_limits<float>::quiet_NaN();
    check(!lan_encode(invalid,bytes,error),"NaN snapshot rejected");
    invalid={};invalid.kind=LanMessageKind::input;invalid.input.axes[1]=32768;
    check(!lan_encode(invalid,bytes,error),"oversized axis rejected");
    invalid.input.axes[1]=0;invalid.input.buttons=1U<<20;check(!lan_encode(invalid,bytes,error),"unknown buttons rejected");
    invalid={};invalid.kind=LanMessageKind::welcome;invalid.race.laps=7;
    check(!lan_encode(invalid,bytes,error),"unsupported race settings rejected");
    check(lan_newer(0,0xffffffffU)&&!lan_newer(5,5)&&!lan_newer(4,5)&&!lan_newer(0x80000000U,0),"serial wrap and stale rejection");
    std::uint32_t rng=123;
    for(unsigned trial=0;trial<1000;++trial){std::vector<std::uint8_t> junk(trial%200);for(auto& b:junk){rng=rng*1664525+1013904223;b=std::uint8_t(rng>>24);}LanMessage out;check(!lan_decode(junk.data(),junk.size(),out,error),"untrusted junk rejected");}
    {
        LanLink host,guest;std::uint64_t now=1000000;
        const auto receive_until=[&](LanLink& peer,const auto& received) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
            do {
                if(!peer.poll(now,error))return false;
                if(received())return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }while(std::chrono::steady_clock::now()<deadline);
            return false;
        };
        check(host.host(0,1234,now,error),"host binds ephemeral loopback room");
        check(guest.join("127.0.0.1",host.port(),5678,now,error),"guest opens real UDP socket");
        for(unsigned i=0;i<100&&!host.connected();++i){now+=10000;check(guest.poll(now,error)&&host.poll(now,error),"nonblocking handshake polls");}
        check(!host.connected()&&!guest.connected(),"room waits for both ready");
        LanRaceOptions settings{3,4,1};settings.rides[0].car=0x1001;
        check(host.paired()&&guest.paired()&&host.configure_race(settings,false,error)&&guest.poll(now,error)&&
            guest.race_options()==settings&&!guest.race_launched(),"paired guest sees edits before launch");
        check(!guest.configure_race({},true,error),"guest cannot replace host race settings");
        check(!host.configure_race(settings,true,error),"host cannot start while guest is still in garage");
        LanRide guest_ride;guest_ride.car=0x1002;guest_ride.installed[10]=0x1010;
        check(guest.confirm_guest_ride(guest_ride,error)&&guest.poll(now,error)&&host.poll(now,error)&&host.guest_ride_ready(),"guest confirms separate configured ride");
        settings.rides[1]=guest_ride;
        check(host.configure_race(settings,true,error)&&guest.poll(now,error)&&guest.race_launched()&&
            guest.race_options()==settings&&!guest.connected(),"launch locks shared settings before load");
        check(!host.configure_race({},false,error),"host settings freeze for the launched race");
        host.set_ready();guest.set_ready();
        for(unsigned i=0;i<100&&(!host.connected()||!guest.connected());++i){now+=10000;check(guest.poll(now,error)&&host.poll(now,error),"ready handshake polls");}
        check(host.connected()&&guest.connected(),"both peers agree ready");
        std::uint32_t sent_sequence=0;
        check(!host.has_remote_input()&&guest.send_input(pad(true,-12345).pads[0],error,&sent_sequence)&&sent_sequence!=0,"guest sends identified controls");
        now+=10000;check(receive_until(host,[&]{return host.has_remote_input();}),"host receives controls before virtual time advances");
        check(host.has_remote_input()&&host.remote_input(now).axes[0]==-12345&&host.remote_input(now).buttons==pad(true).pads[0].buttons,"remote controls delivered");
        check(host.remote_input(now+250001).buttons==gamepad_button_bit(GamepadButton::trigger_left),"stale controls brake instead of holding throttle");
        LanRaceFrame frame;frame.step=321;frame.cars[1].position[0]=123;
        check(host.publish(frame,error),"host publishes real UDP snapshot");
        LanRaceFrame received;check(receive_until(guest,[&]{return guest.take_snapshot(received);})&&received.step==321&&received.cars[1].position[0]==123,"guest receives authoritative frame");
        check(guest.input_ack()==sent_sequence,"authoritative snapshot acknowledges the sent sequence");
        check(!guest.take_snapshot(received),"snapshot consumed once");
        check(!host.send_input({},error)&&!guest.publish(frame,error),"directional authority enforced");
        LanSocket outsider;check(outsider.open(0,error),"foreign endpoint opens");LanAddress to;check(LanSocket::address("127.0.0.1",host.port(),to,error),"host address parsed");
        LanMessage forged;forged.kind=LanMessageKind::input;forged.session=1234;forged.sequence=100;forged.input=pad(false,30000).pads[0];
        check(lan_encode(forged,bytes,error)&&outsider.send(to,bytes,error)&&receive_until(host,[&]{return host.invalid_packets()>0;}),"foreign datagram processed without blocking");
        check(host.invalid_packets()>0&&host.remote_input(now).axes[0]==-12345,"foreign endpoint cannot steer remote car");
        check(!guest.poll(now+3000001,error),"live session times out after peer silence");host.leave();
    }
    {
        // A real UDP relay drops the first hello and reverses delivery order.
        // This tests the peer state machine, not just the serial helper.
        LanSocket relay;LanLink host,guest;std::uint64_t now=1000000;
        check(relay.open(0,error)&&host.host(0,901,now,error)&&guest.join("127.0.0.1",relay.port(),902,now,error),"loss/reorder room sockets open");
        LanAddress host_address,guest_address;check(LanSocket::address("127.0.0.1",host.port(),host_address,error),"relay resolves host");
        bool dropped=false;
        const auto pump=[&](){
            for(unsigned i=0;i<64;++i){std::array<std::uint8_t,kLanDatagramLimit+1> b{};LanAddress from;int n=relay.receive(from,b.data(),b.size(),error);if(n<=0)break;
                std::vector<std::uint8_t> packet(b.begin(),b.begin()+n);LanMessage m;
                if(!lan_decode(packet.data(),packet.size(),m,error))continue;
                if(!dropped&&m.kind==LanMessageKind::hello){dropped=true;continue;}
                if(from==host_address)check(relay.send(guest_address,packet,error),"relay host datagram");
                else {guest_address=from;check(relay.send(host_address,packet,error),"relay guest datagram");}
            }
        };
        for(unsigned i=0;i<100&&(!host.paired()||!guest.paired());++i){now+=10000;check(guest.poll(now,error),"loss guest poll");pump();check(host.poll(now,error),"loss host poll");pump();check(guest.poll(now,error),"loss guest reply");}
        check(dropped&&host.paired()&&guest.paired(),"room retries a lost hello");pump();
        const auto collect=[&](LanMessageKind kind,std::size_t count){
            std::vector<std::vector<std::uint8_t>> result;
            const auto limit=std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
            while(result.size()<count&&std::chrono::steady_clock::now()<limit){
                std::array<std::uint8_t,kLanDatagramLimit+1> bytes{};LanAddress from;
                const int size=relay.receive(from,bytes.data(),bytes.size(),error);
                if(size<0)break;
                if(!size){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
                LanMessage message;std::string reason;
                if(lan_decode(bytes.data(),std::size_t(size),message,reason)&&message.kind==kind)
                    result.emplace_back(bytes.begin(),bytes.begin()+size);
            }
            return result;
        };
        LanRaceOptions settings{2,3,1};settings.rides[1].car=0x1002;
        check(guest.confirm_guest_ride(settings.rides[1],error)&&guest.poll(now,error),"guest confirms its lobby choice");
        pump();check(host.poll(now,error)&&host.guest_ride_ready(),"host receives the guest ride before launch");pump();
        check(host.configure_race(settings,false,error),"room edit sent via relay");
        const auto stale_options=collect(LanMessageKind::welcome,1);
        check(stale_options.size()==1,"relay captured the pre-launch options");
        check(host.configure_race(settings,true,error),"room launch sent via relay");
        check(!collect(LanMessageKind::welcome,1).empty()&&!guest.race_launched(),"initial launch datagram is dropped");
        now+=100000;
        check(host.poll(now,error),"host retransmits launch after loss");pump();
        for(const auto& packet:stale_options)check(relay.send(guest_address,packet,error),"older lobby snapshot delivered late");
        check(guest.poll(now,error)&&guest.race_launched()&&guest.race_options()==settings,
            "retried launch freezes setup even after a delayed old lobby packet");
        host.set_ready();
        now+=100000;check(host.poll(now,error),"host advertises loaded race");pump();
        check(guest.poll(now,error)&&!guest.connected()&&!host.connected(),"one loaded console cannot start the race");pump();
        guest.set_ready();
        for(unsigned i=0;i<100&&(!host.connected()||!guest.connected());++i){now+=10000;check(guest.poll(now,error),"loaded guest poll");pump();check(host.poll(now,error),"loaded host poll");pump();}
        check(host.connected()&&guest.connected(),"both loaded consoles enter the same race");pump();
        check(guest.send_input(pad(true,1111).pads[0],error)&&guest.send_input(pad(true,-2222).pads[0],error),"two input generations sent");
        auto inputs=collect(LanMessageKind::input,2);check(inputs.size()==2,"relay captured both inputs");
        if(inputs.size()==2){check(relay.send(host_address,inputs[1],error)&&relay.send(host_address,inputs[0],error)&&host.poll(now,error),"inputs delivered in reverse order");check(host.remote_input(now).axes[0]==-2222,"old input cannot overwrite newer controls");}
        LanRaceFrame first,second_frame;first.step=10;second_frame.step=20;
        check(host.publish(first,error)&&host.publish(second_frame,error),"two snapshot generations sent");
        auto frames=collect(LanMessageKind::snapshot,2);check(frames.size()==2,"relay captured both snapshots");
        if(frames.size()==2){LanRaceFrame received;
            check(relay.send(guest_address,frames[1],error)&&relay.send(guest_address,frames[0],error)&&guest.poll(now,error),"snapshots delivered in reverse order");
            check(guest.take_snapshot(received)&&received.step==20,"old snapshot cannot roll back guest presentation");
            check(relay.send(guest_address,frames[1],error)&&guest.poll(now,error)&&!guest.take_snapshot(received),"duplicate snapshot does not produce another frame");
        }
        host.leave();guest.leave();
    }
    VehicleTrackCourse course;check(build_vehicle_proving_ground(course,error),"two-human proving ground builds");
    RaceRivalSetup second;second.spec=stock_240sx_vehicle_spec();second.hull=stock_240sx_hull();second.pose=course.start;second.pose.ground.y+=12;second.name="HUMAN TWO";
    RaceSession world(course);check(world.set_human_rival(second,error)&&world.start(error),"two-human world starts");
    check(world.timing_config().humans==2&&world.rival_count()==1,"both cars are timing humans");
    check(!world.advance({},error),"single-input API refused for two-human world");
    check(world.advance_humans({{pad(false),pad(false)}},error),"neutral connection frame precedes held controls");
    std::array<GamepadFrame,2> controls{{pad(true),pad(false)}};const auto before=world.timing().step;
    controls[1].pads[0].axes[0]=99999;check(!world.advance_humans(controls,error)&&world.timing().step==before,"invalid second input cannot partially advance world");
    controls[1]=pad(false);bool okay=true;
    for(unsigned i=0;i<1000&&okay&&race_car_held(world.timing());++i)okay=world.advance_humans(controls,error);
    check(okay&&!race_car_held(world.timing()),"both input pipelines run through countdown");
    const auto parked=world.rival(0).car().position;const auto first=world.car().position;
    for(unsigned i=0;i<90&&okay;++i)okay=world.advance_humans(controls,error);
    check(okay,"two-human physics advances");
    check(std::abs(world.car().position.x-first.x)>1,"host throttle moves host car");
    check(std::abs(world.rival(0).car().position.x-parked.x)<.5,"guest with no gas stays independent");
    const auto second_start=world.rival(0).car().position;controls={{pad(false),pad(true)}};
    for(unsigned i=0;i<90&&okay;++i)okay=world.advance_humans(controls,error);
    check(okay&&std::abs(world.rival(0).car().position.x-second_start.x)>1,"guest uses real human throttle pipeline");
    {
        RaceSession collision(course);auto target=second;target.pose=course.start;target.pose.ground.x+=6;
        bool valid=collision.set_human_rival(target,error)&&collision.start(error);
        std::array<GamepadFrame,2> drive{{pad(true),pad(false)}};
        for(unsigned i=0;i<1000&&valid&&race_car_held(collision.timing());++i)valid=collision.advance_humans(drive,error);
        check(valid&&collision.car().engine.angular_speed>100,"held launch throttle works for already connected lobby player");
        bool hit=false,moved=false;
        for(unsigned i=0;i<180&&valid;++i){valid=collision.advance_humans(drive,error);hit=hit||collision.pair_impulses()>0;moved=moved||collision.rival(0).car().owner_speeds.magnitude>.1F;}
        check(valid&&hit&&moved,"host solves physical human-human collision and moves remote body");
        LanRaceView render;check(render.open(collision,stock_240sx_chase_camera_table(),1,error),"guest-local render view opens");
        LanRaceFrame finish;finish.stage=4;finish.go=true;finish.outcome=1;finish.cars[0].position={{30,40,1}};finish.cars[1].position={{32,42,1}};
        finish.cars[0].finished=finish.cars[1].finished=true;finish.cars[0].status=finish.cars[1].status=1;
        finish.cars[0].place=1;finish.cars[1].place=2;finish.cars[0].finish_ticks=4000;finish.cars[1].finish_ticks=4200;
        check(render.apply(finish,1./60,error)&&render.session().car().position.x==32&&render.session().rival(0).car().position.x==30,"guest viewport selects guest car and preserves other actor");
        check(render.session().player_position()==2&&render.session().timing().racers[0].finish_ticks==4200&&render.session().timing().outcome==RaceOutcome::finished,"guest receives its own authoritative finishing place/time");
    }
    {
        RaceSession draft(course);auto follower=second;follower.pose=course.start;follower.pose.ground.x-=6;
        bool valid=draft.set_human_rival(follower,error)&&draft.start(error),benefit=false;
        std::array<GamepadFrame,2> both{{pad(true),pad(true)}};
        for(unsigned i=0;i<330&&valid;++i){valid=draft.advance_humans(both,error);benefit=benefit||draft.rival(0).car().aero.drag_scale<.999F;}
        check(valid&&benefit,"remote human receives the world's drafting benefit");
    }
    {
        RaceSession host_role(course),guest_role(course);
        auto distant=second;distant.pose.ground.y+=60;
        auto near=second;near.pose=course.start;
        bool valid=host_role.set_human_rival(distant,error)&&host_role.start(error)&&
            guest_role.set_human_rival(near,error)&&guest_role.start(error,&distant.pose);
        bool matched=true,accelerated=false,turned=false,braked=false;
        const auto close=[](double a,double b){return std::abs(a-b)<1e-5;};
        for(unsigned tick=0;valid&&tick<340;++tick){
            auto driver=pad(tick<270,tick>=200&&tick<270?16000:0);
            if(tick>=270)driver.pads[0].buttons=gamepad_button_bit(GamepadButton::trigger_left);
            valid=host_role.advance_humans({{driver,pad(false)}},error)&&
                guest_role.advance_humans({{pad(false),driver}},error);
            const auto& a=host_role.car();const auto& b=guest_role.rival(0).car();
            accelerated=accelerated||a.owner_speeds.magnitude>5;
            turned=turned||std::abs(a.steer_angle)>.02;
            braked=braked||(tick>=300&&a.owner_speeds.magnitude<4);
            matched=matched&&close(a.position.x,b.position.x)&&close(a.position.y,b.position.y)&&
                close(a.position.z,b.position.z)&&close(a.velocity.x,b.velocity.x)&&
                close(a.velocity.y,b.velocity.y)&&close(a.velocity.z,b.velocity.z)&&
                close(a.engine_rpm,b.engine_rpm)&&close(a.steer_angle,b.steer_angle)&&a.gear==b.gear;
        }
        check(valid&&accelerated&&turned&&braked,"role comparison exercises launch, acceleration, steering and braking");
        check(valid&&matched,"identical driver input matches across host and guest physics roles");
    }
    LanRaceFrame a,b;b.step=4;b.go=true;b.cars[0].gear=3;b.cars[0].position[0]=4;b.cars[0].orientation={{-1,0,0,0}};
    const auto middle=lan_interpolate(a,b,.5F);check(middle.cars[0].position[0]==2&&std::abs(middle.cars[0].orientation[0])==1,"snapshot interpolation and quaternion sign");
    check(!middle.go&&middle.step==a.step&&middle.cars[0].gear==a.cars[0].gear,"discrete state waits for its authoritative step");
    b.cars[0].position[0]=100;check(lan_interpolate(a,b,.5F).cars[0].position[0]==0&&lan_interpolate(a,b,1.F).cars[0].position[0]==100,"teleport cuts only at its authoritative step");
    {
        LanSnapshotBuffer timeline;LanRaceFrame view;
        std::uint32_t ack=0;double prior_position=0;
        const auto push=[&](unsigned step){LanRaceFrame f;f.step=step;f.cars[0].position[0]=float(step);timeline.push(f,step+100);};
        push(0);push(1);push(2);
        check(!timeline.sample(1./60,view,ack),"timeline waits for three steps of startup headroom");
        bool smooth=true;
        for(unsigned tick=3;tick<120;++tick){
            // Drop every 10th packet, delay the next, then deliver a burst.
            if(tick%10==2)push(tick-1);
            if(tick%10>=2)push(tick);
            const bool has_frame=timeline.sample(1./60,view,ack);
            const double position=view.cars[0].position[0],change=position-prior_position;
            smooth=smooth&&has_frame&&ack==view.step+100&&(tick==3||(change>=.89&&change<=1.11));
            prior_position=position;
        }
        check(smooth&&timeline.underrun_frames()==0,"loss and jitter do not stop or double presentation speed");
        for(unsigned tick=0;tick<16;++tick)timeline.sample(1./60,view,ack);
        check(view.step==119&&timeline.underrun_frames()>0,"prolonged loss holds the last authoritative state");
        push(117);push(119);
        check(timeline.sample(1./60,view,ack)&&view.step==119,"late and duplicate snapshots cannot rewind playback");
        LanRaceFrame finish;finish.step=120;finish.outcome=unsigned(RaceOutcome::finished);
        finish.cars[0].position[0]=120;timeline.push(finish,220);
        for(unsigned tick=0;tick<8;++tick)timeline.sample(1./60,view,ack);
        check(view.step==120&&ack==220&&view.outcome==finish.outcome,"final snapshot appears without a later packet");
    }
    std::cout<<(failures?"FAIL ":"PASS ")<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
