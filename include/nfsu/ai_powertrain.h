#pragma once

#include "nfsu/vehicle.h"

namespace nfsu {

// Selector3 wheel/engine component475070. Binary32, tonnes/kN/metres.
// These are physical component callbacks, not selector1 with fewer steps.
// Primitive callbacks require finite component state, a decoded valid setup
// and a valid gear index. The system caller owns contact queries, aero, activation,
// collision/rest handling and publication. No allocation occurs in callbacks.
struct AiWheelState {
    float spin = 0, compression = 0, compression_rate = 0, load = 0;
    std::array<float,2> feedback{}, slip{};
    bool contact = false, sliding = false, locked = false;
    float excess = 0;
    PlayerRigidVector normal{{0,0,1}};
    float preload = 0;
};

struct AiPowertrainState {
    std::array<AiWheelState,4> wheels{};
    float rpm = 0, throttle = 0, brake = 0;
    std::uint16_t steering = 0;
    int gear = kVehicleGearFirst;
    int gear_request = -1; // -1 automatic; 0 reverse; other values retain gear
    float boost = 1, grip = 1, low_slip = 1;
    bool extra_torque = false;
    float extra_torque_scale = 0; // selected setup+2AC, not a stock substitute
    std::int32_t traction_until = 0, world_time = 0;
};

struct AiPowertrainTables {
    std::array<float,kVehicleGearSlots> up{}, down{};
};

//581B30 sampled acceleration descriptor, shared by original AI and frontend.
bool ai_setup_acceleration_curve(const VehicleHandling&,std::array<float,64>&) noexcept;
//580240 searches the cached acceleration table against drag for steering.
bool ai_setup_predicted_speed(const VehicleHandling&,float& speed) noexcept;

//581B30/57F61D ordinary-car performance area and474203 constructor grip.
// Requires a valid selected decoded setup; adaptive event upgrades are external.
bool ai_setup_performance(const VehicleHandling&,float& rating,float& low_slip) noexcept;

//475BF0 returns the actual lookup height and world hub point;475A00 consumes
// them with the supplied normal. A query miss must retain source query
// semantics in the adapter, not silently become a flat supported wheel.
struct AiWheelGround {
    float height = 0;
    PlayerRigidVector hub{}, normal{{0,0,1}};
};

//440A40 no polygon: hub height and downward normal, passed through577060
// when no dynamic surface overrides it.475A00 consumes these raw values.
void ai_ground_clear(AiWheelGround&) noexcept;

void ai_powertrain_struts(const VehicleHandling&,const PlayerRigidBodyState&,
                          const std::array<AiWheelGround,4>&,float dt,
                          AiPowertrainState&) noexcept;
void ai_powertrain_feedback(const VehicleHandling&,PlayerRigidBodyState&,
                            AiPowertrainState&) noexcept;
std::array<float,4> ai_powertrain_derivative(const VehicleHandling&,
                                             const PlayerRigidBodyState&,
                                             const AiPowertrainState&) noexcept;
//4757D0 after its strut callback: brake latch, physical gear and RPM.
void ai_powertrain_complete(const VehicleHandling&,const AiPowertrainTables&,
                            AiPowertrainState&) noexcept;

//474A90 latches actor state before integration;474DD0 uses that actor's
// published axes/velocity/support, not the refreshed component pose.
struct AiCollisionContext {
    unsigned state=0,support=0;
    PlayerRigidMatrix actor_axes{{{{1,0,0}},{{0,1,0}},{{0,0,1}}}};
    PlayerRigidVector actor_velocity{};
    int published_gear=2;
    float throttle=0;
    bool recovery_active=false;
};
void ai_collision_policy(const AiCollisionContext&,PlayerRigidBodyState&,
                         PlayerAeroCache&) noexcept;
PlayerRigidVector ai_collision_stabilize(const AiCollisionContext&,float mass,
                                         PlayerRigidBodyState&) noexcept;

struct AiPhysicsState {
    PlayerRigidBodyState body;
    AiPowertrainState engine;
    PlayerAeroCache aero;
    bool prepared = false;
    bool collision_context_valid=false;
    AiCollisionContext collision_context;
    unsigned rest_state=0;
    // Last component interval (original6F0738). External contact refreshes
    // retain it; only constructor completion starts with zero time.
    float component_interval = 0;
    std::uint64_t slices = 0, ground_queries = 0;
};
class AiPhysicsGround {
public:
    virtual ~AiPhysicsGround() = default;
    virtual bool query(const VehicleHandling&,const PlayerRigidBodyState&,
                        std::array<AiWheelGround,4>&) const = 0;
    // Optional world contact adapter, after component completion and before
    // feedback. It may change only the supplied candidate and local evidence.
    // It must refresh rigid caches if it changes pose or momentum.
    virtual bool resolve(const VehicleHandling&,AiPhysicsState&) const { return true; }
};
// Ordinary active component integration. No player camera, input filtering,
// constraint iterations, or selector1 engine/gearbox component is involved.
// Caller supplies a decoded valid setup, initialized selected scales/shift
// tables, processed controls,
// ground service and cached outer aero inputs. World rest/collision callbacks
// and actor publication remain outside this bounded component system.
// False leaves state unchanged. Ground service must be read-only.
bool ai_physics_advance(const VehicleHandling&,const AiPowertrainTables&,
                        const PlayerInitialAeroConfiguration&,float world_dt,
                        const AiPhysicsGround&,AiPhysicsState&,bool refresh_only=false) noexcept;

} // namespace nfsu
