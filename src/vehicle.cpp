#include "nfsu/vehicle.h"
#include "nfsu/player_powertrain.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

// The step's determinism rests on every double operation being one IEEE-754
// binary64 operation. FLT_EVAL_METHOD 0 is what says so; the x87 unit of a
// 32-bit x86 build evaluates in extended precision (FLT_EVAL_METHOD 2) and
// would round differently from x86-64 and AArch64.
#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "nfsu_vehicle needs FLT_EVAL_METHOD == 0: IEEE binary64 without excess precision"
#endif

namespace nfsu {

namespace {

// ---------------------------------------------------------------------------
// Recovered constants of speed.exe (docs/VEHICLE.md, "What was recovered")
// ---------------------------------------------------------------------------

// 0x007000AC, 9.8f: the gravity the weight rule at 0x00463C51 multiplies the
// mass by, and the value 0x00475202 stores into the engine object.
constexpr double kGravity = static_cast<double>(9.8F);

// 0x006CCB78, 9.549304f = 60 / (2 pi): wheel rad/s to engine rpm at
// 0x0047586F and 0x00475896.
constexpr double kRadPerSecondToRpm = static_cast<double>(9.549304F);

// 0x0046D1C0: the shift search starts halfway between idle and redline,
// steps by 50 rpm (0x006B7244), gives redline - 100 (0x006CC9F8) when no
// crossing is found, and skips a ratio no larger than 1e-5 (0x006CCB7C).
constexpr double kShiftSearchStep = static_cast<double>(50.0F);
constexpr double kShiftRedlineMargin = static_cast<double>(100.0F);
constexpr double kShiftRatioEpsilon = static_cast<double>(1e-5F);

// 0x006FFF14: the anti-roll partner of each wheel, 1 0 3 2.
constexpr std::size_t kAntiRollPartner[kVehicleWheelCount] = {1, 0, 3, 2};

// ---------------------------------------------------------------------------
// Vector helpers. Every one is a fixed sequence of basic operations.
// ---------------------------------------------------------------------------

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

bool finite(double x) noexcept {
    // Not std::isfinite: the step calls nothing from <cmath>. x - x is 0 for
    // every finite x and NaN for infinities and NaN.
    return (x - x) == 0.0;
}

bool finite(const VehicleVec3& v) noexcept {
    return finite(v.x) && finite(v.y) && finite(v.z);
}

bool state_finite(const VehicleState& s) noexcept {
    bool ok = finite(s.simulation_seconds) && s.simulation_seconds>=0.0 &&
              finite(s.position) && finite(s.velocity) && finite(s.angular_velocity) &&
              finite(s.orientation.w) && finite(s.orientation.x) && finite(s.orientation.y) &&
              finite(s.orientation.z) && finite(s.steer_angle) && finite(s.engine_rpm) &&
              finite(s.wheel_rpm);
    ok = ok && finite(s.engine.angular_speed) && finite(s.engine.feedback_torque) &&
         finite(s.engine.throttle_multiplier) && finite(s.engine.cutoff_until) &&
         finite(s.transmission.clutch) && finite(s.transmission.previous_slip_rpm) &&
         finite(s.transmission.wheel_engine_speed) && s.transmission.mode>=0 && s.transmission.mode<=3;
    ok=ok&&finite(s.transmission.up_offset)&&finite(s.transmission.down_offset);
    ok=ok&&finite(s.owner_speeds.forward)&&finite(s.owner_speeds.magnitude)&&s.owner_speeds.magnitude>=0.0F;
    ok=ok&&finite(s.prepared_force)&&finite(s.prepared_torque)&&s.body_rest_state<=2&&
       s.published_support<=4&&s.published_gear>=kVehicleGearReverse&&s.published_gear<static_cast<int>(kVehicleGearSlots);
    const auto& body=s.rigid_body;
    for(const auto& vector:{body.position,body.momentum,body.angular_momentum,body.velocity,
                           body.angular_velocity,body.force,body.torque})
        for(float value:vector) ok=ok&&finite(value);
    for(const auto& matrix:{body.axes,body.snapshot_axes})
        for(const auto& vector:matrix) for(float value:vector) ok=ok&&finite(value);
    ok=ok&&finite(body.adaptive_damping)&&finite(body.yaw_torque_limit)&&body.yaw_torque_limit>=0.0F&&
       finite(s.rigid_orientation.w)&&finite(s.rigid_orientation.x)&&
       finite(s.rigid_orientation.y)&&finite(s.rigid_orientation.z);
    for (const VehicleWheelState& w : s.wheels) {
        ok = ok && finite(w.spin) && finite(w.compression) && finite(w.previous_compression) &&
             finite(w.history_torque) && w.spin_direction>=0 && w.spin_direction<=2&&
             finite(w.prepared_torque)&&finite(w.prepared_force_long)&&finite(w.prepared_slip);
    }
    ok=ok&&finite(s.aero.speed_divisor)&&finite(s.aero.drag_scale)&&
       finite(s.aero.processed_throttle)&&finite(s.aero.support_kind);
    // Noninitial479160 scales would also change body, engine and wheels.
    // Reject them in this explicitly bounded VehicleModel adapter.
    if(s.aero_ready) ok=ok&&s.aero.speed_divisor==1.0F&&s.aero.drag_scale==1.0F&&
        s.aero.processed_throttle>=0.0F&&s.aero.processed_throttle<=1.0F&&
        s.aero.support_kind>=0.0F&&s.aero.support_kind<=1.0F;
    for(float value:s.aero_preloads) ok=ok&&finite(value);
    return ok;
}

double absolute(double x) noexcept {
    return x < 0.0 ? -x : x;
}

double clamp(double x, double lo, double hi) noexcept {
    return x < lo ? lo : (x > hi ? hi : x);
}

double sanitize(double x, double lo, double hi) noexcept {
    return finite(x) ? clamp(x, lo, hi) : 0.0;
}

struct Mat3 {
    // Row-major; columns are the body's +x, +y and +z axes in the world.
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

VehicleQuat multiply(const VehicleQuat& a, const VehicleQuat& b) noexcept {
    VehicleQuat q;
    q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return q;
}

VehicleQuat normalized(const VehicleQuat& q) noexcept {
    const double n = vehicle_sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(n > 0.0)) {
        return VehicleQuat{};
    }
    VehicleQuat r;
    r.w = q.w / n;
    r.x = q.x / n;
    r.y = q.y / n;
    r.z = q.z / n;
    return r;
}

PlayerRigidVector rigid_vector(const VehicleVec3& v) noexcept {
    return {{static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z)}};
}
VehicleVec3 public_vector(const PlayerRigidVector& v) noexcept {
    return v3(v[0],v[1],v[2]);
}
Mat3 rigid_rotation(const PlayerRigidMatrix& axes) noexcept {
    Mat3 r;
    for(int c=0;c<3;++c) for(int row=0;row<3;++row) r.m[row][c]=axes[c][row];
    return r;
}
PlayerRigidMatrix rigid_axes(const Mat3& r) noexcept {
    PlayerRigidMatrix axes{};
    for(int c=0;c<3;++c) for(int row=0;row<3;++row) axes[c][row]=static_cast<float>(r.m[row][c]);
    return axes;
}
bool same_quaternion(const VehicleQuat& a,const VehicleQuat& b) noexcept {
    return a.w==b.w&&a.x==b.x&&a.y==b.y&&a.z==b.z;
}
bool same_vector(const VehicleVec3& a,const PlayerRigidVector& b) noexcept {
    return static_cast<float>(a.x)==b[0]&&static_cast<float>(a.y)==b[1]&&static_cast<float>(a.z)==b[2];
}
VehicleQuat rigid_quaternion(const PlayerRigidMatrix& axes,const VehicleQuat& previous) noexcept {
    // Public render pose only. The solver retains the original float matrix.
    const Mat3 r=rigid_rotation(axes);
    VehicleQuat q;
    const double trace=r.m[0][0]+r.m[1][1]+r.m[2][2];
    if(trace>0.0) {
        const double s=2.0*vehicle_sqrt(trace+1.0);
        q.w=0.25*s;q.x=(r.m[2][1]-r.m[1][2])/s;
        q.y=(r.m[0][2]-r.m[2][0])/s;q.z=(r.m[1][0]-r.m[0][1])/s;
    } else {
        int i=r.m[1][1]>r.m[0][0]?1:0;
        if(r.m[2][2]>r.m[i][i]) i=2;
        const int j=(i+1)%3,k=(i+2)%3;
        const double s=2.0*vehicle_sqrt(1.0+r.m[i][i]-r.m[j][j]-r.m[k][k]);
        double xyz[3]{};
        xyz[i]=0.25*s;xyz[j]=(r.m[j][i]+r.m[i][j])/s;xyz[k]=(r.m[k][i]+r.m[i][k])/s;
        q.w=(r.m[k][j]-r.m[j][k])/s;q.x=xyz[0];q.y=xyz[1];q.z=xyz[2];
    }
    q=normalized(q);
    if(q.w*previous.w+q.x*previous.x+q.y*previous.y+q.z*previous.z<0.0) {
        q.w=-q.w;q.x=-q.x;q.y=-q.y;q.z=-q.z;
    }
    return q;
}
void rigid_public_cache(VehicleState& s) noexcept {
    s.position=public_vector(s.rigid_body.position);
    s.velocity=public_vector(s.rigid_body.velocity);
    s.angular_velocity=public_vector(s.rigid_body.angular_velocity);
}
void rigid_import_public(const VehicleHandling& h,VehicleState& s) noexcept {
    auto& body=s.rigid_body;
    const bool pose_changed=!s.rigid_body_ready||!same_quaternion(s.orientation,s.rigid_orientation);
    const bool velocity_changed=pose_changed||!same_vector(s.velocity,body.velocity);
    const bool omega_changed=pose_changed||!same_vector(s.angular_velocity,body.angular_velocity);
    if(pose_changed) body.axes=rigid_axes(rotation(s.orientation));
    body.position=rigid_vector(s.position);
    // Spawn, teleport and explicit public velocity edits are port adapters.
    // Ordinary integration keeps P/L rather than reconstructing them from caches.
    if(velocity_changed) {
        const auto v=rigid_vector(s.velocity);
        for(int k=0;k<3;++k) body.momentum[k]=v[k]*h.mass;
    }
    if(omega_changed) {
        const Mat3 r=rigid_rotation(body.axes);
        VehicleVec3 local=r.apply_transpose(s.angular_velocity);
        local.x*=h.inertia[0];local.y*=h.inertia[1];
        local.z*=h.inertia[2]*(s.alternate_yaw_active?h.alternate_yaw_scale:1.0F);
        body.angular_momentum=rigid_vector(r.apply(local));
    }
    body.alternate_yaw=s.alternate_yaw_active;
    body.gravity=s.body_gravity;body.yaw_torque_limit=h.yaw_torque_limit;
    if(velocity_changed||omega_changed) {
        player_rigid_body_refresh(h,body);rigid_public_cache(s);
    }
    s.rigid_body_ready=true;s.rigid_orientation=s.orientation;
}

// Little-endian readers and writers of a record, independent of the host.
float read_f32(const unsigned char* p) noexcept {
    const std::uint32_t bits = static_cast<std::uint32_t>(p[0]) |
                               (static_cast<std::uint32_t>(p[1]) << 8) |
                               (static_cast<std::uint32_t>(p[2]) << 16) |
                               (static_cast<std::uint32_t>(p[3]) << 24);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t read_u32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_u32(unsigned char* p, std::uint32_t v) noexcept {
    p[0] = static_cast<unsigned char>(v & 0xFFU);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFU);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFFU);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFFU);
}

