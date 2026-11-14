#include "nfsu/race_contacts.h"
#include "nfsu/prop_settling.h"
#include "nfsu/player_powertrain.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace nfsu {
namespace {
VehicleVec3 add(VehicleVec3 a,VehicleVec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
VehicleVec3 sub(VehicleVec3 a,VehicleVec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
VehicleVec3 mul(VehicleVec3 a,double b){return {a.x*b,a.y*b,a.z*b};}
double dot(VehicleVec3 a,VehicleVec3 b){return (a.x*b.x+a.y*b.y)+a.z*b.z;}
VehicleVec3 cross(VehicleVec3 a,VehicleVec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
ContactVector narrow(VehicleVec3 a){return {float(a.x),float(a.y),float(a.z)};}
VehicleVec3 wide(ContactVector a){return {a[0],a[1],a[2]};}
VehicleVec3 separation_direction(VehicleVec3 a,VehicleVec3 b) {
    const auto pa=narrow(a),pb=narrow(b);
    ContactVector delta{pb[0]-pa[0],pb[1]-pa[1],pb[2]-pa[2]};
    //46B31D ->567370: binary32 differences, X/Y/Z squares, reciprocal.
    const float length=float(vehicle_sqrt((delta[0]*delta[0]+delta[1]*delta[1])+delta[2]*delta[2]));
    if(length==0)return {1,0,0};
    const float inverse=1.F/length;
    for(float& x:delta)x*=inverse;
    return wide(delta);
}
std::array<VehicleVec3,3> axes(const VehicleState& s) {
    if(s.rigid_body_ready && s.orientation.w==s.rigid_orientation.w &&
       s.orientation.x==s.rigid_orientation.x && s.orientation.y==s.rigid_orientation.y &&
       s.orientation.z==s.rigid_orientation.z) {
        const auto& a=s.rigid_body.axes;
        return {{{a[0][0],a[0][1],a[0][2]},{a[1][0],a[1][1],a[1][2]},
                 {a[2][0],a[2][1],a[2][2]}}};
    }
    const auto& q=s.orientation;
    return {{{1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)},
             {2*(q.x*q.y-q.w*q.z),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z+q.w*q.x)},
             {2*(q.x*q.z+q.w*q.y),2*(q.y*q.z-q.w*q.x),1-2*(q.x*q.x+q.y*q.y)}}};
}
struct Box {VehicleVec3 center,half;std::array<VehicleVec3,3> axis;};
Box placed(const VehicleState& s,const VehicleSpec& spec,const RaceContactBox& b) {
    Box out;out.axis=axes(s);out.half=mul(sub(b.high,b.low),.5);
    const auto& c=spec.handling.centre_of_mass;
    const auto offset=sub(mul(add(b.high,b.low),.5),{c[0],c[1],c[2]});
    out.center=add(s.position,add(add(mul(out.axis[0],offset.x),mul(out.axis[1],offset.y)),mul(out.axis[2],offset.z)));
    return out;
}
double radius(const Box& b,VehicleVec3 n) {
    return std::abs(dot(b.axis[0],n))*b.half.x+std::abs(dot(b.axis[1],n))*b.half.y+std::abs(dot(b.axis[2],n))*b.half.z;
}
int overlap_boxes(const Box& a,const Box& b,double& depth,VehicleVec3& normal,double margin=0) {
    const auto delta=sub(b.center,a.center);
    // Conservative world AABB rejection before all 15 oriented axes.
    for(const auto n:std::array<VehicleVec3,3>{{{1,0,0},{0,1,0},{0,0,1}}})
        if(std::abs(dot(delta,n))>=radius(a,n)+radius(b,n)+margin)return false;
    std::array<VehicleVec3,15> candidates{};
    for(unsigned i=0;i<3;++i){candidates[i]=a.axis[i];candidates[i+3]=b.axis[i];}
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)candidates[6+3*i+j]=cross(a.axis[i],b.axis[j]);
    depth=std::numeric_limits<double>::max();
    for(auto n:candidates) {
        const double length=vehicle_sqrt(dot(n,n));if(length<1e-8)continue;n=mul(n,1/length);
        const double overlap=radius(a,n)+radius(b,n)-std::abs(dot(delta,n));
        if(!std::isfinite(overlap))return -1;
        if(overlap<=-margin)return false;
        if(overlap<depth){depth=overlap;normal=dot(delta,n)<0?mul(n,-1):n;}
    }
    return true;
}
ContactBody body(const VehicleState& s,const VehicleSpec& spec,unsigned id) {
    ContactBody out;out.identity=id;out.inverse_mass=1/spec.handling.mass;
    out.collision_state=s.collision_state;out.state_timer=s.collision_support_since;
    out.position=narrow(s.position);out.cached_velocity=narrow(s.velocity);
    out.cached_angular_velocity=narrow(s.angular_velocity);
    out.linear_momentum=s.rigid_body.momentum;out.angular_momentum=s.rigid_body.angular_momentum;
    const auto a=axes(s);
    //58593F rotates the inverse for refreshed angular velocity.46A0DE
    //instead returns its local diagonal to46BA50; do not rotate that
    //response denominator along with a rewound/tilted car.
    out.inverse_inertia=player_body_inverse_tensor(spec.handling,
        {{narrow(a[0]),narrow(a[1]),narrow(a[2])}},s.alternate_yaw_active);
    out.response_inverse_inertia=player_body_inverse_tensor(spec.handling,
        {{{1,0,0},{0,1,0},{0,0,1}}},s.alternate_yaw_active);
    out.separate_response_inertia=true;
    return out;
}
bool publish(VehicleState& s,const VehicleSpec& spec,ContactBody& b,VehicleVec3 move) {
    if(!refresh_car_contact_velocity(b,spec.handling.mass,200.0F))return false;
    s.position=add(s.position,move);s.rigid_body.position=narrow(s.position);
    s.rigid_body.momentum=b.linear_momentum;s.rigid_body.angular_momentum=b.angular_momentum;
    s.rigid_body.velocity=b.cached_velocity;s.rigid_body.angular_velocity=b.cached_angular_velocity;
    s.velocity=wide(b.cached_velocity);s.angular_velocity=wide(b.cached_angular_velocity);
    s.collision_state=b.collision_state;s.collision_support_since=b.state_timer;
    s.body_rest_state=0;return true;
}
}
RaceContactBox race_contact_box(const VehicleHull& hull) {
    RaceContactBox b;b.low={1e30,1e30,1e30};b.high={-1e30,-1e30,-1e30};
    b.points=hull.points;
    b.neighbors=hull.neighbors;
    for(const auto& p:hull.points) {
        b.low={std::min(b.low.x,double(p[0])),std::min(b.low.y,double(p[1])),std::min(b.low.z,double(p[2]))};
        b.high={std::max(b.high.x,double(p[0])),std::max(b.high.y,double(p[1])),std::max(b.high.z,double(p[2]))};
    }
    return b;
}
static bool resolve_pair_impl(VehicleState& sa,const VehicleSpec& as,const RaceContactBox& ab,
                       VehicleState& sb,const VehicleSpec& bs,const RaceContactBox& bb,RacePairContact& result,RacePropImpact* prop,RaceContactContext* context,const RaceContactJob* job,RacePropImpact* own_prop=nullptr,std::array<bool,2> fixed_world={}) {
    result={};if(&sa==&sb)return false;
    const auto a=placed(sa,as,ab),b=placed(sb,bs,bb);
    double depth=0;VehicleVec3 normal{};
    const int overlap=overlap_boxes(a,b,depth,normal,prop?0:double(.005F));
    if(overlap<0)return false;
    if(!overlap&&!job)return true;
    auto na=sa,nb=sb;
    ContactGeometry geometry;
    VehicleVec3 point{};
    if(!prop||job) {
        RaceHullQuery query;
        if(job)query=job->query;
        else {
            if(!query_race_hulls(na,as,ab,nb,bs,bb,query))return false;
            if(query.distance>double(.005F))return true;
        }
        result.overlapping=true;
        // 46B260 kind-0/kind-0 fallback separates along body origins,
        // never along an OBB minimum-depth axis (which can be the roof).
        // 46B5E0 applies the same correction to both movable bodies.
        if(job) {
            if(!fixed_world[0]&&(!own_prop||own_prop->activation==0))na.position=wide(narrow(sub(na.position,job->correction)));
            if(!fixed_world[1]&&(!prop||prop->activation==0))nb.position=wide(narrow(add(nb.position,job->correction)));
        } else if(query.distance<=double(.0001F)) {
            const auto direction=separation_direction(na.position,nb.position);
            float distance=0,increment=.005F;
            for(unsigned attempt=0;attempt<20;++attempt) {
                distance+=increment;if(increment<.1F)increment+=.01F;
                auto move=wide(narrow(mul(direction,distance)));move.z=std::clamp(move.z,-double(.15F),double(.15F));
                na.position=wide(narrow(sub(sa.position,move)));nb.position=wide(narrow(add(sb.position,move)));
                if(!query_race_hulls(na,as,ab,nb,bs,bb,query))return false;
                if(query.distance>=double(.0001F))break;
            }
        }
        if(!prepare_car_contact_geometry(query.witness_a,query.witness_b,narrow(na.position),narrow(nb.position),geometry))return false;
        point=wide(geometry.point);normal=wide(geometry.normal);
        result.depth=vehicle_sqrt(dot(sub(na.position,sa.position),sub(na.position,sa.position)))*2;
    } else {
        result.overlapping=true;result.depth=depth;
        // The Smokeable prop adapter still supplies its own OBB witness.
        point=mul(add(a.center,b.center),.5);
        const double along=(dot(a.center,normal)+radius(a,normal)+dot(b.center,normal)-radius(b,normal))*.5;
        point=add(point,mul(normal,along-dot(point,normal)));
        geometry.normal=narrow(normal);geometry.point=narrow(point);
    }
    if(!na.rigid_body_ready)vehicle_import_rigid(as.handling,na);
    if(!nb.rigid_body_ready)vehicle_import_rigid(bs.handling,nb);
    if(job&&context&&context->refresh) {
        // 46B72F/46B73C write the corrected body origins before either
        // component refresh. Smokeable refresh publishes from rigid state.
        na.rigid_body.position=narrow(na.position);
        nb.rigid_body.position=narrow(nb.position);
        if(!context->refresh(context->refresh_owner,0,na)||
           !context->refresh(context->refresh_owner,1,nb))return false;
    }
    auto ba=body(na,as,1),bbod=body(nb,bs,2);
    float activation=prop?prop->activation:0;
    float own_activation=own_prop?own_prop->activation:0;
    const auto material=prop?prop->material:ContactMaterial{};
    const auto own_material=own_prop?own_prop->material:ContactMaterial{};
    const bool pinned_upright=!own_prop&&(fixed_world[1]||(prop&&activation>0))&&axes(na)[2].z>.7F;
    const bool pinned_upright_b=!prop&&(fixed_world[0]||(own_prop&&own_activation>0))&&axes(nb)[2].z>.7F;
    if(own_prop) {
        ba.kind=1;ba.car_scaling=false;
        if(own_activation>0){ba.inverse_mass=0;ba.response_inverse_inertia={};}
    }
    if(prop) {
        bbod.kind=1;bbod.car_scaling=false;
        // 5951A0 returns the local inverse tensor, or the zero tensor while pinned.
        if(activation>0){bbod.inverse_mass=0;bbod.response_inverse_inertia={};}
    }
    for(unsigned k=0;k<2;++k)if(fixed_world[k]) {
        auto& fixed=k?bbod:ba;fixed.kind=4;fixed.car_scaling=false;
        fixed.inverse_mass=0;fixed.inverse_inertia={};fixed.response_inverse_inertia={};
        //592880/592900 retain animated node velocities despite zero inverse mass.
        fixed.linear_momentum={};fixed.angular_momentum={};
    }
    result.point=point;result.position_a=sa.position;result.position_b=sb.position;
    result.velocity_a=sa.velocity;result.velocity_b=sb.velocity;
    auto pending_record=context?context->record:ContactRecord{};
    auto pending_seed=context?context->world_seed:0U;
    //46BB07 allocates the pair record before computing closing velocity.
    if(context&&!pending_record.active)
        if(!initialize_car_contact_record(pending_record,geometry.point,context->tick))return false;
    ContactResponse response;
    if(!calculate_car_contact_response(ba,bbod,geometry,response,1.F,material,pinned_upright,
        own_material,pinned_upright_b))return false;
    if(response.closing) {
        bool repeat=false;
        float fraction=1;
        const auto activate=[&](const VehicleState& state,const VehicleSpec& spec,ContactBody& response_body,
            float& threshold,const ContactVector& normal_impulse,const ContactVector& arm) {
            if(threshold<=0)return true;
            // 597980 receives the NORMAL impulse before friction. Its returned
            // break fraction scales the first solve; 46C5E7 retries after the
            // now-active object's mass/inertia and velocities are refreshed.
            const auto& com=spec.handling.centre_of_mass;
            const auto local_arm=add(wide(arm),{com[0],com[1],com[2]});
            const auto j=wide(normal_impulse),moment=cross(local_arm,j);
            const double severity=std::max(std::sqrt(dot(j,j)),std::sqrt(dot(moment,moment)));
            if(severity>=threshold) {
                const float part=float(threshold/severity);
                response_body=body(state,spec,response_body.identity);response_body.car_scaling=false;
                const auto compensation=narrow(mul(j,-part));
                const ContactVector pivot{{-com[0],-com[1],-com[2]}};
                if(!apply_car_contact_impulse(response_body,compensation,pivot))return false;
                threshold=0;fraction=std::min(fraction,part);repeat|=part<1;
            }
            return true;
        };
        // 46BEF0..46BF50 invokes B's activation before A's.
        const auto normal_b=narrow(mul(normal,response.normal_impulse));
        const auto normal_a=narrow(mul(normal,-response.normal_impulse));
        const auto initial_a=ba,initial_b=bbod;
        if(prop&&!activate(nb,bs,bbod,activation,normal_b,response.arm_b))return false;
        if(own_prop&&!activate(na,as,ba,own_activation,normal_a,response.arm_a))return false;
        if(fraction<1) {
            // The first solve still uses the pre-activation mass and tensor.
            if(!calculate_car_contact_response(initial_a,initial_b,geometry,response,fraction,material,
                pinned_upright,own_material,pinned_upright_b))return false;
        }
        if(context&&!prop&&!own_prop) {
            ContactCallbackContext callback{context->world_seed};
            callback.drag=context->drag;
            callback.has_physical_bounds=true;
            callback.physical_lateral_bounds={{{as.handling.wheels[0].lateral,as.handling.wheels[1].lateral},
                {bs.handling.wheels[0].lateral,bs.handling.wheels[1].lateral}}};
            const auto aa=axes(na),bb=axes(nb);
            for(unsigned k=0;k<3;++k){callback.body_axes[0][k]=narrow(aa[k]);callback.body_axes[1][k]=narrow(bb[k]);}
            // 469390 rewinds the body matrix, not actor+90.465D00 reads the
            // still-published actor up axis; continuation publishes it later.
            callback.actor_up=job?job->actor_up:std::array<ContactVector,2>{{narrow(aa[2]),narrow(bb[2])}};
            callback.grounded_wheels={na.published_support,nb.published_support};
            auto record=pending_record;
            const auto resolved=resolve_prepared_car_contact(ba,bbod,record,true,geometry,
                context->tick,response,&callback);
            if(resolved!=ContactResolution::applied)return false;
            pending_record=record;pending_seed=callback.world_seed;
        } else {
            if((!own_prop||own_activation==0)&&
               !apply_car_contact_impulse(ba,response.current_impulse,response.arm_a))return false;
            if((!prop || activation==0) &&
               !apply_car_contact_impulse(bbod,response.impulse_b,response.arm_b))return false;
        }
        const auto observe_prop=[&]() {
            const auto index=result.prop_impulse_count++;
            result.prop_impulses[index]=response.current_impulse;
            if(context&&!accumulate_contact_hit(pending_record,response.current_impulse,
                geometry.point,response.tangent_speed,context->tick))return false;
            result.prop_magnitudes[index]=context?pending_record.magnitude_sum:prop_speed(response.current_impulse);
            return true;
        };
        if((prop||own_prop)&&!observe_prop())return false;
        result.applied=true;result.impulse=response.current_impulse;
        if(context&&context->drag&&!prop&&!own_prop) {
            bool failed_a=false,failed_b=false;
            if((ba.kind==0&&!drag_contact_failure(response.current_impulse,true,context->physical_selectors[0],
                    bbod.kind,context->body_names[1],failed_a))||
               (bbod.kind==0&&!drag_contact_failure(response.current_impulse,true,context->physical_selectors[1],
                    ba.kind,context->body_names[0],failed_b)))return false;
            if(failed_a){na.drag_totalled=true;na.engine.heat=1.5F;}
            if(failed_b){nb.drag_totalled=true;nb.engine.heat=1.5F;}
        }
        if(repeat) {
            // 46C601 repeats from 46BB94 with the cached velocities; the
            // preceding impulse writes changed momentum and the prop mass.
            if(!calculate_car_contact_response(ba,bbod,geometry,response,1.F,material,
                pinned_upright&&activation>0,own_material,pinned_upright_b&&own_activation>0))return false;
            if(response.closing) {
                if((!prop||activation==0)&&!apply_car_contact_impulse(bbod,response.impulse_b,response.arm_b))return false;
                if((!own_prop||own_activation==0)&&!apply_car_contact_impulse(ba,response.current_impulse,response.arm_a))return false;
                if(!observe_prop())return false;
                for(unsigned k=0;k<3;++k)result.impulse[k]+=response.current_impulse[k];
            }
        }
    }
    if(!prop&&!own_prop&&!result.applied&&!job&&!context)return true;
    if(context&&!context->record.active&&pending_record.active) {
        //465640 allocation increments both body+1C counts before continuation;
        //42A530 consumes those counts before the resumed physics invocation.
        if((!fixed_world[0]&&!own_prop&&na.collision_body_flag==127)||(!fixed_world[1]&&!prop&&nb.collision_body_flag==127))return false;
        if(!fixed_world[0]&&!own_prop)++na.collision_body_flag;if(!fixed_world[1]&&!prop)++nb.collision_body_flag;
    }
    // Normal-mode body callback is enabled by an explicit world record/seed.
    // Standalone legacy probes and Smokeable activation keep their own boundary.
    const double invsum=double(ba.inverse_mass)+bbod.inverse_mass;
    if((!fixed_world[0]&&!publish(na,as,ba,mul(normal,prop&&!job?-depth*ba.inverse_mass/invsum:0))) ||
       (!fixed_world[1]&&!publish(nb,bs,bbod,mul(normal,prop&&!job?depth*bbod.inverse_mass/invsum:0))))return false;
    //46B755..46B762 refresh BOTH components before either outer continuation.
    if(context){context->record=pending_record;context->world_seed=pending_seed;}
    if(job&&context&&context->refresh) {
        if(!context->refresh(context->refresh_owner,0,na)||
           !context->refresh(context->refresh_owner,1,nb))return false;
    }
    if(!fixed_world[0]&&(!own_prop||own_activation==0))sa=std::move(na);
    if(!fixed_world[1]&&(!prop||activation==0))sb=std::move(nb);
    if(prop)prop->activation=activation;
    if(own_prop)own_prop->activation=own_activation;
    if(context){context->record=pending_record;context->world_seed=pending_seed;}
    return true;
}

