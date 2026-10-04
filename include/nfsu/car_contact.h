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
};
struct ContactRecord {
    bool state_gate = false;
    ContactVector accumulated_impulse{};
    float magnitude_sum = 0;
    float timestamp = 0;
    ContactVector point{};
    float tangent_speed = 0;
};
// Port of 00465830 up to, but excluding, its state callback at 004658B0.
// Current impulse remains the caller's distinct input; no severity is inferred.
bool accumulate_contact_hit(ContactRecord& record, const ContactVector& current,
                            const ContactVector& point, float tangent_speed,
                            float timestamp) noexcept;
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
};
bool calculate_car_contact_response(const ContactBody& a, const ContactBody& b,
                                    const ContactGeometry& geometry,
                                    ContactResponse& response) noexcept;

// Synthetic witness callback mode: identity orientation, four wheels, null
// optional actor feedback/config hooks and race-event bypass. This is explicit
// caller evidence, never a default for production actors.
struct ContactCallbackContext {
    std::uint32_t world_seed = 0;
    // Authenticated lateral physical-data bounds at actor+20+14/+44, A/B.
    bool has_physical_bounds = false;
    std::array<std::array<float,2>,2> physical_lateral_bounds{};
};
enum class ContactResolution { refused, not_admitted, non_closing, applied, needs_state_callback };
// Explicitly admitted/prepared kind-0 pair in the witnessed normal mode.
// This bounded step closes no production admission gate. A hard current hit on
// a state-0 car requires explicit witnessed_callback evidence. Other callback
// branches refuse all mutation. A prior record gate is supported with explicit
// callback context when both actors are already state1. Optional alternate/race/effects modes are
// outside this API; caller must establish this witnessed mode before entry.
ContactResolution resolve_prepared_car_contact(ContactBody& a, ContactBody& b,
    ContactRecord& record, bool admitted, const ContactGeometry& geometry,
    float timestamp, ContactResponse& response,
    ContactCallbackContext* witnessed_callback = nullptr) noexcept;
}