void write_f32(unsigned char* p, float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32(p, bits);
}

std::uint32_t f32_bits(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool finite_f32(float value) noexcept {
    return (f32_bits(value) & 0x7F800000U) != 0x7F800000U;
}

// Visits every float field of the handling with its record offset. The one
// list decode, encode, check and compare all walk, so they cannot disagree
// about where a field lives.
template <typename Handling, typename Visitor>
void visit_floats(Handling& h, Visitor&& visit) {
    for (std::size_t i = 0; i < 3; ++i) {
        visit(kVehicleCentreOfMassOffset + i * 4, h.centre_of_mass[i]);
    }
    for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
        const std::size_t base = kVehicleWheelOffset + k * kVehicleWheelStride;
        visit(base + 0x00, h.wheels[k].longitudinal);
        visit(base + 0x04, h.wheels[k].lateral);
        visit(base + 0x08, h.wheels[k].vertical);
        visit(base + 0x10, h.wheels[k].radius);
    }
    for (std::size_t a = 0; a < 2; ++a) {
        const std::size_t base = kVehicleTyreOffset + a * kVehicleAxleStride;
        visit(base + 0x00, h.tyres[a].slip_scale);
        visit(base + 0x04, h.tyres[a].lateral_load_scale);
        visit(base + 0x08, h.tyres[a].lateral_scale);
        visit(base + 0x0C, h.tyres[a].static_friction);
        visit(base + 0x10, h.tyres[a].sliding_friction);
    }
    visit(kVehicleMassOffset, h.mass);
    visit(kVehicleLengthOffset, h.length);
    visit(kVehicleWidthOffset, h.width);
    visit(kVehicleHeightOffset, h.height);
    for (std::size_t i = 0; i < 3; ++i) {
        visit(kVehicleInertiaOffset + i * kVehicleInertiaStride, h.inertia[i]);
    }
    visit(kVehicleAlternateYawScaleOffset, h.alternate_yaw_scale);
    for (std::size_t a = 0; a < 2; ++a) {
        const std::size_t base = kVehicleStrutOffset + a * kVehicleAxleStride;
        visit(base + 0x00, h.struts[a].progressive);
        visit(base + 0x04, h.struts[a].spring);
        visit(base + 0x08, h.struts[a].damping_bump);
        visit(base + 0x0C, h.struts[a].damping_rebound);
        visit(base + 0x10, h.struts[a].anti_roll);
        visit(base + 0x14, h.struts[a].bump_limit);
        visit(base + 0x18, h.struts[a].droop_limit);
    }
    visit(kVehicleShiftFractionOffset, h.shift_fraction);
    visit(kVehicleFinalRearOffset, h.final_drive_rear);
    visit(kVehicleFinalFrontOffset, h.final_drive_front);
    visit(kVehicleRearFractionOffset, h.rear_drive_fraction);
    visit(kVehicleClutchStiffnessOffset, h.clutch_stiffness);
    visit(kVehicleClutchRateOffset, h.clutch_rate);
    for (std::size_t g = 0; g < kVehicleGearSlots; ++g) {
        visit(kVehicleGearRatioOffset + g * 4, h.gear_ratios[g]);
    }
    visit(kVehicleIdleRpmOffset, h.idle_rpm);
    visit(kVehicleRedlineRpmOffset, h.redline_rpm);
    visit(kVehicleMaxRpmOffset, h.max_rpm);
    visit(kVehicleEngineInertiaOffset, h.engine_inertia);
    for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
        visit(kVehicleTorqueOffset + i * 4, h.torque[i]);
    }
    for (std::size_t i = 0; i < kVehicleEngineBrakePoints; ++i) {
        visit(kVehicleEngineBrakeOffset + i * 4, h.engine_brake[i]);
    }
    visit(kVehicleDragOffset, h.drag);
    visit(kVehicleDownforceFrontOffset, h.downforce_front);
    visit(kVehicleDownforceRearOffset, h.downforce_rear);
    visit(kVehicleAeroPreloadOffset, h.aero_preload);
    visit(kVehicleServiceBrakeOffset, h.service_brake);
    visit(kVehicleParkingBrakeOffset, h.parking_brake);
    visit(kVehicleBrakeBalanceOffset, h.brake_balance);
    visit(kVehicleYawTorqueLimitOffset, h.yaw_torque_limit);
}

std::string hex_offset(std::size_t offset) {
    static const char digits[] = "0123456789ABCDEF";
    std::string text = "+0x";
    bool started = false;
    for (int shift = 12; shift >= 0; shift -= 4) {
        const std::size_t nibble = (offset >> shift) & 0xFU;
        if (nibble != 0 || started || shift == 0) {
            text.push_back(digits[nibble]);
            started = true;
        }
    }
    return text;
}

void add_issue(std::vector<VehicleHandlingIssue>& issues, std::size_t offset,
               const std::string& message) {
    VehicleHandlingIssue issue;
    issue.offset = offset;
    issue.message = hex_offset(offset) + ": " + message;
    issues.push_back(issue);
}

} // namespace

// ---------------------------------------------------------------------------
// Deterministic arithmetic
// ---------------------------------------------------------------------------

double vehicle_sqrt(double x) noexcept {
    if (!(x > 0.0)) {
        return 0.0;
    }
    if (x > DBL_MAX) {
        return x;
    }
    // A subnormal is scaled by 2^108 first, which is exact, and the root by
    // 2^-54 after, which is exact too.
    double scale_back = 1.0;
    if (x < DBL_MIN) {
        x *= 324518553658426726783156020576256.0;  // 2^108
        scale_back = 5.5511151231257827e-17;       // 2^-54
    }
    std::uint64_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    const std::uint64_t exponent = (bits >> 52) & 0x7FFU;
    // 2^floor((e - 1023) / 2): within a factor of 2 of the root.
    const std::uint64_t guess_bits = ((exponent + 1023U) / 2U) << 52;
    double y = 0.0;
    std::memcpy(&y, &guess_bits, sizeof(y));
    for (int i = 0; i < 7; ++i) {
        y = 0.5 * (y + x / y);
    }
    return y * scale_back;
}

// Range reduction keeps the alternating atan series within |z| <= sqrt(2)-1.
// The omitted term is bounded by |z|^43/43 (< 9e-19). Reciprocal reduction
// avoids squaring extreme inputs and preserves saturation at infinite slip.
double vehicle_atan(double x) noexcept {
    constexpr double pi_over_two = 1.57079632679489661923;
    constexpr double pi_over_four = 0.78539816339744830962;
    if (x != x) return 0.0;
    const bool negative = x < 0.0;
    double z = negative ? -x : x;
    const bool reciprocal = z > 1.0;
    if (reciprocal) z = 1.0 / z;
    const bool shifted = z > 0.41421356237309504880;
    if (shifted) z = (z - 1.0) / (z + 1.0);
    const double z2 = z * z;
    double polynomial = 1.0 / 41.0;
    for (int denominator = 39; denominator >= 1; denominator -= 2)
        polynomial = 1.0 / static_cast<double>(denominator) - z2 * polynomial;
    double angle = z * polynomial;
    if (shifted) angle += pi_over_four;
    if (reciprocal) angle = pi_over_two - angle;
    return negative ? -angle : angle;
}

namespace {

// The original lateral load-curve family of 0x00587740, Unicorn-verified:
// the slip angle selects and blends seven six-point curves over x 0..10
// (descriptors 0x7043EC..0x704464), each evaluated at the wheel's load.
// Curve 0's values sit in BSS and read as zero at rest.
// pi/2 split so that n * kPiOver2High is exact for |n| < 2^20 (fdlibm's
// pio2_1 and pio2_1t).
constexpr double kTwoOverPi = 6.36619772367581382433e-01;
constexpr double kPiOver2High = 1.57079632673412561417e+00;
constexpr double kPiOver2Low = 6.07710050650619224932e-11;

// sin and cos of r, |r| <= pi/4, by their Taylor series in Horner form.
double sin_kernel(double r) noexcept {
    const double r2 = r * r;
    // r - r^3/3! + r^5/5! - r^7/7! + r^9/9! - r^11/11! + r^13/13! - r^15/15!
    double s = -1.0 / 1307674368000.0;
    s = 1.0 / 6227020800.0 + r2 * s;
    s = -1.0 / 39916800.0 + r2 * s;
    s = 1.0 / 362880.0 + r2 * s;
    s = -1.0 / 5040.0 + r2 * s;
    s = 1.0 / 120.0 + r2 * s;
    s = -1.0 / 6.0 + r2 * s;
    return r + r * (r2 * s);
}

double cos_kernel(double r) noexcept {
    const double r2 = r * r;
    // 1 - r^2/2! + r^4/4! - ... + r^16/16!
    double c = 1.0 / 20922789888000.0;
    c = -1.0 / 87178291200.0 + r2 * c;
    c = 1.0 / 479001600.0 + r2 * c;
    c = -1.0 / 3628800.0 + r2 * c;
    c = 1.0 / 40320.0 + r2 * c;
    c = -1.0 / 720.0 + r2 * c;
    c = 1.0 / 24.0 + r2 * c;
    c = -0.5 + r2 * c;
    return 1.0 + r2 * c;
}

// Returns false outside the supported range; otherwise the quadrant and the
// reduced argument.
bool reduce(double x, long long& quadrant, double& r) noexcept {
    if (!finite(x) || !(absolute(x) < 1.0e6)) {
        return false;
    }
    const double t = x * kTwoOverPi;
    const long long n = static_cast<long long>(t < 0.0 ? t - 0.5 : t + 0.5);
    const double nd = static_cast<double>(n);
    r = (x - nd * kPiOver2High) - nd * kPiOver2Low;
    quadrant = ((n % 4) + 4) % 4;
    return true;
}

} // namespace

