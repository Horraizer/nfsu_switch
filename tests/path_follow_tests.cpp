// The path-following controller (issue #63, docs/RACING_LINE.md).
//
// No game data and no retail file. Every expected value below is derived from
// the geometry and from the tuning the case sets, written out rather than read
// back from the controller: a straight path along +x makes the projection,
// the lookahead and the bearing arithmetic that can be done by hand.

#include "nfsu/path_follow.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846F;

void check(bool condition, const std::string& what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

void near(float got, float want, float tolerance, const std::string& what) {
    if (!(std::fabs(got - want) <= tolerance)) {
        std::ostringstream text;
        text << what << ": got " << got << ", wanted " << want << " +- "
             << tolerance;
        throw std::runtime_error(text.str());
    }
}

// A straight path from (0,0) to (10 * (count - 1), 0), one point every 10.
nfsu::PathFollowPath straight(std::size_t count) {
    nfsu::PathFollowPath path;
    for (std::size_t i = 0; i < count; ++i) {
        nfsu::PathFollowPoint point;
        point.position[0] = static_cast<float>(10 * i);
        point.position[1] = 0.0F;
        path.points.push_back(point);
    }
    return path;
}

nfsu::PathFollowTuning plain() {
    nfsu::PathFollowTuning tuning;
    // No jitter: the controller then uses no randomness at all.
    tuning.lane_jitter = 0.0F;
    return tuning;
}

nfsu::PathFollowVehicleState at(float x, float y, float heading, float speed) {
    nfsu::PathFollowVehicleState state;
    state.position[0] = x;
    state.position[1] = y;
    state.heading = heading;
    state.speed = speed;
    return state;
}

// -------------------------------------------------------------------------

void test_refusals() {
    nfsu::PathFollower follower;
    std::string why;
    nfsu::PathFollowPath one = straight(1);
    check(!follower.reset(one, plain(), 0, why) && !follower.ready(),
          "a one-point path is refused");
    check(why.find("fewer than 2") != std::string::npos, "why: " + why);

    nfsu::PathFollowPath doubled = straight(3);
    doubled.points[1].position[0] = 0.0F;
    check(!follower.reset(doubled, plain(), 0, why), "a zero segment refused");
    check(why.find("path segment 0") != std::string::npos, "why: " + why);

    nfsu::PathFollowPath bad = straight(3);
    bad.points[2].position[1] = std::nanf("");
    check(!follower.reset(bad, plain(), 0, why), "a non-finite point refused");
    check(why.find("not finite") != std::string::npos, "why: " + why);

    nfsu::PathFollowPath negative = straight(3);
    negative.points[1].speed_limit = -1.0F;
    check(!follower.reset(negative, plain(), 0, why),
          "a negative speed limit is refused");
    check(why.find("is negative") != std::string::npos, "why: " + why);

    nfsu::PathFollowTuning tuning = plain();
    tuning.steer_gain = 0.0F;
    check(!follower.reset(straight(3), tuning, 0, why),
          "a zero steer gain is refused");
    check(why.find("positive steer_gain") != std::string::npos, "why: " + why);

    // A follower that never reset returns a zero demand rather than reading
    // uninitialised state.
    nfsu::PathFollower fresh;
    const nfsu::PathFollowOutput out = fresh.step(at(0, 0, 0, 0), 0.016F);
    check(out.steering == 0.0F && out.throttle == 0.0F && out.brake == 0.0F,
          "an unready follower demands nothing");
}

void test_projection_and_lookahead() {
    nfsu::PathFollower follower;
    std::string why;
    nfsu::PathFollowTuning tuning = plain();
    tuning.lookahead_min = 8.0F;
    tuning.lookahead_seconds = 0.5F;
    tuning.cruise_speed = 40.0F;
    tuning.speed_band = 4.0F;
    tuning.steer_gain = 1.6F;
    check(follower.reset(straight(5), tuning, 0, why), "reset: " + why);
    near(follower.path_length(), 40.0F, 1.0e-4F, "path length");

    // On the line at x = 15, pointing along it, stopped. Segment 1, fraction
    // 0.5, distance 15, no cross-track error. Lookahead is
    // max(8, 0.5 * 0) = 8, so the target is (23, 0) and the bearing is 0.
    nfsu::PathFollowOutput out = follower.step(at(15, 0, 0, 0), 0.016F);
    check(out.mode == nfsu::PathFollowMode::following, "following on the line");
    check(out.telemetry.segment == 1, "segment 1");
    near(out.telemetry.segment_fraction, 0.5F, 1.0e-5F, "fraction");
    near(out.telemetry.path_distance, 15.0F, 1.0e-4F, "path distance");
    near(out.telemetry.cross_track_error, 0.0F, 1.0e-5F, "no cross track");
    near(out.telemetry.lookahead, 8.0F, 1.0e-5F, "lookahead at rest");
    near(out.telemetry.target[0], 23.0F, 1.0e-4F, "target x");
    near(out.telemetry.target[1], 0.0F, 1.0e-5F, "target y");
    near(out.telemetry.heading_error, 0.0F, 1.0e-5F, "no heading error");
    near(out.steering, 0.0F, 1.0e-5F, "no steering");
    // chosen 40, speed 0, band 4: throttle clamps to 1.
    near(out.telemetry.chosen_speed, 40.0F, 1.0e-4F, "cruise speed");
    near(out.throttle, 1.0F, 1.0e-5F, "full throttle from rest");
    near(out.brake, 0.0F, 1.0e-5F, "no brake");

    // Lookahead grows with speed: 0.5 * 30 = 15, so the target is at 30.
    follower.reset(straight(5), tuning, 0, why);
    out = follower.step(at(15, 0, 0, 30), 0.016F);
    near(out.telemetry.lookahead, 15.0F, 1.0e-5F, "lookahead at 30");
    near(out.telemetry.target[0], 30.0F, 1.0e-4F, "target at 30");
    // 40 - 30 = 10 over a band of 4 clamps to 1.
    near(out.throttle, 1.0F, 1.0e-5F, "throttle below cruise");

    // Above cruise it brakes: 40 - 42 = -2 over 4 is 0.5.
    follower.reset(straight(5), tuning, 0, why);
    out = follower.step(at(15, 0, 0, 42), 0.016F);
    near(out.throttle, 0.0F, 1.0e-5F, "no throttle over cruise");
    near(out.brake, 0.5F, 1.0e-5F, "half brake two over cruise");

    // Off to the left of a +x path the cross-track error is positive.
    follower.reset(straight(5), tuning, 0, why);
    out = follower.step(at(15, 3, 0, 0), 0.016F);
    near(out.telemetry.cross_track_error, 3.0F, 1.0e-4F, "left is positive");
    // The target is at (23, 0) from (15, 3): bearing atan2(-3, 8).
    const float bearing = std::atan2(-3.0F, 8.0F);
    near(out.telemetry.heading_error, bearing, 1.0e-5F, "heading error");
    near(out.steering, bearing / 1.6F, 1.0e-5F, "steering is error / gain");

    // And negative to the right.
    follower.reset(straight(5), tuning, 0, why);
    out = follower.step(at(15, -3, 0, 0), 0.016F);
    near(out.telemetry.cross_track_error, -3.0F, 1.0e-4F, "right is negative");

    // The steering clamp.
    follower.reset(straight(5), tuning, 0, why);
    out = follower.step(at(15, 0, kPi, 0), 0.016F);
    near(std::fabs(out.steering), tuning.max_steer, 1.0e-5F,
          "a reversed heading saturates the steering");
}

void test_closed_path_wraps() {
    // A square of side 20: length 80.
    nfsu::PathFollowPath path;
    const float corners[4][2] = {{0, 0}, {20, 0}, {20, 20}, {0, 20}};
    for (const auto& corner : corners) {
        nfsu::PathFollowPoint point;
        point.position[0] = corner[0];
        point.position[1] = corner[1];
        path.points.push_back(point);
    }
    path.closed = true;
    nfsu::PathFollower follower;
    std::string why;
    nfsu::PathFollowTuning tuning = plain();
    tuning.lookahead_min = 20.0F;
    tuning.lookahead_seconds = 0.0F;
    check(follower.reset(path, tuning, 0, why), "reset: " + why);
    near(follower.path_length(), 80.0F, 1.0e-4F, "closed length");

    // At (0, 15) the projection is on the last segment, (0,20) -> (0,0), at
    // distance 60 + 5 = 65. A lookahead of 20 reaches 85, which wraps to 5,
    // a quarter along the first segment, so the target is (5, 0).
    const nfsu::PathFollowOutput out = follower.step(at(0, 15, 0, 0), 0.016F);
    near(out.telemetry.path_distance, 65.0F, 1.0e-3F, "distance on the seam");
    near(out.telemetry.target[0], 5.0F, 1.0e-3F, "target wraps: x");
    near(out.telemetry.target[1], 0.0F, 1.0e-3F, "target wraps: y");

    // point_at wraps a distance past the end and a negative one.
    float probe[2] = {0.0F, 0.0F};
    follower.point_at(85.0F, probe);
    near(probe[0], 5.0F, 1.0e-3F, "point_at wraps forward");
    follower.point_at(-5.0F, probe);
    near(probe[0], 0.0F, 1.0e-3F, "point_at wraps back: x");
    near(probe[1], 5.0F, 1.0e-3F, "point_at wraps back: y");
}

void test_speed_window() {
    nfsu::PathFollowPath path = straight(5);
    // A 12 limit at the point 20 along and a 25 limit at 30 along.
    path.points[2].speed_limit = 12.0F;
    path.points[3].speed_limit = 25.0F;
    nfsu::PathFollower follower;
    std::string why;
    nfsu::PathFollowTuning tuning = plain();
    tuning.lookahead_min = 8.0F;
    tuning.lookahead_seconds = 0.0F;
    tuning.cruise_speed = 40.0F;
    check(follower.reset(path, tuning, 0, why), "reset: " + why);

    // At 15 the window is [15, 23]: it holds the 20-point's 12 only.
    nfsu::PathFollowOutput out = follower.step(at(15, 0, 0, 0), 0.016F);
    near(out.telemetry.chosen_speed, 12.0F, 1.0e-4F, "the nearer limit wins");

    // At 5 the window is [5, 13]: no limit point, so cruise.
    follower.reset(path, tuning, 0, why);
    out = follower.step(at(5, 0, 0, 0), 0.016F);
    near(out.telemetry.chosen_speed, 40.0F, 1.0e-4F, "no limit in the window");

    // At 25 the window is [25, 33]: the 30-point's 25.
    follower.reset(path, tuning, 0, why);
    out = follower.step(at(25, 0, 0, 0), 0.016F);
    near(out.telemetry.chosen_speed, 25.0F, 1.0e-4F, "the later limit");
}

void test_determinism_and_seed() {
    nfsu::PathFollowTuning tuning = plain();
    const auto run = [&](std::uint32_t seed, const nfsu::PathFollowTuning& t) {
        nfsu::PathFollower follower;
        std::string why;
        if (!follower.reset(straight(9), t, seed, why)) {
            throw std::runtime_error("reset: " + why);
        }
        std::vector<float> trace;
        nfsu::PathFollowVehicleState state = at(5, 2, 0.1F, 20);
        for (int i = 0; i < 60; ++i) {
            const nfsu::PathFollowOutput out = follower.step(state, 1.0F / 60.0F);
            trace.push_back(out.steering);
            trace.push_back(out.throttle);
            trace.push_back(out.telemetry.lane_offset);
            // A crude integration, so the trace is not a constant.
            state.heading += out.steering * 0.05F;
            state.position[0] += std::cos(state.heading) * state.speed / 60.0F;
            state.position[1] += std::sin(state.heading) * state.speed / 60.0F;
        }
        return trace;
    };

    // With no jitter the seed makes no difference and two runs agree.
    const std::vector<float> a = run(1, tuning);
    const std::vector<float> b = run(99, tuning);
    check(a == b, "with lane_jitter 0 the seed is ignored");
    check(run(1, tuning) == a, "the same seed and states give the same trace");
    bool any_offset = false;
    for (std::size_t i = 2; i < a.size(); i += 3) {
        any_offset = any_offset || a[i] != 0.0F;
    }
    check(!any_offset, "no jitter means no lane offset");

    // With jitter the seed does make a difference, and each seed repeats.
    nfsu::PathFollowTuning jitter = tuning;
    jitter.lane_jitter = 2.0F;
    jitter.jitter_period = 0.25F;
    const std::vector<float> c = run(1, jitter);
    const std::vector<float> d = run(2, jitter);
    check(c != d, "two seeds give two traces");
    check(run(1, jitter) == c, "a seed repeats exactly");
    float largest = 0.0F;
    for (std::size_t i = 2; i < c.size(); i += 3) {
        largest = std::fabs(c[i]) > largest ? std::fabs(c[i]) : largest;
    }
    check(largest > 0.0F && largest <= jitter.lane_jitter,
          "the jitter stays inside lane_jitter");

    // A non-finite state leaves the follower where it was.
    nfsu::PathFollower follower;
    std::string why;
    check(follower.reset(straight(5), tuning, 0, why), "reset: " + why);
    follower.step(at(15, 0, 0, 20), 1.0F / 60.0F);
    const nfsu::PathFollowOutput bad =
        follower.step(at(std::nanf(""), 0, 0, 20), 1.0F / 60.0F);
    check(bad.steering == 0.0F && bad.throttle == 0.0F,
          "a non-finite state demands nothing");
    const nfsu::PathFollowOutput after =
        follower.step(at(15, 0, 0, 20), 1.0F / 60.0F);
    near(after.telemetry.path_distance, 15.0F, 1.0e-4F,
         "and changed nothing behind it");
}

// -------------------------------------------------------------------------
// The three recovery cases issue #63 names.

void test_recovery_stalled() {
    nfsu::PathFollower follower;
    std::string why;
    nfsu::PathFollowTuning tuning = plain();
    tuning.stall_speed = 1.0F;
    tuning.stall_seconds = 1.0F;
    tuning.reverse_seconds = 0.5F;
    tuning.reverse_throttle = 0.6F;
    tuning.cruise_speed = 40.0F;
    check(follower.reset(straight(9), tuning, 0, why), "reset: " + why);

    // Held at 0.2 units per second on the line, asking for throttle. The
    // timer needs 1.0 s. A step of 0.125 s is exact in binary floating point,
    // so eight of them sum to exactly 1.0 and the eighth is the one that
    // trips: index 7, counting from zero.
    const float dt = 0.125F;
    const nfsu::PathFollowVehicleState stuck = at(15, 0, 0, 0.2F);
    nfsu::PathFollowOutput out;
    int steps = 0;
    for (; steps < 200; ++steps) {
        out = follower.step(stuck, dt);
        if (out.mode == nfsu::PathFollowMode::reversing) {
            break;
        }
    }
    check(out.mode == nfsu::PathFollowMode::reversing,
          "a stalled car ends up reversing");
    check(steps == 7, "it trips on the eighth step, not before: " +
                          std::to_string(steps));
    check(out.reverse && out.throttle == tuning.reverse_throttle &&
              out.brake == 0.0F,
          "reversing drives backwards and does not brake");
    // Backing up steers the other way, so the nose comes round.
    nfsu::PathFollower mirror;
    check(mirror.reset(straight(9), tuning, 0, why), "reset: " + why);
    const nfsu::PathFollowOutput forward =
        mirror.step(at(15, 3, 0.3F, 0.2F), dt);
    nfsu::PathFollower reversing;
    check(reversing.reset(straight(9), tuning, 0, why), "reset: " + why);
    nfsu::PathFollowOutput back;
    for (int i = 0; i < 200; ++i) {
        back = reversing.step(at(15, 3, 0.3F, 0.2F), dt);
        if (back.mode == nfsu::PathFollowMode::reversing) {
            break;
        }
    }
    near(back.steering, -forward.steering, 1.0e-5F,
         "the reverse steering is the mirror of the forward demand");

    // It stops reversing after reverse_seconds and, back on the line, goes
    // on following.
    const int reverse_steps =
        static_cast<int>(tuning.reverse_seconds / dt + 0.5F);
    for (int i = 0; i < reverse_steps; ++i) {
        out = follower.step(at(15, 0, 0, -3.0F), dt);
    }
    check(out.mode == nfsu::PathFollowMode::following,
          "the reverse ends and following resumes");
    check(out.telemetry.stall_timer == 0.0F, "and the stall timer is cleared");

    // A car that is merely slow but moving does not stall.
    nfsu::PathFollower rolling;
    check(rolling.reset(straight(9), tuning, 0, why), "reset: " + why);
    for (int i = 0; i < 400; ++i) {
        out = rolling.step(at(15, 0, 0, 5.0F), dt);
    }
    check(out.mode == nfsu::PathFollowMode::following,
          "a rolling car never stalls");
    check(out.telemetry.stall_timer == 0.0F, "and its timer stays at zero");
}

void test_recovery_contact_stalls_sooner() {
    nfsu::PathFollower follower;
    std::string why;
    nfsu::PathFollowTuning tuning = plain();
    tuning.stall_speed = 1.0F;
    tuning.stall_seconds = 1.0F;
    tuning.contact_stall_scale = 2.0F;
    check(follower.reset(straight(9), tuning, 0, why), "reset: " + why);

    // Pinned against something: the timer runs at twice the rate, so the
    // same 1.0 s takes half as many steps. At 0.125 s a step that is four,
    // index 3, against the eight of test_recovery_stalled.
    nfsu::PathFollowVehicleState pinned = at(15, 0, 0, 0.0F);
    pinned.contact = true;
    int steps = 0;
    nfsu::PathFollowOutput out;
    for (; steps < 200; ++steps) {
        out = follower.step(pinned, 0.125F);
        if (out.mode == nfsu::PathFollowMode::reversing) {
            break;
        }
    }
    check(out.mode == nfsu::PathFollowMode::reversing,
          "a car in contact reverses");
    check(steps == 3, "at twice the rate it trips on the fourth step: " +
                          std::to_string(steps));
}

void test_recovery_missed_checkpoint() {
    nfsu::PathFollowPath path = straight(9);
    // Gates 20 and 60 along the path.
    path.points[2].checkpoint = true;
    path.points[6].checkpoint = true;
    nfsu::PathFollower follower;
    std::string why;
    nfsu::PathFollowTuning tuning = plain();
    tuning.capture_radius = 4.0F;
    tuning.rejoin_speed = 12.0F;
    tuning.speed_band = 4.0F;
    check(follower.reset(path, tuning, 0, why), "reset: " + why);

    // Before the first gate: following, and the next gate is point 2.
    nfsu::PathFollowOutput out = follower.step(at(10, 0, 0, 20), 0.016F);
    check(out.mode == nfsu::PathFollowMode::following, "following before a gate");
    check(out.telemetry.next_checkpoint == 2, "point 2 is next");
    check(!out.telemetry.checkpoint_missed, "nothing missed yet");

    // Within 4 of it captures it and moves on to point 6.
    out = follower.step(at(17, 0, 0, 20), 0.016F);
    check(out.telemetry.next_checkpoint == 6, "capturing 2 leaves 6 next");

    // Now skip point 6 entirely: appear at 70 along, past it and never
    // within 4 of it.
    out = follower.step(at(70, 0, 0, 20), 0.016F);
    check(out.telemetry.checkpoint_missed, "point 6 is missed");
    check(out.mode == nfsu::PathFollowMode::rejoining,
          "a missed gate puts the follower into rejoining");
    near(out.telemetry.target[0], 60.0F, 1.0e-4F,
         "and it aims at the gate it missed");
    near(out.telemetry.target[1], 0.0F, 1.0e-5F, "gate y");
    near(out.telemetry.chosen_speed, tuning.rejoin_speed, 1.0e-4F,
         "at the rejoin speed");
    // Aiming back at 60 from 70 while heading along +x means turning round.
    near(std::fabs(out.telemetry.heading_error), kPi, 1.0e-4F,
         "the bearing is behind it");
    // 12 - 20 is -8 over a band of 4, so it brakes fully.
    near(out.brake, 1.0F, 1.0e-5F, "and it brakes");
    check(out.telemetry.lookahead == 0.0F,
          "rejoining takes no lookahead");

    // Coming back within 4 of it captures it, and with no gate left the
    // follower goes back to following.
    out = follower.step(at(62, 0, 0, 5), 0.016F);
    check(out.telemetry.next_checkpoint == path.points.size(),
          "no gate is left");
    check(!out.telemetry.checkpoint_missed, "nothing is missed now");
    check(out.mode == nfsu::PathFollowMode::following, "following resumes");
}

void test_recovery_collision_displacement() {
    nfsu::PathFollower follower;
    std::string why;
    nfsu::PathFollowTuning tuning = plain();
    tuning.rejoin_cross_track = 6.0F;
    tuning.rejoin_clear = 2.5F;
    tuning.rejoin_speed = 12.0F;
    tuning.lookahead_min = 8.0F;
    tuning.lookahead_seconds = 0.0F;
    check(follower.reset(straight(9), tuning, 0, why), "reset: " + why);

    nfsu::PathFollowOutput out = follower.step(at(20, 0, 0, 30), 0.016F);
    check(out.mode == nfsu::PathFollowMode::following, "following on the line");

    // A collision throws the car 9 to the left and spins it. 9 is over
    // rejoin_cross_track, so the follower aims at the path itself: the
    // projection of (20, 9) on a +x path is (20, 0).
    nfsu::PathFollowVehicleState hit = at(20, 9, 1.2F, 6);
    hit.contact = true;
    out = follower.step(hit, 0.016F);
    check(out.mode == nfsu::PathFollowMode::rejoining,
          "a displaced car rejoins");
    near(out.telemetry.cross_track_error, 9.0F, 1.0e-4F, "cross track 9");
    near(out.telemetry.target[0], 20.0F, 1.0e-4F, "target is the projection x");
    near(out.telemetry.target[1], 0.0F, 1.0e-4F, "target is the projection y");
    // From (20, 9) to (20, 0) the bearing is -pi/2; heading 1.2 makes the
    // error -pi/2 - 1.2.
    near(out.telemetry.heading_error, -kPi / 2.0F - 1.2F, 1.0e-4F,
         "heading error back to the line");
    near(out.steering,
         std::fmax(-tuning.max_steer,
                   (-kPi / 2.0F - 1.2F) / tuning.steer_gain),
         1.0e-4F, "steering back to the line");
    near(out.telemetry.chosen_speed, tuning.rejoin_speed, 1.0e-4F,
         "at the rejoin speed");

    // It stays rejoining while the error is between rejoin_clear and
    // rejoin_cross_track: 4 is inside that band.
    out = follower.step(at(20, 4, 0, 6), 0.016F);
    check(out.mode == nfsu::PathFollowMode::rejoining,
          "it holds on until rejoin_clear");
    near(out.telemetry.target[0], 20.0F, 1.0e-4F, "still aiming at the line");

    // Under rejoin_clear it goes back to following, and takes a lookahead
    // target again: 2 off the line at 20 along, plus 8, is (28, 0).
    out = follower.step(at(20, 2, 0, 6), 0.016F);
    check(out.mode == nfsu::PathFollowMode::following, "and then follows");
    near(out.telemetry.target[0], 28.0F, 1.0e-4F, "lookahead target again");
    near(out.telemetry.lookahead, 8.0F, 1.0e-5F, "the lookahead is back");

    // Contact on its own, at speed, changes nothing.
    nfsu::PathFollower touching;
    check(touching.reset(straight(9), tuning, 0, why), "reset: " + why);
    nfsu::PathFollowVehicleState scraping = at(20, 0, 0, 30);
    scraping.contact = true;
    const nfsu::PathFollowOutput clean = touching.step(scraping, 0.016F);
    nfsu::PathFollower free_run;
    check(free_run.reset(straight(9), tuning, 0, why), "reset: " + why);
    const nfsu::PathFollowOutput plain_out =
        free_run.step(at(20, 0, 0, 30), 0.016F);
    check(clean.steering == plain_out.steering &&
              clean.throttle == plain_out.throttle &&
              clean.mode == plain_out.mode,
          "contact at speed does not change the demand");
}

} // namespace

int main() {
    try {
        test_refusals();
        test_projection_and_lookahead();
        test_closed_path_wraps();
        test_speed_window();
        test_determinism_and_seed();
        test_recovery_stalled();
        test_recovery_contact_stalls_sooner();
        test_recovery_missed_checkpoint();
        test_recovery_collision_displacement();
        std::cout << "path follow tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "path follow tests: " << e.what() << '\n';
        return 1;
    }
}
