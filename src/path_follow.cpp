#include "nfsu/path_follow.h"

#include <cmath>
#include <sstream>

namespace nfsu {
namespace {

constexpr float kPi = 3.14159265358979323846F;
// Two consecutive points closer than this are refused: the segment has no
// usable direction.
constexpr float kMinSegment = 1.0e-3F;

float clampf(float value, float low, float high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

// To (-pi, pi].
float wrap_angle(float angle) {
    const float turn = 2.0F * kPi;
    float value = std::fmod(angle, turn);
    if (value <= -kPi) {
        value += turn;
    } else if (value > kPi) {
        value -= turn;
    }
    return value;
}

bool finite2(const float value[2]) {
    return std::isfinite(value[0]) && std::isfinite(value[1]);
}

} // namespace

bool validate_path(const PathFollowPath& path, std::string& diagnostic) {
    diagnostic.clear();
    if (path.points.size() < 2) {
        std::ostringstream text;
        text << "path has " << path.points.size() << " points, fewer than 2";
        diagnostic = text.str();
        return false;
    }
    for (std::size_t i = 0; i < path.points.size(); ++i) {
        const PathFollowPoint& point = path.points[i];
        if (!finite2(point.position) || !std::isfinite(point.speed_limit)) {
            std::ostringstream text;
            text << "path point " << i << " is not finite";
            diagnostic = text.str();
            return false;
        }
        if (point.speed_limit < 0.0F) {
            std::ostringstream text;
            text << "path point " << i << " speed limit "
                 << point.speed_limit << " is negative";
            diagnostic = text.str();
            return false;
        }
    }
    const std::size_t segments =
        path.closed ? path.points.size() : path.points.size() - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        const PathFollowPoint& a = path.points[i];
        const PathFollowPoint& b = path.points[(i + 1) % path.points.size()];
        const float dx = b.position[0] - a.position[0];
        const float dy = b.position[1] - a.position[1];
        const float length = std::sqrt(dx * dx + dy * dy);
        if (!(length >= kMinSegment)) {
            std::ostringstream text;
            text << "path segment " << i << " is " << length
                 << " long, under " << kMinSegment;
            diagnostic = text.str();
            return false;
        }
    }
    return true;
}

bool PathFollower::reset(const PathFollowPath& path,
                        const PathFollowTuning& tuning,
                        std::uint32_t seed,
                        std::string& diagnostic) {
    ready_ = false;
    path_ = PathFollowPath{};
    cumulative_.clear();
    segment_length_.clear();
    length_ = 0.0F;
    segments_ = 0;
    mode_ = PathFollowMode::following;
    stall_timer_ = 0.0F;
    reverse_timer_ = 0.0F;
    next_checkpoint_ = 0;
    jitter_ = 0.0F;
    jitter_clock_ = 0.0F;
    jitter_drawn_ = false;
    seed_ = seed;
    rng_ = seed;

    if (!validate_path(path, diagnostic)) {
        return false;
    }
    if (!(tuning.steer_gain > 0.0F) || !(tuning.speed_band > 0.0F) ||
        !(tuning.stall_seconds > 0.0F) || !(tuning.reverse_seconds > 0.0F) ||
        !(tuning.jitter_period > 0.0F)) {
        diagnostic = "tuning needs positive steer_gain, speed_band, "
                     "stall_seconds, reverse_seconds and jitter_period";
        return false;
    }
    if (tuning.lane_jitter < 0.0F || tuning.capture_radius < 0.0F ||
        tuning.max_steer < 0.0F) {
        diagnostic = "tuning needs non-negative lane_jitter, capture_radius "
                     "and max_steer";
        return false;
    }

    path_ = path;
    tuning_ = tuning;
    segments_ = path_.closed ? path_.points.size() : path_.points.size() - 1;
    cumulative_.assign(path_.points.size(), 0.0F);
    segment_length_.assign(segments_, 0.0F);
    float running = 0.0F;
    for (std::size_t i = 0; i < segments_; ++i) {
        const PathFollowPoint& a = path_.points[i];
        const PathFollowPoint& b = path_.points[(i + 1) % path_.points.size()];
        const float dx = b.position[0] - a.position[0];
        const float dy = b.position[1] - a.position[1];
        segment_length_[i] = std::sqrt(dx * dx + dy * dy);
        running += segment_length_[i];
        if (i + 1 < path_.points.size()) {
            cumulative_[i + 1] = running;
        }
    }
    length_ = running;

    next_checkpoint_ = path_.points.size();
    for (std::size_t i = 0; i < path_.points.size(); ++i) {
        if (path_.points[i].checkpoint) {
            next_checkpoint_ = i;
            break;
        }
    }
    ready_ = true;
    return true;
}

PathFollower::Projection PathFollower::project(const float position[2]) const {
    Projection best;
    float best_distance2 = -1.0F;
    for (std::size_t i = 0; i < segments_; ++i) {
        const PathFollowPoint& a = path_.points[i];
        const PathFollowPoint& b = path_.points[(i + 1) % path_.points.size()];
        const float ax = a.position[0];
        const float ay = a.position[1];
        const float dx = b.position[0] - ax;
        const float dy = b.position[1] - ay;
        const float length = segment_length_[i];
        const float inv = 1.0F / (length * length);
        float t = ((position[0] - ax) * dx + (position[1] - ay) * dy) * inv;
        t = clampf(t, 0.0F, 1.0F);
        const float px = ax + dx * t;
        const float py = ay + dy * t;
        const float ex = position[0] - px;
        const float ey = position[1] - py;
        const float distance2 = ex * ex + ey * ey;
        if (best_distance2 < 0.0F || distance2 < best_distance2) {
            best_distance2 = distance2;
            best.segment = i;
            best.fraction = t;
            best.distance = cumulative_[i] + length * t;
            best.closest[0] = px;
            best.closest[1] = py;
            // Positive to the left of the segment direction.
            best.cross_track =
                ((position[1] - ay) * dx - (position[0] - ax) * dy) / length;
        }
    }
    return best;
}

void PathFollower::point_at(float distance, float out[2]) const {
    out[0] = 0.0F;
    out[1] = 0.0F;
    if (!ready_) {
        return;
    }
    float d = distance;
    if (path_.closed && length_ > 0.0F) {
        d = std::fmod(d, length_);
        if (d < 0.0F) {
            d += length_;
        }
    } else {
        d = clampf(d, 0.0F, length_);
    }
    for (std::size_t i = 0; i < segments_; ++i) {
        if (d <= cumulative_[i] + segment_length_[i] || i + 1 == segments_) {
            const PathFollowPoint& a = path_.points[i];
            const PathFollowPoint& b =
                path_.points[(i + 1) % path_.points.size()];
            const float t =
                clampf((d - cumulative_[i]) / segment_length_[i], 0.0F, 1.0F);
            out[0] = a.position[0] + (b.position[0] - a.position[0]) * t;
            out[1] = a.position[1] + (b.position[1] - a.position[1]) * t;
            return;
        }
    }
}

// The smallest non-zero speed limit of every point whose cumulative distance
// lies in [from, to], wrapping on a closed path; cruise_speed when none.
float PathFollower::speed_window(float from, float to) const {
    float limit = tuning_.cruise_speed;
    for (std::size_t i = 0; i < path_.points.size(); ++i) {
        const float value = path_.points[i].speed_limit;
        if (!(value > 0.0F)) {
            continue;
        }
        float d = cumulative_[i];
        bool inside = false;
        if (path_.closed && to > length_) {
            inside = (d >= from) || (d <= to - length_);
        } else {
            inside = (d >= from) && (d <= to);
        }
        if (inside && value < limit) {
            limit = value;
        }
    }
    return limit;
}

float PathFollower::draw_jitter() {
    // Numerical Recipes' LCG, spelled out so the value does not depend on the
    // standard library.
    rng_ = rng_ * 1664525U + 1013904223U;
    const float unit =
        static_cast<float>(rng_ >> 8U) / static_cast<float>(1U << 24U);
    return (unit * 2.0F - 1.0F) * tuning_.lane_jitter;
}

PathFollowOutput PathFollower::step(const PathFollowVehicleState& state,
                                   float dt) {
    PathFollowOutput out;
    out.mode = mode_;
    if (!ready_) {
        return out;
    }
    if (!finite2(state.position) || !std::isfinite(state.heading) ||
        !std::isfinite(state.speed) || !std::isfinite(dt)) {
        out.telemetry.next_checkpoint = next_checkpoint_;
        out.telemetry.stall_timer = stall_timer_;
        out.telemetry.reverse_timer = reverse_timer_;
        return out;
    }
    const float step_dt = clampf(dt, 0.0F, 1.0F);

    const Projection projection = project(state.position);

    // Checkpoints. One is captured when the vehicle comes within
    // capture_radius of it; capturing it advances to the next one in path
    // order. It counts as missed while the projection has passed it and it
    // is still not captured.
    bool missed = false;
    if (next_checkpoint_ < path_.points.size()) {
        const PathFollowPoint& gate = path_.points[next_checkpoint_];
        const float dx = state.position[0] - gate.position[0];
        const float dy = state.position[1] - gate.position[1];
        if (std::sqrt(dx * dx + dy * dy) <= tuning_.capture_radius) {
            std::size_t next = path_.points.size();
            for (std::size_t i = next_checkpoint_ + 1;
                 i < path_.points.size(); ++i) {
                if (path_.points[i].checkpoint) {
                    next = i;
                    break;
                }
            }
            next_checkpoint_ = next;
        } else if (projection.distance > cumulative_[next_checkpoint_]) {
            missed = true;
        }
    }

    // Mode. Reversing wins over rejoining, which wins over following.
    const float error_magnitude = std::fabs(projection.cross_track);
    if (mode_ != PathFollowMode::reversing) {
        if (missed || error_magnitude > tuning_.rejoin_cross_track) {
            mode_ = PathFollowMode::rejoining;
        } else if (mode_ == PathFollowMode::rejoining &&
                   error_magnitude <= tuning_.rejoin_clear) {
            mode_ = PathFollowMode::following;
        }
    }

    // Where to aim.
    float target[2] = {0.0F, 0.0F};
    float lookahead = 0.0F;
    float lane_offset = 0.0F;
    float chosen_speed = 0.0F;
    if (mode_ == PathFollowMode::rejoining) {
        if (missed) {
            target[0] = path_.points[next_checkpoint_].position[0];
            target[1] = path_.points[next_checkpoint_].position[1];
        } else {
            target[0] = projection.closest[0];
            target[1] = projection.closest[1];
        }
        chosen_speed = tuning_.rejoin_speed;
    } else {
        lookahead = tuning_.lookahead_min;
        const float from_speed =
            tuning_.lookahead_seconds * (state.speed > 0.0F ? state.speed : 0.0F);
        if (from_speed > lookahead) {
            lookahead = from_speed;
        }
        point_at(projection.distance + lookahead, target);
        if (tuning_.lane_jitter > 0.0F) {
            jitter_clock_ += step_dt;
            if (!jitter_drawn_ || jitter_clock_ >= tuning_.jitter_period) {
                jitter_clock_ = 0.0F;
                jitter_drawn_ = true;
                jitter_ = draw_jitter();
            }
            lane_offset = jitter_;
            const std::size_t i = projection.segment;
            const PathFollowPoint& a = path_.points[i];
            const PathFollowPoint& b =
                path_.points[(i + 1) % path_.points.size()];
            const float dx = (b.position[0] - a.position[0]) / segment_length_[i];
            const float dy = (b.position[1] - a.position[1]) / segment_length_[i];
            // Left of the segment direction, matching cross_track's sign.
            target[0] += -dy * lane_offset;
            target[1] += dx * lane_offset;
        }
        chosen_speed =
            speed_window(projection.distance, projection.distance + lookahead);
    }

    // Steering from the bearing to the target.
    const float bearing = std::atan2(target[1] - state.position[1],
                                     target[0] - state.position[0]);
    const float heading_error = wrap_angle(bearing - state.heading);
    float steering = clampf(heading_error / tuning_.steer_gain,
                            -tuning_.max_steer, tuning_.max_steer);

    // Longitudinal.
    float throttle = 0.0F;
    float brake = 0.0F;
    bool reverse = false;
    if (mode_ == PathFollowMode::reversing) {
        reverse = true;
        throttle = tuning_.reverse_throttle;
        brake = 0.0F;
        // Back up turning the other way, so the nose comes round toward the
        // target.
        steering = -steering;
        chosen_speed = 0.0F;
        reverse_timer_ -= step_dt;
        if (reverse_timer_ <= 0.0F) {
            reverse_timer_ = 0.0F;
            stall_timer_ = 0.0F;
            mode_ = error_magnitude > tuning_.rejoin_cross_track
                        ? PathFollowMode::rejoining
                        : PathFollowMode::following;
        }
    } else {
        const float speed_error = chosen_speed - state.speed;
        throttle = clampf(speed_error / tuning_.speed_band, 0.0F, 1.0F);
        brake = clampf(-speed_error / tuning_.speed_band, 0.0F, 1.0F);
        // Stall: slow while asking for throttle. Contact makes it count
        // faster.
        if (throttle > 0.0F && std::fabs(state.speed) < tuning_.stall_speed) {
            stall_timer_ +=
                step_dt * (state.contact ? tuning_.contact_stall_scale : 1.0F);
            if (stall_timer_ >= tuning_.stall_seconds) {
                mode_ = PathFollowMode::reversing;
                reverse_timer_ = tuning_.reverse_seconds;
                reverse = true;
                throttle = tuning_.reverse_throttle;
                brake = 0.0F;
                steering = -steering;
                chosen_speed = 0.0F;
            }
        } else {
            stall_timer_ = 0.0F;
        }
    }

    out.steering = steering;
    out.throttle = throttle;
    out.brake = brake;
    out.reverse = reverse;
    out.mode = mode_;
    out.telemetry.segment = projection.segment;
    out.telemetry.segment_fraction = projection.fraction;
    out.telemetry.cross_track_error = projection.cross_track;
    out.telemetry.path_distance = projection.distance;
    out.telemetry.target[0] = target[0];
    out.telemetry.target[1] = target[1];
    out.telemetry.lookahead = lookahead;
    out.telemetry.heading_error = heading_error;
    out.telemetry.chosen_speed = chosen_speed;
    out.telemetry.lane_offset = lane_offset;
    out.telemetry.next_checkpoint = next_checkpoint_;
    out.telemetry.checkpoint_missed = missed;
    out.telemetry.stall_timer = stall_timer_;
    out.telemetry.reverse_timer = reverse_timer_;
    return out;
}

} // namespace nfsu
