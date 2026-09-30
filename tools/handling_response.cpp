// Issue 248 measurement harness, not a new game mode or a replacement pin.
// Link against the existing desktop libraries; see docs/HANDLING_GAPS.md.
#include "nfsu/drive_controls.h"
#include "nfsu/vehicle.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main() try {
    using namespace nfsu;
    std::cout << std::setprecision(9)
              << "variant,target_mps,input,actual_mps,angle_rad,yaw_rad_s,radius_m,ay_g,t90_s,yaw_range_last_s,sliding_steps,drive_cut_steps\n";
    for (const std::string variant : {"baseline", "steer-ref-13", "tyres-12-18",
                                      "yaw-inertia-x2", "force-lift-0.8", "no-traction-limit"}) {
        for (double target : {10.0, 15.0, 25.0, 35.0}) {
            for (double steer : {0.3, 1.0}) {
                auto spec = stock_240sx_vehicle_spec();
                if (variant == "steer-ref-13") spec.approx.steer_speed_reference = 13;
                if (variant == "tyres-12-18") {
                    spec.approx.cornering_stiffness_front = 12;
                    spec.approx.cornering_stiffness_rear = 18;
                }
                if (variant == "yaw-inertia-x2") spec.handling.inertia[2] *= 2;
                if (variant == "force-lift-0.8") spec.approx.tyre_force_lift = 0.8;
                if (variant == "no-traction-limit") spec.approx.traction_limit = false;
                const VehicleModel model(spec);
                VehicleState state;
                VehiclePose pose; pose.speed = target;
                if (!model.valid() || !model.reset(state, pose)) throw std::runtime_error("reset failed");
                const VehicleFlatGround ground(0);
                std::vector<VehicleTelemetry> rows;
                double speed = target;
                unsigned sliding = 0, cuts = 0;
                // The same throttle controller as tests/vehicle_tests.cpp run_steady.
                // It holds speed approximately; velocity is never overwritten.
                for (unsigned k = 0; k < 2400; ++k) {
                    VehicleInput in;
                    in.steer = steer;
                    in.throttle = std::clamp(0.6 + 0.8*(target-speed), 0.0, 1.0);
                    VehicleTelemetry t;
                    if (!model.step(state, in, ground, t)) throw std::runtime_error("step failed");
                    speed = t.forward_speed;
                    bool slide = false, cut = false;
                    for (const auto& wheel : t.wheels) {
                        slide = slide || wheel.sliding;
                        cut = cut || std::abs(wheel.drive_applied-wheel.drive_torque) > 1e-6;
                    }
                    sliding += slide; cuts += cut; rows.push_back(t);
                }
                const auto& t = rows.back();
                double lo = t.angular_velocity.z, hi = lo;
                for (unsigned k = 2280; k < 2400; ++k) {
                    lo = std::min(lo, rows[k].angular_velocity.z);
                    hi = std::max(hi, rows[k].angular_velocity.z);
                }
                double t90 = -1;
                for (unsigned k = 0; k < rows.size(); ++k) {
                    if (rows[k].angular_velocity.z >= 0.9*t.angular_velocity.z) {
                        t90 = (k+1)*kVehicleStepSeconds; break;
                    }
                }
                const double v = std::hypot(t.velocity.x,t.velocity.y);
                std::cout << variant << ',' << target << ',' << steer << ',' << t.forward_speed
                          << ',' << t.steer_angle << ',' << t.angular_velocity.z << ','
                          << v/t.angular_velocity.z << ',' << v*t.angular_velocity.z/9.8 << ','
                          << t90 << ',' << hi-lo << ',' << sliding << ',' << cuts << '\n';
            }
        }
    }
    // Input-only timing at a held speed; excludes body response and road lock.
    for (bool digital : {false,true}) {
        DriveControls controls;
        GamepadFrame pad; pad.pads[0].connected = true;
        controls.update(pad); // Connect with buttons released.
        pad.time_us = 16666;
        if (digital) pad.pads[0].buttons = gamepad_button_bit(GamepadButton::dpad_right);
        else pad.pads[0].axes[0] = 30000;
        controls.update(pad);
        unsigned full = 0;
        for (unsigned k=1; k<=240; ++k) {
            auto in = controls.step(25, kVehicleGearFirst, VehicleGearbox::automatic);
            if (!full && in.steer <= -0.999999) full = k;
        }
        pad.pads[0].buttons = digital ? gamepad_button_bit(GamepadButton::dpad_left) : 0;
        pad.pads[0].axes[0] = digital ? 0 : -30000;
        controls.update(pad);
        unsigned opposite = 0;
        for (unsigned k=1; k<=240; ++k) {
            auto in = controls.step(25, kVehicleGearFirst, VehicleGearbox::automatic);
            if (!opposite && in.steer >= 0.999999) opposite = k;
        }
        if (!full || !opposite) throw std::runtime_error("input did not reach target");
        std::cout << "input " << (digital ? "digital" : "analog") << " at 25 m/s: full "
                  << full*kVehicleStepSeconds << " s, reversal " << opposite*kVehicleStepSeconds << " s\n";
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n'; return 1;
}
