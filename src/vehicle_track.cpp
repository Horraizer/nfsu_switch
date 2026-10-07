#include "nfsu/vehicle_track.h"

#include "nfsu/det_sqrt.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <limits>

// The step's arithmetic must be IEEE-754 binary64 and binary32 without
// excess precision, as src/vehicle.cpp requires of the model it extends.
#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "nfsu_vehicle_track needs FLT_EVAL_METHOD 0 (no x87 excess precision)"
#endif

namespace nfsu {
namespace {

VehicleVec3 v3(double x, double y, double z) noexcept {
    VehicleVec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

VehicleVec3 add(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

VehicleVec3 sub(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

VehicleVec3 scale(const VehicleVec3& a, double s) noexcept {
    return v3(a.x * s, a.y * s, a.z * s);
}

double dot(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

VehicleVec3 cross(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

// (x - x) is 0 for every finite x and NaN otherwise; std::isfinite is not
// used, for the reason src/vehicle.cpp gives.
double absolute(double x) noexcept {
    return x < 0.0 ? -x : x;
}

bool finite(double x) noexcept {
    const double zero = x - x;
    return zero == zero;
}

bool finite(const VehicleVec3& v) noexcept {
    return finite(v.x) && finite(v.y) && finite(v.z);
}

bool finite_state(const VehicleState& s) noexcept {
    return finite(s.position) && finite(s.velocity) && finite(s.angular_velocity) &&
           finite(s.orientation.w) && finite(s.orientation.x) && finite(s.orientation.y) &&
           finite(s.orientation.z);
}

VehicleVec3 widen(const float v[3]) noexcept {
    return v3(static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2]));
}

void narrow(const VehicleVec3& v, float out[3]) noexcept {
    out[0] = static_cast<float>(v.x);
    out[1] = static_cast<float>(v.y);
    out[2] = static_cast<float>(v.z);
}

// Body to world, the same matrix src/vehicle.cpp builds from a unit
// quaternion: its columns are the body's +x, +y and +z axes in the world.
struct Mat3 {
    double m[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

    VehicleVec3 apply(const VehicleVec3& v) const noexcept {
        return v3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                  m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                  m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
    }

    VehicleVec3 apply_transpose(const VehicleVec3& v) const noexcept {
        return v3(m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                  m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                  m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z);
    }

    VehicleVec3 column(int c) const noexcept {
        return v3(m[0][c], m[1][c], m[2][c]);
    }
};

Mat3 rotation(const VehicleQuat& q) noexcept {
    const double xx = q.x * q.x;
    const double yy = q.y * q.y;
    const double zz = q.z * q.z;
    const double xy = q.x * q.y;
    const double xz = q.x * q.z;
    const double yz = q.y * q.z;
    const double wx = q.w * q.x;
    const double wy = q.w * q.y;
    const double wz = q.w * q.z;
    Mat3 r;
    r.m[0][0] = 1.0 - 2.0 * (yy + zz);
    r.m[0][1] = 2.0 * (xy - wz);
    r.m[0][2] = 2.0 * (xz + wy);
    r.m[1][0] = 2.0 * (xy + wz);
    r.m[1][1] = 1.0 - 2.0 * (xx + zz);
    r.m[1][2] = 2.0 * (yz - wx);
    r.m[2][0] = 2.0 * (xz - wy);
    r.m[2][1] = 2.0 * (yz + wx);
    r.m[2][2] = 1.0 - 2.0 * (xx + yy);
    return r;
}

// The 37 points of 240SX_CV, as the IEEE-754 bits of the floats at +0x10 of
// each record of the 0x00039201 payload at Cars/240SX/GEOMETRY.BIN offset
// 4778608 (include/nfsu/vehicle_track.h). Bits rather than decimals, so no
// compiler's reading of a literal can move one.
constexpr std::uint32_t k240sxHullBits[kVehicle240sxHullPoints][3] = {
    {0xC000CD7DU, 0xBF098FC6U, 0x3E3101C2U},  //  0: -2.01254201 -0.537350059 0.172858268
    {0xBFF56ACFU, 0xBF44605DU, 0x3DB42B4AU},  //  1: -1.91732204 -0.767095387 0.0879731923
    {0x3EA6E436U, 0xBF588739U, 0x3DB42B42U},  //  2: 0.325959861 -0.845813334 0.0879731327
    {0x3FC5EFD3U, 0xBF61783CU, 0x3DB42B42U},  //  3: 1.54638135 -0.880740881 0.0879731327
    {0xC0036C93U, 0xBA0103B9U, 0x3E3B579DU},  //  4: -2.05350184 -0.000492151419 0.182951406
    {0xBFF56ACFU, 0xBA0103A3U, 0x3DB42B49U},  //  5: -1.91732204 -0.000492150139 0.0879731849
    {0x3EA6E436U, 0xB998EAA9U, 0x3DB42B42U},  //  6: 0.325959861 -0.000291665347 0.0879731327
    {0x4017770BU, 0x39CCCB35U, 0x3DB42B49U},  //  7: 2.36664081 0.000390613131 0.0879731849
    {0xC000CD7DU, 0x3F0588DBU, 0x3E3101BBU},  //  8: -2.01254201 0.521619499 0.172858164
    {0xBFF56ACEU, 0x3F44140EU, 0x3DB42B49U},  //  9: -1.91732192 0.76593101 0.0879731849
    {0x3EA6E437U, 0x3F585459U, 0x3DB42B49U},  // 10: 0.325959891 0.845037043 0.0879731849
    {0x3FC5EFD3U, 0x3F5FB320U, 0x3DB42B42U},  // 11: 1.54638135 0.873826981 0.0879731327
    {0xC000CD7DU, 0xBF098FC4U, 0x3F364D25U},  // 12: -2.01254201 -0.537349939 0.712114632
    {0xBFF56ACFU, 0xBF446062U, 0x3F2BB2C5U},  // 13: -1.91732204 -0.767095685 0.670696557
    {0x3EA6E437U, 0xBF58873DU, 0x3F132777U},  // 14: 0.325959891 -0.845813572 0.574820936
    {0x3FC5EFFDU, 0xBF5A7804U, 0x3EF4A5BDU},  // 15: 1.54638636 -0.853393793 0.477826983
    {0xC0036C93U, 0xBA010382U, 0x3F364D25U},  // 16: -2.05350184 -0.000492148218 0.712114632
    {0x3EA6E437U, 0xB998EB42U, 0x3F84C0A6U},  // 17: 0.325959891 -0.000291669799 1.03712916
    {0x401776F4U, 0x39CCCA1BU, 0x3ED36062U},  // 18: 2.36663532 0.000390604924 0.412844718
    {0xBFF56ACFU, 0x3F44140AU, 0x3F2BB2C6U},  // 19: -1.91732204 0.765930772 0.670696616
    {0x3EA6E437U, 0x3F585459U, 0x3F132777U},  // 20: 0.325959891 0.845037043 0.574820936
    {0x3FC5EFFDU, 0x3F58B2E9U, 0x3EF4A5BCU},  // 21: 1.54638636 0.846479952 0.477826953
    {0x3EA6E437U, 0xBF0553F0U, 0x3F79DA93U},  // 22: 0.325959891 -0.520812035 0.975991428
    {0xC000CD7DU, 0x3F0588DCU, 0x3F364D24U},  // 23: -2.01254201 0.521619558 0.712114573
    {0x3EA6E437U, 0x3F012310U, 0x3F79DA93U},  // 24: 0.325959891 0.504441261 0.975991428
    {0xBF4EC929U, 0xB9939DDAU, 0x3DB42B42U},  // 25: -0.80775696 -0.000281556335 0.0879731327
    {0xBF4EC929U, 0xBF566766U, 0x3DB42B44U},  // 26: -0.80775696 -0.837515235 0.0879731476
    {0xBF4EC929U, 0x3F5635C6U, 0x3DB42B49U},  // 27: -0.80775696 0.836758018 0.0879731849
    {0xBF4EC929U, 0xBF05FDD4U, 0x3F79DA93U},  // 28: -0.80775696 -0.52340436 0.975991428
    {0xBF4EC929U, 0xB9939F0AU, 0x3F84C0A6U},  // 29: -0.80775696 -0.000281565182 1.03712916
    {0xBF4EC929U, 0x3F01C91DU, 0x3F79DA93U},  // 30: -0.80775696 0.506974995 0.975991428
    {0xBF4EC929U, 0xBF566769U, 0x3F229689U},  // 31: -0.80775696 -0.837515414 0.635109484
    {0xBF4EC929U, 0x3F5635C7U, 0x3F229689U},  // 32: -0.80775696 0.836758077 0.635109484
    {0x400EA03CU, 0xBF30F04EU, 0x3DB42B4FU},  // 33: 2.22852993 -0.691166759 0.0879732296
    {0x400EA03CU, 0x3F30275CU, 0x3DB42B4DU},  // 34: 2.22852993 0.688100576 0.0879732147
    {0x400EA03BU, 0xBF2A68FBU, 0x3ED36062U},  // 35: 2.22852969 -0.665664375 0.412844718
    {0x400EA03BU, 0x3F29A004U, 0x3ED36062U},  // 36: 2.22852969 0.662597895 0.412844718
};

float float_of_bits(std::uint32_t bits) noexcept {
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void put_u32(std::uint8_t* out, std::uint32_t value) noexcept {
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
    }
}

void put_u64(std::uint8_t* out, std::uint64_t value) noexcept {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
    }
}

// A query that meets nothing because none was made.
constexpr std::uint32_t kNoPolygon = 0xFFFFFFFFU;

// Hands the model the track's ground and remembers what each of its four
// casts met, in the order the model makes them (wheel 0 to 3). Recording
// changes no answer, which is what VehicleGround asks of an implementation.
class RecordingGround final : public VehicleGround {
public:
    explicit RecordingGround(const TrackVehicleGround& inner) noexcept : inner_(inner) {}

    VehicleGroundHit cast(const VehicleVec3& from,
                          const VehicleVec3& direction,
                          double length) const override {
        const VehicleTrackCast answer = inner_.probe(from, direction, length);
        if (calls_ < kVehicleWheelCount) {
            surfaces_[calls_] = answer.surface;
        }
        ++calls_;
        return answer.hit;
    }

    const std::array<VehicleTrackSurface, kVehicleWheelCount>& surfaces() const noexcept {
        return surfaces_;
    }

private:
    const TrackVehicleGround& inner_;
    mutable std::array<VehicleTrackSurface, kVehicleWheelCount> surfaces_{};
    mutable std::size_t calls_ = 0;
};

// One hull point's probe that met the track.
struct HullHit {
    std::size_t point = 0;
    std::size_t polygon = 0;
    TrackContactKind kind = TrackContactKind::road;
    float normal_f[3] = {0.0F, 0.0F, 0.0F};
    VehicleVec3 normal;
    // The point less the centre of mass: where the impulse acts.
    VehicleVec3 arm;
    VehicleVec3 plane_point;
    double depth = 0.0;
};

} // namespace

// ---------------------------------------------------------------------------
// Names and small helpers
// ---------------------------------------------------------------------------

const char* vehicle_track_surface_name(VehicleTrackSurface surface) noexcept {
    switch (surface) {
    case VehicleTrackSurface::none:
        return "none";
    case VehicleTrackSurface::road:
        return "road";
    case VehicleTrackSurface::wall:
        return "wall";
    case VehicleTrackSurface::clear:
        return "clear";
    case VehicleTrackSurface::outside:
        return "outside";
    case VehicleTrackSurface::refused:
        return "refused";
    }
    return "unknown";
}

const char* vehicle_track_reset_name(VehicleTrackReset reason) noexcept {
    switch (reason) {
    case VehicleTrackReset::none:
        return "none";
    case VehicleTrackReset::zone:
        return "reset-zone";
    case VehicleTrackReset::zone_to_point:
        return "reset-to-point-zone";
    case VehicleTrackReset::off_path:
        return "off-path";
    case VehicleTrackReset::out_of_bounds:
        return "out-of-bounds";
    case VehicleTrackReset::sunk:
        return "sunk";
    case VehicleTrackReset::manual:
        return "manual";
    case VehicleTrackReset::upside_down:
        return "upside-down";
    case VehicleTrackReset::stuck:
        return "stuck";
    }
    return "unknown";
}

double vehicle_track_heading(std::int64_t units) noexcept {
    // 2 pi / 65536, the double nearest it: pi / 32768 is exact to the
    // double's rounding of pi.
    constexpr double kUnit = 3.141592653589793 / 32768.0;
    return static_cast<double>(units) * kUnit;
}

// 0x0046B410..0x0046B492: the separation search along the normal, in float
// as the x87 in single precision computes it. Every try moves the car by
// the normal times a growing distance, its vertical part held to +-0.15,
// and the first move that clears `needed` - the deepest point's depth
// along the normal, which is this project's stand-in for "the hull and the
// polygon no longer intersect" - is the answer. After 20 tries retail
// leaves the pair's separation as it was and still moves the bodies by it
// (0x0046B80F..0x0046B87B); here there is no pair record and no move
// (APPROXIMATION, docs/VEHICLE_TRACK.md).
bool vehicle_track_separation(const float normal[3], double needed,
                              VehicleVec3& move) noexcept {
    float step = kVehicleTrackSeparationFirstStep;
    float total = 0.0F;
    const VehicleVec3 n = widen(normal);
    for (std::uint32_t k = 0; k < kVehicleTrackSeparationTries; ++k) {
        total = total + step;
        if (step < kVehicleTrackSeparationStepLimit) {
            step = step + kVehicleTrackSeparationStepGrowth;
        }
        float m[3] = {normal[0] * total, normal[1] * total, normal[2] * total};
        if (!(m[2] <= kVehicleTrackSeparationVertical)) {
            m[2] = kVehicleTrackSeparationVertical;
        } else if (m[2] < -kVehicleTrackSeparationVertical) {
            m[2] = -kVehicleTrackSeparationVertical;
        }
        const VehicleVec3 candidate = widen(m);
        if (dot(candidate, n) >= needed) {
            move = candidate;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// The hull
// ---------------------------------------------------------------------------

VehicleHull stock_240sx_hull() {
    VehicleHull hull;
    hull.points.resize(kVehicle240sxHullPoints);
    for (std::size_t k = 0; k < kVehicle240sxHullPoints; ++k) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            hull.points[k][axis] = float_of_bits(k240sxHullBits[k][axis]);
        }
    }
    return hull;
}

bool encode_vehicle_hull(const VehicleHull& hull, unsigned char* payload,
                         std::size_t size) noexcept {
    if (payload == nullptr || hull.points.size() > size / kVehicleHullRecordSize) {
        return false;
    }
    for (std::size_t k = 0; k < hull.points.size(); ++k) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &hull.points[k][axis], sizeof(bits));
            put_u32(payload + k * kVehicleHullRecordSize + kVehicleHullPointOffset + axis * 4,
                    bits);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// The ground
// ---------------------------------------------------------------------------

VehicleTrackCast TrackVehicleGround::probe(const VehicleVec3& from,
                                           const VehicleVec3& direction,
                                           double length) const {
    VehicleTrackCast out;
    out.surface = VehicleTrackSurface::refused;
    const double squared = dot(direction, direction);
    if (world_ == nullptr || !finite(from) || !finite(direction) || !finite(length) ||
        !(length > 0.0) || !(squared > 0.0) || !finite(squared)) {
        return out;
    }
    const VehicleVec3 to = add(from, scale(direction, length));
    float from_f[3];
    float to_f[3];
    narrow(from, from_f);
    narrow(to, to_f);
    out.status = query_track_contact_segment(*world_, from_f, to_f, out.contact);
    switch (out.status) {
    case TrackContactStatus::hit:
        out.surface = out.contact.kind == TrackContactKind::road ? VehicleTrackSurface::road
                                                                 : VehicleTrackSurface::wall;
        break;
    case TrackContactStatus::clear:
        out.surface = VehicleTrackSurface::clear;
        break;
    case TrackContactStatus::outside:
        out.surface = VehicleTrackSurface::outside;
        break;
    case TrackContactStatus::refused:
        out.surface = VehicleTrackSurface::refused;
        break;
    }
    if (out.surface != VehicleTrackSurface::road) {
        return out;
    }
    // The contact point's parameter on the cast's own line, from the start
    // the model gave: the query measured from the float start along the
    // float direction, and this undoes both roundings to first order.
    double distance = dot(sub(widen(out.contact.point), from), direction) / squared;
    if (!(distance >= 0.0)) {
        distance = 0.0;
    } else if (distance > length) {
        distance = length;
    }
    out.hit.hit = true;
    out.hit.distance = distance;
    out.hit.normal = widen(out.contact.normal);
    out.hit.surface = 0;
    return out;
}

VehicleGroundHit TrackVehicleGround::cast(const VehicleVec3& from,
                                          const VehicleVec3& direction,
                                          double length) const {
    return probe(from, direction, length).hit;
}

// ---------------------------------------------------------------------------
// Telemetry
// ---------------------------------------------------------------------------

void encode_vehicle_track_telemetry(
    const VehicleTrackTelemetry& t,
    std::array<std::uint8_t, kVehicleTrackTelemetryRecordBytes>& out) noexcept {
    std::size_t at = 0;
    const auto u32 = [&](std::uint32_t value) {
        put_u32(out.data() + at, value);
        at += 4;
    };
    const auto f64 = [&](double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        put_u64(out.data() + at, bits);
        at += 8;
    };
    const auto vec = [&](const VehicleVec3& v) {
        f64(v.x);
        f64(v.y);
        f64(v.z);
    };
    put_u64(out.data() + at, t.step);
    at += 8;
    std::array<std::uint8_t, kVehicleTelemetryRecordBytes> model{};
    encode_vehicle_telemetry(t.model, model);
    std::memcpy(out.data() + at, model.data(), model.size());
    at += model.size();
    vec(t.position);
    f64(t.orientation.w);
    f64(t.orientation.x);
    f64(t.orientation.y);
    f64(t.orientation.z);
    vec(t.velocity);
    vec(t.angular_velocity);
    for (VehicleTrackSurface surface : t.wheel_surface) {
        u32(static_cast<std::uint32_t>(surface));
    }
    u32(t.hull_hits);
    u32(t.contacts);
    u32(t.impulses);
    u32(t.deepest_polygon);
    u32(static_cast<std::uint32_t>(t.deepest_surface));
    f64(t.deepest_depth);
    vec(t.separation);
    vec(t.impulse);
    u32(t.tick ? 1U : 0U);
    u32(static_cast<std::uint32_t>(t.bounds));
    u32(static_cast<std::uint32_t>(t.reset));
    u32(static_cast<std::uint32_t>(t.pending));
    u32(t.resets);
    u32(static_cast<std::uint32_t>(t.lap));
    std::uint32_t percent_bits = 0;
    std::memcpy(&percent_bits, &t.percent, sizeof(percent_bits));
    u32(percent_bits);
    u32(static_cast<std::uint32_t>(t.zone));
    u32(t.off_path ? 1U : 0U);
}

void VehicleTrackTelemetryHash::add(const VehicleTrackTelemetry& record) noexcept {
    std::array<std::uint8_t, kVehicleTrackTelemetryRecordBytes> bytes{};
    encode_vehicle_track_telemetry(record, bytes);
    sha_.update(bytes.data(), bytes.size());
    ++records_;
}

Sha256Digest VehicleTrackTelemetryHash::finish() noexcept {
    return sha_.finish();
}

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

VehicleTrackApproximations vehicle_track_first_lap_approximations() noexcept {
    VehicleTrackApproximations approx;
    approx.upside_down_steps = 120;
    return approx;
}

VehicleTrackSession::VehicleTrackSession(const VehicleModel& model,
                                         const VehicleTrackCourse& course,
                                         const VehicleTrackApproximations& approx,
                                         const VehicleHull& hull)
    : model_(&model), course_(&course), approx_(approx), ground_(course.world) {
    if (!model.valid()) {
        error_ = "the vehicle model is not valid: " + model.error();
        return;
    }
    if (!course.world.valid) {
        error_ = "the course's collision is not valid";
        return;
    }
    // Every point is one segment query a step, so the hull is what bounds
    // the body's work; the stock hull has 37.
    constexpr std::size_t kMostHullPoints = 256;
    if (hull.points.empty() || hull.points.size() > kMostHullPoints) {
        error_ = "the hull must hold 1 to 256 points";
        return;
    }
    if (!(approx.upside_down_speed >= 0.0) || !finite(approx.upside_down_speed) ||
        !(approx.stuck_speed >= 0.0) || !finite(approx.stuck_speed) ||
        !finite(approx.stuck_throttle)) {
        error_ = "an approximation is negative or not finite";
        return;
    }
    const VehicleHandling& h = model.spec().handling;
    const double unit = model.spec().approx.unit_scale;
    // 0x00472E40: each point less the centre of mass, in float, once.
    hull_.reserve(hull.points.size());
    for (const std::array<float, 3>& point : hull.points) {
        const float moved[3] = {point[0] - h.centre_of_mass[0], point[1] - h.centre_of_mass[1],
                                point[2] - h.centre_of_mass[2]};
        const VehicleVec3 v = widen(moved);
        if (!finite(v)) {
            error_ = "a hull point is not finite";
            hull_.clear();
            return;
        }
        hull_.push_back(v);
    }
    mass_ = static_cast<double>(h.mass) * unit;
    collision_mass_ = kVehicleTrackCollisionMass * unit;
    inertia_ = v3(static_cast<double>(h.inertia[0]) * unit,
                  static_cast<double>(h.inertia[1]) * unit,
                  static_cast<double>(h.inertia[2]) * unit);
    // A column that meets every surface of the course, from one above the
    // highest corner to one below the lowest: 0x00440A40's query from
    // 99999.1 down, bounded. The query's length cap bounds it too.
    const TrackCollisionCost& cost = course.world.collision.cost;
    column_top_ = static_cast<double>(cost.corner_max[2]) + 1.0;
    column_bottom_ = static_cast<double>(cost.corner_min[2]) - 1.0;
    const double cap = static_cast<double>(TrackContactQueryLimits{}.max_length);
    if (column_top_ - column_bottom_ > cap) {
        column_bottom_ = column_top_ - cap;
    }
    if (!finite(column_top_) || !finite(column_bottom_)) {
        error_ = "the course's bounds are not finite";
        return;
    }
    valid_ = true;
}

bool VehicleTrackSession::start(const VehiclePose& pose) {
    if (!valid_) {
        return false;
    }
    VehicleState state;
    if (!model_->reset(state, pose)) {
        return false;
    }
    return start(state);
}

bool VehicleTrackSession::start(const VehicleState& state) {
    if (!valid_ || !finite_state(state)) {
        return false;
    }
    state_ = state;
    progress_ = RaceProgress{};
    if (course_->has_route) {
        const float xy[2] = {static_cast<float>(state_.position.x),
                             static_cast<float>(state_.position.y)};
        start_race_progress(course_->route, course_->track, xy, progress_);
    }
    events_.clear();
    steps_ = 0;
    ticks_ = 0;
    pending_ = VehicleTrackReset::none;
    resets_ = 0;
    upside_down_count_ = 0;
    stuck_count_ = 0;
    last_reset_ = VehicleTrackResetInfo{};
    started_ = true;
    return true;
}

void VehicleTrackSession::request_reset() noexcept {
    if (pending_ == VehicleTrackReset::none) {
        pending_ = VehicleTrackReset::manual;
    }
}

bool VehicleTrackSession::ground_under(double x, double y, double& z) const {
    const float from[3] = {static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(column_top_)};
    const float to[3] = {from[0], from[1], static_cast<float>(column_bottom_)};
    TrackContact contact;
    if (query_track_contact_segment(course_->world, from, to, contact) !=
        TrackContactStatus::hit) {
        return false;
    }
    z = static_cast<double>(contact.point[2]);
    return true;
}

bool VehicleTrackSession::sunk(const VehicleState& s) const {
    for (const VehicleWheelState& wheel : s.wheels) {
        if (!wheel.contact) {
            return false;
        }
    }
    // APPROXIMATION of 0x00440A40, the ground query 0x0042A4D0 asks: the
    // first surface a column meets from 1.0 above the position down to 0.25
    // above it. Retail answers the car's cached last polygon first, at any
    // height, then the first polygon the point is strictly inside, and makes
    // a second pass with no upper limit (docs/VEHICLE_TRACK.md).
    const float from[3] = {static_cast<float>(s.position.x), static_cast<float>(s.position.y),
                           static_cast<float>(s.position.z + kVehicleTrackSunkReach)};
    const float to[3] = {from[0], from[1],
                         static_cast<float>(s.position.z + kVehicleTrackSunkMargin)};
    TrackContact contact;
    return query_track_contact_segment(course_->world, from, to, contact) ==
           TrackContactStatus::hit;
}

void VehicleTrackSession::resolve_body(VehicleState& s, VehicleTrackTelemetry& t) const {
    const Mat3 r = rotation(s.orientation);
    const VehicleVec3 centre = s.position;
    float from[3];
    narrow(centre, from);

    // Each hull point's probe: the segment from the centre of mass, inside
    // the hull, out to the point. What it meets lies between the two, so
    // the point is on or beyond that surface, by the depth along the
    // surface's normal, which faces the centre.
    std::vector<HullHit> hits;
    hits.reserve(hull_.size());
    for (std::size_t k = 0; k < hull_.size(); ++k) {
        const VehicleVec3 point = add(centre, r.apply(hull_[k]));
        float to[3];
        narrow(point, to);
        TrackContact contact;
        if (query_track_contact_segment(course_->world, from, to, contact) !=
            TrackContactStatus::hit) {
            continue;
        }
        HullHit hit;
        hit.point = k;
        hit.polygon = contact.polygon;
        hit.kind = contact.kind;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            hit.normal_f[axis] = contact.normal[axis];
        }
        hit.normal = widen(contact.normal);
        hit.arm = sub(point, centre);
        hit.plane_point = widen(contact.point);
        hit.depth = dot(sub(widen(contact.point), point), hit.normal);
        if (!(hit.depth > 0.0)) {
            hit.depth = 0.0;
        }
        hits.push_back(hit);
    }
    t.hull_hits = static_cast<std::uint32_t>(hits.size());
    if (hits.empty()) {
        return;
    }
    // The game pairs the car's body with each polygon's body; here each
    // polygon is one contact, at its deepest point (the lowest point index
    // on a tie), and the contacts are taken in polygon order.
    std::sort(hits.begin(), hits.end(), [](const HullHit& a, const HullHit& b) {
        if (a.polygon != b.polygon) {
            return a.polygon < b.polygon;
        }
        if (a.depth != b.depth) {
            return a.depth > b.depth;
        }
        return a.point < b.point;
    });

    const VehicleVec3 up = r.column(2);
    const double unit = model_->spec().approx.unit_scale;
    const double inverse_mass = 1.0 / mass_;
    const double floor_inverse_mass = 1.0 / collision_mass_;
    // 0x0046A080: the solver's inverse mass of the car, never above that of
    // the collision mass.
    const double solver_inverse_mass =
        inverse_mass > floor_inverse_mass ? floor_inverse_mass : inverse_mass;
    // 0x0046A260: the impulse is scaled so the car answers as the
    // collision mass would.
    const double mass_scale = inverse_mass > floor_inverse_mass
                                  ? floor_inverse_mass / inverse_mass
                                  : 1.0;
    // 0x0046C00C..0x0046C0A3: against a body that cannot move, an upright
    // car - its up axis's vertical part above 0.7 - slides with friction 0.1
    // and takes the fast-car rule below; a car tilted further keeps 0.3.
    const bool upright = up.z > kVehicleTrackUprightUp;
    const double friction = upright ? kVehicleTrackUprightFriction : kVehicleTrackFriction;
    const VehicleVec3 forward = r.column(0);
    const double car_speed = vehicle_sqrt(dot(s.velocity, s.velocity));
    const auto inverse_inertia = [&](const VehicleVec3& world) {
        const VehicleVec3 body = r.apply_transpose(world);
        return r.apply(v3(body.x / inertia_.x, body.y / inertia_.y, body.z / inertia_.z));
    };

    VehicleVec3 moved;
    VehicleVec3 applied_total;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        if (i > 0 && hits[i].polygon == hits[i - 1].polygon) {
            continue;
        }
        const HullHit& hit = hits[i];
        ++t.contacts;
        auto& event = t.contact_events.emplace_back();
        event.polygon = static_cast<std::uint32_t>(hit.polygon);
        event.hull_point = static_cast<std::uint32_t>(hit.point);
        event.order = t.contacts - 1;
        event.surface = hit.kind == TrackContactKind::road ? VehicleTrackSurface::road :
                                                            VehicleTrackSurface::wall;
        event.normal = hit.normal; event.plane_point = hit.plane_point; event.arm = hit.arm;
        event.depth = hit.depth;
        event.velocity_before = s.velocity; event.angular_before = s.angular_velocity;
        event.velocity_after = s.velocity; event.angular_after = s.angular_velocity;
        event.accumulated_impulse = applied_total;
        if (t.deepest_polygon == kNoPolygon || hit.depth > t.deepest_depth) {
            t.deepest_polygon = static_cast<std::uint32_t>(hit.polygon);
            t.deepest_surface = hit.kind == TrackContactKind::road ? VehicleTrackSurface::road
                                                                   : VehicleTrackSurface::wall;
            t.deepest_depth = hit.depth;
        }

        // The separation first (0x0046B7C2, then 0x0046B80F..0x0046B87B),
        // less what earlier contacts of this step already moved the car
        // along this normal.
        const double needed = hit.depth - dot(moved, hit.normal);
        if (needed > 0.0) {
            VehicleVec3 move;
            if (vehicle_track_separation(hit.normal_f, needed, move)) {
                s.position = add(s.position, move);
                moved = add(moved, move);
                event.separation = move;
            }
        }

        // Then the impulse, 0x0046BAE0, at the contact. 0x0046B8C0 puts
        // it midway between the two bodies' closest points once they are
        // apart, which for a hull point against a polygon is that point.
        const VehicleVec3 arm = hit.arm;
        const VehicleVec3 point_velocity = add(s.velocity, cross(s.angular_velocity, arm));
        const double approach = dot(point_velocity, hit.normal);
        event.approach = approach;
        // Copy only: this observer never changes contact order or solver math.
        VehicleContactObservation* audio_contact = nullptr;
        if (t.audio_contact_count < t.audio_contacts.size()) {
            audio_contact = &t.audio_contacts[t.audio_contact_count++];
            audio_contact->polygon = static_cast<std::uint32_t>(hit.polygon);
            audio_contact->forward = forward; audio_contact->up = up;
            const auto audio_tangent = sub(point_velocity, scale(hit.normal, approach));
            audio_contact->slide_mps = vehicle_sqrt(dot(audio_tangent, audio_tangent));
        }
        if (!(approach < 0.0)) {
            continue;
        }
        ++t.impulses;
        // 0x0046BA50 over 0x0046A0C0's inverse inertia, which carries the
        // angular factor.
        const VehicleVec3 turn =
            scale(inverse_inertia(cross(arm, hit.normal)), kVehicleTrackAngularFactor);
        const double effective = solver_inverse_mass + dot(hit.normal, cross(turn, arm));
        // 0x0046C620: against a wall polygon, whose normal's vertical part
        // is below 0.1, a hit whose arm lies more than acos 0.9 off the
        // car's forward axis - a side hit - takes half the restitution.
        double restitution = kVehicleTrackRestitution;
        if (absolute(hit.normal_f[2]) < kVehicleTrackSideHitWall) {
            float arm_f[3];
            narrow(arm, arm_f);
            const float arm_length = static_cast<float>(det_sqrt(static_cast<double>(
                arm_f[0] * arm_f[0] + arm_f[1] * arm_f[1] + arm_f[2] * arm_f[2])));
            if (arm_length > 0.0F) {
                const double along =
                    (static_cast<double>(arm_f[0] / arm_length) * forward.x +
                     static_cast<double>(arm_f[1] / arm_length) * forward.y) +
                    static_cast<double>(arm_f[2] / arm_length) * forward.z;
                if (absolute(along) < kVehicleTrackSideHitAlong) {
                    restitution = restitution * kVehicleTrackSideHitScale;
                }
            }
        }
        double j = -(restitution + 1.0) * approach / effective;
        const double smallest = kVehicleTrackSeparationSpeed / solver_inverse_mass;
        if (!(j >= smallest)) {
            j = smallest;
        }
        const VehicleVec3 tangential = sub(point_velocity, scale(hit.normal, approach));
        const double slide = vehicle_sqrt(dot(tangential, tangential));
        double jt = friction * j;
        if (slide < kVehicleTrackSlideSpeed) {
            jt = jt / kVehicleTrackSlideSpeed * slide;
        }
        VehicleVec3 impulse = scale(hit.normal, j);
        if (slide > kVehicleTrackSlideEpsilon) {
            impulse = add(impulse, scale(tangential, -1.0 / slide * jt));
        }
        // The impulse turns the car about its full arm. Retail's four-wheel
        // lever blocks (0x0046C3FE..0x0046C52C), which would drop the arm's
        // part along the up axis of a car with all four wheels down, run
        // only when the collision record's +0x10 is set
        // (0x0046C3F3..0x0046C3F8), and only 0x00465EB4 sets it: for a
        // car-vs-car or car-vs-kind-4 hit of 30 t m/s or more. A track
        // polygon is kind 3, so they never run against the track.
        VehicleVec3 lever = arm;
        // 0x0046C223..0x0046C3D9: an upright car faster than 30 m/s loses a
        // horizontal impulse of 0.4 t m/s against its velocity, and its arm
        // is shortened by |J| / (|J| + v / 1.75), impulse in t m/s, so a wall
        // at speed turns it less.
        if (upright && car_speed > kVehicleTrackFastSpeed) {
            const double size_t_m_s = vehicle_sqrt(dot(impulse, impulse)) / unit;
            const double drag = -kVehicleTrackFastDrag * unit / car_speed;
            impulse.x = impulse.x + drag * s.velocity.x;
            impulse.y = impulse.y + drag * s.velocity.y;
            if (size_t_m_s + car_speed > 1.0) {
                const double shorten =
                    size_t_m_s / (car_speed / kVehicleTrackFastArm + size_t_m_s);
                lever = scale(lever, shorten);
            }
        }
        // 0x0046A260, then 0x00584F60's two clamps and the angular factor.
        event.contact_impulse = impulse;
        if (audio_contact) audio_contact->impulse = scale(impulse, 1.0 / unit);
        VehicleVec3 applied = scale(impulse, mass_scale);
        const double linear_limit = kVehicleTrackImpulseLimit * unit;
        const double size = vehicle_sqrt(dot(applied, applied));
        if (size > linear_limit) {
            applied = scale(applied, linear_limit / size);
        }
        VehicleVec3 angular = cross(lever, applied);
        const double angular_limit = kVehicleTrackAngularLimit * unit;
        const double angular_size = vehicle_sqrt(dot(angular, angular));
        if (angular_size > angular_limit) {
            angular = scale(angular, angular_limit / angular_size);
        }
        angular = scale(angular, kVehicleTrackAngularFactor);
        s.velocity = add(s.velocity, scale(applied, inverse_mass));
        s.angular_velocity = add(s.angular_velocity, inverse_inertia(angular));
        applied_total = add(applied_total, applied);
        event.applied_impulse = applied; event.accumulated_impulse = applied_total;
        event.velocity_after = s.velocity; event.angular_after = s.angular_velocity;
    }
    t.separation = moved;
    t.impulse = applied_total;
}

// 0x00427CD2..0x00427D10: after a reset the velocity is the car's forward
// axis at the speed plus 0.1; the stored speed takes the speed itself
// (0x00427CBA) and each wheel's spin the speed over its radius
// (0x00427D36..0x00427D7E), as the model's reset gives them.
void VehicleTrackSession::lead_velocity(VehicleState& s, double speed) noexcept {
    const Mat3 r = rotation(s.orientation);
    s.velocity = scale(r.column(0), speed + kVehicleTrackResetSpeedLead);
}

bool VehicleTrackSession::reset_to_node(VehicleState& s, VehicleTrackReset reason) {
    VehicleTrackResetInfo info;
    info.reason = reason;
    info.step = steps_;
    last_reset_ = info;
    if (!course_->has_route) {
        return false;
    }
    // 0x004317A2..0x004317B4: the heading is the path's direction at the
    // car's coordinate, in race direction, taken before the car moves.
    const std::uint16_t units = track_angle(progress_.coordinate.tangent[0],
                                            progress_.coordinate.tangent[1]);
    // 0x0043164C..0x00431781: every node of the table, nearest first by
    // horizontal distance in float, a node tried only when strictly nearer
    // than the best so far, and kept when the ground query finds a polygon
    // under it.
    const float cx = static_cast<float>(s.position.x);
    const float cy = static_cast<float>(s.position.y);
    int best = -1;
    float best_distance = 0.0F;
    double best_z = 0.0;
    const std::vector<TrackPathNode>& nodes = course_->track.nodes;
    for (std::size_t n = 0; n < nodes.size(); ++n) {
        const float dx = cx - nodes[n].x;
        const float dy = cy - nodes[n].y;
        const float squared = dy * dy + dx * dx;
        const float distance = static_cast<float>(det_sqrt(static_cast<double>(squared)));
        if (best >= 0 && !(distance < best_distance)) {
            continue;
        }
        double z = 0.0;
        if (!ground_under(static_cast<double>(nodes[n].x), static_cast<double>(nodes[n].y), z)) {
            continue;
        }
        best = static_cast<int>(n);
        best_distance = distance;
        best_z = z;
    }
    if (best < 0) {
        return false;
    }
    const TrackPathNode& node = nodes[static_cast<std::size_t>(best)];
    VehiclePose pose;
    pose.ground = v3(static_cast<double>(node.x), static_cast<double>(node.y), best_z);
    pose.heading = vehicle_track_heading(units);
    pose.speed = 0.0;
    pose.gearbox = s.gearbox;
    VehicleState next;
    if (!model_->reset(next, pose)) {
        return false;
    }
    lead_velocity(next, pose.speed);
    s = next;
    // 0x00427C7F: the coordinate follows the car to where it now is. The
    // replay's stand-in for the car's heading is its last step, which a
    // reset must not read as a drive across the course.
    const float xy[2] = {node.x, node.y};
    update_track_path_coordinate(course_->route.spline, course_->track, progress_.coordinate,
                                 xy);
    progress_.previous_position[0] = xy[0];
    progress_.previous_position[1] = xy[1];
    ++resets_;
    info.performed = true;
    info.node = best;
    info.heading_units = units;
    info.pose = pose;
    last_reset_ = info;
    upside_down_count_ = 0;
    stuck_count_ = 0;
    return true;
}

bool VehicleTrackSession::reset_to_point(VehicleState& s, int zone_index) {
    VehicleTrackResetInfo info;
    info.reason = VehicleTrackReset::zone_to_point;
    info.step = steps_;
    info.zone = zone_index;
    last_reset_ = info;
    const TrackPathZone& zone = course_->track.zones[static_cast<std::size_t>(zone_index)];
    // 0x00427F01..0x00427F36: the zone's point, facing its direction, at
    // 8.0, through 0x00427E60 and 0x00427C50.
    const std::uint16_t units = track_angle(zone.direction[0], zone.direction[1]);
    double z = 0.0;
    if (!ground_under(static_cast<double>(zone.point[0]), static_cast<double>(zone.point[1]), z)) {
        return false;
    }
    VehiclePose pose;
    pose.ground = v3(static_cast<double>(zone.point[0]), static_cast<double>(zone.point[1]), z);
    pose.heading = vehicle_track_heading(units);
    pose.speed = kVehicleTrackResetToPointSpeed;
    pose.gearbox = s.gearbox;
    VehicleState next;
    if (!model_->reset(next, pose)) {
        return false;
    }
    lead_velocity(next, pose.speed);
    s = next;
    const float xy[2] = {zone.point[0], zone.point[1]};
    update_track_path_coordinate(course_->route.spline, course_->track, progress_.coordinate,
                                 xy);
    progress_.previous_position[0] = xy[0];
    progress_.previous_position[1] = xy[1];
    ++resets_;
    info.performed = true;
    info.heading_units = units;
    info.pose = pose;
    last_reset_ = info;
    upside_down_count_ = 0;
    stuck_count_ = 0;
    return true;
}

void VehicleTrackSession::run_tick(VehicleState& s, const VehicleInput& input,
                                   VehicleTrackTelemetry& t) {
    events_.clear();
    const auto request = [&](VehicleTrackReset reason) {
        if (pending_ == VehicleTrackReset::none) {
            pending_ = reason;
        }
    };

    // The car's frame update: 0x0042A4D0 (sunk) is called at 0x0042650F,
    // just before the flag is read at 0x00426518, so a sunk car is reset on
    // the frame it is found; a flag set on an earlier frame is acted on now.
    VehicleTrackReset reason = pending_;
    if (reason == VehicleTrackReset::none && sunk(s)) {
        reason = VehicleTrackReset::sunk;
    }
    if (reason != VehicleTrackReset::none) {
        pending_ = VehicleTrackReset::none;
        reset_to_node(s, reason);
        t.reset = reason;
        return;
    }

    // The coordinate, the off-path rule, the wrong-way test with its zones
    // and the laps, as the route layer replays them.
    if (course_->has_route) {
        const float xy[2] = {static_cast<float>(s.position.x), static_cast<float>(s.position.y)};
        step_race_progress(course_->route, course_->track, progress_, xy, 1.0F / 60.0F, events_);
        if (progress_.off_path) {
            request(VehicleTrackReset::off_path);
        }
        if (progress_.zone >= 0 &&
            static_cast<std::size_t>(progress_.zone) < course_->track.zones.size()) {
            const TrackPathZone& zone =
                course_->track.zones[static_cast<std::size_t>(progress_.zone)];
            // 0x00427ED9..0x00427EF6: a zone with a height resets only a
            // car below it.
            const bool below = zone.height == 0.0F ||
                               static_cast<float>(s.position.z) < zone.height;
            if (below) {
                if (zone.type == kZoneResetToPoint) {
                    reset_to_point(s, progress_.zone);
                    t.reset = VehicleTrackReset::zone_to_point;
                    return;
                }
                request(VehicleTrackReset::zone);
            }
        }
    }

    // 0x00426B03..0x00426B59: every 32nd frame, ground under the car or one
    // unit along +x.
    if ((ticks_ + kVehicleTrackBoundsPhase) % kVehicleTrackBoundsPeriod == 0) {
        double z = 0.0;
        const bool ground = ground_under(s.position.x, s.position.y, z) ||
                            ground_under(s.position.x + kVehicleTrackBoundsSideStep,
                                         s.position.y, z);
        t.bounds = ground ? VehicleTrackBoundsCheck::ground : VehicleTrackBoundsCheck::none;
        if (!ground) {
            request(VehicleTrackReset::out_of_bounds);
        }
    }

    // The two APPROXIMATIONS, when asked for.
    const double speed = vehicle_sqrt(dot(s.velocity, s.velocity));
    if (approx_.upside_down_steps > 0) {
        const Mat3 r = rotation(s.orientation);
        if (r.m[2][2] < 0.0 && speed < approx_.upside_down_speed) {
            upside_down_count_ += kVehicleTrackStepsPerTick;
        } else {
            upside_down_count_ = 0;
        }
        if (upside_down_count_ >= approx_.upside_down_steps) {
            request(VehicleTrackReset::upside_down);
            upside_down_count_ = 0;
        }
    }
    if (approx_.stuck_steps > 0) {
        double throttle = input.throttle;
        if (!finite(throttle)) {
            throttle = 0.0;
        }
        if (throttle >= approx_.stuck_throttle && speed < approx_.stuck_speed) {
            stuck_count_ += kVehicleTrackStepsPerTick;
        } else {
            stuck_count_ = 0;
        }
        if (stuck_count_ >= approx_.stuck_steps) {
            request(VehicleTrackReset::stuck);
            stuck_count_ = 0;
        }
    }
}

bool VehicleTrackSession::step(const VehicleInput& input, VehicleTrackTelemetry& telemetry,
                               double publication_dt,double solver_dt,bool select_request,
                               std::uint32_t steps_per_tick) {
    if (!valid_ || !started_ || steps_per_tick==0) {
        return false;
    }
    VehicleState s = state_;
    VehicleTrackTelemetry t;
    const RecordingGround recording(ground_);
    if (!model_->step(s, input, recording, t.model, publication_dt,solver_dt,select_request)) {
        return false;
    }
    t.wheel_surface = recording.surfaces();
    if (approx_.body_contact) {
        resolve_body(s, t);
    }
    if (!finite_state(s)) {
        return false;
    }
    ++steps_;
    t.step = steps_;
    if (steps_ % steps_per_tick == 0) {
        t.tick = true;
        ++ticks_;
        run_tick(s, input, t);
    }
    state_ = s;
    t.pending = pending_;
    t.resets = resets_;
    t.position = s.position;
    t.orientation = s.orientation;
    t.velocity = s.velocity;
    t.angular_velocity = s.angular_velocity;
    t.lap = progress_.lap;
    t.percent = progress_.coordinate.fraction * 100.0F;
    t.zone = progress_.zone;
    t.off_path = progress_.off_path;
    telemetry = t;
    return true;
}

} // namespace nfsu
