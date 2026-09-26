#pragma once

#include "nfsu/drive_session.h"
#include "nfsu/race_timing.h"
#include "nfsu/vehicle_track.h"

namespace nfsu {

// Segment adapter for the chase camera's collision hook. The hook itself
// is an approximation documented by #59; contacts use #60's track world.
class RaceCameraWorld final : public ChaseCameraWorld {
public:
    explicit RaceCameraWorld(const TrackContactWorld& world) : world_(world) {}
    double clear_fraction(const VehicleVec3& from, const VehicleVec3& to) const override;
private:
    const TrackContactWorld& world_;
};

// Event-1 integration: one pad tick at 60 Hz, two collision steps at 120 Hz,
// route events, race timing, then chase camera. No wall clock or file access.
// The course must outlive the session. Failure is terminal for this session.
class RaceSession {
public:
    explicit RaceSession(const VehicleTrackCourse& course);
    bool start(std::string& error);
    bool advance(const GamepadFrame& pad, std::string& error);
    const VehicleState& car() const { return vehicle_.state(); }
    const RaceProgress& progress() const { return vehicle_.progress(); }
    const RaceTimingState& timing() const { return timing_; }
    const RaceTimingConfig& timing_config() const { return timing_config_; }
    const ChaseCameraView& camera() const { return view_; }
    const VehicleSpec& spec() const { return spec_; }
    const VehicleTrackTelemetry& telemetry() const { return track_telemetry_[1]; }
    const std::vector<std::uint8_t>& record() const { return record_; }
    const VehicleTrackCourse& course() const { return course_; }
private:
    void encode();
    const VehicleTrackCourse& course_;
    VehicleSpec spec_;
    VehicleModel model_;
    VehicleTrackSession vehicle_;
    DriveControls controls_;
    ChaseCamera chase_;
    RaceCameraWorld camera_world_;
    ChaseCameraView view_;
    RaceTimingConfig timing_config_;
    RaceTimingState timing_;
    std::vector<RaceTimingEvent> timing_events_;
    VehicleInput inputs_[2]{};
    VehicleTrackTelemetry track_telemetry_[2]{};
    std::vector<std::uint8_t> record_;
    bool started_ = false;
};

struct RaceInputRun {
    std::uint32_t ticks = 0;
    std::uint32_t buttons = 0;
    std::int32_t steer = 0;
};
constexpr std::size_t kRaceInputMaxTicks = 120000;
bool parse_race_input(const std::string& text, std::vector<RaceInputRun>& runs,
                      std::string& error);
std::string format_race_input(const std::vector<RaceInputRun>& runs);
GamepadFrame race_input_frame(const RaceInputRun& run, std::uint64_t tick);
void append_race_input(std::vector<RaceInputRun>& runs, const GamepadFrame& pad);

// A conservative pure-pursuit driver of the recovered progress spline.
// It changes controller values only: 10 m/s cruise, 10 Hz decisions and
// quantized steering. It never places, advances or resets the car itself.
class RaceRouteDriver {
public:
    GamepadFrame sample(const RaceSession& session);
private:
    std::uint64_t ticks_ = 0;
    bool accelerating_ = true;
    GamepadFrame held_;
};

std::vector<std::string> race_session_hud(const RaceSession& session);

// Opaque passes rendered with the same camera, track first and car second.
// A depth tie belongs to the car. MaterialStateMode::off is required for
// both passes; alpha blending across separate depth buffers is not modeled.
bool compose_race_frame(const Frame& track, const Frame& car,
                        std::vector<unsigned char>& rgba);

} // namespace nfsu
