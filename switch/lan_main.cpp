#include <switch.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "deko_renderer.h"
#include "lan_menu.h"
#include "nfsu/lan_scene.h"
#include "nfsu/lan_link.h"
#include "nfsu/stock_view.h"
#include "nfsu/screen_text.h"
#include "nfsu/race_audio.h"
#include "nfsu/startup_journal.h"
#include "nfsu/texture.h"
#include <malloc.h>
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

using namespace nfsu;
namespace {
StartupJournal* diagnostic_journal=nullptr;
void trace(const char* stage,const char* note="none") noexcept {
    if(!diagnostic_journal)return;
    char safe[241]{};
    for(unsigned i=0;note[i]&&i+1<sizeof safe;++i){const auto c=static_cast<unsigned char>(note[i]);safe[i]=c>=32&&c<=126?char(c):'?';}
    const auto heap=mallinfo();u64 total=0,used=0;
    const auto total_rc=svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
    const auto used_rc=svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    char detail[640];std::snprintf(detail,sizeof detail,
        "note=%s ticks=%llu heap_used=%llu heap_free=%llu process_total=%llu process_used=%llu total_rc=%lu used_rc=%lu nxlink_host=%08lx",
        safe,(unsigned long long)armGetSystemTick(),(unsigned long long)heap.uordblks,
        (unsigned long long)heap.fordblks,(unsigned long long)total,(unsigned long long)used,
        (unsigned long)total_rc,(unsigned long)used_rc,(unsigned long)__nxlink_host.s_addr);
    if(!diagnostic_journal->mark(stage,detail)){
        std::fprintf(stderr,"LAN_TRACE_WRITE_FAILED %s\n",diagnostic_journal->error());std::fflush(stderr);
    }
}
void require(bool value,const std::string& error){if(!value){trace("lan_error",error.c_str());throw std::runtime_error(error);}}
std::uint64_t micros(){return armTicksToNs(armGetSystemTick())/1000;}
class LanScreen {
public:
    ~LanScreen(){close();}
    void close(){
        if(opened_){framebufferClose(&framebuffer_);opened_=false;}
    }
    void show(const std::vector<std::string>& lines){
        constexpr unsigned width=1280,height=720;
        if(!opened_){
            require(R_SUCCEEDED(framebufferCreate(&framebuffer_,nwindowGetDefault(),width,height,PIXEL_FORMAT_RGBA_8888,2)),
                    "LAN framebuffer unavailable");
            opened_=true;
            if(R_FAILED(framebufferMakeLinear(&framebuffer_))){close();throw std::runtime_error("LAN framebuffer cannot map pixels");}
        }
        u32 stride=0;
        auto* pixels=static_cast<unsigned char*>(framebufferBegin(&framebuffer_,&stride));
        require(pixels!=nullptr,"LAN framebuffer cannot begin");
        ScreenImage image{pixels,width,height,stride};
        constexpr ScreenColor background{12,16,22,255};
        fill_screen_rect(image,0,0,width,height,background);
        for(unsigned i=0;i<lines.size();++i)
            draw_screen_text(image,54,60+i*34,lines[i].substr(0,92),2,{230,239,250,255},background);
        framebufferEnd(&framebuffer_);
    }
private:
    Framebuffer framebuffer_{};
    bool opened_=false;
};
struct RoomExit {};
struct InputTiming {
    struct Sample {std::uint32_t sequence=0;std::uint64_t sent_at=0;};
    struct Measurement {
        std::uint32_t last_sequence=0;
        std::uint64_t count=0,total_us=0,max_us=0;
        double mean_ms()const{return count?double(total_us)/double(count)*.001:0;}
        double max_ms()const{return double(max_us)*.001;}
    } received,presented;
    std::array<Sample,128> sent{};
    void send(std::uint32_t sequence,std::uint64_t now){sent[sequence%sent.size()]={sequence,now};}
    void measure(Measurement& result,std::uint32_t sequence,std::uint64_t now)const{
        const auto& sample=sent[sequence%sent.size()];
        if(sequence==result.last_sequence||sample.sequence!=sequence||!sample.sent_at)return;
        const auto delay=now-sample.sent_at;
        result.last_sequence=sequence;++result.count;result.total_us+=delay;
        result.max_us=std::max(result.max_us,delay);
    }
};
GamepadState sample(const PadState& pad){
    GamepadState input;input.connected=padIsConnected(&pad);input.buttons=gamepad_buttons_from_npad(padGetButtons(&pad));
    auto left=padGetStickPos(&pad,0),right=padGetStickPos(&pad,1);
    input.axes[0]=left.x;input.axes[1]=left.y;input.axes[2]=right.x;input.axes[3]=right.y;return input;
}
std::string local_address(){
    in_addr address{};address.s_addr=std::uint32_t(gethostid());
    return inet_ntoa(address);
}
bool choose(PadState& pad,LanScreen& screen,std::string& ip){
    const std::string address=local_address();
    while(appletMainLoop()){
        screen.show({"MULTIPLAYER / LAN","","Your IPv4: "+address,"",
                     "A: Create room","X: Join room by IPv4","B: Back"});
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
        if(down&(HidNpadButton_B|HidNpadButton_Plus))throw RoomExit{};
        if(down&HidNpadButton_A)return true;
        if(down&HidNpadButton_X){
            SwkbdConfig keyboard{};require(R_SUCCEEDED(swkbdCreate(&keyboard,0)),"LAN keyboard unavailable");
            swkbdConfigMakePresetDefault(&keyboard);swkbdConfigSetHeaderText(&keyboard,"Host IPv4 address");
            swkbdConfigSetStringLenMax(&keyboard,15);char text[32]{};
            const auto result=swkbdShow(&keyboard,text,sizeof(text));swkbdClose(&keyboard);
            if(R_SUCCEEDED(result)&&text[0]){ip=text;return false;}
        }
    }
    throw RoomExit{};
}
LanRide chosen_ride(const std::string& car,const std::map<std::string,CareerRide>& rides){
    LanRide result;result.car=bin_hash(car);
    const auto installed=rides.find(car);
    if(installed!=rides.end())result.installed=installed->second.installed;
    return result;
}
void room_lobby(PadState& pad,LanScreen& screen,LanLink& link,bool host,
                const std::vector<std::string>& cars,const std::map<std::string,CareerRide>& rides,
                const LanGarageEditor& edit){
    LanRaceOptions options;unsigned selected=0;
    unsigned car=unsigned(std::distance(cars.begin(),std::find(cars.begin(),cars.end(),"CIVIC")))%cars.size();
    const std::string address=local_address();std::string error;
    const auto poll=[&]{require(link.poll(micros(),error),error);};
    const auto update_host=[&](bool launch){
        options.rides[0]=chosen_ride(cars[car],rides);
        require(link.configure_race(options,launch,error),error);
    };
    if(host)update_host(false);
    while(appletMainLoop()){
        poll();
        if(link.race_launched())return;
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
        if(down&(HidNpadButton_B|HidNpadButton_Plus)){link.leave();throw RoomExit{};}
        if(host){
            if(down&HidNpadButton_Up)selected=(selected+3)%4;
            if(down&HidNpadButton_Down)selected=(selected+1)%4;
        }
        const bool locked=!host&&link.guest_ride_ready();
        const bool left=down&HidNpadButton_Left,right=down&HidNpadButton_Right;
        if(!locked&&left!=right){
            const auto cycle=[left](unsigned value,unsigned count){
                return (value+(left?count-1:1))%count;
            };
            if(!host||selected==3)car=cycle(car,unsigned(cars.size()));
            else if(selected==0)options.course=static_cast<std::uint8_t>(cycle(options.course,std::size(kLanCourses)));
            else if(selected==1)options.laps=static_cast<std::uint8_t>(cycle(options.laps-1,6)+1);
            else options.tier=static_cast<std::uint8_t>(cycle(options.tier,std::size(kLanPerformance)));
            if(host)update_host(false);
        }
        if(!locked&&edit&&(down&HidNpadButton_Y)){
            screen.close();edit(cars[car],options.tier,poll);
            if(host)update_host(false);
        }
        if((down&HidNpadButton_A)&&link.paired()){
            if(host&&link.guest_ride_ready()){update_host(true);return;}
            if(!host&&!locked)require(link.confirm_guest_ride(chosen_ride(cars[car],rides),error),error);
        }
        if(!host)options=link.race_options();
        const auto row=[&](unsigned index,const std::string& text){
            return std::string(host&&selected==index?"> ":"  ")+text;
        };
        const auto* peer=find_lan_car_name(cars,link.race_options().rides[host?1:0].car);
        std::string state="Waiting for peer...";
        if(link.paired()){
            state=host?(link.guest_ride_ready()?"Guest ready":"Guest choosing a car"):
                (locked?"Ready. Waiting for host":"Choose your car and press A when ready");
            if(peer&&(!host||link.guest_ride_ready()))state+=" / Other car: "+*peer;
        }
        screen.show({host?"LAN / HOST ROOM":"LAN / JOIN ROOM","Your IPv4: "+address,state,
            "",row(0,std::string("Course: ")+kLanCourses[options.course].name),
            row(1,"Laps: "+std::to_string(options.laps)),
            row(2,std::string("Performance: ")+kLanPerformance[options.tier]),
            row(3,"Your car: "+cars[car]),"",
            host?"D-pad: Setup   A: Start race   B: Back":"Left/Right: Car   A: Ready   B: Back",
            edit&&!locked?"Y: Customize ride":""});
    }
    throw RoomExit{};
}
void run_lan(const std::string& root,PadState& pad,bool selected,bool host,std::string ip,bool write_logs,
             const std::map<std::string,CareerRide>& rides,const LanGarageEditor& edit){
    StartupJournal journal;char path[96]{};
    for(unsigned i=0;write_logs&&i<10000;++i){
        std::snprintf(path,sizeof path,"sdmc:/nfsu-lan-runtime-%04u.log",i);
        if(journal.open(path)){diagnostic_journal=&journal;break;}
        if(errno!=EEXIST)break;
    }
    struct JournalScope {~JournalScope(){diagnostic_journal=nullptr;}} journal_scope;
    LanScreen screen;
    char timeline_summary[512]{};
    trace("lan_run_enter",NFSU_RACE_BUILD_TAG);
    try {
        std::string error;LanSocket bootstrap;require(bootstrap.open(0,error),error);
        if(!selected)host=choose(pad,screen,ip);
        trace("lan_role_ready",host?"host":ip.c_str());
        LanLink link;const auto opened=micros();auto nonce=std::uint32_t(opened)^std::uint32_t(opened>>32);if(!nonce)nonce=1;
        require(host?link.host(kLanPort,nonce,opened,error):link.join(ip,kLanPort,nonce,opened,error),error);
        std::vector<std::string> cars;
        require(load_lan_car_names(root,cars,error),error);
        room_lobby(pad,screen,link,host,cars,rides,edit);
        const auto options=link.race_options();const unsigned local=host?0:1;
        screen.show({"LAN / LOADING",kLanCourses[options.course].name,
                     std::to_string(options.laps)+" laps",kLanPerformance[options.tier]});
        LanRaceWorld world;trace("lan_world_begin");require(world.load(root,error,options),error);trace("lan_world_ready");
        LanRaceView view;require(view.open(world.session(),world.cameras(local),local,error,&world.host_hull()),error);
        auto latest=world.snapshot(),presented=latest;require(view.apply(latest,1./60,error),error);
        LanScene scene;trace("lan_scene_begin");require(scene.load(root,options,world.models(),local,error),error);trace("lan_scene_ready");
        RaceGpuFrame frame;RaceHudFrame hud;require(scene.frame(view,frame,hud,error),error);
        screen.show({"LAN / READY","Waiting for the other console to finish loading...","+ cancels"});
        // Release the software framebuffer before opening a deko swapchain.
        screen.close();
        DekoRenderer renderer;trace("lan_renderer_begin");
        require(renderer.open(frame.commands,error,true,scene.draw_capacity(),false,true,false,0,&scene.atlas()),error);
        trace("lan_renderer_ready");audio::RaceAudioService sound;std::string audio_note;
        const auto audio_core=svcGetCurrentProcessorNumber()==2?1:2;
        const auto levels=lan_performance_levels(options.tier);
        const auto& remote_induction=view.session().rival(0).spec().induction;
        const std::vector<audio::RaceRemoteEngineBinding> remote_audio{{world.models()[1-local],
            levels.engine_a,levels.induction,std::any_of(remote_induction.begin(),remote_induction.end(),
                [](float value){return value>0.F;})}};
        const bool audio_started=sound.start(root,audio_note,{},[audio_core]{
            return R_SUCCEEDED(svcSetThreadCoreMask(threadGetCurHandle(),audio_core,1U<<audio_core))&&
                R_SUCCEEDED(svcSetThreadPriority(threadGetCurHandle(),0x30));
        },true,world.models()[local],levels.engine_a,levels.nitrous,levels.tyres,remote_audio,levels.induction,0,0);
        trace("lan_audio_result",audio_started?"started":audio_note.c_str());
        link.set_ready();trace("lan_room_ready");
        LanSnapshotBuffer snapshot_buffer;InputTiming input_timing;
        std::uint64_t last=micros(),debt=0,stats_at=last,simulations=0,last_presentation=last,last_audio_step=~std::uint64_t(0);
        double simulation_ms=0,sim_max=0;bool was_connected=false;std::uint32_t previous_buttons=0;
        unsigned diagnostic_frames=0;bool first_simulation=true;
        while(appletMainLoop()){
            padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;
            const auto now=micros();auto input=sample(pad);
            if(diagnostic_frames<2){trace("lan_poll_begin");}
            require(link.poll(now,error),error);
            if(diagnostic_frames<2){trace("lan_poll_ready");}
            if(link.connected()){
                if(!was_connected){trace("lan_connected",host?"host":"guest");last=now;debt=0;was_connected=true;std::printf("LAN_CONNECTED role=%s\n",host?"host":"guest");}
                if(host&&link.has_remote_input()){
                    const auto elapsed=now-last;last=now;
                    // No giant catch-up on system resume. The peer timeout is
                    // checked above; ordinary frame debt is never discarded.
                    require(elapsed<1000000,"LAN host suspended; restart the room");debt+=elapsed*60;
                    unsigned steps=0;
                    while(debt>=1000000&&steps++<8){
                        debt-=1000000;
                        if(world.session().timing().outcome==RaceOutcome::running){
                            std::array<GamepadFrame,2> pads{};pads[0].pads[0]=input;pads[1].pads[0]=link.remote_input(now);
                            if(first_simulation)trace("lan_simulation_begin");
                            const auto begin=micros();require(world.advance(pads,error),error);
                            if(first_simulation){trace("lan_simulation_ready");first_simulation=false;}
                            const double cost=double(micros()-begin)*.001;simulation_ms+=cost;sim_max=std::max(sim_max,cost);++simulations;
                            latest=world.snapshot();
                            if(latest.outcome==unsigned(RaceOutcome::running))require(link.publish(latest,error),error);
                        }
                    }
                    // Retry the final state so a lost UDP packet cannot hide results.
                    if(latest.outcome==unsigned(RaceOutcome::finished)&&steps)
                        require(link.publish(latest,error),error);
                }else if(!host){
                    // One sample per displayed frame. A 16667 us timer gate
                    // can skip a frame at a nominal 60 Hz even on a perfect link.
                    std::uint32_t sent_sequence=0;
                    require(link.send_input(input,error,&sent_sequence),error);input_timing.send(sent_sequence,now);
                    LanRaceFrame incoming;
                    if(link.take_snapshot(incoming)&&incoming.step>latest.step){
                        latest=incoming;snapshot_buffer.push(latest,link.input_ack());
                        input_timing.measure(input_timing.received,link.input_ack(),now);
                    }
                }else last=now;
            }else last=now;
            const auto camera=gamepad_button_bit(GamepadButton::face_north);
            const auto rear=gamepad_button_bit(GamepadButton::stick_right);
            const double presentation_dt=std::clamp(double(now-last_presentation)*.000001,.001,.1);last_presentation=now;
            std::uint32_t presented_ack=0;
            if(host)presented=latest;
            const bool guest_sample=!host&&snapshot_buffer.sample(presentation_dt,presented,presented_ack);
            require(view.apply(presented,presentation_dt,error,(input.buttons&rear)!=0,(input.buttons&camera)&&!(previous_buttons&camera)),error);
            previous_buttons=input.buttons;
            if(audio_started&&was_connected&&presented.step!=last_audio_step){sound.on_tick(audio::race_audio_snapshot(view.session()));last_audio_step=presented.step;}
            if(diagnostic_frames<2){trace("lan_frame_begin");}
            require(scene.frame(view,frame,hud,error),error);
            if(diagnostic_frames<2){trace("lan_update_begin");}
            require(renderer.update(frame.commands,error),error);
            if(diagnostic_frames<2){trace("lan_present_begin");}
            require(renderer.present(error,nullptr,nullptr,&hud),error);
            if(guest_sample)input_timing.measure(input_timing.presented,presented_ack,micros());
            if(diagnostic_frames<2){trace("lan_present_ready");++diagnostic_frames;}
            if(now-stats_at>=5000000){
                std::printf("LAN_METRICS role=%s step=%u sent=%llu received=%llu invalid=%llu sim_mean_ms=%.3f sim_max_ms=%.3f\n",host?"host":"guest",latest.step,
                    (unsigned long long)link.sent_bytes(),(unsigned long long)link.received_bytes(),(unsigned long long)link.invalid_packets(),
                    simulations?simulation_ms/double(simulations):0,sim_max);
                if(!host){
                    std::snprintf(timeline_summary,sizeof timeline_summary,
                        "LAN_TIMELINE received_step=%u presented_step=%u underrun_frames=%llu ack_samples=%llu ack_mean_ms=%.3f ack_max_ms=%.3f presented_samples=%llu presented_mean_ms=%.3f presented_max_ms=%.3f",
                        latest.step,presented.step,(unsigned long long)snapshot_buffer.underrun_frames(),
                        (unsigned long long)input_timing.received.count,input_timing.received.mean_ms(),input_timing.received.max_ms(),
                        (unsigned long long)input_timing.presented.count,input_timing.presented.mean_ms(),input_timing.presented.max_ms());
                    std::puts(timeline_summary);
                }
                std::fflush(stdout);stats_at=now;
            }
        }
        trace("lan_shutdown_begin");link.leave();sound.stop();require(renderer.close(error),error);trace("lan_shutdown_ready");
    }catch(const RoomExit&){
        trace("lan_cancelled");
    }catch(const std::exception& e){
        trace("lan_exception",e.what());
        while(appletMainLoop()){
            screen.show({"LAN / ROOM CLOSED","",e.what(),"","B or +: Back"});
            padUpdate(&pad);
            if(padGetButtonsDown(&pad)&(HidNpadButton_B|HidNpadButton_Plus))break;
        }
    }
    if(diagnostic_journal){
        if(timeline_summary[0])journal.mark("lan_timeline",timeline_summary,false);
        if(!journal.close())std::fprintf(stderr,"LAN_TRACE_CLOSE_FAILED %s\n",journal.error());
    }
}
}

void run_switch_lan(const std::string& root,PadState& pad,bool write_logs,
                    const std::map<std::string,CareerRide>& rides,const LanGarageEditor& edit){
    run_lan(root,pad,false,false,{},write_logs,rides,edit);
}

#ifdef NFSU_LAN_STANDALONE
int main(int argc,char** argv){
    std::string root="sdmc:/nfsu",ip;bool selected=false,host=false;
    for(int i=1;i<argc;++i){
        const std::string argument=argv[i];
        if(argument=="--host"){host=true;selected=true;}
        else if(argument=="--join"&&i+1<argc){ip=argv[++i];host=false;selected=true;}
        else if(argument=="--root"&&i+1<argc)root=argv[++i];
        else {std::fprintf(stderr,"Unknown LAN argument: %s\n",argument.c_str());return 1;}
    }
    if(R_FAILED(romfsInit()))return 1;
    struct RomfsGuard{~RomfsGuard(){romfsExit();}} romfs;
    LanSocket bootstrap;std::string error;
    const int log=__nxlink_host.s_addr&&bootstrap.open(0,error)?nxlinkStdio():-1;
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    run_lan(root,pad,selected,host,ip,true,{},{});
    if(log>=0)::close(log);
    return 0;
}
#endif
