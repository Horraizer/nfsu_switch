#include "nfsu/handling_telemetry.h"
#include <cmath>

namespace nfsu {
bool HandlingTelemetryLog::write(const RaceSession& session, const GamepadFrame& pad,
                                  std::uint64_t consumed_us, bool clock_available) {
    if (!output_ || rows_ >= kMaxRows || !session.physics_telemetry()[0].step) return true;
    if (drive_ticks_++ % 6) return true;
    if (!header_) {
        if (std::fputs("handling-v1 tick,frame,physics,substep,connected,raw_steer_axis,raw_buttons,sample_age_us,filtered_steer,requested_road_rad,actual_road_rad,yaw_world_rad_s,body_slip_rad,gear_slot,engine_rpm,camera_heading_rad,w0_load_N,w0_slip_ratio,w0_slip_lateral,w0_requested_Nm,w0_applied_Nm,w0_contact,w1_load_N,w1_slip_ratio,w1_slip_lateral,w1_requested_Nm,w1_applied_Nm,w1_contact,w2_load_N,w2_slip_ratio,w2_slip_lateral,w2_requested_Nm,w2_applied_Nm,w2_contact,w3_load_N,w3_slip_ratio,w3_slip_lateral,w3_requested_Nm,w3_applied_Nm,w3_contact\n",output_) < 0) return false;
        header_ = true;
    }
    for (std::size_t k = 0; k < 2 && rows_ < kMaxRows; ++k) {
        const auto& track = session.physics_telemetry()[k];
        const auto& t = track.model;
        std::fprintf(output_,"handling %llu,",static_cast<unsigned long long>(session.timing().step));
        if (pad.sample_clock_available) std::fprintf(output_,"%llu",static_cast<unsigned long long>(pad.display_frame));
        else std::fputs("unavailable",output_);
        std::fprintf(output_,",%llu,%zu,%u,",static_cast<unsigned long long>(track.step),k,
            unsigned(pad.pads[0].connected));
        if (pad.pads[0].connected) std::fprintf(output_,"%d,%u,",int(pad.pads[0].axes[0]),pad.pads[0].buttons);
        else std::fputs("unavailable,unavailable,",output_);
        if (clock_available && pad.sample_clock_available && consumed_us >= pad.sample_time_us)
            std::fprintf(output_,"%llu",static_cast<unsigned long long>(consumed_us-pad.sample_time_us));
        else std::fputs("unavailable",output_);
        std::fprintf(output_,",%.17g,%.17g,%.17g,%.17g,",t.steer,t.requested_steer_angle,
            t.steer_angle,t.angular_velocity.z);
        if (t.forward_speed*t.forward_speed+t.lateral_speed*t.lateral_speed > 1e-12)
            std::fprintf(output_,"%.17g",std::atan2(t.lateral_speed,t.forward_speed));
        else std::fputs("unavailable",output_);
        std::fprintf(output_,",%d,%.17g,%.17g",t.gear,t.engine_rpm,
            session.camera().heading*(6.28318530717958647692/65536.0));
        for (const auto& w : t.wheels) {
            std::fprintf(output_,",%.17g,",w.load);
            if (w.contact) std::fprintf(output_,"%.17g,%.17g",w.slip_ratio,w.slip_lateral);
            else std::fputs("unavailable,unavailable",output_);
            std::fprintf(output_,",%.17g,%.17g,%u",w.drive_torque,w.drive_applied,unsigned(w.contact));
        }
        if (std::fputc('\n',output_) == EOF || std::ferror(output_)) return false;
        ++rows_;
    }
    if (rows_ == kMaxRows && std::fputs("handling-limit reached: 2400 rows\n",output_) < 0) return false;
    return true;
}
} // namespace nfsu
