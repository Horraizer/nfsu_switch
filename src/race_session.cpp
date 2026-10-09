#include "nfsu/race_session.h"

#include <algorithm>
#include <cstring>
#include <locale>
#include <sstream>

namespace nfsu {
namespace {
void u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(value >> (8*k)));
}
void f32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits; std::memcpy(&bits, &value, sizeof bits); u32(out, bits);
}
double speed(const VehicleState& state) {
    return vehicle_sqrt(state.velocity.x*state.velocity.x + state.velocity.y*state.velocity.y);
}
double forward_speed(const VehicleState& state) {
    const auto& q = state.orientation;
    return state.velocity.x*(1-2*(q.y*q.y+q.z*q.z)) +
           state.velocity.y*2*(q.x*q.y+q.w*q.z) +
           state.velocity.z*2*(q.x*q.z-q.w*q.y);
}
}

RaceSession::RaceSession(const VehicleTrackCourse& course)
    : RaceSession(course, stock_240sx_vehicle_spec()) {}

RaceSession::RaceSession(const VehicleTrackCourse& course, const VehicleSpec& spec, RaceSteering steering)
    : course_(course), spec_(spec), model_(spec_),
      vehicle_(model_, course_, vehicle_track_first_lap_approximations()),
      chase_(stock_240sx_chase_camera_table(), {}, {}), camera_world_(course.world), steering_mode_(steering) {
    // Explicit integration approximation: kinematic top-gear redline speed,
    // not the unrecovered cached retail acceleration predictor (0x580240).
    const auto& h = spec_.handling;
    const int top = kVehicleGearFirst+int(h.forward_gears)-1;
    if (top >= kVehicleGearFirst && top < int(kVehicleGearSlots)) {
        const double final = h.final_drive_rear*h.rear_drive_fraction+
            h.final_drive_front*(1-h.rear_drive_fraction);
        const double ratio = final*h.gear_ratios[std::size_t(top)];
        if (ratio>0) steering_max_speed_ = static_cast<float>(h.redline_rpm*
            (6.28318530717958647692/60)*h.wheels[2].radius/ratio);
    }
}

bool RaceSession::steer_input(VehicleInput& input, int substep, std::string& error) {
    const auto& s = car(); const auto& q = s.orientation;
    const double fx = 1-2*(q.y*q.y+q.z*q.z), fy = 2*(q.x*q.y+q.w*q.z);
    const std::uint16_t difference = static_cast<std::uint16_t>(
        chase_angle(fx,fy)-chase_angle(s.velocity.x,s.velocity.y));
    SteeringContext context;
    context.raw = float(input.steer);
    context.seconds = float(1+double(steering_steps_)/60);
    context.dt = static_cast<float>(kDriveFrameSeconds);
    context.forward_speed = float(forward_speed(s));
    context.speed = float(vehicle_sqrt(s.velocity.x*s.velocity.x+s.velocity.y*s.velocity.y+s.velocity.z*s.velocity.z));
    context.heading_minus_velocity_deg = float(double(static_cast<std::int16_t>(difference))*(360.0/65536));
    // Traced consumers are processed controls, not separate digital/analog
    // sources. Use the applied port values, including reverse assist. The
    // native speed/gear brake normalizer itself remains unrecovered here.
    context.throttle = float(input.throttle);
    context.handbrake = input.handbrake ? 1 : 0;
    context.brake = float(input.brake);
    context.predicted_max_speed = steering_max_speed_;
    context.shift_elapsed = shifted_ ? float(double(steering_steps_-shift_step_)/60) : 1;
    context.predicate = steering_mode_ == RaceSteering::recovered_predicate1;
    if (!steering_step(context,steering_state_,steering_result_)) {
        error = "recovered steering context refused at substep "+std::to_string(substep); return false;
    }
    input.road_steering = true;
    input.road_angle_word_valid = true;
    input.road_angle_word = steering_result_.angle;
    input.road_angle = double(steering_result_.angle_deg)*(3.14159265358979323846/180);
    input.road_target = double(steering_result_.target_deg)*(3.14159265358979323846/180);
    input.steer_history = steering_result_.filtered;
    input.steer_limit_deg=steering_result_.limit_deg; input.steer_rate_deg_s=steering_result_.rate_deg_s;
    input.steer_shift_factor=steering_result_.shift_factor; input.steering_mode=int(steering_mode_);
    return true;
}

