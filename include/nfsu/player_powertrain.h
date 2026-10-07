#pragma once

#include "nfsu/vehicle.h"

namespace nfsu {

// Selector1 component equations. Torque and inertia use the original kN m
// and tonnes m^2 units. These functions do not publish RPM into physical
// state. The caller owns wheel/body constraints and frame publication.
// Binary32 operations match the declared x87 0x007F native witness; live
// x87 configuration and whole-player dynamics remain separate evidence.
// Working wheel values before original component update clears forces/slip.
// Input velocity is resolved in wheel axes; force is kN, torque kN m.
struct PlayerWheelWork {
    float spin = 0.0F, history_torque = 0.0F;
    float torque = 0.0F, force_long = 0.0F, force_lat = 0.0F;
    float load = 0.0F, velocity_long = 0.0F, velocity_lat = 0.0F;
    float owner_speed = 0.0F; // signed owner+390 used by low-speed friction
    float slip = 0.0F;
    float service = 0.0F, parking = 0.0F;
    float heading_x = 1.0F, heading_y = 0.0F;
    float side_x = 0.0F, side_y = 1.0F;
    float position_x = 0.0F, position_y = 0.0F;
    int spin_direction = 1; // wheel+110, sign-crossing clamp
    bool contact = false, locked = false, sliding = false;
};

// 46E020 ordinary owner-state0 differential bias, including contact flags,
// unequal wheel speeds and original locked-wheel exclusion.
void player_distribute_torque(const VehicleHandling& setup, int gear,
                              PlayerEngineState& engine,
                              std::array<PlayerWheelWork,4>& wheels,
                              float transmitted) noexcept;

// 46E360 coupled correction and4785F0..478991 planar body/wheel constraints.
// The constraint slice includes external force, but builds its yaw moment
// from the wheel forces only. Rigid external torque belongs to body integration.
float player_coupled_correction(const VehicleHandling& setup, int gear,
                                const PlayerEngineState& engine,
                                const PlayerGearState& gearbox,
                                const std::array<PlayerWheelWork,4>& wheels,
                                float crank) noexcept;
void player_body_wheel_constraints(const VehicleHandling& setup,
                                   std::array<PlayerWheelWork,4>& wheels,
                                   float external_force_x,
                                   float external_force_y) noexcept;

// 587330 ordinary owner-state0/surface0 tyre/brake producer. The caller
// uses the original16-bit angle table and587740 standing load-curve family.
void player_wheel_prepare(const VehicleHandling& setup, std::size_t wheel,
                          PlayerWheelWork& state) noexcept;
void player_wheel_limit(const VehicleHandling& setup, std::size_t wheel,
                        PlayerWheelWork& state) noexcept;
float player_wheel_acceleration(const VehicleHandling& setup, std::size_t wheel,
                                PlayerWheelWork& state, float solver_dt,
                                float rolling_input = 0.0F) noexcept;
void player_wheel_integrate(PlayerWheelWork& state, float acceleration,
                            float solver_dt) noexcept;

//46E440 ordinary stock rear-drive feedback scale0, before integration.
void player_engine_feedback_finalize(const VehicleHandling& setup, int gear,
                                     const PlayerGearState& gearbox,
                                     PlayerEngineState& engine,
                                     const std::array<PlayerWheelWork,4>& wheels,
                                     float throttle, float crank) noexcept;
//46DB60 common update and46DFA0 with boost/timer/special/manual override0.
// RNG-assisted shift boost and complete request selection are separate.
void player_gear_integrate(const VehicleHandling& setup, PlayerGearState& gearbox,
                           float solver_dt) noexcept;
void player_gear_update(const VehicleHandling& setup, int& gear,
                        PlayerGearState& gearbox, PlayerEngineState& engine,
                        std::array<PlayerWheelWork,4>& wheels,
                        float throttle, float owner_speed_magnitude = 0.0F) noexcept;
//46D760 arithmetic. The seed is an explicit deterministic port state, not
// recovered live world RNG identity. Constructor consumes two values before
// setting the random offset bounds; subsequent shifts use [-100,0].
void player_gear_seed(PlayerGearState& gearbox, std::uint32_t seed) noexcept;
void player_gear_randomize(PlayerGearState& gearbox) noexcept;
void player_engine_cutoff_update(const VehicleHandling& setup, int gear,
                                 PlayerEngineState& engine,
                                 float simulation_seconds) noexcept;

constexpr float kPlayerWheelInertia = 0.01F; // 47721B:6F076C, tonnes m^2
constexpr float kPlayerRpmToRadS = 0.10471966862678528F; // 6CCAB4
constexpr float kPlayerRadSToRpm = 9.549304008483887F;    // 6CCB78

//564CD0/564DB0 lookup and564B10 polynomial in the original16-bit angle
// domain. Kept separate from the existing body/camera trigonometry.
std::uint16_t player_velocity_angle(float longitudinal,float lateral) noexcept;
float player_angle_sin(std::uint16_t angle) noexcept;

//478A47..478A6E and478AD2..478AE8 actor speed publication, after the
// completed outer solve. Source binary32 operation order is retained.
PlayerOwnerSpeeds player_publish_owner_speeds(const VehicleVec3& velocity,
                                            const VehicleVec3& forward) noexcept;

//588900..5889C2 resolves the strut hub arm through the CURRENT rigid
// rotation, then adds angular cross-arm velocity to rigid linear velocity.
// A tyre's contact arm is a separate force-application input.
VehicleVec3 player_hub_velocity(const std::array<VehicleVec3,3>& current_axes,
                               const VehicleVec3& local_hub_arm,
                               const VehicleVec3& linear_velocity,
                               const VehicleVec3& angular_velocity) noexcept;

//5889C5..5889E7 and5AC6A2 combine local wheel and rigid snapshot axes.
//587830's default force basis resolves Fx/Fy/load in those world axes.
std::array<VehicleVec3,3> player_world_wheel_axes(
    const std::array<VehicleVec3,3>& snapshot_axes,
    const std::array<VehicleVec3,3>& local_wheel_axes) noexcept;
VehicleVec3 player_world_wheel_force(const std::array<VehicleVec3,3>& world_axes,
                                    float longitudinal, float lateral,
                                    float load) noexcept;
//584B10 builds normal/alternate tensors;585810 selects by rigid138.
float player_body_inverse_yaw(const VehicleHandling& setup,bool alternate) noexcept;

//47AA40 common upright/support path with cleared recovery timer and optional
//735FCC body-pair scan disabled. Inputs are cached completed body velocities,
// published owner support/gear, and driver throttle; genuine live actor/body
// pair production remains outside this explicit port adapter.
unsigned player_body_rest_state(const VehicleVec3& velocity,
                               const VehicleVec3& angular_velocity,
                               float forward_vertical,float left_vertical,
                               float throttle,int published_gear,
                               unsigned published_support,unsigned prior_state,
                               bool recovery_active = false) noexcept;

// Constructor/reset conversion478365. Not a recurring display feedback.
void player_engine_reset(PlayerEngineState& engine, float rpm) noexcept;

// 46CFF0 without nitrous, damage or owner collision-state modifiers.
float player_engine_torque(const VehicleHandling& setup,
                          const PlayerEngineState& engine, int gear,
                          float throttle) noexcept;

// 46D680. Original near-zero/near-one axle selection and final-drive fields.
float player_wheel_engine_speed(const VehicleHandling& setup, int gear,
                               const std::array<float, 4>& spins) noexcept;

// 46E270 slip/coupled torque before46E020 distribution. Neutral bypass is
// handled here; the distributor must subtract this torque from engine+7C.
float player_clutch_torque(const VehicleHandling& setup,
                          const PlayerEngineState& engine,
                          PlayerGearState& gearbox, int gear,
                          float wheel_engine_speed, float crank) noexcept;

// 46CF80 and46AA40 Euler plus46CA90 clamp/consumption. Time-based adaptation
// and rev-cut scheduling are not part of this bounded primitive.
float player_engine_acceleration(const VehicleHandling& setup,
                                 const PlayerEngineState& engine,
                                 float crank) noexcept;
void player_engine_integrate(const VehicleHandling& setup,
                             PlayerEngineState& engine, float crank,
                             float solver_dt) noexcept;

// 478FBB..47904B with retail7000E4 sentinel100. Called once after all solver
// slices, with the caller's unsplit frame dt. Does not mutate the engine.
float player_engine_publish_rpm(const VehicleHandling& setup,
                               const PlayerEngineState& engine,
                               float previous_rpm, float frame_dt) noexcept;

} // namespace nfsu
