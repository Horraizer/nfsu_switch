#include "nfsu/vehicle_track.h"
#include "nfsu/race_contacts.h"

#include "nfsu/det_sqrt.h"
#include "nfsu/player_powertrain.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>

// The step's arithmetic must be IEEE-754 binary64 and binary32 without
// excess precision, as src/vehicle.cpp requires of the model it extends.
#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "nfsu_vehicle_track needs FLT_EVAL_METHOD 0 (no x87 excess precision)"
#endif

namespace nfsu {
bool VehicleTrackSession::meet_track(const RaceContactPose& history,const RaceTrackContactJob& job,
    float friction,RaceContactContext& context,RacePairContact& result) {
    if(!started_)return false;
    const auto saved=state_;const auto saved_ai=ai_;const bool saved_started=ai_started_;
    const auto record=context.record;
    if(resolve_swept_race_track_contact(state_,model_->spec(),history,job,friction,context,result)&&
       (!result.searched||continue_contact(result.remaining_a,&saved,context.refresh!=nullptr)))return true;
    state_=saved;ai_=saved_ai;ai_started_=saved_started;context.record=record;result={};return false;
}
bool VehicleTrackSession::meet_external(VehicleState& other,const VehicleSpec& spec,
    const RaceContactBox& self_box,const RaceContactBox& other_box,RacePairContact& result,
    const RaceContactPose* previous,const RaceContactPose* other_previous,RaceContactContext* context,const RaceContactJob* prepared,bool self_is_b,bool other_fixed_world) {
    if(!started_)return false;
    const auto saved=state_,saved_other=other;const auto saved_ai=ai_;
    const bool saved_started=ai_started_;
    const auto saved_record=context?context->record:ContactRecord{};
    const auto saved_seed=context?context->world_seed:0U;
    bool ok=false;
    if(self_is_b) {
        ok=previous&&other_previous?
            resolve_swept_race_pair(other,spec,other_box,state_,model_->spec(),self_box,result,*other_previous,*previous,context,prepared,{other_fixed_world,false}):
            resolve_race_pair(other,spec,other_box,state_,model_->spec(),self_box,result,nullptr,context);
    } else {
        ok=previous&&other_previous?
            resolve_swept_race_pair(state_,model_->spec(),self_box,other,spec,other_box,result,*previous,*other_previous,context,prepared,{false,other_fixed_world}):
            resolve_race_pair(state_,model_->spec(),self_box,other,spec,other_box,result,nullptr,context);
    }
    if(ok&&(!(result.applied||result.searched)||continue_contact(self_is_b?result.remaining_b:result.remaining_a,&saved,context&&context->refresh)))return true;
    state_=saved;other=saved_other;ai_=saved_ai;ai_started_=saved_started;
    if(context){context->record=saved_record;context->world_seed=saved_seed;}
    result={};return false;
}

bool VehicleTrackSession::meet(VehicleTrackSession& other,const RaceContactBox& self_box,
                              const RaceContactBox& other_box,RacePairContact& result,
                              const RaceContactPose* previous,const RaceContactPose* other_previous,RaceContactContext* context,const RaceContactJob* prepared) {
    if(this==&other || !started_ || !other.started_)return false;
    const auto saved=state_,saved_other=other.state_;
    const auto saved_ai=ai_,saved_other_ai=other.ai_;
    const bool saved_started=ai_started_,saved_other_started=other.ai_started_;
    const auto saved_record=context?context->record:ContactRecord{};
    const auto saved_seed=context?context->world_seed:0U;
    const bool ok=previous&&other_previous?
        resolve_swept_race_pair(state_,model_->spec(),self_box,other.state_,other.model_->spec(),other_box,result,*previous,*other_previous,context,prepared):
        resolve_race_pair(state_,model_->spec(),self_box,other.state_,other.model_->spec(),other_box,result,nullptr,context);
    if(ok&&(!(result.applied||result.searched)||(continue_contact(result.remaining_a,&saved,context&&context->refresh)&&other.continue_contact(result.remaining_b,&saved_other,context&&context->refresh))))return true;
    state_=saved;other.state_=saved_other;ai_=saved_ai;other.ai_=saved_other_ai;
    ai_started_=saved_started;other.ai_started_=saved_other_started;
    if(context){context->record=saved_record;context->world_seed=saved_seed;}
    result={};return false;
}

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

