#include "nfsu/player_powertrain.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
int failures=0;
void check(bool ok,const char* label) {
    if (!ok) {std::cerr<<"test failure: "<<label<<'\n';++failures;}
}

void publication_and_reset() {
    using namespace nfsu;
    const VehicleModel model(stock_240sx_vehicle_spec());
    VehicleState a,b;VehiclePose pose;pose.speed=25.0;
    check(model.reset(a,pose),"moving reset");b=a;
    a.engine_rpm=7000.0;b.engine_rpm=3000.0;
    VehicleInput input;input.brake=1.0;
    VehicleTelemetry ta,tb;
    const auto cached=a.owner_speeds;
    for (unsigned step=0;step<48;++step) {
        check(model.step(a,input,VehicleFlatGround(),ta,0.0),"unpublished first solver slice");
        check(model.step(b,input,VehicleFlatGround(),tb,0.0),"unpublished twin slice");
        check(a.engine_rpm==7000.0&&b.engine_rpm==3000.0,"suppressed publication retains actor values");
        check(a.owner_speeds.forward==cached.forward&&a.owner_speeds.magnitude==cached.magnitude,
              "solver slices retain previous outer-frame actor speed fields");
        check(a.engine.angular_speed==b.engine.angular_speed&&a.velocity.x==b.velocity.x&&
              a.transmission.clutch==b.transmission.clutch&&a.gear==b.gear&&
              a.wheels[2].spin==b.wheels[2].spin,"actor RPM does not feed physical engine or gearbox");
    }
    const float previous=static_cast<float>(a.engine_rpm);
    check(model.step(a,input,VehicleFlatGround(),ta,1.0/60.0),"unsplit publication slice");
    const float maximum_drop=(2.5F/model.spec().handling.engine_inertia)*(1.0F/60.0F);
    check(a.engine_rpm>=previous-maximum_drop-0.001F,"falling actor RPM respects original unsplit slope");
    check(a.engine_rpm!=a.wheel_rpm,"falling publication remains distinct from mechanical wheel RPM");
    check(a.owner_speeds.magnitude!=cached.magnitude,"outer publication refreshes completed body speed");
    a.wheels[2].history_torque=9.0F;a.transmission.shift_pending=true;a.engine.cutoff_until=99.0F;
    check(model.reset(a,VehiclePose()),"reset after mechanical history");
    check(a.wheels[2].history_torque==0.0F&&a.wheels[2].spin_direction==1&&
          !a.transmission.shift_pending&&a.engine.cutoff_until==0.0F,
          "reset clears wheel history, shift request and cutoff");
    check(std::fabs(a.engine.angular_speed*kPlayerRadSToRpm-model.spec().handling.idle_rpm)<0.001F,
          "reset initializes physical angular speed once");
    const auto before=a;
    check(!model.step(a,input,VehicleFlatGround(),ta,std::numeric_limits<double>::quiet_NaN()),
          "nonfinite publication interval refused");
    check(a.step==before.step&&a.engine.angular_speed==before.engine.angular_speed,"refusal preserves state");
}

void wheel_history_and_player_brake() {
    using namespace nfsu;
    const auto h=stock_240sx_handling();
    PlayerWheelWork wheel;wheel.spin=26.0F;wheel.contact=true;wheel.load=3.0F;
    wheel.service=1.0F;wheel.velocity_long=8.316501835256739F;wheel.owner_speed=wheel.velocity_long;
    player_wheel_prepare(h,0,wheel);
    check(!wheel.locked&&wheel.spin==26.0F,"player full service does not apply selector3 front26 pin");
    PlayerWheelWork slow;slow.spin=16.0F;slow.contact=true;slow.load=3.0F;
    slow.service=1.0F;slow.velocity_long=8.316501835256739F;slow.owner_speed=slow.velocity_long;
    player_wheel_prepare(h,2,slow);
    check(!slow.locked&&slow.spin==16.0F,
          "capacity includes longitudinal velocity demand, preserving original rear16 rolling state");
    PlayerWheelWork rear;rear.spin=26.0F;rear.contact=true;rear.load=3.0F;
    rear.service=1.0F;rear.velocity_long=8.316501835256739F;rear.velocity_lat=4.0F;
    rear.owner_speed=9.227F;
    player_wheel_prepare(h,2,rear);
    check(!rear.sliding&&rear.force_lat<0.0F,
          "original radians threshold retains standing rear26 tyre in lateral4 fixture");
    auto twin=wheel;twin.history_torque=0.25F;
    const float rate=player_wheel_acceleration(h,0,wheel,1.0F/120.0F);
    const float twin_rate=player_wheel_acceleration(h,0,twin,1.0F/120.0F);
    player_wheel_integrate(wheel,rate,1.0F/120.0F);
    player_wheel_integrate(twin,twin_rate,1.0F/120.0F);
    check(twin.spin!=wheel.spin,"same current spin and force with different torque history has different future");
    VehicleTelemetry ta,tb;ta.wheels[0].spin=tb.wheels[0].spin=26.0;
    tb.wheel_history_torque[0]=0.25F;
    std::array<std::uint8_t,kVehicleTelemetryRecordBytes> ba{},bb{};
    encode_vehicle_telemetry(ta,ba);encode_vehicle_telemetry(tb,bb);
    check(ba!=bb,"canonical telemetry identifies torque-history futures");
    tb=ta;tb.transmission.requested_gear=3;
    encode_vehicle_telemetry(tb,bb);check(ba!=bb,"canonical telemetry identifies pending gear request");
}

