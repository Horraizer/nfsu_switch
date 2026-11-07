#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace nfsu {
// Issue #251: the isolated 0x00460930 steering path. Inputs are explicit
// retail context, not claims about the meaning of untraced device flags.
struct SteeringContext {
    float raw = 0;
    float seconds = 0;
    float dt = 1.0F / 120.0F;
    float forward_speed = 0;
    float speed = 0;
    float heading_minus_velocity_deg = 0;
    float throttle = 0; // processed controls +0x1DC, sent to engine
    float handbrake = 0; // controls +0x1E0, from input +0x2AC
    float brake = 0; // processed brake component, controls +0x1E4
    float multiplier = 1;
    float predicted_max_speed = 1;
    float shift_elapsed = 1; // >= 1 means no recent shift
    bool bypass_mode = false; // retail byte 0x00736514
    bool predicate = false; // result of 0x004607A0
};

template<std::size_t N> struct SteeringHistory {
    std::array<float,N> values{}, times{};
    float sum = 0, average = 0;
    std::size_t count = 0, write = 0, oldest = 0;
};
struct SteeringState {
    SteeringHistory<33> input;
    SteeringHistory<9> rate;
    std::uint16_t angle = 0;
    float previous_limit = 45;
    float previous_raw = 0;
    bool assist = false;
};
struct SteeringResult {
    float filtered = 0;
    float rate_history = 0;
    float limit_deg = 0;
    float target_deg = 0;
    float rate_deg_s = 0;
    float angle_deg = 0;
    float shift_factor = 0;
    std::uint16_t angle = 0;
};
// Finite, bounded contexts only. Refusal leaves state/result unchanged.
bool steering_step(const SteeringContext&, SteeringState&, SteeringResult&) noexcept;
// Numeric table provenance: speed.exe C0FAD450...074B7; descriptors
// 0x705F48, 5C, 70, 84, 98, AC, 20, 34, E0 in this order.
// Samples only, little endian. Tests rebuild and pin their combined SHA-256.
std::vector<std::uint8_t> steering_table_bytes();
enum class RaceSteering { baseline, recovered_predicate0, recovered_predicate1 };
const char* race_steering_name(RaceSteering) noexcept;
bool parse_race_steering(const std::string&, RaceSteering&) noexcept;
} // namespace nfsu
