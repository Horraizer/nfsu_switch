#include "nfsu/lan_link.h"
#include "nfsu/lan_scene.h"
#include "nfsu/race_result.h"
#include "nfsu/race_finish_flow.h"
#include "nfsu/lan_results.h"
#include "nfsu/posix_file_system.h"
#include "race_camera_native.inc"
#include "race_finish_nis_native.inc"
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace nfsu {
// Supplied terminal records after real GO/physics. This is a presentation
// diagnostic, not an earned victory or a change to gameplay termination.
struct RaceWorldContactProbe {
    static void lap_publication(const RaceSession& input,unsigned lap,bool retirement=false) {
        auto& world=const_cast<RaceSession&>(input);auto& timing=world.timing_;
        ++timing.step;timing.race_ticks=lap==1?210000:460000;
        timing.now_ticks=timing.step_ticks=timing.race_start_ticks+timing.race_ticks;
        for(unsigned actor=0;actor<2;++actor) {
            auto& row=timing.racers[actor];row.lap=int(lap);row.highest_lap=int(lap);
            row.lap_ticks[1]=200000+int(actor)*10000;row.lap_total_ticks[1]=row.lap_ticks[1];
            if(lap==2){row.lap_ticks[2]=240000+int(actor)*10000;row.lap_total_ticks[2]=row.lap_ticks[1]+row.lap_ticks[2];}
            row.crossing_ticks[0]=timing.race_start_ticks+row.lap_total_ticks[lap];
            row.best_split_ticks[std::size_t(world.course_.route.finish)]=row.lap_ticks[1];
            if(lap==2){row.status=RacerStatus::normal_completion;row.finish_valid=true;row.position=int(actor+1);row.finish_ticks=row.lap_total_ticks[2];}
        }
        if(lap==2)timing.outcome=RaceOutcome::finished;
        if(retirement) {
            auto& failed=timing.racers[0];failed.status=RacerStatus::totalled;failed.finish_valid=false;failed.finish_ticks=0;failed.position=2;
            timing.racers[1].position=1;
            world.standings_.count=2;world.standings_.order={{0,1,2,3}}; // Deliberately stale live ranking.
        }
    }
    static void constant_motion(const RaceSession& input) {
        auto& world=const_cast<RaceSession&>(input);++world.timing_.step;
        world.timing_.step_ticks+=67;world.timing_.now_ticks+=67;world.timing_.race_ticks+=67;
        for(unsigned actor=0;actor<2;++actor) {
            auto& car=const_cast<VehicleState&>((actor?world.rival(0):world).car());
            car.velocity={10,0,0};car.position.x+=10.0/60;
        }
    }
    static void lap(const RaceSession& input,unsigned actor,int ticks) {
        auto& row=const_cast<RaceSession&>(input).timing_.racers.at(actor);
        row.lap_ticks[1]=ticks;row.lap_total_ticks[1]=ticks;
    }
    static void terminal(const RaceSession& input,unsigned actor,RacerStatus status,unsigned place,int finish_ticks=0) {
        auto& world=const_cast<RaceSession&>(input);
        auto& row=world.timing_.racers.at(actor);
        row.status=status;row.position=int(place);row.finish_valid=status==RacerStatus::normal_completion;
        row.finish_ticks=row.finish_valid?(finish_ticks?finish_ticks:120000+int(actor)*4000):0;
        row.finish_step=world.timing_.step;
        bool done=true;for(unsigned i=0;i<2;++i)done&=world.timing_.racers[i].status!=RacerStatus::not_finished;
        world.timing_.outcome=done?RaceOutcome::finished:RaceOutcome::running;
    }
};
}

