#include "nfsu/chase_camera.h"
#include "nfsu/player_powertrain.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// The chase camera of issue #59. include/nfsu/chase_camera.h states each
// rule with its address and docs/CONTROLS_CAMERA.md labels it. Plain IEEE
// arithmetic with contraction disabled by cmake/drive_libraries.cmake; the
// square root, sine and cosine are the vehicle library's, the arctangent is
// below, and nothing calls a library mathematics function.

#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "the chase camera needs double arithmetic without excess precision"
#endif

namespace nfsu {

namespace {

constexpr double kPi = 3.14159265358979311600e+00;
constexpr double kPiOver2 = 1.57079632679489655800e+00;
constexpr double kUnitsPerRadian = kChaseAngleUnitsPerTurn / (2.0 * kPi);
constexpr double kRadiansPerUnit = (2.0 * kPi) / kChaseAngleUnitsPerTurn;

bool finite(double value) noexcept {
    return (value - value) == 0.0;
}

bool finite(const VehicleVec3& v) noexcept {
    return finite(v.x) && finite(v.y) && finite(v.z);
}

double absolute(double value) noexcept {
    return value < 0.0 ? -value : value;
}

VehicleVec3 add(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

VehicleVec3 sub(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

VehicleVec3 scale(const VehicleVec3& a, double s) noexcept {
    return {a.x * s, a.y * s, a.z * s};
}

double dot(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

VehicleVec3 cross(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// v rotated by the unit quaternion q: v + 2w (q x v) + 2 q x (q x v).
VehicleVec3 rotate(const VehicleQuat& q, const VehicleVec3& v) noexcept {
    const VehicleVec3 u{q.x, q.y, q.z};
    const VehicleVec3 t = scale(cross(u, v), 2.0);
    return add(add(v, scale(t, q.w)), cross(u, t));
}

std::uint16_t read_u16(const unsigned char* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8U));
}

std::uint32_t read_u32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}

float read_f32(const unsigned char* p) noexcept {
    const std::uint32_t bits = read_u32(p);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void write_u16(unsigned char* p, std::uint16_t value) noexcept {
    p[0] = static_cast<unsigned char>(value & 0xFFU);
    p[1] = static_cast<unsigned char>(value >> 8U);
}

void write_u32(unsigned char* p, std::uint32_t value) noexcept {
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<unsigned char>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU);
    }
}

void write_f32(unsigned char* p, float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32(p, bits);
}

std::string hex_offset(std::size_t offset) {
    static const char* const kDigits = "0123456789ABCDEF";
    std::string text = "+0x";
    bool leading = true;
    for (int shift = 12; shift >= 0; shift -= 4) {
        const unsigned digit = static_cast<unsigned>((offset >> static_cast<unsigned>(shift)) & 0xFU);
        if (digit == 0 && leading && shift != 0) {
            continue;
        }
        leading = false;
        text += kDigits[digit];
    }
    return text;
}

bool chase_pov(int pov) noexcept {
    return pov == 0 || pov == 1 || pov == 4;
}

// 0x00583F20 on the two blend pairs: at or below the first speed its
// weight, at or above the last its weight, linear between.
double blend_weight(const ChaseCameraRules& rules, double speed) noexcept {
    const double x0 = static_cast<double>(rules.blend_speed_low);
    const double y0 = static_cast<double>(rules.blend_weight_low);
    const double x1 = static_cast<double>(rules.blend_speed_high);
    const double y1 = static_cast<double>(rules.blend_weight_high);
    if (!(speed > x0)) {
        return y0;
    }
    if (!(speed < x1)) {
        return y1;
    }
    return y0 + (speed - x0) / (x1 - x0) * (y1 - y0);
}

std::int16_t wrap16(std::int32_t value) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(value & 0xFFFF));
}

// _ftol2 (0x00674898): toward zero.
std::int32_t toward_zero(double value) noexcept {
    if (!(value > -2147483648.0) || !(value < 2147483648.0)) {
        return 0;
    }
    return static_cast<std::int32_t>(value);
}

bool check_rules(const ChaseCameraRules& rules, std::string& error) {
    const float values[] = {rules.blend_speed_low, rules.blend_weight_low, rules.blend_speed_high,
                            rules.blend_weight_high, rules.lag_scale, rules.lag_gain};
    for (const float value : values) {
        if (!finite(static_cast<double>(value))) {
            error = "a rule constant is not finite";
            return false;
        }
    }
    if (!(rules.blend_speed_low < rules.blend_speed_high)) {
        error = "the blend speeds must rise";
        return false;
    }
    if (!(rules.lag_scale > 0.0F) || !(rules.lag_gain > 0.0F)) {
        error = "the lag constants must be above 0";
        return false;
    }
    for (const std::uint16_t fov : rules.fov) {
        if (fov == 0 || fov >= 32768U) {
            error = "a field of view must be above 0 and below half a turn";
            return false;
        }
    }
    return true;
}