double RaceCameraWorld::clear_fraction(const VehicleVec3& from, const VehicleVec3& to) const {
    const float a[3] = {float(from.x),float(from.y),float(from.z)};
    const float b[3] = {float(to.x),float(to.y),float(to.z)};
    const double x=double(b[0])-a[0], y=double(b[1])-a[1], z=double(b[2])-a[2];
    const double length=vehicle_sqrt(x*x+y*y+z*z);
    if (length==0) return 1;
    TrackContact hit;
    const auto status=query_track_contact_segment(world_,a,b,hit);
    if (status==TrackContactStatus::refused) return 0;
    return status==TrackContactStatus::hit ? std::clamp(double(hit.distance)/length,0.0,1.0) : 1.0;
}

bool RaceSession::start(std::string& error) {
    if (started_) { error = "race session is one-shot"; return false; }
    if (steering_mode_ != RaceSteering::baseline && steering_mode_ != RaceSteering::recovered_predicate0 &&
        steering_mode_ != RaceSteering::recovered_predicate1) { error="invalid steering mode"; return false; }
    if (!course_.has_route || !vehicle_.valid() || !vehicle_.start(course_.start)) {
        error = "race course/vehicle: " + vehicle_.error(); return false;
    }
    if (!make_race_timing_config(60, 1, timing_config_, error) ||
        !start_race_timing(course_.route, timing_config_, timing_, error)) return false;
    set_race_stage(timing_, RaceStage::countdown, timing_events_);
    chase_.set_pov(0);
    if (!chase_.reset(chase_subject(car(), spec_), camera_world_, view_)) {
        error = chase_.error(); return false;
    }
    started_ = true;
    return true;
}

bool RaceSession::advance(const GamepadFrame& pad, std::string& error) {
    if (!started_ || timing_.outcome != RaceOutcome::running) {
        error = "race session is not running"; return false;
    }
    controls_.update(pad);
    const DriveFrameState& frame = controls_.frame_state();
    animation_.braking =
        (frame.brake_from_button ? frame.brake_digital : frame.brake_analog) > 0.0 ||
        frame.handbrake;
    RaceTimingInput timing_input;
    if (!race_car_held(timing_)) {
        if (controls_.frame_state().reset) vehicle_.request_reset();
        const bool recovered = steering_mode_ != RaceSteering::baseline;
        // Keep the legacy processed-controls adapter at two120Hz updates.
        // Human4615E0 calls steering460930 once with the world dt before
        // physics. Calling it twice with half dt incorrectly snaps each
        // small slew below one degree back to zero during gradual turn-in.
        auto first=controls_.step(forward_speed(car()),car().gear,car().gearbox,recovered);
        auto held=controls_.step(forward_speed(car()),car().gear,car().gearbox,recovered);
        if(recovered) {
            if(!steer_input(held,1,error)) {started_=false;return false;}
            ++steering_steps_;
        }
        if(first.shift!=VehicleShift::none) held.shift=first.shift;
        for (std::size_t k = 0; k < kDriveStepsPerFrame; ++k) {
            inputs_[k]=held;
            if(k) inputs_[k].shift=VehicleShift::none;
            const int previous_gear = car().gear;
            if (!vehicle_.step(inputs_[k], track_telemetry_[k],
                k+1==kDriveStepsPerFrame?kDriveFrameSeconds:0.0,
                kDriveSolverSeconds,k==0,kDriveStepsPerFrame)) {
                started_ = false; error = vehicle_.error(); return false;
            }
            if (recovered) {
                if (track_telemetry_[k].reset != VehicleTrackReset::none) {
                    steering_state_={}; steering_result_={}; shifted_=false;
                } else if (previous_gear != car().gear) {
                    shift_step_ = steering_steps_; shifted_=true;
                }
            }
            for (std::size_t w = 0; w < kVehicleWheelCount; ++w) {
                double phase = animation_.wheel_phase[w] +
                    car().wheels[w].spin * kDriveSolverSeconds;
                while (phase > 3.14159265358979323846)
                    phase -= 6.28318530717958647692;
                while (phase < -3.14159265358979323846)
                    phase += 6.28318530717958647692;
                animation_.wheel_phase[w] = phase;
            }
        }
        timing_input.events = vehicle_.events().data();
        timing_input.event_count = vehicle_.events().size();
        timing_input.speed = static_cast<float>(speed(car()));
    } else {
        // Hold the grid pose, including gravity, until the countdown says GO.
        // Still sample controls, so hot-plug latches and button releases work.
        for (std::size_t k = 0; k < kDriveStepsPerFrame; ++k) {
            inputs_[k] = {};
            track_telemetry_[k].position = car().position;
            track_telemetry_[k].orientation = car().orientation;
        }
    }
    timing_events_.clear();
    if (!step_race_timing(course_.route, timing_config_, timing_, &timing_input,
                          timing_events_, error)) { started_ = false; return false; }
    if (controls_.frame_state().camera) chase_.next_pov();
    if (!chase_.update(chase_subject(car(), spec_), kDriveFrameSeconds, camera_world_, view_)) {
        started_ = false; error = chase_.error(); return false;
    }
    encode();
    return true;
}

