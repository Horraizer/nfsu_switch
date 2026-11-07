#pragma once

#include <array>
#include <cstdint>

namespace nfsu {
// Original564BD0 heading basis, shared with traffic prediction.
std::array<float,3> ai_driver_heading(std::uint16_t heading) noexcept;


using AiDriverPoint = std::array<float,3>;
struct AiDriverPath {
    std::array<AiDriverPoint,25> points{};
    std::uint8_t count = 0;
};
struct AiDriverTarget {
    AiDriverPoint point{};
    float heading_error = 0;
    float velocity_error = 0;
};

// 45FB30/4590A0/459710/45E370 for an already produced ordinary driver path.
// The action planner owns path selection and resampling, outside this API.
// Empty/single paths use original heading/movement fallback, not their point.
// traffic_mode0 is the racer curve;3/4 select original traffic lookahead.
bool ai_driver_target(const AiDriverPath& path, const AiDriverPoint& position,
                      const AiDriverPoint& velocity, std::uint16_t heading,
                      float forward_speed, float absolute_speed, int category,
                      AiDriverTarget& result,unsigned traffic_mode=0) noexcept;

// Original driver primitives. These consume the controller's desired speed;
// they neither choose a racing line nor stand in for the action scheduler.
// Source: authenticated speed.exe, 45FC90 and 460100, controller293 evidence.
struct AiDriverError {
    float current = 0;
    float integral = 0;
    float derivative = 0;
};

struct AiDriverPedalState {
    float throttle = 0;
    float brake = 0;
};

struct AiDriverPedalInput {
    float desired_speed = 0;
    float absolute_speed = 0;
    float throttle_cap = 1;
    AiDriverError speed_error;
    AiDriverError slip_error;
    // Mode selected by 45EA80..45EAE4, not car category or difficulty.
    std::uint32_t mode = 0;
    bool special = false;
    bool slip_limit = false;
};

// Selector3 ordinary pedal calculation only. Caller owns histories, reverse
// hysteresis, contact/enable gates, timers and the final special override.
// False refuses invalid inputs transactionally; no partial output is published.
bool ai_driver_pedals(const AiDriverPedalInput& input,
                      AiDriverPedalState& state) noexcept;

// Original 460100 role/hold/race-time/finish gate; time is in seconds.
bool ai_driver_slip_limit(bool hold, std::uint32_t role, float race_seconds,
                          bool has_status, std::int16_t finish_place) noexcept;

// History storage is fixed-size: no allocations in an AI tick. Constructor
// lengths are five for steering and four for speed/slip (45DBB0).
struct AiDriverHistorySnapshot {
    struct Ring {
        std::array<float,5> values{};
        float sum=0,average=0;
        std::uint8_t count=0,next=0;
    };
    Ring time,integral,derivative;
    float current=0;
    std::uint8_t samples=4;
};
class AiDriverHistory {
public:
    explicit AiDriverHistory(std::uint8_t samples = 4) noexcept;
    void update(float error, float dt, bool suppress_derivative = false) noexcept;
    // 45E710/5842D0: clear error samples; retain time and ring cursor/cache.
    void clear_error_samples() noexcept;
    // 5842D0 with a nonzero seed: retain time, ring cursor and average cache.
    void reset_error_samples(float value) noexcept;
    AiDriverError error() const noexcept;
    // Restore an isolated captured tick without replaying invented history.
    // Retain ring cursors/cache exactly, including reset histories whose
    // cached average is intentionally older than their current sample count.
    AiDriverHistorySnapshot snapshot() const noexcept;
    bool restore(const AiDriverHistorySnapshot&) noexcept;
private:
    struct Ring {
        std::array<float, 5> values{};
        float sum = 0;
        float average = 0;
        std::uint8_t count = 0;
        std::uint8_t next = 0;
        void push(float value, std::uint8_t length) noexcept;
    };
    Ring time_, integral_, derivative_;
    float current_ = 0;
    std::uint8_t length_ = 4;
};

struct AiDriverSteeringState {
    AiDriverHistory heading{5};
    AiDriverHistory velocity_heading{5};
    std::uint16_t word = 0;
};

// Grounded selector3/controller3 ordinary mode. Errors are the original
// wrapped-radian heading and velocity-blended errors from 45E370. No player
// joystick curve is applied. The two direction flags select the ordinary
// signed-speed steering inversion and slew; they do not enable special modes.
// Alternate roles and airborne steering use separate source branches.
bool ai_driver_ground_steering(float heading_error, float velocity_error,
                              float speed, int category, float dt,
                              AiDriverSteeringState& state,bool reverse_command=false,
                              bool moving_reverse=false) noexcept;

struct AiDriverForwardInput {
    AiDriverPath path;
    AiDriverPoint position{}, velocity{};
    std::uint16_t heading = 0;
    float forward_speed = 0, absolute_speed = 0;
    float desired_speed = 0, throttle_cap = 1;
    float rear_drive_fraction = 0;
    std::array<bool,4> contact{};
    std::array<float,4> wheel_slip{};
    float route_distance = 10; // actor3B8 travelled distance; not local road progress
    int category = 0;
    float race_seconds = 0;
    bool enabled = true;
};
struct AiDriverForwardState {
    AiDriverSteeringState steering;
    AiDriverHistory speed{4}, slip{4};
    AiDriverPedalState pedals;
    AiDriverTarget target;
    bool direction_ready=true; // original driver+218 hysteresis latch
    int gear_request=-1; // original driver+1D4: automatic=-1, reverse=0
};

// Ordinary forward control, selector3/controller3/role2, including loss of
// front-wheel support (45F2B4). The caller
// must not use this for reverse, hold, recovery or active override
// timers. Unsupported input is refused without changing persistent state.
// This is a driver, not the action planner or rival physics implementation.
bool ai_driver_forward_step(const AiDriverForwardInput& input,float dt,
                            AiDriverForwardState& state) noexcept;
// Same selector3/controller3/role2 domain, including ordinary signed-speed
// direction changes used by action10. No active special override/hold timers.
// The original reverse request is gear0; the physics adapter applies it.
bool ai_driver_ordinary_step(const AiDriverForwardInput& input,float dt,
                             AiDriverForwardState& state) noexcept;
// Selector1 drag driver, below the engine overheat gate, without active
// collision override. Original mode1 pedal tables and7060F0 steering table
// equal the ordinary tables; tools/ai_driver_vectors.py --drag verifies both
// grounded and unsupported-wheel sequences. Engine damage is owned separately.
bool ai_driver_drag_step(const AiDriverForwardInput&,float dt,AiDriverForwardState&) noexcept;
// Ordinary selector2/controller4 traffic. Path/action owner supplies the
// current lane and speed. No collision/recovery selector3 transition here.
bool ai_driver_traffic_step(const AiDriverForwardInput&,float dt,AiDriverForwardState&,float yaw_scale=1.F) noexcept;
// Controller4 retained after583E80 selects physics3: mode4 coefficients,
// real wheel support and ordinary direction hysteresis; role3 has no racer
// slip limiter. Reuses the pre-impact traffic histories without a reset.
bool ai_driver_traffic_impact_step(const AiDriverForwardInput&,float dt,AiDriverForwardState&,float yaw_scale=1.F) noexcept;
// 452190's driver+5C/45E650 cleanup. Uses the retained target, not a new path
// sample. Does not reset direction hysteresis, speed history or steering word.
bool ai_driver_recovery_reset(const AiDriverForwardInput&,AiDriverForwardState&) noexcept;

} // namespace nfsu
