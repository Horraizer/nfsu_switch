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
    check(a.transmission.mode==3&&a.transmission.clutch==1.0F&&
          a.transmission.requested_gear==a.gear&&a.transmission.previous_slip_rpm==0.0F,
          "original completed reset closes the clutch with matching current/requested gear");
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
    check(a.transmission.mode==3&&a.transmission.clutch==1.0F,
          "stopped reset also uses authentic completed gearbox state");
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

void steered_constraint_witness() {
    using namespace nfsu;
    const auto h=stock_240sx_handling();
    for (const bool rotated_body:{false,true}) {
    std::array<PlayerWheelWork,4> wheels{};
    for (std::size_t k=0;k<4;++k) {
        auto& w=wheels[k];
        w.spin=26.0F;w.contact=true;w.load=3.0F;w.service=1.0F;w.owner_speed=8.3165F;
        const float hx=k<2?0.96F:1.0F,hy=k<2?0.28F:0.0F;
        w.heading_x=hx;w.heading_y=hy;w.side_x=-hy;w.side_y=hx;
        const float bx=rotated_body?0.6F:1.0F,by=rotated_body?0.8F:0.0F;
        const float vx=rotated_body?4.9899F:8.316501835256739F,vy=rotated_body?6.6532F:0.0F;
        const float global_hx=bx*hx-by*hy,global_hy=by*hx+bx*hy;
        const float global_sx=bx*(-hy)-by*hx,global_sy=by*(-hy)+bx*hx;
        w.velocity_long=vy*global_hy+vx*global_hx;
        w.velocity_lat=vy*global_sy+vx*global_sx;
        w.position_x=h.wheels[k].longitudinal-h.centre_of_mass[0];
        w.position_y=h.wheels[k].lateral;
        player_wheel_prepare(h,k,w);
    }
    for (unsigned pass=0;pass<5;++pass) player_body_wheel_constraints(h,wheels,0.0F,0.0F);
    for (std::size_t k=0;k<4;++k) player_wheel_limit(h,k,wheels[k]);
    // Original full478570/587830: neutral, initial spin26, supplied front
    // relative basis(0.96,0.28), body identity or(0.6,0.8), stock geometry,
    // load3/service1. Each explicit original fixture pins these force bits.
    // B0/C0 and B0/B4 differ in this fixture; straight cases cannot expose it.
    const std::array<float,4> original_long{{-2.1532840728759766F,-2.481177806854248F,
                                          -3.9759674072265625F,-3.482743263244629F}};
    const std::array<float,4> original_lat{{4.617764949798584F,4.421684741973877F,0.0F,0.0F}};
    for (std::size_t k=0;k<4;++k)
        check(wheels[k].force_long==original_long[k]&&wheels[k].force_lat==original_lat[k],
              "steered original constraint witness retains distinct source matrix operands");
    }
}

void strut_hub_and_world_force_witness() {
    using namespace nfsu;
    const std::array<VehicleVec3,3> identity{{{1,0,0},{0,1,0},{0,0,1}}};
    const std::array<VehicleVec3,3> pitch{{{.96,0,-.28},{0,1,0},{.28,0,.96}}};
    const auto plain=player_hub_velocity(identity,{1.14,.73,-.33},{4,2,.1},{.2,-.3,.4});
    const auto pitched=player_hub_velocity(pitch,{1.14,.73,-.33},{4,2,.1},{.2,-.3,.4});
    // Original588900..5889C2, supplied current matrices and hub arm.
    check(plain.x==3.806999921798706&&plain.y==2.5220000743865967&&
          plain.z==.5880000591278076,"original hub velocity uses current rigid rotation");
    check(pitched.x==3.8987998962402344&&pitched.y==2.5279998779296875&&
          pitched.z==.5466000437736511,"pitched original hub velocity retains float order");
    const auto at_contact=player_hub_velocity(identity,{1.14,.73,-.63},{4,2,.1},{.2,-.3,.4});
    check(at_contact.y!=plain.y,"contact arm cannot substitute for hub velocity input");
    const std::array<VehicleVec3,3> yaw{{{.6,.8,0},{-.8,.6,0},{0,0,1}}};
    const auto snapshot=player_world_wheel_axes(yaw,identity);
    check(snapshot[0].x==static_cast<double>(.6F)&&snapshot[0].y==static_cast<double>(.8F)&&
          snapshot[1].x==static_cast<double>(-.8F),"world wheel basis uses supplied rigid snapshot");
    const std::array<VehicleVec3,3> delivery_pitch{{{.8,0,-.6},{0,1,0},{.6,0,.8}}};
    const auto force=player_world_wheel_force(delivery_pitch,1,2,3);
    // Full587830 default basis1, independently executed by the reviewer.
    check(force.x==2.6000001430511475&&force.y==2&&force.z==1.8000000715255737,
          "original full wheel force includes Fx Fy load through pitched world axes");
    const auto h=stock_240sx_handling();
    check(player_body_inverse_yaw(h,false)==.42239347100257874F&&
          player_body_inverse_yaw(h,true)==.21119673550128937F,
          "original normal and alternate yaw response remain distinct from wheel constraints");
}