void RaceSession::encode() {
    VehicleTelemetry telemetry[kDriveStepsPerFrame];
    for(std::size_t k=0;k<kDriveStepsPerFrame;++k) telemetry[k]=track_telemetry_[k].model;
    encode_drive_frame(timing_.step, inputs_, telemetry, view_, controls_.frame_state(), record_);
    for (const auto& t : track_telemetry_) {
        std::array<std::uint8_t, kVehicleTrackTelemetryRecordBytes> bytes{};
        encode_vehicle_track_telemetry(t, bytes);
        record_.insert(record_.end(), bytes.begin(), bytes.end());
    }
    const auto& p = progress();
    for (auto v : {p.lap,p.max_lap,p.checkpoint,p.max_checkpoint,p.next_boundary,
                   int(p.finished),int(p.wrong_way),p.zone,int(p.off_path),p.coordinate.node})
        u32(record_, static_cast<std::uint32_t>(v));
    for (float v : {p.coordinate.parameter,p.coordinate.fraction,p.coordinate.distance,
                    p.coordinate.tangent[0],p.coordinate.tangent[1],p.previous_percent}) f32(record_,v);
    const auto& t = timing_;
    for (auto v : {int(t.stage),int(t.outcome),t.now_ticks,t.step_ticks,
                   int(t.countdown_valid),t.countdown_start_ticks,int(t.go_valid),t.go_ticks,
                   int(t.race_start_valid),t.race_start_ticks,t.race_ticks,t.fastest_lap_ticks,
                   t.countdown_shown}) u32(record_,static_cast<std::uint32_t>(v));
    const auto& r = t.racers[0];
    for (auto v : {int(r.status),r.lap,r.highest_lap,r.checkpoint,r.highest_checkpoint,
                   int(r.new_lap),int(r.new_checkpoint),r.position,int(r.finish_valid),
                   r.finish_ticks,int(r.lap_shown_valid),r.lap_shown_since,r.lap_shown_ticks,
                   int(r.wrong_way),r.wrong_way_since}) u32(record_,static_cast<std::uint32_t>(v));
    for (std::size_t k = 0; k < kRaceBoundaryLimit; ++k) {
        for (auto v : {int(r.crossing_valid[k]),r.crossing_ticks[k],r.split_ticks[k],
                       r.previous_split_ticks[k],r.best_split_ticks[k],r.previous_best_split_ticks[k]})
            u32(record_,static_cast<std::uint32_t>(v));
    }
    for (int k = 0; k < kRaceLapSlots; ++k) {
        u32(record_,static_cast<std::uint32_t>(r.lap_ticks[k]));
        u32(record_,static_cast<std::uint32_t>(r.lap_total_ticks[k]));
    }
    if (steering_mode_ != RaceSteering::baseline) {
        u32(record_,0x31525453); // STR1, recovered-only state extension
        u32(record_,static_cast<std::uint32_t>(steering_mode_));
        const auto history = [this](const auto& h) {
            for (float v : h.values) f32(record_,v);
            for (float v : h.times) f32(record_,v);
            f32(record_,h.sum); f32(record_,h.average);
            for (auto v : {h.count,h.write,h.oldest}) u32(record_,static_cast<std::uint32_t>(v));
        };
        history(steering_state_.input); history(steering_state_.rate);
        u32(record_,steering_state_.angle); f32(record_,steering_state_.previous_limit);
        f32(record_,steering_state_.previous_raw); u32(record_,steering_state_.assist);
        for (auto v : {steering_steps_,shift_step_}) {
            u32(record_,static_cast<std::uint32_t>(v)); u32(record_,static_cast<std::uint32_t>(v>>32));
        }
        u32(record_,shifted_); f32(record_,steering_max_speed_);
    }
}

