#pragma once
#include <array>
#include <cstdint>

namespace nfsu {
using ContactVector = std::array<float, 3>;
// A solver boundary snapshot, not an actor or refreshed rigid-body state.
struct ContactBody {
    std::uint64_t identity = 0;
    float inverse_mass = 0;
    std::uint32_t collision_state = 0;
    float state_timer = 0;
    ContactVector linear_momentum{};
    ContactVector angular_momentum{};
    ContactVector position{};
    // World-space inverse inertia from the original selected matrix (not yaw only).
    std::array<ContactVector, 3> inverse_inertia{};
    ContactVector cached_velocity{};
    ContactVector cached_angular_velocity{};
    std::uint8_t kind = 0; // Explicit original body kind; kind4 support is unbacked only.
    bool car_scaling = true; // false: generic rigid impulse, 584E30 / Smokeable
    //46A0C0 returns the selected LOCAL inverse for the impulse denominator.
    //585810 separately rotates it for refreshed angular velocity.
    std::array<ContactVector,3> response_inverse_inertia{};
    bool separate_response_inertia=false;
};
struct ContactRecord {
    bool active = false; // Original allocated pair record, even without a closing hit.
    bool state_gate = false;
    ContactVector accumulated_impulse{};
    float magnitude_sum = 0;
    std::uint32_t creation_tick = 0, last_hit_tick = 0;
    ContactVector initial_point{};
    ContactVector point{};
    float tangent_speed = 0;
};
// Numeric record initialization from 00465640, excluding world-list ownership,
// allocation, counters and optional callbacks. Current-hit point/tangent fields
// are preserved, as the original constructor does not initialize them.
bool initialize_car_contact_record(ContactRecord& record,
    const ContactVector& initial_point, std::uint32_t tick) noexcept;
// Port of 00465830 up to, but excluding, its state callback at 004658B0.
// Current impulse remains the caller's distinct input; no severity is inferred.
bool accumulate_contact_hit(ContactRecord& record, const ContactVector& current,
                            const ContactVector& point, float tangent_speed,
                            std::uint32_t tick) noexcept;
//465D00: current-hit vector, not accumulated record strength or vehicle speed.
//Physical selector1 only; two authored kind4 objects use the lower boundary.
bool drag_contact_failure(const ContactVector& current,bool drag,
    unsigned physical_selector,unsigned other_kind,std::uint32_t other_name,
    bool& totalled) noexcept;
// Port of the car mass/state scaling and rigid momentum application:
// 0046A260..0046A318 and 00584F60..0058509D. Optional actor effects excluded.
// No admission, geometry, impulse calculation, callback, or velocity publication.
bool apply_car_contact_impulse(ContactBody& body, const ContactVector& impulse,
                              const ContactVector& arm) noexcept;
// Final solver order at 0046C5D2/0046C5E4: B, then A. State callback extra
// impulses must already have been applied in their original order by the caller.
// Distinct identities are mandatory; refusal leaves both bodies unchanged.
bool apply_contact_pair(ContactBody& a, ContactBody& b,
                        const ContactVector& impulse_a, const ContactVector& arm_a,
                        const ContactVector& impulse_b, const ContactVector& arm_b) noexcept;
// Explicit cache phase from 00585810..005859E4. The caller supplies the
// original selected, rotated world inverse inertia in body.inverse_inertia,
// raw mass and mutable speed limit. This does not choose scheduler placement,
// integrate a pose or publish an actor/race snapshot.
bool refresh_car_contact_velocity(ContactBody& body, float raw_mass,
                                  float speed_limit) noexcept;
// Prepared geometry response calculation only, through original 0046C3DB.
// Normal/point must come from authenticated preparation; distant pairs are
// deliberately NOT rejected here, just as original forced solver entry is not.
struct ContactGeometry {
    ContactVector normal{}, point{};
};
// Original 0046B8C0 car/car preparation from an authenticated shape query's
// world witness points. Coincident witnesses use B-A body origins, then the
// original zero-vector normalization fallback. This is not pair admission.
bool prepare_car_contact_geometry(const ContactVector& witness_a,
    const ContactVector& witness_b, const ContactVector& position_a,
    const ContactVector& position_b, ContactGeometry& geometry) noexcept;
struct ContactResponse {
    bool closing = false;
    ContactVector current_impulse{}, impulse_b{}, arm_a{}, arm_b{};
    float tangent_speed = 0;
    float normal_impulse = 0;
};
struct ContactMaterial {
    float restitution = .1F;
    float friction = .3F;
};
bool calculate_car_contact_response(const ContactBody& a, const ContactBody& b,
                                    const ContactGeometry& geometry,
                                    ContactResponse& response, float callback_factor = 1.0F,
                                    ContactMaterial other_material = {},
                                    bool pinned_prop_against_upright_car = false,
                                    ContactMaterial own_material = {},
                                    bool pinned_prop_against_upright_car_b = false) noexcept;
//46BAE0 kind0 against an immovable kind3 polygon. Surface friction comes
//from46A910 (polygon flags), and axes are the current rigid body axes.
bool calculate_track_contact_response(const ContactBody& car,const ContactGeometry&,
    const ContactVector& forward,const ContactVector& up,float surface_friction,
    ContactResponse&) noexcept;

// Normal-mode kind0 callback; unbacked kind4 receivers are excluded. Optional actor feedback/config hooks and
// race-event effects are outside this numeric body boundary. Axes are columns
// of the original body/actor matrices; support is the published wheel count.
struct ContactCallbackContext {
    std::uint32_t world_seed = 0;
    // Authenticated lateral physical-data bounds at actor+20+14/+44, A/B.
    bool has_physical_bounds = false;
    std::array<std::array<float,2>,2> physical_lateral_bounds{};
    using Axes = std::array<ContactVector,3>;
    std::array<Axes,2> body_axes{Axes{ContactVector{1,0,0},ContactVector{0,1,0},ContactVector{0,0,1}},
                                Axes{ContactVector{1,0,0},ContactVector{0,1,0},ContactVector{0,0,1}}};
    std::array<ContactVector,2> actor_up{{{0,0,1},{0,0,1}}};
    std::array<unsigned,2> grounded_wheels{{4,4}};
    bool drag=false;
};
// 0042A530: timer stores the beginning of continuous four-wheel support,
// not elapsed time since the impact. now_seconds is the original world clock.
void update_car_collision_state(std::uint32_t& state, float& support_since,
    unsigned grounded_wheels, std::int8_t body_flag, double now_seconds) noexcept;
enum class ContactResolution { refused, not_admitted, non_closing, applied, needs_state_callback };
// Explicitly admitted/prepared kind0 pairs or one unbacked kind4 in the witnessed normal mode.
// This bounded step closes no production admission gate. A hard current hit on
// a state-0 car requires explicit witnessed_callback evidence. Other callback
// branches refuse all mutation. A prior record gate is supported with explicit
// callback context when both actors are already state1. Optional alternate/race/effects modes are
// outside this API; caller must establish this witnessed mode before entry.
ContactResolution resolve_prepared_car_contact(ContactBody& a, ContactBody& b,
    ContactRecord& record, bool admitted, const ContactGeometry& geometry,
    std::uint32_t tick, ContactResponse& response,
    ContactCallbackContext* witnessed_callback = nullptr) noexcept;
}