bool check_approximations(const ChaseCameraApproximations& a, std::string& error) {
    if (a.fov_mode != ChaseFovMode::retail && a.fov_mode != ChaseFovMode::horizontal && a.fov_mode != ChaseFovMode::vertical_4_3) {
        error = "fov_mode is not a known mode";
        return false;
    }
    if (!finite(a.near_plane) || !finite(a.far_plane) || !(a.near_plane > 0.0) ||
        !(a.far_plane > a.near_plane) || a.far_plane > 100000.0) {
        error = "the clip planes need 0 < near < far <= 100000";
        return false;
    }
    if (!finite(a.collision_margin) || a.collision_margin < 0.0 || a.collision_margin > 10.0) {
        error = "collision_margin must be within 0..10 m";
        return false;
    }
    if (!finite(a.collision_return_rate) || !(a.collision_return_rate > 0.0) ||
        a.collision_return_rate > 1000.0) {
        error = "collision_return_rate must be above 0 and at most 1000 m/s";
        return false;
    }
    if (!finite(a.lag_factor) || a.lag_factor < 0.0 || a.lag_factor > 1.0) {
        error = "lag_factor must be within 0..1";
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Angles
// ---------------------------------------------------------------------------

double chase_atan(double x) noexcept {
    // fdlibm s_atan.c: the reduction to |x| < 7/16 through four breakpoints
    // and its eleven-term odd polynomial, basic operations only.
    static const double kAtanHi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01,
                                     9.82793723247329054082e-01, 1.57079632679489655800e+00};
    static const double kAtanLo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17,
                                     1.39033110312309984516e-17, 6.12323399573676603587e-17};
    static const double kAt[] = {3.33333333333329318027e-01,  -1.99999999998764832476e-01,
                                 1.42857142725034663711e-01,  -1.11111104054623557880e-01,
                                 9.09088713343650656196e-02,  -7.69187620504482999495e-02,
                                 6.66107313738753120669e-02,  -5.83357013379057348645e-02,
                                 4.97687799461593236017e-02,  -3.65315727442169155270e-02,
                                 1.62858201153657823623e-02};
    if (x != x) {
        return 0.0;
    }
    const bool negative = x < 0.0;
    double ax = negative ? -x : x;
    if (ax >= 7.3786976294838206464e19) {  // 2^66
        return negative ? -(kAtanHi[3] + kAtanLo[3]) : kAtanHi[3] + kAtanLo[3];
    }
    int id = -1;
    if (ax < 0.4375) {
        if (ax < 1.862645149230957031e-09) {  // 2^-29
            return x;
        }
    } else if (ax < 1.1875) {
        if (ax < 0.6875) {
            id = 0;
            ax = (2.0 * ax - 1.0) / (2.0 + ax);
        } else {
            id = 1;
            ax = (ax - 1.0) / (ax + 1.0);
        }
    } else if (ax < 2.4375) {
        id = 2;
        ax = (ax - 1.5) / (1.0 + 1.5 * ax);
    } else {
        id = 3;
        ax = -1.0 / ax;
    }
    const double z = ax * ax;
    const double w = z * z;
    const double s1 =
        z * (kAt[0] + w * (kAt[2] + w * (kAt[4] + w * (kAt[6] + w * (kAt[8] + w * kAt[10])))));
    const double s2 = w * (kAt[1] + w * (kAt[3] + w * (kAt[5] + w * (kAt[7] + w * kAt[9]))));
    double result = 0.0;
    if (id < 0) {
        result = ax - ax * (s1 + s2);
    } else {
        result = kAtanHi[id] - ((ax * (s1 + s2) - kAtanLo[id]) - ax);
    }
    return negative ? -result : result;
}

std::uint16_t chase_angle(double x, double y) noexcept {
    if (!finite(x) || !finite(y) || (x == 0.0 && y == 0.0)) {
        return 0;
    }
    const double ax = absolute(x);
    const double ay = absolute(y);
    double angle = ay <= ax ? chase_atan(ay / ax) : kPiOver2 - chase_atan(ax / ay);
    if (x < 0.0) {
        angle = kPi - angle;
    }
    if (y < 0.0) {
        angle = -angle;
    }
    const double units = angle * kUnitsPerRadian;
    const double rounded = units < 0.0 ? units - 0.5 : units + 0.5;
    const std::int32_t whole = static_cast<std::int32_t>(rounded);
    return static_cast<std::uint16_t>(whole & 0xFFFF);
}

double chase_angle_radians(std::uint16_t units) noexcept {
    return static_cast<double>(units) * kRadiansPerUnit;
}

double chase_angle_degrees(std::uint16_t units) noexcept {
    return static_cast<double>(units) * (360.0 / kChaseAngleUnitsPerTurn);
}

// ---------------------------------------------------------------------------
// The anchors
// ---------------------------------------------------------------------------