bool parse_race_input(const std::string& text, std::vector<RaceInputRun>& runs, std::string& error) {
    runs.clear();
    if (text.size() > 4*1024*1024) { error = "race input exceeds 4 MiB"; return false; }
    for (unsigned char c : text) if (c != '\n' && c != '\r' && c != '\t' && (c < 32 || c > 126)) {
        error = "race input must be ASCII"; return false;
    }
    std::istringstream stream(text); stream.imbue(std::locale::classic());
    std::string line; bool header = false; std::size_t total = 0;
    while (std::getline(stream,line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream row(line); row.imbue(std::locale::classic());
        row >> std::ws; if (row.eof()) continue;
        if (!header) {
            std::string magic, extra; int version = 0;
            if (!(row >> magic >> version) || magic != "NFSU_RACE_INPUT" || version != 1 || row >> extra) {
                error = "expected NFSU_RACE_INPUT 1"; return false;
            }
            header = true; continue;
        }
        long long ticks, buttons, steer; std::string extra;
        if (!(row >> ticks >> buttons >> steer) || row >> extra || ticks < 1 ||
            ticks > static_cast<long long>(kRaceInputMaxTicks-total) || buttons < 0 ||
            buttons > kGamepadButtonMask || steer < kAxisMin || steer > kAxisMax) {
            error = "invalid race input run or more than 120000 ticks"; runs.clear(); return false;
        }
        total += static_cast<std::size_t>(ticks);
        runs.push_back({static_cast<std::uint32_t>(ticks),static_cast<std::uint32_t>(buttons),
                        static_cast<std::int32_t>(steer)});
    }
    if (!header || runs.empty()) { error = "race input has no runs"; return false; }
    return true;
}

std::string format_race_input(const std::vector<RaceInputRun>& runs) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << "NFSU_RACE_INPUT 1\n";
    for (const auto& r : runs) out << r.ticks << ' ' << r.buttons << ' ' << r.steer << '\n';
    return out.str();
}
GamepadFrame race_input_frame(const RaceInputRun& run, std::uint64_t tick) {
    GamepadFrame pad; pad.time_us = tick*1000000/60; pad.pads[0].connected = true;
    pad.pads[0].buttons = run.buttons; pad.pads[0].axes[0] = run.steer; return pad;
}
void append_race_input(std::vector<RaceInputRun>& runs, const GamepadFrame& pad) {
    const auto& p = pad.pads[0];
    if (!runs.empty() && runs.back().buttons == p.buttons && runs.back().steer == p.axes[0])
        ++runs.back().ticks;
    else runs.push_back({1,p.buttons,p.axes[0]});
}

