// Issue #60: the vehicle on the track, over synthetic geometry only - the
// proving ground nfsu_vehicle_track builds in code, and small courses built
// here. No retail file is read. Each failure prints a "test failure:" line
// and the program exits 1; docs/VEHICLE_TRACK.md lists what is held.

#include "nfsu/vehicle_track.h"

#include "nfsu/det_sqrt.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cout << "test failure: " << message << '\n';
    }
}

std::string text(double value) {
    std::ostringstream out;
    out.precision(10);
    out << value;
    return out.str();
}

nfsu::VehicleTrackCourse proving_ground() {
    nfsu::VehicleTrackCourse course;
    std::string error;
    const bool built = nfsu::build_vehicle_proving_ground(course, error);
    check(built, "the proving ground builds: " + error);
    return course;
}

const nfsu::VehicleModel& stock_model() {
    static const nfsu::VehicleModel model(nfsu::stock_240sx_vehicle_spec());
    return model;
}

nfsu::VehicleTrackScenario scenario(const char* name) {
    nfsu::VehicleTrackScenario out;
    const bool found =
        nfsu::vehicle_track_scenario(nfsu::VehicleTrackCourseKind::proving_ground, name, out);
    check(found, std::string("the proving ground has a scenario ") + name);
    return out;
}

// A whole scenario's records.
std::vector<nfsu::VehicleTrackTelemetry> run(const nfsu::VehicleTrackCourse& course,
                                             const nfsu::VehicleTrackScenario& s,
                                             const nfsu::VehicleTrackApproximations& approx =
                                                 nfsu::VehicleTrackApproximations{}) {
    std::vector<nfsu::VehicleTrackTelemetry> out;
    nfsu::VehicleTrackSession session(stock_model(), course, approx);
    check(session.valid(), "a session over the course is valid: " + session.error());
    if (!session.start(nfsu::vehicle_track_scenario_pose(course.start, s))) {
        check(false, s.name + ": the session starts");
        return out;
    }
    const std::uint64_t steps = s.script.total_steps();
    for (std::uint64_t i = 0; i < steps; ++i) {
        nfsu::VehicleTrackTelemetry t;
        if (!session.step(s.script.input_at(i), t)) {
            check(false, s.name + ": step " + std::to_string(i + 1) + " is refused");
            return out;
        }
        out.push_back(t);
    }
    return out;
}

std::string digest(const std::vector<nfsu::VehicleTrackTelemetry>& records) {
    nfsu::VehicleTrackTelemetryHash hash;
    for (const nfsu::VehicleTrackTelemetry& t : records) {
        hash.add(t);
    }
    return nfsu::sha256_hex(hash.finish());
}

// The body's axes from a unit quaternion, as the model builds them.
struct Axes {
    nfsu::VehicleVec3 x;
    nfsu::VehicleVec3 y;
    nfsu::VehicleVec3 z;
};

Axes axes(const nfsu::VehicleQuat& q) {
    Axes a;
    a.x = {1.0 - 2.0 * (q.y * q.y + q.z * q.z), 2.0 * (q.x * q.y + q.w * q.z),
           2.0 * (q.x * q.z - q.w * q.y)};
    a.y = {2.0 * (q.x * q.y - q.w * q.z), 1.0 - 2.0 * (q.x * q.x + q.z * q.z),
           2.0 * (q.y * q.z + q.w * q.x)};
    a.z = {2.0 * (q.x * q.z + q.w * q.y), 2.0 * (q.y * q.z - q.w * q.x),
           1.0 - 2.0 * (q.x * q.x + q.y * q.y)};
    return a;
}

// The hull's points in the world for a record's state: the stock hull less
// the centre of mass, in float as 0x00472E40 moves it.
std::vector<nfsu::VehicleVec3> hull_in_world(const nfsu::VehicleTrackTelemetry& t) {
    const nfsu::VehicleHull hull = nfsu::stock_240sx_hull();
    const nfsu::VehicleHandling h = nfsu::stock_240sx_handling();
    const Axes a = axes(t.orientation);
    std::vector<nfsu::VehicleVec3> out;
    for (const std::array<float, 3>& p : hull.points) {
        const double bx = static_cast<double>(p[0] - h.centre_of_mass[0]);
        const double by = static_cast<double>(p[1] - h.centre_of_mass[1]);
        const double bz = static_cast<double>(p[2] - h.centre_of_mass[2]);
        out.push_back({t.position.x + a.x.x * bx + a.y.x * by + a.z.x * bz,
                       t.position.y + a.x.y * bx + a.y.y * by + a.z.y * bz,
                       t.position.z + a.x.z * bx + a.y.z * by + a.z.z * bz});
    }
    return out;
}

double speed(const nfsu::VehicleVec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// ---------------------------------------------------------------------------
// The hull table and its source bytes
// ---------------------------------------------------------------------------

void test_hull_source() {
    const nfsu::VehicleHull hull = nfsu::stock_240sx_hull();
    check(hull.points.size() == nfsu::kVehicle240sxHullPoints, "the hull holds 37 points");
    // Encoded into an all-0x00 and an all-0xFF payload, the bytes equal in
    // both are the ones the encoder writes: 444 of them, 12 at +0x10 of each
    // 0x30-byte record, hashing to the retail bytes' SHA-256.
    std::vector<unsigned char> zeros(nfsu::kVehicle240sxHullChunkBytes, 0x00);
    std::vector<unsigned char> ones(nfsu::kVehicle240sxHullChunkBytes, 0xFF);
    check(nfsu::encode_vehicle_hull(hull, zeros.data(), zeros.size()), "the hull encodes");
    check(nfsu::encode_vehicle_hull(hull, ones.data(), ones.size()), "the hull encodes twice");
    std::vector<unsigned char> written;
    bool layout = true;
    for (std::size_t i = 0; i < zeros.size(); ++i) {
        const std::size_t within = i % nfsu::kVehicleHullRecordSize;
        const bool expected = within >= nfsu::kVehicleHullPointOffset &&
                              within < nfsu::kVehicleHullPointOffset + 12;
        if ((zeros[i] == ones[i]) != expected) {
            layout = false;
        }
        if (zeros[i] == ones[i]) {
            written.push_back(zeros[i]);
        }
    }
    check(layout, "the encoder writes exactly +0x10..+0x1B of each record");
    check(written.size() == nfsu::kVehicle240sxHullBytes,
          "the encoder writes 444 bytes, got " + std::to_string(written.size()));
    nfsu::Sha256 sha;
    sha.update(written.data(), written.size());
    const std::string got = nfsu::sha256_hex(sha.finish());
    check(got == nfsu::kVehicle240sxHullBytesSha256,
          "the 444 bytes hash to the retail bytes' SHA-256, got " + got);
    check(!nfsu::encode_vehicle_hull(hull, zeros.data(), zeros.size() - 1),
          "a payload one byte short of 37 records is refused");
    check(!nfsu::encode_vehicle_hull(hull, nullptr, zeros.size()), "no payload is refused");
    // The hull spans the car: its extremes, as the file holds them.
    float low[3] = {1e9F, 1e9F, 1e9F};
    float high[3] = {-1e9F, -1e9F, -1e9F};
    for (const std::array<float, 3>& p : hull.points) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], p[axis]);
            high[axis] = std::max(high[axis], p[axis]);
        }
    }
    check(low[0] < -2.05F && high[0] > 2.36F && low[1] < -0.88F && high[1] > 0.87F &&
              low[2] > 0.087F && high[2] > 1.037F && high[2] < 1.038F,
          "the hull runs from -2.05 to 2.37 along, +-0.88 across and 0.088 to 1.037 up");
}

// ---------------------------------------------------------------------------
// The square root the step takes
// ---------------------------------------------------------------------------

