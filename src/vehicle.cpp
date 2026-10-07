#include "nfsu/vehicle.h"

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

// 0x006CC960 0.2f and 0x006CC95C 0.8f: the rear drive fraction below which
// the engine speed is read from the front wheels only, and above which from
// the rear wheels only (0x00475850, 0x0047587D).
constexpr double kFrontDriveBelow = static_cast<double>(0.2F);
constexpr double kRearDriveAbove = static_cast<double>(0.8F);

// 0x006CC964, 0.05f: below this throttle, at exactly idle rpm, the engine
// gives no torque at all (0x00475601..0x00475626).
constexpr double kIdleThrottle = static_cast<double>(0.05F);

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
    bool ok = finite(s.position) && finite(s.velocity) && finite(s.angular_velocity) &&
              finite(s.orientation.w) && finite(s.orientation.x) && finite(s.orientation.y) &&
              finite(s.orientation.z) && finite(s.steer_angle) && finite(s.engine_rpm) &&
              finite(s.wheel_rpm);
    for (const VehicleWheelState& w : s.wheels) {
        ok = ok && finite(w.spin) && finite(w.compression) && finite(w.previous_compression);
    }
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
    for (std::size_t g = 0; g < kVehicleGearSlots; ++g) {
        visit(kVehicleGearRatioOffset + g * 4, h.gear_ratios[g]);
    }
    visit(kVehicleIdleRpmOffset, h.idle_rpm);
    visit(kVehicleRedlineRpmOffset, h.redline_rpm);
    visit(kVehicleMaxRpmOffset, h.max_rpm);
    for (std::size_t i = 0; i < kVehicleTorquePoints; ++i) {
        visit(kVehicleTorqueOffset + i * 4, h.torque[i]);
    }
    for (std::size_t i = 0; i < kVehicleEngineBrakePoints; ++i) {
        visit(kVehicleEngineBrakeOffset + i * 4, h.engine_brake[i]);
    }
    visit(kVehicleDragOffset, h.drag);
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
constexpr float kLateralCurves[7][6] = {
    {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},
    {0.0F, 1.2F, 2.3F, 3.0F, 3.0F, 2.8F},
    {0.0F, 1.7F, 3.2F, 4.3F, 5.1F, 5.2F},
    {0.0F, 1.8F, 3.5F, 4.9F, 5.8F, 6.1F},
    {0.0F, 1.83F, 3.6F, 5.0F, 5.96F, 6.4F},
    {0.0F, 1.86F, 3.7F, 5.1F, 6.13F, 6.7F},
    {0.0F, 1.9F, 3.8F, 5.2F, 6.3F, 7.1F},
};

