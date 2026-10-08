#include "nfsu/player_powertrain.h"
#include <iostream>
#include <string>
#include <vector>

namespace {
struct Phase {
    int gear,request,mode;
    float clutch;
};
struct Update { Phase entry,exit; };
std::vector<Update> updates;
int failures=0;
void check(bool ok,const char* label) {
    if(!ok){std::cerr<<"FAIL: "<<label<<'\n';++failures;}
}
Phase phase(int gear,const nfsu::PlayerGearState& box) {
    return {gear,box.requested_gear,box.mode,box.clutch};
}
}

namespace nfsu {
// Only the separately compiled test vehicle object redirects this call.
// Observe every invocation and forward to the unchanged original helper.
void nfsu_test_player_gear_update(const VehicleHandling& h,int& gear,PlayerGearState& box,
                                PlayerEngineState& engine,std::array<PlayerWheelWork,4>& wheels,
                                float throttle,float owner_speed_magnitude) noexcept {
    const auto entry=phase(gear,box);
    player_gear_update(h,gear,box,engine,wheels,throttle,owner_speed_magnitude);
    updates.push_back({entry,phase(gear,box)});
}
}

namespace {
unsigned attribute_updates(int outer_before,int outer_after) {
    int current=outer_before;
    unsigned changed=0;
    check(!updates.empty(),"ordinary step exercises observed gear updates");
    for(const auto& event:updates) {
        check(event.entry.gear==current,"no physical gear write before an observed callback");
        if(event.entry.mode==1) {
            //46DE9A..46DE9D has no primitive clutch gate.
            check(event.exit.gear==event.entry.request,"mode1 copies its own retained request");
        } else {
            check(event.exit.gear==event.entry.gear,"nonmode1 callback preserves physical gear");
        }
        changed+=event.exit.gear!=event.entry.gear?1U:0U;
        current=event.exit.gear;
    }
    check(current==outer_after,"outer physical gear is attributed to observed callbacks");
    return changed;
}

void explicit_mode1_cases() {
    using namespace nfsu;
    const auto h=stock_240sx_handling();
    for(float clutch:{0.0F,0.5F}) {
        PlayerGearState box;box.mode=1;box.clutch=clutch;box.requested_gear=3;
        PlayerEngineState engine;player_engine_reset(engine,h.idle_rpm);
        std::array<PlayerWheelWork,4> wheels{};int gear=2;updates.clear();
        nfsu_test_player_gear_update(h,gear,box,engine,wheels,1,0);
        check(updates.size()==1,"explicit mode1 helper observed once");
        check(attribute_updates(2,gear)==1,"explicit helper exercises a physical copy");
        check(gear==3&&box.clutch==clutch,"mode1 helper copies request without a clutch gate");

        const VehicleModel model(stock_240sx_vehicle_spec());VehicleState state;
        check(model.reset(state,VehiclePose{}),"explicit public state reset");
        state.transmission.mode=1;state.transmission.clutch=clutch;
        state.transmission.requested_gear=3;updates.clear();VehicleTelemetry telemetry;
        VehicleInput input;input.throttle=1;
        check(model.step(state,input,VehicleFlatGround{},telemetry),"valid externally set mode1 public state");
        check(attribute_updates(2,state.gear)==1,"initial public state exercises a physical copy");
        check(updates.size()==2,"initial force preparation exercises both callbacks");
        if(updates.size()==2) {
            check(updates[0].entry.mode==1&&updates[0].entry.clutch==clutch&&
                  updates[0].entry.gear==2&&updates[0].exit.gear==3,
                  "initial callback copies request at supplied clutch0 or0.5");
            check(updates[1].entry.mode==2&&updates[1].entry.gear==3&&updates[1].exit.gear==3,
                  "second initial callback observes the first callback's physical copy");
        }
    }
}

void reachable_prefix(const char* name,unsigned limit) {
    using namespace nfsu;
    VehicleScript script;check(builtin_vehicle_script(name,script),"builtin phase prefix");
    const VehicleModel model(stock_240sx_vehicle_spec());VehiclePose pose;
    pose.speed=script.speed;pose.gearbox=script.gearbox;VehicleState state;
    check(model.reset(state,pose),"reachable phase reset");
    bool opened_by_mode0=false;unsigned requests=0,copies=0,callbacks=0,first_change=0;
    for(unsigned k=0;k<limit;++k) {
        const auto before=state;updates.clear();VehicleTelemetry telemetry;
        check(model.step(state,script.input_at(k),VehicleFlatGround{},telemetry),"reachable phase step");
        if(k==0)check(updates.size()==2,"ordinary initial step has two separately observed updates");
        requests+=state.transmission.requested_gear!=before.transmission.requested_gear?1U:0U;
        copies+=attribute_updates(before.gear,state.gear);callbacks+=static_cast<unsigned>(updates.size());
        // Only these reset prefixes have no external mode/clutch edits.
        // Their open-clutch assertion comes from the preceding mode0 clamp,
        // not a restriction on the primitive or externally supplied state.
        for(const auto& event:updates) {
            if(event.entry.mode==1)
                check(opened_by_mode0&&event.entry.clutch==0,"reachable mode1 follows mode0 open-clutch clamp");
            if(event.entry.mode==0&&event.exit.mode==1) {
                check(event.entry.clutch<=0&&event.exit.clutch==0,"mode0 clamps before entering mode1");
                opened_by_mode0=true;
            }
            if(event.exit.mode!=1)opened_by_mode0=false;
        }
        if(!first_change&&state.gear!=before.gear)first_change=k+1;
        if(first_change&&k+1>=first_change+2)break;
    }
    check(requests>0,"reachable prefix exercises a request change");
    check(copies>0,"reachable prefix exercises an observed physical copy");
    std::cout<<"phase prefix "<<name<<": requests="<<requests<<" copies="<<copies
             <<" callbacks="<<callbacks<<'\n';
}
}

int main() {
    explicit_mode1_cases();reachable_prefix("accelerate",620);reachable_prefix("brake",360);
    std::cout<<"SUMMARY gear phase failures="<<failures<<'\n';
    if(failures)return 1;
    std::cout<<"PASS individual gear updates, initial attribution and reachable clutch lifecycle\n";
    return 0;
}