double vehicle_sin(double x) noexcept {
    long long q = 0;
    double r = 0.0;
    if (!reduce(x, q, r)) {
        return 0.0;
    }
    switch (q) {
    case 0:
        return sin_kernel(r);
    case 1:
        return cos_kernel(r);
    case 2:
        return -sin_kernel(r);
    default:
        return -cos_kernel(r);
    }
}

double vehicle_cos(double x) noexcept {
    long long q = 0;
    double r = 0.0;
    if (!reduce(x, q, r)) {
        return 1.0;
    }
    switch (q) {
    case 0:
        return cos_kernel(r);
    case 1:
        return -sin_kernel(r);
    case 2:
        return -cos_kernel(r);
    default:
        return sin_kernel(r);
    }
}

double vehicle_uniform_curve(const float* values, std::size_t count, double x0,
                             double x1, double x) noexcept {
    if (values == nullptr || count == 0) {
        return 0.0;
    }
    if (count == 1 || !(x1 > x0) || !finite(x)) {
        return static_cast<double>(values[0]);
    }
    const double inverse_step = static_cast<double>(count - 1) / (x1 - x0);
    const double u = (x - x0) * inverse_step;
    const double last = static_cast<double>(count - 1);
    // truncation toward zero reaches count - 1 exactly when u does, and
    // falls below 0 exactly when u <= -1
    if (u >= last) {
        return static_cast<double>(values[count - 1]);
    }
    if (u <= -1.0) {
        return static_cast<double>(values[0]);
    }
    const long long i = static_cast<long long>(u);
    double lower = static_cast<double>(i);
    if (lower > u) {
        lower -= 1.0;
    }
    const double t = u - lower;
    const std::size_t index = static_cast<std::size_t>(i);
    return (1.0 - t) * static_cast<double>(values[index]) +
           t * static_cast<double>(values[index + 1]);
}

// ---------------------------------------------------------------------------
// The recovered handling
// ---------------------------------------------------------------------------

// Source: Global/GLOBALB.BUN (SHA-256 39DF38FA...8DB9), chunk 0x00034600,
// record 18 (240SX) at file offset 1736752. The reproduced bytes in offset
// order are pinned by kVehicle240sxRecordBytesSha256; nfsu_vehicle_tests
// reconstructs and checks them.
VehicleHandling stock_240sx_handling() {
    VehicleHandling h;
    // +0x110.. the centre of mass.
    h.centre_of_mass = {0.25F, 0.0F, 0.28F};
    // +0x120.. the wheel table, docs/PART_PLACEMENT.md "Retail validation".
    const float lon[kVehicleWheelCount] = {1.39F, 1.39F, -1.04878F, -1.04878F};
    const float lat[kVehicleWheelCount] = {0.73F, -0.73F, -0.73F, 0.73F};
    const float radius[kVehicleWheelCount] = {0.288F, 0.288F, 0.3135F, 0.3135F};
    for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
        h.wheels[k].longitudinal = lon[k];
        h.wheels[k].lateral = lat[k];
        h.wheels[k].vertical = 0.08F;
        h.wheels[k].radius = radius[k];
    }
    // +0x1E0 front and +0x200 rear tyre blocks.
    h.tyres[0] = VehicleAxleTyre{12.0F, 1.0F, 1.0F, 1.8F, 1.5F};
    h.tyres[1] = VehicleAxleTyre{12.0F, 1.0F, 1.0F, 1.7F, 1.47F};
    // +0x220..+0x22C
    h.mass = 1.22F;
    h.length = 4.52F;
    h.width = 1.69F;
    h.height = 1.29F;
    // +0x230, +0x244, +0x258: the inertia diagonal.
    h.inertia = {0.4595537F, 2.2462742F, 2.367461F};
    h.alternate_yaw_scale = 2.0F;
    // +0x280 front and +0x2A0 rear struts.
    h.struts[0] = VehicleAxleStrut{5.4F, 35.0F, 5.4F, 5.8F, 37.0F, 0.125F, -0.13F};
    h.struts[1] = VehicleAxleStrut{5.1F, 30.0F, 5.0F, 5.65F, 30.0F, 0.125F, -0.13F};
    // +0x2C0.. the gearbox.
    h.shift_fraction = 0.8F;
    h.final_drive_rear = 4.083F;
    h.final_drive_front = 4.083F;
    h.rear_drive_fraction = 1.0F;
    h.clutch_stiffness = 0.04F;
    h.forward_gears = 5;
    h.clutch_rate = 2.0F;
    h.gear_ratios = {-3.657F, 0.0F, 3.321F, 1.902F, 1.308F, 1.0F, 0.9F, 0.0F};
    // +0x300.. the engine.
    h.idle_rpm = 800.0F;
    h.redline_rpm = 7500.0F;
    h.max_rpm = 8000.0F;
    h.engine_inertia = 0.00023F;
    h.torque = {0.14F, 0.15F, 0.16F, 0.18F, 0.21F, 0.22F, 0.19F, 0.165F, 0.145F};
    h.engine_brake = {0.12F, 0.18F, 0.13F};
    h.drag = 0.0004F;
    h.downforce_front = 0.14F;
    h.downforce_rear = 0.155F;
    h.aero_preload = 0.01F;
    h.service_brake = 0.4F;
    h.parking_brake = 0.5F;
    h.brake_balance = 0.5F;
    h.yaw_torque_limit = 17.0F; //477C32, stock raw record+398
    return h;
}

bool check_vehicle_handling(const VehicleHandling& h,
                            std::vector<VehicleHandlingIssue>& issues) {
    const std::size_t before = issues.size();
    visit_floats(h, [&](std::size_t offset, const float& value) {
        if (!finite_f32(value)) {
            add_issue(issues, offset, "not a finite number");
        }
    });
    if (issues.size() != before) {
        return false;
    }
    const auto positive = [&](std::size_t offset, float value, const char* what) {
        if (!(value > 0.0F)) {
            add_issue(issues, offset, std::string(what) + " must be positive");
        }
    };
    for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
        positive(kVehicleWheelOffset + k * kVehicleWheelStride + 0x10, h.wheels[k].radius,
                 "the rolling radius");
    }
    for (std::size_t a = 0; a < 2; ++a) {
        const std::size_t tyre = kVehicleTyreOffset + a * kVehicleAxleStride;
        positive(tyre + 0x00, h.tyres[a].slip_scale, "the lateral slip divisor");
        positive(tyre + 0x04, h.tyres[a].lateral_load_scale, "the lateral load divisor");
        if (h.tyres[a].lateral_scale < 0.0F)
            add_issue(issues, tyre + 0x08, "the lateral force scale must not be negative");
        positive(tyre + 0x0C, h.tyres[a].static_friction, "the static friction");
        positive(tyre + 0x10, h.tyres[a].sliding_friction, "the sliding friction");
        const std::size_t strut = kVehicleStrutOffset + a * kVehicleAxleStride;
        positive(strut + 0x04, h.struts[a].spring, "the spring rate");
        if (h.struts[a].progressive < 0.0F) {
            add_issue(issues, strut + 0x00, "the progressive term must not be negative");
        }
        if (h.struts[a].damping_bump < 0.0F || h.struts[a].damping_rebound < 0.0F) {
            add_issue(issues, strut + 0x08, "a damping rate must not be negative");
        }
        if (h.struts[a].anti_roll < 0.0F) {
            add_issue(issues, strut + 0x10, "the anti-roll rate must not be negative");
        }
        if (!(h.struts[a].bump_limit > 0.0F)) {
            add_issue(issues, strut + 0x14, "the bump limit must be positive");
        }
        if (!(h.struts[a].droop_limit < 0.0F)) {
            add_issue(issues, strut + 0x18, "the droop limit must be negative");
        }
    }
    positive(kVehicleMassOffset, h.mass, "the mass");
    for (std::size_t i = 0; i < 3; ++i) {
        positive(kVehicleInertiaOffset + i * kVehicleInertiaStride, h.inertia[i],
                 "a moment of inertia");
    }
    positive(kVehicleLengthOffset, h.length, "the length");
    positive(kVehicleWidthOffset, h.width, "the width");
    positive(kVehicleHeightOffset, h.height, "the height");
    positive(kVehicleAlternateYawScaleOffset, h.alternate_yaw_scale, "the alternate yaw scale");
    positive(kVehicleFinalRearOffset, h.final_drive_rear, "the rear final drive");
    positive(kVehicleFinalFrontOffset, h.final_drive_front, "the front final drive");
    positive(kVehicleEngineInertiaOffset, h.engine_inertia, "the engine inertia");
    positive(kVehicleClutchStiffnessOffset, h.clutch_stiffness, "the clutch stiffness");
    positive(kVehicleClutchRateOffset, h.clutch_rate, "the clutch rate");
    if (h.service_brake < 0.0F || h.parking_brake < 0.0F)
        add_issue(issues, kVehicleServiceBrakeOffset, "brake strength must not be negative");
    if (h.brake_balance < 0.0F || h.brake_balance > 1.0F)
        add_issue(issues, kVehicleBrakeBalanceOffset, "brake balance must be between0 and1");
    if (!(h.rear_drive_fraction >= 0.0F && h.rear_drive_fraction <= 1.0F)) {
        add_issue(issues, kVehicleRearFractionOffset,
                  "the rear drive fraction must be between 0 and 1");
    }
    if (h.forward_gears < 1 || h.forward_gears > kVehicleGearSlots - 2) {
        add_issue(issues, kVehicleForwardGearsOffset,
                  "the forward gear count must be between 1 and 6");
    } else {
        for (std::size_t g = 0; g < h.forward_gears; ++g) {
            const std::size_t slot = static_cast<std::size_t>(kVehicleGearFirst) + g;
            positive(kVehicleGearRatioOffset + slot * 4, h.gear_ratios[slot],
                     "a forward gear ratio");
        }
    }
    if (!(h.gear_ratios[static_cast<std::size_t>(kVehicleGearReverse)] < 0.0F)) {
        add_issue(issues, kVehicleGearRatioOffset, "the reverse ratio must be negative");
    }
    if (!(h.idle_rpm > 0.0F)) {
        add_issue(issues, kVehicleIdleRpmOffset, "the idle speed must be positive");
    }
    if (!(h.idle_rpm < h.redline_rpm)) {
        add_issue(issues, kVehicleRedlineRpmOffset, "the redline must be above idle");
    }
    if (!(h.redline_rpm <= h.max_rpm)) {
        add_issue(issues, kVehicleMaxRpmOffset, "the maximum speed must not be below the redline");
    }
    if (h.drag < 0.0F) {
        add_issue(issues, kVehicleDragOffset, "the drag must not be negative");
    }
    if (h.downforce_front < 0.0F)
        add_issue(issues,kVehicleDownforceFrontOffset,"front downforce must not be negative");
    if (h.downforce_rear < 0.0F)
        add_issue(issues,kVehicleDownforceRearOffset,"rear downforce must not be negative");
    if (h.aero_preload < 0.0F)
        add_issue(issues,kVehicleAeroPreloadOffset,"aero preload must not be negative");
    if (h.yaw_torque_limit < 0.0F) {
        add_issue(issues, kVehicleYawTorqueLimitOffset, "the yaw torque limit must not be negative");
    }
    // The preload levers the weight between wheel 0's and wheel 3's axles
    // about the centre of mass, so each axle's pair must share one position
    // and the centre of mass must lie between the axles.
    const float front = h.wheels[0].longitudinal;
    const float rear = h.wheels[2].longitudinal;
    if (h.wheels[1].longitudinal != front) {
        add_issue(issues, kVehicleWheelOffset,
                  "wheels 0 and 1 must share one longitudinal position");
    }
    if (h.wheels[3].longitudinal != rear) {
        add_issue(issues, kVehicleWheelOffset + 2 * kVehicleWheelStride,
                  "wheels 2 and 3 must share one longitudinal position");
    }
    if (!(h.centre_of_mass[0] < front) || !(h.centre_of_mass[0] > rear)) {
        add_issue(issues, kVehicleCentreOfMassOffset,
                  "the centre of mass must lie between the front and the rear axle");
    }
    return issues.size() == before;
}

