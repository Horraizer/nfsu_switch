#include "nfsu/steering.h"
#include "nfsu/sha256.h"
#include "nfsu/race_run.h"
#include "vehicle_runtime_oracles.h"
#include <cmath>
#include <iostream>
#include <limits>

namespace {
int failures=0;
void check(bool okay,const char* label) {
    if (!okay) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
struct NativePin {
    nfsu::SteeringContext input;
    float limit,target,rate;
    std::uint16_t angle;
    bool assist;
};
const NativePin pins[] = {
#include "steering_native.inc"
};
}
int main() {
    using namespace nfsu;
    const auto table=steering_table_bytes();
    check(sha256_hex(sha256(table.data(),table.size()))==
        "3D0AFC80D224200C5541127121D55169343BE95EBCF16F709CAC9FDE7FEFDADA","retail table bytes rebuilt");
    for (const auto& pin:pins) {
        SteeringState s; SteeringResult r;
        check(steering_step(pin.input,s,r),"native context accepted");
        check(std::abs(r.limit_deg-pin.limit)<.0002F && std::abs(r.target_deg-pin.target)<.0002F &&
            std::abs(r.rate_deg_s-pin.rate)<.002F && r.angle==pin.angle && s.assist==pin.assist,
            "native limit, target, rate, angle word and assist pin");
    }
    // Native word sequence covers turn-in, sustained input, reversal,
    // release and a shift. Input history wraps repeatedly at 120 Hz.
    for (bool bypass : {false,true}) {
        SteeringState s; std::vector<std::uint8_t> words;
        for (int k=0;k<240;++k) {
            SteeringContext c; c.raw=k<15 || k>=180 ? 0 : k<90 ? .7F : -.3F;
            c.seconds=float(double(10000+33*k)*double(.00025F));
            c.forward_speed=c.speed=25; c.predicted_max_speed=50; c.bypass_mode=bypass;
            // Mirror only the fixture's clock quantization, not its formula.
            if (k>=70 && k<190) {
                const double elapsed=double(k-70)/120;
                const int ticks=int(std::round(elapsed/double(.00025F)));
                c.shift_elapsed=float(double(ticks)*double(.00025F));
            }
            SteeringResult r; check(steering_step(c,s,r),"sequence context accepted");
            words.push_back(static_cast<std::uint8_t>(r.angle)); words.push_back(static_cast<std::uint8_t>(r.angle>>8));
        }
        check(sha256_hex(sha256(words.data(),words.size()))==(bypass ?
            "A6627E9B2F71477272715C5D7D6400E3B94AE6507D6CE49F98A6D8D0929461B3" :
            "0A06D892AEE1A9748A8EFB71C5B324CF332E4F9ABF9CFC0828BD0C5AEEC0B1A9"),"native 240-step word sequence");
    }
    SteeringState bad; bad.input.write=33; SteeringResult sentinel; sentinel.limit_deg=123;
    check(!steering_step({},bad,sentinel) && sentinel.limit_deg==123,"invalid ring refused without writes");
    bad={}; SteeringContext c; c.dt=0;
    check(!steering_step(c,bad,sentinel) && bad.input.count==0,"zero dt refused");
    c={}; c.raw=std::numeric_limits<float>::quiet_NaN();
    check(!steering_step(c,bad,sentinel),"nonfinite input refused");
    RaceSteering parsed=RaceSteering::baseline;
    check(!parse_race_steering("recovered",parsed) && parsed==RaceSteering::baseline,"unknown mode preserves config");
    for (auto mode:{RaceSteering::baseline,RaceSteering::recovered_predicate0,RaceSteering::recovered_predicate1})
        check(parse_race_steering(race_steering_name(mode),parsed) && parsed==mode,"mode round trip");
    RaceRun run; std::string error;
    check(!run.set_steering(static_cast<RaceSteering>(99),error),"invalid run mode refused");

    // Raw binding remains the sole input for recovered mode, including button
    // releases and shift edges. Baseline's filter remains independently active.
    DriveControls raw, baseline;
    auto pad=race_input_frame({1,0,0},0); raw.update(pad); baseline.update(pad);
    const auto left=gamepad_button_bit(GamepadButton::dpad_left);
    pad=race_input_frame({1,left,0},1); raw.update(pad); baseline.update(pad);
    check(raw.step(25,2,VehicleGearbox::automatic,true).steer==1 &&
        baseline.step(25,2,VehicleGearbox::automatic).steer<.1,"recovered bypasses digital filter once");
    pad=race_input_frame({1,0,0},2); raw.update(pad);
    check(raw.step(25,2,VehicleGearbox::automatic,true).steer==0,"raw release reaches road-wheel path");
    VehicleModel model(stock_240sx_vehicle_spec()); VehicleState car;
    VehiclePose pose; pose.speed=25; pose.gearbox=VehicleGearbox::manual;
    check(model.reset(car,pose),"road override fixture reset");
    car.gearbox=VehicleGearbox::manual;
    VehicleInput in; in.road_steering=true; in.road_angle=.15; in.road_target=.2;
    in.steer=.7; in.shift=VehicleShift::up; VehicleTelemetry t;
    check(model.step(car,in,VehicleFlatGround(),t) && car.transmission.requested_gear==3 &&
        car.steer_angle==.15 && t.requested_steer_angle==.2,"override removes approximate lock/slew while native clutch accepts shift request");
    in.shift=VehicleShift::none;
    unsigned shift_ticks=0;
    while(car.gear!=3 && shift_ticks++<120)
        check(model.step(car,in,VehicleFlatGround(),t),"accepted manual shift completes through native phases");
    check(car.gear==3 && car.steer_angle==.15 && t.requested_steer_angle==.2,
          "completed native shift preserves explicit road steering override");

    VehicleTrackCourse course;
    check(build_vehicle_proving_ground(course,error),"steering replay course");
    // A held pad sample must not be slewed twice with half the world dt:
    // the native one-degree snap otherwise suppresses gradual high-speed
    // turn-in. Original460930 slow-ramp evidence is recorded in the checkpoint.
    for(int sign : {-1,1}) {
        auto fast_course=course; fast_course.start.speed=58;
        RaceSession fast(fast_course,stock_240sx_vehicle_spec(),RaceSteering::recovered_predicate1);
        check(fast.start(error),"high-speed steering session starts");
        unsigned tick=0;
        while(race_car_held(fast.timing())&&tick<1000)
            check(fast.advance(race_input_frame({1,0,0},tick++),error),"high-speed countdown");
        for(int frame=0;frame<=44;++frame) {
            const double raw=std::min(.8,std::max(0.,(frame-30)*.025));
            const int axis=raw==0?0:sign*int(3000+27000*raw);
            check(fast.advance(race_input_frame({1,0,axis},tick++),error),"gradual high-speed turn advances");
        }
        const auto& t=fast.telemetry();
        check(std::abs(t.model.steer_angle)>.017&&sign*t.model.steer_angle<0,
              "gradual high-speed input reaches road wheels before full lock");
        check(sign*t.model.angular_velocity.z<-.0001,
              "gradual high-speed input turns the body in the requested direction");
        check(t.contacts==0&&t.model.wheels[0].contact&&t.model.wheels[1].contact,
              "turn-in response is measured before impact with front contact");
    }
    for (auto mode:{RaceSteering::recovered_predicate0,RaceSteering::recovered_predicate1}) {
        RaceSession a(course,stock_240sx_vehicle_spec(),mode), b(course,stock_240sx_vehicle_spec(),mode);
        check(a.start(error) && b.start(error),"recovered sessions start");
        bool saw_steer=false, saw_shift=false; int last_gear=2;
        for (int k=0;k<1200;++k) {
            const auto gas=gamepad_button_bit(GamepadButton::trigger_right);
            const auto buttons=k==900 ? gamepad_button_bit(GamepadButton::select) :
                gas | (k>=390 && k<410 ? gamepad_button_bit(GamepadButton::face_south) : 0);
            const int axis=k<360 || k>=650 ? 0 : k<500 ? -9000 : 9000;
            auto frame=race_input_frame({1,buttons,axis},k);
            if (!a.advance(frame,error) || !b.advance(frame,error)) { check(false,"recovered replay advances"); break; }
            check(a.record()==b.record(),"recovered full history replay deterministic");
            const auto& telemetry=a.telemetry().model;
            check(std::isfinite(telemetry.angular_velocity.z) && std::isfinite(telemetry.lateral_speed) &&
                std::isfinite(telemetry.forward_speed),"yaw slip and speed finite");
            saw_steer=saw_steer || std::abs(telemetry.steer_angle)>.01;
            saw_shift=saw_shift || a.car().gear!=last_gear; last_gear=a.car().gear;
            if (k==900) check(a.telemetry().reset==VehicleTrackReset::manual &&
                a.steering_state().input.count==0 && a.steering_state().rate.count==0 &&
                a.steering_state().angle==0,"reset clears both histories and angle");
        }
        check(saw_steer && saw_shift,"integrated corner and automatic gear transition exercised");
    }
    // Exercise the production recovered1 path through actual pad binding,
    // both fixed substeps, wheels and engine. A cosmetic HUD lag cannot
    // satisfy this engine/wheel coupling regression.
    for (const auto button:{GamepadButton::trigger_left,GamepadButton::face_south}) {
        RaceSession session(course,stock_240sx_vehicle_spec(),RaceSteering::recovered_predicate1);
        check(session.start(error),"production braking session starts");
        double brake_start=0, min_up=1; unsigned moving_samples=0, idle_samples=0;
        unsigned pinned_samples=0, release_samples=0;
        unsigned service_samples=0,parking_samples=0;
        for (int k=0;k<644;++k) {
            // Connect with neutral input; a held trigger on hot-plug is latched.
            const bool releasing=button==GamepadButton::trigger_left && k>=540;
            const auto buttons=k<164 ? 0 : gamepad_button_bit(k<464 || releasing ? GamepadButton::trigger_right : button);
            const auto pad=race_input_frame({1,buttons,0},k);
            const auto service_before=candidate_oracles::service_calls;
            const auto parking_before=candidate_oracles::parking_calls;
            if (!session.advance(pad,error)) { check(false,"production braking advances"); break; }
            if(k>=464&&!releasing) {
                service_samples+=candidate_oracles::service_calls-service_before;
                parking_samples+=candidate_oracles::parking_calls-parking_before;
            }
            const auto& t=session.telemetry().model;
            const auto q=session.car().orientation;
            min_up=std::min(min_up,1-2*(q.x*q.x+q.y*q.y));
            if(k==463) brake_start=t.forward_speed;
            if(k>=464 && t.forward_speed>8) {
                ++moving_samples;
                const bool pinned=session.car().wheels[2].brake_locked && session.car().wheels[3].brake_locked;
                if(pinned) {
                    ++pinned_samples;
                    // Selector1 wheel locking does not impose selector3's
                    // separate completed-RPM idle write. Native coupled wheel,
                    // engine and publication operations are covered separately.
                } else if(t.engine_rpm<=stock_240sx_handling().idle_rpm) ++idle_samples;
                if(releasing && k>540 && t.engine_rpm>stock_240sx_handling().idle_rpm) ++release_samples;
            }
        }
        check(brake_start>8 && moving_samples>10,"braking exercises a moving car");
        check(idle_samples==0,"moving recovered1 RPM stays above idle unless driven wheels are pinned");
        if(button==GamepadButton::trigger_left)
            check(service_samples>0 && parking_samples==0 && release_samples>0,
                  "production service brake uses original service path and restores moving RPM on release");
        else check(parking_samples>0 && service_samples==0,
                   "production handbrake uses original rear parking path without service input");
        check(candidate_oracles::failures==0,"original brake/gear/strut callback operands remain valid");
        check(min_up>.95,"straight recovered1 braking stays upright");
        std::cout << "production brake " << gamepad_button_bit(button)
                  << " entry-speed " << brake_start << " moving-samples " << moving_samples
                  << " idle-samples " << idle_samples << " locked-samples " << pinned_samples << " min-up " << min_up << '\n';
    }
    std::cout << "steering tests: " << failures << " failures\n";
    return failures ? 1 : 0;
}