bool decode_chase_camera_table(const unsigned char* record,
                               std::size_t size,
                               ChaseCameraTable& out,
                               std::string& error) {
    if (record == nullptr || size != kVehicleRecordSize) {
        error = "a CarTypeInfo record is " + std::to_string(kVehicleRecordSize) + " bytes";
        return false;
    }
    const unsigned char* block = record + kChaseCameraBlockOffset;
    ChaseCameraTable table;
    table.single_count = read_u32(block);
    table.shared_count = read_u32(block + 4);
    for (int list = 0; list < 2; ++list) {
        const std::size_t base = list == 0 ? 0x10U : 0x60U;
        const std::uint32_t count = list == 0 ? table.single_count : table.shared_count;
        const std::size_t count_offset = kChaseCameraBlockOffset + (list == 0 ? 0U : 4U);
        if (count > kChaseCameraListCapacity) {
            error = hex_offset(count_offset) + ": " + std::to_string(count) +
                    " anchors; the list holds at most 5";
            return false;
        }
        auto& entries = list == 0 ? table.single : table.shared;
        for (std::size_t k = 0; k < kChaseCameraListCapacity; ++k) {
            const unsigned char* p = block + base + 16U * k;
            ChaseCameraAnchor& anchor = entries[k];
            anchor.pov = static_cast<std::int16_t>(read_u16(p));
            anchor.pitch = read_u16(p + 2);
            anchor.along = read_f32(p + 4);
            anchor.height = read_f32(p + 8);
            anchor.across = read_f32(p + 12);
            if (k >= count) {
                continue;
            }
            const std::size_t at = kChaseCameraBlockOffset + base + 16U * k;
            if (anchor.pov < 0 || anchor.pov > 4) {
                error = hex_offset(at) + ": POV " + std::to_string(anchor.pov) +
                        " is outside 0..4";
                return false;
            }
            for (std::size_t j = 0; j < k; ++j) {
                if (entries[j].pov == anchor.pov) {
                    error = hex_offset(at) + ": POV " + std::to_string(anchor.pov) +
                            " is named twice in one list";
                    return false;
                }
            }
            if (chase_pov(anchor.pov)) {
                if (!finite(static_cast<double>(anchor.along)) ||
                    !finite(static_cast<double>(anchor.height)) ||
                    !finite(static_cast<double>(anchor.across))) {
                    error = hex_offset(at + 4) + ": a chase anchor's offsets are not finite";
                    return false;
                }
                if (!(anchor.along < 0.0F)) {
                    error = hex_offset(at + 4) + ": a chase anchor must sit behind the car";
                    return false;
                }
            }
        }
    }
    out = table;
    return true;
}

void encode_chase_camera_table(const ChaseCameraTable& table,
                               unsigned char* record,
                               std::size_t size) noexcept {
    if (record == nullptr || size != kVehicleRecordSize) {
        return;
    }
    unsigned char* block = record + kChaseCameraBlockOffset;
    write_u32(block, table.single_count);
    write_u32(block + 4, table.shared_count);
    for (int list = 0; list < 2; ++list) {
        const std::size_t base = list == 0 ? 0x10U : 0x60U;
        const auto& entries = list == 0 ? table.single : table.shared;
        for (std::size_t k = 0; k < kChaseCameraListCapacity; ++k) {
            unsigned char* p = block + base + 16U * k;
            write_u16(p, static_cast<std::uint16_t>(entries[k].pov));
            write_u16(p + 2, entries[k].pitch);
            write_f32(p + 4, entries[k].along);
            write_f32(p + 8, entries[k].height);
            write_f32(p + 12, entries[k].across);
        }
    }
}

ChaseCameraTable stock_240sx_chase_camera_table() {
    ChaseCameraTable table;
    table.single_count = 5;  // +0xB50
    table.shared_count = 5;  // +0xB54
    // +0xB60: POVs 0 to 4. The words at +0x2 are the retail bytes: 0x01F4
    // on POV 2, 0x0BE0 on POV 4.
    table.single[0] = {0, 0x0000, -5.8F, 1.7F, 0.0F};
    table.single[1] = {1, 0x0000, -4.7F, 1.7F, 0.0F};
    table.single[2] = {2, 0x01F4, 0.716F, 0.79F, 0.0F};
    table.single[3] = {3, 0x0000, -0.195F, 0.805F, -0.44F};
    table.single[4] = {4, 0x0BE0, -5.8F, 3.45F, 0.0F};
    // +0xBB0: the shared-screen list. 0xFAC2 on POV 2, 0x07EB on POV 4.
    table.shared[0] = {0, 0x0000, -8.2F, 1.85F, 0.0F};
    table.shared[1] = {1, 0x0000, -6.8F, 1.7F, 0.0F};
    table.shared[2] = {2, 0xFAC2, 0.716F, 0.64F, 0.0F};
    table.shared[3] = {3, 0x0000, -0.26F, 0.772F, -0.315F};
    table.shared[4] = {4, 0x07EB, -8.2F, 3.45F, 0.0F};
    return table;
}

bool find_chase_anchor(const ChaseCameraTable& table,
                       bool shared,
                       int pov,
                       ChaseCameraAnchor& out) noexcept {
    const std::uint32_t count = shared ? table.shared_count : table.single_count;
    const auto& entries = shared ? table.shared : table.single;
    for (std::size_t k = 0; k < count && k < kChaseCameraListCapacity; ++k) {
        if (entries[k].pov == pov) {
            out = entries[k];
            return true;
        }
    }
    return false;
}

void encode_chase_camera_rules(const ChaseCameraRules& rules,
                               std::array<std::uint8_t, kChaseCameraRuleBytes>& out) noexcept {
    unsigned char* p = out.data();
    for (std::size_t i = 0; i < rules.fov.size(); ++i) {
        write_u16(p + 2 * i, rules.fov[i]);
    }
    write_f32(p + 10, rules.blend_speed_low);
    write_f32(p + 14, rules.blend_weight_low);
    write_f32(p + 18, rules.blend_speed_high);
    write_f32(p + 22, rules.blend_weight_high);
    write_f32(p + 26, rules.lag_scale);
    write_f32(p + 30, rules.lag_gain);
}

// ---------------------------------------------------------------------------
// The world and the subject
// ---------------------------------------------------------------------------

VehicleVec3 ChaseCameraWorld::clear_race_eye(const VehicleVec3& from, const VehicleVec3& to) const {
    const double fraction = clear_fraction(from, to);
    if (!finite(fraction) || fraction >= 1.0) return to;
    const auto arm = sub(to, from);
    const double length = vehicle_sqrt(dot(arm, arm));
    return length > 0.0 ? add(from, scale(arm, std::max(0.0, fraction - .01F / length))) : to;
}