bool decode_vehicle_handling(const unsigned char* record, std::size_t size,
                             VehicleHandling& out,
                             std::vector<VehicleHandlingIssue>& issues) {
    if (record == nullptr || size != kVehicleRecordSize) {
        VehicleHandlingIssue issue;
        issue.offset = 0;
        issue.message = "a CarTypeInfo record is exactly 3216 bytes";
        issues.push_back(issue);
        return false;
    }
    VehicleHandling h;
    visit_floats(h, [&](std::size_t offset, float& value) {
        value = read_f32(record + offset);
    });
    h.forward_gears = read_u32(record + kVehicleForwardGearsOffset);
    if (!check_vehicle_handling(h, issues)) {
        return false;
    }
    out = h;
    return true;
}

void encode_vehicle_handling(const VehicleHandling& handling, unsigned char* record,
                             std::size_t size) noexcept {
    if (record == nullptr || size != kVehicleRecordSize) {
        return;
    }
    visit_floats(handling, [&](std::size_t offset, const float& value) {
        write_f32(record + offset, value);
    });
    write_u32(record + kVehicleForwardGearsOffset, handling.forward_gears);
}

std::size_t compare_vehicle_handling(const VehicleHandling& a,
                                     const VehicleHandling& b) noexcept {
    // Both walks visit the same fields in the same order, so the i-th value
    // of one is the i-th of the other.
    std::array<std::uint32_t, 128> left{};
    std::size_t count = 0;
    visit_floats(a, [&](std::size_t, const float& value) {
        if (count < left.size()) {
            left[count] = f32_bits(value);
        }
        ++count;
    });
    std::size_t first = kVehicleRecordSize;
    std::size_t index = 0;
    visit_floats(b, [&](std::size_t offset, const float& value) {
        if (index < left.size() && left[index] != f32_bits(value) && offset < first) {
            first = offset;
        }
        ++index;
    });
    if (a.forward_gears != b.forward_gears && kVehicleForwardGearsOffset < first) {
        first = kVehicleForwardGearsOffset;
    }
    return first;
}

std::array<std::array<float, 2>, 3> vehicle_low_support_curve() noexcept {
    // Descriptor 0x00705E98 -> three pairs at 0x00705E80.
    return {{{0.0F, 0.0F}, {0.1F, 0.0F}, {2.0F, 20.0F}}};
}

VehicleVec3 vehicle_low_support_torque(const VehicleVec3& omega, double mass,
                                     unsigned grounded, unsigned state) noexcept {
    if (state == 1 || grounded >= 2 || !finite(omega) || !finite(mass) || !(mass > 0)) {
        return {};
    }
    const double speed = vehicle_sqrt(dot(omega, omega));
    const auto curve = vehicle_low_support_curve();
    const double first = static_cast<double>(curve[1][0]);
    const double last = static_cast<double>(curve[2][0]);
    const double damping = speed <= first ? 0.0 : speed >= last ? curve[2][1] :
        (speed - first) / (last - first) * curve[2][1];
    // 0x00474E30..0x00474E85 adds -curve(|omega|)*mass*omega
    // to world torque. It changes no position, velocity or support force.
    return scale(omega, -damping * mass);
}

VehicleSpec stock_240sx_vehicle_spec() {
    VehicleSpec spec;
    spec.handling = stock_240sx_handling();
    return spec;
}

// ---------------------------------------------------------------------------
// Ground
// ---------------------------------------------------------------------------

VehicleGroundHit VehicleFlatGround::cast(const VehicleVec3& from,
                                         const VehicleVec3& direction,
                                         double length) const {
    VehicleGroundHit hit;
    // Only a segment going down can meet a plane that faces up.
    if (!(direction.z < 0.0) || !finite(from) || !finite(direction) || !finite(length)) {
        return hit;
    }
    const double above = from.z - height_;
    if (above < 0.0) {
        return hit;
    }
    const double distance = above / -direction.z;
    if (!(distance <= length)) {
        return hit;
    }
    hit.hit = true;
    hit.distance = distance;
    hit.normal = v3(0.0, 0.0, 1.0);
    hit.surface = 0;
    return hit;
}

// ---------------------------------------------------------------------------
// Telemetry
// ---------------------------------------------------------------------------

namespace {

struct Encoder {
    std::array<std::uint8_t, kVehicleTelemetryRecordBytes>& out;
    std::size_t at = 0;

    void u64(std::uint64_t v) noexcept {
        for (int i = 0; i < 8; ++i) {
            out[at++] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU);
        }
    }
    void u32(std::uint32_t v) noexcept {
        for (int i = 0; i < 4; ++i) {
            out[at++] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU);
        }
    }
    void f64(double v) noexcept {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }
    void vec(const VehicleVec3& v) noexcept {
        f64(v.x);
        f64(v.y);
        f64(v.z);
    }
};

} // namespace

void encode_vehicle_telemetry(const VehicleTelemetry& r,
                              std::array<std::uint8_t, kVehicleTelemetryRecordBytes>& out) noexcept {
    Encoder e{out, 0};
    e.u64(r.step);
    e.vec(r.position);
    e.vec(r.velocity);
    e.vec(r.forward);
    e.vec(r.angular_velocity);
    e.f64(r.forward_speed);
    e.f64(r.lateral_speed);
    e.f64(r.engine_rpm);
    e.f64(r.wheel_rpm);
    e.f64(r.engine_torque);
    e.u32(static_cast<std::uint32_t>(r.gear));
    e.f64(r.throttle);
    e.f64(r.brake);
    e.f64(r.steer);
    e.u32(r.handbrake ? 1U : 0U);
    e.f64(r.steer_angle);
    for (const VehicleWheelTelemetry& w : r.wheels) {
        e.f64(w.compression);
        e.f64(w.load);
        e.f64(w.spin);
        e.f64(w.slip_ratio);
        e.f64(w.slip_lateral);
        e.f64(w.force_long);
        e.f64(w.force_lat);
        e.f64(w.drive_torque);
        e.f64(w.drive_applied);
        e.u32(w.surface);
        e.u32((w.contact ? 1U : 0U) | (w.brake_locked ? 2U : 0U));
        e.u32(w.sliding ? 1U : 0U);
    }
    // Every new persistent mechanical state that can distinguish futures.
    e.f64(r.engine.angular_speed);
    e.f64(r.engine.throttle_multiplier);
    e.f64(r.engine.cutoff_until);
    e.f64(r.transmission.clutch);
    e.f64(r.transmission.previous_slip_rpm);
    e.f64(r.transmission.up_offset);
    e.f64(r.transmission.down_offset);
    e.u32(static_cast<std::uint32_t>(r.transmission.mode));
    e.u32(static_cast<std::uint32_t>(r.transmission.requested_gear));
    e.u32(r.transmission.rng_state);
    e.u32(r.transmission.shift_pending?1U:0U);
    for (std::size_t k=0;k<kVehicleWheelCount;++k) {
        e.f64(r.wheel_history_torque[k]);
        e.u32(static_cast<std::uint32_t>(r.wheel_spin_direction[k]));
    }
    e.f64(r.owner_speeds.forward);
    e.f64(r.owner_speeds.magnitude);
    e.f64(r.simulation_seconds);
    e.f64(r.solver_dt);
    e.u32(r.alternate_yaw_active?1U:0U);
    e.vec(r.prepared_force);e.vec(r.prepared_torque);
    e.u32(r.body_rest_state);e.u32(r.body_gravity?1U:0U);e.u32(r.forces_prepared?1U:0U);
    e.u32(r.published_support);e.u32(static_cast<std::uint32_t>(r.published_gear));
    e.f64(r.engine.feedback_torque);
    for(std::size_t k=0;k<4;++k) {
        e.f64(r.wheel_prepared_torque[k]);e.f64(r.wheel_prepared_force_long[k]);
        e.f64(r.wheel_prepared_slip[k]);e.u32(r.wheel_prepared_sliding[k]?1U:0U);
    }
    const auto& body=r.rigid_body;
    for(const auto& vector:{body.position,body.momentum,body.angular_momentum,body.velocity,
                           body.angular_velocity,body.force,body.torque})
        for(float value:vector) e.f64(value);
    for(const auto& matrix:{body.axes,body.snapshot_axes})
        for(const auto& vector:matrix) for(float value:vector) e.f64(value);
    e.f64(body.adaptive_damping);e.f64(body.yaw_torque_limit);
    e.u32(body.linear_damping?1U:0U);e.u32(body.angular_damping?1U:0U);
    e.u32(body.alternate_yaw?1U:0U);e.u32(body.gravity?1U:0U);e.u32(r.rigid_body_ready?1U:0U);
    e.f64(r.rigid_orientation.w);e.f64(r.rigid_orientation.x);
    e.f64(r.rigid_orientation.y);e.f64(r.rigid_orientation.z);
    e.u32(r.steering_word);e.u32(r.steering_word_ready?1U:0U);
    e.f64(r.aero.speed_divisor);e.f64(r.aero.drag_scale);
    e.f64(r.aero.processed_throttle);e.f64(r.aero.support_kind);
    e.u32(r.aero.enabled?1U:0U);
    for(float value:r.aero_preloads) e.f64(value);
    e.u32(r.aero_ready?1U:0U);
}

