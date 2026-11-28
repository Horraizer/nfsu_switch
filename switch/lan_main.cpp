#include <switch.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "deko_renderer.h"
#include "lan_menu.h"
#include "race_input.h"
#include "drag_input.h"
#include "gyro_steering.h"
#include "split_diagnostics.h"
#include "nfsu/lan_scene.h"
#include "nfsu/frontend.h"
#include "nfsu/frontend_music.h"
#include "nfsu/race_result.h"
#include "nfsu/race_finish_flow.h"
#include "race_presentation_clock.h"
#include <future>
#include <chrono>
#include "nfsu/deko_scene_target.h"
#include "nfsu/memory_budget.h"
#include "nfsu/lan_link.h"
#include "nfsu/lan_frame_stats.h"
#include "nfsu/lan_results.h"
#include "nfsu/stock_view.h"
#include "nfsu/screen_text.h"
#include "nfsu/race_audio.h"
#include "nfsu/startup_journal.h"
#include "nfsu/texture.h"
#include <malloc.h>
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>
#include <type_traits>
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
#include "multiplayer_finish.inc"
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
    if(installed!=rides.end()){result.installed=installed->second.installed;result.turbo_brand=installed->second.turbo_brand;}
    return result;
}
using LocalPads=std::array<PadState,2>;
using LocalRides=std::array<std::map<std::string,CareerRide>,2>;
constexpr u32 local_pad_styles=HidNpadStyleSet_NpadStandard&~HidNpadStyleTag_NpadHandheld;
u64 local_buttons(u32 style,u64 buttons) {
    const bool left=style==HidNpadStyleTag_NpadJoyLeft;
    if(!left&&style!=HidNpadStyleTag_NpadJoyRight)return buttons;
    const auto has=[buttons](u64 bit){return (buttons&bit)!=0;};
    u64 out=0;
    if(has(left?HidNpadButton_Down:HidNpadButton_X))out|=HidNpadButton_A;
    if(has(left?HidNpadButton_Left:HidNpadButton_A))out|=HidNpadButton_B;
    if(has(left?HidNpadButton_Right:HidNpadButton_Y))out|=HidNpadButton_X;
    if(has(left?HidNpadButton_Up:HidNpadButton_B))out|=HidNpadButton_Y;
    if(has(HidNpadButton_AnySR))out|=HidNpadButton_ZR;
    if(has(HidNpadButton_AnySL))out|=HidNpadButton_ZL;
    if(has(HidNpadButton_Plus|HidNpadButton_Minus))out|=HidNpadButton_Plus;
    if(has(HidNpadButton_StickL|HidNpadButton_StickR))out|=HidNpadButton_StickR;
    const u64 up=left?HidNpadButton_StickLUp:HidNpadButton_StickRUp;
    const u64 down=left?HidNpadButton_StickLDown:HidNpadButton_StickRDown;
    const u64 right=left?HidNpadButton_StickLRight:HidNpadButton_StickRRight;
    const u64 leftward=left?HidNpadButton_StickLLeft:HidNpadButton_StickRLeft;
    if(has(left?right:leftward))out|=HidNpadButton_Up;
    if(has(left?leftward:right))out|=HidNpadButton_Down;
    if(has(left?down:up))out|=HidNpadButton_Right;
    if(has(left?up:down))out|=HidNpadButton_Left;
    return out;
}
u64 local_buttons(const PadState& pad,u64 buttons) { return local_buttons(padGetStyleSet(&pad),buttons); }
u64 local_garage_buttons(const PadState& pad,u64 buttons) {
    auto folded=local_buttons(pad,buttons);
    const auto style=padGetStyleSet(&pad);
    if(style==HidNpadStyleTag_NpadJoyLeft||style==HidNpadStyleTag_NpadJoyRight) {
        if(buttons&HidNpadButton_AnySL)folded|=HidNpadButton_L;
        if(buttons&HidNpadButton_AnySR)folded|=HidNpadButton_R;
    }
    return folded;
}
HidAnalogStickState local_garage_stick(const PadState& pad) {
    const auto style=padGetStyleSet(&pad);
    if(style==HidNpadStyleTag_NpadJoyLeft) {
        const auto stick=padGetStickPos(&pad,0);
        return {std::clamp(-stick.y,kAxisMin,kAxisMax),stick.x};
    }
    if(style==HidNpadStyleTag_NpadJoyRight) {
        const auto stick=padGetStickPos(&pad,1);
        return {stick.y,std::clamp(-stick.x,kAxisMin,kAxisMax)};
    }
    return padGetStickPos(&pad,0);
}
GamepadState local_race_input(u32 style,u64 buttons,HidAnalogStickState left,HidAnalogStickState right,bool drag) {
    auto folded=local_buttons(style,buttons);
    if(drag&&(style==HidNpadStyleTag_NpadJoyLeft||style==HidNpadStyleTag_NpadJoyRight)) {
        // The solo horizontal Joy-Con has no right stick or R/L pair.
        if(folded&HidNpadButton_X)folded=(folded&~u64(HidNpadButton_X))|HidNpadButton_R;
        if(folded&HidNpadButton_Y)folded=(folded&~u64(HidNpadButton_Y))|HidNpadButton_L;
    }
    GamepadState input;
    input.buttons=switch_race_buttons(folded);
    input.axes[0]=left.x;input.axes[1]=left.y;input.axes[2]=right.x;input.axes[3]=right.y;
    if(style==HidNpadStyleTag_NpadJoyLeft||style==HidNpadStyleTag_NpadJoyRight) {
        const auto steer=style==HidNpadStyleTag_NpadJoyLeft?
            std::clamp(-input.axes[1],kAxisMin,kAxisMax):input.axes[3];
        // Navigation uses folded stick directions. In the race preserve
        // the stick's analogue range and keep the absent right stick neutral.
        input.buttons&=~(gamepad_button_bit(GamepadButton::dpad_left)|gamepad_button_bit(GamepadButton::dpad_right)|
                          gamepad_button_bit(GamepadButton::dpad_up)|gamepad_button_bit(GamepadButton::dpad_down));
        input.axes[0]=steer;
        input.axes[1]=input.axes[2]=input.axes[3]=0;
    }
    return input;
}
GamepadState local_drag_input(u32 style,u64 buttons,HidAnalogStickState left,HidAnalogStickState right) {
    return local_race_input(style,buttons,left,right,true);
}
bool choose_local_controllers(LocalPads& pads,std::string& error,bool use_assigned=false) {
    error.clear();
    const HidNpadIdType ids[]={HidNpadIdType_No1,HidNpadIdType_No2};
    Result rc=hidSetSupportedNpadStyleSet(local_pad_styles);
    if(R_SUCCEEDED(rc))rc=hidSetSupportedNpadIdType(ids,2);
    if(R_SUCCEEDED(rc))rc=hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);
    HidLaControllerSupportArg settings{};
    hidLaCreateControllerSupportArg(&settings);
    settings.hdr.player_count_min=2;
    settings.hdr.player_count_max=2;
    HidLaControllerSupportResultInfo result{};
    if(R_SUCCEEDED(rc)) {
        if(!use_assigned)rc=hidLaShowControllerSupport(&result,&settings);
        for(unsigned i=0;i<pads.size();++i) {
            padInitialize(&pads[i],ids[i]);
            padUpdate(&pads[i]);
        }
        const bool assigned=padIsConnected(&pads[0])&&padIsConnected(&pads[1]);
        if(assigned&&R_SUCCEEDED(rc)&&(use_assigned||result.player_count==2))return true;
    }
    if(rc==MAKERESULT(Module_Libnx,LibnxError_LibAppletBadExit)) {
        error="Controller selection closed (2345-0046).";
    } else if(R_FAILED(rc)) {
        char message[96];
        std::snprintf(message,sizeof message,"Controller selection closed (%04u-%04u). Press A to retry.",
                      2000+unsigned(R_MODULE(rc)),unsigned(R_DESCRIPTION(rc)));
        error=message;
    } else error="Assign two controllers in HOME > Controllers.";
    return false;
}