double lateral_curve(unsigned index, double load_kn) noexcept {
    if (index > 6) index = 6;
    return vehicle_uniform_curve(kLateralCurves[index], 6, 0.0, 10.0, load_kn);
}

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
// record 18 (240SX) at file offset 1736752. The 316 record bytes this table
// reproduces, in offset order, hash to
// 54394827CC13AEE7DE095524810B790A238FFC24763E7C2A2BA236A1A2430DBD
// (kVehicle240sxRecordBytesSha256); nfsu_vehicle_tests checks it.
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
    // +0x280 front and +0x2A0 rear struts.
    h.struts[0] = VehicleAxleStrut{5.4F, 35.0F, 5.4F, 5.8F, 37.0F, 0.125F, -0.13F};
    h.struts[1] = VehicleAxleStrut{5.1F, 30.0F, 5.0F, 5.65F, 30.0F, 0.125F, -0.13F};
    // +0x2C0.. the gearbox.
    h.shift_fraction = 0.8F;
    h.final_drive_rear = 4.083F;
    h.final_drive_front = 4.083F;
    h.rear_drive_fraction = 1.0F;
    h.forward_gears = 5;
    h.gear_ratios = {-3.657F, 0.0F, 3.321F, 1.902F, 1.308F, 1.0F, 0.9F, 0.0F};
    // +0x300.. the engine.
    h.idle_rpm = 800.0F;
    h.redline_rpm = 7500.0F;
    h.max_rpm = 8000.0F;
    h.torque = {0.14F, 0.15F, 0.16F, 0.18F, 0.21F, 0.22F, 0.19F, 0.165F, 0.145F};
    h.engine_brake = {0.12F, 0.18F, 0.13F};
    h.drag = 0.0004F;
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
    positive(kVehicleFinalRearOffset, h.final_drive_rear, "the rear final drive");
    positive(kVehicleFinalFrontOffset, h.final_drive_front, "the front final drive");
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
        e.u32(w.contact ? 1U : 0U);
        e.u32(w.sliding ? 1U : 0U);
    }
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
    // 0x00475460: the static preload of each strut, in its own operation
    // order. With the retail flag at 0x006FFDE4 set, cg is the centre of
    // mass's first component; a = wheel 0 - cg, L = a - (wheel 3 - cg),
    // rear = a / L * 0.5 per rear wheel, front = 0.5 - rear per front wheel,
    // each times the weight. The recovered total also adds a speed term,
    // the argument times +0x37C, which is not reproduced (its argument,
    // built at 0x004745E6..0x00474610, was not traced), so this is the
    // preload at rest.
    const double cg = static_cast<double>(h.centre_of_mass[0]);
    const double to_front = static_cast<double>(h.wheels[0].longitudinal) - cg;
    const double to_rear = static_cast<double>(h.wheels[3].longitudinal) - cg;
    const double wheelbase = to_front - to_rear;
    const double rear_share = to_front / wheelbase * 0.5;
    const double front_share = 0.5 - rear_share;
    for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
        d_.preload[k] = d_.weight * (k < 2 ? front_share : rear_share);
    }
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
    d_.drag = static_cast<double>(h.drag) * unit;

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

// 0x004757D0: engine speed from the driven wheels' rolling speed, then the
// automatic gearbox's up and down loops, then the clamp to [idle, max].
void VehicleModel::update_engine(VehicleState& state) const noexcept {
    const VehicleHandling& h = spec_.handling;
    const double rear = static_cast<double>(h.rear_drive_fraction);
    const double final_rear = static_cast<double>(h.final_drive_rear);
    const double final_front = static_cast<double>(h.final_drive_front);
    const auto& w = state.wheels;
    double wheel_term = 0.0;
    const double front_term = (w[1].spin + w[0].spin) * final_rear * 0.5 * kRadPerSecondToRpm;
    const double rear_term = (w[3].spin + w[2].spin) * final_front * 0.5 * kRadPerSecondToRpm;
    if (rear < kFrontDriveBelow) {
        wheel_term = front_term;
    } else if (!(rear > kRearDriveAbove)) {
        wheel_term = (front_term + rear_term) * 0.5;
    } else {
        wheel_term = rear_term;
    }

    const int top = kVehicleGearFirst + static_cast<int>(h.forward_gears) - 1;
    const auto ratio = [&](int gear) {
        return static_cast<double>(h.gear_ratios[static_cast<std::size_t>(gear)]);
    };
    double rpm = wheel_term * ratio(state.gear);
    if (state.gearbox == VehicleGearbox::automatic && state.gear != kVehicleGearReverse) {
        // 0x00475900..0x00475905 forces first gear without recomputing the
        // speed, so the loops below and the clamp see the old slot's rpm
        // for this one call.
        if (state.gear < kVehicleGearFirst) {
            state.gear = kVehicleGearFirst;
        }
        // The hold is an APPROXIMATION: the recovered rule shifts at once and
        // has no hysteresis, and this model's tyre slip drops the wheel speed
        // across an upshift by more than the rule's margin, which without a
        // hold shifts back down on the next step.
        if (state.shift_hold == 0) {
            const int before = state.gear;
            while (state.gear < top && rpm > upshift_[static_cast<std::size_t>(state.gear)]) {
                ++state.gear;
                rpm = wheel_term * ratio(state.gear);
            }
            while (state.gear > kVehicleGearFirst &&
                   rpm < downshift_[static_cast<std::size_t>(state.gear)]) {
                --state.gear;
                rpm = wheel_term * ratio(state.gear);
            }
            if (state.gear != before) {
                state.shift_hold = spec_.approx.auto_shift_hold_steps;
            }
        }
    }
    state.wheel_rpm = rpm;
    const double idle = static_cast<double>(h.idle_rpm);
    const double max = static_cast<double>(h.max_rpm);
    state.engine_rpm = rpm < idle ? idle : (rpm > max ? max : rpm);
}