void VehicleTelemetryHash::add(const VehicleTelemetry& record) noexcept {
    std::array<std::uint8_t, kVehicleTelemetryRecordBytes> bytes{};
    encode_vehicle_telemetry(record, bytes);
    sha_.update(bytes.data(), bytes.size());
    ++records_;
}

Sha256Digest VehicleTelemetryHash::finish() noexcept {
    records_ = 0;
    return sha_.finish();
}

// ---------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------

VehicleModel::VehicleModel(const VehicleSpec& spec) : spec_(spec) {
    std::vector<VehicleHandlingIssue> issues;
    if (!check_vehicle_handling(spec_.handling, issues)) {
        error_ = "handling " + issues.front().message;
        return;
    }
    const VehicleApproximations& a = spec_.approx;
    if (!finite(spec_.aero.drag_gain) || !finite(spec_.aero.drag_arm_z)) {
        error_ = "supplied initial aero globals must be finite";
        return;
    }
    const double approx_values[] = {
        a.unit_scale, a.wheel_inertia, a.brake_torque_front, a.brake_torque_rear,
        a.max_steer, a.steer_speed_reference, a.steer_rate,
        a.longitudinal_stiffness, a.cornering_stiffness_front, a.cornering_stiffness_rear,
        a.slip_speed_floor,
        a.rolling_resistance, a.rolling_fade_speed, a.bump_stop_rate, a.probe_lift,
        a.engine_brake_scale, a.tyre_force_lift};
    for (const double value : approx_values) {
        if (!finite(value) || value < 0.0) {
            error_ = "an approximation is negative or not finite";
            return;
        }
    }
    if (!(a.unit_scale > 0.0) || !(a.wheel_inertia > 0.0) || !(a.slip_speed_floor > 0.0) ||
        !(a.rolling_fade_speed > 0.0) || !(a.steer_speed_reference > 0.0)) {
        error_ = "an approximation that divides must be positive";
        return;
    }
    if (!(a.tyre_force_lift <= 1.0)) {
        error_ = "the tyre force lift must lie between 0 and 1";
        return;
    }

    const VehicleHandling& h = spec_.handling;
    const double unit = a.unit_scale;
    d_.mass = static_cast<double>(h.mass) * unit;
    // The weight as 0x0047549D..0x004754A3 forms it, mass times the engine
    // object's 9.8, then in newtons.
    d_.weight = static_cast<double>(h.mass) * kGravity * unit;
    d_.inertia = v3(static_cast<double>(h.inertia[0]) * unit,
                    static_cast<double>(h.inertia[1]) * unit,
                    static_cast<double>(h.inertia[2]) * unit);
    d_.centre_of_mass = v3(static_cast<double>(h.centre_of_mass[0]),
                           static_cast<double>(h.centre_of_mass[1]),
                           static_cast<double>(h.centre_of_mass[2]));
    for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
        d_.attach[k] = sub(v3(static_cast<double>(h.wheels[k].longitudinal),
                              static_cast<double>(h.wheels[k].lateral),
                              static_cast<double>(h.wheels[k].vertical)),
                           d_.centre_of_mass);
        d_.radius[k] = static_cast<double>(h.wheels[k].radius);
    }
    //477100 supplies binary32 speed-dependent preload once per outer call.
    for (std::size_t axle = 0; axle < 2; ++axle) {
        const VehicleAxleStrut& s = h.struts[axle];
        d_.progressive[axle] = static_cast<double>(s.progressive);
        d_.spring[axle] = static_cast<double>(s.spring) * unit;
        d_.damping_bump[axle] = static_cast<double>(s.damping_bump) * unit;
        d_.damping_rebound[axle] = static_cast<double>(s.damping_rebound) * unit;
        d_.anti_roll[axle] = static_cast<double>(s.anti_roll) * unit;
        d_.bump_limit[axle] = static_cast<double>(s.bump_limit);
        d_.droop_limit[axle] = static_cast<double>(s.droop_limit);
        d_.static_friction[axle] = static_cast<double>(h.tyres[axle].static_friction);
        d_.sliding_friction[axle] = static_cast<double>(h.tyres[axle].sliding_friction);
    }

    // The shift tables of 0x0046D1C0, for the retail flag at 0x00700030
    // (1: search the torque curves for the crossing).
    const double idle = static_cast<double>(h.idle_rpm);
    const double redline = static_cast<double>(h.redline_rpm);
    const int top = kVehicleGearFirst + static_cast<int>(h.forward_gears) - 1;
    upshift_.fill(0.0);
    downshift_.fill(0.0);
    upshift_[static_cast<std::size_t>(kVehicleGearNeutral)] =
        (redline - idle) * static_cast<double>(h.shift_fraction) + idle;
    for (int g = kVehicleGearFirst; g < top; ++g) {
        const double current = static_cast<double>(h.gear_ratios[static_cast<std::size_t>(g)]);
        const double next = static_cast<double>(h.gear_ratios[static_cast<std::size_t>(g + 1)]);
        double rpm = (redline + idle) * 0.5;
        double shift = redline - kShiftRedlineMargin;
        if (rpm < redline) {
            for (;;) {
                const double here = torque_curve(rpm);
                double there = 0.0;
                if (absolute(current) > kShiftRatioEpsilon) {
                    const double inverse = 1.0 / current;
                    there = torque_curve(rpm * inverse * next) * inverse * next;
                }
                if (there > here) {
                    shift = rpm;
                    break;
                }
                rpm = kShiftSearchStep + rpm;
                if (!(rpm < redline)) {
                    shift = redline - kShiftRedlineMargin;
                    break;
                }
            }
        }
        upshift_[static_cast<std::size_t>(g)] = shift;
        downshift_[static_cast<std::size_t>(g + 1)] =
            absolute(current) > kShiftRatioEpsilon ? next / current * shift : 0.0;
    }
    upshift_[static_cast<std::size_t>(top)] = redline;
    valid_ = true;
}

double VehicleModel::torque_curve(double rpm) const noexcept {
    const VehicleHandling& h = spec_.handling;
    return vehicle_uniform_curve(h.torque.data(), h.torque.size(),
                                 static_cast<double>(h.idle_rpm),
                                 static_cast<double>(h.max_rpm), rpm);
}

double VehicleModel::engine_brake_curve(double rpm) const noexcept {
    const VehicleHandling& h = spec_.handling;
    return vehicle_uniform_curve(h.engine_brake.data(), h.engine_brake.size(),
                                 static_cast<double>(h.idle_rpm),
                                 static_cast<double>(h.max_rpm), rpm);
}

void vehicle_brake_pin(VehicleWheelState& wheel, double brake) noexcept {
    // 0x004757F0..0x00475845: strict comparisons and a persistent flag.
    // The original state/input operands are floats; the port integrates
    // doubles, so narrow only the gate operands, not the simulation state.
    const float axis = static_cast<float>(sanitize(brake, 0.0, 1.0));
    const float speed = static_cast<float>(absolute(wheel.spin));
    if (axis > 0.95F && speed < 30.0F) {
        wheel.brake_locked = true;
        wheel.spin = 0.0;
    }
    if (axis < 0.2F) wheel.brake_locked = false;
}

// Debug/track pose initialization, once per reset. The port chooses a gear
// compatible with an imposed moving pose; this placement rule is not a
// recovered original reset trace.478365's RPM-to-physical-state conversion
// then runs once. Runtime steps never feed actor RPM back into the engine.
void VehicleModel::initial_engine_speed(VehicleState& state) const noexcept {
    const auto& h=spec_.handling;
    const std::array<float,4> spins{{static_cast<float>(state.wheels[0].spin),
        static_cast<float>(state.wheels[1].spin),static_cast<float>(state.wheels[2].spin),
        static_cast<float>(state.wheels[3].spin)}};
    const auto rpm=[&]() {
        return static_cast<double>(player_wheel_engine_speed(h,state.gear,spins)*kPlayerRadSToRpm);
    };
    double speed=rpm();
    if (state.gearbox==VehicleGearbox::automatic) {
        const int top=kVehicleGearFirst+static_cast<int>(h.forward_gears)-1;
        while (state.gear<top&&speed>upshift_[static_cast<std::size_t>(state.gear)]) {++state.gear;speed=rpm();}
        while (state.gear>kVehicleGearFirst&&speed<downshift_[static_cast<std::size_t>(state.gear)]) {--state.gear;speed=rpm();}
    }
    state.wheel_rpm=speed;
    state.engine_rpm=clamp(speed,h.idle_rpm,h.max_rpm);
}

