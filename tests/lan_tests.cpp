#include "nfsu/lan_link.h"
#include "nfsu/lan_race.h"
#include "nfsu/lan_frame_stats.h"
#include "nfsu/race_finish_flow.h"
#include "nfsu/race_feng_clock.h"
#include "nfsu/race_hud.h"
#include "race_finish_clock_native.inc"
#include "race_finish_phase_native.inc"
#include <cstring>
#include "nfsu/lan_results.h"
#include "race_finish_flow_native.inc"
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
template<class Predicate>bool await_lan(LanLink& link,std::uint64_t now,std::string& error,const Predicate& received){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
    do {if(!link.poll(now,error))return false;if(received())return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<deadline);
    return false;
}
bool await_lan_closed(LanLink& link,std::uint64_t now,std::string& error){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
    do {if(!link.poll(now,error))return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<deadline);
    return false;
}
#include "lan_lobby_cases.inc"
#include "lan_timeline_cases.inc"
}
int main(){
    std::string error;std::vector<std::uint8_t> bytes;
    check_lobby_cancellation(error);
    check_timeline_grid();
    check_timeline_pauses();
    check_timeline_pause_sequences();
    check_timeline_pause_loss();
    {
        LanFrameStats timing;char summary[256]{};
        timing.summary(summary,sizeof summary,true);
        check(std::strstr(summary,"role=host samples=0")&&std::strstr(summary,"p95_bin_upper_ms=-1.00"),
              "empty actual-present statistics report no percentile");
        timing.presented(1000000,false);timing.presented(9000000,false);
        timing.presented(10000000,true);
        check(timing.stats().count()==0,"loading and waiting plus the first racing presentation are excluded");
        timing.presented(10016667,true);timing.presented(10041667,true);timing.presented(10066668,true);
        check(timing.stats().count()==3&&timing.stats().over_budget()==1&&timing.stats().percentile_bin(95)==100,
              "actual-present intervals count strict greater-than-25ms and histogram p95");
        timing.presented(20000000,false);timing.presented(30000000,true);timing.presented(30016667,true);
        check(timing.stats().count()==4&&timing.stats().max_ms()<25.002,
              "a results or waiting gap and the first resumed frame cannot pollute race cadence");
        timing.summary(summary,sizeof summary,false);
        check(std::strstr(summary,"role=guest samples=4")&&std::strstr(summary,"p95_bin_upper_ms=25.25")&&
              std::strstr(summary,"over_25=1"),"both roles emit bounded numeric frame cadence summaries");
        LanFrameStats overflow;overflow.presented(0,true);overflow.presented(101000,true);
        overflow.summary(summary,sizeof summary,true);
        check(overflow.stats().count()==1&&std::strstr(summary,"p95_bin_upper_ms=-1.00")&&
              std::strstr(summary,"over_25=1"),"overflow percentile is explicit and zero-origin clocks are valid");
    }
    {
        // Feed real route progress from positions crossing the line backwards,
        // then return forwards and finish. Timing keeps a signed lap byte and
        // signed checkpoint word; neither is a display-clamped lap count.
        RaceRouteTrack track;track.valid=true;TrackPathRecord path;
        path.closed=1;path.end=path.count=16;path.length=628.31853F;path.distance_scale=1;track.paths.push_back(path);
        constexpr double radians=3.14159265358979323846/180;
        for(unsigned n=0;n<16;++n){TrackPathNode node;node.x=float(100*std::cos(n*22.5*radians));
            node.y=float(100*std::sin(n*22.5*radians));node.distance=float(n)*39.269908F;track.nodes.push_back(node);}
        const auto marker=[&](const char* name,double angle){TrackMarker m;m.name_hash=track_marker_hash(name);
            m.position[0]=float(100*std::cos(angle*radians));m.position[1]=float(100*std::sin(angle*radians));track.markers.push_back(m);};
        marker("ForwardStartingPosition",80);marker("ForwardStartLine",90);
        marker("SplitTimeBoundary1",180);marker("SplitTimeBoundary2",270);
        RaceRoute route;RaceTimingConfig config;RaceTimingState timing;std::vector<RaceTimingEvent> timed;
        bool setup=build_race_route(track,1,0,0,route,error)&&make_race_timing_config(60,2,config,error);
        config.humans=2;setup=setup&&start_race_timing(route,config,timing,error);
        check(setup,"backward network regression builds an actual circular race route");
        if(setup){
            set_race_stage(timing,RaceStage::countdown,timed);std::array<RaceTimingInput,2> input{};
            for(unsigned n=0;n<500&&!timing.go_valid;++n)setup=setup&&step_race_timing(route,config,timing,input.data(),timed,error);
            float at[2]={float(100*std::cos(80*radians)),float(100*std::sin(80*radians))};
            RaceProgress progress;start_race_progress(route,track,at,progress);
            bool back=false,negative_checkpoint=false,returned=false,wire=true;
            const auto move=[&](int degrees){
                const float next[2]={float(100*std::cos(degrees*radians)),float(100*std::sin(degrees*radians))};
                const float dx=next[0]-at[0],dy=next[1]-at[1];at[0]=next[0];at[1]=next[1];
                std::vector<RaceProgressEvent> events;step_race_progress(route,track,progress,at,1.F/60,events);
                for(auto& i:input){i.events=events.data();i.event_count=events.size();i.speed=std::sqrt(dx*dx+dy*dy)*60;}
                setup=setup&&step_race_timing(route,config,timing,input.data(),timed,error);
                back|=timing.racers[0].lap< -1;negative_checkpoint|=timing.racers[0].checkpoint< -1;
                returned|=back&&timing.racers[0].lap>=0;
                if(events.empty())return;
                LanMessage message;message.kind=LanMessageKind::snapshot;auto& f=message.frame;
                f.step=std::uint32_t(timing.step);f.world_ticks=timing.step_ticks;f.race_ticks=timing.race_ticks;
                f.start_ticks=timing.race_start_ticks;f.countdown_start=timing.countdown_start_ticks;
                f.stage=std::uint8_t(timing.stage);f.outcome=std::uint8_t(timing.outcome);f.go=timing.go_valid;
                for(unsigned actor=0;actor<2;++actor){const auto& row=timing.racers[actor];auto& c=f.cars[actor];
                    c.position[0]=at[0];c.position[1]=at[1];c.lap=row.lap;c.highest_lap=row.highest_lap;
                    c.checkpoint=row.checkpoint;c.highest_checkpoint=row.highest_checkpoint;c.place=std::uint8_t(row.position);
                    c.status=std::uint8_t(row.status);c.finished=row.finish_valid;c.finish_ticks=row.finish_ticks;}
                LanMessage decoded;const bool encoded=lan_encode(message,bytes,error);
                wire=wire&&encoded&&lan_decode(bytes.data(),bytes.size(),decoded,error)&&
                    decoded.frame.cars[0].lap==timing.racers[0].lap&&decoded.frame.cars[0].checkpoint==timing.racers[0].checkpoint;
            };
            for(int degree=79;degree>=-310;--degree)move(degree);
            for(int degree=-309;degree<=460&&!progress.finished;++degree)move(degree);
            check(setup&&back&&negative_checkpoint&&returned&&timing.outcome==RaceOutcome::finished&&
                  timing.racers[0].finish_valid&&timing.racers[1].finish_valid,
                  "actual backward line crossing returns through negative checkpoints and finishes both humans");
            check(wire,"all genuine backward/re-entry/finish timing states cross the network unchanged");
        }
        LanMessage width;width.kind=LanMessageKind::snapshot;auto& row=width.frame.cars[1];
        row.lap=-128;row.checkpoint=-32768;
        check(lan_encode(width,bytes,error),"native signed lap and checkpoint lower bounds remain representable");
        row.lap=row.highest_lap=127;row.checkpoint=row.highest_checkpoint=32767;
        check(lan_encode(width,bytes,error),"native signed lap and checkpoint upper bounds remain representable");
        row.lap=128;check(!lan_encode(width,bytes,error),"lap values outside the original signed byte are rejected");
        row.lap=-129;check(!lan_encode(width,bytes,error),"lap underflow outside the original signed byte is rejected");
        row.lap=0;row.checkpoint=-32769;
        check(!lan_encode(width,bytes,error),"checkpoint underflow outside the original signed word is rejected");
    }
    {
        LanMessage original;original.kind=LanMessageKind::snapshot;original.frame.statistics_valid=true;
        auto& car=original.frame.cars[1];car.lap_ticks[1]=210000;car.lap_total_ticks[2]=460000;
        auto& sample=original.frame.statistics[1];sample.top=52.12345678901234;
        sample.distance=1234.56789012345;sample.seconds=97.1234567890123;sample.done=true;
        sample.positions[1]=2;sample.scores[1]=123.25F;sample.last_score=123.25F;
        check(lan_encode(original,bytes,error)&&bytes.size()<=kLanDatagramLimit,
              "complete lap/statistics snapshot fits one existing bounded datagram");
        LanMessage decoded;check(lan_decode(bytes.data(),bytes.size(),decoded,error)&&decoded.frame.statistics_valid&&
            decoded.frame.cars[1].lap_ticks[1]==210000&&decoded.frame.cars[1].lap_total_ticks[2]==460000,
            "exact per-lap records survive the genuine wire codec");
        const auto& copied=decoded.frame.statistics[1];
        check(copied.top==sample.top&&copied.distance==sample.distance&&copied.seconds==sample.seconds&&
              copied.done&&copied.positions==sample.positions&&copied.scores==sample.scores&&copied.last_score==sample.last_score,
              "authoritative physical statistics retain binary64 precision and per-lap publications");
        RaceResultStatistics guest;check(read_lan_statistics(decoded.frame,1,guest)&&guest.sample(0).top==sample.top&&
              guest.sample(1).top==0,"guest reorders complete authoritative statistics with its local driver zero");
        auto invalid=original;invalid.frame.statistics[0].top=std::numeric_limits<double>::quiet_NaN();
        check(!lan_encode(invalid,bytes,error),"nonfinite authoritative statistics rejected");
        invalid=original;invalid.frame.cars[0].lap_ticks[1]=-1;
        check(!lan_encode(invalid,bytes,error),"negative completed lap time rejected");
        original.frame.statistics_valid=false;check(!read_lan_statistics(original.frame,0,guest),
              "initial visual-only frame cannot overwrite authoritative statistics");
    }
    for(const auto& row:kNativeFinishDriverDelays)
        check(race_finish_driver_delay(row.event)==row.frames60,"original finish driver180/360 initialization");
    for(unsigned scenario=0;scenario<sizeof(kNativeFinishPhaseCases)/sizeof(kNativeFinishPhaseCases[0]);++scenario) {
        const auto& input=kNativeFinishPhaseCases[scenario];RaceFengClock producer;
        RaceWorldFrameClock world;RaceOverPhase ui;ui.observe(true);
        std::uint64_t now=1000000;producer.sample(now);
        const auto interval=[&](unsigned frame){return frame%2==0?input.second:input.first;};
        for(unsigned frame=1;frame<=input.driving;++frame) {
            now+=interval(frame);const auto delta=producer.sample(now);world.advance(delta);ui.advance(delta);
        }
        check(ui.ticks()==input.pre_ui&&world.frames()==input.pre_world,
              "native UI and world clocks agree through the last supplied driving frame");
        now+=interval(input.driving+1);RaceFinishLoadingClock phase(producer,now*1000);
        for(const auto& original:kNativeFinishPhaseSamples)if(original.scenario==scenario) {
            if(original.frame){now+=interval(input.driving+1+original.frame);phase.sample(now*1000);}
            unsigned residue=0;const auto remainder=producer.remainder();std::memcpy(&residue,&remainder,4);
            check(residue==original.residue_bits,"loading entry retains the exact native producer timestamp and residue");
            check(phase.original_ticks()==original.phase,"native unscaled4000Hz phase counter, including odd intervals");
            check(world.frames()==original.world&&ui.ticks()==input.pre_ui,
                  "loading does not continue the stopped world or Race Over script");
            check(race_finish_loading_ready(phase.original_ticks(),original.resources)==original.ready,
                  "first uncovered table agrees with native phase AND actual resource readiness");
            const auto before=phase.original_ticks();phase.sample(now*1000);
            check(phase.original_ticks()==before&&producer.remainder()==remainder,
                  "recording another view in the same display cannot advance the loading phase");
        }
        const auto before=phase.original_ticks();const auto residue=producer.remainder();phase.resume();now+=5000000;phase.sample(now*1000);
        check(phase.original_ticks()==before&&producer.remainder()==residue,"focus resume excludes the gap and retains all phase/producer residue");
        auto reference=producer;now+=input.first;reference.sample(now);
        RaceFinishLoadingClock repeated(producer,now*1000);
        check(!repeated.original_ticks()&&producer.remainder()==reference.remainder(),
              "another event latches a new phase origin without resetting the process delta producer");
    }
    for(const auto& row:kNativeFinishClock) {
        RaceFengClock producer;RaceOverPhase phase;phase.observe(true);RaceWorldFrameClock world;
        for(unsigned n=0;n<row.count;++n) {
            const double measured=double(n&1?row.second:row.first)/1000000;
            const float period=row.mode?1.F/60.F:1.F/50.F;
            const auto delta=producer.publish(measured,row.full?period:0.F,row.full?row.blanks*period:1.F);
            phase.advance(delta);world.advance(delta);
        }
        check(phase.ticks()==row.ui,"full/post-clamp native producer and UI consumer numeric schedule");
        check(world.frames()==row.world_frames,"native60Hz world serial retains fractional delta residue");
    }
    for(const auto& edge:kNativeFinishClockEdges) {
        RaceTimingState timing;timing.racers.resize(1);RaceFinishHold hold;
        for(unsigned n=0;n<edge.initial;++n){hold.observe(timing,1);hold.advance(1.F/60.F);++timing.step;}
        timing.racers[0].finish_valid=true;timing.outcome=RaceOutcome::finished;
        const unsigned event=edge.delay==360?6:0;
        check(!hold.observe(timing,1,{{event,0,0,0}}),"terminal classification latches current world serial");
        for(unsigned n=0;n<edge.offset;++n){hold.advance(1.F/60.F);++timing.step;}
        check(hold.observe(timing,1)==edge.ready,"actual native finish consumer strict equality boundary");
    }
    {
        RaceFengClock producer;RaceOverPhase first,second;
        std::uint64_t now=1000000;producer.sample(now);
        for(unsigned n=0;n<17;++n){now+=20200;const auto dt=producer.sample(now);first.advance(dt);second.advance(dt);}
        const auto residue=producer.remainder();producer.resume();
        now+=5000000;check(producer.sample(now)==0&&producer.remainder()==residue,"pause rebases UI anchor without losing producer residue");
        check(producer.sample(now)==0,"same display timestamp cannot advance a common UI clock twice");
        first.observe(true);second.observe(false);
        for(unsigned n=0;n<30;++n) {
            now+=16666;const auto dt=producer.sample(now);first.advance(dt);second.advance(dt);
            if(n==6)second.observe(true);
        }
        check(first.ticks()==600&&second.ticks()==460,"two player views have separate Animate age and one published display delta");
        const auto old=producer.remainder();first.observe(false);first.observe(true);
        check(first.ticks()==0&&producer.remainder()==old,"repeat event resets Animate without restarting the upstream producer");
        now+=16666;first.advance(producer.sample(now));check(first.ticks()==20,"second event consumes the continuing producer");
    }
    {
        RaceTimingState timing;timing.racers.resize(2);RaceFinishHold hold;
        check(!hold.observe(timing,2),"two humans cannot end before a result");
        timing.racers[0].finish_valid=true;
        for(unsigned n=0;n<240;++n){check(!hold.observe(timing,2),"first human waits for the other driver");hold.advance(1.F/60.F);++timing.step;}
        timing.racers[1].status=RacerStatus::totalled;timing.outcome=RaceOutcome::finished;
        check(!hold.observe(timing,2),"second retirement starts its own final driver hold");
        for(unsigned n=0;n<180;++n){hold.advance(1.F/60.F);++timing.step;}
        check(!hold.observe(timing,2),"driver hold equality does not end a race early");
        hold.advance(1.F/60.F);++timing.step;
        check(hold.observe(timing,2),"both humans automatically enter joint ending without a button");
        timing.step=0;++timing.restarts;timing.outcome=RaceOutcome::running;for(auto& racer:timing.racers)racer={};
        check(!hold.observe(timing,2)&&hold.frames()==0,"restart drops former terminal and world clocks");
        timing.racers[0].status=RacerStatus::blown_engine;timing.racers[1].status=RacerStatus::totalled;
        timing.outcome=RaceOutcome::finished;
        check(!hold.observe(timing,2,{{6,0,0,0}}),"camera6 latches the native longer interval");
        for(unsigned n=0;n<180;++n){hold.advance(1.F/60.F);++timing.step;}
        check(!hold.observe(timing,2),"normal second hold cannot shorten a camera6 first hold");
        for(unsigned n=0;n<180;++n){hold.advance(1.F/60.F);++timing.step;}
        check(!hold.observe(timing,2),"camera6 retains strict360-unit equality");
        hold.advance(1.F/60.F);++timing.step;check(hold.observe(timing,2),"longer native interval permits shared ending");
    }
    {
        LanMessage ready;ready.kind=LanMessageKind::snapshot;ready.frame.finish_ready=true;
        check(!lan_encode(ready,bytes,error),"authority cannot announce results while a race is running");
        ready.frame.outcome=unsigned(RaceOutcome::finished);
        check(!lan_encode(ready,bytes,error),"authority cannot announce results before both human records exist");
        for(unsigned i=0;i<2;++i){auto& car=ready.frame.cars[i];car.status=1;car.finished=true;car.place=i+1;car.finish_ticks=171400+int(i)*203320;}
        check(lan_encode(ready,bytes,error),"explicit final authority handoff encodes");
        LanMessage decoded;check(lan_decode(bytes.data(),bytes.size(),decoded,error)&&decoded.frame.finish_ready&&
            decoded.frame.cars[1].finish_ticks==374720,"terminal handoff preserves both exact result times");
        auto invalid=bytes;invalid[46]=2;
        check(!lan_decode(invalid.data(),invalid.size(),decoded,error),"non-boolean finish handoff byte rejected");
        auto old=ready.frame;old.finish_ready=false;old.step=6000;
        LanSnapshotBuffer buffer;buffer.push(old,17);
        auto final=old;final.finish_ready=true;buffer.push(final,18);
        LanRaceFrame shown;std::uint32_t ack=0;
        check(buffer.sample(1./60,shown,ack)&&shown.finish_ready&&ack==18,
              "retry can deliver final handoff at an unchanged world step without waiting for interpolation");
        buffer.push(old,19);check(buffer.sample(0,shown,ack)&&shown.finish_ready&&ack==18,
              "an older ordinary snapshot cannot clear the final authority handoff");
    }
    for(unsigned kind=1;kind<=6;++kind){
        LanMessage original;original.kind=static_cast<LanMessageKind>(kind);original.session=0x12345678;original.sequence=0xabcdef01;
        original.race={3,5,1};original.launch=true;original.guest_ready=true;
        original.race.rides[0].car=0x1234;original.race.rides[0].installed[119]=0x4567;
        original.guest_ride.car=0xabcd;original.guest_ride.installed[17]=0x89ab;
        original.race.rides[0].turbo_brand=4;original.race.rides[1].turbo_brand=0;original.guest_ride.turbo_brand=2;
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
    invalid={};invalid.kind=LanMessageKind::welcome;invalid.race.rides[1].turbo_brand=5;
    check(!lan_encode(invalid,bytes,error),"unknown turbo brand cannot enter room");
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
        check(host.paired()&&guest.paired()&&host.configure_race(settings,false,error)&&
            await_lan(guest,now,error,[&]{return guest.race_options()==settings;})&&
            guest.race_options()==settings&&!guest.race_launched(),"paired guest sees edits before launch");
        check(!guest.configure_race({},true,error),"guest cannot replace host race settings");
        check(!host.configure_race(settings,true,error),"host cannot start while guest is still in garage");
        LanRide guest_ride;guest_ride.car=0x1002;guest_ride.installed[10]=0x1010;
        check(guest.confirm_guest_ride(guest_ride,error)&&guest.poll(now,error)&&
              await_lan(host,now,error,[&]{return host.guest_ride_ready();}),"guest confirms separate configured ride");
        settings.rides[1]=guest_ride;
        check(host.configure_race(settings,true,error)&&await_lan(guest,now,error,[&]{return guest.race_launched();})&&
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
        {
            // Host catch-up and Wi-Fi bursts reach the production poll in one
            // batch. An intermediate reset cannot be replaced by its successor.
            std::array<std::uint32_t,4> acknowledgements{};
            for(unsigned n=0;n<4;++n) {
                check(guest.send_input(pad(true,-12345).pads[0],error,&acknowledgements[n])&&
                      receive_until(host,[&]{return host.input_ack()==acknowledgements[n];}),
                      "each burst snapshot acknowledges its own guest control sample");
                LanRaceFrame burst;burst.step=322+n;
                burst.cars[0].position[0]=n==1?100.F:n==3?1.F:0.F;
                burst.cars[0].gear=int(2+n);burst.go=n!=0;
                check(host.publish(burst,error),"publish ordered burst with a reset and discrete transitions");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            check(guest.poll(now,error),"production guest polls the entire burst once");
            LanSnapshotBuffer timeline;unsigned consumed=0;std::uint32_t received_ack=0;
            while(guest.take_snapshot(received,&received_ack)) {
                check(received.step==322+consumed,"poll preserves every ordered host step");
                check(consumed<acknowledgements.size()&&received_ack==acknowledgements[consumed],
                      "acknowledgement remains bound to its snapshot through a packet batch");
                timeline.push(received,received_ack);++consumed;
            }
            check(consumed==4,"production poll retains all four timed snapshots");
            std::uint32_t ack=0;LanRaceFrame shown;
            check(timeline.sample(0,shown,ack)&&shown.step==322&&!shown.go,
                  "batched countdown transition waits for its host step");
            check(timeline.sample(.5/60,shown,ack)&&shown.step==322&&shown.cars[0].position[0]==0,
                  "batched reset does not blend before its host step");
            check(timeline.sample(.5/60,shown,ack)&&shown.step==323&&shown.cars[0].position[0]==100&&shown.cars[0].gear==3,
                  "batched reset is presented at its host step");
            check(timeline.sample(1./60,shown,ack)&&shown.step==324&&shown.cars[0].position[0]>=0&&shown.cars[0].position[0]<1&&shown.cars[0].gear==4,
                  "reset return is retained rather than blended through the reset");
        }
        check(!host.send_input({},error)&&!guest.publish(frame,error),"directional authority enforced");
        LanSocket outsider;check(outsider.open(0,error),"foreign endpoint opens");LanAddress to;check(LanSocket::address("127.0.0.1",host.port(),to,error),"host address parsed");
        LanMessage forged;forged.kind=LanMessageKind::input;forged.session=1234;forged.sequence=100;forged.input=pad(false,30000).pads[0];
        check(lan_encode(forged,bytes,error)&&outsider.send(to,bytes,error)&&receive_until(host,[&]{return host.invalid_packets()>0;}),"foreign datagram processed without blocking");
        check(host.invalid_packets()>0&&host.remote_input(now).axes[0]==-12345,"foreign endpoint cannot steer remote car");
        for(unsigned n=0;n<96;++n){
            frame.step=400+n;
            check(host.publish(frame,error)&&guest.poll(now,error),"a stalled consumer still polls bounded real UDP batches");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));check(guest.poll(now,error),"last queued UDP snapshot arrives");
        unsigned retained=0;std::uint32_t last_step=0;
        while(guest.take_snapshot(received)){
            check(received.step>=432&&received.step>last_step,"saturated transport retains ordered recent history");
            last_step=received.step;++retained;
        }
        check(retained==64&&last_step==495,"transport memory is bounded while retaining the newest state");
        static_assert(sizeof(LanLink)<4096,"snapshot history must not occupy the Switch stack");
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
        const auto await_relay=[&](const auto& received){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
            do {
                // Advance one synthetic heartbeat interval, independent of
                // the operating system's real sleep granularity.
                now+=100000;
                if(!guest.poll(now,error))return false;
                pump();
                if(!host.poll(now,error))return false;
                pump();
                if(!guest.poll(now,error))return false;
                if(received())return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }while(std::chrono::steady_clock::now()<deadline);
            return false;
        };
        check(await_relay([&]{return host.paired()&&guest.paired();}),"loss/reorder handshake pumps both endpoints and relay");
        check(dropped&&host.paired()&&guest.paired(),"room retries a lost hello");pump();
        const auto collect=[&](LanMessageKind kind,std::size_t count,bool launched_only=false,bool from_guest=false){
            std::vector<std::vector<std::uint8_t>> result;
            const auto limit=std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
            while(result.size()<count&&std::chrono::steady_clock::now()<limit){
                std::array<std::uint8_t,kLanDatagramLimit+1> bytes{};LanAddress from;
                const int size=relay.receive(from,bytes.data(),bytes.size(),error);
                if(size<0)break;
                if(!size){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
                LanMessage message;std::string reason;
                if(lan_decode(bytes.data(),std::size_t(size),message,reason)&&message.kind==kind&&
                   (!launched_only||message.launch)&&(!from_guest||!(from==host_address)))
                    result.emplace_back(bytes.begin(),bytes.begin()+size);
            }
            return result;
        };
        const auto poll_until=[&](LanLink& peer,const auto& received){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
            do {if(!peer.poll(now,error))return false;if(received())return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }while(std::chrono::steady_clock::now()<deadline);
            return false;
        };
        LanRaceOptions settings{2,3,1};settings.rides[1].car=0x1002;
        check(guest.confirm_guest_ride(settings.rides[1],error)&&guest.poll(now,error),"guest confirms its lobby choice");
        check(await_relay([&]{return host.guest_ride_ready();}),"host receives the guest ride before launch");pump();
        check(host.configure_race(settings,false,error),"room edit sent via relay");
        const auto stale_options=collect(LanMessageKind::welcome,1);
        check(stale_options.size()==1,"relay captured the pre-launch options");
        check(host.configure_race(settings,true,error),"room launch sent via relay");
        check(!collect(LanMessageKind::welcome,1,true).empty()&&!guest.race_launched(),"initial launch datagram is dropped");
        now+=100000;
        check(host.poll(now,error),"host retransmits launch after loss");pump();
        for(const auto& packet:stale_options)check(relay.send(guest_address,packet,error),"older lobby snapshot delivered late");
        check(await_relay([&]{return guest.race_launched();})&&guest.race_options()==settings,
            "retried launch freezes setup even after a delayed old lobby packet");
        host.set_ready();
        now+=100000;check(host.poll(now,error),"host advertises loaded race");pump();
        check(guest.poll(now,error)&&!guest.connected()&&!host.connected(),"one loaded console cannot start the race");pump();
        // The guest does not poll while its synchronous asset load runs.
        // A host that is already loaded must retain the room beyond the
        // ordinary in-race silence timeout without starting its world.
        for(unsigned n=0;n<80;++n){now+=100000;check(host.poll(now,error)&&!host.connected(),"loaded host waits through slow guest load");pump();}
        guest.set_ready();
        now+=100000;check(guest.poll(now,error),"slow guest advertises its completed load");
        check(collect(LanMessageKind::ready,1,false,true).size()==1,"first guest ready datagram deliberately lost");
        check(host.poll(now,error)&&!host.connected(),"lost ready cannot start authority before a retry");pump();
        check(await_relay([&]{return host.connected()&&guest.connected();}),"loaded peers retry ready through both endpoints and relay");
        check(host.connected()&&guest.connected(),"both loaded consoles enter the same race");pump();
        check(guest.send_input(pad(true,1111).pads[0],error)&&guest.send_input(pad(true,-2222).pads[0],error),"two input generations sent");
        auto inputs=collect(LanMessageKind::input,2);check(inputs.size()==2,"relay captured both inputs");
        if(inputs.size()==2){LanMessage newest;
            check(lan_decode(inputs[1].data(),inputs[1].size(),newest,error)&&
                  relay.send(host_address,inputs[1],error)&&relay.send(host_address,inputs[0],error)&&
                  poll_until(host,[&]{return host.input_ack()==newest.sequence;}),"inputs delivered in reverse order");
            check(host.remote_input(now).axes[0]==-2222,"old input cannot overwrite newer controls");}
        LanRaceFrame first,second_frame;first.step=10;second_frame.step=20;
        check(host.publish(first,error)&&host.publish(second_frame,error),"two snapshot generations sent");
        auto frames=collect(LanMessageKind::snapshot,2);check(frames.size()==2,"relay captured both snapshots");
        if(frames.size()==2){LanRaceFrame received;
            check(relay.send(guest_address,frames[1],error)&&relay.send(guest_address,frames[0],error)&&
                  poll_until(guest,[&]{return guest.take_snapshot(received);}),"snapshots delivered in reverse order");
            check(received.step==20,"old snapshot cannot roll back guest presentation");
            check(relay.send(guest_address,frames[1],error)&&guest.poll(now,error)&&!guest.take_snapshot(received),"duplicate snapshot does not produce another frame");
        }
        // The final authority marker uses normal sequenced packets and input
        // acknowledgements. Lose it once, then deliver a newer retry before an
        // older ordinary snapshot at exactly the same stopped world step.
        LanRaceFrame terminal;terminal.step=6000;terminal.outcome=unsigned(RaceOutcome::finished);
        for(unsigned i=0;i<2;++i){auto& car=terminal.cars[i];car.status=1;car.finished=true;car.place=i+1;car.finish_ticks=171400+int(i)*203320;}
        check(host.publish(terminal,error),"ordinary final record sent before the UI handoff");
        const auto ordinary=collect(LanMessageKind::snapshot,1);
        terminal.finish_ready=true;check(host.publish(terminal,error),"first final UI marker sent");
        check(collect(LanMessageKind::snapshot,1).size()==1,"first final UI marker deliberately dropped");
        std::uint32_t last_input=0;
        check(guest.send_input(pad(false).pads[0],error,&last_input),"guest keeps transport input acknowledgements during terminal presentation");
        check(await_relay([&]{return host.input_ack()==last_input;})&&host.publish(terminal,error),"host retries completed handoff after lost UDP packet");
        const auto retry=collect(LanMessageKind::snapshot,1);
        if(retry.size()==1&&ordinary.size()==1) {
            check(relay.send(guest_address,retry[0],error)&&relay.send(guest_address,ordinary[0],error),
                  "new final marker delivered ahead of delayed ordinary final record");
            LanRaceFrame final;std::uint32_t final_ack=0;
            check(poll_until(guest,[&]{
                while(guest.take_snapshot(final,&final_ack))if(final.finish_ready)return true;
                return false;
            })&&final.finish_ready&&final.cars[1].finish_ticks==374720&&final_ack==last_input,
                  "real UDP final marker retains times and its own input acknowledgement");
        }else check(false,"relay captured terminal retry and delayed ordinary record");
        for(unsigned n=0;n<40;++n) {
            now+=100000;check(guest.send_input(pad(false).pads[0],error),"result menu continues guest transport");
            pump();check(host.poll(now,error)&&host.publish(terminal,error),"result menu continues host transport");
            pump();check(guest.poll(now,error),"result menu remains connected beyond ordinary silence timeout");
        }
        check(host.connected()&&guest.connected(),"shared results do not depend on a held Plus button or terminate live transport");
        host.leave();guest.leave();
    }
    std::uint16_t rejoin_port=0;
    for(const bool host_first:{false,true}) {
        // Exercise both orders through the same synchronous loading policy as
        // the game. Only the already-loaded console continues to poll.
        LanLink host,guest;std::uint64_t now=1000000;
        check(host.host(rejoin_port,0x4e260101+unsigned(host_first)*2,now,error)&&
              guest.join("127.0.0.1",host.port(),0x4e260102+unsigned(host_first)*2,now,error),
              "asymmetric loading room opens");
        rejoin_port=host.port();
        for(unsigned n=0;n<100&&!(host.race_launched()&&guest.race_launched());++n){
            now+=10000;check(guest.poll(now,error)&&host.poll(now,error),"asymmetric loading lobby progresses");
            if(guest.paired()&&!guest.guest_ride_ready())check(guest.confirm_guest_ride({},error),"asymmetric guest chooses its ride");
            if(host.guest_ride_ready()&&!host.race_launched())check(host.configure_race({},true,error),"asymmetric host launches frozen settings");
        }
        check(host.race_launched()&&guest.race_launched(),"both loading orders freeze the same launch");
        auto& fast=host_first?host:guest;auto& slow=host_first?guest:host;
        fast.set_ready();
        for(unsigned n=0;n<80;++n){now+=100000;check(fast.poll(now,error)&&!fast.connected(),"one ready console waits eight seconds without its peer polling");}
        slow.set_ready();
        for(unsigned n=0;n<100&&!(host.connected()&&guest.connected());++n){
            now+=10000;check(guest.poll(now,error)&&host.poll(now,error),"asymmetric readiness converges after loading");
        }
        check(host.connected()&&guest.connected()&&!host.has_remote_input(),"world waits for a real guest input after both loading orders");
        check(guest.send_input(pad(true).pads[0],error)&&await_lan(host,now,error,[&]{return host.has_remote_input();}),"first connected guest sample releases the authority input gate");
        host.leave();bool closed=false;
        for(unsigned n=0;n<100&&!closed;++n){now+=1000;closed=!guest.poll(now,error);
            if(!closed)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(closed&&error=="LAN peer left","cancel closes the peer room before rejoining its port with fresh nonces");
    }
    for(unsigned ordering=0;ordering<3;++ordering) {
        // A real authoritative UDP endpoint controls final/bye delivery order.
        LanSocket authority;LanLink guest;std::uint64_t now=1000000;
        constexpr std::uint32_t host_session=0x4e260001,guest_nonce=0x4e260002;
        check(authority.open(0,error)&&guest.join("127.0.0.1",authority.port(),guest_nonce,now,error),"final/bye UDP endpoints open");
        LanAddress peer;bool hello=false;
        for(unsigned attempt=0;attempt<100&&!hello;++attempt) {
            check(guest.poll(now,error),"final/bye guest discovery poll");
            std::array<std::uint8_t,kLanDatagramLimit+1> raw{};
            const int size=authority.receive(peer,raw.data(),raw.size(),error);
            LanMessage received;hello=size>0&&lan_decode(raw.data(),std::size_t(size),received,error)&&received.kind==LanMessageKind::hello;
            if(!hello)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(hello,"final/bye endpoint receives real guest hello");
        const auto send=[&](LanMessage message) {
            return lan_encode(message,bytes,error)&&authority.send(peer,bytes,error);
        };
        LanMessage welcome;welcome.kind=LanMessageKind::welcome;welcome.session=host_session;welcome.ack=guest_nonce;
        welcome.sequence=1;welcome.launch=true;
        LanMessage ready;ready.kind=LanMessageKind::ready;ready.session=host_session;
        guest.set_ready();check(send(welcome)&&send(ready)&&await_lan(guest,now,error,[&]{return guest.connected();}),"final/bye real room enters connected state");
        LanMessage final;final.kind=LanMessageKind::snapshot;final.session=host_session;final.sequence=2;final.ack=91;
        final.frame.step=6000;final.frame.outcome=1;final.frame.finish_ready=true;
        for(unsigned i=0;i<2;++i){auto& car=final.frame.cars[i];car.status=1;car.finished=true;car.place=i+1;car.finish_ticks=171400+int(i)*203320;}
        LanMessage bye;bye.kind=LanMessageKind::bye;bye.session=host_session;
        LanRaceFrame latest;
        if(ordering==1)check(send(bye)&&send(final),"bye deliberately precedes the unaccepted final packet");
        else {
            if(ordering==0){LanMessage prior;prior.kind=LanMessageKind::snapshot;prior.session=host_session;
                prior.sequence=1;prior.frame.step=5999;check(send(prior),"ordinary running snapshot precedes final and bye in one poll");}
            check(send(final),"valid authoritative final sent before bye");
            if(ordering==2)check(await_lan(guest,now,error,[&]{return guest.take_snapshot(latest);}),"final already presented in an earlier successful poll");
            check(send(bye),"peer bye follows accepted final");
        }
        check(await_lan_closed(guest,now,error),"bye reports transport closure in all orders");
        const bool recovered=lan_recover_completed_results(guest,latest);
        check(recovered==(ordering!=1),"only a valid final accepted before closure takes precedence over room error");
        if(recovered)check(latest.finish_ready&&latest.cars[1].finish_ticks==374720,
                           "accepted final and exact P2 time survive same-poll bye or later closure");
        else check(!guest.poll(now+1,error)&&!lan_recover_completed_results(guest,latest),
                   "a packet delivered after bye cannot resurrect an unfinished closed room");
    }
    VehicleTrackCourse course;check(build_vehicle_proving_ground(course,error),"two-human proving ground builds");
    RaceRivalSetup second;second.spec=stock_240sx_vehicle_spec();second.hull=stock_240sx_hull();second.pose=course.start;second.pose.ground.y+=12;second.name="HUMAN TWO";
    for(const int pov:{0,1}) {
        const auto table=stock_240sx_chase_camera_table();
        RaceSession local(course);
        check(local.set_camera_table(table,error,pov)&&local.set_human_rival(second,error,&table)&&local.start(error),
              "split camera world starts with the selected chase POV");
        for(unsigned actor=0;actor<2;++actor) {
            const auto& driver=actor?local.rival(0):local;
            const auto subject=chase_subject(driver.car(),driver.spec());
            const auto& eye=driver.camera().eye;
            const auto offset=VehicleVec3{eye.x-subject.origin.x,eye.y-subject.origin.y,eye.z-subject.origin.z};
            ChaseCameraAnchor anchor;
            const int selected=actor?0:pov;
            const double along=offset.x*subject.forward.x+offset.y*subject.forward.y+offset.z*subject.forward.z;
            const bool matched=find_chase_anchor(table,true,selected,anchor)&&
                  driver.camera().pov==selected&&std::abs(along-anchor.along)<.0001;
            if(!matched)std::cout<<"split camera actor="<<actor<<" pov="<<driver.camera().pov
                <<" along="<<along<<" height="<<offset.z<<" expected="<<anchor.along<<','<<anchor.height<<'\n';
            check(matched,
                  "both local players use original layout3 anchors instead of single-player distances");
        }
    }
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
    {
        LanRaceFrame old,next;next.step=1;
        old.cars[0].wheel_phase={{0,3.1F,-3.1F,1.F}};
        next.cars[0].wheel_phase={{1,-3.1F,3.1F,-1.F}};
        const auto halfway=lan_interpolate(old,next,.5F);
        check(std::abs(halfway.cars[0].wheel_phase[0]-.5F)<1e-6F&&
              std::abs(halfway.cars[0].wheel_phase[3])<1e-6F,
              "wheel rotation advances with the interpolated body clock");
        constexpr double pi=3.14159265358979323846;
        check(std::abs(std::abs(halfway.cars[0].wheel_phase[1])-pi)<1e-6&&
              std::abs(std::abs(halfway.cars[0].wheel_phase[2])-pi)<1e-6,
              "forward and reverse wheel phase wraps take the short arc");
        next.cars[0].position[0]=100;
        check(lan_interpolate(old,next,.5F).cars[0].wheel_phase==old.cars[0].wheel_phase&&
              lan_interpolate(old,next,1.F).cars[0].wheel_phase==next.cars[0].wheel_phase,
              "wheel rotation stays with its body across a reset cut");
    }
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
    for(const unsigned batch:{1U,4U,6U,8U}) {
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        double previous=0,maximum_error=0;unsigned samples=0;
        for(unsigned tick=1;tick<=600;++tick) {
            if(tick%batch==0)for(unsigned step=tick-batch+1;step<=tick;++step) {
                LanRaceFrame frame;frame.step=step;frame.cars[0].position[0]=float(step)*.75F;
                timeline.push(frame,step);
            }
            if(!timeline.sample(1./60,shown,ack))continue;
            const double position=shown.cars[0].position[0];
            if(tick>60){maximum_error=std::max(maximum_error,std::abs((position-previous)/.75-1));++samples;}
            previous=position;
        }
        check(samples==540&&maximum_error<.002,
              "steady packet groups preserve constant playback speed across each whole group");
    }
    for(const unsigned batch:{1U,4U,7U}){
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        std::vector<LanRaceFrame> pending;double previous=0,maximum_error=0;
        unsigned previous_display=0,samples=0;bool coherent=true;
        for(unsigned tick=1;tick<=1200;++tick){
            if(tick%batch==0)for(unsigned step=tick-batch+1;step<=tick;++step){
                LanRaceFrame frame;frame.step=step;
                for(unsigned actor=0;actor<2;++actor){frame.cars[actor].position[0]=step*.75F+actor*8;
                    frame.cars[actor].braking=step%20==1||step%20==2;}
                pending.push_back(frame);
            }
            if(tick%6!=0&&tick%6!=1&&tick%6!=3)continue;
            for(const auto& frame:pending)timeline.push(frame,frame.step);
            pending.clear();const unsigned period=tick-previous_display;previous_display=tick;
            if(!timeline.sample(double(period)/60,shown,ack))continue;
            const double position=shown.cars[0].position[0];
            if(tick>180){
                ++samples;maximum_error=std::max(maximum_error,std::abs((position-previous)/(.75*period)-1));
                const auto body_step=unsigned(std::floor(position/.75+.00005));
                coherent=coherent&&ack==shown.step&&shown.cars[0].braking==(body_step%20==1||body_step%20==2)&&
                    std::abs(shown.cars[1].position[0]-position-8)<.001;
            }
            previous=position;
        }
        check(samples==510&&maximum_error<.002&&coherent,
              "mixed one, two and three tick display intervals preserve speed and discrete body time");
    }
    {
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        const std::array<unsigned,8> groups{{1,4,2,6,3,1,8,2}};
        unsigned group=0,last_sent=0,next=groups[0],samples=0,held_wheels=0;
        double previous=0,previous_phase=0,maximum_delta=0;
        bool coherent=true;
        for(unsigned tick=1;tick<=900;++tick){
            if(tick==next){
                for(unsigned step=last_sent+1;step<=tick;++step){
                    LanRaceFrame frame;frame.step=step;
                    for(unsigned actor=0;actor<2;++actor){
                        frame.cars[actor].position[0]=float(step)*.75F+8*actor;
                        frame.cars[actor].wheel_phase[0]=float(std::remainder(step*.07,6.28318530717958647692));
                        frame.cars[actor].braking=step%20==1||step%20==2;
                    }
                    timeline.push(frame,step);
                }
                last_sent=tick;group=(group+1)%groups.size();next+=groups[group];
            }
            if(!timeline.sample(1./60,shown,ack))continue;
            const double position=shown.cars[0].position[0],phase=shown.cars[0].wheel_phase[0];
            if(tick>120){
                ++samples;maximum_delta=std::max(maximum_delta,std::abs((position-previous)/.75-1));
                if(phase==previous_phase)++held_wheels;
                const auto body_step=unsigned(std::floor(position/.75+.0001));
                coherent=coherent&&ack==shown.step&&shown.step==body_step&&
                    shown.cars[0].braking==(body_step%20==1||body_step%20==2)&&
                    std::abs(shown.cars[1].position[0]-position-8)<.001;
            }
            previous=position;previous_phase=phase;
        }
        check(samples==780&&maximum_delta<.111&&held_wheels==0&&coherent,
              "variable real-time groups preserve moving wheels, paired bodies, discrete states and exact acknowledgements");
    }
    {
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        bool held_authority=true,recovered=false;unsigned rendered=0;
        for(unsigned tick=0;tick<=840;++tick){
            if(tick<=500||tick>720){
                LanRaceFrame frame;frame.step=tick;frame.cars[0].position[0]=float(tick)*.5F;timeline.push(frame,tick);
            }
            if(!timeline.sample(1./60,shown,ack))continue;
            ++rendered;
            if(tick>500&&tick<=720)held_authority&=shown.cars[0].position[0]<=250;
            if(tick==840)recovered=std::abs(840-shown.cars[0].position[0]*2-3)<.1&&ack==shown.step;
        }
        check(held_authority&&timeline.underrun_frames()>100&&recovered&&rendered==838,
              "long loss holds authority and rebuilds current playback without permanent delay");
    }
    {
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;bool coherent=true;
        for(unsigned tick=1;tick<=900;++tick){
            if(tick>240||tick%6==0)for(unsigned step=tick>240?tick:tick-5;step<=tick;++step){
                LanRaceFrame frame;frame.step=step;frame.cars[0].position[0]=step*.75F;
                frame.cars[0].braking=step%20==1||step%20==2;timeline.push(frame,step);
            }
            if(!timeline.sample(1./60,shown,ack)||tick<=60)continue;
            const auto body_step=unsigned(std::floor(shown.cars[0].position[0]/.75+.00005));
            coherent&=shown.cars[0].braking==(body_step%20==1||body_step%20==2);
        }
        check(coherent&&std::abs(900-shown.cars[0].position[0]/.75-3)<.002,
              "shorter groups recover normal delay and align discrete state at float pose boundaries");
    }
    for(const double drift:{.998,1.002}){
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        for(unsigned tick=0;tick<=3000;++tick){LanRaceFrame frame;frame.step=tick;frame.cars[0].position[0]=float(tick);
            timeline.push(frame,tick);timeline.sample(drift/60,shown,ack);}
        check(std::abs(3000-shown.cars[0].position[0]-3)<.2,
              "small sender and display clock drift remains bounded in both directions");
    }
    {
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        double previous=0,maximum_error=0;
        for(unsigned tick=1;tick<=900;++tick){
            if(tick%4==0)for(unsigned step=tick-3;step<=tick;++step){LanRaceFrame frame;frame.step=step;
                frame.cars[0].position[0]=step*.75F;timeline.push(frame,step);}
            if(!timeline.sample(1./59.94,shown,ack))continue;
            const double position=shown.cars[0].position[0];
            if(tick>180)maximum_error=std::max(maximum_error,std::abs((position-previous)/.75-1));
            previous=position;
        }
        check(maximum_error<.002,"regular groups absorb real clock drift without pulsing playback speed");
    }
    {
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        std::vector<LanRaceFrame> held;bool authority=true;double recovered_lag=100;
        for(unsigned tick=1;tick<=360;++tick){
            LanRaceFrame frame;frame.step=tick;frame.cars[0].position[0]=float(tick);
            if(tick>=61&&tick<=120)held.push_back(frame);
            else {for(const auto& delayed:held)timeline.push(delayed,delayed.step);held.clear();timeline.push(frame,tick);}
            if(!timeline.sample(1./60,shown,ack))continue;
            if(tick>=61&&tick<=120)authority&=shown.cars[0].position[0]<=60;
            if(tick==241)recovered_lag=tick-shown.cars[0].position[0];
        }
        check(authority&&recovered_lag>=2.8&&recovered_lag<=3.2,
              "one accumulated sixty-packet Wi-Fi backlog returns to normal delay within two seconds");
    }
    {
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;double previous=0,maximum_error=0;
        unsigned stationary=0;
        for(unsigned tick=1;tick<=900;++tick){
            if(tick%4==0)for(unsigned step=tick-3;step<=tick;++step)if(step%19){
                LanRaceFrame frame;frame.step=step;frame.cars[0].position[0]=step*.75F;timeline.push(frame,step);}
            if(!timeline.sample(1./60,shown,ack))continue;
            const double position=shown.cars[0].position[0];
            if(tick>120){maximum_error=std::max(maximum_error,std::abs((position-previous)/.75-1));stationary+=position==previous;}
            previous=position;
        }
        check(maximum_error<.002&&!stationary,"isolated loss within regular groups preserves constant buffered speed");
    }
    {
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        constexpr double periods[]={.017,.018,.033,.025,.017};
        double elapsed=0,previous=0,maximum_error=0;unsigned sent=0,samples=0,stationary=0;
        for(unsigned display=0;display<1200;++display){
            const double seconds=periods[display%5];elapsed+=seconds;
            const unsigned available=unsigned(std::floor(elapsed*60+1e-8))/9*9;
            for(unsigned step=sent+1;step<=available;++step){LanRaceFrame frame;frame.step=step;
                frame.cars[0].position[0]=step*.75F;timeline.push(frame,step);}
            sent=available;if(!timeline.sample(seconds,shown,ack))continue;
            const double position=shown.cars[0].position[0];
            if(elapsed>3){++samples;stationary+=position==previous;
                maximum_error=std::max(maximum_error,std::abs((position-previous)/(.75*seconds*60)-1));}
            previous=position;
        }
        check(samples>1000&&!stationary&&maximum_error<.02,
              "fractional display periods and long packet groups preserve continuous body speed");
    }
    {
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;double lag=100;
        for(unsigned tick=1;tick<=500;++tick){
            if(tick%4==0)for(unsigned step=tick-3;step<=tick;++step)if(step<242||step>265){
                LanRaceFrame frame;frame.step=step;frame.cars[0].position[0]=float(step);timeline.push(frame,step);}
            if(timeline.sample(1./60,shown,ack)&&tick==392)lag=tick-shown.cars[0].position[0];
        }
        check(lag>=2.8&&lag<=3.2,"dropped outage refills current group cadence without prolonged extra delay");
    }
    std::cout<<(failures?"FAIL ":"PASS ")<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