void outer_slice_and_selection_phase() {
    using namespace nfsu;
    const VehicleModel model(stock_240sx_vehicle_spec());
    VehiclePose pose;pose.speed=25;
    VehicleState state;check(model.reset(state,pose),"cadence pose reset");
    const auto cached=state.owner_speeds;const double published=state.engine_rpm;
    VehicleInput input;input.brake=1;
    VehicleTelemetry t;
    for(unsigned k=0;k<4;++k) {
        check(model.step(state,input,VehicleFlatGround(),t,k==3?1.0/60.0:0.0,1.0/240.0,k==0),
              "four original-branch slices complete one outer interval");
        check(t.solver_dt==1.0/240.0,"telemetry identifies actual integration interval");
        if(k<3) check(state.engine_rpm==published&&state.owner_speeds.forward==cached.forward&&
                     state.owner_speeds.magnitude==cached.magnitude,"only last slice publishes completed owner fields");
    }
    check(state.step==4&&std::fabs(state.simulation_seconds-1.0/60.0)<1e-15,
          "four slices advance elapsed clock by one frame rather than four legacy steps");
    check(state.owner_speeds.magnitude!=cached.magnitude,"outer publication follows all four completed slices");
    check(model.reset(state,VehiclePose()),"selection phase reset");
    input={};input.throttle=1;
    check(model.step(state,input,VehicleFlatGround(),t,0,1.0/240.0,true),"first pre-loop selection slice");
    state.engine.angular_speed=8000.0F*kPlayerRpmToRadS;
    state.transmission.mode=3;state.transmission.clutch=1;
    const auto request=state.transmission.requested_gear;
    auto reselection=state;
    check(model.step(state,input,VehicleFlatGround(),t,0,1.0/240.0,false)&&
          state.transmission.requested_gear==request,
          "RPM threshold crossing between slices cannot reselect the request");
    check(model.step(reselection,input,VehicleFlatGround(),t,0,1.0/240.0,true)&&
          reselection.transmission.requested_gear==request+1,
          "same above-threshold input selects at the next outer selection boundary");
    check(model.reset(state,VehiclePose()),"manual edge reset");
    state.gearbox=VehicleGearbox::manual;input.shift=VehicleShift::up;
    for(unsigned k=0;k<4;++k)
        check(model.step(state,input,VehicleFlatGround(),t,0,1.0/240.0,k==0),"manual held outer edge slices");
    check(state.transmission.requested_gear==kVehicleGearFirst+1,"held outer shift is consumed only once");
    check(model.reset(state,VehiclePose()),"airborne flag reset");
    for(auto& wheel:state.wheels) wheel.contact=false;
    state.published_support=0;
    input={};
    check(model.step(state,input,VehicleFlatGround(),t,0,1.0/240.0,true)&&!state.alternate_yaw_active,
          "outer zero-support count selects normal yaw tensor");
    check(model.step(state,input,VehicleFlatGround(),t,0,1.0/240.0,false)&&!state.alternate_yaw_active,
          "support regained during a slice does not change latched outer tensor");
    check(model.step(state,input,VehicleFlatGround(),t,0,1.0/240.0,true)&&state.alternate_yaw_active,
          "next supported outer boundary selects alternate tensor");
    const auto before=state;
    check(!model.step(state,input,VehicleFlatGround(),t,0,0,false)&&state.step==before.step&&
          state.simulation_seconds==before.simulation_seconds,"invalid solver interval refusal is atomic");
    VehicleTelemetry other=t;other.simulation_seconds+=1;
    std::array<std::uint8_t,kVehicleTelemetryRecordBytes> a{},b{};
    encode_vehicle_telemetry(t,a);encode_vehicle_telemetry(other,b);
    check(a!=b,"canonical telemetry retains cutoff clock futures");
    other=t;other.alternate_yaw_active=!t.alternate_yaw_active;encode_vehicle_telemetry(other,b);
    check(a!=b,"canonical telemetry retains latched yaw tensor futures");
}