bool VehicleModel::reset(VehicleState& state, const VehiclePose& pose) const {
    if (!valid_ || !finite(pose.ground) || !finite(pose.heading) || !finite(pose.speed) ||
        !(absolute(pose.heading) < 1.0e6)) {
        return false;
    }
    // The preloads carry the lever-rule loads exactly, so every strut rests
    // at zero compression and each attachment point one rolling radius
    // above the ground; the axles' different radii pitch the body.
    std::array<double, 2> attach_height{};
    for (std::size_t axle = 0; axle < 2; ++axle) {
        const std::size_t wheel = axle * 2;
        attach_height[axle] = (d_.radius[wheel] + d_.radius[wheel + 1]) * 0.5;
    }
    // Pitch about the body's +y axis: a positive sine lowers the nose. The
    // heights here are of the attachment points relative to the centre of
    // mass, so z0 is the centre of mass's height above the ground.
    const double front = d_.attach[0].x;
    const double rear = d_.attach[2].x;
    const double vertical_front = d_.attach[0].z;
    const double vertical_rear = d_.attach[2].z;
    const double s = ((attach_height[1] - vertical_rear) - (attach_height[0] - vertical_front)) /
                     (front - rear);
    const double c = vehicle_sqrt(1.0 - s * s);
    const double z0 = attach_height[0] - vertical_front * c + front * s;
    const double half_cos = vehicle_sqrt((1.0 + c) * 0.5);
    VehicleQuat pitch;
    pitch.w = half_cos;
    pitch.y = s / (2.0 * half_cos);
    VehicleQuat yaw;
    yaw.w = vehicle_cos(pose.heading * 0.5);
    yaw.z = vehicle_sin(pose.heading * 0.5);

    VehicleState next;
    next.step = 0;
    next.orientation = normalized(multiply(yaw, pitch));
    next.position = v3(pose.ground.x, pose.ground.y, pose.ground.z + z0);
    const Mat3 r = rotation(next.orientation);
    next.velocity = scale(r.column(0), pose.speed);
    next.angular_velocity = VehicleVec3{};
    for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
        VehicleWheelState& wheel = next.wheels[k];
        wheel.spin = pose.speed / d_.radius[k];
        wheel.compression = 0.0;
        wheel.previous_compression = 0.0;
        wheel.contact = true;
    }
    next.steer_angle = 0.0;
    next.gearbox = pose.gearbox;
    next.gear = kVehicleGearFirst;
    initial_engine_speed(next);
    next.shift_hold = 0;
    player_engine_reset(next.engine,static_cast<float>(next.engine_rpm));
    next.owner_speeds=player_publish_owner_speeds(next.velocity,r.column(0));
    next.published_gear=next.gear;
    next.transmission.requested_gear=next.gear;
    player_gear_seed(next.transmission,12345);
    //478398..4783B5 resets current/requested gear together, closes the
    // clutch and clears its previous slip. Ctor mode2/clutch0 is not the
    // completed reset state; moving-pose gear/RPM placement above is still
    // an explicit port adapter rather than a recovered spawn policy.
    next.transmission.mode=3;
    next.transmission.clutch=1.0F;
    next.transmission.previous_slip_rpm=0.0F;
    next.transmission.wheel_engine_speed=static_cast<float>(next.wheel_rpm)/kPlayerRadSToRpm;
    state = next;
    return true;
}