double ChaseCameraOpenWorld::clear_fraction(const VehicleVec3&, const VehicleVec3&) const {
    return 1.0;
}

ChaseCameraSubject chase_subject(const VehicleState& state, const VehicleSpec& spec,
                                 const VehicleVec3& acceleration) noexcept {
    const VehicleVec3 com{static_cast<double>(spec.handling.centre_of_mass[0]),
                          static_cast<double>(spec.handling.centre_of_mass[1]),
                          static_cast<double>(spec.handling.centre_of_mass[2])};
    ChaseCameraSubject subject;
    subject.origin = sub(state.position, rotate(state.orientation, com));
    subject.forward = rotate(state.orientation, VehicleVec3{1.0, 0.0, 0.0});
    subject.body_up = rotate(state.orientation, VehicleVec3{0.0, 0.0, 1.0});
    subject.velocity = state.velocity;
    subject.acceleration = acceleration;
    subject.centre_of_mass = state.position;
    subject.support = state.published_support;
    subject.collision_active = state.collision_state != 0;
    return subject;
}

// ---------------------------------------------------------------------------
// The rules
// ---------------------------------------------------------------------------

std::uint16_t chase_target_heading(const ChaseCameraRules& rules,
                                   const ChaseCameraSubject& subject) noexcept {
    const double fx = subject.forward.x;
    const double fy = subject.forward.y;
    double vx = subject.velocity.x;
    double vy = subject.velocity.y;
    // 0x0048A0B6..0x0048A119: a velocity that points backwards along the
    // heading is reflected off it, v + (-2 (f . v)) f, so a reversing car
    // does not swing the camera round to face it.
    const double along = fy * vy + vx * fx;
    if (along < 0.0) {
        const double k = along * -2.0;
        vx = vx + fx * k;
        vy = fy * k + vy;
    }
    // 0x0048A11D..0x0048A13F: the speed and its blend weight.
    const double speed = vehicle_sqrt(vy * vy + vx * vx);
    const double t = blend_weight(rules, speed);
    // 0x0048A148..0x0048A17B: the velocity scaled to length t, through
    // speed / t and its reciprocal, as the recovered code divides. A weight
    // of 0 - below 8 m/s - leaves no velocity term, which is what the
    // recovered 1 / infinity gives; at exactly zero speed the recovered
    // 0 / 0 is not reproduced and the term is 0 too.
    double ax = 0.0;
    double ay = 0.0;
    if (t != 0.0 && speed > 0.0) {
        const double r = speed / t;
        const double s = 1.0 / r;
        ax = vx * s;
        ay = s * vy;
    }
    // 0x0048A18B..0x0048A1C9: (1 - t) f + that, and its angle.
    const double keep = 1.0 - t;
    const double dx = fx * keep + ax;
    const double dy = fy * keep + ay;
    return chase_angle(dx, dy);
}

std::int16_t chase_lag_step(const ChaseCameraRules& rules,
                            double lag_factor,
                            std::int16_t lag,
                            std::int16_t delta,
                            double dt) noexcept {
    // 0x0048A3F2..0x0048A3F8: the lag grows by the target's move, in 16 bits.
    const std::int16_t moved = wrap16(static_cast<std::int32_t>(lag) + delta);
    // 0x00489300: rate = -sign(a) * gain * atan16(scale * |a|), atan16 being
    // bATan(1, x), the angle of the vector (1, x).
    const std::int32_t magnitude = moved < 0 ? -static_cast<std::int32_t>(moved) : moved;
    const double x = static_cast<double>(magnitude) * static_cast<double>(rules.lag_scale);
    const double atan16 = static_cast<double>(chase_angle(1.0, x));
    double rate = atan16 * static_cast<double>(rules.lag_gain);
    rate = -rate;
    if (moved < 0) {
        rate = -rate;
    }
    // 0x0048A400..0x0048A417: times the lag factor and the frame time,
    // truncated toward zero, and added in 16 bits.
    const std::int32_t change = toward_zero(rate * lag_factor * dt);
    return wrap16(static_cast<std::int32_t>(moved) + change);
}

// ---------------------------------------------------------------------------
// The camera
// ---------------------------------------------------------------------------

ChaseCamera::ChaseCamera()
    : ChaseCamera(stock_240sx_chase_camera_table(), ChaseCameraRules{},
                  ChaseCameraApproximations{}) {}

ChaseCamera::ChaseCamera(const ChaseCameraTable& table,
                         const ChaseCameraRules& rules,
                         const ChaseCameraApproximations& approx)
    : table_(table), rules_(rules), approx_(approx) {
    valid_ = false;
    if (!check_rules(rules_, error_) || !check_approximations(approx_, error_)) {
        return;
    }
    if (table_.single_count > kChaseCameraListCapacity ||
        table_.shared_count > kChaseCameraListCapacity) {
        error_ = "an anchor list holds at most 5";
        return;
    }
    bool found = false;
    for (const int pov : std::array<int,4>{{0,1,2,4}}) {
        ChaseCameraAnchor anchor;
        if (!find_chase_anchor(table_, false, pov, anchor)) {
            continue;
        }
        if (!finite(static_cast<double>(anchor.along)) ||
            !finite(static_cast<double>(anchor.height)) ||
            !finite(static_cast<double>(anchor.across)) || (chase_pov(pov) && !(anchor.along < 0.0F))) {
            error_ = "the anchor of POV " + std::to_string(pov) + " is not a chase anchor";
            return;
        }
        if (!found) {
            pov_ = pov;
            found = true;
        }
    }
    if (!found) {
        error_ = "the single-player list names no chase POV (0, 1 or 4)";
        return;
    }
    valid_ = true;
}

