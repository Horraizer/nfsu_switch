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
    return b.identity != 0 && (b.inverse_mass > 0 || (!b.car_scaling && b.inverse_mass==0)) && std::isfinite(b.inverse_mass)
        && finite(b.linear_momentum) && finite(b.angular_momentum);
}
}
bool refresh_car_contact_velocity(ContactBody& body, float raw_mass,
                                  float speed_limit) noexcept {
    if (!valid(body) || !std::isfinite(raw_mass) || raw_mass <= 0
        || !std::isfinite(speed_limit) || speed_limit < 0) return false;
    for (const auto& row:body.inverse_inertia) if (!finite(row)) return false;
    ContactBody next=body;
    for(unsigned i=0;i<3;++i) next.cached_velocity[i]=next.linear_momentum[i]*next.inverse_mass;
    const auto norm=[](const ContactVector& v) {
        const float x=v[0]*v[0],y=v[1]*v[1],z=v[2]*v[2];
        return static_cast<float>(det_sqrt(static_cast<double>((x+y)+z)));
    };
    const float speed=norm(next.cached_velocity);
    if(!std::isfinite(speed)) return false;
    // Original compares a float raw mass against the double constant.
    if(speed>speed_limit && static_cast<double>(raw_mass)>0.7) {
        const float scale=speed_limit/speed;
        for(unsigned i=0;i<3;++i) {
            next.linear_momentum[i]*=scale;
            next.cached_velocity[i]=next.linear_momentum[i]*next.inverse_mass;
        }
    }
    for(unsigned i=0;i<3;++i) {
        const float x=next.angular_momentum[0]*next.inverse_inertia[0][i];
        const float y=next.angular_momentum[1]*next.inverse_inertia[1][i];
        const float z=next.angular_momentum[2]*next.inverse_inertia[2][i];
        next.cached_angular_velocity[i]=((z+y)+x)+0.0F;
    }
    const float spin=norm(next.cached_angular_velocity);
    if(!std::isfinite(spin)) return false;
    if(spin>250.0F) {
        const float scale=250.0F/spin;
        for(float& component:next.cached_angular_velocity) component*=scale;
    }
    if(!finite(next.cached_velocity)||!finite(next.cached_angular_velocity)
        || !finite(next.linear_momentum)) return false;
    body=next;
    return true;
}
bool prepare_car_contact_geometry(const ContactVector& witness_a,
    const ContactVector& witness_b, const ContactVector& position_a,
    const ContactVector& position_b, ContactGeometry& geometry) noexcept {
    if(!finite(witness_a)||!finite(witness_b)||!finite(position_a)||!finite(position_b))
        return false;
    ContactGeometry next;
    for(unsigned i=0;i<3;++i) {
        next.point[i]=(witness_b[i]+witness_a[i])*0.5F;
        next.normal[i]=witness_b[i]-witness_a[i];
    }
    float norm=length(next.normal);
    if(!std::isfinite(norm)) return false;
    if(norm < 0.000001F) {
        for(unsigned i=0;i<3;++i) next.normal[i]=position_b[i]-position_a[i];
        const float x=next.normal[0]*next.normal[0];
        const float y=next.normal[1]*next.normal[1];
        const float z=next.normal[2]*next.normal[2];
        norm=static_cast<float>(det_sqrt(static_cast<double>((x+y)+z)));
        if(!std::isfinite(norm)) return false;
        if(norm==0) next.normal={1,0,0};
    }
    if(norm!=0) {
        const float scale=1.0F/norm;
        for(float& v:next.normal) v*=scale;
    }
    if(!finite(next.point)||!finite(next.normal)) return false;
    geometry=next;
    return true;
}
bool initialize_car_contact_record(ContactRecord& record,
    const ContactVector& initial_point, std::uint32_t tick) noexcept {
    if (!finite(initial_point)) return false;
    ContactRecord next=record;
    next.active=true;
    next.state_gate=false;
    next.accumulated_impulse={};next.magnitude_sum=0;
    next.creation_tick=tick;next.last_hit_tick=tick;
    next.initial_point=initial_point;
    record=next;
    return true;
}
bool accumulate_contact_hit(ContactRecord& record, const ContactVector& current,
                            const ContactVector& point, float tangent_speed,
                            std::uint32_t tick) noexcept {
    if (!finite(current) || !finite(point) || !finite(record.accumulated_impulse)
        || !std::isfinite(record.magnitude_sum) || !std::isfinite(tangent_speed)) return false;
    ContactRecord out = record;
    for (unsigned i = 0; i < 3; ++i) out.accumulated_impulse[i] += current[i];
    // At this boundary original norm sums x*x + z*z + y*y.
    const float x = current[0] * current[0];
    const float z = current[2] * current[2];
    const float y = current[1] * current[1];
    out.magnitude_sum += static_cast<float>(det_sqrt(static_cast<double>((x + z) + y)));
    out.last_hit_tick = tick;
    out.point = point;
    out.tangent_speed = tangent_speed;
    if (!finite(out.accumulated_impulse) || !std::isfinite(out.magnitude_sum)) return false;
    record = out;
    return true;
}
bool apply_car_contact_impulse(ContactBody& body, const ContactVector& impulse,
                              const ContactVector& arm) noexcept {
    if (!valid(body) || !finite(impulse) || !finite(arm)) return false;
    //592940 ignores impulses when WorldObject backing is absent. Backed
    //kind4 requires its separate raw-mass cap and is outside this boundary.
    if(body.kind==4)return body.inverse_mass==0;
    ContactBody out = body;
    // speed.exe constants 006CC7BC=1, 006B727C=1.85, 006B7280=.4.
    const float threshold = 1.0F / 1.85F;
    const float mass_scale = body.car_scaling && body.inverse_mass > threshold
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
    const float angular_scale = !body.car_scaling || body.collision_state == 1 ? 1.0F : 0.4F;
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
    const float state_scale = !b.car_scaling || b.collision_state == 1 ? 1.0F : 0.4F;
    ContactVector transformed{};
    const auto& inverse=b.separate_response_inertia?b.response_inverse_inertia:b.inverse_inertia;
    // Scalar math table 005ABDAF: row-vector transform, each matrix scalar
    // rounded after original CarCollisionBody state scaling at 0046A0C0.
    for (unsigned i=0; i<3; ++i) {
        const float x = c[0]*(inverse[0][i]*state_scale);
        const float y = c[1]*(inverse[1][i]*state_scale);
        const float z = c[2]*(inverse[2][i]*state_scale);
        transformed[i] = (z+y)+x;
    }
    return dot(cross(transformed,r),n);
}
}
static bool calculate_response(const ContactBody& a, const ContactBody& b,
                                    const ContactGeometry& geometry,
                                    ContactResponse& response, float callback_factor,
                                    float restitution,float friction_coefficient) noexcept {
    if (!valid(a) || !valid(b) || a.identity == b.identity
        || !finite(a.position) || !finite(b.position)
        || !finite(a.cached_velocity) || !finite(b.cached_velocity)
        || !finite(a.cached_angular_velocity) || !finite(b.cached_angular_velocity)
        || !finite(geometry.point) || !finite(geometry.normal)) return false;
    for (unsigned i=0;i<3;++i)
        if (!finite(a.inverse_inertia[i]) || !finite(b.inverse_inertia[i])) return false;
    for(const auto* body:{&a,&b})if(body->separate_response_inertia)
        for(const auto& row:body->response_inverse_inertia)if(!finite(row))return false;
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
    const float inv_a = a.car_scaling && a.inverse_mass > 1.0F/1.85F ? 1.0F/1.85F : a.inverse_mass;
    const float inv_b = b.car_scaling && b.inverse_mass > 1.0F/1.85F ? 1.0F/1.85F : b.inverse_mass;
    const float rotational_a = rotational_denominator(a,out.arm_a,geometry.normal);
    const float denominator = rotational_denominator(b,out.arm_b,geometry.normal)
        + ((inv_b+inv_a)+rotational_a);
    if (!(denominator > 0) || !std::isfinite(denominator)) return false;
    // Both kind-0 bodies use 00466900=.1 restitution, 00466840=1
    // impulse factor, 00466910=.3 friction. No opponent/traffic constants.
    const float numerator = -((restitution+1.0F)*normal_speed);
    float normal_impulse = numerator/denominator;
    // Original 0046BE82..A5 sets a LOWER bound, including slow-closing cases.
    const float minimum = 0.3F/(inv_a > inv_b ? inv_a : inv_b);
    if (normal_impulse < minimum) normal_impulse = minimum;
    normal_impulse*=callback_factor;
    out.normal_impulse=normal_impulse;
    ContactVector tangent{};
    for (unsigned i=0;i<3;++i) tangent[i] = relative[i]+geometry.normal[i]*(-normal_speed);
    out.tangent_speed = length(tangent);
    float friction_impulse = friction_coefficient*normal_impulse;
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

bool calculate_car_contact_response(const ContactBody& a,const ContactBody& b,
    const ContactGeometry& geometry,ContactResponse& response,float callback_factor,
    ContactMaterial other_material,bool pinned_prop_against_upright_car,
    ContactMaterial own_material,bool pinned_prop_against_upright_car_b) noexcept {
    // 46BAE0 selects the greater value returned by the two body material getters.
    if(!std::isfinite(other_material.restitution)||other_material.restitution<0||
       !std::isfinite(other_material.friction)||other_material.friction<0||
       !std::isfinite(own_material.restitution)||own_material.restitution<0||
       !std::isfinite(own_material.friction)||own_material.friction<0)return false;
    const float friction=pinned_prop_against_upright_car||pinned_prop_against_upright_car_b?
        .1F:std::max(own_material.friction,other_material.friction);
    if(!calculate_response(a,b,geometry,response,callback_factor,
        std::max(own_material.restitution,other_material.restitution),friction))return false;
    // 46C09B..46C342 adds horizontal drag to the car above 30 m/s and
    // shortens its impulse arm. The impulse on the prop is unaffected.
    if((pinned_prop_against_upright_car||pinned_prop_against_upright_car_b)&&response.closing) {
        const auto& velocity=pinned_prop_against_upright_car?a.cached_velocity:b.cached_velocity;
        auto& current=pinned_prop_against_upright_car?response.current_impulse:response.impulse_b;
        auto& arm=pinned_prop_against_upright_car?response.arm_a:response.arm_b;
        const float speed=length(velocity);
        if(speed>30.F) {
            const float impulse=length(current);
            const float drag=(-1.F/speed)*.4F;
            current[0]+=velocity[0]*drag;
            current[1]+=velocity[1]*drag;
            const float arm_scale=impulse/(speed/1.75F+impulse);
            for(auto& value:arm)value*=arm_scale;
        }
    }
    return true;
}

bool calculate_track_contact_response(const ContactBody& car,const ContactGeometry& geometry,
    const ContactVector& forward,const ContactVector& up,float surface_friction,
    ContactResponse& response) noexcept {
    if(!finite(forward)||!finite(up)||!std::isfinite(surface_friction)||surface_friction<0)return false;
    ContactBody fixed;fixed.identity=car.identity==1?2:1;fixed.car_scaling=false;
    const auto norm=[](const ContactVector& v) {
        return static_cast<float>(det_sqrt(double((v[1]*v[1]+v[2]*v[2])+v[0]*v[0])));
    };
    ContactVector arm{};for(unsigned k=0;k<3;++k)arm[k]=geometry.point[k]-car.position[k];
    float restitution=.1F;
    if(std::abs(geometry.normal[2])<.1F) {
        const float magnitude=length(arm);
        ContactVector unit{{1,0,0}};
        if(magnitude!=0)for(unsigned k=0;k<3;++k)unit[k]=arm[k]*(1.F/magnitude);
        const float projection=(unit[1]*forward[1]+unit[2]*forward[2])+unit[0]*forward[0];
        if(std::abs(projection)<.9F)restitution*=.5F;
    }
    const bool upright=up[2]>.7F;
    const float friction=upright?.1F:(surface_friction>.3F?surface_friction:.3F);
    ContactResponse out;
    if(!calculate_response(car,fixed,geometry,out,1,restitution,friction))return false;
    const float speed=norm(car.cached_velocity);
    if(out.closing&&upright&&speed>30.F) {
        const float magnitude=norm(out.current_impulse);
        float drag=-.025F;
        if(-.4F>speed*drag)drag=(-1.F/speed)*.4F;
        out.current_impulse[0]+=car.cached_velocity[0]*drag;
        out.current_impulse[1]+=car.cached_velocity[1]*drag;
        if(magnitude+speed>1.F) {
            const float shorten=magnitude/(speed/1.75F+magnitude);
            for(float& v:out.arm_a)v*=shorten;
        }
    }
    response=out;return true;
}

bool drag_contact_failure(const ContactVector& current,bool drag,
    unsigned physical_selector,unsigned other_kind,std::uint32_t other_name,
    bool& totalled) noexcept {
    if(!finite(current))return false;
    const float x=current[0]*current[0],y=current[1]*current[1],z=current[2]*current[2];
    const float magnitude=static_cast<float>(det_sqrt(double((x+y)+z)));
    const bool special=other_kind==4&&(other_name==0x24cebc53U||other_name==0x427a6453U);
    totalled=drag&&physical_selector==1&&magnitude>=(special?15.F:35.F);
    return true;
}
ContactResolution resolve_prepared_car_contact(ContactBody& a, ContactBody& b,
    ContactRecord& record, bool admitted, const ContactGeometry& geometry,
    std::uint32_t tick, ContactResponse& response,
    ContactCallbackContext* witnessed_callback) noexcept {
    if (!admitted) { response = {}; return ContactResolution::not_admitted; }
    ContactResponse calculated;
    const bool fixed_a=a.kind==4&&a.inverse_mass==0,fixed_b=b.kind==4&&b.inverse_mass==0;
    if((fixed_a||fixed_b)&&!witnessed_callback)return ContactResolution::needs_state_callback;
    const bool upright_a=fixed_b&&a.kind==0&&witnessed_callback->body_axes[0][2][2]>.7F;
    const bool upright_b=fixed_a&&b.kind==0&&witnessed_callback->body_axes[1][2][2]>.7F;
    if (&a == &b || !calculate_car_contact_response(a,b,geometry,calculated,1.F,{},upright_a,{},upright_b))
        return ContactResolution::refused;
    response = calculated;
    if (!calculated.closing) return ContactResolution::non_closing;
    // Callback 00465D00 sums x*x + y*y + z*z, caps at 50, then compares
    // to 006B72BC=30. A state-0 body receives extra impulses and state changes.
    const auto& v = calculated.current_impulse;
    const float x = v[0]*v[0], y = v[1]*v[1], z = v[2]*v[2];
    const float current_magnitude = static_cast<float>(det_sqrt(static_cast<double>((x+y)+z)));

    const bool hard = current_magnitude >= 30.0F
        && ((a.kind==0&&a.collision_state==0)||(b.kind==0&&b.collision_state==0));
    if ((record.state_gate && !witnessed_callback)
        || (hard && !witnessed_callback))
        return ContactResolution::needs_state_callback;
    ContactBody next_a=a, next_b=b;
    ContactRecord next_record=record;
    ContactCallbackContext callback = witnessed_callback ? *witnessed_callback : ContactCallbackContext{};
    for(unsigned receiver=0;receiver<2;++receiver) {
        for(const auto& axis:callback.body_axes[receiver])
            if(!finite(axis)) return ContactResolution::refused;
        if(!finite(callback.actor_up[receiver])) return ContactResolution::refused;
    }
    if (!accumulate_contact_hit(next_record,calculated.current_impulse,geometry.point,
                               calculated.tangent_speed,tick)) return ContactResolution::refused;
    ContactVector arm_a=calculated.arm_a, arm_b=calculated.arm_b;
    if (hard) {
        // Both car kinds satisfy original 004663A0 eligibility. The
        // non-opposing branch requires original actor+20 physical bounds.
        for (unsigned receiver=0;receiver<2;++receiver) {
            ContactBody& body = receiver == 0 ? next_a : next_b;
            if (body.kind!=0||body.collision_state != 0) continue;
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
            const bool opposing = projection < -0.97F;
            if (!opposing && next_a.kind==0 && next_b.kind==0 && (!callback.has_physical_bounds
                || !std::isfinite(callback.physical_lateral_bounds[receiver][0])
                || !std::isfinite(callback.physical_lateral_bounds[receiver][1])))
                return ContactResolution::needs_state_callback;
            body.collision_state=1;body.state_timer=0;next_record.state_gate=true;
            const float severity=current_magnitude > 50.0F ? 50.0F : current_magnitude;
            //465D00 derives its own arm from the witness before the static
            //high-speed branch shortens the final response arm.
            ContactVector arm{};for(unsigned k=0;k<3;++k)arm[k]=geometry.point[k]-body.position[k];
            // 565230 transposes the body matrix; 5ABDAF transforms the arm
            // into body space. Each operation rounds to binary32 (007F);
            // double intermediates change subsequent grounded angular impulses.
            // Do not flatten a world-space arm on slopes.
            const auto& axes=callback.body_axes[receiver];
            ContactVector local_arm{};
            for(unsigned k=0;k<3;++k)
                local_arm[k]=((arm[2]*axes[k][2]+arm[1]*axes[k][1])+arm[0]*axes[k][0])+0.F;
            local_arm[2]=0;
            if (opposing) {
                ContactVector sideways{};
                for(unsigned i=0;i<3;++i) sideways[i]=direction[i]-velocity[i]*projection;
                sideways=unit(sideways);
                const float scale=-(std::fabs(projection)*0.2F*severity);
                ContactVector extra{sideways[0]*scale,sideways[1]*scale,0};
                if(!apply_car_contact_impulse(body,extra,arm)) return ContactResolution::refused;
                // 00466113 consumes seed only in the opposing branch.
                const unsigned remainder=callback.world_seed%100;
                const auto mixed=callback.world_seed^0x1D872B41U;
                const auto shifted=(mixed>>5)^mixed;
                callback.world_seed=(shifted<<27)^shifted^mixed;
                if(remainder>=50) local_arm[0]=-(local_arm[0]*1.1F);
                if(std::fabs(local_arm[1])>0.13F) local_arm[1]*=0.2F;
            } else if(next_a.kind==0&&next_b.kind==0) {
                // 004663A0 requires TWO kind0 bodies. Physical data is the
                // original actor+20=RideInfo+60 subobject, never guessed.
                const auto& bounds=callback.physical_lateral_bounds[receiver];
                const float limit=(std::fabs(bounds[0]-bounds[1])*1.19F)*0.5F;
                if (!std::isfinite(limit)) return ContactResolution::refused;
                local_arm[1]*=1.19F;
                if(local_arm[1]>limit) local_arm[1]=limit;
                if(local_arm[1]<-limit) local_arm[1]=-limit;
                local_arm[0]*=1.0F;
            } else {
                // Non-opposing car/kind4 enters state1 without the car/car
                //lateral scaling or the extra vertical impulse (466181).
                continue;
            }
            //4661ED..466283: drag scales both the value and clamp by1.2.
            const float mode=callback.drag?1.2F:1.F;
            float vertical=((std::fabs(projection)*0.0675F)*severity)*mode;
            const float limit=mode*4.1F;
            if(vertical>limit) vertical=limit;
            ContactVector world_arm{};
            for(unsigned k=0;k<3;++k)
                world_arm[k]=((local_arm[2]*axes[2][k]+local_arm[1]*axes[1][k])+local_arm[0]*axes[0][k])+0.F;
            if(!apply_car_contact_impulse(body,{0,0,vertical},world_arm)) return ContactResolution::refused;
        }
    }
    // 46C425/46C4BA use each ACTOR's published up, independently of the
    // body matrix used by the callback. Only all-four-wheel support projects.
    if(next_record.state_gate) for(unsigned receiver=0;receiver<2;++receiver) {
        if((receiver?next_b:next_a).kind!=0||callback.grounded_wheels[receiver]!=4) continue;
        auto& arm=receiver==0?arm_a:arm_b;
        const auto& up=callback.actor_up[receiver];
        const float along=(up[2]*arm[2]+up[1]*arm[1])+up[0]*arm[0];
        for(unsigned k=0;k<3;++k) arm[k]=arm[k]-up[k]*along;
    }
    if (!apply_contact_pair(next_a,next_b,calculated.current_impulse,arm_a,
                            calculated.impulse_b,arm_b)) return ContactResolution::refused;
    a=next_a; b=next_b; record=next_record;
    if(witnessed_callback) *witnessed_callback=callback;
    return ContactResolution::applied;
}

void update_car_collision_state(std::uint32_t& state,float& support_since,
    unsigned grounded_wheels,std::int8_t body_flag,double now_seconds) noexcept {
    if(state==0) {if(body_flag>0)state=2;return;}
    if(state==2) {if(body_flag==0&&grounded_wheels==4)state=0;return;}
    if(state!=1) return;
    if(body_flag!=0||grounded_wheels!=4) {support_since=0;return;}
    // 42A579 rounds world seconds before 42A58B subtracts actor+4CC.
    const float now=static_cast<float>(now_seconds);
    if(support_since==0) support_since=now;
    else if(now-support_since>=0.10000000149011612F) state=0;
}
}
