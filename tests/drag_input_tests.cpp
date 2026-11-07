#include "drag_input.h"
#include "nfsu/drive_controls.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<HidNpadCommonState> history;
std::size_t requested=0;
u32 read_style=0;
HidNpadIdType read_id=HidNpadIdType_No1;
constexpr u64 up=1ULL<<7; // Npad R (mapping verified below by behavior).
constexpr u64 down=1ULL<<6; // Npad L.
constexpr u64 nos=1ULL; // Npad A.
constexpr u64 gas=1ULL<<9; // Npad ZR.
constexpr u64 left=1ULL<<12,right=1ULL<<14; // Npad D-pad.
void check(bool p,const char* why){if(!p)throw std::runtime_error(why);}
std::size_t read(HidNpadIdType id,HidNpadCommonState* out,std::size_t n,u32 style) {
    requested=n;read_style=style;read_id=id;
    n=std::min(n,history.size());std::copy_n(history.begin(),n,out);return n;
}
void states(std::initializer_list<std::pair<u64,u64>> list) {
    history.clear();for(auto [number,buttons]:list){HidNpadCommonState s;s.sampling_number=number;s.buttons=buttons;history.push_back(s);}
}
void stick_states(std::initializer_list<std::pair<u64,std::int32_t>> list,u64 buttons=0) {
    history.clear();for(auto [number,y]:list){HidNpadCommonState s;s.sampling_number=number;s.buttons=buttons;s.analog_stick_r.y=y;history.push_back(s);}
}
void lane_states(std::initializer_list<std::pair<u64,std::int32_t>> list,u64 buttons=0) {
    history.clear();for(auto [number,x]:list){HidNpadCommonState s;s.sampling_number=number;s.buttons=buttons;s.analog_stick_l.x=x;history.push_back(s);}
}
struct Rig {
    DragPadInput input;
    nfsu::DriveControls controls;
    PadState pad;
    std::uint64_t tick=0;
    Rig() {
        std::string error;
        check(controls.configure(nfsu::default_drag_controls_config(),error),"drag bindings");
        states({{1,0}});input.poll(pad);check(step()==nfsu::VehicleShift::none,"connect baseline");
    }
    nfsu::VehicleShift step() {
        auto f=input.take(tick++);controls.update(f);
        return controls.step(20,nfsu::kVehicleGearFirst,nfsu::VehicleGearbox::manual).shift;
    }
    void stick(u64 number,std::int32_t y,bool discard=false) {
        pad.sticks[1].y=y;stick_states({{number,y}},pad.buttons_cur);input.poll(pad,discard);
    }
    int lane() {
        step();const auto& f=controls.frame_state();
        const auto value=f.steer_from_button?f.steer_digital:f.steer_analog;
        return value<0?-1:value>0?1:0;
    }
};
}
std::size_t hidGetNpadStatesFullKey(HidNpadIdType i,HidNpadFullKeyState* s,std::size_t n){return read(i,s,n,HidNpadStyleTag_NpadFullKey);}
std::size_t hidGetNpadStatesHandheld(HidNpadIdType i,HidNpadHandheldState* s,std::size_t n){return read(i,s,n,HidNpadStyleTag_NpadHandheld);}
std::size_t hidGetNpadStatesJoyDual(HidNpadIdType i,HidNpadJoyDualState* s,std::size_t n){return read(i,s,n,HidNpadStyleTag_NpadJoyDual);}
std::size_t hidGetNpadStatesJoyLeft(HidNpadIdType i,HidNpadJoyLeftState* s,std::size_t n){return read(i,s,n,HidNpadStyleTag_NpadJoyLeft);}
std::size_t hidGetNpadStatesJoyRight(HidNpadIdType i,HidNpadJoyRightState* s,std::size_t n){return read(i,s,n,HidNpadStyleTag_NpadJoyRight);}