// det_sqrt (issue #165) is the root of every length on the step outside the
// model: the contact queries' and this layer's.
void test_contact_sqrt() {
    // The correctly rounded roots, as IEEE-754 bits; the ones marked up are
    // rounded up, so a root that truncated would give one bit less.
    struct Case {
        double x;
        std::uint64_t bits;
    };
    const Case cases[] = {
        {2.0, 0x3FF6A09E667F3BCDULL},                    // up
        {3.0, 0x3FFBB67AE8584CAAULL},
        {5.0, 0x4001E3779B97F4A8ULL},                    // up
        {0.5, 0x3FE6A09E667F3BCDULL},                    // up
        {10.0, 0x40094C583ADA5B53ULL},                   // up
        {7.0, 0x40052A7FA9D2F8EAULL},                    // up
        {1e-300, 0x20CA2FE76A3F9475ULL},
        {5e-324, 0x1E60000000000000ULL},                 // the smallest subnormal
        {1e300, 0x5F138D352E5096AFULL},
        {1.7976931348623157e308, 0x5FEFFFFFFFFFFFFFULL}, // the largest double
        {123456789.0, 0x40C5B38E373B76FEULL},
        {0.1, 0x3FD43D136248490FULL},
        {4.0, 0x4000000000000000ULL},
        {1267650600228229401496703205376.0, 0x4310000000000000ULL},  // 2^100
    };
    for (const Case& c : cases) {
        const double r = nfsu::det_sqrt(c.x);
        std::uint64_t bits = 0;
        std::memcpy(&bits, &r, sizeof(bits));
        check(bits == c.bits, "det_sqrt(" + text(c.x) + ") is correctly rounded");
    }
    check(nfsu::det_sqrt(0.0) == 0.0 && !std::signbit(nfsu::det_sqrt(0.0)) &&
              std::signbit(nfsu::det_sqrt(-0.0)),
          "+0 and -0 are their own roots");
    check(std::isinf(nfsu::det_sqrt(HUGE_VAL)), "+infinity is its own root");
    check(std::isnan(nfsu::det_sqrt(-1.0)) && std::isnan(nfsu::det_sqrt(-HUGE_VAL)) &&
              std::isnan(nfsu::det_sqrt(std::nan(""))),
          "a negative number and NaN give NaN");
}

// ---------------------------------------------------------------------------
// The separation search
// ---------------------------------------------------------------------------

void test_separation() {
    // The distances retail tries, in float: 0.005, then a step that grows
    // by 0.01 up to 0.1.
    std::vector<float> totals;
    float step = 0.005F;
    float total = 0.0F;
    for (int k = 0; k < 20; ++k) {
        total = total + step;
        if (step < 0.1F) {
            step = step + 0.01F;
        }
        totals.push_back(total);
    }
    check(totals[0] == 0.005F && totals[9] > 0.49F && totals[9] < 0.51F &&
              totals[19] > 1.54F && totals[19] < 1.56F,
          "the tries run 0.005, 0.02, ... 0.5 at the tenth and 1.55 at the twentieth");
    const float across[3] = {1.0F, 0.0F, 0.0F};
    nfsu::VehicleVec3 move;
    const auto first = [&](double needed) {
        for (float t : totals) {
            if (static_cast<double>(t) >= needed) {
                return t;
            }
        }
        return -1.0F;
    };
    for (double needed : {0.001, 0.005, 0.0051, 0.03, 0.2, 0.5, 1.2}) {
        const bool found = nfsu::vehicle_track_separation(across, needed, move);
        check(found && move.x == static_cast<double>(first(needed)) && move.y == 0.0 &&
                  move.z == 0.0,
              "across a wall, " + text(needed) + " needs the first try that reaches it, got " +
                  text(move.x));
    }
    check(!nfsu::vehicle_track_separation(across, static_cast<double>(totals[19]) + 1e-6,
                                          move),
          "past the twentieth try nothing separates");
    // Up: the vertical part of a move is held to 0.15.
    const float up[3] = {0.0F, 0.0F, 1.0F};
    check(nfsu::vehicle_track_separation(up, 0.12, move) &&
              move.z == static_cast<double>(totals[4]),
          "up, 0.12 needs the fifth try, 0.125, got " + text(move.z));
    check(nfsu::vehicle_track_separation(up, 0.14, move) &&
              move.z == static_cast<double>(0.15F),
          "up, 0.14 is reached by the move held to 0.15, got " + text(move.z));
    check(!nfsu::vehicle_track_separation(up, 0.16, move),
          "up, 0.16 is never reached: every move is held to 0.15");
    const float down[3] = {0.0F, 0.0F, -1.0F};
    check(nfsu::vehicle_track_separation(down, 0.14, move) &&
              move.z == -static_cast<double>(0.15F),
          "down, the hold is -0.15");
    // A slanted normal: its vertical part is held, its horizontal one not.
    const float slant[3] = {0.6F, 0.0F, 0.8F};
    check(nfsu::vehicle_track_separation(slant, 0.1, move) &&
              move.x == static_cast<double>(0.6F * totals[4]) &&
              move.z == static_cast<double>(0.8F * totals[4]),
          "slanted, 0.1 needs the fifth try, 0.125 along the normal");
    check(nfsu::vehicle_track_separation(slant, 0.5, move) &&
              move.z == static_cast<double>(0.15F) && move.x * 0.6 + move.z * 0.8 >= 0.5 &&
              move.x > 0.6,
          "slanted, 0.5 is reached with the vertical held to 0.15, got " + text(move.x));
}

// ---------------------------------------------------------------------------
// The ground
// ---------------------------------------------------------------------------

void test_ground() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    const nfsu::TrackVehicleGround ground(course.world);
    const nfsu::VehicleVec3 down{0.0, 0.0, -1.0};

    nfsu::VehicleTrackCast c = ground.probe({32.0, 48.0, 11.0}, down, 2.0);
    check(c.surface == nfsu::VehicleTrackSurface::road && c.hit.hit && c.hit.distance == 1.0 &&
              c.hit.normal.x == 0.0 && c.hit.normal.y == 0.0 && c.hit.normal.z == 1.0 &&
              c.hit.surface == 0,
          "a column onto the flat road is ground one down, facing up, surface 0");
    // An unrounded start: the distance is measured from it, not from its
    // float rounding.
    c = ground.probe({32.3, 48.7, 11.3}, down, 2.0);
    check(c.hit.hit && std::fabs(c.hit.distance - 1.3) < 2e-6,
          "from 11.3 the road is 1.3 down, got " + text(c.hit.distance));
    // A start exactly on the road is ground at distance 0.
    c = ground.probe({40.0, 40.0, 10.0}, down, 1.0);
    check(c.hit.hit && c.hit.distance == 0.0, "from the road itself the ground is at 0");
    // The ramp: 13 at px 212, its normal (-6, 0, 40) made unit.
    c = ground.probe({212.0, 48.0, 14.0}, down, 2.0);
    const double n = std::sqrt(6.0 * 6.0 + 40.0 * 40.0);
    check(c.hit.hit && std::fabs(c.hit.distance - 1.0) < 1e-5 &&
              std::fabs(c.hit.normal.x + 6.0 / n) < 1e-6 && c.hit.normal.y == 0.0 &&
              std::fabs(c.hit.normal.z - 40.0 / n) < 1e-6,
          "the ramp is ground 1 down at px 212 with its own normal");
    // Seams: every road quad is 32 square; on a shared edge and at a shared
    // corner the ground is found.
    for (const std::array<double, 2>& at : std::vector<std::array<double, 2>>{
             {96.0, 48.0}, {96.0, 32.0}, {128.0, 64.0}, {64.0, 0.5}, {191.99, 48.0}}) {
        c = ground.probe({at[0], at[1], 11.0}, down, 2.0);
        check(c.surface == nfsu::VehicleTrackSurface::road && c.hit.hit &&
                  std::fabs(c.hit.distance - 1.0) < 1e-6,
              "the seam at (" + text(at[0]) + ", " + text(at[1]) + ") is ground");
    }
    // A wall is not ground: a cast that meets the north wall first.
    c = ground.probe({40.0, 95.5, 11.0}, {0.0, 1.0, 0.0}, 1.0);
    check(c.surface == nfsu::VehicleTrackSurface::wall && !c.hit.hit,
          "a cast into the north wall meets a wall and is refused as ground");
    c = ground.probe({40.0, 95.5, 11.0}, {0.0, 0.8, -0.6}, 2.0);
    check(c.surface == nfsu::VehicleTrackSurface::wall && !c.hit.hit,
          "a slanted cast that meets the wall before the road is no ground");
    c = ground.probe({32.0, 48.0, 20.0}, down, 2.0);
    check(c.surface == nfsu::VehicleTrackSurface::clear && !c.hit.hit,
          "high over the road the cast is clear");
    c = ground.probe({-50.0, 48.0, 11.0}, down, 2.0);
    check(c.surface == nfsu::VehicleTrackSurface::outside && !c.hit.hit,
          "west of the proving ground no cell lies under the cast");
    c = ground.probe({120.0, -8.0, 11.0}, down, 2.0);
    check(c.surface == nfsu::VehicleTrackSurface::outside,
          "past the open south edge no cell lies under the cast");
    const double nan = std::nan("");
    for (const nfsu::VehicleTrackCast& refused :
         {ground.probe({nan, 48.0, 11.0}, down, 2.0), ground.probe({32.0, 48.0, 11.0}, down, 0.0),
          ground.probe({32.0, 48.0, 11.0}, down, -1.0),
          ground.probe({32.0, 48.0, 11.0}, {0.0, 0.0, 0.0}, 2.0),
          ground.probe({32.0, 48.0, 11.0}, down, nan),
          ground.probe({32.0, 48.0, 11.0}, down, 5000.0),
          ground.probe({1e39, 48.0, 11.0}, down, 2.0)}) {
        check(refused.surface == nfsu::VehicleTrackSurface::refused && !refused.hit.hit,
              "a malformed or overlong cast is refused");
    }
    nfsu::TrackContactWorld invalid;
    const nfsu::TrackVehicleGround nothing(invalid);
    check(nothing.probe({32.0, 48.0, 11.0}, down, 2.0).surface ==
              nfsu::VehicleTrackSurface::refused,
          "a world that is not valid answers nothing");
    check(!ground.cast({32.0, 48.0, 20.0}, down, 2.0).hit &&
              ground.cast({32.0, 48.0, 11.0}, down, 2.0).hit,
          "cast is probe's hit");
}