bool VehicleModel::step(VehicleState& state, const VehicleInput& raw_input,
                        const VehicleGround& ground, VehicleTelemetry& telemetry,
                        double publication_dt, double step_dt, bool select_request) const {
    if (!valid_ || !state_finite(state) || state.gear < kVehicleGearReverse ||
        state.gear > kVehicleGearFirst + static_cast<int>(spec_.handling.forward_gears) - 1 ||
        !finite(publication_dt) || publication_dt<0.0 ||
        !finite(step_dt) || !(step_dt>0.0) || step_dt>kVehicleStepSeconds ||
        state.transmission.requested_gear<kVehicleGearReverse ||
        state.transmission.requested_gear>kVehicleGearFirst+static_cast<int>(spec_.handling.forward_gears)-1) {
        return false;
    }
    const VehicleHandling& h = spec_.handling;
    const VehicleApproximations& a = spec_.approx;
    const double dt = step_dt;
    VehicleState s = state;
    if(select_request) {
        //4776FC/477714: ordinary state0 selects alternate yaw unless its
        // previous contact count is zero. The flag is set before the solve
        // and retained across all slices. Kind3-only port collision remains
        // state0; state1 car-versus-car activation is outside this adapter.
        s.alternate_yaw_active=s.published_support!=0;
    }

    const double throttle = sanitize(raw_input.throttle, 0.0, 1.0);
    // Selector1 has separate service and parking paths47792A/477953.
    const double brake = sanitize(raw_input.brake, 0.0, 1.0);
    const double steer = sanitize(raw_input.steer, -1.0, 1.0);
    if (select_request || !s.aero_ready) {
        //477BED and477100 precede the inner loop.479160's demonstrated
        // INITIAL1/0 coupling leaves body/engine/wheel scales at1. This
        // named adapter excludes active race scaling and traffic drafting.
        s.aero = PlayerAeroCache{};
        s.aero.enabled = spec_.aero.enabled;
        s.aero.processed_throttle = static_cast<float>(throttle);
        s.aero.support_kind = static_cast<float>(s.published_support)*0.25F;
        s.aero_preloads = player_aero_preloads(h,s.owner_speeds.forward,s.aero.enabled);
        s.aero_ready = true;
    }

    rigid_import_public(h,s);
    const bool original_steering=raw_input.road_steering&&raw_input.road_angle_word_valid;
    if(original_steering&&(select_request||!s.steering_word_ready)) {
        //4775E4 consumes the processed word before the solver loop.
        // Each subsequent strut callback consumes the retained front matrices.
        s.steering_word=raw_input.road_angle_word;s.steering_word_ready=true;
    }
    Mat3 r = rigid_rotation(s.rigid_body.axes);
    const VehicleVec3 forward = r.column(0);
    const double forward_speed = dot(s.velocity, forward);

    // Requests enter the actual clutch phases rather than changing physical
    // gear and resetting engine speed at once.
    const int top = kVehicleGearFirst + static_cast<int>(h.forward_gears) - 1;
    if (select_request && raw_input.shift != VehicleShift::none) {
        const bool up_request = raw_input.shift == VehicleShift::up;
        if (s.gearbox == VehicleGearbox::manual) {
            if (up_request && s.transmission.requested_gear < top) {
                ++s.transmission.requested_gear;
            } else if (!up_request && s.transmission.requested_gear > kVehicleGearReverse) {
                --s.transmission.requested_gear;
            }
        } else if (up_request && s.gear == kVehicleGearReverse) {
            s.transmission.requested_gear = kVehicleGearFirst;
        } else if (!up_request && s.gear == kVehicleGearFirst && absolute(forward_speed) < 1.0) {
            s.transmission.requested_gear = kVehicleGearReverse;
        }
    }
    if (select_request && s.gearbox==VehicleGearbox::automatic&&s.gear!=kVehicleGearReverse) {
        if (s.gear<=kVehicleGearNeutral) s.transmission.requested_gear=kVehicleGearFirst;
        else if (s.transmission.mode==3) {
            //46E5B0 selection uses physical engine speed, not actor RPM.
            const double rpm=static_cast<double>(s.engine.angular_speed*kPlayerRadSToRpm);
            const double up=std::min(static_cast<double>(h.redline_rpm),
                upshift_[static_cast<std::size_t>(s.gear)]+s.transmission.up_offset);
            double down=downshift_[static_cast<std::size_t>(s.gear)]+s.transmission.down_offset;
            double safe_down=0.0;
            if (s.gear>kVehicleGearFirst) safe_down=
                std::min(static_cast<double>(h.redline_rpm),upshift_[static_cast<std::size_t>(s.gear-1)]-100.0)*
                h.gear_ratios[static_cast<std::size_t>(s.gear)]/h.gear_ratios[static_cast<std::size_t>(s.gear-1)]-200.0;
            else down=throttle<0.2?h.idle_rpm+100.0:0.0;
            bool support=true;
            for (const auto& wheel:s.wheels) support=support&&wheel.contact;
            if (rpm>=up&&s.gear<top&&support) s.transmission.requested_gear=s.gear+1;
            else if (s.gear>kVehicleGearFirst&&rpm<down&&rpm<safe_down)
                s.transmission.requested_gear=s.gear-1;
        }
    }

    double target=0.0,engine_torque=0.0;
    unsigned support=0;VehicleVec3 support_torque,force,torque;
    std::array<VehicleWheelTelemetry,kVehicleWheelCount> wheel_out{};
    const auto prepare_forces=[&](double suspension_dt) {
        r=rigid_rotation(s.rigid_body.axes);
        const VehicleVec3 forward=r.column(0),left=r.column(1),up=r.column(2);
        const double forward_speed=dot(s.velocity,forward);
        // Steering: toward the input times the speed-dependent lock, at a
        // limited rate (APPROXIMATION).
        const double speed_ratio = forward_speed / a.steer_speed_reference;
        const double lock = a.max_steer / (1.0 + speed_ratio * speed_ratio);
        target = raw_input.road_steering ?
            sanitize(raw_input.road_target,-3.14159265358979323846,3.14159265358979323846) : steer * lock;
        const double max_change = a.steer_rate * suspension_dt;
        if(original_steering) s.steer_angle=
            static_cast<double>(static_cast<std::int16_t>(s.steering_word))*9.58738019107841e-5F;
        else if (raw_input.road_steering) s.steer_angle =
            sanitize(raw_input.road_angle,-3.14159265358979323846,3.14159265358979323846);
        else s.steer_angle = s.steer_angle + clamp(target - s.steer_angle, -max_change, max_change);
        if(!original_steering) {
            // Qualified compatibility adapter for callers that supply radians
            // or the approximate lock/slew instead of original controls1D0.
            const double scaled=s.steer_angle*10430.378350470453;
            const double rounded=scaled>=0?scaled+0.5:scaled-0.5;
            s.steering_word=rounded>=-9223372036854775808.0&&rounded<9223372036854775808.0?
                static_cast<std::uint16_t>(static_cast<std::int64_t>(rounded)):0;
            s.steering_word_ready=true;
        }
        const auto front_axes=player_front_steering_axes(h,s.steering_word);

        force = v3(0.0,0.0,s.body_gravity?-d_.weight:0.0);
        torque = {};
        wheel_out = {};

        // Struts: probe, compression, force.
        std::array<VehicleVec3, kVehicleWheelCount> attach_world{};
        std::array<VehicleGroundHit, kVehicleWheelCount> hits{};
        std::array<double, kVehicleWheelCount> raw_compression{};
        for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
            const std::size_t axle = k / 2;
            attach_world[k] = public_vector(player_strut_attachment(h,k,s.rigid_body));
            const VehicleVec3 from = add(attach_world[k], scale(up, a.probe_lift));
            const VehicleVec3 down = scale(up, -1.0);
            const double length = a.probe_lift + d_.radius[k] - d_.droop_limit[axle];
            hits[k] = ground.cast(from, down, length);
            // A hit off the segment, or whose normal is not finite, has no
            // length or faces away from the car, is a miss; the normal is made
            // unit length here so an implementation need not be exact about it.
            if (hits[k].hit) {
                const VehicleVec3 n = hits[k].normal;
                const double n_length = vehicle_sqrt(dot(n, n));
                if (!finite(hits[k].distance) || hits[k].distance < 0.0 ||
                    hits[k].distance > length || !finite(n) || !(n_length > 0.0) ||
                    !(dot(n, up) > 0.0)) {
                    hits[k].hit = false;
                } else {
                    hits[k].normal = scale(n, 1.0 / n_length);
                }
            }
            // The virtual segment remains a port ground-query adapter.
            // Convert its signed along-up gap before adding the radius, as
            //587070/588607 do at the declared binary32 witness precision.
            // This agrees with the cached-flat-quad witness; it does not
            // replace the retail height/material/world-query producers.
            if(hits[k].hit) {
                const float gap=static_cast<float>(a.probe_lift-hits[k].distance);
                raw_compression[k]=h.wheels[k].radius+gap;
            } else {
                raw_compression[k]=d_.droop_limit[axle]-1.0;
            }
        }
        std::array<double, kVehicleWheelCount> strut_force{};
        std::array<double, kVehicleWheelCount> overflow{};
        for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
            const std::size_t axle = k / 2;
            VehicleWheelState& wheel = s.wheels[k];
            wheel.previous_compression = wheel.compression;
            const double x = raw_compression[k];
            if (x < d_.droop_limit[axle]) {
                wheel.compression = d_.droop_limit[axle];
                wheel.contact = false;
            } else if (x > d_.bump_limit[axle]) {
                wheel.compression = d_.bump_limit[axle];
                overflow[k] = x - d_.bump_limit[axle];
                wheel.contact = true;
            } else {
                wheel.compression = x;
                wheel.contact = true;
            }
        }
        for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
            const std::size_t axle = k / 2;
            const VehicleWheelState& wheel = s.wheels[k];
            if (!wheel.contact) {
                continue;
            }
            const float x=static_cast<float>(wheel.compression);
            const float rate=suspension_dt>0.0?
                (x-static_cast<float>(wheel.previous_compression))/static_cast<float>(suspension_dt):0.0F;
            //46ABD0 refreshes the factory's ascending strut list.588580
            // consumes its partner immediately: the first strut in each
            // axle sees the previous compression, the second the new one.
            const auto& partner=s.wheels[kAntiRollPartner[k]];
            const float partner_compression=static_cast<float>(k%2==0?
                partner.previous_compression:partner.compression);
            const float nominal=player_strut_nominal_force(h.struts[axle],x,rate,
                partner_compression,s.aero_preloads[k],static_cast<float>(overflow[k]),
                static_cast<float>(a.bump_stop_rate/a.unit_scale));
            strut_force[k]=static_cast<double>(nominal)*a.unit_scale;
        }

        // Selector1: prepare all wheel inputs before the shared drivetrain/body
        // constraints. The existing contact/strut adapter and force application
        // remain explicit 3D approximations; mechanical wheel state is binary32.
        std::array<PlayerWheelWork,4> work{};
        std::array<std::array<VehicleVec3,3>,4> wheel_axes{};
        std::array<VehicleVec3,4> contact_arms{};
        const Mat3 snapshot=rigid_rotation(s.rigid_body.snapshot_axes);
        const std::array<VehicleVec3,3> body_axes{{snapshot.column(0),snapshot.column(1),snapshot.column(2)}};
        for (std::size_t k=0;k<kVehicleWheelCount;++k) {
            auto& wheel=s.wheels[k];auto& out=wheel_out[k];auto& w=work[k];
            out.compression=wheel.compression;out.contact=wheel.contact;
            out.load=strut_force[k];out.surface=hits[k].hit?hits[k].surface:0;
            w.spin=static_cast<float>(wheel.spin);w.history_torque=wheel.history_torque;
            w.spin_direction=wheel.spin_direction;w.locked=wheel.brake_locked;
            w.contact=wheel.contact;w.load=static_cast<float>(strut_force[k]/a.unit_scale);
            w.owner_speed=s.owner_speeds.forward;
            const float balance=k<2?1.0F-h.brake_balance:h.brake_balance;
            w.service=static_cast<float>(brake)*(balance+balance);
            // Ordinary owner state0: rear mask1, front mask0; >0.2 adds0.5.
            w.parking=k>=2&&raw_input.handbrake?1.5F:0.0F;
            w.position_x=static_cast<float>(d_.attach[k].x);
            w.position_y=static_cast<float>(d_.attach[k].y);
            const VehicleVec3 normal=wheel.contact?hits[k].normal:up;
            const std::array<VehicleVec3,3> local_axes=k<2?front_axes[k]:
                std::array<VehicleVec3,3>{{v3(1,0,0),v3(0,1,0),v3(0,0,1)}};
            const float c=static_cast<float>(local_axes[0].x),sn=static_cast<float>(local_axes[0].y);
            // Original5889C5 combines local wheel axes with the rigid snapshot,
            // without projecting them onto the ground. The raw refresh snapshot
            // remains distinct from the later outer-normalized live matrix.
            wheel_axes[k]=player_world_wheel_axes(body_axes,local_axes);
            const auto& along=wheel_axes[k][0];const auto& across=wheel_axes[k][1];
            const VehicleVec3 hub_local=v3(d_.attach[k].x,d_.attach[k].y,
                static_cast<float>(d_.attach[k].z)+static_cast<float>(wheel.compression));
            const VehicleVec3 hub_arm=r.apply(hub_local);
            VehicleVec3 arm=hub_arm;
            if (wheel.contact) {
                const VehicleVec3 from=add(attach_world[k],scale(up,a.probe_lift));
                const VehicleVec3 contact=add(from,scale(up,-hits[k].distance));
                arm=sub(contact,s.position);out.ground_point=contact;out.ground_normal=normal;
            }
            contact_arms[k]=arm;
            //588900 uses the strut hub for velocity.478480 applies the combined
            // wheel force at contact instead; the fitted tyre-force lift is unused.
            const VehicleVec3 point=player_hub_velocity(body_axes,hub_local,s.velocity,s.angular_velocity);
            const auto resolve=[&point](const VehicleVec3& axis) {
                return (static_cast<float>(point.z)*static_cast<float>(axis.z)+
                        static_cast<float>(point.y)*static_cast<float>(axis.y))+
                        static_cast<float>(point.x)*static_cast<float>(axis.x);
            };
            const float vlong=resolve(along),vlat=resolve(across);
            w.velocity_long=vlong;w.velocity_lat=vlat;
            w.heading_x=c;w.heading_y=sn;w.side_x=-sn;w.side_y=c;
            // The original standing/sliding producer uses quantized angles.
            // The diagnostic slip ratio keeps its documented speed floor.
            const double denominator=std::max(absolute(vlong),a.slip_speed_floor);
            out.slip_lateral=vlat/denominator;
        }
        //46ABD0 updates the gearbox after wheel/strut component refresh. Force
        // inputs/slip were cleared by those updates; the new producer runs later.
        player_gear_update(h,s.gear,s.transmission,s.engine,work,static_cast<float>(throttle),
                           s.owner_speeds.magnitude);
        if(!s.forces_prepared) {
            // Port spawn/ground adapter: initialize owner support from the first
            // actual ground probes. This is not a recovered live actor spawn policy.
            s.published_support=0;
            for(const auto& wheel:s.wheels) s.published_support+=wheel.contact?1U:0U;
            s.published_gear=s.gear;
            // Supplied moving-spawn force preparation: initial ground probes
            // seed support before the first aero callback. This retains the
            // diagnosed port epoch, not authentic actor activation timing.
            s.aero.support_kind=static_cast<float>(s.published_support)*0.25F;
        }
        s.body_rest_state=player_body_rest_state(s.velocity,s.angular_velocity,
            static_cast<float>(forward.z),static_cast<float>(left.z),static_cast<float>(throttle),
            s.published_gear,s.published_support,s.body_rest_state);
        s.body_gravity=s.body_rest_state!=2;
        s.rigid_body.gravity=s.body_gravity;
        if(s.body_rest_state==2) {s.rigid_body.momentum={};s.rigid_body.angular_momentum={};}
        else if(s.body_rest_state==1) {
            for(int k=0;k<3;++k) {
                s.rigid_body.momentum[k]*=0.95F;s.rigid_body.angular_momentum[k]*=0.95F;
            }
        }
        if(s.body_rest_state!=0) {player_rigid_body_refresh(h,s.rigid_body);rigid_public_cache(s);}
        for(std::size_t k=0;k<4;++k) {
            auto& w=work[k];auto& wheel=s.wheels[k];
            if(s.published_support>0&&s.body_rest_state==2) w.spin=0.0F;
            else if(s.body_rest_state==1) w.spin*=0.94999998807907104F;
            if(s.body_rest_state!=0)
                w.spin_direction=w.spin>1.0e-5F?0:(w.spin<-1.0e-5F?2:1);
            wheel.spin=w.spin;
            //477300 refreshes components again after rest momentum mutation;
            // the port recomputes the corresponding current hub velocity here.
            const VehicleVec3 hub=v3(d_.attach[k].x,d_.attach[k].y,
                static_cast<float>(d_.attach[k].z)+static_cast<float>(wheel.compression));
            const VehicleVec3 point=player_hub_velocity(body_axes,hub,s.velocity,s.angular_velocity);
            const auto resolve=[&point](const VehicleVec3& axis) {
                return (static_cast<float>(point.z)*static_cast<float>(axis.z)+
                        static_cast<float>(point.y)*static_cast<float>(axis.y))+
                        static_cast<float>(point.x)*static_cast<float>(axis.x);
            };
            w.velocity_long=resolve(wheel_axes[k][0]);w.velocity_lat=resolve(wheel_axes[k][1]);
            wheel_out[k].slip_lateral=w.velocity_lat/std::max(absolute(w.velocity_long),a.slip_speed_floor);
        }
        force = v3(0.0,0.0,s.body_gravity?-d_.weight:0.0);
        const float crank=player_engine_torque(h,s.engine,s.gear,static_cast<float>(throttle));
        engine_torque=static_cast<double>(crank)*a.unit_scale;
        s.transmission.wheel_engine_speed=player_wheel_engine_speed(h,s.gear,
            {{work[0].spin,work[1].spin,work[2].spin,work[3].spin}});
        const float transmitted=player_clutch_torque(h,s.engine,s.transmission,s.gear,
                                                    s.transmission.wheel_engine_speed,crank);
        player_distribute_torque(h,s.gear,s.engine,work,transmitted);
        for (std::size_t k=0;k<4;++k) {
            wheel_out[k].drive_torque=static_cast<double>(work[k].torque)*a.unit_scale;
            player_wheel_prepare(h,k,work[k]);
        }
        PlayerRigidBodyState aero_body=s.rigid_body;
        aero_body.force=rigid_vector(scale(force,1.0/a.unit_scale));
        aero_body.torque={};
        player_aero_apply(h,aero_body,s.aero,spec_.aero);
        force=scale(public_vector(aero_body.force),a.unit_scale);
        torque=scale(public_vector(aero_body.torque),a.unit_scale);
        const VehicleVec3 local_force=r.apply_transpose(force);
        for (unsigned iteration=0;iteration<5;++iteration) {
            player_body_wheel_constraints(h,work,static_cast<float>(local_force.x/a.unit_scale),
                                         static_cast<float>(local_force.y/a.unit_scale));
            const float correction=player_coupled_correction(h,s.gear,s.engine,s.transmission,work,crank);
            player_distribute_torque(h,s.gear,s.engine,work,correction);
        }
        for (std::size_t k=0;k<4;++k) player_wheel_limit(h,k,work[k]);
        player_engine_feedback_finalize(h,s.gear,s.transmission,s.engine,work,static_cast<float>(throttle),crank);
        PlayerRigidBodyState contact_body;
        contact_body.force=rigid_vector(scale(force,1.0/a.unit_scale));
        contact_body.torque=rigid_vector(scale(torque,1.0/a.unit_scale));
        for (std::size_t k=0;k<4;++k) {
            auto& wheel=s.wheels[k];auto& out=wheel_out[k];const auto& w=work[k];
            wheel.spin=w.spin;wheel.brake_locked=w.locked;wheel.history_torque=w.history_torque;
            wheel.spin_direction=w.spin_direction;
            wheel.prepared_torque=w.torque;wheel.prepared_force_long=w.force_long;
            wheel.prepared_slip=w.slip;wheel.prepared_sliding=w.sliding;
            out.spin=w.spin;out.drive_applied=static_cast<double>(w.torque)*a.unit_scale;
            out.sliding=w.sliding;out.force_long=static_cast<double>(w.force_long)*a.unit_scale;
            out.force_lat=static_cast<double>(w.force_lat)*a.unit_scale;
            out.slip_ratio=(wheel.spin*d_.radius[k]-w.velocity_long)/std::max(absolute(w.velocity_long),a.slip_speed_floor);
            if (wheel.contact) {
                //587830 default basis1 transforms Fx/Fy/load together through
                // the full world wheel basis.478480/584CD0 use the contact arm.
                const VehicleVec3 wheel_force=player_world_wheel_force(wheel_axes[k],
                    w.force_long,w.force_lat,w.load);
                player_rigid_body_add_contact_force(contact_body,wheel_force,contact_arms[k]);
            }
        }

        force=scale(public_vector(contact_body.force),a.unit_scale);
        torque=scale(public_vector(contact_body.torque),a.unit_scale);

        // Selector1 4791F0 also reads the outer-published actor3CC support.
        // This model only meets kind-3 track bodies (state remains0).
        // The separate zero-wheel alignment branch is not recovered here.
        support = 0;
        for (const auto& wheel : s.wheels) support += wheel.contact ? 1U : 0U;
        support_torque =
            vehicle_low_support_torque(s.angular_velocity, d_.mass, s.published_support, 0);
        if (s.published_support < 2) torque = add(torque, support_torque);

        s.prepared_force=force;s.prepared_torque=torque;s.forces_prepared=true;
        s.rigid_body.force=rigid_vector(scale(force,1.0/a.unit_scale));
        s.rigid_body.torque=rigid_vector(scale(torque,1.0/a.unit_scale));
    };
    if(!s.forces_prepared) prepare_forces(0.0);
    // Derivatives consume the previous completed force callback's data.
    // Wheel history and sign/lock fields retain the authentic update epoch.
    for(std::size_t k=0;k<4;++k) {
        auto& wheel=s.wheels[k];PlayerWheelWork w;
        w.spin=static_cast<float>(wheel.spin);w.history_torque=wheel.history_torque;
        w.spin_direction=wheel.spin_direction;w.locked=wheel.brake_locked;
        w.torque=wheel.prepared_torque;w.force_long=wheel.prepared_force_long;
        w.slip=wheel.prepared_slip;w.sliding=wheel.prepared_sliding;
        const float rate=player_wheel_acceleration(h,k,w,static_cast<float>(dt));
        player_wheel_integrate(w,rate,static_cast<float>(dt));
        wheel.spin=w.spin;wheel.history_torque=w.history_torque;wheel.spin_direction=w.spin_direction;
    }
    const float derivative_crank=player_engine_torque(h,s.engine,s.gear,static_cast<float>(throttle));
    player_engine_integrate(h,s.engine,derivative_crank,static_cast<float>(dt));
    player_gear_integrate(h,s.transmission,static_cast<float>(dt));
    s.simulation_seconds+=dt;
    player_engine_cutoff_update(h,s.gear,s.engine,static_cast<float>(s.simulation_seconds));
    // Common585410 consumes old cached V/W, advances P/L and raw axes,
    // then585810 refreshes before rest and the next force callback.
    s.rigid_body.force=rigid_vector(scale(s.prepared_force,1.0/a.unit_scale));
    s.rigid_body.torque=rigid_vector(scale(s.prepared_torque,1.0/a.unit_scale));
    player_rigid_body_integrate(h,s.rigid_body,static_cast<float>(dt));
    rigid_public_cache(s);
    prepare_forces(dt);
    //40BD40 changes live axes only; refreshed snapshot/W are retained.
    player_rigid_body_normalize(s.rigid_body);
    s.orientation=rigid_quaternion(s.rigid_body.axes,s.orientation);
    s.rigid_orientation=s.orientation;

    if (s.shift_hold > 0) {
        --s.shift_hold;
    }
    s.wheel_rpm=static_cast<double>(s.transmission.wheel_engine_speed*kPlayerRadSToRpm);
    if (publication_dt>0.0) s.engine_rpm=player_engine_publish_rpm(h,s.engine,
        static_cast<float>(s.engine_rpm),static_cast<float>(publication_dt));
    if (publication_dt>0.0) {
        s.owner_speeds=player_publish_owner_speeds(s.velocity,rigid_rotation(s.rigid_body.axes).column(0));
        s.published_support=support;s.published_gear=s.gear;
    }
    s.step = s.step + 1;

    if (!state_finite(s)) {
        return false;
    }

    r = rigid_rotation(s.rigid_body.axes);
    VehicleTelemetry t;
    t.step = s.step;
    t.simulation_seconds = s.simulation_seconds;
    t.solver_dt = dt;
    t.alternate_yaw_active = s.alternate_yaw_active;
    t.position = s.position;
    t.velocity = s.velocity;
    t.forward = r.column(0);
    t.angular_velocity = s.angular_velocity;
    t.forward_speed = dot(s.velocity, t.forward);
    t.lateral_speed = dot(s.velocity, r.column(1));
    t.engine_rpm = s.engine_rpm;
    t.wheel_rpm = s.wheel_rpm;
    t.engine_torque = engine_torque;
    t.engine=s.engine;t.transmission=s.transmission;
    t.owner_speeds=s.owner_speeds;
    t.aero=s.aero;t.aero_preloads=s.aero_preloads;t.aero_ready=s.aero_ready;
    t.rigid_body=s.rigid_body;t.rigid_body_ready=s.rigid_body_ready;t.rigid_orientation=s.rigid_orientation;
    t.steering_word=s.steering_word;t.steering_word_ready=s.steering_word_ready;
    t.prepared_force=s.prepared_force;t.prepared_torque=s.prepared_torque;
    t.body_rest_state=s.body_rest_state;t.body_gravity=s.body_gravity;t.forces_prepared=s.forces_prepared;
    t.published_support=s.published_support;t.published_gear=s.published_gear;
    for (std::size_t k=0;k<4;++k) {
        t.wheel_history_torque[k]=s.wheels[k].history_torque;
        t.wheel_spin_direction[k]=s.wheels[k].spin_direction;
        t.wheel_prepared_torque[k]=s.wheels[k].prepared_torque;
        t.wheel_prepared_force_long[k]=s.wheels[k].prepared_force_long;
        t.wheel_prepared_slip[k]=s.wheels[k].prepared_slip;
        t.wheel_prepared_sliding[k]=s.wheels[k].prepared_sliding;
    }
    t.gear = s.gear;
    t.throttle = throttle;
    t.brake = brake;
    t.steer = steer;
    t.handbrake = raw_input.handbrake;
    t.steer_angle = s.steer_angle;
    t.requested_steer_angle = target;
    t.low_support_torque = support_torque;
    t.grounded_wheels = support;
    t.steering_history = raw_input.road_steering ? sanitize(raw_input.steer_history,-1,1) : steer;
    if (raw_input.road_steering) {
        t.steering_limit_deg=raw_input.steer_limit_deg; t.steering_rate_deg_s=raw_input.steer_rate_deg_s;
        t.steering_shift_factor=raw_input.steer_shift_factor; t.steering_mode=raw_input.steering_mode;
    }
    t.wheels = wheel_out;
    for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
        t.wheels[k].spin = s.wheels[k].spin;
        t.wheels[k].compression = s.wheels[k].compression;
        t.wheels[k].brake_locked = s.wheels[k].brake_locked;
    }
    state = s;
    telemetry = t;
    return true;
}

} // namespace nfsu
