// Issue251 port telemetry, using the production recovered1 steering context.
// Synthetic flat ground/input sequences; not an original live-game replay.
#include "nfsu/vehicle.h"
#include "nfsu/steering.h"
#include "nfsu/chase_camera.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>

int main() {
    using namespace nfsu;
    const auto spec = stock_240sx_vehicle_spec();
    const auto& h = spec.handling;
    const int top = kVehicleGearFirst + int(h.forward_gears) - 1;
    const double final = h.final_drive_rear*h.rear_drive_fraction +
                         h.final_drive_front*(1-h.rear_drive_fraction);
    const float predictor = float(h.redline_rpm*(6.28318530717958647692/60)*
                                  h.wheels[2].radius/(final*h.gear_ratios[top]));
    std::cout << std::setprecision(17)
        << "case,step,phase,speed,yaw,lateral,rpm,wheel_rpm,gear,brake,spin0,spin1,spin2,spin3,force_long2,force_lat2,sliding2\n";
    for (const std::string name : {"accelerate-coast", "service-brake", "brake-release",
            "handbrake", "corner", "traction-recovery", "shift", "reverse", "stopped-rpm"}) {
        VehicleModel model(spec); VehicleState car; VehiclePose pose;
        pose.speed = name == "accelerate-coast" || name == "stopped-rpm" ? 0 : 25;
        if (name == "reverse") { pose.speed = -12; pose.gearbox = VehicleGearbox::manual; }
        if (name == "shift") pose.gearbox = VehicleGearbox::manual;
        if (!model.reset(car, pose)) return 1;
        if (name == "reverse") car.gear = car.transmission.requested_gear = kVehicleGearReverse;
        SteeringState steering; unsigned shift_step = 0; bool shifted = false;
        for (unsigned k = 0; k < 1440; ++k) {
            VehicleInput input; std::string phase;
            if (name == "accelerate-coast") {
                input.throttle = k < 960 ? 1 : 0; phase = k < 960 ? "gas" : "coast";
            } else if (name == "service-brake") {
                input.brake = 1; phase = "brake";
            } else if (name == "brake-release") {
                input.brake = k < 144 ? 1 : k < 180 ? .5 : 0;
                input.throttle = k < 180 ? 0 : 1;
                phase = k < 144 ? "brake" : k < 180 ? "half" : "release-gas";
            } else if (name == "handbrake") {
                input.handbrake = k < 360; input.throttle = k < 360 ? 0 : 1;
                phase = k < 360 ? "handbrake" : "release-gas";
            } else if (name == "corner" || name == "traction-recovery") {
                input.steer = k < 480 ? .3 : k < 960 ? -.3 : 0;
                input.throttle = name == "corner" ? .5 : k < 960 ? 1 : 0;
                phase = k < 480 ? "entry-hold" : k < 960 ? "reversal" : "release";
            } else if (name == "shift") {
                input.throttle = 1; input.shift = k == 240 ? VehicleShift::up :
                    k == 480 ? VehicleShift::down : VehicleShift::none;
                phase = k < 240 ? "first" : k < 480 ? "up" : "down";
            } else if (name == "reverse") {
                input.brake = k < 240 ? 1 : 0; input.throttle = k < 240 ? 0 : .5;
                phase = k < 240 ? "reverse-brake" : "reverse-gas";
            } else {
                input.brake = k < 480 ? 1 : 0; input.throttle = k < 480 ? 1 : 0;
                phase = k < 480 ? "gas-brake" : "release";
            }
            const auto q = car.orientation;
            const double fx = 1-2*(q.y*q.y+q.z*q.z), fy = 2*(q.x*q.y+q.w*q.z);
            SteeringContext c; c.raw = float(input.steer); c.predicate = true;
            c.seconds = float(1+double(k)/120);
            c.forward_speed = float(car.velocity.x*fx+car.velocity.y*fy+
                car.velocity.z*2*(q.x*q.z-q.w*q.y));
            c.speed = float(vehicle_sqrt(car.velocity.x*car.velocity.x+
                car.velocity.y*car.velocity.y+car.velocity.z*car.velocity.z));
            const auto delta = std::uint16_t(chase_angle(fx,fy)-chase_angle(car.velocity.x,car.velocity.y));
            c.heading_minus_velocity_deg = float(double(std::int16_t(delta))*360/65536);
            c.throttle = float(input.throttle); c.brake = float(input.brake);
            c.handbrake = input.handbrake ? 1.F : 0.F; c.predicted_max_speed = predictor;
            c.shift_elapsed = shifted ? float(double(k-shift_step)/120) : 1;
            SteeringResult road;
            if (!steering_step(c, steering, road)) return 1;
            input.road_steering = true; input.road_angle = road.angle_deg*3.14159265358979323846/180;
            input.road_target = road.target_deg*3.14159265358979323846/180;
            VehicleTelemetry t; const int previous = car.gear;
            if (!model.step(car, input, VehicleFlatGround(), t)) return 1;
            if (car.gear != previous) { shifted = true; shift_step = k; }
            std::cout << name << ',' << t.step << ',' << phase << ',' << t.forward_speed << ','
                << t.angular_velocity.z << ',' << t.lateral_speed << ',' << t.engine_rpm << ','
                << t.wheel_rpm << ',' << t.gear << ',' << t.brake;
            for (const auto& w : t.wheels) std::cout << ',' << w.spin;
            std::cout << ',' << t.wheels[2].force_long << ',' << t.wheels[2].force_lat << ','
                      << t.wheels[2].sliding << '\n';
        }
    }
    return 0;
}