// ---------------------------------------------------------------------------
// Road, seams, slope
// ---------------------------------------------------------------------------

void test_road_matches_flat_ground() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    const nfsu::VehicleModel& model = stock_model();
    const nfsu::VehicleTrackScenario rest = scenario("rest");
    for (int body = 0; body < 2; ++body) {
        nfsu::VehicleTrackApproximations approx;
        approx.body_contact = body == 1;
        nfsu::VehicleTrackSession session(model, course, approx);
        check(session.start(course.start), "the session starts at the start");
        nfsu::VehicleState flat;
        check(model.reset(flat, course.start), "the model resets at the start");
        const nfsu::VehicleFlatGround ground(10.0);
        double worst = 0.0;
        std::uint32_t hull_hits = 0;
        bool road = true;
        for (std::uint64_t i = 0; i < rest.script.total_steps(); ++i) {
            nfsu::VehicleTrackTelemetry t;
            nfsu::VehicleTelemetry f;
            check(session.step(rest.script.input_at(i), t), "a rest step runs");
            check(model.step(flat, rest.script.input_at(i), ground, f), "a flat step runs");
            worst = std::max(worst, std::fabs(t.position.z - f.position.z));
            worst = std::max(worst, std::fabs(t.position.x - f.position.x));
            worst = std::max(worst, std::fabs(t.position.y - f.position.y));
            hull_hits += t.hull_hits;
            for (nfsu::VehicleTrackSurface s : t.wheel_surface) {
                road = road && s == nfsu::VehicleTrackSurface::road;
            }
        }
        check(road, "every wheel stands on road at rest");
        check(worst < 1e-6, "at rest the track follows the flat ground within 1e-6, off by " +
                                text(worst));
        check(hull_hits == 0, "at rest the hull meets nothing");
    }
}

void test_seams() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    const std::vector<nfsu::VehicleTrackTelemetry> r = run(course, scenario("drive"));
    bool road = true;
    double low = 1e9;
    double high = -1e9;
    std::uint32_t hull_hits = 0;
    for (std::size_t i = 60; i < r.size(); ++i) {
        for (nfsu::VehicleTrackSurface s : r[i].wheel_surface) {
            road = road && s == nfsu::VehicleTrackSurface::road;
        }
        low = std::min(low, r[i].position.z);
        high = std::max(high, r[i].position.z);
        hull_hits += r[i].hull_hits;
    }
    check(road, "driving across the seams every wheel finds road on every step");
    check(high - low < 0.002, "across the seams the body rises and falls less than 2 mm, got " +
                                  text(high - low));
    check(hull_hits == 0, "across the seams the hull meets nothing");
    check(!r.empty() && r.front().position.x < 96.0 && r.back().position.x > 128.0,
          "the drive crosses the seams at px 96 and 128");
}

void test_slope() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    const std::vector<nfsu::VehicleTrackTelemetry> climb = run(course, scenario("slope"));
    bool on_ramp = false;
    for (const nfsu::VehicleTrackTelemetry& t : climb) {
        if (t.position.x > 200.0 && t.position.x < 225.0) {
            const double ground = 10.0 + (t.position.x - 192.0) * 6.0 / 40.0;
            on_ramp = true;
            check(t.position.z > ground + 0.3 && t.position.z < ground + 0.7,
                  "on the ramp the car rides half a metre over it, at px " +
                      text(t.position.x) + " z " + text(t.position.z));
        }
    }
    check(on_ramp, "the climb crosses the ramp");
    check(!climb.empty() && climb.back().position.x > 232.0 && climb.back().position.z > 16.0,
          "the climb reaches the plateau");
    // Held on the ramp by the brake.
    nfsu::VehicleTrackScenario hold;
    hold.name = "hold";
    hold.forward = 180.0;
    nfsu::VehicleScriptSegment braked;
    braked.steps = 240;
    braked.input.brake = 1.0;
    hold.script.segments.push_back(braked);
    nfsu::VehiclePose pose = nfsu::vehicle_track_scenario_pose(course.start, hold);
    pose.ground.z = 13.0;
    nfsu::VehicleTrackSession session(stock_model(), course);
    check(session.start(pose), "the car starts on the ramp");
    nfsu::VehicleTrackTelemetry t;
    for (int i = 0; i < 240; ++i) {
        check(session.step(hold.script.input_at(static_cast<std::uint64_t>(i)), t),
              "a braked step on the ramp runs");
    }
    // The model's tyre is a damper at a standstill (docs/VEHICLE.md), so the
    // brake holds the car to a creep down the slope, not to rest.
    check(std::fabs(t.position.x - 212.0) < 0.25 && speed(t.velocity) < 0.1,
          "the brake holds the car on the 8.5-degree ramp to a creep, at px " +
              text(t.position.x) + " moving " + text(speed(t.velocity)));
    check(t.model.wheels[0].contact && t.model.wheels[3].contact, "on the ramp the wheels touch");
}

// ---------------------------------------------------------------------------
// Walls
// ---------------------------------------------------------------------------

struct WallCase {
    const char* name;
    // The wall's line and the side the car starts on: a point on the line,
    // a unit normal toward the car.
    double px;
    double py;
    double nx;
    double ny;
};

void test_walls() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    const double oblique = std::sqrt(28.0 * 28.0 + 48.0 * 48.0);
    const WallCase cases[] = {
        {"wall", 0.0, 96.0, 0.0, -1.0},
        {"wall-glance", 0.0, 96.0, 0.0, -1.0},
        {"wall-fast", 0.0, 96.0, 0.0, -1.0},
        {"wall-square", 0.0, 96.0, 0.0, -1.0},
        {"wall-oblique", 112.0, 8.0, -28.0 / oblique, 48.0 / oblique},
    };
    for (const WallCase& w : cases) {
        const std::vector<nfsu::VehicleTrackTelemetry> r = run(course, scenario(w.name));
        double deepest = 0.0;
        double closest = 1e9;
        std::uint32_t contacts = 0;
        std::uint32_t impulses = 0;
        for (std::size_t i = 0; i < r.size(); ++i) {
            for (const nfsu::VehicleVec3& p : hull_in_world(r[i])) {
                const double side = (p.x - w.px) * w.nx + (p.y - w.py) * w.ny;
                deepest = std::max(deepest, -side);
            }
            closest = std::min(closest, (r[i].position.x - w.px) * w.nx +
                                            (r[i].position.y - w.py) * w.ny);
            contacts += r[i].contacts;
            impulses += r[i].impulses;
        }
        check(contacts > 0 && impulses > 0, std::string(w.name) + ": the hull meets the wall");
        check(deepest < 0.12, std::string(w.name) +
                                  ": no hull point ends a step more than 0.12 beyond the wall, "
                                  "the deepest " + text(deepest));
        check(closest > 0.7, std::string(w.name) + ": the centre of mass stays 0.7 from it, " +
                                 text(closest));
        for (const nfsu::VehicleTrackTelemetry& t : r) {
            if (t.contacts > 0) {
                check(t.deepest_surface == nfsu::VehicleTrackSurface::wall,
                      std::string(w.name) + ": the contact is with a wall");
                break;
            }
        }
    }
    // Square on, the car ends against the wall and at rest.
    const std::vector<nfsu::VehicleTrackTelemetry> square =
        run(course, scenario("wall-square"));
    check(!square.empty() && speed(square.back().velocity) < 1.0 &&
              square.back().position.y > 93.0,
          "square on, the car ends at rest against the wall");
    // Above 30 m/s an upright car slides along the wall: the wall costs it
    // speed and turns it along the wall without spinning it.
    {
        const std::vector<nfsu::VehicleTrackTelemetry> r = run(course, scenario("wall-fast"));
        double turn = 0.0;
        for (const nfsu::VehicleTrackTelemetry& t : r) {
            turn = std::max(turn, std::fabs(t.angular_velocity.z));
        }
        const Axes a = axes(r.back().orientation);
        check(!r.empty() && r.back().velocity.x > 25.0 && std::fabs(a.x.y) < 0.2 &&
                  turn < 2.0,
              "at 35 m/s the car slides on along the wall, turning at most 2 rad/s, got " +
                  text(turn));
    }
    // At 15 and 45 degrees the car turns along the wall and keeps going.
    for (const char* name : {"wall", "wall-glance"}) {
        const std::vector<nfsu::VehicleTrackTelemetry> r = run(course, scenario(name));
        const Axes a = axes(r.back().orientation);
        check(!r.empty() && std::fabs(a.x.y) < 0.2 && r.back().velocity.x > 2.0,
              std::string(name) + ": the car ends along the wall, moving along it");
    }
}

