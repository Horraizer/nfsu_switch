#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// ---------------------------------------------------------------------------
// A path-following controller, separate from any vehicle and from the race
// ---------------------------------------------------------------------------
//
// docs/RACING_LINE.md. This is the path-following half of issue #63, kept
// apart from vehicle actuation on purpose: it takes a path and a vehicle
// state and returns a steering, throttle and brake demand plus telemetry. It
// reads no clock, no file, no random device and nothing from the race
// session; every step is a pure function of the path, the tuning, the seed
// and the states it has been given since reset().
//
// It is this project's own controller, not a port of retail's. Retail's
// opponent is a goal and action machine (`AIGoal`, `AICarController`,
// `AIActionJustDrive`, `AIActionGetUnstuck`, `AIActionAccidentRecovery`,
// 0x006C9618..0x006C9660) driving the same route data; docs/RACING_LINE.md
// says what is recovered of it and what is not.
//
// Frames and units. Positions are the two horizontal slots of the retail
// track, in the same units route points use. `heading` is radians, 0 along
// +x, increasing toward +y. `speed` is forward speed in those units per
// second and may be negative. Steering is -1..+1, positive turning toward
// increasing heading (the same sense as a positive heading error).

struct PathFollowPoint {
    float position[2] = {0.0F, 0.0F};
    // The largest speed the follower will choose while this point is in the
    // window it looks at. Zero means "no limit from this point".
    float speed_limit = 0.0F;
    // A point the follower must pass within PathFollowTuning::capture_radius
    // of before it treats any later checkpoint as next. Skipping one puts
    // the follower into PathFollowMode::rejoining until it is captured.
    bool checkpoint = false;
};

struct PathFollowPath {
    std::vector<PathFollowPoint> points;
    // A closed path wraps from the last point back to the first.
    bool closed = false;
};

struct PathFollowVehicleState {
    float position[2] = {0.0F, 0.0F};
    float heading = 0.0F;
    float speed = 0.0F;
    // True on a step in which the vehicle is touching something. It does not
    // change the demand on its own; it makes the stall timer run faster, so
    // a vehicle held against geometry gives up sooner than one merely slow.
    bool contact = false;
};

struct PathFollowTuning {
    // Lookahead along the path: max(lookahead_min, lookahead_seconds *
    // max(speed, 0)).
    float lookahead_min = 8.0F;
    float lookahead_seconds = 0.6F;
    // Steering. The demand is the heading error divided by steer_gain,
    // clamped to +-max_steer.
    float steer_gain = 1.6F;
    float max_steer = 1.0F;
    // Longitudinal. The demand is the speed error divided by speed_band,
    // clamped to 0..1 on each of throttle and brake.
    float cruise_speed = 40.0F;
    float speed_band = 4.0F;
    // Stall. |speed| under stall_speed for stall_seconds of demanded
    // throttle starts a reverse of reverse_seconds. Contact multiplies the
    // rate the timer runs at.
    float stall_speed = 1.0F;
    float stall_seconds = 1.5F;
    float contact_stall_scale = 2.0F;
    float reverse_seconds = 1.5F;
    float reverse_throttle = 0.6F;
    // Rejoining. Above rejoin_cross_track of cross-track error the follower
    // aims at the path itself rather than at a lookahead point, and stays
    // there until the error is back under rejoin_clear.
    float rejoin_cross_track = 6.0F;
    float rejoin_clear = 2.5F;
    float rejoin_speed = 12.0F;
    // How close counts as having reached a checkpoint.
    float capture_radius = 4.0F;
    // Optional lateral jitter of the target point, in path units. Zero, the
    // default, makes the controller use no randomness at all and ignore the
    // seed. Above zero, one value in [-lane_jitter, +lane_jitter] is drawn
    // per jitter_period seconds from the seed reset() was given.
    float lane_jitter = 0.0F;
    float jitter_period = 1.0F;
};