    void cast_wheels(const std::array<VehicleVec3,kVehicleWheelCount>& from,
        const VehicleVec3& direction,const std::array<double,kVehicleWheelCount>& length,
        std::array<VehicleGroundHit,kVehicleWheelCount>& hits) const override {
        std::array<VehicleTrackCast,kVehicleWheelCount> out{};
        inner_.probe_wheels(from,direction,length,out);
        for(std::size_t i=0;i<kVehicleWheelCount;++i) {
            hits[i]=out[i].hit;
            if(calls_<kVehicleWheelCount)surfaces_[calls_]=out[i].surface;
            ++calls_;
        }
    }

    bool strut_heights(const std::array<VehicleVec3,kVehicleWheelCount>& hubs,
        std::array<VehicleGroundHit,kVehicleWheelCount>& hits) const override {
        if(!inner_.uses_cached_struts())return false;
        std::array<VehicleTrackCast,kVehicleWheelCount> out{};
        inner_.probe_strut_heights(hubs,out);
        for(std::size_t i=0;i<kVehicleWheelCount;++i) {
            hits[i]=out[i].hit;
            if(calls_<kVehicleWheelCount)surfaces_[calls_]=out[i].surface;
            ++calls_;
        }
        return true;
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
    //472F30 support adjacency from the same authenticated240SX_CV records.
    hull.neighbors={
        {1,13,4,12,16},
        {5,13,0,4,25,26},
        {26,3,25,11},
        {7,33,26,14,31,15,2,11},
        {5,1,9,8,0,23,16},
        {9,25,1,4},
        {},
        {3,11,33,18,34},
        {9,4,19,23},
        {5,27,8,4,11,25,32,19},
        {},
        {7,27,9,34,20,21,32,2,25,3},
        {13,0,29,16,28},
        {1,26,0,28,31,12},
        {22,31,15,3},
        {35,33,14,22,3},
        {4,0,29,23,12},
        {35,22,18,24,36,29,30,28},
        {7,35,33,34,17,36},
        {9,8,32,23,30},
        {24,11,32,21},
        {34,24,20,11,36},
        {35,14,31,17,28,15},
        {8,19,4,29,30,16},
        {21,20,17,30,32,36},
        {5,1,9,26,2,11},
        {1,25,3,31,13,2},
        {9,11,32},
        {13,31,22,29,17,12},
        {23,30,16,17,28,12},
        {19,24,32,29,23,17},
        {26,13,14,28,22,3},
        {27,9,19,24,20,11,30},
        {7,35,3,18,15},
        {7,11,18,21,36},
        {22,17,18,33,15},
        {34,18,24,21,17},
    };
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

std::array<float,4> vehicle_track_ground_plane(const TrackContactWorld& world,std::size_t polygon) {
    const auto& p=world.collision.polygons[polygon];
    std::array<ContactVector,4> corners;
    for(unsigned i=0;i<4;++i)std::copy_n(p.corner[i],3,corners[i].begin());
    const auto n=race_track_polygon_normal(corners);
    const float d=(corners[0][2]*n[2]+corners[0][1]*n[1])+corners[0][0]*n[0];
    return {n[0],n[1],n[2],d};
}
namespace {
VehicleTrackCast ground_answer(VehicleTrackCast out,const VehicleVec3& from,
    const VehicleVec3& direction,double length,double squared) {
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
} // namespace

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
    out=ground_answer(out,from,direction,length,squared);
    if(out.hit.hit){out.hit.plane=vehicle_track_ground_plane(*world_,out.contact.polygon);out.hit.has_plane=true;}
    // 43FF39 copies polygon+2 to body+B; 440A97 returns that surface code.
    if(materials_&&out.hit.hit)out.hit.surface=world_->collision.polygons[out.contact.polygon].unknown0[2];
    return out;
}

void TrackVehicleGround::probe_wheels(const std::array<VehicleVec3,kVehicleWheelCount>& from,
    const VehicleVec3& direction,const std::array<double,kVehicleWheelCount>& length,
    std::array<VehicleTrackCast,kVehicleWheelCount>& out) const {
    const double squared=dot(direction,direction);
    bool valid=world_&&finite(direction)&&squared>0&&finite(squared);
    for(std::size_t i=0;i<kVehicleWheelCount;++i)
        valid=valid&&finite(from[i])&&finite(length[i])&&length[i]>0;
    if(!valid) {
        for(std::size_t i=0;i<kVehicleWheelCount;++i)out[i]=probe(from[i],direction,length[i]);
        return;
    }
    std::array<std::array<float,3>,kVehicleWheelCount> origins{},ends{};
    std::array<TrackContact,kVehicleWheelCount> contacts{};
    std::array<TrackContactStatus,kVehicleWheelCount> statuses{};
    for(std::size_t i=0;i<kVehicleWheelCount;++i) {
        narrow(from[i],origins[i].data());
        narrow(add(from[i],scale(direction,length[i])),ends[i].data());
    }
    query_track_contact_segments(*world_,origins.data(),ends.data(),kVehicleWheelCount,
        contacts.data(),statuses.data());
    for(std::size_t i=0;i<kVehicleWheelCount;++i) {
        VehicleTrackCast answer;answer.contact=contacts[i];answer.status=statuses[i];
        out[i]=ground_answer(answer,from[i],direction,length[i],squared);
        if(out[i].hit.hit){out[i].hit.plane=vehicle_track_ground_plane(*world_,out[i].contact.polygon);out[i].hit.has_plane=true;}
        if(materials_&&out[i].hit.hit)out[i].hit.surface=world_->collision.polygons[out[i].contact.polygon].unknown0[2];
    }
}

void TrackVehicleGround::probe_strut_heights(
    const std::array<VehicleVec3,kVehicleWheelCount>& hubs,
    std::array<VehicleTrackCast,kVehicleWheelCount>& out) const {
    for(std::size_t i=0;i<kVehicleWheelCount;++i) {
        auto& answer=out[i];answer={};
        if(!world_||!world_->valid||!finite(hubs[i])) {
            answer.surface=VehicleTrackSurface::refused;continue;
        }
        const bool road=query_track_ground(*world_,hubs[i],strut_cache_,answer.contact);
        const float x=float(hubs[i].x),y=float(hubs[i].y),limit=float(hubs[i].z)+1.F;
        const auto rank=[limit](float height){return height<limit?height:height-50000.F;};
        float best=road?rank(answer.contact.point[2]):0.F;
        if(support_faces_)for(const auto& face:*support_faces_) {
            if(face.plane[2]<=0||x<face.low[0]||x>face.high[0]||y<face.low[1]||y>face.high[1])continue;
            bool inside=true;
            for(unsigned j=0;j<4;++j) {
                const auto& a=face.corners[j];const auto& b=face.corners[(j+1)%4];
                if((x-a[0])*(b[1]-a[1])-(b[0]-a[0])*(y-a[1])>0.F){inside=false;break;}
            }
            if(!inside)continue;
            const auto& p=face.plane;
            const float height=-((x*p[0]+y*p[1]-p[3])/p[2]);
            const float candidate=rank(height);
            if((road||answer.hit.hit)&&candidate<=best)continue;
            best=candidate;
            answer.status=TrackContactStatus::hit;
            answer.surface=VehicleTrackSurface::road;
            answer.hit.hit=true;answer.hit.has_plane=true;answer.hit.plane=p;
            answer.hit.normal={p[0],p[1],p[2]};
            // WorldObjectCollisionBody +54 returns material zero.
        }
        if(answer.hit.hit)continue;
        if(!road) {
            std::array<std::size_t,4> cells{};std::size_t count=0;
            if(!query_track_collision_cells(world_->collision,float(hubs[i].x),float(hubs[i].y),
                float(hubs[i].x),float(hubs[i].y),cells.data(),cells.size(),count)) {
                answer.surface=VehicleTrackSurface::refused;continue;
            }
            answer.surface=count?VehicleTrackSurface::clear:VehicleTrackSurface::outside;
            answer.status=count?TrackContactStatus::clear:TrackContactStatus::outside;continue;
        }
        answer.status=TrackContactStatus::hit;answer.surface=VehicleTrackSurface::road;
        answer.hit.hit=true;answer.hit.normal=widen(answer.contact.normal);
        answer.hit.plane=vehicle_track_ground_plane(*world_,answer.contact.polygon);
        answer.hit.has_plane=true;
        if(materials_)answer.hit.surface=world_->collision.polygons[answer.contact.polygon].unknown0[2];
    }
}

bool TrackVehicleGround::strut_heights(
    const std::array<VehicleVec3,kVehicleWheelCount>& hubs,
    std::array<VehicleGroundHit,kVehicleWheelCount>& hits) const {
    if(!uses_cached_struts())return false;
    std::array<VehicleTrackCast,kVehicleWheelCount> out{};probe_strut_heights(hubs,out);
    for(std::size_t i=0;i<kVehicleWheelCount;++i)hits[i]=out[i].hit;
    return true;
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
                                         const VehicleHull& hull,TrackStrutQuery struts)
    : model_(&model), course_(&course), approx_(approx), ground_(course.world,true,struts,&course.support_faces) {
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
    ground_.reset_strut_cache();
    ai_={};ai_started_=false;
    last_input_={};last_solver_dt_=kVehicleStepSeconds;last_ai_gear_request_=-1;
    owner_recovery_start_=0;
    last_ghost_overlap_=0;
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

namespace {
float recovery_age(std::uint32_t now,std::uint32_t then) noexcept {
    const auto delta=now-then;
    std::int32_t elapsed;
    std::memcpy(&elapsed,&delta,sizeof(elapsed));
    return float(elapsed)*.0002500000118743628F;
}
}

bool VehicleTrackSession::owner_recovery_active(std::int32_t now) const noexcept {
    if(owner_recovery_start_==0||owner_recovery_start_==std::numeric_limits<std::int32_t>::max())
        return false;
    //4280A0 compares the wrapped world clock at 4000 ticks per second.
    return recovery_age(std::uint32_t(now),std::uint32_t(owner_recovery_start_))<2.F;
}

void VehicleTrackSession::extend_owner_recovery(std::int32_t now) noexcept {
    if(pending_!=VehicleTrackReset::none||last_ghost_overlap_==0||
       last_ghost_overlap_==0x7fffffffU||!owner_recovery_active(now))return;
    //4266D9 reads the timestamp from the previous ghost-pair query.
    const auto tick=std::uint32_t(now);
    const auto start=std::uint32_t(owner_recovery_start_);
    const float age=recovery_age(tick,last_ghost_overlap_);
    if(recovery_age(tick,start)>1.5F&&age>=0.F&&age<.5F) {
        const auto extended=start+2000U;
        std::memcpy(&owner_recovery_start_,&extended,sizeof(extended));
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

// The ground a reset places a car on: like ground_under, but it walks past
// each hit that is not a drivable road, so on the multi level spots of the
// newer circuits the car lands on the road rather than the roof or overpass
// deck above it (witnessed on track 1006 reverse, node 4: reset to py 57.89
// on a deck while the road below it is the drivable surface). The bounds
// check keeps ground_under's any-surface rule.
bool VehicleTrackSession::ground_under_road(double x, double y, double& z) const {
    const float from[3] = {static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(column_top_)};
    const float to[3] = {from[0], from[1], static_cast<float>(column_bottom_)};
    TrackContact contact;
    if (query_track_contact_segment(course_->world, from, to, contact) !=
        TrackContactStatus::hit) {
        return false;
    }
    for (int surface = 0; surface < 8; ++surface) {
        if (contact.kind == TrackContactKind::road && contact.up > 0.5F) break;
        const float next_from[3] = {from[0], from[1],
                                    contact.point[2] - 0.05F};
        if (next_from[2] <= to[2]) break;
        TrackContact below;
        if (query_track_contact_segment(course_->world, next_from, to, below) !=
            TrackContactStatus::hit)
            break;
        contact = below;
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

bool VehicleTrackSession::resolve_body(VehicleState& s, VehicleTrackTelemetry& t) const {
    const auto axes = race_contact_axes(s);
    const VehicleVec3 centre = s.position;
    float from[3];
    narrow(centre, from);

    // Each hull point's probe: the segment from the centre of mass, inside
    // the hull, out to the point. What it meets lies between the two, so
    // the point is on or beyond that surface, by the depth along the
    // surface's normal, which faces the centre.
    std::array<HullHit,256> hits;
    std::size_t hit_count=0;
    // Constructor validates the 256-point bound. Transform in the original
    // order, then share validated leaf candidates across this body's rays.
    std::array<VehicleVec3,256> points;
    std::array<std::array<float,3>,256> endpoints;
    std::array<TrackContact,256> contacts;
    std::array<TrackContactStatus,256> statuses;
    for(std::size_t k=0;k<hull_.size();++k) {
        float transformed[3];
        const float local[3]={float(hull_[k].x),float(hull_[k].y),float(hull_[k].z)};
        for(unsigned j=0;j<3;++j)
            transformed[j]=((local[2]*axes[2][j]+local[1]*axes[1][j])
                +local[0]*axes[0][j])+from[j];
        points[k]=widen(transformed);
        narrow(points[k],endpoints[k].data());
    }
    query_track_contact_fan(course_->world,from,endpoints.data(),hull_.size(),
                            contacts.data(),statuses.data());
    for (std::size_t k = 0; k < hull_.size(); ++k) {
        const VehicleVec3 point=points[k];
        const TrackContact& contact=contacts[k];
        if (statuses[k] != TrackContactStatus::hit) {
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
        hits[hit_count++]=hit;
    }
    t.hull_hits = static_cast<std::uint32_t>(hit_count);
    if (hit_count==0) {
        return true;
    }
    // The game pairs the car's body with each polygon's body; here each
    // polygon is one contact, at its deepest point (the lowest point index
    // on a tie), and the contacts are taken in polygon order.
    std::sort(hits.begin(), hits.begin()+hit_count, [](const HullHit& a, const HullHit& b) {
        if (a.polygon != b.polygon) {
            return a.polygon < b.polygon;
        }
        if (a.depth != b.depth) {
            return a.depth > b.depth;
        }
        return a.point < b.point;
    });

    const double unit = model_->spec().approx.unit_scale;
    const VehicleVec3 forward = widen(axes[0].data()), up = widen(axes[2].data());

    VehicleVec3 moved;
    VehicleVec3 applied_total;
    for (std::size_t i = 0; i < hit_count; ++i) {
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

        // Observe the current fan arm for audio; the response below owns
        // the binary32 closing-velocity decision.
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
            audio_contact->normal = hit.normal;
            const auto audio_tangent = sub(point_velocity, scale(hit.normal, approach));
            audio_contact->slide_mps = vehicle_sqrt(dot(audio_tangent, audio_tangent));
        }
        ContactGeometry geometry;
        // The fan currently supplies an admitted polygon and hull arm. The
        // prepared response is shared with the original binary32 solver;
        // convex witness preparation and world queue ordering remain separate.
        const auto point=add(s.position,arm);
        if(audio_contact)audio_contact->point=point;
        narrow(scale(hit.normal,-1),geometry.normal.data());
        narrow(point,geometry.point.data());
        const auto flags=course_->world.collision.polygons[hit.polygon].unknown0[3];
        const float friction=(flags&0xAU)?0.3F:0.6F; //46A910
        ContactResponse response;
        const auto momentum=s.rigid_body.momentum;
        if(!apply_race_track_contact(s,model_->spec(),geometry,friction,response))return false;
        if(!response.closing)continue;
        ++t.impulses;
        event.contact_impulse=scale(widen(response.current_impulse.data()),unit);
        if(audio_contact)audio_contact->impulse=widen(response.current_impulse.data());
        const VehicleVec3 applied={
            double(s.rigid_body.momentum[0]-momentum[0])*unit,
            double(s.rigid_body.momentum[1]-momentum[1])*unit,
            double(s.rigid_body.momentum[2]-momentum[2])*unit};
        applied_total = add(applied_total, applied);
        event.applied_impulse = applied; event.accumulated_impulse = applied_total;
        event.velocity_after = s.velocity; event.angular_after = s.angular_velocity;
    }
    t.separation = moved;
    t.impulse = applied_total;
    return true;
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
        if (!ground_under_road(static_cast<double>(nodes[n].x), static_cast<double>(nodes[n].y), z)) {
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
    if (!ground_under_road(static_cast<double>(zone.point[0]), static_cast<double>(zone.point[1]), z)) {
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
    const auto world_tick=input.world_clock_valid?input.world_clock:static_cast<std::int32_t>((ticks_*4000)/60);
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
        //4264D0/4314A0 arm the owner timer after a successful reset.
        if(reset_to_node(s,reason))owner_recovery_start_=world_tick;
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
                    //427ED0 invokes427E60 with its recovery flag set.
                    if(reset_to_point(s,progress_.zone))owner_recovery_start_=world_tick;
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

bool VehicleTrackSession::prepare_grid(unsigned frames) {
    if(!valid_ || !started_ || steps_!=0) return false;
    if(state_.velocity.x!=0 || state_.velocity.y!=0 || state_.velocity.z!=0) return true;
    // Countdown currently omits the original pre-race world simulation. Run
    // the existing body/strut solver during loading instead of presenting an
    // unqueried, flat-ground reset pose until GO. No force constants change.
    auto prepared=*this;prepared.preparing_grid_=true;
    VehicleInput hold;hold.brake=1;hold.handbrake=true;
    hold.road_steering=true;hold.road_angle_word_valid=true;
    VehicleTrackTelemetry telemetry;
    for(unsigned frame=0;frame<frames;++frame) {
        if(ai_configured_) {
            if(!prepared.step_ai(hold,telemetry)) return false;
        } else for(unsigned slice=0;slice<4;++slice) {
            // Let the body find its terrain attitude before ordinary rest
            // detection can freeze the unqueried reset pose on a slope.
            if(!model_->step(prepared.state_,hold,ground_,telemetry.model,
                slice==3?1.0/60.0:0.0,1.0/240.0,slice==0,false)) return false;
            if(approx_.body_contact && !prepared.resolve_body(prepared.state_,telemetry))return false;
        }
        if(prepared.resets_!=0) return false;
    }
    // Only the physical placement and contact history leave preparation.
    // Route/lap/reset counters and AI driver history still begin at zero.
    state_=prepared.state_;state_.step=0;state_.simulation_seconds=0;
    ai_=prepared.ai_;ai_.slices=0;ai_.ground_queries=0;ai_started_=prepared.ai_started_;
    return true;
}

bool VehicleTrackSession::step_grid_engine(double throttle, VehicleTrackTelemetry& telemetry,
                                           double publication_dt, double solver_dt,bool drag,std::int32_t clock) {
    if (!valid_ || !started_ || ai_configured_ || steps_ != 0 ||
        !finite(throttle) || throttle < 0 || throttle > 1 ||
        !finite(publication_dt) || publication_dt < 0 ||
        !finite(solver_dt) || solver_dt <= 0) return false;
    auto next=state_;
    const auto& h=model_->spec().handling;
    // Original stage setter434B50 calls human vfunc1C/20;4616BE requests
    // neutral with handbrake while held. This port enters countdown directly,
    // after the omitted flyby/prerace, so begin with settled neutral here.
    next.gear=kVehicleGearNeutral;next.published_gear=next.gear;
    next.transmission.requested_gear=next.gear;
    next.transmission.mode=3;next.transmission.clutch=1;
    next.transmission.previous_slip_rpm=0;next.transmission.wheel_engine_speed=0;
    next.transmission.shift_pending=false;
    next.engine.throttle_multiplier=1;next.engine.feedback_torque=0;
    const float crank=player_engine_torque(h,next.engine,next.gear,float(throttle));
    player_engine_integrate(h,next.engine,crank,float(solver_dt));
    next.simulation_seconds+=solver_dt;
    if(drag)player_drag_heat_update(h,next.engine,next.transmission,next.gear,clock,true);
    //46CC5E/46CC8C read the world clock, held constant across solver slices.
    player_engine_cutoff_update(h,next.gear,next.engine,
        float(clock)*.00025F);
    if(publication_dt>0) next.engine_rpm=player_engine_publish_rpm(h,next.engine,
        float(next.engine_rpm),float(publication_dt));
    if(!finite_state(next)) return false;
    VehicleTrackTelemetry t;
    t.position=next.position;t.orientation=next.orientation;
    t.velocity=next.velocity;t.angular_velocity=next.angular_velocity;
    t.model.position=next.position;t.model.velocity=next.velocity;
    t.model.engine_rpm=next.engine_rpm;t.model.engine=next.engine;
    t.model.transmission=next.transmission;t.model.gear=next.gear;
    t.model.published_gear=next.published_gear;
    t.model.throttle=throttle;t.model.handbrake=true;
    t.model.engine_torque=double(crank)*1000.0;
    t.model.simulation_seconds=next.simulation_seconds;t.model.solver_dt=solver_dt;
    state_=next;telemetry=t;return true;
}

void VehicleTrackSession::release_grid_engine(bool drag,unsigned category,float window_scale) noexcept {
    // Original461510 clears the hold and4615B0 restores the request policy.
    // Keep physical RPM and clutch phase; normal gearbox update engages first.
    if(!ai_configured_ && state_.gear==kVehicleGearNeutral) {
        //46E8FB..46E925 evaluates the first request with PHYSICAL engine RPM.
        // The held-grid adapter bypasses that request consumer, so publish its
        // quality here exactly once. The existing clutch/46DEC3 tail consumes it.
        if(drag)state_.transmission.shift_quality=player_drag_shift_quality(
            state_.engine.angular_speed*kPlayerRadSToRpm,
            model_->request_upshift_rpm()[kVehicleGearNeutral],category,kVehicleGearNeutral,window_scale);
        state_.transmission.requested_gear=kVehicleGearFirst;
    }
}

void VehicleTrackSession::publish_world_acceleration(const VehicleVec3& before,float dt) noexcept {
    player_publish_owner_acceleration(state_.owner_motion,before,state_.velocity,dt);
}

void VehicleTrackSession::publish_contact_count(unsigned count,std::uint32_t tick) noexcept {
    // The fixed race pool has at most9 other cars; this signed field is the
    // original body+1C record count, not a severity or a permanent crash flag.
    state_.collision_body_flag=static_cast<std::int8_t>(count);
    update_car_collision_state(state_.collision_state,state_.collision_support_since,
        state_.published_support,state_.collision_body_flag,
        double(static_cast<std::int32_t>(tick))*double(.00025F));
}

bool VehicleTrackSession::refresh_contact(VehicleState& sampled,const VehicleState* published_actor,float interval) {
    if(ai_configured_) {
        VehicleTrackTelemetry telemetry;
        const auto previous=state_;state_=sampled;
        const bool ok=step_ai(last_input_,telemetry,last_ai_gear_request_,0,published_actor,true,false,interval);
        if(ok)sampled=state_;
        state_=previous;return ok;
    }
    VehicleTelemetry telemetry;
    if(published_actor) {
        sampled.owner_motion=published_actor->owner_motion;
        sampled.owner_traction=published_actor->owner_traction;
    }
    return model_->step(sampled,last_input_,ground_,telemetry,0,0,false,true,true,true,interval);
}

bool VehicleTrackSession::continue_contact(double seconds,const VehicleState* published_actor,bool refreshed) {
    if(!std::isfinite(seconds)||seconds<0)return false;
    VehicleTrackTelemetry t;
    if(ai_configured_)return step_ai(last_input_,t,last_ai_gear_request_,float(seconds),published_actor,false,refreshed);
    auto next=state_;const auto saved_step=next.step;const auto saved_time=next.simulation_seconds;
    if(published_actor) {
        next.owner_motion=published_actor->owner_motion;
        next.owner_traction=published_actor->owner_traction;
    }
    const RecordingGround recording(ground_);
    //469460 refreshes component forces before the outer4774A0 policy latch.
    //The first resumed derivative consumes those forces even when state1
    //then disables aero. Do not replace them with a new-policy force sample.
    if(!refreshed&&!model_->step(next,last_input_,recording,t.model,0,0,false,true,true,true))return false;
    update_car_collision_state(next.collision_state,next.collision_support_since,
        next.published_support,next.collision_body_flag,
        double(last_input_.world_clock_valid?last_input_.world_clock:static_cast<std::int32_t>((ticks_*4000)/60))*double(.00025F));
    next.simulation_seconds-=seconds;
    next.rigid_body.linear_damping=next.collision_state!=1;
    next.rigid_body.angular_damping=next.collision_state!=1;
    const auto& damping_actor=published_actor?*published_actor:next;
    player_race_body_policy(next.rigid_body,last_input_.race_active,float(last_input_.throttle),
        damping_actor.published_support,damping_actor.rigid_body.axes[0][2]);
    next.alternate_yaw_active=next.collision_state!=1&&next.published_support!=0;
    //46B5E0 invokes the outer physics even at zero remaining time.
    next.aero_ready=false;
    //477D56 uses the original human interval, truncation and an added slice.
    //A caller's last ordinary solver step does not select this schedule.
    const float ratio=float(seconds)/.004399999976158142F;
    if(!std::isfinite(ratio)||ratio>=float(std::numeric_limits<int>::max()))return false;
    const unsigned count=static_cast<unsigned>(ratio)+1;
    const double dt=float(seconds)/float(count);
    for(unsigned i=0;i<count;++i) {
        if(!model_->step(next,last_input_,recording,t.model,0,dt,false,true,true,false,0,seconds))return false;
        if(approx_.body_contact && !deferred_body_contacts_ && !resolve_body(next,t))return false;
        if(!finite_state(next))return false;
    }
    //478251 calls4789D0 once after the resumed slices, including zero time.
    //Publish fresh wheel support before the next queued collision:46C425
    //must not project its arm using support from before the previous hit.
    next.published_support=0;
    for(const auto& wheel:next.wheels)next.published_support+=wheel.contact?1U:0U;
    next.published_gear=next.gear;
    const auto& forward=next.rigid_body.axes[0];
    player_publish_owner_motion(next.owner_motion,next.rigid_body.velocity,
        next.rigid_body.angular_velocity,next.rigid_body.axes);
    next.owner_speeds=player_publish_owner_speeds(next.velocity,{forward[0],forward[1],forward[2]});
    next.owner_slip_angle=player_owner_slip_angle(next.velocity,next.owner_motion.heading);
    next.engine_rpm=player_engine_publish_rpm(model_->spec().handling,next.engine,
        float(next.engine_rpm),float(seconds));
    // Continuation belongs to this already-counted world frame. It does not
    // sample controls, publish another lap event or advance the world clock.
    next.step=saved_step;next.simulation_seconds=saved_time;state_=next;return true;
}

bool VehicleTrackSession::step(const VehicleInput& input, VehicleTrackTelemetry& telemetry,
                               double publication_dt,double solver_dt,bool select_request,
                               std::uint32_t steps_per_tick,bool explicit_frame_end) {
    if (!valid_ || !started_ || steps_per_tick==0) {
        return false;
    }
    VehicleState s = state_;
    VehicleTrackTelemetry t;
    const RecordingGround recording(ground_);
    if(select_request)update_car_collision_state(s.collision_state,s.collision_support_since,
        s.published_support,s.collision_body_flag,double(input.world_clock_valid?input.world_clock:static_cast<std::int32_t>((ticks_*4000)/60))*double(.00025F));
    auto applied=input;
    const auto world_tick=input.world_clock_valid?input.world_clock:static_cast<std::int32_t>((ticks_*4000)/60);
    if(select_request)extend_owner_recovery(world_tick);
    applied.owner_traction_suppressed|=owner_recovery_active(world_tick);
    if (!model_->step(s, applied, recording, t.model, publication_dt,solver_dt,select_request,true,false,false,0,solver_dt*steps_per_tick)) {
        return false;
    }
    t.wheel_surface = recording.surfaces();
    if (approx_.body_contact && !deferred_body_contacts_) {
        if(!resolve_body(s, t))return false;
    }
    if (!finite_state(s)) {
        return false;
    }
    ++steps_;
    t.step = steps_;
    if (explicit_frame_end ? publication_dt>0 : steps_ % steps_per_tick == 0) {
        t.tick = true;
        ++ticks_;
        run_tick(s, input, t);
    }
    state_ = s;
    last_input_=applied;last_solver_dt_=solver_dt;
    t.pending = pending_;
    t.resets = resets_;
    t.position = s.position;
    t.orientation = s.orientation;
    t.velocity = s.velocity;
    t.angular_velocity = s.angular_velocity;
    t.model.rigid_body=s.rigid_body;t.model.rigid_body_ready=s.rigid_body_ready;
    t.model.rigid_orientation=s.rigid_orientation;
    t.model.steering_word=s.steering_word;t.model.steering_word_ready=s.steering_word_ready;
    t.lap = progress_.lap;
    t.percent = progress_.coordinate.fraction * 100.0F;
    t.zone = progress_.zone;
    t.off_path = progress_.off_path;
    telemetry = t;
    return true;
}

} // namespace nfsu