void body_derivative_epoch() {
    using namespace nfsu;
    const VehicleModel model(stock_240sx_vehicle_spec());
    for(const double dt:{1.0/120.0,1.0/240.0}) {
        VehicleState state;VehicleTelemetry t;
        check(model.reset(state,VehiclePose()),"body derivative rest reset");
        // Isolate the original body-only kernel from the separately recovered
        // post-refresh rest callback: supplied prepared force and input wake.
        state.forces_prepared=true;state.prepared_force={0,19460,0};
        VehicleInput wake;wake.throttle=1;
        const auto before=state;
        check(model.step(state,wake,VehicleFlatGround(),t,0,dt,true),
              "body derivative rest slice completes");
        // Actual body-only46AA40/585410 witness: zero old velocity leaves
        // position fixed even when supplied force changes the new momentum.
        check(state.position.x==static_cast<float>(before.position.x) && state.position.y==static_cast<float>(before.position.y) &&
              state.position.z==static_cast<float>(before.position.z),
              "first body position uses old velocity despite nonzero wheel force");
        check(state.velocity.y!=before.velocity.y,
              "supplied body force changes velocity when original input gate excludes rest");
        check(model.reset(state,VehiclePose()),"moving derivative reset");
        state.velocity={4,2,.1};
        const auto moving=state;
        check(model.step(state,VehicleInput{},VehicleFlatGround(),t,0,dt,true),
              "moving body derivative slice completes");
        const auto integrated_position=[dt](double position,double velocity) {
            return static_cast<float>(position)+static_cast<float>(dt)*static_cast<float>(velocity);
        };
        check(state.position.x==integrated_position(moving.position.x,moving.velocity.x) &&
              state.position.y==integrated_position(moving.position.y,moving.velocity.y) &&
              state.position.z==integrated_position(moving.position.z,moving.velocity.z),
              "position derivative keeps the pre-force velocity epoch at both intervals");
        VehiclePose pose;pose.speed=15;
        check(model.reset(state,pose),"rotation derivative moving reset");
        state.angular_velocity={.2,-.3,.4};
        state.forces_prepared=true;state.prepared_torque={0,0,1000};
        auto turning=state;
        turning.prepared_torque={0,0,-1000};
        VehicleInput turn;turn.throttle=1;turn.steer=1;
        check(model.step(state,wake,VehicleFlatGround(),t,0,dt,true) &&
              model.step(turning,turn,VehicleFlatGround(),t,0,dt,true),
              "paired force inputs complete body slices");
        check(state.orientation.w==turning.orientation.w &&
              state.orientation.x==turning.orientation.x &&
              state.orientation.y==turning.orientation.y &&
              state.orientation.z==turning.orientation.z &&
              state.angular_velocity.z!=turning.angular_velocity.z,
              "different prepared torques cannot change this slice's old-angular-velocity rotation");
    }
}