// The friction impulse against a wall, from a car in the air - no tyre
// force - 20 over the road, its side 0.01 into the north wall, closing at
// 0.05 m/s and sliding along it at 0.75: mu j, scaled by the slide below
// 1 m/s (0x0046C0E4..0x0046C0FB, [0x007000A8]), so the friction impulse is
// mu times the slide times the normal one. mu is 0.1 for a car whose up
// axis's vertical part is above 0.7 ([0x006CCA04]) and 0.3 otherwise
// (0x0046C00C..0x0046C0A3). Returns that ratio, and the step's telemetry.
double wall_friction_ratio(double roll_degrees, nfsu::VehicleTrackTelemetry& t) {
    const nfsu::VehicleTrackCourse course = proving_ground();
    nfsu::VehicleTrackSession session(stock_model(), course);
    check(session.start(course.start), "the car starts");
    nfsu::VehicleState s = session.state();
    const double half = roll_degrees * 3.14159265358979323846 / 360.0;
    s.orientation = nfsu::VehicleQuat{std::cos(half), std::sin(half), 0.0, 0.0};
    s.angular_velocity = {0.0, 0.0, 0.0};
    s.velocity = {0.75, 0.05, 0.0};
    // How far the hull reaches along +y from the centre of mass.
    nfsu::VehicleTrackTelemetry frame;
    frame.orientation = s.orientation;
    double reach = -1e9;
    for (const nfsu::VehicleVec3& p : hull_in_world(frame)) {
        reach = std::max(reach, p.y);
    }
    s.position = {48.0, 96.01 - reach - 0.05 / 120.0, 20.0};
    check(session.start(s), "the car starts in the air by the north wall");
    check(session.step(nfsu::VehicleInput{}, t), "the step runs");
    check(t.contacts == 1 && t.impulses == 1 &&
              t.deepest_surface == nfsu::VehicleTrackSurface::wall,
          "the car's side meets the north wall once, contacts " + std::to_string(t.contacts));
    const double normal = std::fabs(t.impulse.y);
    const double along = std::sqrt(t.impulse.x * t.impulse.x + t.impulse.z * t.impulse.z);
    return normal > 0.0 ? along / normal : 0.0;
}

void test_wall_friction() {
    nfsu::VehicleTrackTelemetry t;
    // Upright, sliding at 0.754 (0.75 along x, 0.082 down): 0.1 x 0.754.
    const double upright = wall_friction_ratio(0.0, t);
    check(upright > 0.07 && upright < 0.08 && t.impulse.x < 0.0,
          "upright, the friction is 0.1 of the normal impulse scaled by a slide under 1 m/s, "
          "got " + text(upright));
    // Rolled 30 degrees, the up axis's vertical part 0.866: still upright.
    const double rolled = wall_friction_ratio(30.0, t);
    check(rolled > 0.07 && rolled < 0.08,
          "rolled 30 degrees, up.z 0.866 is above 0.7 and the car still slides with 0.1, got " +
              text(rolled));
    // Rolled 50 degrees, 0.643: 0.3.
    const double tilted = wall_friction_ratio(50.0, t);
    check(tilted > 0.21 && tilted < 0.24,
          "rolled 50 degrees, up.z 0.643 is below 0.7 and the car slides with 0.3, got " +
              text(tilted));
}

// A point just inside a wall on a car already leaving it: the separation
// moves it out and no impulse is given (0x0046BD89..0x0046BD98), where the
// smallest-impulse rule would otherwise push it away at 0.3 m/s.
void test_separating_contact() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    nfsu::VehicleTrackSession session(stock_model(), course);
    nfsu::VehiclePose pose;
    // Facing +y with the nose, 2.1166 ahead of the centre of mass, 0.01
    // into the north wall at pz 96.
    pose.ground = {48.0, 96.0 - 2.1166 + 0.01, 10.0};
    pose.heading = nfsu::vehicle_track_heading(0x4000);
    check(session.start(pose), "the car starts with its nose in the wall");
    nfsu::VehicleState leaving = session.state();
    leaving.velocity.y = -0.05;
    check(session.start(leaving), "the car starts leaving the wall at 0.05 m/s");
    nfsu::VehicleTrackTelemetry t;
    check(session.step(nfsu::VehicleInput{}, t), "a step runs");
    check(t.contacts == 1 && t.deepest_surface == nfsu::VehicleTrackSurface::wall &&
              t.impulses == 0 && t.separation.y < -0.004 && t.impulse.y == 0.0,
          "the point is moved out and no impulse is given, impulses " +
              std::to_string(t.impulses) + " separation " + text(t.separation.y));
    check(t.velocity.y > -0.06 && t.velocity.y < -0.04,
          "the car leaves at its own speed, " + text(t.velocity.y));
}

// ---------------------------------------------------------------------------
// Airborne and landing
// ---------------------------------------------------------------------------

void test_drop() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    const std::vector<nfsu::VehicleTrackTelemetry> r = run(course, scenario("drop"));
    bool airborne = false;
    std::size_t landed = 0;
    double lowest = 1e9;
    for (std::size_t i = 0; i < r.size(); ++i) {
        bool all_clear = true;
        bool all_road = true;
        for (nfsu::VehicleTrackSurface s : r[i].wheel_surface) {
            all_clear = all_clear && s == nfsu::VehicleTrackSurface::clear;
            all_road = all_road && s == nfsu::VehicleTrackSurface::road;
        }
        if (i < 60) {
            airborne = airborne || all_clear;
        }
        if (landed == 0 && all_road && i > 60) {
            landed = i;
        }
        for (const nfsu::VehicleVec3& p : hull_in_world(r[i])) {
            lowest = std::min(lowest, p.z);
        }
    }
    check(airborne, "dropped, the wheels are over clear air");
    check(landed != 0, "the car lands on the road");
    check(lowest > 9.95, "no hull point goes 5 cm into the road, the lowest " + text(lowest));
    check(!r.empty() && std::fabs(r.back().position.z - 10.4999) < 0.005 &&
              speed(r.back().velocity) < 0.05,
          "three seconds after the drop the car rests on its struts");
}

// 0x00584F60 holds the impulse to 1000 t m/s and its moment to 10 t m^2/s.
// No proving-ground scenario reaches either, and a fall onto the
// underside's lowest point reaches the first only at about 1850 m/s. A car
// falling at 4000 m/s, put where its step ends with the centre of mass 10.1
// over the road - its underside 0.1 into it - meets it once; the step's drop
// is measured first, 200 m up.
void test_impulse_clamps() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    nfsu::VehicleTrackSession session(stock_model(), course);
    nfsu::VehiclePose pose;
    pose.ground = {48.0, 48.0, 10.0};
    check(session.start(pose), "the car starts in the middle of a square");
    nfsu::VehicleState falling = session.state();
    falling.position.z = 200.0;
    falling.velocity = {0.0, 0.0, -4000.0};
    nfsu::VehicleTrackTelemetry t;
    check(session.start(falling) && session.step(nfsu::VehicleInput{}, t) && t.contacts == 0,
          "high up, the step meets nothing");
    falling.position.z = 10.1 + (200.0 - t.position.z);
    check(session.start(falling), "the car starts falling at 4000 m/s");
    check(session.step(nfsu::VehicleInput{}, t), "the step runs");
    const double unit = nfsu::VehicleApproximations{}.unit_scale;
    const double size = speed(t.impulse);
    check(t.contacts == 1 && t.impulses == 1 &&
              t.deepest_surface == nfsu::VehicleTrackSurface::road,
          "the underside meets the road once, contacts " + std::to_string(t.contacts));
    check(std::fabs(size - 1000.0 * unit) <= 1e-9 * 1000.0 * unit,
          "the impulse is held to 1000 t m/s, got " + text(size));
    check(speed(t.angular_velocity) < 20.0,
          "its moment is held, the car turning at " + text(speed(t.angular_velocity)) +
              " rad/s");
}