bool ChaseCamera::set_pov(int pov) noexcept {
    ChaseCameraAnchor anchor;
    if (!valid_ || !(chase_pov(pov) || pov == 2) || !find_chase_anchor(table_, false, pov, anchor)) {
        return false;
    }
    if (retail_race_mover_ && chase_pov(pov_) != chase_pov(pov)) race_pov_cut_ = true;
    pov_ = pov;
    return true;
}

void ChaseCamera::next_pov(bool drift) noexcept {
    if (!valid_) return;
    const auto* povs=drift?kDriftChasePovs.data():kChasePovs.data();
    const auto count=drift?kDriftChasePovs.size():kChasePovs.size();
    std::size_t current=count-1;
    for(std::size_t i=0;i<count;++i)if(povs[i]==pov_)current=i;
    for(std::size_t step=1;step<=count;++step)
        if(set_pov(povs[(current+step)%count]))return;
}

bool ChaseCamera::reset(const ChaseCameraSubject& subject,
                        const ChaseCameraWorld& world,
                        ChaseCameraView& view) {
    return place(subject, world, 0.0, true, view);
}

bool ChaseCamera::update(const ChaseCameraSubject& subject,
                         double dt,
                         const ChaseCameraWorld& world,
                         ChaseCameraView& view,
                         bool look_back) {
    if (!finite(dt) || !(dt > 0.0) || dt > 0.1) {
        return false;
    }
    return place(subject, world, dt, !started_, view, look_back);
}

float CameraImpactShake::envelope() const noexcept {
    constexpr std::array<float,5> time{{0.F,.1F,.3F,.5F,1.F}};
    constexpr std::array<float,5> strength{{1.F,.7F,.5F,.4F,0.F}};
    if(phase_<0.F||phase_>=1.F)return 0.F;
    for(std::size_t i=1;i<time.size();++i)if(phase_<time[i]) {
        const float slope=(strength[i]-strength[i-1])/(time[i]-time[i-1]);
        return strength[i-1]+(phase_-time[i-1])*slope;
    }
    return 0.F;
}

void CameraImpactShake::observe(const std::array<float,3>& acceleration,bool second_crash_shot) noexcept {
    // 420FD2 reads actor+80 after 426930's completed-world publication.
    const VehicleVec3 weighted{acceleration[0]*.7F,acceleration[1]*.5F,acceleration[2]*2.F};
    if(!finite(weighted))return;
    const double norm=vehicle_sqrt((weighted.z*weighted.z+weighted.x*weighted.x)+weighted.y*weighted.y);
    if(norm<=250.0)return;
    const float t=float((std::min(600.0,norm)-250.0)/350.0);
    const float amplitude=.08F+t*(.16F-.08F);
    if(amplitude<=envelope())return;
    // 574E60 retains the opposite of the normalized, scaled direction.
    const float gain=amplitude*(second_crash_shot?.8F:1.F);
    const auto unit=scale(weighted,-double(gain)/norm);
    impulse_={float(unit.x),float(unit.y),float(unit.z)};
    duration_=(.3F+t*(.7F-.3F))*(second_crash_shot?1.2F:1.F);
    phase_=0.F;
}

void apply_countdown_purge_view(ChaseCameraView& view) noexcept {
    // Isolate the held-grid view response measured in the original type-17
    // mover on Stadium. Keep it out of the arm history and vehicle state.
    const auto right=cross(view.forward,view.up);
    view.eye=add(view.eye,add(scale(view.forward,-.012),scale(right,-.0041)));
    view.eye=add(view.eye,scale(view.up,-.00017));
    const auto direction=add(add(view.forward,scale(right,.00084)),scale(view.up,.00071));
    view.forward=scale(direction,1.0/vehicle_sqrt(dot(direction,direction)));
    const auto adjusted_right=cross(view.forward,view.up);
    view.up=cross(scale(adjusted_right,1.0/vehicle_sqrt(dot(adjusted_right,adjusted_right))),view.forward);
    view.heading=chase_angle(view.forward.x,view.forward.y);
    view.pitch=chase_angle(vehicle_sqrt(view.forward.x*view.forward.x+view.forward.y*view.forward.y),-view.forward.z);
}

void CameraImpactShake::apply(ChaseCameraView& view) const noexcept {
    // 575040 alternates an active and silent half-wave. The envelope comes
    // from the five knots at 6F1390; the phase runs in world time.
    if(phase_>=1.F||(int(17.F*phase_)&1))return;
    const auto value=scale(impulse_,envelope());
    const auto right=cross(view.forward,view.up);
    const double down=-dot(value,view.up);
    const double forward=dot(value,view.forward);
    const double across=dot(value,right);
    const auto pitch=std::uint16_t(int(-1200.F*float(down+forward)));
    const auto yaw=std::uint16_t(int(2000.F*float(across)));
    const double p=chase_angle_radians(pitch),y=chase_angle_radians(yaw);
    const auto tilted=add(scale(view.forward,vehicle_cos(p)),scale(view.up,-vehicle_sin(p)));
    view.up=add(scale(view.up,vehicle_cos(p)),scale(view.forward,vehicle_sin(p)));
    view.forward=add(scale(tilted,vehicle_cos(y)),scale(right,-vehicle_sin(y)));
    view.heading=chase_angle(view.forward.x,view.forward.y);
    view.pitch=chase_angle(vehicle_sqrt(view.forward.x*view.forward.x+view.forward.y*view.forward.y),-view.forward.z);
}