void shift_cycle_and_differential_sync() {
    using namespace nfsu;
    const auto h=stock_240sx_handling();
    PlayerEngineState engine;player_engine_reset(engine,5000.0F);
    PlayerGearState box;box.mode=3;box.clutch=1.0F;box.requested_gear=3;player_gear_seed(box,12345U);
    std::array<PlayerWheelWork,4> wheels{};
    for (auto& w:wheels) {w.spin=30.0F;w.contact=true;}
    int gear=2;
    player_gear_update(h,gear,box,engine,wheels,1.0F);
    check(gear==2&&box.mode==0&&box.shift_pending,"shift request starts disengagement before gear changes");
    unsigned ticks=0;
    while (gear==2&&ticks<120) {
        player_gear_integrate(h,box,1.0F/120.0F);
        player_gear_update(h,gear,box,engine,wheels,1.0F);++ticks;
    }
    check(gear==3&&box.mode==2&&ticks>1&&!box.shift_pending,"gear changes through open clutch before engagement");
    check(box.rng_state!=808147100U&&box.up_offset>=-100.0F&&box.up_offset<=0.0F,
          "completed shift consumes deterministic RNG offset");
    for (const float speed:{0.0F,30.0F}) {
        player_engine_reset(engine,6000.0F);box.mode=3;box.requested_gear=gear;
        wheels[2].spin=20.0F;wheels[3].spin=40.0F;wheels[2].slip=wheels[3].slip=1.0F;
        player_gear_update(h,gear,box,engine,wheels,1.0F,speed);
        check(speed==0.0F?wheels[2].spin==wheels[3].spin:
              std::fabs((wheels[3].spin-wheels[2].spin)-20.0F)<0.0001F,
              "wheel synchronization respects low versus high owner forward-speed branch");
    }
    player_engine_reset(engine,6000.0F);wheels[2].spin=20.0F;wheels[3].spin=40.0F;
    wheels[2].contact=wheels[3].contact=false;
    player_gear_update(h,gear,box,engine,wheels,1.0F,30.0F);
    check(wheels[2].spin==wheels[3].spin,"airborne driven pair uses averaged synchronization");
}

void owner_speed_domains() {
    using namespace nfsu;
    const VehicleVec3 forward{1.0,0.0,0.0};
    const auto reverse=player_publish_owner_speeds({-8.0,0.0,0.0},forward);
    const auto lateral=player_publish_owner_speeds({0.0,4.0,0.0},forward);
    check(reverse.forward==-8.0F&&reverse.magnitude==8.0F,
          "original owner390 is signed forward and owner394 is magnitude on reverse");
    check(lateral.forward==0.0F&&lateral.magnitude==4.0F,
          "lateral velocity separates original speed fields");
    const auto diagonal=player_publish_owner_speeds({-8.0,4.0,0.0},{0.6,0.8,0.0});
    check(diagonal.forward==-1.6000001430511475F&&diagonal.magnitude==8.944272041320801F,
          "original publisher float order matches rotated-heading witness");
    VehicleTelemetry a,b;b.owner_speeds=lateral;
    std::array<std::uint8_t,kVehicleTelemetryRecordBytes> ba{},bb{};
    encode_vehicle_telemetry(a,ba);encode_vehicle_telemetry(b,bb);
    check(ba!=bb,"canonical telemetry retains actor speed inputs across solver slices");
}
} // namespace

int main() {
    publication_and_reset();wheel_history_and_player_brake();shift_cycle_and_differential_sync();
    owner_speed_domains();
    if (failures) {std::cerr<<failures<<" player powertrain test failure(s)\n";return 1;}
    std::cout<<"player powertrain tests passed\n";
}
