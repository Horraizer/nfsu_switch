#include "nfsu/nitrous.h"

#include <cmath>
#include <limits>

namespace nfsu {
namespace {
constexpr double tick_seconds = static_cast<double>(0.00025000001187436282634735107421875F);
std::int64_t signed_word(std::uint32_t value) noexcept {
    return value <= 0x7FFFFFFFU ? static_cast<std::int64_t>(value)
                              : static_cast<std::int64_t>(value) - 0x100000000LL;
}
bool timestamp(std::uint32_t value) noexcept {
    return value != 0 && value != kNitrousInactiveSentinel;
}
bool positive(std::uint32_t value) noexcept {
    return value != kNitrousInactiveSentinel && signed_word(value) > 0;
}
std::uint32_t committed_remaining(const NitrousResource& resource, std::uint32_t clock) noexcept {
    const double seconds = (static_cast<double>(signed_word(resource.remaining)) -
                            static_cast<double>(signed_word(clock - resource.started))) * tick_seconds;
    const double ticks = (seconds > 0 ? seconds : 0) * 4000.0 + 0.5;
    // The original signed conversion returns the indefinite integer on
    // overflow. Retain its bits, without an out-of-range C++ conversion.
    if (ticks >= 2147483648.0) return 0x80000000U;
    return static_cast<std::uint32_t>(ticks);
}
}

bool NitrousResource::initialize(float seconds, std::int32_t count) noexcept {
    if (positive(capacity)) return true;
    const double ticks = static_cast<double>(seconds) * 4000.0 + 0.5;
    if (!std::isfinite(seconds) || seconds < 0 || ticks >= 2147483647.0) return false;
    capacity = remaining = static_cast<std::uint32_t>(ticks);
    step = count > 0 ? static_cast<std::uint32_t>(
        static_cast<double>(seconds) / static_cast<double>(count) * 4000.0 + 0.5) : 0;
    started = 0;
    return true;
}
bool NitrousResource::available() const noexcept {
    return positive(remaining) && !timestamp(started);
}
bool NitrousResource::start(std::uint32_t clock, bool held_mode) noexcept {
    if (!available()) return false;
    started = clock;
    if (held_mode) step = remaining;
    else remaining -= step;
    return true;
}
bool NitrousResource::active(std::uint32_t clock) noexcept {
    if (!timestamp(started)) return false;
    if (signed_word(clock - started) <= signed_word(remaining)) return true;
    stop(clock);
    return false;
}
void NitrousResource::stop(std::uint32_t clock) noexcept {
    if (!timestamp(started)) return;
    remaining = committed_remaining(*this, clock);
    step = started = 0;
}
std::uint32_t NitrousResource::remaining_at(std::uint32_t clock) const noexcept {
    return timestamp(started) ? committed_remaining(*this, clock) : remaining;
}
void NitrousEngineState::update(NitrousResource& resource, std::uint32_t clock) noexcept {
    if (resource.active(clock) && !active) {
        // Original fild/fmul/fstp rounds once to binary32. A double
        // intermediate double-rounds at clock1400075121. The float tick
        // constant is exactly 8589935 * 2^-35; its integer product fits
        // int64. Round that product directly to float, then scale exactly.
        activation_seconds = static_cast<float>(signed_word(clock) * 8589935LL) * 0x1p-35F;
    }
    active = resource.active(clock);
}
bool NitrousCurveDescriptor::initialize(float idle, float redline) noexcept {
    if (!std::isfinite(idle) || !std::isfinite(redline) || idle < 0 || redline <= idle) return false;
    const float scale = static_cast<float>(8.0 / (static_cast<double>(redline) - idle));
    if (!std::isfinite(scale) || scale <= 0) return false;
    rpm_origin = idle;
    rpm_scale = scale;
    return true;
}
NitrousTransition nitrous_dispatch(NitrousResource& resource, std::uint32_t clock,
                      bool pressed, const NitrousDispatchContext& context) noexcept {
    if (context.flag_78a345 || context.override_7361a4 ||
        context.flag_78a346 || context.override_7361a0) return NitrousTransition::none;
    if (pressed) return resource.start(clock, context.held_mode)
        ? NitrousTransition::started : NitrousTransition::none;
    const bool was_active = timestamp(resource.started);
    if (resource.active(clock) && context.held_mode) {
        resource.stop(clock);
        return resource.remaining == 0 ? NitrousTransition::depleted : NitrousTransition::stopped;
    }
    return was_active && !timestamp(resource.started) ? NitrousTransition::depleted : NitrousTransition::none;
}
NitrousSnapshot nitrous_snapshot(const NitrousResource& resource, std::uint32_t clock) noexcept {
    return {resource.capacity, resource.remaining_at(clock),
            timestamp(resource.started) && signed_word(clock - resource.started) <= signed_word(resource.remaining),
            resource.available()};
}

const std::array<float, 9> kNitrousTorqueCurve = {
    0.3F, 0.4F, 0.45F, 0.5F, 0.55F, 0.6F, 0.7F, 1.0F, 0.7F
};

float nitrous_engine_torque(float base, float angular_speed, float rate,
                           bool enabled, const NitrousTorqueContext& context) noexcept {
    if (!enabled) return base;
    // Original x87 spills RPM and fractional index to float before use.
    const float rpm = static_cast<float>(static_cast<double>(angular_speed) *
                                        static_cast<double>(9.54930400848388671875F));
    const double index = (static_cast<double>(rpm) - context.rpm_origin) * context.rpm_scale;
    // Descriptors and engine input must be finite. Do not invent a torque
    // for a missing/invalid descriptor in this explicitly bounded API.
    if (!std::isfinite(index) || context.rpm_scale <= 0) return base;
    double curve = 0;
    // The native index conversion truncates toward zero. In (-1, 0),
    // it therefore still interpolates points 0/1 using the fractional
    // part of the spilled index. Preserve that original boundary quirk.
    if (index <= -1) curve = kNitrousTorqueCurve.front();
    else if (index >= 8) curve = kNitrousTorqueCurve.back();
    else {
        const auto point = index < 0 ? std::size_t{0} : static_cast<std::size_t>(index);
        const double spilled = static_cast<double>(static_cast<float>(index));
        const double fraction = spilled - std::floor(spilled);
        curve = (1.0 - fraction) * kNitrousTorqueCurve[point] +
                fraction * kNitrousTorqueCurve[point + 1];
    }
    float combined = static_cast<float>(curve * rate + base);
    if ((context.flag_78a344 || context.override_7361a8) &&
        static_cast<double>(signed_word(context.clock)) * tick_seconds <=
        static_cast<double>(context.activation_seconds) + 0.5) {
        combined = static_cast<float>(static_cast<double>(combined) * 1.5);
    }
    return combined;
}
} // namespace nfsu