void CameraImpactShake::advance(float world_seconds) noexcept {
    if(phase_<1.F&&world_seconds>0.F)phase_=std::min(1.F,phase_+world_seconds/duration_);
}

void ChaseCamera::CubicChannel::snap(float next) noexcept {
    value = requested = next;
    derivative = 0.F;
    active = pending = false;
}

void ChaseCamera::CubicChannel::advance(float dt, float duration) noexcept {
    // 593D40 rebuilds the coefficients at the current value and derivative
    // for a newly queued target. The endpoint derivative is zero.
    if (pending) {
        const float delta = requested - value;
        cubic = derivative - 2.F * delta;
        quadratic = 3.F * delta - 2.F * derivative;
        linear = derivative;
        initial = value;
        phase = 0.F;
        active = true;
        pending = false;
    }
    if (!active) return;
    phase = duration > .00001F ? phase + dt / duration : 1.F;
    if (phase >= 1.F) {
        snap(requested);
        return;
    }
    value = ((phase * cubic + quadratic) * phase + linear) * phase + initial;
    derivative = (2.F * quadratic + 3.F * cubic * phase) * phase + linear;
}

void ChaseCamera::CubicChannel::request(float next) noexcept {
    requested = next;
    if (next != value) pending = true;
}

bool ChaseCamera::place_race_chase(const ChaseCameraSubject& subject,
                                   const ChaseCameraAnchor& anchor,
                                   const ChaseCameraWorld& world, float dt, bool snap,
                                   bool look_back, ChaseCameraView& view) {
    const auto normalized = [](const VehicleVec3& v, const VehicleVec3& fallback) {
        const double length = vehicle_sqrt(dot(v, v));
        return length > 0.000001 ? scale(v, 1.0 / length) : fallback;
    };
    const auto update = [dt](CubicVector& channels, const VehicleVec3& request,
                             const VehicleVec3& duration, bool reset) {
        const float next[] = {float(request.x), float(request.y), float(request.z)};
        const float seconds[] = {float(duration.x), float(duration.y), float(duration.z)};
        for (std::size_t i = 0; i < channels.size(); ++i) {
            if (reset) channels[i].snap(next[i]);
            else {
                channels[i].advance(dt, seconds[i]);
                channels[i].request(next[i]);
            }
        }
        return VehicleVec3{channels[0].value, channels[1].value, channels[2].value};
    };

    const VehicleVec3 vertical{0.0, 0.0, 1.0};
    const auto body_forward = normalized(subject.forward, {1.0, 0.0, 0.0});
    const auto velocity = subject.velocity;
    const double along = dot(velocity, body_forward);
    const auto reflected = along < 0.0 ? sub(velocity, scale(body_forward, 2.0 * along))
                                       : velocity;
    const double weight = blend_weight(rules_, vehicle_sqrt(dot(velocity, velocity)));
    const bool reset = snap || !race_chase_started_;
    if (snap) {
        race_sample_ = race_samples_ = 0;
        race_acceleration_ = {};
    }

    // 48EC00 averages four acceleration samples before 48B8C0 rotates
    // them into the chase frame. 48B530 holds the request on light contact.
    race_acceleration_[race_sample_] = snap ? VehicleVec3{} : subject.acceleration;
    race_sample_ = (race_sample_ + 1U) % race_acceleration_.size();
    race_samples_ = std::min(race_samples_ + 1U, race_acceleration_.size());
    VehicleVec3 average{};
    for (const auto& sample : race_acceleration_) average = add(average, sample);
    average = scale(average, 1.0 / double(race_samples_));

    const bool light_contact = subject.collision_active && subject.support < 3;
    auto requested_direction = add(scale(body_forward, 1.0 - weight),
                                   scale(normalized(reflected, body_forward), weight));
    race_air_time_ = snap || subject.support > 1 ? 0.F : std::min(2.F, race_air_time_ + dt);
    requested_direction.z *= 1.F - race_air_time_ * .5F;
    if (light_contact && !reset) {
        requested_direction = {race_direction_[0].requested, race_direction_[1].requested,
                               race_direction_[2].requested};
    }

    // 48BD85 samples 705C70 at the fixed 6B6E28=.5. Eye, aim and frame
    // are independent channels. The original uses the body origin here.
    const bool near = anchor.pov == 1;
    const auto frame = normalized(update(race_direction_, requested_direction,
        {.3, .3, near ? .5 : .55}, reset), body_forward);
    const auto left = cross(vertical, frame);
    const auto up = cross(frame, left);
    const double local_x = dot(average, frame);
    const double arm_scale = light_contact ? double(1.3F) : 1.0;
    const auto angle = std::uint16_t(int(anchor.pitch) +
        toward_zero((subject.support > 1 ? world.surface_height_offset(subject.centre_of_mass) : 0.F) *
                    (near ? 100.F : 150.F)));
    const double radians = chase_angle_radians(angle);
    const double target_height = anchor.height + anchor.along * vehicle_sin(radians) / vehicle_cos(radians);
    const bool cut = reset || race_pov_cut_;
    race_pov_cut_ = false;
    const double eye_limit = near ? 1.25 : 1.5;
    const VehicleVec3 eye_request{
        anchor.along * arm_scale + std::clamp(local_x * (near ? -.125 : -.15), -eye_limit, near ? 1.0 : 1.25),
        anchor.across * arm_scale, anchor.height * arm_scale};
    const VehicleVec3 target_request{std::clamp(local_x * -.06, -1.25, 1.25), 0.0, target_height};
    const double eye_duration = 1.0 / arm_scale;
    const auto eye_local = update(race_eye_, eye_request, {eye_duration, eye_duration, eye_duration}, cut);
    const auto target_local = update(race_target_, target_request, {.35, .35, .35}, cut);

    const auto world_point = [&](VehicleVec3 local) {
        if (look_back) { local.x = -local.x; local.y = -local.y; }
        return add(subject.origin, add(add(scale(frame, local.x), scale(left, local.y)), scale(up, local.z)));
    };
    const auto wanted = world_point(eye_local);
    const auto aim = world_point(target_local);
    const auto& pivot = subject.centre_of_mass;
    auto eye = world.clear_race_eye(pivot, wanted);
    // 483B90 keeps the chase eye outside a sphere around the car, moving
    // vertically when the wall forces the eye inside the sphere.
    const double radius = subject.vertical_body_extent + .7F;
    const auto offset = sub(eye, pivot);
    const double distance_squared = dot(offset, offset);
    if (distance_squared < radius * radius) {
        eye.z = pivot.z + vehicle_sqrt(radius * radius - offset.x * offset.x - offset.y * offset.y);
    }
    const auto desired_arm = sub(wanted, pivot);
    const double desired_distance = dot(desired_arm, desired_arm);
    const double reach = desired_distance > 0.0
        ? std::min(1.0, vehicle_sqrt(dot(sub(eye, pivot), sub(eye, pivot)) / desired_distance)) : 1.0;

    view = {};
    view.pov = anchor.pov;
    view.eye = eye;
    view.pivot = pivot;
    const auto initial_forward = normalized(sub(aim, wanted), scale(frame, look_back ? -1.0 : 1.0));
    view.forward = initial_forward;
    // 483AD0 rebuilds the view at the corrected eye while retaining the
    // actor's projected offset from the old optical axis.
    const auto to_actor = sub(pivot, wanted);
    const double depth = dot(to_actor, initial_forward);
    if (dot(sub(eye, wanted), sub(eye, wanted)) > 0.0 && depth > .00001) {
        const auto initial_right = normalized(cross(initial_forward, vertical), scale(left, -1.0));
        const auto initial_up = cross(initial_right, initial_forward);
        const auto base = normalized(sub(pivot, eye), initial_forward);
        const auto right = normalized(cross(base, vertical), initial_right);
        const auto base_up = cross(right, base);
        view.forward = normalized(sub(sub(base, scale(right, dot(to_actor, initial_right) / depth)),
                                      scale(base_up, dot(to_actor, initial_up) / depth)), base);
    }
    view.up = cross(normalized(cross(vertical, view.forward), left), view.forward);
    view.heading = chase_angle(view.forward.x, view.forward.y);
    view.target = chase_angle(frame.x, frame.y);
    if (look_back) view.target = std::uint16_t(view.target + 0x8000U);
    view.lag = wrap16(int(view.target) - int(view.heading));
    view.pitch = chase_angle(vehicle_sqrt(view.forward.x * view.forward.x +
                                         view.forward.y * view.forward.y), -view.forward.z);
    view.fov = rules_.fov[std::size_t(anchor.pov)];
    view.reach = reach;
    view.near_plane = approx_.near_plane;
    view.far_plane = approx_.far_plane;
    race_chase_started_ = started_ = true;
    return true;
}