GamepadFrame RaceRouteDriver::sample(const RaceSession& session) {
    if (race_car_held(session.timing())) return race_input_frame({1,0,0},session.timing().step);
    if (ticks_++ % 6 == 0) {
        const auto& state = session.car(); const auto& coord = session.progress().coordinate;
        const auto& spline = session.course().route.spline;
        float derivative[3], target[3]; evaluate_track_path(spline,coord.parameter,1,derivative);
        const double norm = vehicle_sqrt(double(derivative[0])*derivative[0]+double(derivative[1])*derivative[1]);
        // Fixed look-ahead does not collapse when braking or accelerating.
        const double v = speed(state);
        const float parameter = coord.parameter + static_cast<float>(12/std::max(norm,1.0));
        evaluate_track_path(spline,parameter,0,target);
        if (line_ == RaceDrivingLine::event1_forward) {
            // Authored lane offsets from the track-1003 collision map. The
            // progress spline crosses medians and even runs outside barriers;
            // it measures progress, not a drivable centre line. Positive is left.
            // Linear ramps, including the 30->0 seam; metres, not pose changes.
            constexpr double lanes[][2] = {
                {0,0},{0.7,0},{1.15,-12},{1.5,-10},{2,-5},{2.5,-10},{4,-10},{5,0},
                {7.5,0},{8.2,6},{8.5,6},{8.8,0},{9.5,0},{12.1,0},{12.3,-10},
                {12.5,-15},{12.9,-10},{13.5,0},{21.5,0},{22,12},{23.5,12},
                {24.5,4},{25.2,4},{25.8,12},{28.9,12},{29.3,0},{30,0}};
            double p = parameter;
            while (p >= 30) p -= 30;
            double offset = 0;
            for (std::size_t i = 1; i < sizeof(lanes)/sizeof(*lanes); ++i) {
                if (p > lanes[i][0]) continue;
                const double f = (p-lanes[i-1][0])/(lanes[i][0]-lanes[i-1][0]);
                offset = lanes[i-1][1] + f*(lanes[i][1]-lanes[i-1][1]);
                break;
            }
            float tangent[3]; evaluate_track_path(spline,parameter,1,tangent);
            const double length = vehicle_sqrt(double(tangent[0])*tangent[0]+double(tangent[1])*tangent[1]);
            target[0] -= static_cast<float>(offset*tangent[1]/std::max(length,1.0));
            target[1] += static_cast<float>(offset*tangent[0]/std::max(length,1.0));
        }
        float preview[3];
        if (line_ == RaceDrivingLine::event8_forward) {
            // Authored test-driver offsets from Atlantic's road corridors and poles.
            // Positive is left of the forward tangent. Controller input only.
            double p = parameter;
            while (p >= 33) p -= 33;
            constexpr double lanes[][2] = {
                {0,0},{3,0},{3.2,4},{3.4,14},{3.6,26},{3.8,38},{4,47},{4.2,46},
                {4.4,35},{4.6,22},{4.8,10},{5,0},{5.4,0},{5.8,8},{6,12},{6.2,15},
                {6.4,18},{6.6,17},{6.8,15},{7,12},{7.2,10},{7.4,8},{7.6,6},{8,3},{8.2,0},
                {20.8,0},{21,7},{21.2,13},{21.4,13},{21.6,10},{21.8,3},{22,-5},
                {23.2,-5},{23.4,0},{23.6,15},{23.8,38},{24,58},{24.2,64},{24.4,74},
                {24.6,74},{24.8,59},{25,44},{25.2,29},{25.6,26},{26,15},{26.3,-6},
                {26.6,-6},{26.8,0},{27,8},{27.4,8},{27.6,0},{28.4,0},{28.8,-10},
                {29,-6},{29.2,5},{29.4,10},{30.5,10},{30.8,4},{31,8},{31.5,10},{31.8,6},{32,3},{32.3,0},{33,0}};
            double offset = 0;
            for (std::size_t i=1;i<sizeof(lanes)/sizeof(*lanes);++i) {
                if(p>lanes[i][0])continue;
                const double fraction=(p-lanes[i-1][0])/(lanes[i][0]-lanes[i-1][0]);
                offset=lanes[i-1][1]+fraction*(lanes[i][1]-lanes[i-1][1]);break;
            }
            float tangent[3]; evaluate_track_path(spline,parameter,1,tangent);
            const double length = vehicle_sqrt(double(tangent[0])*tangent[0]+double(tangent[1])*tangent[1]);
            target[0] -= static_cast<float>(offset*tangent[1]/std::max(length,1.0));
            target[1] += static_cast<float>(offset*tangent[0]/std::max(length,1.0));
        }
        evaluate_track_path(spline,coord.parameter+static_cast<float>(30/std::max(norm,1.0)),1,preview);
        const double pn = vehicle_sqrt(double(preview[0])*preview[0]+double(preview[1])*preview[1]);
        const double ux = derivative[0]/std::max(norm,1.0)-preview[0]/std::max(pn,1.0);
        const double uy = derivative[1]/std::max(norm,1.0)-preview[1]/std::max(pn,1.0);
        const double turn = vehicle_sqrt(ux*ux+uy*uy);
        const double desired = std::max(6.0,10/(1+2*turn));
        const auto& q = state.orientation;
        const double fx = 1-2*(q.y*q.y+q.z*q.z), fy = 2*(q.x*q.y+q.w*q.z);
        const double dx = target[0]-state.position.x, dy = target[1]-state.position.y;
        const double distance = vehicle_sqrt(dx*dx+dy*dy);
        const double steer = std::clamp(-(fx*dy-fy*dx)/std::max(distance,1.0)*1.8,-1.0,1.0);
        int raw = steer == 0 ? 0 : static_cast<int>(steer*27000+(steer>0?3000:-3000));
        raw = (raw/256)*256;
        if (v < desired-0.5) accelerating_ = true;
        if (v > desired+0.5) accelerating_ = false;
        std::uint32_t buttons = accelerating_ ? gamepad_button_bit(GamepadButton::trigger_right) : 0;
        if (v > desired+0.7) buttons = gamepad_button_bit(GamepadButton::trigger_left);
        held_ = race_input_frame({1,buttons,raw},session.timing().step);
    }
    held_.time_us = session.timing().step*1000000/60;
    return held_;
}