void completed_rest_phase() {
    using namespace nfsu;
    const VehicleModel model(stock_240sx_vehicle_spec());
    VehicleState state;VehicleTelemetry t;const VehicleFlatGround ground;
    check(model.reset(state,VehiclePose()),"rest phase reset");
    const VehicleVec3 initial{static_cast<float>(state.position.x),static_cast<float>(state.position.y),
                              static_cast<float>(state.position.z)};
    for(unsigned k=0;k<480;++k)
        check(model.step(state,{},ground,t,k%4==3?1.0/60.0:0,1.0/240.0,k%4==0),"two seconds of rest slices");
    check(state.position.x==initial.x&&state.position.y==initial.y&&state.position.z==initial.z&&
          state.velocity.x==0&&state.velocity.y==0&&state.velocity.z==0&&state.body_rest_state==2,
          "original post-refresh rest transition holds body pose despite standing forces");
    check(state.prepared_force.y!=0&&state.forces_prepared,
          "rest retains authentic next standing force rather than suppressing the producer");
    VehicleInput gas;gas.throttle=1;
    check(model.step(state,gas,ground,t,0,1.0/240.0,true)&&state.body_rest_state==0&&state.body_gravity,
          "first-gear throttle wakes genuine rest transition and restores gravity");
    check(state.velocity.x!=0||state.velocity.y!=0,"wake preserves force-created momentum");
    VehiclePose moving;moving.speed=15;
    check(model.reset(state,moving),"coast rest phase reset");
    check(model.step(state,{},ground,t,0,1.0/240.0,true)&&state.body_rest_state==0&&state.velocity.x>14,
          "coasting at speed retains momentum and excludes sleep");
    VehiclePose airborne;airborne.ground.z=3;
    check(model.reset(state,airborne),"airborne rest phase reset");
    const double height=state.position.z;
    for(unsigned k=0;k<8;++k)
        check(model.step(state,{},ground,t,k%4==3?1.0/60.0:0,1.0/240.0,k%4==0),"airborne gravity slice");
    check(state.published_support==0&&state.body_rest_state==0&&state.body_gravity&&
          state.velocity.z<0&&state.position.z<height,"unsupported spawn adapter falls with gravity");
    check(model.reset(state,VehiclePose()),"published support phase reset");
    state.forces_prepared=true;state.published_support=0;
    check(model.step(state,{},ground,t,0,1.0/240.0,false)&&state.published_support==0&&state.body_rest_state==0,
          "new contacts do not rewrite the original published support between outer exports");
    check(model.step(state,{},ground,t,1.0/60.0,1.0/240.0,false)&&state.published_support==4,
          "outer export publishes completed support");
    state.published_gear=kVehicleGearNeutral;
    check(model.step(state,gas,ground,t,0,1.0/240.0,false)&&state.published_gear==kVehicleGearNeutral&&
          state.body_rest_state==2,"rest uses published neutral gear between exports");
    check(model.step(state,gas,ground,t,1.0/60.0,1.0/240.0,false)&&state.published_gear==state.gear,
          "outer export publishes completed gear");
    check(model.step(state,gas,ground,t,0,1.0/240.0,true)&&state.body_rest_state==0,
          "next original input gate observes the published drive gear");
    std::array<std::uint8_t,kVehicleTelemetryRecordBytes> a{},b{};
    encode_vehicle_telemetry(t,a);auto other=t;other.prepared_force.y+=1;encode_vehicle_telemetry(other,b);
    check(a!=b,"canonical record distinguishes next body force futures");
    other=t;other.engine.feedback_torque+=1;encode_vehicle_telemetry(other,b);
    check(a!=b,"canonical record distinguishes persistent next engine feedback");
    other=t;other.wheel_prepared_slip[3]+=1;encode_vehicle_telemetry(other,b);
    check(a!=b,"canonical record distinguishes next wheel derivative slip");
    check(model.reset(state,moving),"prepared future pair reset");
    state.forces_prepared=true;auto twin=state;twin.prepared_force.y=1220;
    VehicleTelemetry twin_t;
    check(model.step(state,gas,ground,t,0,1.0/240.0,true)&&
          model.step(twin,gas,ground,twin_t,0,1.0/240.0,true),"prepared future pair runs");
    check(state.position.x==twin.position.x&&state.position.y==twin.position.y&&
          std::fabs((twin.velocity.y-state.velocity.y)-1.0/240.0)<1e-9,
          "identical body pose/velocity with different cached force has the predicted next velocity");
    auto continued=state;
    for(unsigned k=0;k<80;++k) {
        VehicleInput input=gas;input.brake=k>=40?1:0;input.handbrake=k>=60;
        check(model.step(state,input,ground,t,k%4==3?1.0/60.0:0,1.0/240.0,k%4==0)&&
              model.step(continued,input,ground,twin_t,k%4==3?1.0/60.0:0,1.0/240.0,k%4==0),
              "saved prepared state continues through gas/service/parking slices");
        encode_vehicle_telemetry(t,a);encode_vehicle_telemetry(twin_t,b);
        check(a==b,"saved state retains every prepared phase/cache future during continuation");
    }
    state.gearbox=VehicleGearbox::manual;state.transmission.requested_gear=kVehicleGearReverse;
    check(model.reset(state,VehiclePose())&&!state.forces_prepared&&state.body_rest_state==0&&
          state.body_gravity&&state.published_gear==kVehicleGearFirst&&!state.rigid_body_ready&&
          state.rigid_body.momentum==PlayerRigidVector{}&&state.rigid_body.angular_momentum==PlayerRigidVector{},
          "reset clears prepared phase and pending reverse while restoring initial gravity");
}
void recovered_rigid_component() {
    using namespace nfsu;
    const auto h=stock_240sx_handling();
    PlayerRigidBodyState body;
    body.axes={{{{.8F,0,-.6F}},{{0,1,0}},{{.6F,0,.8F}}}};
    body.position={{0,0,.5F}};body.angular_momentum={{1,-2,0}};body.alternate_yaw=true;
    player_rigid_body_refresh(h,body);
    check(body.angular_velocity==PlayerRigidVector{{1.4686863422393799F,-.8903632164001465F,-.9431172013282776F}},
          "native stock pitched world tensor refresh");
    body.force[1]=19.46F;
    player_rigid_body_integrate(h,body,1.0F/120.0F);
    check(body.position==PlayerRigidVector{{0,0,.5F}}&&
          body.momentum==PlayerRigidVector{{0,.16216666996479034F,-.0996333435177803F}},
          "native rigid old velocity pose and next momentum epochs");
    check(body.angular_momentum==PlayerRigidVector{{.9995833039283752F,-1.9991666078567505F,0}}&&
          body.angular_velocity==PlayerRigidVector{{1.4750902652740479F,-.8865600824356079F,-.9417856335639954F}},
          "native zero torque damping and completed matrix refresh change omega");
    check(body.axes[0]==PlayerRigidVector{{.8044518232345581F,.001055983710102737F,-.5940642952919006F}}&&
          body.adaptive_damping==.00083333341171965F,
          "native matrix Euler and adaptive damping state");
    const auto snapshot=body.snapshot_axes;const auto cached_omega=body.angular_velocity;
    player_rigid_body_normalize(body);
    check(body.axes[0]==PlayerRigidVector{{.8044291734695435F,.001055954024195671F,-.5940476059913635F}}&&
          body.snapshot_axes==snapshot&&body.angular_velocity==cached_omega,
          "native outer normalization retains pre-normalization snapshot and omega cache");
    PlayerRigidBodyState equal,above;
    equal.alternate_yaw=true;equal.angular_momentum[2]=4;
    player_rigid_body_refresh(h,equal);equal.yaw_torque_limit=17;equal.torque[2]=17;above=equal;
    above.torque[2]=18;
    player_rigid_body_integrate(h,equal,1.0F/240.0F);
    player_rigid_body_integrate(h,above,1.0F/240.0F);
    check(equal.angular_momentum[2]==4.070000171661377F&&above.angular_momentum[2]==4.0708332061767578F,
          "native yaw cap equality retains damping and beyond cap replaces final derivative");
}
void recovered_rigid_runtime() {
    using namespace nfsu;
    const auto spec=stock_240sx_vehicle_spec();const auto& h=spec.handling;
    const VehicleModel model(spec);VehicleInput wake;wake.throttle=1;
    for(double dt:{1.0/120.0,1.0/240.0}) for(bool alternate:{false,true}) {
        VehicleState state;VehicleTelemetry telemetry;
        check(model.reset(state,{}),"rigid runtime supplied state reset");
        state.rigid_body_ready=true;state.forces_prepared=true;
        state.alternate_yaw_active=alternate;
        auto& body=state.rigid_body;
        body.axes={{{{.8F,0,-.6F}},{{0,1,0}},{{.6F,0,.8F}}}};
        body.position={{0,0,.5F}};body.angular_momentum={{1,-2,0}};
        body.alternate_yaw=alternate;body.yaw_torque_limit=h.yaw_torque_limit;
        player_rigid_body_refresh(h,body);body.force[1]=19.46F;
        state.position={body.position[0],body.position[1],body.position[2]};
        state.velocity={body.velocity[0],body.velocity[1],body.velocity[2]};
        state.angular_velocity={body.angular_velocity[0],body.angular_velocity[1],body.angular_velocity[2]};
        state.rigid_orientation=state.orientation;
        state.prepared_force={body.force[0]*1000.0,body.force[1]*1000.0,body.force[2]*1000.0};
        auto expected=body;player_rigid_body_integrate(h,expected,static_cast<float>(dt));
        player_rigid_body_normalize(expected);
        check(model.step(state,wake,VehicleFlatGround(),telemetry,0,dt,false),"rigid runtime slice completes");
        check(state.body_rest_state==0&&state.rigid_body.position==expected.position&&
              state.rigid_body.momentum==expected.momentum&&state.rigid_body.angular_momentum==expected.angular_momentum&&
              state.rigid_body.velocity==expected.velocity&&state.rigid_body.angular_velocity==expected.angular_velocity&&
              state.rigid_body.axes==expected.axes&&state.rigid_body.snapshot_axes==expected.snapshot_axes&&
              state.rigid_body.adaptive_damping==expected.adaptive_damping,
              "runtime bridge keeps recovered momentum matrix refresh and outer normalization epochs");
        check(telemetry.rigid_body.axes==state.rigid_body.axes&&telemetry.rigid_body_ready,
              "runtime telemetry contains completed rigid state");
        std::array<std::uint8_t,kVehicleTelemetryRecordBytes> a{},b{};
        encode_vehicle_telemetry(telemetry,a);auto changed=telemetry;
        changed.rigid_body.angular_momentum[0]+=1;encode_vehicle_telemetry(changed,b);
        check(a!=b,"canonical state distinguishes angular momentum futures");
        changed=telemetry;changed.rigid_body.snapshot_axes[2][0]+=1;encode_vehicle_telemetry(changed,b);
        check(a!=b,"canonical state distinguishes retained matrix snapshots");
        changed=telemetry;changed.rigid_body.adaptive_damping+=1;encode_vehicle_telemetry(changed,b);
        check(a!=b,"canonical state distinguishes adaptive damping futures");
        auto edited=state;edited.velocity={4,2,0};
        auto imported=edited.rigid_body;
        imported.momentum={{4*h.mass,2*h.mass,0}};
        player_rigid_body_refresh(h,imported);
        const PlayerRigidVector old_position{{static_cast<float>(edited.position.x),
            static_cast<float>(edited.position.y),static_cast<float>(edited.position.z)}};
        PlayerRigidVector expected_position=old_position;
        for(int k=0;k<3;++k) expected_position[k]+=static_cast<float>(dt)*imported.velocity[k];
        check(model.step(edited,wake,VehicleFlatGround(),telemetry,0,dt,false)&&
              edited.rigid_body.position==expected_position,
              "explicit public velocity edit imports momentum and refreshes the old velocity cache");
        auto invalid=state;invalid.rigid_body.angular_momentum[0]=std::numeric_limits<float>::quiet_NaN();
        check(!model.step(invalid,wake,VehicleFlatGround(),telemetry,0,dt,false),
              "nonfinite authoritative angular momentum is refused before integration");
        invalid=state;invalid.rigid_body.snapshot_axes[2][1]=std::numeric_limits<float>::infinity();
        check(!model.step(invalid,wake,VehicleFlatGround(),telemetry,0,dt,false),
              "nonfinite retained matrix cache is refused before integration");
    }
}
void original_front_steering_epoch() {
    using namespace nfsu;const auto h=stock_240sx_handling();
    const auto axes=player_front_steering_axes(h,4096);
    check(axes[0][0].x==.9238796234130859F&&axes[0][0].y==.3826834261417389F&&
          axes[1][0].x==.9498887658119202F&&axes[1][0].y==.3125891089439392F,
          "original controls word4096 produces distinct front0/front1 matrices");
    const auto negative=player_front_steering_axes(h,61440);
    check(negative[0][0].x==.9498587250709534F&&negative[0][0].y==-.31258895993232727F&&
          negative[1][0].x==.9238796234130859F&&negative[1][0].y==-.3825950026512146F,
          "original negative word preserves native front exchange and table quantization");
    const VehicleModel model(stock_240sx_vehicle_spec());VehicleState state;VehicleTelemetry t;
    check(model.reset(state,{}),"front steering epoch reset");
    state.position={0,0,.5};state.orientation={1,0,0,0};state.velocity={15,0,0};state.forces_prepared=true;
    VehicleInput input;input.road_steering=true;input.road_angle_word_valid=true;
    input.road_angle_word=4096;input.road_angle=-1;input.throttle=1;
    check(model.step(state,input,VehicleFlatGround{},t,0,1.0/240.0,true),"original-word front steering slice runs");
    const float speed=static_cast<float>(state.velocity.x);
    const double expected0=static_cast<double>(-static_cast<float>(axes[0][0].y)*speed)/
        (static_cast<float>(axes[0][0].x)*speed);
    const double expected1=static_cast<double>(-static_cast<float>(axes[1][0].y)*speed)/
        (static_cast<float>(axes[1][0].x)*speed);
    check(t.wheels[0].slip_lateral==expected0&&t.wheels[1].slip_lateral==expected1&&expected0!=expected1,
          "runtime front velocities consume the distinct original matrices rather than one shared basis");
    check(state.steering_word==4096&&t.steering_word==4096&&state.steering_word_ready,
          "runtime consumes exact original control word rather than the radian compatibility field");
    input.road_angle_word=8192;
    for(int k=0;k<3;++k) {
        check(model.step(state,input,VehicleFlatGround{},t,k==2?1.0/60.0:0,1.0/240.0,false),
              "continuing original steering callback slice runs");
        check(state.steering_word==4096&&t.steering_word==4096,
              "strut callbacks retain the outer-selected matrices until the next selection");
    }
    check(model.step(state,input,VehicleFlatGround{},t,0,1.0/240.0,true)&&state.steering_word==8192,
          "next outer selection consumes the new processed word");
    std::array<std::uint8_t,kVehicleTelemetryRecordBytes> a{},b{};
    encode_vehicle_telemetry(t,a);auto changed=t;changed.steering_word=4096;encode_vehicle_telemetry(changed,b);
    check(a!=b,"canonical state distinguishes retained steering matrix futures");
    check(model.reset(state,{})&&!state.steering_word_ready&&state.steering_word==0,
          "reset clears the retained original steering epoch");
}
void original_contact_force_and_tyre_wrap() {
    using namespace nfsu;
    PlayerRigidBodyState body;body.force={{1,-2,-11.956000328063965F}};
    body.torque={{.3F,-.4F,.5F}};
    player_rigid_body_add_contact_force(body,{1500,0,0},{0,.075,0});
    check(body.force==PlayerRigidVector{{1201,-2,-11.956000328063965F}}&&
          body.torque==PlayerRigidVector{{.3F,-.4F,-99.5F}},
          "original force cap and independent moment cap use the uncapped supplied force");
    // Authentic full4783C0/587830 fixture: original word4096, cached V15,
    // front0 pre-limit fields and original completed force from native-r7.
    PlayerWheelWork wheel;wheel.force_long=.8601332306861877F;
    wheel.force_lat=4.417034149169922F;wheel.load=3.0000014305114746F;
    wheel.velocity_long=13.858195304870605F;wheel.velocity_lat=-5.740251541137695F;
    wheel.sliding=true;wheel.contact=true;
    player_wheel_limit(stock_240sx_handling(),0,wheel);
    check(wheel.force_long==1.7051007747650146F&&wheel.force_lat==4.417034149169922F,
          "native tyre angle wrap constants preserve the original quantized lateral drag word");
}
void original_strut_partner_epoch() {
    using namespace nfsu;
    auto spec=stock_240sx_vehicle_spec();
    for(auto& w:spec.handling.wheels) w.vertical=spec.handling.centre_of_mass[2]-.25F;
    const VehicleModel model(spec);VehicleState a;VehicleTelemetry ta,tb;
    check(model.reset(a,{}),"strut partner epoch reset");
    a.position={0,0,.5};a.velocity={};a.orientation={1,0,0,0};
    a.forces_prepared=true;a.prepared_force={};a.prepared_torque={};a.body_rest_state=0;
    a.wheels[0].compression=.04F;a.wheels[1].compression=0;
    auto b=a;b.wheels[1].compression=-.02F;
    auto c=a;auto d=a;d.wheels[0].compression=-.02F;
    VehicleInput input;input.throttle=1;input.road_steering=true;
    input.road_angle_word_valid=true;input.road_angle_word=0;
    check(model.step(a,input,VehicleFlatGround{},ta,0,1.0/240.0,true)&&
          model.step(b,input,VehicleFlatGround{},tb,0,1.0/240.0,true),
          "strut partner epoch twin callbacks complete");
    check(a.wheels[0].compression==b.wheels[0].compression&&
          a.wheels[1].compression==b.wheels[1].compression&&ta.wheels[0].contact&&tb.wheels[0].contact,
          "same query produces matching new compressions despite different prior partner states");
    // Full original588580/46ABD0 witnesses retain that prior partner
    // dependency. The nominal force law remains the qualified double adapter.
    const double expected=double(.02F)*spec.handling.struts[0].anti_roll*spec.approx.unit_scale;
    check(std::abs((tb.wheels[0].load-ta.wheels[0].load)-expected)<1e-9,
          "first strut force consumes the previous partner rather than all-new compression");
    check(model.step(c,input,VehicleFlatGround{},ta,0,1.0/240.0,true)&&
          model.step(d,input,VehicleFlatGround{},tb,0,1.0/240.0,true),
          "second strut partner epoch twin callbacks complete");
    check(c.wheels[0].compression==d.wheels[0].compression&&
          c.wheels[1].compression==d.wheels[1].compression&&ta.wheels[1].load==tb.wheels[1].load,
          "second strut consumes its already-updated partner despite different prior first-strut states");
}
} // namespace

int main() {
    publication_and_reset();wheel_history_and_player_brake();shift_cycle_and_differential_sync();
    owner_speed_domains();steered_constraint_witness();strut_hub_and_world_force_witness();
    outer_slice_and_selection_phase();
    body_derivative_epoch();
    completed_rest_phase();
    recovered_rigid_component();
    recovered_rigid_runtime();
    original_front_steering_epoch();
    original_contact_force_and_tyre_wrap();
    original_strut_partner_epoch();
    if (failures) {std::cerr<<failures<<" player powertrain test failure(s)\n";return 1;}
    std::cout<<"player powertrain tests passed\n";
}