// ---------------------------------------------------------------------------
// Resets
// ---------------------------------------------------------------------------

void test_out_of_bounds() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    const std::vector<nfsu::VehicleTrackTelemetry> r = run(course, scenario("leave"));
    std::size_t none = 0;
    std::size_t reset = 0;
    for (std::size_t i = 0; i < r.size(); ++i) {
        const nfsu::VehicleTrackTelemetry& t = r[i];
        const bool check_tick = t.tick && (t.step / 2 + 7) % 32 == 0;
        check(check_tick == (t.bounds != nfsu::VehicleTrackBoundsCheck::not_run),
              "the bounds check runs on exactly every 32nd tick, step " +
                  std::to_string(t.step));
        if (none == 0 && t.bounds == nfsu::VehicleTrackBoundsCheck::none) {
            none = i;
        }
        if (reset == 0 && t.reset != nfsu::VehicleTrackReset::none) {
            reset = i;
        }
        if (none == 0 && t.bounds == nfsu::VehicleTrackBoundsCheck::ground) {
            check(t.position.y > -0.5, "on the course the check finds ground");
        }
    }
    check(none != 0 && r[none].position.y < 0.0 &&
              r[none].pending == nfsu::VehicleTrackReset::out_of_bounds,
          "off the open edge the check finds no ground and asks for a reset");
    check(reset == none + 2 && r[reset].reset == nfsu::VehicleTrackReset::out_of_bounds &&
              r[reset].resets == 1 && r[reset].pending == nfsu::VehicleTrackReset::none,
          "the reset is performed on the next tick, step " + std::to_string(reset + 1));
    if (reset != 0) {
        const nfsu::VehicleTrackTelemetry& t = r[reset];
        // 0x00427CD2..0x00427D10: at speed 0 plus 0.1 along its forward
        // axis, which faces +x there, 4 degrees off (0x02F4).
        check(t.position.x == 176.0 && t.position.y == 40.0 &&
                  std::fabs(speed(t.velocity) - 0.1) < 1e-8 && t.velocity.x > 0.099,
              "the car is put on node 10, (176, 40), moving 0.1, got " + text(t.position.x) +
                  " " + text(t.position.y) + " at " + text(speed(t.velocity)));
        check(t.position.z > 10.4 && t.position.z < 10.6,
              "on the road's height under the node");
    }
}

// 0x00426B32..0x00426B57: with no polygon under the car the check tries one
// unit along +x before it asks for a reset. A car resting across a gap a
// metre wide, its centre of mass over the gap, is not out of bounds.
void test_bounds_side_probe() {
    nfsu::VehicleTrackLayout layout;
    layout.name = "gap";
    const float xs[2][2] = {{0.0F, 31.5F}, {32.5F, 64.0F}};
    for (const auto& span : xs) {
        nfsu::VehicleTrackQuad q;
        const float c[4][3] = {{span[0], 0.0F, 10.0F}, {span[1], 0.0F, 10.0F},
                               {span[1], 64.0F, 10.0F}, {span[0], 64.0F, 10.0F}};
        std::memcpy(q.corner, c, sizeof(c));
        layout.quads.push_back(q);
    }
    layout.grid[0] = 8.0F;
    layout.grid[1] = 32.0F;
    layout.grid[2] = 10.0F;
    nfsu::VehicleTrackCourse course;
    std::string error;
    const bool built = nfsu::build_vehicle_track_course(layout, course, error);
    check(built, "the course with a gap builds: " + error);
    nfsu::VehicleTrackSession session(stock_model(), course);
    nfsu::VehiclePose pose;
    pose.ground = {32.0, 32.0, 10.0};
    pose.heading = nfsu::vehicle_track_heading(0x4000);
    check(session.start(pose), "the car starts across the gap");
    nfsu::VehicleTrackTelemetry t;
    nfsu::VehicleTrackBoundsCheck checked = nfsu::VehicleTrackBoundsCheck::not_run;
    for (int i = 0; i < 60; ++i) {
        check(session.step(nfsu::VehicleInput{}, t), "a step across the gap runs");
        if (t.bounds != nfsu::VehicleTrackBoundsCheck::not_run) {
            checked = t.bounds;
        }
    }
    const nfsu::TrackVehicleGround ground(course.world);
    const nfsu::VehicleTrackCast under =
        ground.probe({t.position.x, t.position.y, 12.0}, {0.0, 0.0, -1.0}, 4.0);
    check(under.surface == nfsu::VehicleTrackSurface::clear,
          "nothing lies under the car's centre of mass at " + text(t.position.x) + " " +
              text(t.position.y) + " " + text(t.position.z) + ", surface " +
              std::to_string(static_cast<int>(under.surface)));
    check(checked == nfsu::VehicleTrackBoundsCheck::ground &&
              t.pending == nfsu::VehicleTrackReset::none && t.resets == 0,
          "one unit along +x the check finds the road, and nothing is reset");
}

// Road one unit along -x only: the check looks along +x alone
// (0x00426B32..0x00426B3A adds [0x006CC7BC] to x), so a car over a hole that
// runs 2 m east of it and ends 0.5 m west of it is out of bounds.
void test_bounds_side_probe_east_only() {
    nfsu::VehicleTrackLayout layout;
    layout.name = "hole";
    const float spans[4][4] = {{0.0F, 64.0F, 0.0F, 31.5F},
                               {0.0F, 64.0F, 32.5F, 64.0F},
                               {0.0F, 31.5F, 31.5F, 32.5F},
                               {34.0F, 64.0F, 31.5F, 32.5F}};
    for (const auto& s : spans) {
        nfsu::VehicleTrackQuad q;
        const float c[4][3] = {{s[0], s[2], 10.0F}, {s[1], s[2], 10.0F},
                               {s[1], s[3], 10.0F}, {s[0], s[3], 10.0F}};
        std::memcpy(q.corner, c, sizeof(c));
        layout.quads.push_back(q);
    }
    layout.grid[0] = 8.0F;
    layout.grid[1] = 8.0F;
    layout.grid[2] = 10.0F;
    nfsu::VehicleTrackCourse course;
    std::string error;
    const bool built = nfsu::build_vehicle_track_course(layout, course, error);
    check(built, "the course with a hole builds: " + error);
    nfsu::VehicleTrackSession session(stock_model(), course);
    nfsu::VehiclePose pose;
    pose.ground = {32.0, 32.0, 10.0};
    pose.heading = nfsu::vehicle_track_heading(0x4000);
    check(session.start(pose), "the car starts over the hole");
    nfsu::VehicleTrackTelemetry t;
    nfsu::VehicleTrackBoundsCheck checked = nfsu::VehicleTrackBoundsCheck::not_run;
    nfsu::VehicleTrackReset asked = nfsu::VehicleTrackReset::none;
    for (int i = 0; i < 60; ++i) {
        check(session.step(nfsu::VehicleInput{}, t), "a step over the hole runs");
        if (t.bounds != nfsu::VehicleTrackBoundsCheck::not_run) {
            checked = t.bounds;
            asked = t.pending;
        }
    }
    const nfsu::TrackVehicleGround ground(course.world);
    const auto surface = [&](double x) {
        return ground.probe({x, t.position.y, 12.0}, {0.0, 0.0, -1.0}, 4.0).surface;
    };
    check(surface(t.position.x) == nfsu::VehicleTrackSurface::clear &&
              surface(t.position.x + 1.0) == nfsu::VehicleTrackSurface::clear &&
              surface(t.position.x - 1.0) == nfsu::VehicleTrackSurface::road,
          "nothing lies under the car nor one unit east, and road lies one unit west, at " +
              text(t.position.x) + " " + text(t.position.y));
    check(checked == nfsu::VehicleTrackBoundsCheck::none &&
              asked == nfsu::VehicleTrackReset::out_of_bounds,
          "the check does not look west: the car is out of bounds");
}

