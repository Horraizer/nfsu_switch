#include "nfsu/car_contact.h"
#include "nfsu/det_sqrt.h"
#include <cmath>
#include <limits>

namespace nfsu {
namespace {
static_assert(std::numeric_limits<float>::is_iec559, "IEEE float required");
bool finite(const ContactVector& v) noexcept {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
float length(const ContactVector& v) noexcept {
    // Original x87 precision-24 instruction order is z*z + y*y + x*x.
    const float z = v[2] * v[2];
    const float y = v[1] * v[1];
    const float x = v[0] * v[0];
    const float zy = z + y;
    return static_cast<float>(det_sqrt(static_cast<double>(zy + x)));
}
bool valid(const ContactBody& b) noexcept {
    return b.identity != 0 && b.inverse_mass > 0 && std::isfinite(b.inverse_mass)
        && finite(b.linear_momentum) && finite(b.angular_momentum);
}
}
bool accumulate_contact_hit(ContactRecord& record, const ContactVector& current,
                            const ContactVector& point, float tangent_speed,
                            float timestamp) noexcept {
    if (!finite(current) || !finite(point) || !finite(record.accumulated_impulse)
        || !std::isfinite(record.magnitude_sum) || !std::isfinite(tangent_speed)
        || !std::isfinite(timestamp)) return false;
    ContactRecord out = record;
    for (unsigned i = 0; i < 3; ++i) out.accumulated_impulse[i] += current[i];
    // At this boundary original norm sums x*x + z*z + y*y.
    const float x = current[0] * current[0];
    const float z = current[2] * current[2];
    const float y = current[1] * current[1];
    out.magnitude_sum += static_cast<float>(det_sqrt(static_cast<double>((x + z) + y)));
    out.timestamp = timestamp;
    out.point = point;
    out.tangent_speed = tangent_speed;
    if (!finite(out.accumulated_impulse) || !std::isfinite(out.magnitude_sum)) return false;
    record = out;
    return true;
}
bool apply_car_contact_impulse(ContactBody& body, const ContactVector& impulse,
                              const ContactVector& arm) noexcept {
    if (!valid(body) || !finite(impulse) || !finite(arm)) return false;
    ContactBody out = body;
    // speed.exe constants 006CC7BC=1, 006B727C=1.85, 006B7280=.4.
    const float threshold = 1.0F / 1.85F;
    const float mass_scale = body.inverse_mass > threshold
        ? threshold / body.inverse_mass : 1.0F;
    ContactVector applied{};
    for (unsigned i = 0; i < 3; ++i) applied[i] = impulse[i] * mass_scale;
    const float magnitude = length(applied);
    if (!std::isfinite(magnitude)) return false;
    // 006B5B4C: clamp impulse magnitude before both linear and angular update.
    if (magnitude > 1000.0F) {
        const float scale = 1000.0F / magnitude;
        for (float& v : applied) v *= scale;
    }
    for (unsigned i = 0; i < 3; ++i) out.linear_momentum[i] += applied[i];
    ContactVector torque = {applied[2] * arm[1] - applied[1] * arm[2],
                           applied[0] * arm[2] - applied[2] * arm[0],
                           applied[1] * arm[0] - applied[0] * arm[1]};
    // 0058501B..2F sums y*y + z*z + x*x. 006B5B50=10.
    const float yy = torque[1] * torque[1];
    const float zz = torque[2] * torque[2];
    const float xx = torque[0] * torque[0];
    const float torque_length = static_cast<float>(det_sqrt(static_cast<double>((yy + zz) + xx)));
    if (!std::isfinite(torque_length)) return false;
    if (torque_length > 10.0F) {
        const float scale = 10.0F / torque_length;
        for (float& v : torque) v *= scale;
    }
    const float angular_scale = body.collision_state == 1 ? 1.0F : 0.4F;
    for (unsigned i = 0; i < 3; ++i) out.angular_momentum[i] += torque[i] * angular_scale;
    if (!finite(out.linear_momentum) || !finite(out.angular_momentum)) return false;
    body = out;
    return true;
}
bool apply_contact_pair(ContactBody& a, ContactBody& b,
                        const ContactVector& impulse_a, const ContactVector& arm_a,
                        const ContactVector& impulse_b, const ContactVector& arm_b) noexcept {
    if (&a == &b || a.identity == b.identity) return false;
    ContactBody next_a = a, next_b = b;
    if (!apply_car_contact_impulse(next_b, impulse_b, arm_b)
        || !apply_car_contact_impulse(next_a, impulse_a, arm_a)) return false;
    b = next_b;
    a = next_a;
    return true;
}
namespace {
ContactVector cross(const ContactVector& a, const ContactVector& b) noexcept {
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
float dot(const ContactVector& a, const ContactVector& b) noexcept {
    const float z = a[2]*b[2], y = a[1]*b[1], x = a[0]*b[0];
    return (z+y)+x;
}
float rotational_denominator(const ContactBody& b, const ContactVector& r,
                             const ContactVector& n) noexcept {
    const auto c = cross(r,n);
    const float state_scale = b.collision_state == 1 ? 1.0F : 0.4F;
    ContactVector transformed{};
    // Scalar math table 005ABDAF: row-vector transform, each matrix scalar
    // rounded after original CarCollisionBody state scaling at 0046A0C0.
    for (unsigned i=0; i<3; ++i) {
        const float x = c[0]*(b.inverse_inertia[0][i]*state_scale);
        const float y = c[1]*(b.inverse_inertia[1][i]*state_scale);
        const float z = c[2]*(b.inverse_inertia[2][i]*state_scale);
        transformed[i] = (z+y)+x;
    }
    return dot(cross(transformed,r),n);
}
}
bool calculate_car_contact_response(const ContactBody& a, const ContactBody& b,
                                    const ContactGeometry& geometry,
                                    ContactResponse& response) noexcept {
    if (!valid(a) || !valid(b) || a.identity == b.identity
        || !finite(a.position) || !finite(b.position)
        || !finite(a.cached_velocity) || !finite(b.cached_velocity)
        || !finite(a.cached_angular_velocity) || !finite(b.cached_angular_velocity)
        || !finite(geometry.point) || !finite(geometry.normal)) return false;
    for (unsigned i=0;i<3;++i)
        if (!finite(a.inverse_inertia[i]) || !finite(b.inverse_inertia[i])) return false;
    ContactResponse out;
    for (unsigned i=0;i<3;++i) {
        out.arm_a[i] = geometry.point[i]-a.position[i];
        out.arm_b[i] = geometry.point[i]-b.position[i];
    }
    const auto spin_a = cross(a.cached_angular_velocity,out.arm_a);
    const auto spin_b = cross(b.cached_angular_velocity,out.arm_b);
    ContactVector relative{};
    for (unsigned i=0;i<3;++i)
        relative[i] = (b.cached_velocity[i]+spin_b[i])-(a.cached_velocity[i]+spin_a[i]);
    const float normal_speed = dot(relative,geometry.normal);
    if (!std::isfinite(normal_speed)) return false;
    if (!(normal_speed < 0)) { response = out; return true; }
    const float inv_a = a.inverse_mass > 1.0F/1.85F ? 1.0F/1.85F : a.inverse_mass;
    const float inv_b = b.inverse_mass > 1.0F/1.85F ? 1.0F/1.85F : b.inverse_mass;
    const float rotational_a = rotational_denominator(a,out.arm_a,geometry.normal);
    const float denominator = rotational_denominator(b,out.arm_b,geometry.normal)
        + ((inv_b+inv_a)+rotational_a);
    if (!(denominator > 0) || !std::isfinite(denominator)) return false;
    // Both kind-0 bodies use 00466900=.1 restitution, 00466840=1
    // impulse factor, 00466910=.3 friction. No opponent/traffic constants.
    const float numerator = -((0.1F+1.0F)*normal_speed);
    float normal_impulse = numerator/denominator;
    // Original 0046BE82..A5 sets a LOWER bound, including slow-closing cases.
    const float minimum = 0.3F/(inv_a > inv_b ? inv_a : inv_b);
    if (normal_impulse < minimum) normal_impulse = minimum;
    ContactVector tangent{};
    for (unsigned i=0;i<3;++i) tangent[i] = relative[i]+geometry.normal[i]*(-normal_speed);
    out.tangent_speed = length(tangent);
    float friction_impulse = 0.3F*normal_impulse;
    if (out.tangent_speed < 1.0F) friction_impulse *= out.tangent_speed;
    ContactVector friction{};
    if (out.tangent_speed > 0.00001F) {
        const float scale = (-1.0F/out.tangent_speed)*friction_impulse;
        for (unsigned i=0;i<3;++i) friction[i] = tangent[i]*scale;
    }
    for (unsigned i=0;i<3;++i) {
        out.impulse_b[i] = geometry.normal[i]*normal_impulse+friction[i];
        out.current_impulse[i] = -out.impulse_b[i];
    }
    if (!finite(out.current_impulse) || !std::isfinite(out.tangent_speed)) return false;
    out.closing = true;
    response = out;
    return true;
}

ContactResolution resolve_prepared_car_contact(ContactBody& a, ContactBody& b,
    ContactRecord& record, bool admitted, const ContactGeometry& geometry,
    float timestamp, ContactResponse& response,
    ContactCallbackContext* witnessed_callback) noexcept {
    if (!admitted) { response = {}; return ContactResolution::not_admitted; }
    ContactResponse calculated;
    if (&a == &b || !calculate_car_contact_response(a,b,geometry,calculated))
        return ContactResolution::refused;
    response = calculated;
    if (!calculated.closing) return ContactResolution::non_closing;
    // Callback 00465D00 sums x*x + y*y + z*z, caps at 50, then compares
    // to 006B72BC=30. A state-0 body receives extra impulses and state changes.
    const auto& v = calculated.current_impulse;
    const float x = v[0]*v[0], y = v[1]*v[1], z = v[2]*v[2];
    const float current_magnitude = static_cast<float>(det_sqrt(static_cast<double>((x+y)+z)));

    const bool hard = current_magnitude >= 30.0F
        && (a.collision_state == 0 || b.collision_state == 0);
    if (record.state_gate || (hard && !witnessed_callback))
        return ContactResolution::needs_state_callback;
    ContactBody next_a=a, next_b=b;
    ContactRecord next_record=record;
    ContactCallbackContext callback = witnessed_callback ? *witnessed_callback : ContactCallbackContext{};
    if (!accumulate_contact_hit(next_record,calculated.current_impulse,geometry.point,
                               calculated.tangent_speed,timestamp)) return ContactResolution::refused;
    ContactVector arm_a=calculated.arm_a, arm_b=calculated.arm_b;
    if (hard) {
        // Only the source-addressed head-on hard callback branch is currently
        // ported. Its normalized current impulse must oppose each car velocity.
        // Other branches read actor+20 handling/config and 004663A0 eligibility.
        for (unsigned receiver=0;receiver<2;++receiver) {
            ContactBody& body = receiver == 0 ? next_a : next_b;
            if (body.collision_state != 0) continue;
            ContactVector directed=calculated.current_impulse;
            if(receiver) for(float& t:directed) t=-t;
            const auto unit = [](const ContactVector& input) {
                const float x=input[0]*input[0],y=input[1]*input[1],z=input[2]*input[2];
                const float norm=static_cast<float>(det_sqrt(static_cast<double>((x+y)+z)));
                if(norm==0) return ContactVector{1,0,0};
                const float scale=1.0F/norm;
                return ContactVector{input[0]*scale,input[1]*scale,input[2]*scale};
            };
            const auto direction=unit(directed), velocity=unit(body.cached_velocity);
            const float projection=(direction[0]*velocity[0]+velocity[2]*direction[2])
                +velocity[1]*direction[1];
            if(!(projection < -0.97F)) return ContactResolution::needs_state_callback;
            body.collision_state=1;body.state_timer=0;next_record.state_gate=true;
            ContactVector sideways{};
            for(unsigned i=0;i<3;++i) sideways[i]=direction[i]-velocity[i]*projection;
            sideways=unit(sideways);
            const float severity=current_magnitude > 50.0F ? 50.0F : current_magnitude;
            const float scale=-(std::fabs(projection)*0.2F*severity);
            ContactVector extra{sideways[0]*scale,sideways[1]*scale,0};
            const auto arm=receiver == 0 ? calculated.arm_a : calculated.arm_b;
            if(!apply_car_contact_impulse(body,extra,arm)) return ContactResolution::refused;
            // 00466113 consumes original world seed once for each state-0 body.
            const unsigned remainder=callback.world_seed%100;
            const auto mixed=callback.world_seed^0x1D872B41U;
            const auto shifted=(mixed>>5)^mixed;
            callback.world_seed=(shifted<<27)^shifted^mixed;
            ContactVector local_arm=arm;
            local_arm[2]=0;
            if(remainder>=50) local_arm[0]=-(local_arm[0]*1.1F);
            if(std::fabs(local_arm[1])>0.13F) local_arm[1]*=0.2F;
            // Original clamp helper 00401C80: [-4.1,+4.1].
            float vertical=std::fabs(projection)*0.0675F*severity;
            if(vertical>4.1F) vertical=4.1F;
            if(!apply_car_contact_impulse(body,{0,0,vertical},local_arm)) return ContactResolution::refused;
        }
        // Callback-set record gate, original four-wheel identity-up projection.
        arm_a[2]=0;arm_b[2]=0;
    }
    if (!apply_contact_pair(next_a,next_b,calculated.current_impulse,arm_a,
                            calculated.impulse_b,arm_b)) return ContactResolution::refused;
    a=next_a; b=next_b; record=next_record;
    if(witnessed_callback) *witnessed_callback=callback;
    return ContactResolution::applied;
}
}