// 0x00475588..0x00475626: the torque curve under throttle less the engine
// braking curve under the rest, nothing at all at exactly idle with the
// throttle closed. Two APPROXIMATIONS keep that sum from doing what an engine
// cannot: the recovered code only clamps the stored engine speed, so above
// the maximum the throttle part is cut here to keep a manual gearbox from
// driving past it; and the engine braking part is a drag on a crank the
// wheels are turning, so it acts only while the wheel-derived speed is above
// idle and never pushes a slow car backwards, and it is scaled by
// engine_brake_scale (the recovered wheel coupling that follows at
// 0x00475667 was not reproduced, so how much of it reaches a wheel in the
// game is unknown).
double VehicleModel::crank_torque(const VehicleState& state, double throttle) const noexcept {
    const double rpm = state.engine_rpm;
    double drive = torque_curve(rpm) * throttle;
    if (state.wheel_rpm > static_cast<double>(spec_.handling.max_rpm)) {
        drive = 0.0;
    }
    double braking = engine_brake_curve(rpm) * (1.0 - throttle) * spec_.approx.engine_brake_scale;
    if (!(state.wheel_rpm > static_cast<double>(spec_.handling.idle_rpm))) {
        braking = 0.0;
    }
    double torque = drive - braking;
    if (!(throttle >= kIdleThrottle) && rpm == static_cast<double>(spec_.handling.idle_rpm)) {
        torque = 0.0;
    }
    return torque * spec_.approx.unit_scale;
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
    update_engine(next);
    next.shift_hold = 0;
    state = next;
    return true;
}