void test_zones_and_off_path() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    // RESET: asked for on the tick the zone is entered, performed on the
    // next, onto the nearest node facing along the path there.
    const std::vector<nfsu::VehicleTrackTelemetry> zone = run(course, scenario("zone"));
    std::size_t asked = 0;
    std::size_t done = 0;
    for (std::size_t i = 0; i < zone.size(); ++i) {
        if (asked == 0 && zone[i].pending == nfsu::VehicleTrackReset::zone) {
            asked = i;
        }
        if (done == 0 && zone[i].reset != nfsu::VehicleTrackReset::none) {
            done = i;
        }
    }
    check(asked != 0 && zone[asked].zone == 0 && zone[asked].position.y >= 80.0,
          "entering the RESET zone asks for a reset");
    check(done == asked + 2 && zone[done].reset == nfsu::VehicleTrackReset::zone &&
              zone[done].position.x == 128.0 && zone[done].position.y == 72.0,
          "the next tick puts the car on node 15, (128, 72)");
    if (done != 0) {
        const Axes a = axes(zone[done].orientation);
        check(a.x.x < -0.999, "facing -x, the path's direction on the north side");
    }
    // RESET_TO_POINT: performed on the same tick, at the zone's point, facing
    // its direction, at 8.
    const std::vector<nfsu::VehicleTrackTelemetry> point = run(course, scenario("zone-point"));
    std::size_t moved = 0;
    for (std::size_t i = 0; i < point.size(); ++i) {
        if (point[i].reset != nfsu::VehicleTrackReset::none) {
            moved = i;
            break;
        }
    }
    check(moved != 0 && point[moved].reset == nfsu::VehicleTrackReset::zone_to_point &&
              point[moved].position.x == 40.0 && point[moved].position.y == 56.0 &&
              std::fabs(speed(point[moved].velocity) - 8.1) < 1e-8 &&
              point[moved].velocity.x > 8.09 && std::fabs(point[moved].velocity.y) < 1e-9,
          "the RESET_TO_POINT zone puts the car at (40, 56) moving +x at 8 plus 0.1");
    check(moved != 0 && point[moved - 1].position.y > 79.0 && point[moved - 1].position.y < 81.0,
          "the car was at the zone's edge the step before");
    // Off the path: more than 5000 from the tracked node on the first tick,
    // the reset on the second.
    const std::vector<nfsu::VehicleTrackTelemetry> far = run(course, scenario("far"));
    check(far.size() > 4 && far[1].pending == nfsu::VehicleTrackReset::off_path &&
              far[1].off_path && far[3].reset == nfsu::VehicleTrackReset::off_path &&
              far[3].position.x == 16.0 && far[3].position.y == 40.0,
          "5000 from the route the car is off the path and is reset onto node 0");
}

// A small course: a road slab and, when asked, a deck over part of it.
nfsu::VehicleTrackCourse deck_course(float deck, bool with_route, float half = 8.0F) {
    nfsu::VehicleTrackLayout layout;
    layout.name = "deck";
    for (float x = 0.0F; x < 64.0F; x += 32.0F) {
        for (float z = 0.0F; z < 64.0F; z += 32.0F) {
            nfsu::VehicleTrackQuad q;
            const float c[4][3] = {{x, z, 10.0F}, {x + 32.0F, z, 10.0F},
                                   {x + 32.0F, z + 32.0F, 10.0F}, {x, z + 32.0F, 10.0F}};
            std::memcpy(q.corner, c, sizeof(c));
            layout.quads.push_back(q);
        }
    }
    if (deck > 0.0F) {
        nfsu::VehicleTrackQuad q;
        const float lo = 32.0F - half;
        const float hi = 32.0F + half;
        const float c[4][3] = {{lo, lo, deck}, {hi, lo, deck}, {hi, hi, deck}, {lo, hi, deck}};
        std::memcpy(q.corner, c, sizeof(c));
        layout.quads.push_back(q);
    }
    if (with_route) {
        for (float x = 8.0F; x < 56.0F; x += 16.0F) {
            layout.loop.push_back({x, 8.0F});
        }
        for (float y = 8.0F; y < 56.0F; y += 16.0F) {
            layout.loop.push_back({56.0F, y});
        }
        for (float x = 56.0F; x > 8.0F; x -= 16.0F) {
            layout.loop.push_back({x, 56.0F});
        }
        for (float y = 56.0F; y > 8.0F; y -= 16.0F) {
            layout.loop.push_back({8.0F, y});
        }
    }
    layout.grid[0] = 8.0F;
    layout.grid[1] = 8.0F;
    layout.grid[2] = 10.0F;
    nfsu::VehicleTrackCourse course;
    std::string error;
    const bool built = nfsu::build_vehicle_track_course(layout, course, error);
    check(built, "a small course builds: " + error);
    return course;
}

void test_sunk() {
    for (float deck : {11.0F, 11.625F}) {
        const nfsu::VehicleTrackCourse course = deck_course(deck, true);
        nfsu::VehicleTrackSession session(stock_model(), course);
        nfsu::VehiclePose pose;
        pose.ground = {32.0, 32.0, 10.0};
        check(session.start(pose), "the car starts under the deck");
        nfsu::VehicleTrackTelemetry first;
        nfsu::VehicleTrackTelemetry second;
        check(session.step(nfsu::VehicleInput{}, first) &&
                  session.step(nfsu::VehicleInput{}, second),
              "two steps under the deck run");
        if (deck == 11.0F) {
            check(first.model.wheels[0].contact && first.model.wheels[1].contact &&
                      first.model.wheels[2].contact && first.model.wheels[3].contact,
                  "under the deck all four wheels are down");
            check(second.tick && second.reset == nfsu::VehicleTrackReset::sunk &&
                      session.last_reset().performed,
                  "a deck 0.5 over the centre of mass with four wheels down resets on the "
                  "first tick");
        } else {
            check(second.reset == nfsu::VehicleTrackReset::none,
                  "a deck 1.125 over the centre of mass is beyond the query's reach");
        }
    }
}

// The reset skips a node with no ground under it (0x00431701..0x00431743):
// a course with a hole where the nearest nodes are.
// The margin: a surface more than 0.25 over the position ([0x006CC90C]).
// A deck a metre square over the centre of mass only, clear of the wheels,
// with the body's collision off so that nothing touches it: 0.2 over the
// centre of mass the car stays, 0.3 over it it is reset. The heights are on
// the collision's 1/256 grid.
void test_sunk_margin() {
    for (float deck : {10.703125F, 10.80078125F}) {
        const nfsu::VehicleTrackCourse course = deck_course(deck, true, 0.5F);
        nfsu::VehicleTrackApproximations approx;
        approx.body_contact = false;
        nfsu::VehicleTrackSession session(stock_model(), course, approx);
        nfsu::VehiclePose pose;
        pose.ground = {32.0, 32.0, 10.0};
        check(session.start(pose), "the car starts under the small deck");
        nfsu::VehicleTrackTelemetry first;
        nfsu::VehicleTrackTelemetry second;
        check(session.step(nfsu::VehicleInput{}, first) &&
                  session.step(nfsu::VehicleInput{}, second),
              "two steps under the small deck run");
        const double over = static_cast<double>(deck) - first.position.z;
        check(first.model.wheels[0].contact && first.model.wheels[1].contact &&
                  first.model.wheels[2].contact && first.model.wheels[3].contact &&
                  std::fabs(first.position.x - 32.0) < 0.4 &&
                  std::fabs(first.position.y - 32.0) < 0.4,
              "all four wheels are down and the centre of mass is under the deck");
        if (deck < 10.75F) {
            check(over > 0.18 && over < 0.23 && second.tick &&
                      second.reset == nfsu::VehicleTrackReset::none,
                  "a deck 0.2 over the centre of mass is within the margin, got " + text(over));
        } else {
            check(over > 0.28 && over < 0.33 && second.tick &&
                      second.reset == nfsu::VehicleTrackReset::sunk,
                  "a deck 0.3 over the centre of mass resets the car, got " + text(over));
        }
    }
}

// The same deck over a car in the air, a metre over the road with no wheel
// down: not sunk (0x0042A4DB needs the car's +0x3CC to be 4).
void test_sunk_needs_wheels() {
    const nfsu::VehicleTrackCourse course = deck_course(12.0F, true);
    nfsu::VehicleTrackSession session(stock_model(), course);
    nfsu::VehiclePose pose;
    pose.ground = {32.0, 32.0, 11.0};
    check(session.start(pose), "the car starts under the deck, a metre over the road");
    nfsu::VehicleTrackTelemetry first;
    nfsu::VehicleTrackTelemetry second;
    check(session.step(nfsu::VehicleInput{}, first) &&
              session.step(nfsu::VehicleInput{}, second),
          "two steps in the air run");
    check(!first.tick && !first.model.wheels[0].contact && !first.model.wheels[1].contact &&
              !first.model.wheels[2].contact && !first.model.wheels[3].contact &&
              first.position.z + 0.25 < 12.0 && first.position.z + 1.0 > 12.0,
          "no wheel is down and the deck is within the query's reach");
    check(second.tick && second.reset == nfsu::VehicleTrackReset::none,
          "a car with no wheel down is not sunk");
}