enum class PathFollowMode {
    // Aiming at a lookahead point on the path.
    following,
    // Aiming at the path itself: too far off it, or a checkpoint was
    // skipped.
    rejoining,
    // Backing up after a stall.
    reversing,
};

struct PathFollowTelemetry {
    // The segment the vehicle projects onto, and the fraction along it.
    std::size_t segment = 0;
    float segment_fraction = 0.0F;
    // Signed perpendicular distance to that segment's line, positive when
    // the vehicle is to the left of the segment direction.
    float cross_track_error = 0.0F;
    // Distance along the path of the projection.
    float path_distance = 0.0F;
    // Where the controller is aiming this step, and how far ahead along the
    // path that was taken (0 in rejoining and reversing).
    float target[2] = {0.0F, 0.0F};
    float lookahead = 0.0F;
    // Radians, wrapped to (-pi, pi], of the target bearing less the heading.
    float heading_error = 0.0F;
    // The speed the controller asked for, before the throttle and brake
    // split.
    float chosen_speed = 0.0F;
    // The jitter applied to the target this step; 0 unless lane_jitter is.
    float lane_offset = 0.0F;
    // The earliest checkpoint not yet captured, or path point count when
    // none is left.
    std::size_t next_checkpoint = 0;
    bool checkpoint_missed = false;
    float stall_timer = 0.0F;
    float reverse_timer = 0.0F;
};

struct PathFollowOutput {
    float steering = 0.0F;
    float throttle = 0.0F;
    float brake = 0.0F;
    // True while backing up; `throttle` then drives the vehicle backwards.
    bool reverse = false;
    PathFollowMode mode = PathFollowMode::following;
    PathFollowTelemetry telemetry;
};

// Refuses a path with fewer than 2 points, a non-finite coordinate, a
// negative speed limit, or two consecutive points closer together than
// 1e-3. The diagnostic says which.
bool validate_path(const PathFollowPath& path, std::string& diagnostic);

class PathFollower {
public:
    // Copies the path and the tuning. Returns false, with a diagnostic and
    // the follower left unusable, when validate_path refuses the path or the
    // tuning holds a non-positive steer_gain, speed_band, stall_seconds,
    // reverse_seconds or jitter_period.
    bool reset(const PathFollowPath& path,
               const PathFollowTuning& tuning,
               std::uint32_t seed,
               std::string& diagnostic);

    // One step. `dt` is clamped to [0, 1] seconds; a non-finite state or dt
    // returns a zero demand in the current mode and changes nothing.
    PathFollowOutput step(const PathFollowVehicleState& state, float dt);

    bool ready() const noexcept { return ready_; }
    // Total length of the path, including the closing segment when closed.
    float path_length() const noexcept { return length_; }
    // The point at `distance` along the path, wrapped when closed and
    // clamped when open.
    void point_at(float distance, float out[2]) const;

private:
    struct Projection {
        std::size_t segment = 0;
        float fraction = 0.0F;
        float distance = 0.0F;
        float cross_track = 0.0F;
        float closest[2] = {0.0F, 0.0F};
    };

    Projection project(const float position[2]) const;
    float speed_window(float from, float to) const;
    float draw_jitter();

    PathFollowPath path_;
    PathFollowTuning tuning_;
    // Cumulative distance at each point, and the length of each segment.
    std::vector<float> cumulative_;
    std::vector<float> segment_length_;
    float length_ = 0.0F;
    std::size_t segments_ = 0;
    bool ready_ = false;

    std::uint32_t seed_ = 0;
    std::uint32_t rng_ = 0;
    float jitter_clock_ = 0.0F;
    float jitter_ = 0.0F;
    bool jitter_drawn_ = false;

    std::size_t next_checkpoint_ = 0;
    float stall_timer_ = 0.0F;
    float reverse_timer_ = 0.0F;
    PathFollowMode mode_ = PathFollowMode::following;
};

} // namespace nfsu