PlayerRigidMatrix race_contact_axes(const VehicleState& s) {
    const auto a=axes(s);return {{narrow(a[0]),narrow(a[1]),narrow(a[2])}};
}
bool apply_race_track_contact(VehicleState& s,const VehicleSpec& spec,
    const ContactGeometry& geometry,float friction,ContactResponse& response) {
    auto next=s;
    if(!next.rigid_body_ready)vehicle_import_rigid(spec.handling,next);
    auto b=body(next,spec,1);
    const auto a=race_contact_axes(next);
    if(!calculate_track_contact_response(b,geometry,a[0],a[2],friction,response))return false;
    if(!response.closing)return true;
    if(!apply_car_contact_impulse(b,response.current_impulse,response.arm_a) ||
       !publish(next,spec,b,{}))return false;
    s=std::move(next);return true;
}
RaceContactPose race_contact_pose(const VehicleState& s) {
    return {s.position,s.orientation,s.velocity,s.simulation_seconds,race_contact_axes(s)};
}

RaceTrackBodyBounds race_track_body_bounds(const VehicleSpec& spec,const RaceContactBox& box) {
    RaceTrackBodyBounds out;ContactVector low{},high{};
    const auto& com=spec.handling.centre_of_mass;
    const unsigned count=box.points.empty()?8:unsigned(box.points.size());
    for(unsigned i=0;i<count;++i) {
        ContactVector p=box.points.empty()?ContactVector{{float(i&1?box.high.x:box.low.x),
            float(i&2?box.high.y:box.low.y),float(i&4?box.high.z:box.low.z)}}:box.points[i];
        for(unsigned k=0;k<3;++k){p[k]-=com[k];low[k]=std::min(low[k],p[k]);high[k]=std::max(high[k],p[k]);}
        const float radius=float(vehicle_sqrt(p[0]*p[0]+p[1]*p[1]));
        out.radius=std::max(out.radius,radius);
    }
    out.radius+=.05F;out.below=.05F-low[2];out.above=.05F+high[2];
    //4733A0 shape extrema, unlike468A50's zero-initialized cylinder bounds.
    const auto lo=narrow(box.low),hi=narrow(box.high);
    for(unsigned k=0;k<3;++k)out.centre[k]=((lo[k]-com[k])+(hi[k]-com[k]))*.5F;
    return out;
}
ContactVector race_track_side_reference(const RaceTrackBodyBounds& bounds,const RaceContactPose& pose) {
    ContactVector out;const auto p=narrow(pose.position);
    for(unsigned k=0;k<3;++k)
        out[k]=((bounds.centre[2]*pose.axes[2][k]+bounds.centre[1]*pose.axes[1][k])+
            bounds.centre[0]*pose.axes[0][k])+p[k];
    return out;
}
ContactVector race_track_polygon_normal(const std::array<ContactVector,4>& p) {
    ContactVector a,b,n;
    for(unsigned k=0;k<3;++k){a[k]=p[1][k]-p[0][k];b[k]=p[2][k]-p[0][k];}
    n={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    const float length=float(vehicle_sqrt((n[0]*n[0]+n[1]*n[1])+n[2]*n[2]));
    if(length==0)return {1,0,0}; //567370 zero-vector result.
    const float inverse=1.F/length;for(auto& v:n)v*=inverse;
    return n;
}
bool race_track_plane_admitted(const VehicleState& s,const RaceTrackBodyBounds& bounds,
    const std::array<ContactVector,4>& corners,const ContactVector& normal) {
    const auto basis=race_contact_axes(s);const auto p=narrow(s.position);
    float cosine=(basis[2][2]*normal[2]+basis[2][1]*normal[1])+basis[2][0]*normal[0];
    if(cosine>1)cosine=1;
    const float radial=float(vehicle_sqrt(1.F-cosine*cosine));
    const float plane=(corners[0][2]*normal[2]+corners[0][1]*normal[1])+corners[0][0]*normal[0];
    const float distance=((p[2]*normal[2]+p[1]*normal[1])+p[0]*normal[0])-plane;
    const float vertical=cosine*distance<0?bounds.above:bounds.below;
    const float extent=std::abs(cosine)*vertical+radial*bounds.radius;
    //469AB6 returns one only when abs(distance) is strictly below extent.
    return std::abs(distance)<extent;
}
bool update_race_track_polygon_cache(const TrackContactWorld& world,const RaceContactPose& pose,
    RaceTrackPolygonCache& cache,float supplied_radius) {
    if(!world.valid)return false;
    const auto p=narrow(pose.position),v=narrow(pose.velocity);
    //42681B..426855: (Vz*Vz+Vx*Vx)+Vy*Vy under007F, then .05*speed+3.
    const float speed=float(vehicle_sqrt((v[2]*v[2]+v[0]*v[0])+v[1]*v[1]));
    const float radius=supplied_radius>=0?supplied_radius:speed*.05F+3.F;
    const std::array<float,4> current{{p[0]-radius,p[1]-radius,p[0]+radius,p[1]+radius}};
    if(cache.valid&&current[0]>=cache.bounds[0]&&current[1]>=cache.bounds[1]&&
        current[2]<=cache.bounds[2]&&current[3]<=cache.bounds[3])return true;
    RaceTrackPolygonCache next;const float expanded=radius+.5F;
    float ahead=.15F;
    for(unsigned attempt=0;attempt<5;++attempt) {
        next.bounds={p[0]-expanded,p[1]-expanded,p[0]+expanded,p[1]+expanded};
        for(unsigned k=0;k<2;++k) {
            const float projected=ahead*v[k]+p[k];
            next.bounds[k]=std::min(next.bounds[k],projected-expanded);
            next.bounds[k+2]=std::max(next.bounds[k+2],projected+expanded);
        }
        const float dx=next.bounds[0]-next.bounds[2],dy=next.bounds[1]-next.bounds[3];
        if(float(vehicle_sqrt(dx*dx+dy*dy))<=28.799999237060547F)break;
        ahead*=.75F;if(attempt==3)ahead=0;
    }
    std::vector<std::size_t> cells;
    if(!query_track_collision_cells(world.collision,next.bounds[0],next.bounds[1],
        next.bounds[2],next.bounds[3],cells))return false;
    //441C10 scans rows from low Y and cells from low X. Quadtree traversal
    //order differs; cap160 and first-occurrence dedup precede grouping flags.
    std::stable_sort(cells.begin(),cells.end(),[&](std::size_t a,std::size_t b) {
        const auto& x=world.collision.cells[a];const auto& y=world.collision.cells[b];
        return x.center[1]!=y.center[1]?x.center[1]<y.center[1]:x.center[0]<y.center[0];
    });
    std::vector<std::size_t> admitted;
    for(auto cell_id:cells) {
        const auto& cell=world.collision.cells[cell_id];
        if(std::uint64_t(cell.first_index)+cell.index_count>world.collision.indices.size())return false;
        for(unsigned j=0;j<cell.index_count&&admitted.size()<160;++j) {
            const auto id=world.collision.indices[cell.first_index+j];
            if(id>=world.collision.polygons.size())return false;
            if(std::find(admitted.begin(),admitted.end(),id)!=admitted.end())continue;
            const auto& poly=world.collision.polygons[id];
            float low[2]={poly.corner[0][0],poly.corner[0][1]},high[2]={low[0],low[1]};
            for(unsigned i=1;i<4;++i)for(unsigned k=0;k<2;++k) {
                low[k]=std::min(low[k],poly.corner[i][k]);high[k]=std::max(high[k],poly.corner[i][k]);
            }
            if(low[0]>next.bounds[2]||low[1]>next.bounds[3]||
                high[0]<next.bounds[0]||high[1]<next.bounds[1])continue;
            admitted.push_back(id);
        }
    }
    for(unsigned pass=0;pass<3;++pass)for(auto id:admitted) {
        const auto flags=world.collision.polygons[id].unknown0[3];
        if((pass==0&&!(flags&10))||(pass==1&&(flags&2))||(pass==2&&(flags&8)))
            next.polygons.push_back(id);
    }
    // Valid retail flags put each cached polygon in exactly one category.
    if(next.polygons.size()!=admitted.size())return false;
    next.valid=true;cache=std::move(next);return true;
}

bool race_track_candidates(const TrackContactWorld& world,const VehicleState& s,
    const RaceTrackBodyBounds& bounds,const RaceTrackPolygonCache& cache,
    const std::unordered_map<std::size_t,std::size_t>& registered,std::vector<std::size_t>& out) {
    out.clear();if(!world.valid||!cache.valid)return false;
    const float x=float(s.position.x),y=float(s.position.y),r=bounds.radius;
    for(auto index:cache.polygons) {
        if(registered.find(index)!=registered.end())continue;
        if(index>=world.collision.polygons.size())return false;
        const auto& p=world.collision.polygons[index];
        std::array<ContactVector,4> corners;
        for(unsigned i=0;i<4;++i)std::copy_n(p.corner[i],3,corners[i].begin());
        // Same horizontal extrema as43FF30's retail selectors; derive also
        // for synthetic collision reports which have no encoded selector byte.
        float low_x=corners[0][0],low_y=corners[0][1],high_x=low_x,high_y=low_y;
        for(unsigned k=1;k<4;++k) {
            low_x=std::min(low_x,corners[k][0]);high_x=std::max(high_x,corners[k][0]);
            low_y=std::min(low_y,corners[k][1]);high_y=std::max(high_y,corners[k][1]);
        }
        if(x-r>high_x||x+r<low_x||y-r>high_y||y+r<low_y)continue;
        const auto normal=race_track_polygon_normal(corners);
        if(race_track_plane_admitted(s,bounds,corners,normal)&&out.size()<48)out.push_back(index);
    }
    //46989E caps new admissions BEFORE sorting raw polygon pointers.
    std::sort(out.begin(),out.end());
    return true;
}

bool sample_race_contact(VehicleState& out,const VehicleSpec& spec,
    const RaceContactPose& start,const VehicleState& end,float remaining) {
    const float duration=float(end.simulation_seconds-start.time);
    if(!std::isfinite(duration)||!std::isfinite(remaining)||duration<0||remaining<0||remaining>duration)return false;
    out=end;
    if(!out.rigid_body_ready)vehicle_import_rigid(spec.handling,out);
    // 468FE0 keeps the current sample for duration <= float1e-6.
    if(duration<=.000001F)return true;
    //468FE0 runs under007F: the ratio and every interpolation operation
    //round to binary32, even when no explicit fstp separates the x87 ops.
    const float t=(duration-remaining)/duration;
    const auto old=narrow(start.position),now=narrow(end.position);
    ContactVector position{};
    for(unsigned i=0;i<3;++i)position[i]=(now[i]-old[i])*t+old[i];
    out.position=wide(position);out.rigid_body.position=position;
    const auto basis=race_contact_axes(end);
    for(unsigned i=0;i<3;++i)for(unsigned k=0;k<3;++k) {
        const float delta=basis[i][k]-start.axes[i][k];
        const float scaled=delta*t;
        out.rigid_body.axes[i][k]=scaled+start.axes[i][k];
    }
    out.rigid_orientation=out.orientation;
    const auto previous=narrow(start.velocity),current=narrow(end.velocity);
    const float velocity_t=float(t);
    auto sampled=body(out,spec,1);
    for(unsigned k=0;k<3;++k) {
        const float velocity=(current[k]-previous[k])*velocity_t+previous[k];
        sampled.linear_momentum[k]=velocity*spec.handling.mass;
    }
    return publish(out,spec,sampled,{});
}

bool search_race_contact(const VehicleState& sa,const VehicleSpec& as,const RaceContactBox& ab,
    const VehicleState& sb,const VehicleSpec& bs,const RaceContactBox& bb,
    const RaceContactPose& pa,const RaceContactPose& pb,RaceContactJob& result,RaceHullCache* persistent_cache,
    RaceContactPartner partner,RaceContactPartner own) {
    result={};if(&sa==&sb)return false;
    double depth=0;VehicleVec3 normal{};
    const int overlap=overlap_boxes(placed(sa,as,ab),placed(sb,bs,bb),depth,normal,vehicle_sqrt(double(.001F)));
    if(overlap<0)return false;
    if(!overlap)return true;
    RaceHullCache local_cache;
    auto& cache=persistent_cache?*persistent_cache:local_cache;
    RaceHullQuery query;
    if(!query_race_hulls(sa,as,ab,sb,bs,bb,query,&cache))return false;
    // 468060 enqueues only flag15, not the wider flag14 interval.
    if(query.distance>double(.0001F))return true;
    const float duration_a=float(sa.simulation_seconds-pa.time),duration_b=float(sb.simulation_seconds-pb.time);
    if(!std::isfinite(duration_a)||!std::isfinite(duration_b)||duration_a<0||duration_b<0)return false;
    float upper=std::min(duration_a,duration_b),lower=0,remaining=0;
    VehicleState a,b;unsigned count=0;
    // 46AE60 searches BACK from the integrated endpoint. It returns its last
    // midpoint, including after exhaustion; there is no initial-overlap bypass.
    for(;count<8;) {
        remaining=float((double(upper)-lower)*.5+lower);
        if(!sample_race_contact(a,as,pa,sa,remaining)||!sample_race_contact(b,bs,pb,sb,remaining))return false;
        if(!query_race_hulls(a,as,ab,b,bs,bb,query,&cache))return false;
        ++count;
        if(query.distance<=double(.0001F))lower=remaining;
        else if(query.distance<=double(.005F))break;
        else upper=remaining;
    }
    VehicleVec3 correction{};
    if(query.distance<=double(.0001F)) {
        auto direction=separation_direction(a.position,b.position);
        // 46B260 reads the other body's velocity when either body is a Smokeable.
        const bool a_prop=own==RaceContactPartner::prop||own==RaceContactPartner::pinned_prop;
        const bool b_prop=partner==RaceContactPartner::prop||partner==RaceContactPartner::pinned_prop;
        if(a_prop||b_prop) {
            const auto v=narrow(a_prop?mul(sb.velocity,-1):sa.velocity);
            const auto speed=vehicle_sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
            if(speed>=27.77777862548828F)direction=separation_direction({},wide(v));
        }
        const auto origin_a=a.position,origin_b=b.position;
        float distance=0,increment=.005F;
        for(unsigned attempt=0;attempt<20;++attempt) {
            distance+=increment;if(increment<.1F)increment+=.01F;
            auto move=wide(narrow(mul(direction,distance)));move.z=std::clamp(move.z,-double(.15F),double(.15F));
            if(own!=RaceContactPartner::pinned_prop&&own!=RaceContactPartner::fixed_world)a.position=wide(narrow(sub(origin_a,move)));
            if(partner!=RaceContactPartner::pinned_prop&&partner!=RaceContactPartner::fixed_world)b.position=wide(narrow(add(origin_b,move)));
            if(!query_race_hulls(a,as,ab,b,bs,bb,query,&cache))return false;
            if(query.distance>double(.0001F)){correction=move;break;}
        }
    }
    result={true,remaining,count,query,correction,{{narrow(axes(sa)[2]),narrow(axes(sb)[2])}}};return true;
}

bool search_race_track_contact(const VehicleState& end,const VehicleSpec& spec,const RaceContactBox& box,
    const RaceContactPose& history,const std::array<ContactVector,4>& corners,
    const ContactVector& normal,const ContactVector& side_reference,
    RaceTrackContactJob& result,RaceHullCache* persistent_cache,bool initial_expanded,bool prop_body,bool fixed) {
    result={};RaceHullCache local_cache;
    auto& cache=persistent_cache?*persistent_cache:local_cache;
    RaceHullQuery query;
    if(!query_race_track_polygon(end,spec,box,corners,normal,initial_expanded,query,&cache))return false;
    if(query.distance>double(.0001F))return true;
    const float duration=float(end.simulation_seconds-history.time);
    if(!std::isfinite(duration)||duration<0)return false;
    bool triangle=true;for(unsigned k=0;k<3;++k)
        if(std::abs(corners[2][k]-corners[3][k])>.001F)triangle=false;
    const unsigned vertices=triangle?3:4;
    const auto facing=[&](bool expanded) {
        ContactVector centre{};
        for(unsigned i=0;i<vertices;++i)for(unsigned k=0;k<3;++k)centre[k]+=corners[i][k];
        if(expanded)for(unsigned i=0;i<vertices;++i)for(unsigned k=0;k<3;++k)
            centre[k]+=corners[i][k]-normal[k];
        const float inverse=1.F/float(vertices*(expanded?2:1));
        ContactVector delta;
        for(unsigned k=0;k<3;++k)delta[k]=side_reference[k]-centre[k]*inverse;
        const float side=(normal[2]*delta[2]+normal[1]*delta[1])+normal[0]*delta[0];
        ContactVector direction=normal;
        // B is static:46B2DE and46B96B negate the polygon normal facing A.
        if(!(side<0))for(float& x:direction)x=-x;
        return direction;
    };
    VehicleState sampled;float upper=duration,lower=0,remaining=0;unsigned count=0;
    for(;count<8;) {
        remaining=float((double(upper)-lower)*.5+lower);
        if(!sample_race_contact(sampled,spec,history,end,remaining)||
           !query_race_track_polygon(sampled,spec,box,corners,normal,prop_body,query,&cache))return false;
        ++count;
        if(query.distance<=double(.0001F))lower=remaining;
        else if(query.distance<=double(.005F))break;
        else upper=remaining;
    }
    VehicleVec3 correction{};
    if(query.distance<=double(.0001F)&&!fixed) {
        //467CA0 only enables the expanded polygon for kind1 objects, not
        //kind0 cars. Car history search and separation keep the thin face.
        const auto direction=facing(prop_body);const auto origin=narrow(sampled.position);
        float distance=0,increment=.005F;
        for(unsigned attempt=0;attempt<20;++attempt) {
            distance+=increment;if(increment<.1F)increment+=.01F;
            ContactVector move;for(unsigned k=0;k<3;++k)move[k]=direction[k]*distance;
            move[2]=std::clamp(move[2],-.15F,.15F);
            sampled.position={origin[0]-move[0],origin[1]-move[1],origin[2]-move[2]};
            if(!query_race_track_polygon(sampled,spec,box,corners,normal,prop_body,query,&cache))return false;
            if(query.distance>double(.0001F)){correction=wide(move);break;}
        }
    }
    ContactGeometry geometry;geometry.normal=facing(prop_body);
    for(unsigned k=0;k<3;++k)geometry.point[k]=(query.witness_a[k]+query.witness_b[k])*.5F;
    result={true,remaining,count,query,geometry,correction};return true;
}

bool resolve_race_pair(VehicleState& sa,const VehicleSpec& as,const RaceContactBox& ab,
    VehicleState& sb,const VehicleSpec& bs,const RaceContactBox& bb,RacePairContact& result,
    RacePropImpact* prop,RaceContactContext* context) {
    return resolve_pair_impl(sa,as,ab,sb,bs,bb,result,prop,context,nullptr);
}
bool resolve_swept_race_pair(VehicleState& sa,const VehicleSpec& as,const RaceContactBox& ab,
    VehicleState& sb,const VehicleSpec& bs,const RaceContactBox& bb,RacePairContact& result,
    const RaceContactPose& pa,const RaceContactPose& pb,RaceContactContext* context,const RaceContactJob* prepared,std::array<bool,2> fixed_world) {
    result={};RaceContactJob job;
    if(prepared)job=*prepared;
    else if(!search_race_contact(sa,as,ab,sb,bs,bb,pa,pb,job,nullptr,
        fixed_world[1]?RaceContactPartner::fixed_world:RaceContactPartner::car,
        fixed_world[0]?RaceContactPartner::fixed_world:RaceContactPartner::car))return false;
    if(!job.admitted)return true;
    VehicleState a,b;
    if(!sample_race_contact(a,as,pa,sa,job.remaining)||!sample_race_contact(b,bs,pb,sb,job.remaining))return false;
    if(!resolve_pair_impl(a,as,ab,b,bs,bb,result,nullptr,context,&job,nullptr,fixed_world))return false;
    result.searched=true;
    const float duration=float(sa.simulation_seconds-pa.time);
    result.contact_fraction=duration>.000001F?(double(duration)-job.remaining)/duration:1;
    result.remaining_a=result.remaining_b=job.remaining;
    result.resume_a=race_contact_pose(a);result.resume_b=race_contact_pose(b);
    result.resume_a.time-=job.remaining;result.resume_b.time-=job.remaining;
    // 46B5E0 samples and resumes both bodies even if46BAE0 finds no closing
    // velocity. The next job must use the new snapshot, not a zero span.
    sa=std::move(a);sb=std::move(b);
    return true;
}
bool resolve_swept_race_prop(VehicleState& sa,const VehicleSpec& as,const RaceContactBox& ab,
    VehicleState& sb,const VehicleSpec& bs,const RaceContactBox& bb,RacePropImpact& prop,
    const RaceContactPose& pa,const RaceContactPose& pb,const RaceContactJob& job,
    RaceContactContext& context,RacePairContact& result) {
    result={};if(!job.admitted)return true;
    VehicleState a,b;
    if(!sample_race_contact(a,as,pa,sa,job.remaining)||!sample_race_contact(b,bs,pb,sb,job.remaining))return false;
    if(!resolve_pair_impl(a,as,ab,b,bs,bb,result,&prop,&context,&job))return false;
    result.searched=true;result.remaining_a=result.remaining_b=job.remaining;
    result.resume_a=race_contact_pose(a);result.resume_b=race_contact_pose(b);
    result.resume_a.time-=job.remaining;result.resume_b.time-=job.remaining;
    sa=std::move(a);sb=std::move(b);
    return true;
}
bool resolve_swept_race_rigid_pair(VehicleState& sa,const VehicleSpec& as,const RaceContactBox& ab,RacePropImpact* ap,
    VehicleState& sb,const VehicleSpec& bs,const RaceContactBox& bb,RacePropImpact* bp,
    const RaceContactPose& pa,const RaceContactPose& pb,const RaceContactJob& job,
    RaceContactContext& context,RacePairContact& result,std::array<bool,2> fixed_world) {
    result={};if(!job.admitted)return true;
    VehicleState a,b;
    if(!sample_race_contact(a,as,pa,sa,job.remaining)||!sample_race_contact(b,bs,pb,sb,job.remaining))return false;
    if(!resolve_pair_impl(a,as,ab,b,bs,bb,result,bp,&context,&job,ap,fixed_world))return false;
    result.searched=true;result.remaining_a=result.remaining_b=job.remaining;
    result.resume_a=race_contact_pose(a);result.resume_b=race_contact_pose(b);
    result.resume_a.time-=job.remaining;result.resume_b.time-=job.remaining;
    sa=std::move(a);sb=std::move(b);return true;
}
static bool resolve_swept_track_impl(VehicleState& state,const VehicleSpec& spec,
    const RaceContactPose& history,const RaceTrackContactJob& job,float friction,
    RaceContactContext& context,RacePairContact& result,const RacePropImpact* prop) {
    result={};if(!job.admitted)return true;
    VehicleState next;
    if(!sample_race_contact(next,spec,history,state,job.remaining))return false;
    const auto before=next;
    next.position=wide(narrow(sub(next.position,job.correction)));
    next.rigid_body.position=narrow(next.position);
    if(context.refresh&&!context.refresh(context.refresh_owner,0,next))return false;
    auto record=context.record;
    if(!record.active&&!initialize_car_contact_record(record,job.geometry.point,context.tick))return false;
    ContactResponse response;
    if(prop) {
        auto moving=body(next,spec,1);moving.car_scaling=false;
        if(prop->activation>0){moving.inverse_mass=0;moving.response_inverse_inertia={};}
        ContactBody fixed;fixed.identity=2;fixed.car_scaling=false;
        if(!calculate_car_contact_response(moving,fixed,job.geometry,response,1.F,{.1F,friction},false,
            prop->material))return false;
        if(response.closing&&(!apply_car_contact_impulse(moving,response.current_impulse,response.arm_a)||
            !publish(next,spec,moving,{})))return false;
    } else if(!apply_race_track_contact(next,spec,job.geometry,friction,response))return false;
    if(response.closing&&!accumulate_contact_hit(record,response.current_impulse,
        job.geometry.point,response.tangent_speed,context.tick))return false;
    if(response.closing&&context.drag&&!prop) {
        bool failed=false;
        if(!drag_contact_failure(response.current_impulse,true,context.physical_selectors[0],3,0,failed))return false;
        if(failed){next.drag_totalled=true;next.engine.heat=1.5F;}
    }
    //465D00/4663D0 excludes kind3 from the hard-hit car/prop state callback.
    if(!context.record.active&&!prop) {
        if(next.collision_body_flag==127)return false;
        ++next.collision_body_flag;
    }
    result.overlapping=true;result.searched=true;result.applied=response.closing;
    result.remaining_a=job.remaining;result.impulse=response.current_impulse;
    result.point=wide(job.geometry.point);result.position_a=before.position;
    result.velocity_a=before.velocity;
    result.resume_a=race_contact_pose(next);result.resume_a.time-=job.remaining;
    context.record=record;
    if(context.refresh&&!context.refresh(context.refresh_owner,0,next))return false;
    state=std::move(next);return true;
}
bool resolve_swept_race_track_contact(VehicleState& state,const VehicleSpec& spec,
    const RaceContactPose& history,const RaceTrackContactJob& job,float friction,
    RaceContactContext& context,RacePairContact& result) {
    return resolve_swept_track_impl(state,spec,history,job,friction,context,result,nullptr);
}
bool resolve_swept_race_prop_track(VehicleState& state,const VehicleSpec& spec,const RacePropImpact& prop,
    const RaceContactPose& history,const RaceTrackContactJob& job,float friction,
    RaceContactContext& context,RacePairContact& result) {
    return resolve_swept_track_impl(state,spec,history,job,friction,context,result,&prop);
}
}
