#pragma once

#include "nfsu/vehicle.h"

namespace nfsu {

struct PlayerSurfaceFactors {
    float static_grip=1.F, sliding_grip=1.F, rolling_drag=1.F;
    float bump_frequency=1.F, bump_amplitude=0.F;
};
// Original6EF2D0 table,29 materials, stride0x94. Unknown IDs refuse.
bool player_surface_factors(std::uint32_t material,PlayerSurfaceFactors&) noexcept;
// Original441D80 positional road height modulation; no random/timer state.
bool player_surface_height(std::uint32_t material,float x,float y,float&) noexcept;

// Selector1 component equations. Torque and inertia use the original kN m
// and tonnes m^2 units. These functions do not publish RPM into physical
// state. The caller owns wheel/body constraints and frame publication.
// Binary32 operations match the declared x87 0x007F native witness; live
// x87 configuration and whole-player dynamics remain separate evidence.
// Working wheel values before original component update clears forces/slip.
// Input velocity is resolved in wheel axes; force is kN, torque kN m.
struct PlayerWheelWork {
    PlayerSurfaceFactors surface;
    float spin = 0.0F, history_torque = 0.0F;
    float torque = 0.0F, force_long = 0.0F, force_lat = 0.0F;
    float owner_grip = 1.0F; // wheel+178
    float friction_scale = 1.0F; // wheel+180, drift callback
    float load = 0.0F, velocity_long = 0.0F, velocity_lat = 0.0F;
    float owner_speed = 0.0F; // signed owner+390 used by low-speed friction
    float slip = 0.0F; // wheel+160, cleared by component refresh
    float friction_usage = 0.0F; // diagnostic wheel+168 after587830, not slip speed
    float cached_slip = 0.0F; // wheel+164, retained until58747D writes both
    float service = 0.0F, parking = 0.0F;
    float heading_x = 1.0F, heading_y = 0.0F;
    float side_x = 0.0F, side_y = 1.0F;
    float position_x = 0.0F, position_y = 0.0F;
    int spin_direction = 1; // wheel+110, sign-crossing clamp
    bool contact = false, locked = false, sliding = false;
};

//588580/5ABDB5 strut attachment relative to CG, then live rigid axes and
// body position. The original rigid matrix has no extra translation here.
PlayerRigidVector player_strut_attachment(const VehicleHandling& setup,
                                          std::size_t wheel,
                                          const PlayerRigidBodyState& body) noexcept;
//587070 projects selected static-road height along live body-up. Returns
//false for a refused alignment, with the original sentinel in gap.
bool player_strut_ground_projection(const std::array<float,4>& plane,
    const PlayerRigidVector& attachment,const PlayerRigidVector& body_up,
    float surface_bump,float& gap) noexcept;
//58860A..5886A2 clamps compression/contact, caps excess compression at
// original6F0740 and retains cached rate when the interval is at most .0001F.
struct PlayerStrutCompression {
    float compression=0,rate=0,overflow=0;
    bool contact=false;
};
PlayerStrutCompression player_strut_compression(const VehicleAxleStrut& setup,
    float raw_compression,float previous_compression,float cached_rate,
    float interval) noexcept;
//5889EC nominal contact force in kN, including the immediate partner epoch
// supplied by the caller. player_strut_compression supplies limits/contact
// and capped excess; the BSS bump coefficient remains a supplied parameter.
float player_strut_nominal_force(const VehicleAxleStrut& setup,
                                 float compression, float rate,
                                 float partner_compression, float preload,
                                 float overflow, float bump_rate) noexcept;

// 46E020 owner-state differential bias, including contact flags,
// unequal wheel speeds and original locked-wheel exclusion.
void player_distribute_torque(const VehicleHandling& setup, int gear,
                              PlayerEngineState& engine,
                              std::array<PlayerWheelWork,4>& wheels,
                              float transmitted,int owner_mode=0) noexcept;

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

//584CD0 accumulates one world force and its contact-arm moment in binary32.
// Force and moment have independent magnitude caps; the moment uses the
// original supplied force before its cap. Inputs use tonnes/kN units.
void player_rigid_body_add_contact_force(PlayerRigidBodyState& body,
                                        const VehicleVec3& force,
                                        const VehicleVec3& arm) noexcept;

//464ED0 signed heading minus velocity angle, from published actor values.
std::uint16_t player_owner_heading(const VehicleVec3& forward) noexcept;
float player_owner_slip_angle(const VehicleVec3& velocity,const VehicleVec3& forward) noexcept;
float player_owner_slip_angle(const VehicleVec3& velocity,std::uint16_t heading) noexcept;
void player_publish_owner_motion(PlayerOwnerMotion& owner,const PlayerRigidVector& velocity,
                                const PlayerRigidVector& angular,const PlayerRigidMatrix& axes) noexcept;
//42699E..426A5C, after physics and contacts, with the pre-world velocity.
//Nonpositive dt retains the preceding derivative.
void player_publish_owner_acceleration(PlayerOwnerMotion& owner,const VehicleVec3& before,
                                      const VehicleVec3& after,float world_dt) noexcept;
//479290/429CC0: grounded drag pitch torque from the preceding world derivative.
void player_drag_body_torque(PlayerRigidBodyState& body,const PlayerOwnerMotion& owner,
                            unsigned support,unsigned collision_state,bool nitrous) noexcept;
//477B3D..477B96, drag-only nitrous grip window, inclusive half-second end.
float player_drag_nitrous_grip(bool drag,bool active,std::uint32_t clock,
                              float activation_seconds) noexcept;
//479640 common first branch; ride424 is the recovered performance class0..3.
//Applied after the per-wheel base/nitrous grip and before component stepping.
float player_shift_grip_scale(float quality,float rear_drive_fraction,unsigned category) noexcept;
//477775..4777BF, prior to per-wheel rear grip and transient factors.
float player_owner_base_grip(float physics_scale,unsigned owner_mode,bool assisted) noexcept;
//479640: preceding prepared wheel load/slip drives temporary diagonal grip.
//The final two modes also select46E020's fixed left/right differential split.
std::array<float,4> player_owner_traction_update(PlayerOwnerTraction& state,
    float rear_drive_fraction,unsigned category,bool eligible,bool racing,
    float forward_speed,float slip_angle,float interval,
    const std::array<float,4>& loads,const std::array<float,4>& slips) noexcept;
//4773F0 ordinary owner-mode0 rear-grip helper, initial global controls.
float player_rear_grip(const VehicleHandling& h,float tilt,float slip,
                      float speed,float parking) noexcept;
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

//46E440 feedback finalizer, including the retained launch scale and705EA0.
void player_engine_feedback_finalize(const VehicleHandling& setup, int gear,
                                     const PlayerGearState& gearbox,
                                     PlayerEngineState& engine,
                                     const std::array<PlayerWheelWork,4>& wheels,
                                     float throttle, float crank) noexcept;
//46DB60 common update including descriptor+64 assist and driver+74 override.
// Current supported context:4607A0 is true for drift, false for ordinary/drag;
// its additional device/alternate-world globals are not produced by the port.
void player_gear_integrate(const VehicleHandling& setup, PlayerGearState& gearbox,
                           float solver_dt, float request_scale = 1.F) noexcept;
void player_gear_update(const VehicleHandling& setup, int& gear,
                        PlayerGearState& gearbox, PlayerEngineState& engine,
                        std::array<PlayerWheelWork,4>& wheels,
                        float throttle, float owner_speed_magnitude = 0.0F,
                        const std::array<float,4>* offset_bounds = nullptr,
                        std::uint32_t* world_random = nullptr,
                        bool assisted = false, bool clutch_override = false) noexcept;

//479C30: front-drive body torque, before4791F0 low-support torque. Reads
// outer-published support, live body axes/velocity and local wheel forces.
void player_front_drive_torque(const VehicleHandling& setup,
                              PlayerRigidBodyState& body,
                              const std::array<PlayerWheelWork,4>& wheels,
                              unsigned published_support,
                              bool assisted = false) noexcept;
//46D760 arithmetic. The seed is an explicit deterministic port state, not
// recovered live world RNG identity. Constructor consumes two values before
// setting the random offset bounds; subsequent shifts use [-100,0].
void player_gear_seed(PlayerGearState& gearbox, std::uint32_t seed) noexcept;
void player_gear_randomize(PlayerGearState& gearbox,
                           const std::array<float,4>* offset_bounds = nullptr) noexcept;
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
// Signed564CD0 unit-domain inverse sine, in the original angle word.
std::int16_t player_unit_asin(float value)noexcept;
//4775E4/588240: controls1D0 word -> distinct front0/front1 local axes.
// The strut callback composes these with the refreshed rigid snapshot.
std::array<std::array<VehicleVec3,3>,2> player_front_steering_axes(
    const VehicleHandling& setup,std::uint16_t angle) noexcept;

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
//5889D8/5AC843 world hub translation uses the refreshed rigid snapshot.
//586F10 applies radius along the ground normal;586F7D adjusts the positive
// strut load for the world wheel-across axis. Inputs retain original units.
PlayerRigidVector player_world_wheel_hub(const PlayerRigidBodyState& body,
                                         const PlayerRigidVector& local_hub) noexcept;
PlayerRigidVector player_wheel_contact_point(const PlayerRigidVector& world_hub,
                                             const PlayerRigidVector& normal,
                                             float radius) noexcept;
// Full586F10 point branch: a contact writes even for nonpositive load;
// a miss retains the caller's cached point, independently of load handling.
void player_wheel_contact_point(PlayerRigidVector& cached_point,
                                 const PlayerRigidVector& world_hub,
                                 const PlayerRigidVector& normal,float radius,
                                 bool contact) noexcept;
float player_wheel_contact_load(const PlayerRigidVector& across,
                                 const PlayerRigidVector& normal,float nominal) noexcept;
//584B10 builds normal/alternate tensors;585810 selects by rigid138.
float player_body_inverse_yaw(const VehicleHandling& setup,bool alternate) noexcept;
PlayerRigidMatrix player_body_inverse_tensor(const VehicleHandling& setup,
    const PlayerRigidMatrix& axes,bool alternate) noexcept;

// Refresh clears force/torque then restores gravity; optional drive force
// control1E4 is outside this common component API and must be disabled.
void player_rigid_body_refresh(const VehicleHandling& setup,PlayerRigidBodyState& body) noexcept;
void player_rigid_body_integrate(const VehicleHandling& setup,PlayerRigidBodyState& body,
                                 float solver_dt) noexcept;
//477729..477771: active race stage4 overrides collision damping policy.
// Uses the driver's processed throttle and the outer published actor pose/support.
void player_race_body_policy(PlayerRigidBodyState& body,bool racing,float throttle,
    unsigned published_support,float published_forward_vertical) noexcept;
// Original outer40BD40 changes live axes alone, after the force callback.
void player_rigid_body_normalize(PlayerRigidBodyState& body) noexcept;
//588AB0/584E30: a fully compressed strut damps inward hub motion through
//a generic rigid impulse. Momentum changes immediately; cached V/W do not.
//The caller refreshes components once after all bump-stop impulses.
float player_strut_bump_impulse(const VehicleHandling& setup,PlayerRigidBodyState& body,
    const PlayerRigidVector& local_hub,const PlayerRigidVector& normal) noexcept;

//4791F0 selector1 low-support angular damping, using705E14 and584E00's
// binary32 torque addition. Owner states1/2 and support>=2 bypass it.
// Returns the applied kN m contribution for diagnostics.
PlayerRigidVector player_low_support_apply(const VehicleHandling& setup,
                                           PlayerRigidBodyState& body,
                                           unsigned published_support,
                                           unsigned owner_state) noexcept;

// Original462F90 three force/arm applications, in kN/body metres, using
// source binary32 order. Disabled leaves the optional drag output untouched.
struct PlayerAeroForces {
    std::array<PlayerRigidVector,3> forces{}, arms{};
    float downforce = 0.0F;
};
PlayerAeroForces player_aero_forces(const VehicleHandling& setup,
                                  const PlayerRigidBodyState& body,
                                  const PlayerAeroCache& cache,
                                  const PlayerInitialAeroConfiguration& config) noexcept;
float player_aero_apply(const VehicleHandling& setup, PlayerRigidBodyState& body,
                       const PlayerAeroCache& cache,
                       const PlayerInitialAeroConfiguration& config,
                       PlayerRigidVector* cached_drag = nullptr) noexcept;
//477100/584B10/588520, before substeps, using PRIOR published owner speed.
// Actor state1 gravity/CG flag are primitive operands, not VehicleModel modes.
std::array<float,4> player_aero_preloads(const VehicleHandling& setup,
                                       float published_forward, bool enabled,
                                       unsigned actor_state = 0,
                                       bool cg_enabled = true) noexcept;

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

//46CA90 drag heat consumer, with the original world clock and grid hold.
void player_drag_heat_update(const VehicleHandling&, PlayerEngineState&,
    const PlayerGearState&, int gear, std::int32_t clock, bool held) noexcept;
//46E940 supplied physical RPM and original private upshift threshold.
//46D205/46D447: neutral launch target, including drive-class factory+5C.
float player_drag_launch_rpm(const VehicleHandling& setup) noexcept;
float player_drag_shift_window(unsigned category,unsigned gear,float scale) noexcept;
float player_drag_shift_quality(float rpm, float upshift, unsigned category,
    unsigned gear, float window_scale) noexcept;
//46DB60/46DF05 positive shift-quality torque pulse after clutch engagement.
void player_drag_shift_boost(PlayerGearState&, PlayerEngineState&,
    std::int32_t clock) noexcept;

// Forward request thresholds46D1C0 with ordinary700030=1 and binary32
// arithmetic. Requires validated handling. Neutral is unused by46E5B0.
// Failure leaves both outputs unchanged when required007F intermediates
// cannot be represented in binary32 or a search step cannot advance.
bool player_request_shift_tables(const VehicleHandling& setup,
    std::array<float,kVehicleGearSlots>& up,
    std::array<float,kVehicleGearSlots>& down) noexcept;

// 46CFF0; optional original nitrous term before engine braking.
// Damage and owner collision-state modifiers remain outside this adapter.
float player_engine_torque(const VehicleHandling& setup,
                          const PlayerEngineState& engine, int gear,
                          float throttle, const VehicleInput* input = nullptr,
                          float nitrous_rate = 0) noexcept;

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