bool VehicleModel::step(VehicleState& state, const VehicleInput& raw_input,
                        const VehicleGround& ground, VehicleTelemetry& telemetry) const {
    if (!valid_ || !state_finite(state) || state.gear < kVehicleGearReverse ||
        state.gear > kVehicleGearFirst + static_cast<int>(spec_.handling.forward_gears) - 1) {
        return false;
    }
    const VehicleHandling& h = spec_.handling;
    const VehicleApproximations& a = spec_.approx;
    const double dt = kVehicleStepSeconds;
    VehicleState s = state;

    const double throttle = sanitize(raw_input.throttle, 0.0, 1.0);
    // 0x00474B4B..0x00474B6F: the brake the wheels see is the larger of the
    // processed service brake and half the handbrake axis. That axis is the
    // handbrake's own merged input (0x00461E10: its digital 0x91 code wins
    // over its analog 0x92 code, doubled and clamped to 0xFF), not the
    // service brake; the handbrake has no torque path of its own, so it
    // cannot lock the driven wheels at speed and pin the wheel-derived
    // engine speed at idle.
    const double brake = std::max(sanitize(raw_input.brake, 0.0, 1.0),
                                  raw_input.handbrake ? 0.5 : 0.0);
    const double steer = sanitize(raw_input.steer, -1.0, 1.0);

    Mat3 r = rotation(s.orientation);
    const VehicleVec3 forward = r.column(0);
    const VehicleVec3 left = r.column(1);
    const VehicleVec3 up = r.column(2);
    const double forward_speed = dot(s.velocity, forward);

    // Gear requests, then the engine speed for the gear now selected.
    const int top = kVehicleGearFirst + static_cast<int>(h.forward_gears) - 1;
    if (raw_input.shift != VehicleShift::none) {
        const bool up_request = raw_input.shift == VehicleShift::up;
        if (s.gearbox == VehicleGearbox::manual) {
            if (up_request && s.gear < top) {
                ++s.gear;
            } else if (!up_request && s.gear > kVehicleGearReverse) {
                --s.gear;
            }
        } else if (up_request && s.gear == kVehicleGearReverse) {
            s.gear = kVehicleGearFirst;
        } else if (!up_request && s.gear == kVehicleGearFirst && absolute(forward_speed) < 1.0) {
            s.gear = kVehicleGearReverse;
        }
        update_engine(s);
    }

    // Steering: toward the input times the speed-dependent lock, at a
    // limited rate (APPROXIMATION).
    const double speed_ratio = forward_speed / a.steer_speed_reference;
    const double lock = a.max_steer / (1.0 + speed_ratio * speed_ratio);
    const double target = raw_input.road_steering ?
        sanitize(raw_input.road_target,-3.14159265358979323846,3.14159265358979323846) : steer * lock;
    const double max_change = a.steer_rate * dt;
    if (raw_input.road_steering) s.steer_angle =
        sanitize(raw_input.road_angle,-3.14159265358979323846,3.14159265358979323846);
    else s.steer_angle = s.steer_angle + clamp(target - s.steer_angle, -max_change, max_change);
    const double steer_cos = vehicle_cos(s.steer_angle);
    const double steer_sin = vehicle_sin(s.steer_angle);

    // The engine's torque at the speed the previous step left, and its share
    // at each wheel (0x0047562C..0x00475661).
    const double engine_torque = crank_torque(s, throttle);
    const double gear_ratio = static_cast<double>(h.gear_ratios[static_cast<std::size_t>(s.gear)]);
    const double rear_fraction = static_cast<double>(h.rear_drive_fraction);

    VehicleVec3 force = v3(0.0, 0.0, -d_.weight);
    VehicleVec3 torque;
    std::array<VehicleWheelTelemetry, kVehicleWheelCount> wheel_out{};

    // Struts: probe, compression, force.
    std::array<VehicleVec3, kVehicleWheelCount> attach_world{};
    std::array<VehicleGroundHit, kVehicleWheelCount> hits{};
    std::array<double, kVehicleWheelCount> raw_compression{};
    for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
        const std::size_t axle = k / 2;
        attach_world[k] = add(s.position, r.apply(d_.attach[k]));
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
        raw_compression[k] = hits[k].hit
                                 ? d_.radius[k] - (hits[k].distance - a.probe_lift)
                                 : d_.droop_limit[axle] - 1.0;
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
        const double x = wheel.compression;
        const double rate = (x - wheel.previous_compression) / dt;
        const double positive = x > 0.0 ? x : 0.0;
        double f = (d_.progressive[axle] * positive + 1.0) * d_.spring[axle] * x;
        f = f + (rate > 0.0 ? d_.damping_bump[axle] : d_.damping_rebound[axle]) * rate;
        f = f + d_.preload[k];
        f = f + (x - s.wheels[kAntiRollPartner[k]].compression) * d_.anti_roll[axle];
        f = f + overflow[k] * a.bump_stop_rate;
        strut_force[k] = f > 0.0 ? f : 0.0;
    }

    // Tyres and wheels.
    for (std::size_t k = 0; k < kVehicleWheelCount; ++k) {
        const std::size_t axle = k / 2;
        VehicleWheelState& wheel = s.wheels[k];
        VehicleWheelTelemetry& out = wheel_out[k];
        const double radius = d_.radius[k];
        const double fraction = k < 2 ? 1.0 - rear_fraction : rear_fraction;
        const double final_drive =
            static_cast<double>(k < 2 ? h.final_drive_front : h.final_drive_rear);
        const double drive = final_drive * gear_ratio * engine_torque * fraction * 0.5;
        const double brake_torque = brake * (k < 2 ? a.brake_torque_front : a.brake_torque_rear);
        out.drive_torque = drive;
        out.compression = wheel.compression;
        out.contact = wheel.contact;
        out.load = strut_force[k];
        out.surface = hits[k].hit ? hits[k].surface : 0;

        const double inertia = a.wheel_inertia;
        const auto apply_brake = [&](double spin) {
            if (!(brake_torque > 0.0)) {
                return spin;
            }
            const double change = dt * brake_torque / inertia;
            if (absolute(spin) <= change) {
                return 0.0;
            }
            return spin > 0.0 ? spin - change : spin + change;
        };

        if (!wheel.contact) {
            wheel.spin = apply_brake(wheel.spin + dt * drive / inertia);
            out.spin = wheel.spin;
            out.drive_applied = drive;
            continue;
        }

        const double load = strut_force[k];
        const VehicleVec3 normal = hits[k].normal;
        const VehicleVec3 heading =
            k < 2 ? add(scale(forward, steer_cos), scale(left, steer_sin)) : forward;
        VehicleVec3 along = sub(heading, scale(normal, dot(heading, normal)));
        const double along_length = vehicle_sqrt(dot(along, along));
        const VehicleVec3 from = add(attach_world[k], scale(up, a.probe_lift));
        const VehicleVec3 contact = add(from, scale(up, -hits[k].distance));
        const VehicleVec3 arm_contact = sub(contact, s.position);
        out.ground_point = contact; out.ground_normal = normal;

        // The strut's force reaches the body as the ground's reaction: along
        // the surface normal, at the contact. Along the body's up axis it
        // would push a pitched car along the ground at rest (APPROXIMATION of
        // the recovered application point, which was not traced).
        const VehicleVec3 strut = scale(normal, load);
        force = add(force, strut);
        torque = add(torque, cross(arm_contact, strut));

        if (!(along_length > 1.0e-9)) {
            wheel.spin = apply_brake(wheel.spin + dt * drive / inertia);
            out.spin = wheel.spin;
            out.drive_applied = drive;
            continue;
        }
        along = scale(along, 1.0 / along_length);
        const VehicleVec3 across = cross(normal, along);
        const VehicleVec3 point_velocity =
            add(s.velocity, cross(s.angular_velocity, arm_contact));
        const double v_long = dot(point_velocity, along);
        const double v_lat = dot(point_velocity, across);
        const double denominator =
            absolute(v_long) > a.slip_speed_floor ? absolute(v_long) : a.slip_speed_floor;
        // 0x00587740, Unicorn-verified: the slip angle (against the same
        // floored denominator, so a car at rest has none) selects and
        // blends the load curves - p = slip x 343.775 / slip_scale, the
        // 240SX's 12 making p exactly half the slip in degrees - and each
        // curve is evaluated at x = 0.8 x the wheel's load (the record's
        // kN) / lateral_load_scale, the blend scaled by lateral_scale and
        // 2.5 (0x6F086C). The sign returns separately at 0x00587718.
        const VehicleAxleTyre& tyre = h.tyres[axle];
        const double slip = vehicle_atan((v_lat < 0.0 ? -v_lat : v_lat) / denominator);
        const double p = 343.775 * slip / static_cast<double>(tyre.slip_scale);
        const double x = 0.8 * (load / 1000.0) /
            static_cast<double>(tyre.lateral_load_scale);
        double curve_force = lateral_curve(6, x);
        if (p < 6.0) {
            const auto idx = static_cast<unsigned>(p);
            const double frac = p - static_cast<double>(idx);
            curve_force = lateral_curve(idx, x) * (1.0 - frac) +
                          lateral_curve(idx + 1, x) * frac;
        }
        double fy = -curve_force * static_cast<double>(tyre.lateral_scale) * 2500.0 *
                    (v_lat < 0.0 ? -1.0 : 1.0);
        const double peak = d_.static_friction[axle] * load;
        const double slide = d_.sliding_friction[axle] * load;

        // The traction limit (APPROXIMATION): the engine's torque at this
        // wheel, driving or braking, is held to what the peak friction
        // leaves beside the lateral force the slip angle asks for. Where
        // that force alone passes the peak, vehicle_sqrt's answer for a
        // negative argument, 0, leaves the engine no torque at this wheel.
        double applied = drive;
        if (a.traction_limit) {
            const double limit = vehicle_sqrt(peak * peak - fy * fy) * radius;
            applied = clamp(drive, -limit, limit);
        }
        out.drive_applied = applied;

        // Longitudinal: the wheel's spin solved implicitly against a linear
        // tyre, with the brake as a friction torque that can lock it.
        const double k_long = a.longitudinal_stiffness * load / denominator;
        const double implicit_mass = inertia + dt * k_long * radius * radius;
        const double base = inertia * wheel.spin + dt * (applied + k_long * radius * v_long);
        double spin = base / implicit_mass;
        if (brake_torque > 0.0) {
            if (spin == 0.0) {
                // held
            } else {
                const double direction = spin > 0.0 ? 1.0 : -1.0;
                const double braked = (base - dt * direction * brake_torque) / implicit_mass;
                spin = braked * direction > 0.0 ? braked : 0.0;
            }
        }
        double fx = k_long * (spin * radius - v_long);

        // The friction circle: up to the static coefficient the linear
        // force stands; past it the force falls toward the sliding
        // coefficient (APPROXIMATION of the shape; the two coefficients are
        // the tyre block's +0x0C and +0x10).
        const double demand = vehicle_sqrt(fx * fx + fy * fy);
        bool sliding = false;
        if (demand > peak) {
            const double limit = slide + (peak - slide) * (peak / demand);
            const double factor = limit / demand;
            fx = fx * factor;
            fy = fy * factor;
            sliding = true;
            spin = apply_brake(wheel.spin + dt * (applied - fx * radius) / inertia);
        }
        wheel.spin = spin;

        const double fade = clamp(v_long / a.rolling_fade_speed, -1.0, 1.0);
        const double rolling = -a.rolling_resistance * load * fade;
        const VehicleVec3 tyre_force = add(scale(along, fx + rolling), scale(across, fy));
        force = add(force, tyre_force);
        // The tyre's force acts tyre_force_lift of the way from the contact
        // up to the centre of mass's height along the body's vertical
        // (APPROXIMATION, docs/VEHICLE.md).
        const VehicleVec3 arm_tyre =
            sub(arm_contact, scale(up, dot(arm_contact, up) * a.tyre_force_lift));
        torque = add(torque, cross(arm_tyre, tyre_force));

        out.spin = spin;
        out.slip_ratio = (spin * radius - v_long) / denominator;
        out.slip_lateral = v_lat / denominator;
        out.force_long = fx;
        out.force_lat = fy;
        out.sliding = sliding;
    }

    // Aerodynamic drag, magnitude drag * v^2 as 0x0045688D reads it, against
    // the velocity.
    const double speed = vehicle_sqrt(dot(s.velocity, s.velocity));
    force = add(force, scale(s.velocity, -d_.drag * speed));

    // #275: 0x00474DD0..0x00474E85, car state != 1 and < 2 grounded
    // wheels. This model only meets kind-3 track bodies (state remains 0).
    // The separate zero-wheel alignment branch is not recovered here.
    unsigned support = 0;
    for (const auto& wheel : s.wheels) support += wheel.contact ? 1U : 0U;
    const VehicleVec3 support_torque =
        vehicle_low_support_torque(s.angular_velocity, d_.mass, support, 0);
    if (support < 2) torque = add(torque, support_torque);

    // Semi-implicit Euler.
    s.velocity = add(s.velocity, scale(force, dt / d_.mass));
    const VehicleVec3 body_torque = r.apply_transpose(torque);
    const VehicleVec3 body_change =
        v3(body_torque.x / d_.inertia.x, body_torque.y / d_.inertia.y, body_torque.z / d_.inertia.z);
    s.angular_velocity = add(s.angular_velocity, scale(r.apply(body_change), dt));
    s.position = add(s.position, scale(s.velocity, dt));
    VehicleQuat spin_q;
    spin_q.w = 0.0;
    spin_q.x = s.angular_velocity.x;
    spin_q.y = s.angular_velocity.y;
    spin_q.z = s.angular_velocity.z;
    const VehicleQuat change = multiply(spin_q, s.orientation);
    VehicleQuat q = s.orientation;
    q.w = q.w + change.w * (0.5 * dt);
    q.x = q.x + change.x * (0.5 * dt);
    q.y = q.y + change.y * (0.5 * dt);
    q.z = q.z + change.z * (0.5 * dt);
    s.orientation = normalized(q);

    if (s.shift_hold > 0) {
        --s.shift_hold;
    }
    update_engine(s);
    s.step = s.step + 1;

    if (!state_finite(s)) {
        return false;
    }

    r = rotation(s.orientation);
    VehicleTelemetry t;
    t.step = s.step;
    t.position = s.position;
    t.velocity = s.velocity;
    t.forward = r.column(0);
    t.angular_velocity = s.angular_velocity;
    t.forward_speed = dot(s.velocity, t.forward);
    t.lateral_speed = dot(s.velocity, r.column(1));
    t.engine_rpm = s.engine_rpm;
    t.wheel_rpm = s.wheel_rpm;
    t.engine_torque = engine_torque;
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
    }
    state = s;
    telemetry = t;
    return true;
}

} // namespace nfsu