using namespace nfsu;
namespace {
void require(bool okay,const std::string& message){if(!okay)throw std::runtime_error(message);}
float native_vertical(unsigned word) {
    for(const auto& row:kNativeRaceProjection)if(row[0]==word&&row[1]==640&&row[2]==480){
        float value;std::memcpy(&value,&row[4],4);return -value;
    }
    throw std::runtime_error("native lens fixture missing");
}
std::uint64_t now_us(){return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());}
void verify_statistics_menu(const RaceSession& session,const RaceResultStatistics& statistics,
                            const std::array<unsigned,2>& order) {
    const auto selected_actors=post_race_two_human_order(session);
    require(selected_actors==order,"live results/statistics binding must preserve this driver order");
    PostRaceMenu menu(false,false,0,2,selected_actors[0]==0?0:1);
    FrontendInput next;next.up=true;FrontendInput accept;accept.accept=true;
    menu.update(next);menu.update(accept);
    require(menu.statistics(),"real post-race Statistics action did not open");
    for(unsigned n=0;n<2;++n) {
        const unsigned actor=selected_actors.at(menu.racer());
        const auto values=statistics.values(session,actor?"Remote car":"Local car",actor);
        require(values.at("RacerName")==std::string(actor?session.rival_name(0):session.player_name())+
                (actor?" - Remote car":" - Local car"),"Statistics menu must select the actual row's driver/name");
        if(session.is_drift()) {
            require(values.at("Data_1")==std::string(actor?"77":"11")&&values.at("Data_2")==std::string(actor?"7.70 sec":"1.10 sec"),
                    "Statistics menu must read selected human's best score and longest drift");
            require(values.at("RaceFinishedPosition")==std::string(menu.racer()?"02":"01"),
                    "Drift Statistics rank follows the two real human scores");
        }else {
            const auto& row=session.timing().racers.at(actor);
            require(values.at("Data_1")==std::string(row.finish_valid?format_race_ticks(row.finish_ticks):"--:--.--"),
                    "network Statistics menu preserves each selected driver's actual finish or retirement");
            const auto expected=(row.position<10?"0":"")+std::to_string(row.position);
            require(values.at("RaceFinishedPosition")==expected,"network Statistics menu preserves assigned rank including untimed failures");
        }
        FrontendInput switch_row;switch_row.left=menu.racer()!=0;switch_row.right=menu.racer()==0;
        menu.update(switch_row);
    }
}
void verify_authoritative_network_statistics(const std::filesystem::path& root) {
    std::string error;LanLink host,guest;auto clock=now_us();LanRaceOptions options;
    require(host.host(0,0x265a001U,clock,error)&&guest.join("127.0.0.1",host.port(),0x265a002U,clock,error),error);
    bool confirmed=false,launched=false;
    for(unsigned tick=0;tick<300&&!(host.connected()&&guest.connected());++tick) {
        clock+=1000;require(host.poll(clock,error)&&guest.poll(clock,error),error);
        if(guest.paired()&&!confirmed){require(guest.confirm_guest_ride(options.rides[1],error),error);confirmed=true;}
        if(host.guest_ride_ready()&&!launched){require(host.configure_race(options,true,error),error);launched=true;}
        if(host.race_launched())host.set_ready();if(guest.race_launched())guest.set_ready();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(host.connected()&&guest.connected(),"statistics UDP room did not connect");
    LanRaceWorld authority;require(authority.load(root,error,options),error);
    std::array<GamepadFrame,2> pads;for(auto& pad:pads)pad.pads[0].connected=true;
    for(unsigned n=0;n<500&&!authority.session().timing().go_valid;++n)require(authority.advance(pads,error),error);
    require(authority.session().timing().go_valid,"statistics authority must reach real GO");
    std::array<LanRaceView,2> views;
    for(unsigned local=0;local<2;++local)
        require(views[local].open(authority.session(),authority.cameras(local),local,error,&authority.host_hull()),error);
    RaceResultStatistics physical;std::array<RaceResultStatistics,2> visible;
    FrontendArt source_art,statistics_art;
    require(source_art.load(root,error,false)&&source_art.load_post_race(root,error)&&
            source_art.prepare_post_race(statistics_art,error,true),error);
    statistics_art.advance(1);
    physical.observe(authority.session());
    const auto publish=[&](bool check) {
        auto sent=authority.snapshot();write_lan_statistics(sent,physical);
        require(host.publish(sent,error),error);
        LanRaceFrame received;bool fresh=false;
        for(unsigned tick=0;tick<100&&!fresh;++tick) {
            clock+=1000;require(guest.poll(clock,error),error);fresh=guest.take_snapshot(received);
            if(!fresh)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(fresh&&received.statistics_valid,"authoritative statistics did not traverse actual UDP");
        for(unsigned local=0;local<2;++local) {
            require(views[local].apply(local?received:sent,1./60,error),error);
            require(read_lan_statistics(local?received:sent,local,visible[local]),"normal result binding rejected authoritative statistics");
            if(!check)continue;
            const auto& state=views[local].session();const auto order=post_race_two_human_order(state);
            verify_statistics_menu(state,visible[local],order);
            PostRaceMenu menu(false,false,0,2,order[0]==0?0:1);
            FrontendInput statistics;statistics.up=true;FrontendInput accept;accept.accept=true;
            menu.update(statistics);menu.update(accept);require(menu.statistics(),"authoritative Statistics menu did not open");
            for(unsigned choice=0;choice<2;++choice) {
                const auto actor=order.at(menu.racer()),wire=actor?1-local:local;
                const auto expected=physical.values(authority.session(),"car",wire);
                const auto actual=visible[local].values(state,"car",actor);
                require(expected.size()==actual.size(),"network Statistics must not retain extra or stale result rows");
                for(const auto& field:expected)if(field.first!="RacerName") {
                    const auto at=actual.find(field.first);
                    require(at!=actual.end()&&at->second==field.second,"network Statistics lost exact authority field "+field.first);
                }
                require(actual.count("AllLapTimes_1")&&actual.count("AllLapTimes_2"),"both completed lap rows must survive the network view");
                auto rendered_expected=expected;rendered_expected["RacerName"]=actual.at("RacerName");
                FrontendUiFrame expected_ui,actual_ui;
                statistics_art.record_post_race(1280,720,menu,rendered_expected,expected_ui);
                statistics_art.record_post_race(1280,720,menu,actual,actual_ui);
                require(!actual_ui.vertices.empty()&&expected_ui.vertices.size()==actual_ui.vertices.size()&&
                        !std::memcmp(expected_ui.vertices.data(),actual_ui.vertices.data(),actual_ui.vertices.size()*sizeof(actual_ui.vertices[0])),
                        "actual Statistics frontend must draw the complete authoritative selected row");
                FrontendInput other;other.left=menu.racer()!=0;other.right=menu.racer()==0;menu.update(other);
            }
        }
    };
    publish(false);
    // Omit sixty intervening world snapshots. Render cadence must not become
    // the source of speed, distance, duration or laps-led statistics.
    pads[0].pads[0].buttons=gamepad_button_bit(GamepadButton::trigger_right);
    for(unsigned n=0;n<60;++n){require(authority.advance(pads,error),error);physical.observe(authority.session());}
    const auto before=physical.player_metres();
    for(unsigned n=0;n<600;++n) {
        RaceWorldContactProbe::constant_motion(authority.session());physical.observe(authority.session());
        if(n%5==4)publish(false);
    }
    require(std::abs(physical.player_metres()-before-100)<1e-9,"600 supplied10m/s world steps must add100metres despite5-step snapshot spacing");
    for(unsigned lap=1;lap<=2;++lap) {
        RaceWorldContactProbe::lap_publication(authority.session(),lap);physical.observe(authority.session());publish(lap==2);
    }
    RaceWorldContactProbe::lap_publication(authority.session(),2,true);physical.observe(authority.session());
    require(authority.snapshot().cars[0].place==2&&authority.snapshot().cars[1].place==1,
            "snapshot must carry assigned terminal places instead of stale live standings");
    publish(true);
    std::cout<<"PASS authoritative network statistics, skipped snapshots, both menus and terminal rank\n";
}
}
int main(int argc,char** argv){
    try {
        require(argc==2,"usage: nfsu_lan_camera_retail_tests RETAIL_ROOT");
        const auto root=std::filesystem::u8path(argv[1]);std::string error;
        for(const auto& row:kNativeFinishNis) {
            unsigned selected=0;
            require(select_race_finish_scene(posix_file_system(),root,row.racers,row.place,row.track,row.variant,selected,error,row.humans),error);
            require(selected==row.scene,"original PC finish NIS branch/playlist must match native execution");
        }
        LanRaceOptions options;options.rides[0].car=bin_hash("TIBURON");options.rides[1].car=bin_hash("GOLF");
        LanLink host,guest;auto clock=now_us();
        require(host.host(0,0x25ca001U,clock,error)&&host.configure_race(options,false,error),error);
        require(guest.join("127.0.0.1",host.port(),0x25ca002U,clock,error),error);
        bool confirmed=false,launched=false;
        for(unsigned tick=0;tick<300&&!(host.connected()&&guest.connected()&&guest.race_launched());++tick){
            clock+=1000;require(host.poll(clock,error)&&guest.poll(clock,error),error);
            if(guest.paired()&&!confirmed){require(guest.confirm_guest_ride(options.rides[1],error),error);confirmed=true;}
            if(host.guest_ride_ready()&&!launched){require(host.configure_race(options,true,error),error);launched=true;}
            if(host.race_launched())host.set_ready();
            if(guest.race_launched())guest.set_ready();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(host.connected()&&guest.connected()&&host.race_launched()&&guest.race_launched(),
                "actual UDP lobby did not launch both participants");
        LanRaceWorld world;require(world.load(root,error,host.race_options()),error);
        std::array<LanRaceView,2> views;
        std::array<std::unique_ptr<LanScene>,2> scenes;
        for(unsigned local=0;local<2;++local){
            require(views[local].open(world.session(),world.cameras(local),local,error,&world.host_hull()),error);
            scenes[local]=std::make_unique<LanScene>();
            require(scenes[local]->load(root,host.race_options(),world.models(),local,error),error);
        }
        unsigned failures=0,snapshots=0;
        for(unsigned pov=0;pov<3;++pov){
            GamepadState remote;remote.connected=true;
            require(guest.send_input(remote,error),error);bool received_input=false;
            for(unsigned tick=0;tick<100&&!received_input;++tick){
                clock+=1000;require(host.poll(clock,error),error);received_input=host.has_remote_input();
                if(!received_input)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(received_input,"host did not receive the real guest input packet");
            std::array<GamepadFrame,2> input{};input[0].pads[0].connected=true;input[1].pads[0]=host.remote_input(clock);
            require(world.advance(input,error),error);
            const auto sent=world.snapshot();require(host.publish(sent,error),error);
            LanRaceFrame received;bool fresh=false;
            for(unsigned tick=0;tick<100&&!fresh;++tick){
                clock+=1000;require(guest.poll(clock,error),error);fresh=guest.take_snapshot(received);
                if(!fresh)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(fresh&&received.step==sent.step,"guest did not receive the current authority snapshot");++snapshots;
            for(unsigned local=0;local<2;++local){
                require(views[local].apply(local?received:sent,1./60,error,false,pov!=0),error);
                RaceGpuFrame gpu;RaceHudFrame hud;
                require(scenes[local]->frame(views[local],gpu,hud,error)&&validate_recorded_commands(gpu.commands,error),error);
                const auto& camera=views[local].session().camera();
                const auto& projection=gpu.commands.frame.projection;
                const float expected=native_vertical(camera.fov);
                const bool lens=camera.pov==int(pov)&&std::abs(projection[1]-expected)<2e-6F&&
                    std::abs(projection[0]*gpu.commands.width-projection[1]*gpu.commands.height)<.002F;
                std::cout<<"NETWORK_CAMERA local="<<local<<" pov="<<camera.pov<<" fy="<<projection[1]
                         <<" expected="<<expected<<" received_step="<<received.step<<'\n';
                if(!lens)++failures;
                // Compare the local presentation against the native single-view
                // table on the same received pose, including real track clearance.
                ChaseCamera control(world.cameras(local),{},{});require(control.set_pov(int(pov)),"single-view anchor missing");
                RaceCameraWorld collision(world.session().course().world);ChaseCameraView reference;
                const auto& state=views[local].session();
                require(control.reset(chase_subject(state.car(),state.spec()),collision,reference),control.error());
                const double dx=camera.eye.x-reference.eye.x,dy=camera.eye.y-reference.eye.y,dz=camera.eye.z-reference.eye.z;
                if(dx*dx+dy*dy+dz*dz>1e-8)++failures;
                require(gpu.track_draws&&gpu.car_draws,"network camera check emitted no track/car geometry");
            }
        }
        // An authority terminal edge must enter animate at zero on each peer.
        // The other human remains on the ordinary HUD until its own finish.
        std::array<GamepadFrame,2> release_input;for(auto& pad:release_input)pad.pads[0].connected=true;
        for(unsigned i=0;i<500&&!world.session().timing().go_valid;++i)require(world.advance(release_input,error),error);
        require(world.session().timing().go_valid,"network statistics diagnostic did not reach real GO");
        auto terminal=world.snapshot();
        std::array<RaceResultStatistics,2> network_statistics;
        const auto check_terminal=[&](double first_seconds,double second_seconds) {
            ++terminal.step;require(host.publish(terminal,error),error);
            LanRaceFrame arrived;bool fresh=false;
            for(unsigned tick=0;tick<100&&!fresh;++tick) {
                clock+=1000;require(guest.poll(clock,error),error);fresh=guest.take_snapshot(arrived);
                if(!fresh)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(fresh&&arrived.cars[0].finished==terminal.cars[0].finished&&
                    arrived.cars[1].finished==terminal.cars[1].finished&&arrived.finish_ready==terminal.finish_ready,
                    "terminal edge and separate table-readiness marker did not cross actual UDP");
            for(unsigned local=0;local<2;++local) {
                require(views[local].apply(local?arrived:terminal,1./60,error),error);
                network_statistics[local].observe(views[local].session());
                RaceGpuFrame gpu;RaceHudFrame actual,expected;
                require(scenes[local]->frame(views[local],gpu,actual,error),error);
                if(terminal.cars[local].finished||terminal.cars[local].status!=0)
                    build_race_over_frame(scenes[local]->atlas(),local?second_seconds:first_seconds,1280,720,expected,"Race Over");
                else build_race_hud_frame(views[local].session(),scenes[local]->atlas(),1280,720,expected);
                const bool phase=actual.quads==expected.quads&&
                    !std::memcmp(actual.vertices.data(),expected.vertices.data(),actual.quads*4*sizeof(TrackVertex));
                std::cout<<(phase?"PASS":"FAIL")<<" network finish phase local="<<local<<" seconds="<<(local?second_seconds:first_seconds)<<'\n';
                if(!phase)++failures;
                if(!terminal.finish_ready&&terminal.cars[local].status!=0&&
                   (local?second_seconds:first_seconds)>.3) {
                    bool visible=false;for(std::size_t vertex=4;vertex<actual.quads*4;++vertex)
                        visible|=actual.vertices[vertex].color[3]!=0;
                    require(visible,"last finishing human must see its message during the driver hold before joint results are ready");
                }
                const auto before=actual;
                require(scenes[local]->frame(views[local],gpu,actual,error),error);
                if(actual.quads!=before.quads||std::memcmp(actual.vertices.data(),before.vertices.data(),actual.quads*4*sizeof(TrackVertex)))++failures;
            }
        };
        terminal.cars[0].finished=true;terminal.cars[0].status=1;terminal.cars[0].place=1;
        terminal.cars[0].finish_ticks=120000;
        check_terminal(0,0);
        for(auto& scene:scenes){scene->advance_presentation(.1);scene->advance_presentation(.05);}
        terminal.cars[1].finished=true;terminal.cars[1].status=1;terminal.cars[1].place=2;
        terminal.cars[1].finish_ticks=124000;terminal.outcome=unsigned(RaceOutcome::finished);
        check_terminal(.15,0);
        for(auto& scene:scenes)scene->advance_presentation(.4);
        check_terminal(.55,.4);
        // A stopped authority clock/duplicate render does not stop UI time.
        for(auto& scene:scenes)scene->advance_presentation(.5);
        check_terminal(1.05,.9);
        // Real transport and view reordering, with P2 leading P1. Selection
        // through the actual menu must work on both consoles' local row zero.
        terminal.cars[0].place=2;terminal.cars[0].finish_ticks=124000;
        terminal.cars[1].place=1;terminal.cars[1].finish_ticks=120000;
        check_terminal(1.05,.9);
        for(unsigned local=0;local<2;++local)
            verify_statistics_menu(views[local].session(),network_statistics[local],local?std::array<unsigned,2>{{0,1}}:std::array<unsigned,2>{{1,0}});
        terminal.cars[0].status=7;terminal.cars[0].finished=false;terminal.cars[0].finish_ticks=0;
        check_terminal(1.05,.9);
        for(unsigned local=0;local<2;++local)
            verify_statistics_menu(views[local].session(),network_statistics[local],local?std::array<unsigned,2>{{0,1}}:std::array<unsigned,2>{{1,0}});
        for(auto& scene:scenes){scene->advance_presentation(1);scene->advance_presentation(1);scene->advance_presentation(.2);}
        terminal.finish_ready=true;check_terminal(3.25,3.1);
        for(unsigned local=0;local<2;++local) {
            const auto& state=views[local].session();
            unsigned original=0;
            require(select_race_finish_scene(posix_file_system(),root,2,state.player_position(),1003,0,original,error,1),error);
            require(scenes[local]->load_finish(root,state,0,error),error);
            require(scenes[local]->finish_scene_id()==original&&scenes[local]->finish_car_slot(0)==0,
                    "network participants retain the original single-local-human NIS branch and human-priority slot");
            for(double seconds:{0.,2.}) {
                RaceGpuFrame ending;
                require(scenes[local]->finish_frame(state,seconds,ending,error)&&validate_recorded_commands(ending.commands,error)&&
                        ending.car_draws&&ending.track_draws,error);
            }
            scenes[local]->clear_finish();
        }
        for(auto& car:terminal.cars){car.finished=false;car.status=0;car.place=0;car.finish_ticks=0;}
        terminal.outcome=unsigned(RaceOutcome::running);terminal.finish_ready=false;check_terminal(0,0);
        terminal.cars[1].finished=true;terminal.cars[1].status=1;terminal.cars[1].place=1;terminal.cars[1].finish_ticks=140000;
        check_terminal(0,0);
        require(host.sent_bytes()&&host.received_bytes()&&guest.sent_bytes()&&guest.received_bytes(),"network path exchanged no datagrams");
        scenes={};views={};
        for(const auto mode:{QuickRaceMode::circuit,QuickRaceMode::drag,QuickRaceMode::drift}) {
            const unsigned event=mode==QuickRaceMode::drag?4:mode==QuickRaceMode::drift?3:1;
            auto local_world=std::make_unique<LanRaceWorld>();auto local_scene=std::make_unique<LanScene>();
            auto local_options=options;local_options.laps=mode==QuickRaceMode::drag?1:2;
            require(local_world->load_local(root,event,0,mode,local_options,error),error);
            require(local_scene->load_local(root,event,0,mode,local_options,local_world->models(),error),error);
            std::array<GamepadFrame,2> input;for(auto& pad:input)pad.pads[0].connected=true;
            for(unsigned i=0;i<500&&!local_world->session().timing().go_valid;++i)require(local_world->advance(input,error),error);
            require(local_world->session().timing().go_valid,"local finish diagnostic did not reach real GO");
            for(auto& pad:input)pad.pads[0].buttons=gamepad_button_bit(GamepadButton::trigger_right);
            for(unsigned i=0;i<60;++i)require(local_world->advance(input,error),error);
            const auto check_local=[&](double first,double second) {
                for(const auto layout:{SplitLayout::stacked,SplitLayout::side_by_side})for(unsigned player:{1U,0U}) {
                    RaceGpuFrame gpu;RaceHudFrame actual,expected;
                    require(local_scene->split_frame(local_world->session(),player,gpu,actual,error,false,layout),error);
                    const auto& result=local_world->session().timing().racers[player];
                    if(result.status==RacerStatus::not_finished) {
                        const auto active_view=split_viewport(gpu.commands.width,gpu.commands.height,layout,player);
                        build_race_hud_frame(local_world->session(),local_scene->atlas(),active_view.width,
                                             active_view.height,expected,false,player);
                        require(actual.quads>=expected.quads&&expected.quads>0&&
                                !std::memcmp(actual.vertices.data(),expected.vertices.data(),expected.quads*4*sizeof(TrackVertex)),
                                "unfinished split human keeps normal instruments while the other owns Race Over");
                        continue;
                    }
                    const auto viewport=split_viewport(gpu.commands.width,gpu.commands.height,layout,player);
                    const auto style=layout==SplitLayout::stacked?(player?RaceOverLayout::split_bottom:RaceOverLayout::split_top):RaceOverLayout::split_side;
                    const std::string text=mode==QuickRaceMode::drag?
                        result.status==RacerStatus::normal_completion?drag_finish_message(local_scene->atlas(),unsigned(result.position),result.finish_ticks):
                        result.status==RacerStatus::totalled?"Totaled!":"Blown Engine":"Race Over";
                    build_race_over_frame(local_scene->atlas(),player?second:first,viewport.width,viewport.height,expected,text,style);
                    const bool equal=actual.quads==expected.quads&&
                        !std::memcmp(actual.vertices.data(),expected.vertices.data(),actual.quads*4*sizeof(TrackVertex));
                    if(!equal)++failures;
                    std::cout<<(equal?"PASS":"FAIL")<<" local finish mode="<<unsigned(mode)<<" player="<<player<<" layout="<<unsigned(layout)<<" seconds="<<(player?second:first)<<'\n';
                }
            };
            RaceWorldContactProbe::terminal(local_world->session(),0,RacerStatus::normal_completion,1,171400);
            check_local(0,0);
            local_scene->advance_presentation(.15);
            RaceWorldContactProbe::terminal(local_world->session(),1,mode==QuickRaceMode::drag?RacerStatus::totalled:RacerStatus::normal_completion,2);
            check_local(.15,0);
            local_scene->advance_presentation(.4);check_local(.55,.4);
            RaceFinishHold hold;require(!hold.observe(local_world->session().timing(),2),"real two-human ending must retain the final driver's message");
            bool released=false;
            for(unsigned i=0;i<400&&!released;++i) {
                require(local_world->advance(input,error),error);
                released=hold.observe(local_world->session().timing(),2);
                hold.advance(local_world->session().world_frame_seconds());
            }
            require(released,"real two-human world clock must automatically release joint results after terminal drive");
            const auto before=local_world->session().timing().racers;
            for(unsigned i=0;i<8;++i)require(local_world->advance(input,error),error);
            for(unsigned i=0;i<2;++i)require(local_world->session().timing().racers[i].finish_ticks==before[i].finish_ticks,
                                           "post-finish two-human continuation changed a result");
            if(mode==QuickRaceMode::drift) {
                auto& first=const_cast<RaceStyleItem&>(local_world->session().style().item(0));
                auto& second=const_cast<RaceStyleItem&>(local_world->session().rival(0).style().item(0));
                first.total=100;first.best=11;first.longest=1.1F;
                second.total=200;second.best=77;second.longest=7.7F;
            }else {
                RaceWorldContactProbe::terminal(local_world->session(),0,RacerStatus::normal_completion,1,171400);
                RaceWorldContactProbe::terminal(local_world->session(),1,RacerStatus::normal_completion,2,374720);
            }
            const auto values=post_race_two_human_report(local_world->session(),{},{{"Tiburon","Golf"}});
            RaceResultStatistics human_statistics;
            if(mode==QuickRaceMode::drift) {
                RaceWorldContactProbe::lap(local_world->session(),0,10000);
                RaceWorldContactProbe::lap(local_world->session(),1,12000);
                human_statistics.observe(local_world->session());
                verify_statistics_menu(local_world->session(),human_statistics,{{1,0}});
                require(human_statistics.values(local_world->session(),"Tiburon",0).at("AllLapTimes_1")=="100"&&
                        human_statistics.values(local_world->session(),"Golf",1).at("AllLapTimes_1")=="200",
                        "per-lap Drift statistics retain each human's own score publication");
                require(values.at("PlayerRow")=="2"&&values.at("Time1")=="200"&&values.at("Time2")=="100"&&
                        values.at("CarModel_1")=="Golf"&&values.at("CarModel_2")=="Tiburon",
                        "two-human Drift sorts real scores even when the first finisher loses");
            }else {
                // Native586580 reserves blank columns for minutes and '+'.
                const auto compact=[](std::string text){text.erase(std::remove(text.begin(),text.end(),' '),text.end());return text;};
                require(compact(values.at("Time1"))=="42.85"&&compact(values.at("Time2"))=="1:33.68"&&compact(values.at("Lead2"))=="+50.83",
                        "joint original Drag example retains leader absolute42.85 and follower gap50.83");
            }
            require(!values.at("Name1").empty()&&!values.at("Name2").empty()&&values.at("Name3").empty()&&values.at("Name4").empty(),
                    "joint Quick Race table must contain two real humans, not a campaign/virtual roster");
            require(!values.count("RewardBlurbString")&&!values.count("CollectReward")&&!values.count("CareerStyleBase"),
                    "joint Quick Race result must not create a campaign bank or reward queue");
            require(local_scene->load_finish(root,local_world->session(),0,error),error);
            require(local_scene->finish_scene_id()==2322,"two local humans use the shared original2322 scene, including Drift score defeat");
            for(const auto& native:kNativeFinishSlots)if(native.humans==2&&native.first==0)
                for(unsigned slot=0;slot<2;++slot)require(local_scene->finish_car_slot(unsigned(native.actors[slot]))==slot,
                    "local ending actor slots match executed original finishing order");
            SupportedRaceSelection selected_course;require(supported_race_selection(event,0,selected_course),"ending diagnostic course missing");
            auto reference_finish=std::make_unique<StartCrowd>();
            require(reference_finish->load_finish_camera(posix_file_system(),root,2322,selected_course.region,local_world->session().course(),error),error);
            for(double seconds:{0.,2.}) {
                RaceGpuFrame ending;
                require(local_scene->finish_frame(local_world->session(),seconds,ending,error)&&
                        validate_recorded_commands(ending.commands,error),error);
                require(ending.car_draws&&ending.track_draws,"joint original NIS emitted no physical cars/road");
                if(reference_finish) {
                    OrbitCamera original;require(reference_finish->finish_camera(seconds,original,640,480),"original common two-human camera sample missing");
                    original.focal_x_override=original.focal_y_override*ending.commands.height/ending.commands.width;
                    CameraFrame camera;require(make_camera_frame(original,ending.commands.width,ending.commands.height,camera,error),error);
                    const auto& actual=ending.commands.frame;
                    const std::array<Vec3,4> expected{{camera.eye,camera.right,camera.up,camera.forward}};
                    const std::array<const float*,4> received{{actual.eye,actual.right,actual.up,actual.forward}};
                    for(unsigned axis=0;axis<4;++axis)for(unsigned c=0;c<3;++c) {
                        const std::array<double,3> value{{expected[axis].x,expected[axis].y,expected[axis].z}};
                        require(std::abs(received[axis][c]-value[c])<.001,"joint GPU camera must use the original common two-human clip");
                    }
                    require(std::abs(actual.projection[1]-camera.focal_y)<1e-6,"joint two-human lens must match authored camera");
                }
            }
            local_scene->clear_finish();
            RaceWorldContactProbe::terminal(local_world->session(),0,RacerStatus::normal_completion,2,374720);
            RaceWorldContactProbe::terminal(local_world->session(),1,RacerStatus::normal_completion,1,171400);
            if(mode==QuickRaceMode::drift) {
                const_cast<RaceStyleItem&>(local_world->session().style().item(0)).total=500;
                const_cast<RaceStyleItem&>(local_world->session().rival(0).style().item(0)).total=100;
                verify_statistics_menu(local_world->session(),human_statistics,{{0,1}});
                require(post_race_two_human_report(local_world->session(),{},{{"Tiburon","Golf"}}).at("PlayerRow")=="1",
                        "Drift's later finisher may win the score table independently of NIS slots");
            }
            require(local_scene->load_finish(root,local_world->session(),2,error),error);
            require(local_scene->finish_scene_id()==2322,"split NIS does not vary with score/place/playlist index");
            for(const auto& native:kNativeFinishSlots)if(native.humans==2&&native.first==1)
                for(unsigned slot=0;slot<2;++slot)require(local_scene->finish_car_slot(unsigned(native.actors[slot]))==slot,
                    "first-finishing P2 keeps original NIS slot zero; P1 is not forced to the front");
            RaceGpuFrame reversed;
            require(local_scene->finish_frame(local_world->session(),2,reversed,error)&&validate_recorded_commands(reversed.commands,error)&&
                    reversed.car_draws&&reversed.track_draws,error);
        }
        verify_authoritative_network_statistics(root);
        std::cout<<(failures?"FAIL":"PASS")<<" network camera snapshots="<<snapshots<<" views=6 failures="<<failures<<'\n';
        return failures?1:0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