std::vector<std::string> race_session_hud(const RaceSession& session, std::uint32_t event_number) {
    RaceHud hud;
    if (!race_timing_hud(session.course().route,session.timing_config(),session.timing(),0,hud))
        return {"RACE HUD UNAVAILABLE"};
    const auto& r = session.timing().racers[0];
    const int lap = std::min(hud.lap+1,hud.laps);
    const int current = hud.finished ? r.lap_ticks[r.highest_lap] :
        session.timing().step_ticks-r.crossing_ticks[0];
    const std::string last = r.highest_lap > 0 && r.highest_lap < kRaceLapSlots
        ? format_race_ticks(r.lap_ticks[r.highest_lap]) : "--:--.--";
    return {"EVENT " + std::to_string(event_number) + "  LAP " + std::to_string(lap) + " / " + std::to_string(hud.laps),
        "CURRENT " + (hud.lap_time_valid ? format_race_ticks(current) : "--:--.--") + "  LAST " + last,
        hud.finished ? "FINISHED " + format_race_ticks(r.finish_ticks) :
        hud.countdown==4 ? "GO" : hud.countdown ? "COUNTDOWN " + std::to_string(hud.countdown) : "RACING",
        "W/S GAS/BRAKE  A/D STEER  SPACE HANDBRAKE  R RESET  C CAMERA  ESC QUIT"};
}

bool compose_race_frame(const Frame& track, const Frame& car, Frame& composed) {
    if (!track.width() || track.width()!=car.width() || track.height()!=car.height()) return false;
    composed = track;
    for (std::size_t i=0;i<track.depth().size();++i) {
        if (car.ids()[i]==kNoTriangle || car.depth()[i]>track.depth()[i]) continue;
        std::copy_n(car.rgba_.data()+4*i,4,composed.rgba_.data()+4*i);
        composed.depth_[i]=car.depth_[i];
        composed.ids_[i]=car.ids_[i];
        composed.blended_[i]=car.blended_[i];
    }
    return true;
}

bool compose_race_frame(const Frame& track, const Frame& car, std::vector<unsigned char>& rgba) {
    Frame composed;
    if (!compose_race_frame(track, car, composed)) return false;
    rgba = composed.rgba();
    return true;
}
} // namespace nfsu