bool local_lobby(const std::string& root,LocalPads& pads,LanScreen& screen,
                 const std::map<std::string,CareerRide>& rides,LanRaceOptions& options,
                 FreeDriveCourse& course,QuickRaceMode& selected_mode,unsigned& traffic_density,
                 std::array<bool,2>& gyro_enabled,SplitLayout& layout,SplitEffects& effects,
                 const LanGarageEditor& edit,LocalRides& edited_rides,std::string& error) {
    auto catalog=std::make_unique<CareerCatalog>();
    std::vector<std::string> cars;
    if(!catalog->load_free_drive(root,error)||!load_lan_car_names(root,cars,error))return false;
    constexpr std::array modes{QuickRaceMode::circuit,QuickRaceMode::sprint,QuickRaceMode::drag,QuickRaceMode::drift};
    std::array<std::vector<unsigned>,modes.size()> courses;
    for(unsigned i=0;i<modes.size();++i)courses[i]=catalog->quick_courses(modes[i]);
    require(std::none_of(courses.begin(),courses.end(),[](const auto& choices){return choices.empty();})&&
        !cars.empty(),"Quick Race roster unavailable");
    enum Row { mode_row,track_row,laps_row,tier_row,car1_row,car2_row,traffic_row,
               gyro1_row,gyro2_row,layout_row,particles_row,reflection_row,start_row,row_count };
    unsigned row=mode_row,mode=0,track=0,laps=2,tier=0;
    constexpr const char* traffic_levels[]={"Off","Low","Medium","High"};
    std::array<unsigned,2> car_indices{{0,unsigned(cars.size()>1)}};
    const auto label=[&](unsigned i,const std::string& line){return std::string(row==i?"> ":"  ")+line;};
    const auto cycle=[](unsigned value,unsigned count,int direction){
        return (value+(direction>0?1:count-1))%count;
    };
    while(appletMainLoop()) {
        if(appletGetFocusState()!=AppletFocusState_InFocus)continue;
        for(auto& pad:pads)padUpdate(&pad);
        if(!padIsConnected(&pads[0])||!padIsConnected(&pads[1])) {
            screen.close();
            if(!choose_local_controllers(pads,error))return false;
            continue;
        }
        const auto buttons=local_buttons(pads[0],padGetButtonsDown(&pads[0]));
        if(buttons&HidNpadButton_B)throw RoomExit{};
        if(edit) {
            const auto second=local_buttons(pads[1],padGetButtonsDown(&pads[1]));
            if((buttons|second)&HidNpadButton_Y) {
                const unsigned player=buttons&HidNpadButton_Y?0:1;
                const auto& car=cars[car_indices[player]];
                auto [entry,inserted]=edited_rides[player].try_emplace(car);
                if(inserted)if(const auto original=rides.find(car);original!=rides.end())
                    entry->second=original->second;
                LocalGarageSlot slot{pads[player],entry->second,local_garage_buttons,local_garage_stick};
                screen.close();
                edit(car,tier,{},&slot);
                continue;
            }
        }
        const bool select_laps=modes[mode]==QuickRaceMode::circuit||modes[mode]==QuickRaceMode::drift;
        const bool no_traffic=modes[mode]==QuickRaceMode::drift;
        if((buttons&HidNpadButton_Plus)||(row==start_row&&(buttons&HidNpadButton_A))) {
            options.laps=std::uint8_t(select_laps?laps:1);
            if(no_traffic)traffic_density=0;
            options.tier=std::uint8_t(tier);
            for(unsigned i=0;i<2;++i) {
                const auto& car=cars[car_indices[i]];
                options.rides[i]=chosen_ride(car,edited_rides[i].count(car)?edited_rides[i]:rides);
            }
            course=catalog->free_courses().at(courses[mode].at(track));
            selected_mode=modes[mode];
            return true;
        }
        if(buttons&HidNpadButton_Up)row=cycle(row,row_count,-1);
        if(buttons&HidNpadButton_Down)row=cycle(row,row_count,1);
        const auto adjust=[&](unsigned player){
            const auto down=local_buttons(pads[player],padGetButtonsDown(&pads[player]));
            return (down&HidNpadButton_Right)?1:(down&HidNpadButton_Left)?-1:0;
        };
        const int change=adjust(0);
        if(change)switch(row) {
        case mode_row: mode=cycle(mode,unsigned(modes.size()),change);track=0;break;
        case track_row: track=cycle(track,unsigned(courses[mode].size()),change);break;
        case laps_row: if(select_laps)laps=1+cycle(laps-1,6,change);break;
        case tier_row: tier=cycle(tier,4,change);break;
        case car1_row: car_indices[0]=cycle(car_indices[0],unsigned(cars.size()),change);break;
        case car2_row: car_indices[1]=cycle(car_indices[1],unsigned(cars.size()),change);break;
        case traffic_row: if(!no_traffic)traffic_density=cycle(traffic_density,4,change);break;
        case gyro1_row: gyro_enabled[0]=!gyro_enabled[0];break;
        case gyro2_row: gyro_enabled[1]=!gyro_enabled[1];break;
        case layout_row: layout=layout==SplitLayout::stacked?SplitLayout::side_by_side:SplitLayout::stacked;break;
        case particles_row: effects.particles=!effects.particles;break;
        case reflection_row: effects.reflections=!effects.reflections;break;
        default:break;
        }
        if(const int change_p2=adjust(1))
            car_indices[1]=cycle(car_indices[1],unsigned(cars.size()),change_p2);
        const auto& selected=catalog->free_courses().at(courses[mode].at(track));
        screen.show({"QUICK RACE / SPLIT SCREEN","",
            label(mode_row,quick_race_mode_name(modes[mode])),
            label(track_row,selected.name+(selected.direction?" (Reverse)":"")),
            label(laps_row,"Laps: "+std::to_string(select_laps?laps:1)),
            label(tier_row,std::string("Performance: ")+kLanPerformance[tier]),
            label(car1_row,"Player 1: "+cars[car_indices[0]]),label(car2_row,"Player 2: "+cars[car_indices[1]]),
            label(traffic_row,std::string("Traffic: ")+traffic_levels[no_traffic?0:traffic_density]),
            label(gyro1_row,gyro_enabled[0]?"P1 steering: Tilt":"P1 steering: Stick"),
            label(gyro2_row,gyro_enabled[1]?"P2 steering: Tilt":"P2 steering: Stick"),
            label(layout_row,layout==SplitLayout::stacked?"Split: Top / Bottom":"Split: Left / Right"),
            label(particles_row,effects.particles?"Particle FX: On":"Particle FX: Off"),
            label(reflection_row,effects.reflections?"Wet Road Reflections: On":"Wet Road Reflections: Off"),
            label(start_row,"Start race"),edit?"Y: Open your garage on either controller":"",
            gyro_enabled[0]||gyro_enabled[1]?"Hold controllers straight at the countdown.":"",
            "Left/Right: Change   +: Start   B: Back"});
    }
    throw RoomExit{};
}

