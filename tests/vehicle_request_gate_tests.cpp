#include "nfsu/player_powertrain.h"
#include <iostream>

namespace {
struct Witness {
    const char* name;
    bool actual_prefix;
    int gear,mode,request;
    float engine_rad,up_offset,down_offset;
    double actor_rpm,wheel_rpm;
    std::array<bool,4> contact;
    std::array<float,4> current_slip,spin;
    int original_request;
};
// Authenticated stock constructor; actual477698 caller clip, automatic-1;
// full46E5B0/46D700/46E940 under007F and ordinary global flags0. These are
// request operations on held inputs, not an original whole-player trajectory.
const Witness witnesses[] = {
#include "request_gate_native.inc"
};
int failures=0;
void check(bool ok,const char* label) {
    if(!ok){std::cerr<<"FAIL: "<<label<<'\n';++failures;}
}
}

int main() {
    using namespace nfsu;
    const VehicleModel model(stock_240sx_vehicle_spec());VehicleState reset;
    check(model.reset(reset,VehiclePose{}),"request reset");
    // This is the current builtin before its first request. Its geometry,
    // forces and history are retained for the declared raw-slip cofactors.
    VehicleState actual=reset;VehicleScript script;
    check(builtin_vehicle_script("accelerate",script),"actual request prefix script");
    for(unsigned k=0;k<575;++k) {
        VehicleTelemetry t;
        check(model.step(actual,script.input_at(k),VehicleFlatGround{},t),"actual request prefix step");
    }
    unsigned changes=0,holds=0,actual_cases=0;
    for(const auto& p:witnesses) {
        auto state=p.actual_prefix?actual:reset;
        if(p.actual_prefix) {
            ++actual_cases;
            check(state.engine.angular_speed==p.engine_rad&&state.gear==p.gear&&
                  state.transmission.mode==p.mode&&state.transmission.up_offset==p.up_offset&&
                  state.transmission.down_offset==p.down_offset,
                  "actual prefix retains captured physical selection operands");
        }
        state.gear=p.gear;state.transmission.mode=p.mode;
        state.transmission.requested_gear=p.request;state.transmission.clutch=1;
        state.transmission.up_offset=p.up_offset;state.transmission.down_offset=p.down_offset;
        state.engine.angular_speed=p.engine_rad;state.engine_rpm=p.actor_rpm;state.wheel_rpm=p.wheel_rpm;
        for(unsigned k=0;k<4;++k) {
            state.wheels[k].contact=p.contact[k];state.wheels[k].prepared_slip=p.current_slip[k];
            state.wheels[k].spin=p.spin[k];
        }
        VehicleInput input;input.throttle=1;VehicleTelemetry t;
        check(model.step(state,input,VehicleFlatGround{},t),"held request runtime call");
        const bool match=state.transmission.requested_gear==p.original_request;
        if(!match)std::cerr<<"request case "<<p.name<<" expected "<<p.original_request
                           <<" actual "<<state.transmission.requested_gear<<'\n';
        check(match,"original current-slip/contact/request-priority witness");
        changes+=p.original_request!=p.request?1U:0U;holds+=p.original_request==p.request?1U:0U;
    }
    check(changes>0&&holds>0&&actual_cases==3,"request gate coverage exercises changed and held actual requests");
    // These finite witnesses intentionally do not accept the legacy double
    // shift-table/offset/safety arithmetic. The four saved precision records
    // are checked separately by vehicle_request_precision_tests; they remain
    // preserved. Neither finite target establishes full selection parity.
    std::cout<<"SUMMARY request gate failures="<<failures<<" changed="<<changes<<" held="<<holds<<'\n';
    if(failures)return 1;
    std::cout<<"PASS finite current160/contact gates and up-candidate priority; precision has a separate target\n";
    return 0;
}
