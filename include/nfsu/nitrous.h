#pragma once

#include <array>
#include <cstdint>

namespace nfsu {

// Portable contract for the bounded original consumers in
// docs/NITROUS_RECOVERY.md. The caller supplies the original world clock,
// not controller wall time. There is no event0 caller in this library.
constexpr std::uint32_t kNitrousInactiveSentinel = 0x7FFFFFFFU;
constexpr std::uint32_t kNitrousTicksPerSecond = 4000;

// Original object +3B0..+3BC. Remaining is committed on stop, rather than
// decremented every frame. A display reads remaining_at(clock).
struct NitrousResource {
    std::uint32_t capacity = 0;
    std::uint32_t step = 0;
    std::uint32_t remaining = 0;
    std::uint32_t started = 0;

    // 464DC7: an initialized positive capacity preserves the whole state.
    // Fresh construction starts with a zero resource. Car reset does not
    // refill it. Returns false for tuning outside finite nonnegative ticks.
    bool initialize(float capacity_seconds, std::int32_t count) noexcept;
    bool available() const noexcept;
    bool start(std::uint32_t clock, bool held_mode) noexcept;
    // 465430: exact exhaustion is inclusive; the next tick stops it.
    bool active(std::uint32_t clock) noexcept;
    void stop(std::uint32_t clock) noexcept;
    std::uint32_t remaining_at(std::uint32_t clock) const noexcept;
};

// Original live-engine +84/+88, initialized by 46C7C0. Physics bridge
// 477637..477684 commits active() twice at the same world tick and stamps
// activation seconds only on an inactive-to-active transition. It does not
// manufacture a refill or choose the production physics caller.
struct NitrousEngineState {
    bool active = false;
    float activation_seconds = 0;
    void update(NitrousResource& resource, std::uint32_t clock) noexcept;
};

// Original 46C842..46C867 derives the nitrous descriptor from physical
// engine idle/redline, not its maximum RPM. The table is kNitrousTorqueCurve.
struct NitrousCurveDescriptor {
    float rpm_origin = 0;
    float rpm_scale = 0;
    // Refuses invalid input without changing an existing descriptor.
    bool initialize(float idle_rpm, float redline_rpm) noexcept;
};

// These are explicit original predicate inputs, not assumed production
// defaults. Dispatcher 431960 suppresses both press and release when any
// predicate is true. Their initialization remains a caller evidence gap.
struct NitrousDispatchContext {
    bool flag_78a345 = false;
    bool override_7361a4 = false;
    bool flag_78a346 = false;
    bool override_7361a0 = false;
    bool held_mode = true; // authenticated initial 7000E8 is 1
};

enum class NitrousTransition { none, started, stopped, depleted };
NitrousTransition nitrous_dispatch(NitrousResource& resource, std::uint32_t clock,
                      bool pressed, const NitrousDispatchContext& context) noexcept;

struct NitrousSnapshot {
    std::uint32_t capacity = 0;
    std::uint32_t remaining = 0;
    bool active = false;
    bool available = false;
};
// Read-only HUD/effect contract. No asset, sound or presentation readiness
// is selected here. Transition consumers decide how to present the event.
NitrousSnapshot nitrous_snapshot(const NitrousResource& resource, std::uint32_t clock) noexcept;

// Small numeric table recovered from authenticated speed.exe at 6FFF34.
// The test encodes its 36 bytes and pins their SHA-256.
extern const std::array<float, 9> kNitrousTorqueCurve;
constexpr const char* kNitrousTorqueCurveSha256 =
    "5BFBA693A2DCB4CD93FE2211003622A0B5478452F013EA380B7CFC65889C8BEA";

// Original live engine +60 curve descriptor and +88 activation time.
// A caller must supply a validated descriptor and the actual flags. In
// particular override_7361a8 has no proven event0 initialization yet.
struct NitrousTorqueContext {
    float rpm_origin = 0;
    float rpm_scale = 0;
    float activation_seconds = 0;
    std::uint32_t clock = 0;
    bool flag_78a344 = false;
    bool override_7361a8 = false;
};

// Original 46D057..46D0C4 additive crank torque. angular_speed is engine
// +14; rate is tuning +2AC. The mode/time branch multiplies the combined
// torque, not just the additive term. Inactive returns base unchanged.
float nitrous_engine_torque(float base, float angular_speed, float rate,
                           bool active, const NitrousTorqueContext& context) noexcept;

} // namespace nfsu