void update_multiplayer_audio(audio::RaceAudioService& sound,const RaceSession& session,
                              audio::RaceAudioSnapshot& snapshot) {
    // Construct the tick directly in reusable heap storage. The return
    // value is elided here, so the caller needs no contact buffer on stack.
    static_assert(std::is_trivially_destructible_v<audio::RaceAudioSnapshot>);
    ::new (static_cast<void*>(&snapshot)) audio::RaceAudioSnapshot(audio::race_audio_snapshot(session));
    sound.on_tick(snapshot);
}

void run_local(const std::string& root,bool metric_speed,const FrontendArt& post_art,const std::map<std::string,CareerRide>& rides,
               std::string& error,bool use_assigned,std::uint32_t race_height,const LanGarageEditor& edit,LocalRides& edited_rides) {
    LanScreen screen;
    struct RestorePads {
        HidNpadJoyHoldType hold{};
        bool restore_hold=R_SUCCEEDED(hidGetNpadJoyHoldType(&hold));
        ~RestorePads(){
            if(restore_hold)hidSetNpadJoyHoldType(hold);
            padConfigureInput(1,HidNpadStyleSet_NpadStandard);
        }
    } restore;
    try {
        LocalPads pads{};
        if(!choose_local_controllers(pads,error,use_assigned))return;
        LanRaceOptions options;FreeDriveCourse course;unsigned traffic_density=0;
        auto mode=QuickRaceMode::circuit;
        std::array<bool,2> gyro_enabled{};
        auto layout=SplitLayout::stacked;
        SplitEffects effects;
        require(local_lobby(root,pads,screen,rides,options,course,mode,traffic_density,gyro_enabled,layout,effects,edit,edited_rides,error),error);
        for(;;) {
        screen.show({"QUICK RACE / SPLIT SCREEN","","Loading "+course.name+
            (course.direction?" (Reverse)":"")+"..."});
        auto world=std::make_unique<LanRaceWorld>();
        LocalRaceTraffic traffic;
        require(traffic.load(root,course.event,course.round,traffic_density,error),error);
        require(world->load_local(root,course.event,course.round,mode,options,error,traffic.density,traffic.physics),error);
        // RaceGpuScene owns large fixed render scratch buffers. Keep them
        // off the main thread's stack, including during controller selection.
        auto scene=std::make_unique<LanScene>();
        RaceGpuFrame frame;auto hud=std::make_unique<std::array<RaceHudFrame,2>>();
        require(scene->load_local(root,course.event,course.round,mode,options,world->models(),error,std::move(traffic.visuals),effects),error);
        require(scene->split_frame(world->session(),0,frame,(*hud)[0],error,metric_speed,layout),error);
        const bool drag=world->session().is_drag();
        screen.close();
        DekoRenderer renderer;
        bool docked=appletGetOperationMode()==AppletOperationMode_Console;
        const auto resolution=[&](bool docked_now) {
            return docked_now&&race_height==1080?1080U:race_height==540?540U:720U;
        };
        auto scene_height=resolution(docked);
#ifdef NFSU_SPLIT_DIAGNOSTICS
        const auto capture=std::make_unique<SplitDiagnostics>();
        auto* const profile=capture.get();
#else
        SplitDiagnostics* profile=nullptr;
#endif
        require(renderer.open_race_target(frame.commands,error,scene->draw_capacity(),bool(profile),false,
                                          scene_height,&scene->atlas(),true),error);

        audio::RaceAudioService sound;
        auto audio_snapshot=std::make_unique<audio::RaceAudioSnapshot>();
        std::string audio_note;
        const auto levels=lan_performance_levels(options.tier);
        const auto& induction=world->session().rival(0).spec().induction;
        const std::vector<audio::RaceRemoteEngineBinding> other{{world->models()[1],levels.engine_a,
            levels.induction,std::any_of(induction.begin(),induction.end(),[](float value){return value>0;}),options.rides[1].turbo_brand}};
        const auto audio_core=svcGetCurrentProcessorNumber()==2?1:2;
        sound.start(root,audio_note,{},[audio_core]{
            return R_SUCCEEDED(svcSetThreadCoreMask(threadGetCurHandle(),audio_core,1U<<audio_core))&&
                R_SUCCEEDED(svcSetThreadPriority(threadGetCurHandle(),0x30));
        },true,world->models()[0],levels.engine_a,levels.nitrous,levels.tyres,other,levels.induction,
                    unsigned(traffic.physics.size()),world->session().props()?world->session().props()->sound_materials():0,
                    0,std::nullopt,options.rides[0].turbo_brand);
        require(scene->check_local_budget(world->session(),sound.resident_bytes(),error),error);
        if(profile)require(profile->open(course,options,traffic_density,layout,effects,scene_height,
                                          renderer,sound.resident_bytes(),error),error);

        std::array<std::uint32_t,2> pending_buttons{};
        std::array<GamepadFrame,2> input{};
        std::array<DragPadInput,2> drag_input;
        GyroSteering gyro[]{GyroSteering(HidNpadIdType_No1),GyroSteering(HidNpadIdType_No2)};
        const auto release_gyro=[&]{for(auto& steering:gyro)steering.release();};
        bool reset_drag_input=true;
        auto last=micros();std::uint64_t debt=0;
        auto& ui_clock=race_presentation_clock();ui_clock.resume();
        RaceFinishHold finish_hold;RaceResultStatistics result_stats;
        std::future<std::unique_ptr<RaceLoadingArt>> loading_job;
        bool ready_for_results=false,completed=false;
        const DekoRenderer::SplitViewSource render_view=[&,profile](unsigned player,const RecordedCommands*& commands,
                                                           const RaceHudFrame*& view_hud,std::string& frame_error){
            auto& player_hud=(*hud)[player];
            if(!scene->split_frame(world->session(),player,frame,player_hud,frame_error,metric_speed,layout,
                                   profile?&profile->cpu_view[player]:nullptr,profile))return false;
            if(profile)profile->view(player,frame,player_hud);
            commands=&frame.commands;view_hud=&player_hud;return true;
        };
        while(appletMainLoop()) {
            if(appletGetFocusState()!=AppletFocusState_InFocus){
                release_gyro();
                if(profile)profile->suspend();
                last=micros();debt=0;pending_buttons={};reset_drag_input=true;ui_clock.resume();continue;
            }
            const bool now_docked=appletGetOperationMode()==AppletOperationMode_Console;
            if(now_docked!=docked) {
                docked=now_docked;
                auto next_height=resolution(docked);
                if(next_height!=scene_height) {
                    require(scene->split_frame(world->session(),0,frame,(*hud)[0],error,metric_speed,layout),error);
                    require(renderer.reconfigure_target(frame.commands,error,scene->draw_capacity(),bool(profile),false,
                                                        next_height,&scene->atlas(),true),error);
                    scene_height=next_height;
                }
                last=micros();debt=0;pending_buttons={};reset_drag_input=true;ui_clock.resume();
                continue;
            }
            const auto input_begin=profile?micros():0;
            for(auto& pad:pads)padUpdate(&pad);
            const auto buttons=local_buttons(pads[0],padGetButtonsDown(&pads[0]))|
                local_buttons(pads[1],padGetButtonsDown(&pads[1]));
            if((buttons&HidNpadButton_Plus)&&world->session().timing().outcome==RaceOutcome::running)break;
            if(!padIsConnected(&pads[0])||!padIsConnected(&pads[1])) {
                sound.set_suspended(true);
                release_gyro();
                if(!choose_local_controllers(pads,error))break;
                sound.set_suspended(false);
                last=micros();debt=0;pending_buttons={};reset_drag_input=true;ui_clock.resume();continue;
            }
            const auto now=micros();
            if(profile)profile->begin((now-last)/1000.0);
            scene->advance_presentation(ui_clock.sample(now));
            debt+=std::min<std::uint64_t>(now-last,133333)*60;
            last=now;
            for(unsigned i=0;i<pads.size();++i) {
                const auto& pad=pads[i];
                if(gyro_enabled[i])gyro[i].poll(pad,now);
                if(drag)drag_input[i].poll(pad,reset_drag_input,local_drag_input);
                else {
                    input[i].pads[0]=local_race_input(padGetStyleSet(&pad),padGetButtons(&pad),
                        padGetStickPos(&pad,0),padGetStickPos(&pad,1),false);
                    input[i].pads[0].connected=true;
                    pending_buttons[i]|=switch_race_buttons(local_buttons(pad,padGetButtonsDown(&pad)));
                }
            }
            reset_drag_input=false;
            if(profile)profile->input_ms=(micros()-input_begin)/1000.0;
            while(debt>=1000000) {
                debt-=1000000;
                if(world->session().timing().outcome!=RaceOutcome::running&&
                   world->session().timing().outcome!=RaceOutcome::finished)continue;
                auto sampled=input;
                for(unsigned i=0;i<pads.size();++i) {
                    if(drag)sampled[i]=drag_input[i].take(world->session().timing().step);
                    else {
                        sampled[i].pads[0].buttons|=pending_buttons[i];
                        pending_buttons[i]=0;
                    }
                    if(gyro_enabled[i])gyro[i].steer(sampled[i].pads[0],drag);
                }
                RaceSimulationProfile step;
                step.detailed_contacts=bool(profile);
                auto stage=profile?micros():0;
                require(world->advance(sampled,error,profile?&step:nullptr,profile),error);
                result_stats.observe(world->session());
                const std::array<unsigned,4> ending_events{{world->session().crash_camera_event(),world->session().rival(0).crash_camera_event(),0,0}};
                ready_for_results=finish_hold.observe(world->session().timing(),2,ending_events);
                finish_hold.advance(world->session().world_frame_seconds());
                if(world->session().timing().outcome==RaceOutcome::finished&&!loading_job.valid())
                    loading_job=prepare_multiplayer_loading(root);
                if(profile) {
                    const auto done=micros();profile->simulation_ms+=(done-stage)/1000.0;stage=done;
                    profile->add_simulation(step);
                }
                scene->advance_local_effects(world->session());
                if(profile){const auto done=micros();profile->particle_ms+=(done-stage)/1000.0;stage=done;}
                update_multiplayer_audio(sound,world->session(),*audio_snapshot);
                if(profile)profile->audio_ms+=(micros()-stage)/1000.0;
            }
            DekoSplitTiming present;
            require(renderer.present_split(render_view,error,layout,profile?&present:nullptr),error);
            if(profile)require(profile->write(world->session(),scene_height,present,renderer),
                               "Cannot write the split diagnostics CSV");
            if(ready_for_results&&loading_job.valid()&&loading_job.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                completed=true;break;
            }
        }
        release_gyro();
        PostRaceAction ending_action=PostRaceAction::leave;
        if(completed) {
            MultiplayerFinishInput controls;controls.poll=[]{};
            controls.sample=[&] {
                u64 down=0;for(auto& controller:pads){padUpdate(&controller);down|=local_buttons(controller,padGetButtonsDown(&controller));}
                FrontendInput input;
                input.up=down&(HidNpadButton_Up|HidNpadButton_StickLUp);input.down=down&(HidNpadButton_Down|HidNpadButton_StickLDown);
                input.left=down&(HidNpadButton_Left|HidNpadButton_StickLLeft);input.right=down&(HidNpadButton_Right|HidNpadButton_StickLRight);
                input.accept=down&HidNpadButton_A;input.back=down&(HidNpadButton_B|HidNpadButton_Plus);return input;
            };
            ending_action=show_multiplayer_finish(root,*scene,world->session(),world->models(),frame,renderer,sound,
                post_art,result_stats,loading_job,controls,error);
        }
        sound.stop();
        require(renderer.close(error),error);
        if(ending_action!=PostRaceAction::restart)break;
        }
    } catch(const RoomExit&) {
        return;
    }
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
            screen.close();edit(cars[car],options.tier,poll,nullptr);
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
void run_lan(const std::string& root,PadState& pad,bool selected,bool host,std::string ip,bool write_logs,const FrontendArt& post_art,
             const std::map<std::string,CareerRide>& rides,const LanGarageEditor& edit){
    StartupJournal journal;char path[96]{};
    for(unsigned i=0;write_logs&&i<10000;++i){
        std::snprintf(path,sizeof path,"sdmc:/nfsu-lan-runtime-%04u.log",i);
        if(journal.open(path)){diagnostic_journal=&journal;break;}
        if(errno!=EEXIST)break;
    }
    struct JournalScope {~JournalScope(){diagnostic_journal=nullptr;}} journal_scope;
    LanScreen screen;
    char timeline_summary[512]{},frame_summary[256]{};
    std::unique_ptr<LanFrameStats> frame_timing;
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
        auto world=std::make_unique<LanRaceWorld>();trace("lan_world_begin");require(world->load(root,error,options),error);trace("lan_world_ready");
        LanRaceView view;require(view.open(world->session(),world->cameras(local),local,error,&world->host_hull()),error);
        auto latest=world->snapshot(),presented=latest;require(view.apply(latest,1./60,error),error);
        trace("lan_scene_begin");
        auto scene=std::make_unique<LanScene>();
        require(scene->load(root,options,world->models(),local,error),error);
        trace("lan_scene_ready");
        RaceGpuFrame frame;auto hud=std::make_unique<RaceHudFrame>();require(scene->frame(view,frame,*hud,error),error);
        screen.show({"LAN / READY","Waiting for the other console to finish loading...","+ cancels"});
        // Release the software framebuffer before opening a deko swapchain.
        screen.close();
        DekoRenderer renderer;trace("lan_renderer_begin");
        require(renderer.open(frame.commands,error,true,scene->draw_capacity(),false,true,false,0,&scene->atlas()),error);
        trace("lan_renderer_ready");audio::RaceAudioService sound;std::string audio_note;
        auto audio_snapshot=std::make_unique<audio::RaceAudioSnapshot>();
        const auto audio_core=svcGetCurrentProcessorNumber()==2?1:2;
        const auto levels=lan_performance_levels(options.tier);
        const auto& remote_induction=view.session().rival(0).spec().induction;
        const std::vector<audio::RaceRemoteEngineBinding> remote_audio{{world->models()[1-local],
            levels.engine_a,levels.induction,std::any_of(remote_induction.begin(),remote_induction.end(),
                [](float value){return value>0.F;}),options.rides[1-local].turbo_brand}};
        const bool audio_started=sound.start(root,audio_note,{},[audio_core]{
            return R_SUCCEEDED(svcSetThreadCoreMask(threadGetCurHandle(),audio_core,1U<<audio_core))&&
                R_SUCCEEDED(svcSetThreadPriority(threadGetCurHandle(),0x30));
        },true,world->models()[local],levels.engine_a,levels.nitrous,levels.tyres,remote_audio,levels.induction,0,0,
            0,std::nullopt,options.rides[local].turbo_brand);
        trace("lan_audio_result",audio_started?"started":audio_note.c_str());
        link.set_ready();trace("lan_room_ready");
        frame_timing=std::make_unique<LanFrameStats>();
        LanSnapshotBuffer snapshot_buffer;InputTiming input_timing;
        std::uint64_t last=micros(),debt=0,stats_at=last,simulations=0,last_presentation=last,last_audio_step=~std::uint64_t(0);
        double simulation_ms=0,sim_max=0;bool was_connected=false;std::uint32_t previous_buttons=0;
        unsigned diagnostic_frames=0;bool first_simulation=true;
        RaceFinishHold finish_hold;RaceResultStatistics result_stats;
        std::future<std::unique_ptr<RaceLoadingArt>> loading_job;
        bool completed=false,transport_open=true;
        auto& ui_clock=race_presentation_clock();ui_clock.resume();
        while(appletMainLoop()){
            padUpdate(&pad);
            if((padGetButtonsDown(&pad)&HidNpadButton_Plus)&&latest.outcome!=unsigned(RaceOutcome::finished))break;
            const auto now=micros();auto input=sample(pad);
            if(diagnostic_frames<2){trace("lan_poll_begin");}
            if(!link.poll(now,error)) {
                // poll may accept a final snapshot and then read bye in the
                // same datagram batch. Recover it before deciding the room
                // closed without results. Frames after bye stay rejected.
                if(!lan_recover_completed_results(link,latest))require(false,error);
                // Both results are already authoritative. Peer loss must not
                // replace that completed table with a room-error screen.
                transport_open=false;latest.finish_ready=true;presented=latest;
                require(view.apply(latest,1./60,error),error);
                if(!read_lan_statistics(latest,local,result_stats))result_stats.observe(view.session());
                completed=true;break;
            }
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
                        if(world->session().timing().outcome==RaceOutcome::running||world->session().timing().outcome==RaceOutcome::finished){
                            std::array<GamepadFrame,2> pads{};pads[0].pads[0]=input;pads[1].pads[0]=link.remote_input(now);
                            if(first_simulation)trace("lan_simulation_begin");
                            const auto begin=micros();require(world->advance(pads,error),error);
                            if(first_simulation){trace("lan_simulation_ready");first_simulation=false;}
                            const double cost=double(micros()-begin)*.001;simulation_ms+=cost;sim_max=std::max(sim_max,cost);++simulations;
                            result_stats.observe(world->session());
                            latest=world->snapshot();
                            write_lan_statistics(latest,result_stats);
                            const std::array<unsigned,4> ending_events{{world->session().crash_camera_event(),world->session().rival(0).crash_camera_event(),0,0}};
                            const bool ending=finish_hold.observe(world->session().timing(),2,ending_events);
                            finish_hold.advance(world->session().world_frame_seconds());
                            if(latest.outcome==unsigned(RaceOutcome::finished)&&!loading_job.valid())loading_job=prepare_multiplayer_loading(root);
                            latest.finish_ready=ending&&loading_job.valid()&&loading_job.wait_for(std::chrono::seconds(0))==std::future_status::ready;
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
                    LanRaceFrame incoming;std::uint32_t incoming_ack=0;
                    while(link.take_snapshot(incoming,&incoming_ack)){
                        if(incoming.step<latest.step||(incoming.step==latest.step&&
                           !(incoming.finish_ready&&!latest.finish_ready)))continue;
                        latest=incoming;snapshot_buffer.push(latest,incoming_ack);
                        input_timing.measure(input_timing.received,incoming_ack,now);
                    }
                }else last=now;
            }else last=now;
            if(latest.outcome==unsigned(RaceOutcome::finished)&&!loading_job.valid())loading_job=prepare_multiplayer_loading(root);
            const auto camera=gamepad_button_bit(GamepadButton::face_north);
            const auto rear=gamepad_button_bit(GamepadButton::stick_right);
            const double presentation_dt=std::clamp(double(now-last_presentation)*.000001,.001,.1);last_presentation=now;
            std::uint32_t presented_ack=0;
            if(host)presented=latest;
            const bool guest_sample=!host&&snapshot_buffer.sample(presentation_dt,presented,presented_ack);
            require(view.apply(presented,presentation_dt,error,(input.buttons&rear)!=0,(input.buttons&camera)&&!(previous_buttons&camera)),error);
            previous_buttons=input.buttons;
            if(!host&&!read_lan_statistics(presented,local,result_stats))result_stats.observe(view.session());
            if(audio_started&&was_connected&&presented.step!=last_audio_step){update_multiplayer_audio(sound,view.session(),*audio_snapshot);last_audio_step=presented.step;}
            if(diagnostic_frames<2){trace("lan_frame_begin");}
            scene->advance_presentation(ui_clock.sample(now));
            require(scene->frame(view,frame,*hud,error),error);
            if(diagnostic_frames<2){trace("lan_update_begin");}
            require(renderer.update(frame.commands,error),error);
            if(diagnostic_frames<2){trace("lan_present_begin");}
            require(renderer.present(error,nullptr,nullptr,hud.get()),error);
            const auto completed_at=micros();
            frame_timing->presented(completed_at,link.connected()&&
                presented.outcome==unsigned(RaceOutcome::running)&&(host?simulations>0:guest_sample));
            if(guest_sample)input_timing.measure(input_timing.presented,presented_ack,micros());
            if(diagnostic_frames<2){trace("lan_present_ready");++diagnostic_frames;}
            if(presented.finish_ready&&loading_job.valid()&&loading_job.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                completed=true;break;
            }
            if(now-stats_at>=5000000){
                std::printf("LAN_METRICS role=%s step=%u sent=%llu received=%llu invalid=%llu sim_mean_ms=%.3f sim_max_ms=%.3f\n",host?"host":"guest",latest.step,
                    (unsigned long long)link.sent_bytes(),(unsigned long long)link.received_bytes(),(unsigned long long)link.invalid_packets(),
                    simulations?simulation_ms/double(simulations):0,sim_max);
                frame_timing->summary(frame_summary,sizeof frame_summary,host);std::puts(frame_summary);
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
        if(completed) {
            MultiplayerFinishInput controls;controls.restart=false;
            controls.sample=[&] {
                padUpdate(&pad);const auto down=padGetButtonsDown(&pad);FrontendInput input;
                input.up=down&(HidNpadButton_Up|HidNpadButton_StickLUp);input.down=down&(HidNpadButton_Down|HidNpadButton_StickLDown);
                input.left=down&(HidNpadButton_Left|HidNpadButton_StickLLeft);input.right=down&(HidNpadButton_Right|HidNpadButton_StickLRight);
                input.accept=down&HidNpadButton_A;input.back=down&(HidNpadButton_B|HidNpadButton_Plus);return input;
            };
            controls.poll=[&] {
                if(!transport_open)return;
                if(!link.poll(micros(),error)){transport_open=false;trace("lan_results_peer_closed",error.c_str());return;}
                // Retain normal packet sequence/input acknowledgements and
                // retry the final handoff throughout loading and results.
                const bool okay=host?link.publish(latest,error):link.send_input(sample(pad),error);
                if(!okay){transport_open=false;trace("lan_results_peer_closed",error.c_str());return;}
                if(!host){LanRaceFrame acknowledged;while(link.take_snapshot(acknowledged)){}}
            };
            const std::array<std::string,2> post_models{{world->models()[local],world->models()[1-local]}};
            show_multiplayer_finish(root,*scene,view.session(),post_models,frame,renderer,sound,
                post_art,result_stats,loading_job,controls,error);
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
    if(frame_timing){
        frame_timing->summary(frame_summary,sizeof frame_summary,host);std::puts(frame_summary);
    }
    if(diagnostic_journal){
        if(frame_summary[0])journal.mark("lan_frames",frame_summary,false);
        if(timeline_summary[0])journal.mark("lan_timeline",timeline_summary,false);
        if(!journal.close())std::fprintf(stderr,"LAN_TRACE_CLOSE_FAILED %s\n",journal.error());
    }
}
}

void run_switch_lan(const std::string& root,PadState& pad,bool write_logs,const nfsu::FrontendArt& post_art,
                    const std::map<std::string,CareerRide>& rides,const LanGarageEditor& edit){
    run_lan(root,pad,false,false,{},write_logs,post_art,rides,edit);
}

void run_switch_multiplayer(const std::string& root,PadState& pad,bool write_logs,bool metric_speed,std::uint32_t race_height,const nfsu::FrontendArt& post_art,
                            const std::map<std::string,CareerRide>& rides,const LanGarageEditor& edit) {
    LanScreen screen;
    std::string notice;
    LocalRides edited_rides;
    constexpr u64 actions=HidNpadButton_A|HidNpadButton_B|HidNpadButton_X|HidNpadButton_Y|HidNpadButton_Plus;
    bool armed=false;
    while(appletMainLoop()) {
        if(appletGetFocusState()!=AppletFocusState_InFocus){armed=false;continue;}
        screen.show({"MULTIPLAYER","","A: Quick Race / Split Screen","Y: Split Screen with assigned controllers","X: LAN","B: Back","",notice});
        padUpdate(&pad);
        if(!armed) {
            // A from the main menu or Leave from the previous session.
            armed=(padGetButtons(&pad)&actions)==0;
            continue;
        }
        const auto buttons=padGetButtonsDown(&pad);
        if(buttons&(HidNpadButton_B|HidNpadButton_Plus))return;
        if(buttons&(HidNpadButton_A|HidNpadButton_Y)) {
            screen.close();notice.clear();
            try {
                run_local(root,metric_speed,post_art,rides,notice,(buttons&HidNpadButton_A)==0,race_height,edit,edited_rides);
            } catch(const std::exception& e) {
                notice=e.what();
            }
            if(!notice.empty()) {
                std::fprintf(stderr,"SPLIT SCREEN: %s\n",notice.c_str());
                std::fflush(stderr);
            }
            armed=false;
        } else if(buttons&HidNpadButton_X) {
            screen.close();run_switch_lan(root,pad,write_logs,post_art,rides,edit);
            armed=false;
        }
    }
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
    FrontendArt post_art;
    if(!post_art.load(root,error,false)||!post_art.load_post_race(root,error))return 1;
    run_lan(root,pad,selected,host,ip,true,post_art,{},{});
    if(log>=0)::close(log);
    return 0;
}
#endif