void test_reset_skips_holes() {
    nfsu::VehicleTrackLayout layout;
    layout.name = "hole";
    for (float x = 0.0F; x < 64.0F; x += 32.0F) {
        for (float z = 0.0F; z < 64.0F; z += 32.0F) {
            if (x == 32.0F && z == 0.0F) {
                continue;
            }
            nfsu::VehicleTrackQuad q;
            const float c[4][3] = {{x, z, 10.0F}, {x + 32.0F, z, 10.0F},
                                   {x + 32.0F, z + 32.0F, 10.0F}, {x, z + 32.0F, 10.0F}};
            std::memcpy(q.corner, c, sizeof(c));
            layout.quads.push_back(q);
        }
    }
    for (float x = 8.0F; x < 56.0F; x += 16.0F) {
        layout.loop.push_back({x, 8.0F});
    }
    for (float y = 8.0F; y < 56.0F; y += 16.0F) {
        layout.loop.push_back({56.0F, y});
    }
    for (float x = 56.0F; x > 8.0F; x -= 16.0F) {
        layout.loop.push_back({x, 56.0F});
    }
    for (float y = 56.0F; y > 8.0F; y -= 16.0F) {
        layout.loop.push_back({8.0F, y});
    }
    layout.grid[0] = 8.0F;
    layout.grid[1] = 40.0F;
    layout.grid[2] = 10.0F;
    nfsu::VehicleTrackCourse course;
    std::string error;
    const bool built = nfsu::build_vehicle_track_course(layout, course, error);
    check(built, "the course with a hole builds: " + error);
    nfsu::VehicleTrackSession session(stock_model(), course);
    nfsu::VehiclePose pose;
    pose.ground = {48.0, 16.0, 11.0};
    check(session.start(pose), "the car starts over the hole");
    nfsu::VehicleTrackTelemetry t;
    nfsu::VehicleTrackReset reason = nfsu::VehicleTrackReset::none;
    for (int i = 0; i < 120 && reason == nfsu::VehicleTrackReset::none; ++i) {
        check(session.step(nfsu::VehicleInput{}, t), "a step over the hole runs");
        reason = t.reset;
    }
    // (40, 8), (56, 8) and (56, 24) lie over the hole; (24, 8) and (56, 40)
    // are the nearest with ground, 25.3 away, and the first in node order
    // wins.
    check(reason == nfsu::VehicleTrackReset::out_of_bounds && session.last_reset().performed &&
              session.last_reset().node == 1 && t.position.x == 24.0 && t.position.y == 8.0,
          "over the hole the car is reset onto (24, 8), the nearest node with ground, got "
          "node " + std::to_string(session.last_reset().node));
    // The jump from over the hole to the node points against the path, west
    // where it runs east; it is not a drive, and nothing reads it as one.
    bool wrong_way = false;
    for (int i = 0; i < 60; ++i) {
        check(session.step(nfsu::VehicleInput{}, t), "a step after the reset runs");
        for (const nfsu::RaceProgressEvent& event : session.events()) {
            wrong_way = wrong_way || event.kind == nfsu::RaceEventKind::wrong_way_on;
        }
    }
    check(!wrong_way && !session.progress().wrong_way,
          "the reset is not read as a drive the wrong way");
}

void test_manual_and_approximations() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    // The reset button: asked for between ticks, performed on the next.
    {
        nfsu::VehicleTrackSession session(stock_model(), course);
        check(session.start(course.start), "the car starts");
        nfsu::VehicleTrackTelemetry t;
        for (int i = 0; i < 11; ++i) {
            session.step(nfsu::VehicleInput{}, t);
        }
        session.request_reset();
        session.request_reset();
        check(session.step(nfsu::VehicleInput{}, t) &&
                  t.reset == nfsu::VehicleTrackReset::manual && t.resets == 1 &&
                  t.step == 12,
              "a requested reset happens on the next tick, once");
        check(t.position.x == 32.0 && t.position.y == 40.0,
              "onto the nearest node with ground, (32, 40)");
    }
    // Upside down: off by default; on, as the first lap runs, a car on its
    // roof is reset.
    const nfsu::VehicleTrackApproximations lap = nfsu::vehicle_track_first_lap_approximations();
    check(lap.body_contact && lap.upside_down_steps == 120 && lap.stuck_steps == 0,
          "the first lap runs with the upside-down reset on after a second, nothing else "
          "changed");
    for (int on = 0; on < 2; ++on) {
        const nfsu::VehicleTrackApproximations approx =
            on == 1 ? lap : nfsu::VehicleTrackApproximations{};
        nfsu::VehicleTrackSession session(stock_model(), course, approx);
        check(session.start(course.start), "the car starts");
        nfsu::VehicleState flipped = session.state();
        flipped.orientation = nfsu::VehicleQuat{0.0, 1.0, 0.0, 0.0};
        flipped.position.z = flipped.position.z + 1.0;
        check(session.start(flipped), "the session starts from a state upside down");
        nfsu::VehicleTrackTelemetry t;
        std::uint32_t roof = 0;
        nfsu::VehicleTrackReset reason = nfsu::VehicleTrackReset::none;
        for (int i = 0; i < 480; ++i) {
            check(session.step(nfsu::VehicleInput{}, t), "an upside-down step runs");
            if (t.contacts > 0 && t.deepest_surface == nfsu::VehicleTrackSurface::road) {
                ++roof;
            }
            if (t.reset != nfsu::VehicleTrackReset::none) {
                reason = t.reset;
            }
        }
        check(roof > 0, "upside down, the roof meets the road");
        if (on == 1) {
            check(reason == nfsu::VehicleTrackReset::upside_down,
                  "upside down for a second, the car is reset");
        } else {
            check(reason == nfsu::VehicleTrackReset::none,
                  "with the approximation off, nothing resets a car on its roof");
        }
    }
    // Stuck: off by default; on, full throttle against a wall resets.
    for (int on = 0; on < 2; ++on) {
        nfsu::VehicleTrackApproximations approx;
        approx.stuck_steps = on == 1 ? 120 : 0;
        const std::vector<nfsu::VehicleTrackTelemetry> r =
            run(course, scenario("wall-square"), approx);
        nfsu::VehicleTrackReset reason = nfsu::VehicleTrackReset::none;
        for (const nfsu::VehicleTrackTelemetry& t : r) {
            if (t.reset != nfsu::VehicleTrackReset::none) {
                reason = t.reset;
            }
        }
        check(reason == (on == 1 ? nfsu::VehicleTrackReset::stuck
                                 : nfsu::VehicleTrackReset::none),
              on == 1 ? "held against the wall at full throttle, the car is reset as stuck"
                      : "with the approximation off, nothing resets a stuck car");
    }
}

// ---------------------------------------------------------------------------
// Refusals and invalid geometry
// ---------------------------------------------------------------------------