int main(){try{
    using nfsu::VehicleShift;
    check(nfsu::gamepad_buttons_from_npad(up)==nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_right),"R mapping");
    check(switch_race_buttons(down)==nfsu::gamepad_button_bit(nfsu::GamepadButton::face_east),"L maps to drag downshift");
    check(switch_race_buttons(nos)==nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_left),"A maps to NOS");
    {
        Rig r;r.pad.buttons_cur=nos;states({{2,nos},{1,0}});r.input.poll(r.pad);
        auto frame=r.input.take(1);r.controls.update(frame);
        check(frame.pads[0].buttons==switch_race_buttons(nos),"drag live A delivers held NOS");
        check(r.controls.step(20,nfsu::kVehicleGearFirst,nfsu::VehicleGearbox::manual).shift==VehicleShift::none,
            "drag live A does not shift");
    }
    check(nfsu::gamepad_buttons_from_npad(left)==nfsu::gamepad_button_bit(nfsu::GamepadButton::dpad_left),"D-pad left mapping");
    check(nfsu::gamepad_buttons_from_npad(right)==nfsu::gamepad_button_bit(nfsu::GamepadButton::dpad_right),"D-pad right mapping");
    {
        Rig r;lane_states({{3,0},{2,-16000},{1,0}});r.input.poll(r.pad);
        check(r.lane()==-1&&r.lane()==0,"left flick entirely between display polls survives with its release");
        r.input.poll(r.pad);check(r.lane()==0,"overlapping left-axis history never repeats a flick");
    }
    {
        Rig r;lane_states({{5,0},{4,20000},{3,0},{2,-20000},{1,0}});r.input.poll(r.pad);
        check(r.lane()==-1&&r.lane()==0&&r.lane()==1&&r.lane()==0,"lane reversal retains chronological press and release");
    }
    {
        Rig r;states({{5,0},{4,right},{3,0},{2,left},{1,0}});r.input.poll(r.pad);
        check(r.lane()==-1&&r.lane()==0&&r.lane()==1&&r.lane()==0,"D-pad lane taps survive between polls");
    }
    {
        Rig r;history.clear();
        for(int i=17;i>=2;--i){HidNpadCommonState s;s.sampling_number=u64(i);s.analog_stick_l.x=12000+i*100;history.push_back(s);}
        r.pad.buttons_cur=gas;r.input.poll(r.pad);
        auto f=r.input.take(1);
        check(f.pads[0].axes[0]==13700,"held lane consumes the newest analog magnitude, not its oldest sample");
        check((f.pads[0].buttons&nfsu::gamepad_buttons_from_npad(gas))!=0,"queued lane keeps gas fresh");
        for(int i=0;i<20;++i)check(r.input.take(2+i).pads[0].axes[0]==13700,"magnitude-only HID changes do not create latency");
        r.pad.sticks[0].x=25000;lane_states({{18,0},{17,13700}});r.input.poll(r.pad);
        check(r.input.take(30).pads[0].axes[0]==0,"newer HID center overrides stale padUpdate magnitude");
    }
    {
        Rig r;lane_states({{3,0},{2,-20000},{1,0}});r.input.poll(r.pad);
        r.input.poll(r.pad,true);check(r.lane()==0,"pause discards pending lane flicks");
        lane_states({{4,25000},{3,0}});r.input.poll(r.pad);
        check(r.lane()==1,"lane input resumes after pause");
        history[0].attributes=0;r.pad.attributes=0;r.input.poll(r.pad); // duplicate sample cannot claim a new event
        history[0].sampling_number=5;r.input.poll(r.pad);
        check(r.lane()==0,"disconnect cancels a held lane direction");
    }
    {
        Rig r;r.pad.active_id_mask=1;history.clear();r.input.poll(r.pad);r.lane();
        r.pad.sticks[0].x=-18000;r.input.poll(r.pad);
        r.pad.sticks[0].x=0;r.input.poll(r.pad);
        check(r.lane()==-1&&r.lane()==0,"merged-source snapshot fallback preserves observed lane flicks");
    }
    {
        // Before the fix RaceRun only received the newest display snapshot.
        // A press+release with no world tick therefore supplied two identical
        // released snapshots, and a missed release prevented the next edge.
        nfsu::DriveControls old;nfsu::GamepadFrame f;f.pads[0].connected=true;
        old.update(f);old.step(20,2,nfsu::VehicleGearbox::manual);
        old.update(f);check(old.step(20,2,nfsu::VehicleGearbox::manual).shift==VehicleShift::none,"historical lost-tap witness");
        f.pads[0].buttons=nfsu::gamepad_buttons_from_npad(up);old.update(f);
        check(old.step(20,2,nfsu::VehicleGearbox::manual).shift==VehicleShift::up,"historical first edge");
        old.update(f);check(old.step(20,2,nfsu::VehicleGearbox::manual).shift==VehicleShift::none,"historical lost-release witness");
    }
    {
        Rig r;states({{3,0},{2,up},{1,0}});r.input.poll(r.pad);
        check(requested==17,"read complete bounded HID history");
        check(r.step()==VehicleShift::up,"tap entirely between display polls survives");
        check(r.step()==VehicleShift::none,"release follows short tap");
        r.input.poll(r.pad);check(r.step()==VehicleShift::none,"overlapping HID histories do not replay a tap");
    }
    {
        Rig r;r.pad.buttons_cur=up;states({{2,up},{1,0}});r.input.poll(r.pad);
        r.pad.buttons_cur=0;states({{3,0},{2,up},{1,0}});r.input.poll(r.pad); // no world tick
        check(r.step()==VehicleShift::up,"press and release on zero-tick display frames survive");
        check(r.step()==VehicleShift::none,"queued release is not a repeat");
        r.pad.buttons_cur=up;states({{5,up},{4,0},{3,0}});r.input.poll(r.pad);
        check(r.step()==VehicleShift::up,"later press is accepted");
        // Latest pad still held R, but HID captured a release then a second R.
        states({{7,up},{6,0},{5,up}});r.input.poll(r.pad);
        check(r.step()==VehicleShift::none&&r.step()==VehicleShift::up,"release between polls rearms a second press");
        for(unsigned i=0;i<8;++i){r.input.poll(r.pad);check(r.step()==VehicleShift::none,"held R never repeats in catch-up ticks");}
    }
    for(bool stick:{false,true}) {
        Rig r;if(stick)stick_states({{3,0},{2,32767},{1,0}});else states({{3,0},{2,up},{1,0}});r.input.poll(r.pad);
        nfsu::VehicleModel model(nfsu::stock_240sx_vehicle_spec());nfsu::VehicleState car;
        nfsu::VehiclePose pose;pose.gearbox=nfsu::VehicleGearbox::manual;
        check(model.reset(car,pose),"physical manual car reset");
        r.controls.update(r.input.take(1));
        auto input=r.controls.step(0,car.gear,car.gearbox);input.throttle=1;input.drag_mode=true;
        nfsu::VehicleTelemetry t;
        check(model.step(car,input,nfsu::VehicleFlatGround(),t),"recovered tap reaches physics");
        check(car.transmission.requested_gear==nfsu::kVehicleGearFirst+1,"recovered R changes requested gear exactly once");
        input.shift=VehicleShift::none;
        for(unsigned i=0;i<240&&car.gear==nfsu::kVehicleGearFirst;++i)
            check(model.step(car,input,nfsu::VehicleFlatGround(),t),"physical clutch phases after recovered R");
        check(car.gear==nfsu::kVehicleGearFirst+1,"recovered tap commits second through actual clutch phases");
    }
    {
        Rig r;states({{5,0},{4,down},{3,0},{2,up},{1,0}});
        r.pad.buttons_cur=gas;r.pad.sticks[0].y=2345;r.input.poll(r.pad);
        auto f=r.input.take(1);
        check(f.pads[0].axes[1]==2345&&(f.pads[0].buttons&nfsu::gamepad_buttons_from_npad(gas)),"queued buttons do not delay unselected analog/gas input");
        r.controls.update(f);check(r.controls.step(20,2,nfsu::VehicleGearbox::manual).shift==VehicleShift::up,"ordered R");
        check(r.step()==VehicleShift::none&&r.step()==VehicleShift::down&&r.step()==VehicleShift::none,"ordered release/L/release");
    }
    {
        Rig r;states({{3,0},{2,up},{1,0}});r.input.poll(r.pad);
        r.input.poll(r.pad,true);check(r.step()==VehicleShift::none,"pause cancels undelivered press");
        r.pad.buttons_cur=up;states({{4,up},{3,0}});r.input.poll(r.pad,true);
        check(r.step()==VehicleShift::none,"button held during pause cannot shift on resume");
        states({{6,up},{5,0},{4,up}});r.input.poll(r.pad);
        check(r.step()==VehicleShift::up,"release and fresh press after resume work");
    }
    {
        Rig r;r.pad.buttons_cur=up;states({{2,up},{1,0}});r.input.poll(r.pad);
        r.pad.active_handheld=false;r.pad.attributes=0;history.clear();r.input.poll(r.pad);
        check(r.step()==VehicleShift::none,"disconnect cancels pending shift");
        r.pad.active_handheld=true;r.pad.attributes=1;states({{1,up}});r.input.poll(r.pad);
        check(r.step()==VehicleShift::none,"reconnect held R is latched");
        r.pad.buttons_cur=0;states({{3,0},{2,0},{1,up}});r.input.poll(r.pad);r.step();
        r.pad.buttons_cur=up;states({{4,up},{3,0}});r.input.poll(r.pad);
        check(r.step()==VehicleShift::up,"reconnect release rearms R");
    }
    for(u32 style:{u32(HidNpadStyleTag_NpadFullKey),u32(HidNpadStyleTag_NpadJoyDual),u32(HidNpadStyleTag_NpadJoyLeft),u32(HidNpadStyleTag_NpadJoyRight)}) {
        Rig r;r.pad.active_handheld=false;r.pad.active_id_mask=1;r.pad.style_set=style;
        states({{100,0}});r.input.poll(r.pad);r.step();
        states({{102,0},{101,up},{100,0}});r.input.poll(r.pad);
        check(read_style==style&&read_id==HidNpadIdType_No1,"correct docked/joycon history stream");
        check(r.step()==VehicleShift::up,"short R works for each configured common style");
    }
    {
        Rig r;r.pad.active_id_mask=1; // merged Handheld + No1 uses snapshot fallback
        r.input.poll(r.pad);r.step();r.pad.buttons_cur=up;r.input.poll(r.pad);
        r.pad.buttons_cur=0;r.input.poll(r.pad);
        check(r.step()==VehicleShift::up&&r.step()==VehicleShift::none,"fallback also retains zero-tick tap");
    }
    {
        Rig r;r.stick(2,19999);check(r.step()==VehicleShift::none,"subthreshold right Y is not a shift");
        r.stick(3,20000);check(r.step()==VehicleShift::up,"right stick up shifts up at threshold");
        r.stick(4,19000);check(r.step()==VehicleShift::none,"activation jitter does not release");
        r.stick(5,25000);check(r.step()==VehicleShift::none,"activation jitter does not repeat");
        for(unsigned i=0;i<8;++i)check(r.step()==VehicleShift::none,"held right stick does not repeat in catch-up");
        r.stick(6,8000);check(r.step()==VehicleShift::none,"center releases the gesture");
        r.stick(7,-20000);check(r.step()==VehicleShift::down,"right stick down shifts down");
        r.stick(8,32767);check(r.step()==VehicleShift::none,"direct opposite-side jump requires center");
        r.stick(9,-32767);check(r.step()==VehicleShift::none,"sign oscillation without center cannot repeat");
        r.stick(10,0);r.step();r.stick(11,32767);check(r.step()==VehicleShift::up,"center rearms a fresh upward flick");
    }
    {
        Rig r;stick_states({{5,0},{4,-32767},{3,0},{2,32767},{1,0}});r.input.poll(r.pad);
        check(r.step()==VehicleShift::up&&r.step()==VehicleShift::none&&
              r.step()==VehicleShift::down&&r.step()==VehicleShift::none,"short stick flicks between render polls survive in order");
        r.stick(6,32767);r.stick(7,0); // two display polls, no simulation tick
        check(r.step()==VehicleShift::up&&r.step()==VehicleShift::none,"zero-world-tick stick flick survives");
    }
    {
        Rig r;r.stick(2,32767,true);check(r.step()==VehicleShift::none,"stick held on resume cannot shift");
        r.stick(3,32767);check(r.step()==VehicleShift::none,"held stick stays blocked until center");
        r.stick(4,0);r.step();r.stick(5,-32767);check(r.step()==VehicleShift::down,"post-resume center rearms down");
        r.pad.active_handheld=false;r.pad.active_id_mask=1;r.pad.style_set=HidNpadStyleTag_NpadJoyDual;
        r.stick(100,-32767);check(r.step()==VehicleShift::none,"controller change blocks held deflection");
        r.stick(101,0);r.step();r.stick(102,32767);check(r.step()==VehicleShift::up,"changed controller rearms after center");
    }
    {
        Rig r;r.pad.buttons_cur=up;r.stick(2,32767);
        check(r.step()==VehicleShift::up,"R and stick together issue one shift");
        r.pad.buttons_cur=0;r.stick(3,32767);check(r.step()==VehicleShift::none,"releasing R while stick held does not repeat");
        r.stick(4,0);r.step();r.pad.buttons_cur=nfsu::kNpadButtonStickR;
        r.stick(5,0);check(r.step()==VehicleShift::none&&r.controls.frame_state().look_back,"R3 alone still looks back without shifting");
        r.stick(6,32767);check(r.step()==VehicleShift::up&&r.controls.frame_state().look_back,"R3 hold remains independent of vertical shift");
    }
    {
        Rig r;r.pad.active_id_mask=1;r.input.poll(r.pad);r.step();
        r.pad.sticks[1].y=32767;r.input.poll(r.pad);
        r.pad.sticks[1].y=0;r.input.poll(r.pad);
        check(r.step()==VehicleShift::up&&r.step()==VehicleShift::none,"merged-source fallback retains stick flicks");
    }
    {
        nfsu::DriveButtonQueue q(nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_right));
        nfsu::GamepadFrame f;f.pads[0].connected=true;q.observe(f);
        for(unsigned i=0;i<64;++i){f.pads[0].buttons^=nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_right);q.observe(f);}
        check(q.overflows()==1&&q.pending()<=64,"bounded queue reports overflow without unbounded allocation");
    }
    std::cout<<"PASS drag lane/shift HID transitions, held magnitudes, reversals, zero-world-step polls, pause, reconnect and HID styles\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
