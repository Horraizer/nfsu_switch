#include "nfsu/steering.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nfsu {
namespace {
// Small numeric tables under the owner's PR #169 comment 3860 policy.
// Each literal rebuilds the stored binary32 sample; no executable is bundled.
constexpr float base[] = {45,23,11,5.5F,4,3.1F,2.9F,2.9F,2.9F,2.9F};
constexpr float alternate[] = {45,15,11,8,7,7,7,7,7,7};
constexpr float history_gain[] = {1,1.1F,1.2F,1.25F,1.35F,1.45F};
constexpr float response[] = {1,.92F,.78F,.64F,.54F,.48F,.42F,.38F,.34F,.32F};
constexpr float change_gain[] = {1,1.1F,2.2F,3.1F,3.7F,4.5F};
constexpr float rate_gain[] = {1,1.1F,1.25F,1.4F,1.5F,1.65F};
constexpr float predicate0[] = {.65F,.7F,.74F,.77F,.9F};
constexpr float predicate1[] = {.525F,.55F,.575F,.6F,1};
constexpr float shift[] = {0,.2F,.2F,.5F,.5F,.7F,.7F,1};

template<std::size_t N> double curve(const float (&v)[N], float scale, double x) {
    // 0x583EA0 stores u as binary32 for its fraction, but truncates the
    // unrounded x87 value for the index. Preserve the negative-fraction quirk.
    const double unrounded = x*double(scale);
    const int i = static_cast<int>(unrounded);
    if (i < 0) return v[0];
    if (i >= int(N)-1) return v[N-1];
    const float u = static_cast<float>(unrounded);
    double floor = static_cast<int>(u);
    if (floor > u) --floor;
    const double f = double(u)-floor;
    return (1-f)*v[i]+f*v[i+1];
}
double shift_curve(float t) {
    if (t < shift[0]) return shift[1];
    for (std::size_t i=2; i<8; i+=2)
        if (t < shift[i]) return shift[i-1]+(double(t)-shift[i-2])/
            (double(shift[i])-shift[i-2])*(double(shift[i+1])-shift[i-1]);
    return shift[7];
}
template<std::size_t N> void sample(SteeringHistory<N>& h, float value, float time, float window) {
    if (h.count < N) ++h.count;
    // Native writes the intermediate sum, but continues subtraction with
    // the unrounded value still on ST0.
    const double sum = double(value)+h.sum;
    h.sum = static_cast<float>(sum-h.values[h.write]);
    h.values[h.write] = value; h.times[h.write] = time;
    std::size_t expired = 0;
    while (double(time)-h.times[h.oldest] > window && expired < N) {
        if (h.times[h.oldest] > 0) {
            h.sum = static_cast<float>(double(h.sum)-h.values[h.oldest]);
            h.values[h.oldest] = h.times[h.oldest] = 0;
            h.oldest = (h.oldest+1)%N; --h.count;
        }
        ++expired;
    }
    if (expired == N) {
        // 0x5844B0 clears samples, sum, indices/count, retaining average.
        h.values.fill(0); h.times.fill(0); h.sum = 0;
        h.count = h.write = h.oldest = 0; return;
    }
    h.average = static_cast<float>(double(h.sum)/h.count);
    h.write = (h.write+1)%N;
}
template<std::size_t N> void bytes(std::vector<std::uint8_t>& out, const float (&v)[N]) {
    for (float f : v) {
        std::uint32_t bits; std::memcpy(&bits,&f,4);
        for (unsigned i=0;i<4;++i) out.push_back(static_cast<std::uint8_t>(bits>>(8*i)));
    }
}
}
std::vector<std::uint8_t> steering_table_bytes() {
    std::vector<std::uint8_t> out;
    bytes(out,base); bytes(out,alternate); bytes(out,history_gain); bytes(out,response);
    bytes(out,change_gain); bytes(out,rate_gain); bytes(out,predicate0); bytes(out,predicate1); bytes(out,shift);
    return out;
}
bool steering_step(const SteeringContext& c, SteeringState& state, SteeringResult& result) noexcept {
    const float fields[] = {c.raw,c.seconds,c.dt,c.forward_speed,c.speed,
        c.heading_minus_velocity_deg,c.digital_throttle,c.analog_throttle,c.brake,
        c.multiplier,c.predicted_max_speed,c.shift_elapsed};
    for (float f : fields) if (!std::isfinite(f)) return false;
    if (std::abs(c.raw)>1 || c.seconds<0 || c.dt<=0 || c.dt>1 ||
        std::abs(c.forward_speed)>1000 || c.speed<0 || c.speed>2000 ||
        std::abs(c.heading_minus_velocity_deg)>180 || c.multiplier<0 || c.multiplier>4 ||
        c.predicted_max_speed<=0 || c.predicted_max_speed>1000 ||
        c.digital_throttle<0 || c.digital_throttle>1 || c.analog_throttle<0 ||
        c.analog_throttle>1 || c.brake<0 || c.brake>1) return false;
    SteeringState s = state; SteeringResult r;
    sample(s.input,c.raw,c.seconds,.55F); r.filtered = s.input.average;
    double limit;
    if (c.bypass_mode) limit = curve(alternate,.05625F,c.forward_speed);
    else {
        const double factor = 1+double(.8F)*curve(response,.05625F,c.forward_speed)*
            (1-.5*(double(c.digital_throttle)+1-.5*(double(c.analog_throttle)+c.brake)));
        const float first = static_cast<float>(curve(base,.05625F,c.forward_speed)*factor);
        limit = curve(history_gain,5,std::abs(s.input.average))*first;
    }
    float lock = static_cast<float>(limit);
    if (!c.bypass_mode && c.shift_elapsed < 1) {
        const float ratio = static_cast<float>(std::min(1.0,
            double(c.forward_speed)/(double(.7F)*c.predicted_max_speed)));
        r.shift_factor = static_cast<float>(shift_curve(c.shift_elapsed));
        lock = static_cast<float>((r.shift_factor+(1-double(r.shift_factor))*(1-double(ratio)))*lock);
    }
    const float slip = c.forward_speed > 1 ? c.heading_minus_velocity_deg : 0;
    const float abs_slip = std::abs(slip);
    if (s.assist) {
        if (abs_slip < 5 || c.speed < 17.88109F) s.assist = false;
    } else if (abs_slip > 7 && c.speed >= 13.410818F) s.assist = true;
    if (double(c.raw)*slip < 0) {
        if (s.assist || abs_slip > lock) {
            const double input = c.bypass_mode ? 1 : c.predicate ?
                curve(predicate1,4,std::abs(c.raw)) : curve(predicate0,4,std::abs(c.raw));
            lock = static_cast<float>(std::min(input*45,
                (1+double(r.shift_factor)*double(.6666667F))*input*abs_slip));
        }
    } else if (!c.bypass_mode && std::abs(s.input.average)>=.5F) {
        // The retail decay constant 0x6B738C is exactly zero.
        lock = std::max(lock,s.previous_limit);
    }
    r.limit_deg = s.previous_limit = std::min(lock,45.0F);
    r.target_deg = static_cast<float>(std::min(45.0,double(r.limit_deg)*c.raw*c.multiplier));
    float angle = r.target_deg;
    if (!c.bypass_mode) {
        const float change = static_cast<float>(curve(change_gain,.5F,
            std::abs((double(c.raw)-s.previous_raw)/c.dt)));
        sample(s.rate,change,c.seconds,.15F); r.rate_history = s.rate.average;
        const double speed_rate = curve(response,.05625F,c.forward_speed)*110;
        const float first = static_cast<float>(speed_rate*s.rate.average);
        r.rate_deg_s = static_cast<float>(curve(rate_gain,5,std::abs(s.input.average))*first);
        double previous = double(s.angle)*(360.0/65536);
        if (previous >= 180) previous -= 360;
        const double delta = double(r.rate_deg_s)*c.dt;
        angle = static_cast<float>(std::clamp(double(angle),previous-delta,previous+delta));
        if (std::abs(angle)<1) angle = 0;
    }
    // Native truncates angle*65536, then performs signed division by 360.
    const int scaled = static_cast<int>(double(angle)*65536);
    r.angle = s.angle = static_cast<std::uint16_t>(scaled/360);
    r.angle_deg = float(double(static_cast<std::int16_t>(r.angle))*(360.0/65536));
    s.previous_raw = c.raw; state = s; result = r; return true;
}
} // namespace nfsu