void test_refusals() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    nfsu::VehicleTrackCourse empty;
    {
        nfsu::VehicleTrackSession session(stock_model(), empty);
        nfsu::VehicleTrackTelemetry t;
        check(!session.valid() && !session.start(course.start) &&
                  !session.step(nfsu::VehicleInput{}, t),
              "a course whose collision is not valid is refused");
    }
    {
        nfsu::VehicleSpec spec = nfsu::stock_240sx_vehicle_spec();
        spec.handling.mass = 0.0F;
        const nfsu::VehicleModel broken(spec);
        nfsu::VehicleTrackSession session(broken, course);
        check(!session.valid(), "a model that is not valid is refused");
    }
    {
        nfsu::VehicleHull hull;
        nfsu::VehicleTrackSession none(stock_model(), course, {}, hull);
        check(!none.valid(), "an empty hull is refused");
        hull.points.assign(257, std::array<float, 3>{0.0F, 0.0F, 0.0F});
        nfsu::VehicleTrackSession many(stock_model(), course, {}, hull);
        check(!many.valid(), "a hull of 257 points is refused");
        hull.points.assign(3, std::array<float, 3>{0.0F, 0.0F, 0.0F});
        hull.points[1][2] = std::nanf("");
        nfsu::VehicleTrackSession nan(stock_model(), course, {}, hull);
        check(!nan.valid(), "a hull point that is not finite is refused");
    }
    {
        nfsu::VehicleTrackApproximations approx;
        approx.stuck_speed = -1.0;
        nfsu::VehicleTrackSession session(stock_model(), course, approx);
        check(!session.valid(), "a negative approximation is refused");
    }
    {
        nfsu::VehicleTrackSession session(stock_model(), course);
        nfsu::VehicleTrackTelemetry t;
        check(!session.step(nfsu::VehicleInput{}, t), "a step before the start is refused");
        nfsu::VehicleState bad;
        bad.position.x = std::nan("");
        check(!session.start(bad), "a state that is not finite does not start");
        nfsu::VehiclePose pose = course.start;
        pose.heading = std::nan("");
        check(!session.start(pose), "a pose the model refuses does not start");
    }
    // Without a route a reset is asked for and reported, and not performed.
    {
        const nfsu::VehicleTrackCourse bare = deck_course(0.0F, false);
        check(!bare.has_route, "a layout with no loop has no route");
        nfsu::VehicleTrackSession session(stock_model(), bare);
        nfsu::VehiclePose pose;
        pose.ground = {-20.0, 32.0, 10.0};
        check(session.start(pose), "the car starts beside the slab");
        nfsu::VehicleTrackTelemetry t;
        bool asked = false;
        bool reported = false;
        for (int i = 0; i < 120; ++i) {
            check(session.step(nfsu::VehicleInput{}, t), "a step beside the slab runs");
            asked = asked || t.pending == nfsu::VehicleTrackReset::out_of_bounds;
            if (t.reset == nfsu::VehicleTrackReset::out_of_bounds) {
                reported = true;
                check(!session.last_reset().performed && t.resets == 0,
                      "without a route the reset is not performed");
            }
        }
        check(asked && reported, "without a route the car is still found out of bounds");
    }
    // A quad that repeats a corner is a triangle: its degenerate half is
    // never met, its other half is ground.
    {
        nfsu::VehicleTrackLayout layout;
        nfsu::VehicleTrackQuad q;
        const float c[4][3] = {{0.0F, 0.0F, 10.0F}, {32.0F, 0.0F, 10.0F},
                               {32.0F, 32.0F, 10.0F}, {32.0F, 32.0F, 10.0F}};
        std::memcpy(q.corner, c, sizeof(c));
        layout.quads.push_back(q);
        layout.grid[0] = 24.0F;
        layout.grid[1] = 8.0F;
        layout.grid[2] = 10.0F;
        nfsu::VehicleTrackCourse sliver;
        std::string error;
        const bool built = nfsu::build_vehicle_track_course(layout, sliver, error);
        check(built, "a course of one triangle builds: " + error);
        check(sliver.world.counts.degenerate_triangles == 1, "its second triangle is degenerate");
        const nfsu::TrackVehicleGround ground(sliver.world);
        check(ground.probe({24.0, 8.0, 11.0}, {0.0, 0.0, -1.0}, 2.0).hit.hit,
              "inside the triangle is ground");
        check(ground.probe({8.0, 24.0, 11.0}, {0.0, 0.0, -1.0}, 2.0).surface ==
                  nfsu::VehicleTrackSurface::clear,
              "over the degenerate half nothing is met");
    }
    // The image builder refuses corners off the format's grids.
    {
        std::string error;
        nfsu::VehicleTrackQuad q;
        const float c[4][3] = {{0.0F, 0.0F, 10.0F}, {32.01F, 0.0F, 10.0F},
                               {32.0F, 32.0F, 10.0F}, {0.0F, 32.0F, 10.0F}};
        std::memcpy(q.corner, c, sizeof(c));
        check(nfsu::build_vehicle_track_collision_image({q}, error).empty() && !error.empty(),
              "a corner off the eighths is refused");
        const float d[4][3] = {{0.0F, 0.0F, 10.001F}, {32.0F, 0.0F, 10.0F},
                               {32.0F, 32.0F, 10.0F}, {0.0F, 32.0F, 10.0F}};
        std::memcpy(q.corner, d, sizeof(d));
        check(nfsu::build_vehicle_track_collision_image({q}, error).empty(),
              "a height off the 256ths is refused");
        const float e[4][3] = {{5000.0F, 0.0F, 10.0F}, {32.0F, 0.0F, 10.0F},
                               {32.0F, 32.0F, 10.0F}, {0.0F, 32.0F, 10.0F}};
        std::memcpy(q.corner, e, sizeof(e));
        check(nfsu::build_vehicle_track_collision_image({q}, error).empty(),
              "a corner beyond the format's range is refused");
        check(nfsu::build_vehicle_track_collision_image({}, error).empty(),
              "no quad at all is refused");
    }
}

// ---------------------------------------------------------------------------
// Determinism and the pins
// ---------------------------------------------------------------------------

void test_telemetry_layout() {
    static_assert(nfsu::kVehicleTrackTelemetryRecordBytes == 916, "the record is 916 bytes");
    nfsu::VehicleTrackTelemetry t;
    t.step = 0x0102030405060708ULL;
    t.model.step = 0x1112131415161718ULL;
    t.model.simulation_seconds = 0.125;
    t.model.solver_dt = 1.0 / 240.0;
    t.model.alternate_yaw_active = false;
    t.position.x = 1.5;
    t.off_path = true;
    t.zone = -1;
    std::array<std::uint8_t, nfsu::kVehicleTrackTelemetryRecordBytes> bytes{};
    nfsu::encode_vehicle_track_telemetry(t, bytes);
    check(bytes[0] == 0x08 && bytes[7] == 0x01, "the step is the first u64, little-endian");
    check(bytes[8] == 0x18 && bytes[15] == 0x11, "the model's record follows it");
    std::array<std::uint8_t, nfsu::kVehicleTelemetryRecordBytes> model_bytes{};
    nfsu::encode_vehicle_telemetry(t.model, model_bytes);
    check(std::equal(model_bytes.begin(), model_bytes.end(), bytes.begin()+8),
          "track record embeds the complete model record including clock and yaw flag");
    double clock = 0.0, interval = 0.0;
    std::memcpy(&clock, bytes.data()+8+656, sizeof(clock));
    std::memcpy(&interval, bytes.data()+8+664, sizeof(interval));
    check(clock==0.125 && interval==1.0/240.0 && bytes[8+672]==0,
          "extended model fields retain their declared wire offsets");
    double x = 0.0;
    std::memcpy(&x, bytes.data() + 8 + nfsu::kVehicleTelemetryRecordBytes, sizeof(x));
    check(x == 1.5, "then the position's x");
    const std::size_t end = bytes.size();
    check(bytes[end - 4] == 1 && bytes[end - 8] == 0xFF && bytes[end - 5] == 0xFF,
          "the record ends with the zone, -1, and the off-path flag");
}

void test_pins() {
    const nfsu::VehicleTrackCourse course = proving_ground();
    for (const nfsu::VehicleTrackScenario& s :
         nfsu::vehicle_track_scenarios(nfsu::VehicleTrackCourseKind::proving_ground)) {
        const std::string got = digest(run(course, s));
        check(got == s.pinned, s.name + ": the telemetry is the pinned " + s.pinned + ", got " +
                                   got);
    }
    // The same run twice, on a second course built afresh.
    const nfsu::VehicleTrackCourse again = proving_ground();
    check(digest(run(course, scenario("wall"))) == digest(run(again, scenario("wall"))),
          "the same run twice gives the same bits");
    check(nfsu::vehicle_track_scenarios(nfsu::VehicleTrackCourseKind::proving_ground).size() ==
                  13 &&
              nfsu::vehicle_track_scenarios(nfsu::VehicleTrackCourseKind::circuit).size() == 6,
          "13 proving-ground and 6 circuit scenarios");
    for (const nfsu::VehicleTrackScenario& s :
         nfsu::vehicle_track_scenarios(nfsu::VehicleTrackCourseKind::circuit)) {
        check(s.pinned.size() == 64, s.name + ": the circuit scenario carries its pin");
    }
}

} // namespace

int main() {
    test_hull_source();
    test_contact_sqrt();
    test_separation();
    test_ground();
    test_road_matches_flat_ground();
    test_seams();
    test_slope();
    test_walls();
    test_separating_contact();
    test_wall_friction();
    test_drop();
    test_impulse_clamps();
    test_out_of_bounds();
    test_bounds_side_probe();
    test_bounds_side_probe_east_only();
    test_zones_and_off_path();
    test_sunk();
    test_sunk_needs_wheels();
    test_sunk_margin();
    test_reset_skips_holes();
    test_manual_and_approximations();
    test_refusals();
    test_telemetry_layout();
    test_pins();
    if (failures != 0) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "nfsu_vehicle_track_tests: all checks passed\n";
    return 0;
}