bool ChaseCamera::place(const ChaseCameraSubject& subject,
                        const ChaseCameraWorld& world,
                        double dt,
                        bool snap,
                        ChaseCameraView& view,
                        bool look_back) {
    if (!valid_ || !finite(subject.origin) || !finite(subject.forward) ||
        !finite(subject.velocity) || !finite(subject.acceleration) || !finite(subject.body_up) ||
        (retail_race_mover_ && !finite(subject.centre_of_mass))) {
        return false;
    }
    ChaseCameraAnchor anchor;
    if (!find_chase_anchor(table_, false, pov_, anchor)) {
        return false;
    }
    if (pov_ == 2) {
        // 489E5E/48A460: fixed POVs use the complete body frame, with no
        // chase-heading spring. The model's x/left/z axes match the anchors.
        VehicleVec3 forward=subject.forward, up=subject.body_up;
        const double f=vehicle_sqrt(dot(forward,forward));
        if (!(f>0)) return false;
        forward=scale(forward,1/f);
        VehicleVec3 left=cross(up,forward);
        const double l=vehicle_sqrt(dot(left,left));
        if (!(l>0)) return false;
        left=scale(left,1/l);up=cross(forward,left);
        const double p=chase_angle_radians(anchor.pitch);
        const double cp=vehicle_cos(p),sp=vehicle_sin(p);
        // 48B89D supplies the actor's full orientation and +60 position
        // for the fixed view; 48B87A uses +50 only for the chase views.
        const auto& origin = retail_race_mover_ ? subject.centre_of_mass : subject.origin;
        view={};view.pov=pov_;view.pivot=origin;
        view.eye=add(add(add(origin,scale(forward,anchor.along)),
            scale(up,anchor.height)),scale(left,anchor.across));
        if (look_back) {forward=scale(forward,-1);left=scale(left,-1);}
        view.forward=sub(scale(forward,cp),scale(up,sp));
        view.up=add(scale(forward,sp),scale(up,cp));
        view.heading=chase_angle(view.forward.x,view.forward.y);
        view.target=view.heading;view.pitch=anchor.pitch;
        view.fov=rules_.fov[2];view.near_plane=approx_.near_plane;
        view.far_plane=approx_.far_plane;view.reach=1;
        started_=true;target_=chase_target_heading(rules_,subject);lag_=0;reach_=1;
        surge_.reset();
        race_chase_started_=false;
        return true;
    }
    if (retail_race_mover_)
        return place_race_chase(subject, anchor, world, float(dt), snap, look_back, view);
    if(snap) {
        surge_.reset();
    }
    else {
        const auto speeds=player_publish_owner_speeds(subject.velocity,subject.forward);
        const float ax=float(subject.acceleration.x),ay=float(subject.acceleration.y),az=float(subject.acceleration.z);
        const float local=(az*float(subject.forward.z)+ay*float(subject.forward.y))+ax*float(subject.forward.x);
        surge_.update(local,speeds.forward,player_owner_slip_angle(subject.velocity,subject.forward),float(dt));
    }
    //489ED2..489F34 builds the inverse-view arm as surge - anchor.along;
    //48A826's inverse turns it back into anchor.along - surge in world space.
    anchor.along-=surge_.offset();
    if(surge_.offset()<0.F)anchor.height+=surge_.offset()*.2F;
    const std::uint16_t target = chase_target_heading(rules_, subject);
    std::int16_t lag = 0;
    if (!snap) {
        const std::int16_t delta =
            wrap16(static_cast<std::int32_t>(target) - static_cast<std::int32_t>(target_));
        lag = chase_lag_step(rules_, approx_.lag_factor, lag_, delta, dt);
    }
    // The camera heading is the target less the lag (0x0048A1D5 turns by
    // the negated target, 0x0048A419 by the lag).
    const std::uint16_t heading = static_cast<std::uint16_t>(
        (static_cast<std::int32_t>(target) - static_cast<std::int32_t>(lag)) & 0xFFFF);
    const double h = chase_angle_radians(heading);
    const double c = vehicle_cos(h);
    const double s = vehicle_sin(h);
    const VehicleVec3 along{c, s, 0.0};
    const VehicleVec3 left{-s, c, 0.0};
    const VehicleVec3 up{0.0, 0.0, 1.0};
    const double height = static_cast<double>(anchor.height);
    const VehicleVec3 pivot = add(subject.origin, scale(up, height));
    const VehicleVec3 wanted = add(add(pivot, scale(along, anchor.along)),
                                   scale(left, anchor.across));
    // The collision: the free fraction of the pivot-to-eye segment, less
    // the margin, reached at once inward and at the return rate outward.
    const VehicleVec3 arm = sub(wanted, pivot);
    const double length = vehicle_sqrt(dot(arm, arm));
    double allowed = 1.0;
    if (length > 0.0) {
        double clear = world.clear_fraction(pivot, wanted);
        if (!finite(clear)) {
            clear = 1.0;
        }
        clear = clear < 0.0 ? 0.0 : (clear > 1.0 ? 1.0 : clear);
        if (clear < 1.0) {
            allowed = clear - approx_.collision_margin / length;
            allowed = allowed < 0.0 ? 0.0 : allowed;
        }
    }
    double reach = allowed;
    if (!snap && allowed > reach_) {
        const double step = length > 0.0 ? approx_.collision_return_rate * dt / length : 1.0;
        reach = reach_ + step < allowed ? reach_ + step : allowed;
    }
    const VehicleVec3 eye = add(pivot, scale(arm, reach));

    const auto pitch_word=anchor.pitch;
    const double pitch = chase_angle_radians(pitch_word);
    const double cp = vehicle_cos(pitch);
    const double sp = vehicle_sin(pitch);

    started_ = true;
    target_ = target;
    lag_ = lag;
    reach_ = reach;

    view = ChaseCameraView{};
    view.pov = pov_;
    view.eye = eye;
    view.forward = {cp * c, cp * s, -sp};
    view.up = {sp * c, sp * s, cp};
    view.pivot = pivot;
    view.heading = heading;
    view.target = target;
    view.lag = lag;
    view.pitch = pitch_word;
    view.fov = rules_.fov[static_cast<std::size_t>(pov_)];
    view.reach = reach;
    view.near_plane = approx_.near_plane;
    view.far_plane = approx_.far_plane;
    if (look_back) {
        // Temporary front-of-car view. Keep the ordinary chase history and
        // selected POV advancing so release restores this frame's normal view.
        // Mirror horizontal offsets; retain height and downward pitch.
        const VehicleVec3 front_arm{-arm.x,-arm.y,arm.z};
        const VehicleVec3 front_eye=add(pivot,front_arm);
        double clear=world.clear_fraction(pivot,front_eye);
        if(!finite(clear)) clear=1;
        clear=clear<0?0:clear>1?1:clear;
        const double front_reach=clear<1&&length>0?
            std::max(0.0,clear-approx_.collision_margin/length):1.0;
        view.eye=add(pivot,scale(front_arm,front_reach));
        view.forward.x=-view.forward.x;view.forward.y=-view.forward.y;
        view.up.x=-view.up.x;view.up.y=-view.up.y;
        view.heading=static_cast<std::uint16_t>(view.heading+0x8000U);
        view.target=static_cast<std::uint16_t>(view.target+0x8000U);
        view.reach=front_reach;
    }
    return true;
}

} // namespace nfsu
