#include "nfsu/race_props.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/prop_settling.h"
#include "nfsu/car_collision_hull.h"
#include "nfsu/track_ground.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <limits>
namespace nfsu {
bool VehicleTrackSession::meet_prop(RaceProp& prop,const RaceContactBox& box,const RaceContactPose& history,
    const RaceContactJob& job,RaceContactContext& context,RacePairContact& result) {
    if(!started_)return false;
    const auto saved=state_,saved_prop=prop.state;
    const auto saved_ai=ai_;const bool saved_started=ai_started_;
    const auto impact=prop.impact;const auto record=context.record;
    if(resolve_swept_race_prop(state_,model_->spec(),box,prop.state,prop.spec,prop.box,prop.impact,
        history,prop.history,job,context,result)&&
       (!result.searched||continue_contact(result.remaining_a,&saved,true)))return true;
    state_=saved;ai_=saved_ai;ai_started_=saved_started;
    prop.state=saved_prop;prop.impact=impact;context.record=record;result={};return false;
}

namespace {
using V=ContactVector;
V add(V a,V b){for(unsigned k=0;k<3;++k)a[k]+=b[k];return a;}
V sub(V a,V b){for(unsigned k=0;k<3;++k)a[k]-=b[k];return a;}
V mul(V a,float b){for(auto& x:a)x*=b;return a;}
float dot(V a,V b){return (a[0]*b[0]+a[1]*b[1])+a[2]*b[2];}
V cross(V a,V b){return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};}
V rotated(const PlayerRigidBodyState& b,V v){V out{};for(unsigned k=0;k<3;++k)for(unsigned j=0;j<3;++j)out[k]+=b.axes[j][k]*v[j];return out;}
void publish(VehicleState& s) {
    const auto& a=s.rigid_body.axes;VehicleQuat q;const double tr=a[0][0]+a[1][1]+a[2][2];
    if(tr>0){const double v=2*std::sqrt(1+tr);q={v/4,(a[1][2]-a[2][1])/v,(a[2][0]-a[0][2])/v,(a[0][1]-a[1][0])/v};}
    else {unsigned i=a[1][1]>a[0][0]?1:0;if(a[2][2]>a[i][i])i=2;const unsigned j=(i+1)%3,k=(i+2)%3;
        const double v=2*std::sqrt(1+a[i][i]-a[j][j]-a[k][k]);double x[3]{};x[i]=v/4;x[j]=(a[j][i]+a[i][j])/v;x[k]=(a[k][i]+a[i][k])/v;
        q={(a[j][k]-a[k][j])/v,x[0],x[1],x[2]};}
    const double n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);q={q.w/n,q.x/n,q.y/n,q.z/n};
    s.orientation=s.rigid_orientation=q;s.rigid_body_ready=true;
    const auto& b=s.rigid_body;s.position={b.position[0],b.position[1],b.position[2]};
    s.velocity={b.velocity[0],b.velocity[1],b.velocity[2]};s.angular_velocity={b.angular_velocity[0],b.angular_velocity[1],b.angular_velocity[2]};
}
}
std::array<float,3> race_prop_visual_position(const RaceProp& p) noexcept {
    auto position=sub(p.state.rigid_body.position,rotated(p.state.rigid_body,p.spec.handling.centre_of_mass));
    position[2]-=p.render_support_lift;return position;
}
void RaceProps::convert_proxy(RaceProp& p,const TrackContactWorld& world) {
    if(!p.placement_ready){p.placement_pose=race_contact_pose(p.state);p.placement_ready=true;}
    // The native conversion owns a fresh440740 cache, not a wheel's last hit.
    TrackGroundCache local_cache;TrackContact contact;
    auto& cache=p.world_slot<maintenance_ground_.size()?maintenance_ground_[p.world_slot]:local_cache;
    cache={};
    const auto origin=race_prop_visual_position(p);
    if(query_track_ground(world,{origin[0],origin[1],origin[2]},cache,contact)) {
        float minimum=0;const auto centre=p.spec.handling.centre_of_mass[2];
        for(const auto& point:p.points)minimum=std::min(minimum,point[2]-centre);
        const auto lift=prop_conversion_lift(contact.point[2],minimum,centre,p.state.rigid_body.axes[2][2],origin[2]);
        p.support_lift=p.render_support_lift=lift;
        p.state.rigid_body.position[2]+=lift;publish(p.state);
    }
}
bool RaceProps::load(const std::filesystem::path& root,std::string& error,const std::string& track_name) {
    if(!objects_.empty()){error="props already loaded";return false;}
    std::vector<unsigned char> image;const auto parsed=load_race_route_track(root/"Tracks"/track_name,image);
    if(!parsed.ok()){error="prop track image unavailable";return false;}
    const auto u=[&](std::size_t p){std::uint32_t v;std::memcpy(&v,image.data()+p,4);return v;};
    const auto f=[&](std::size_t p){float v;std::memcpy(&v,image.data()+p,4);return v;};
    std::map<std::uint32_t,std::size_t> definitions;
    std::map<std::uint32_t,VehicleHull> hulls;std::vector<std::size_t> placements;
    bool valid=true;
    const auto start=[&](std::size_t p,std::size_t end,unsigned stride){const auto a=(p+23)&~std::size_t(15);
        if(a>end||(end-a)%stride)valid=false;return a;};
    std::function<void(std::size_t,std::size_t,unsigned,std::size_t)> walk;
    walk=[&](std::size_t begin,std::size_t end,unsigned depth,std::size_t hull_begin){
        if(depth>16){valid=false;return;}
        std::uint32_t shape=0;
        for(auto p=begin;p+8<=end;) {
            const auto id=u(p);const auto finish=p+8+u(p+4);if(finish>end){valid=false;return;}
            if(id==0x34026)for(auto a=start(p,finish,256);valid&&a<finish;a+=256)definitions.emplace(u(a+128),a);
            else if(id==0x34027)for(auto a=start(p,finish,112);valid&&a<finish;a+=112)placements.push_back(a);
            else if(id==0x39200){const auto a=start(p,finish,1);if(finish-a>=0x58)shape=u(a+24);}
            else if(id==0x39201&&shape) {
                VehicleHull hull;
                if(hull_begin==image.size()||!decode_car_collision_hull(image.data(),
                    hull_begin+8+u(hull_begin+4),shape,hull,hull_begin)){valid=false;return;}
                hulls.emplace(shape,std::move(hull));
            }
            else if(id&0x80000000U)walk(p+8,finish,depth+1,id==0x80034020?p:hull_begin);
            p=finish;
        }
    };walk(0,image.size(),0,image.size());
    if(!valid){error="invalid prop arrays";return false;}
    std::vector<RaceProp> next;next.reserve(placements.size());
    for(std::size_t index=0;index<placements.size();++index) {
        const auto at=placements[index];
        const auto key=u(at);const auto d=definitions.find(key);const auto h=hulls.find(key);
        // 595D80..595DD9 skips placements without a loaded definition.
        // TRACKB1210 contains such unused placements; they are not bodies.
        if(d==definitions.end())continue;
        if(h==hulls.end()||h->second.points.empty()){error="prop shape absent";return false;}
        const auto def=d->second;RaceProp p;p.section=u(at+4)&65535;p.instance=u(at+4)>>16;p.model=u(at+8);
        // 596334 assigns a negative ID from the unfiltered placement index.
        p.proxy_id=p.body_id=-1-static_cast<int>(index);
        p.placement_bounds={f(at+16),f(at+20),f(at+32),f(at+36)};
        if(!std::all_of(p.placement_bounds.begin(),p.placement_bounds.end(),[](float v){return std::isfinite(v);})||
           p.placement_bounds[0]>p.placement_bounds[2]||p.placement_bounds[1]>p.placement_bounds[3]) {
            error="invalid prop placement bounds";return false;
        }
        for(unsigned j=8;j<32&&image[def+j];++j)p.name+=char(image[def+j]);
        auto& setup=p.spec.handling;setup.mass=f(def+32);setup.alternate_yaw_scale=1;
        for(unsigned k=0;k<3;++k){setup.inertia[k]=f(def+48+20*k);setup.centre_of_mass[k]=f(def+176+4*k);}
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
            if(row!=col&&f(def+48+16*row+4*col)!=0){error="non-diagonal prop inertia unsupported";return false;}
        if(!(setup.mass>0)||!std::isfinite(setup.mass)||u(def+140)!=0){error="unsupported prop mass or child graph";return false;}
        for(auto v:setup.inertia)if(!(v>0)||!std::isfinite(v)){error="invalid prop inertia";return false;}
        p.sound_material=u(def+216);p.impact={f(def+196),{f(def+204),f(def+224)}};
        if(!std::isfinite(p.impact.material.restitution)||p.impact.material.restitution<0||
           !std::isfinite(p.impact.material.friction)||p.impact.material.friction<0) {
            error="invalid prop contact material";return false;
        }
        p.lifetime=f(def+192);p.points=h->second.points;p.box=race_contact_box(h->second);
        p.reclamation_exempt=u(def+228)!=0;
        for(const auto& point:p.points) {
            V delta=sub(point,setup.centre_of_mass);
            p.sphere_radius=std::max(p.sphere_radius,prop_speed(delta));
        }
        p.bounds=race_track_body_bounds(p.spec,p.box);
        auto& b=p.state.rigid_body;
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)b.axes[row][col]=f(at+48+16*row+4*col);
        b.position=add({{f(at+96),f(at+100),f(at+104)}},rotated(b,setup.centre_of_mass));
        b.snapshot_axes=b.axes;p.state.alternate_yaw_active=false;
        player_rigid_body_refresh(setup,b);publish(p.state);next.push_back(std::move(p));
    }
    std::size_t vertices=0;
    for(auto& p:next){vertices=std::max(vertices,p.points.size());p.ground_contacts.reserve(p.points.size());}
    terrain_queries_.resize(vertices);terrain_hits_.resize(vertices);
    terrain_statuses_.resize(vertices);terrain_arms_.resize(vertices);
    proxy_order_.resize(next.size());widest_proxy_=0;
    for(std::size_t i=0;i<next.size();++i) {
        proxy_order_[i]=i;
        widest_proxy_=std::max(widest_proxy_,next[i].placement_bounds[2]-next[i].placement_bounds[0]);
    }
    std::sort(proxy_order_.begin(),proxy_order_.end(),[&](std::size_t a,std::size_t b){
        return next[a].placement_bounds[0]<next[b].placement_bounds[0];
    });
    objects_=std::move(next);error.clear();return true;
}
bool RaceProps::meet(VehicleState& car,const VehicleSpec& spec,const RaceContactBox& box,std::string& error,VehiclePropContactFrame* observed) {
    if(observed)observed->count=0;
    std::uint32_t audio_order=0;
    for(auto& p:objects_) {
        if(p.expired)continue;
        const auto& q=p.state.position;const double x=q.x-car.position.x,y=q.y-car.position.y,z=q.z-car.position.z;
        if(x*x+y*y+z*z>400)continue;
        ++candidates;RacePairContact result;
        if(!resolve_race_pair(car,spec,box,p.state,p.spec,p.box,result,&p.impact)){error="prop contact refused";return false;}
        observe_contact(p,result,observed,audio_order);
    }
    return true;
}
void RaceProps::observe_contact(RaceProp& p,const RacePairContact& result,
    VehiclePropContactFrame* observed,std::uint32_t& audio_order,bool car_contact) {
    // 5980D0 / 5974EE refresh the car contact clock even without a new impulse.
    if(result.overlapping&&car_contact)p.last_car_contact=clock_;
    for(unsigned i=0;i<result.prop_impulse_count;++i) {
        ++audio_order;
        if(observed&&observed->count<observed->hits.size()) {
            auto& hit=observed->hits[observed->count++];
            hit.id=static_cast<std::uint32_t>(&p-objects_.data());
            const auto& impulse=result.prop_impulses[i];
            hit.impulse={impulse[0],impulse[1],impulse[2]};
            hit.inverse_mass=p.impact.activation==0?1.F/p.spec.handling.mass:0.F;hit.material=p.sound_material;
            hit.audio_order=audio_order;hit.magnitude_sum=result.prop_magnitudes[i];
        }
    }
    if(result.applied&&p.impact.activation==0){
        //597C90 records first activation, not every later contact/reawakening.
        if(!p.awake&&(p.first_activation_tick==0||p.first_activation_tick==0x7fffffffU))
            p.first_activation_tick=clock_;
        p.moved=true;p.awake=true;p.quiet=0;++hits;
    }
}
void RaceProps::maintain_pool(const TrackContactWorld& world,const VehicleVec3& human,
    std::uint32_t tick,std::uint64_t frame) {
    unsigned live=0;for(const auto& p:objects_)live+=p.body_id>=0&&!p.expired;
    const unsigned free_slots=live<32?32-live:0;
    if(!prop_pool_maintenance_due(free_slots,frame))return;
    const bool pressure=free_slots<4;
    for(auto& p:objects_)if(p.body_id>=0&&!p.expired&&!p.reclamation_exempt) {
        bool retire=false;
        if(!pressure&&p.world_slot<maintenance_ground_.size()) {
            // Stock006EF284=1:595750 removes a body wholly below local road.
            TrackContact ground;
            if(query_track_ground(world,p.state.position,maintenance_ground_[p.world_slot],ground))
                retire=float(p.state.position.z)+p.sphere_radius<ground.point[2];
        }
        const V delta{{float(p.state.position.x)-float(human.x),
            float(p.state.position.y)-float(human.y),float(p.state.position.z)-float(human.z)}};
        //420B40 copies human actor+60 to world+280.421400 measures nearest
        //human; AI rivals and traffic do not keep distant debris alive.
        if(retire||prop_pool_reclaims(free_slots,p.awake,p.reclamation_exempt,
                prop_speed(delta),tick,p.first_activation_tick)) {
            p.expired=true;p.awake=false;
        }
    }
}
bool RaceProps::prepare_frame(const TrackContactWorld& world,std::string& error,float dt) {
    if(!std::isfinite(dt)||dt<=0||dt>.1F){error="invalid prop frame interval";return false;}
    // 585E6C: round the supplied update dt * 4000 to the nearest tick.
    // Our fixed 1/60 s update contributes 67 ticks, not a 66/67 average.
    if(!queued_contacts_)clock_+=static_cast<unsigned>(dt*4000.F+.5F);
    if(!world_bounds_ready_) {
        lowest_world_=std::numeric_limits<float>::max();
        for(const auto& polygon:world.collision.polygons)for(const auto& corner:polygon.corner)
            lowest_world_=std::min(lowest_world_,corner[2]);
        world_bounds_ready_=true;
    }
    for(auto& p:objects_) {
        if(p.expired||(!p.awake&&p.body_id<0))continue;
        p.history=race_contact_pose(p.state);
        p.state.simulation_seconds+=dt;
        if(!p.awake)continue;
        p.age+=dt;if(p.lifetime>0&&p.age>p.lifetime){p.expired=true;p.awake=false;continue;}
    }
    error.clear();return true;
}
bool RaceProps::advance(const TrackContactWorld& world,std::string& error,float dt) {
    if(!prepare_frame(world,error,dt))return false;
    for(auto& p:objects_)if(p.awake&&!p.expired&&!advance_body(p,world,error,dt))return false;
    return true;
}
bool RaceProps::continue_contact(RaceProp& p,const TrackContactWorld& world,std::string& error,double remaining) {
    return !p.awake||p.expired||remaining<=0||advance_body(p,world,error,float(remaining));
}
bool RaceProps::advance_body(RaceProp& p,const TrackContactWorld& world,std::string& error,float dt) {
    const unsigned slices=unsigned(dt/.01F)+1;const float slice_dt=dt/float(slices);
    const auto age=[&](std::uint32_t at){return prop_clock_age(clock_,at);};
    auto& b=p.state.rigid_body;auto& h=p.spec.handling;
    // Two equal substeps satisfy the recovered Smokeable .01s maximum.
    for(unsigned slice=0;slice<slices;++slice) {
        const auto before=b;
        player_rigid_body_refresh(h,b);player_rigid_body_integrate(h,b,slice_dt);
        // Original integration-interface callback 597FA0 follows refresh,
        // before outer 40BD40 normalization. It uses the last live contact.
        p.motion_state=prop_motion_state(b.velocity,b.angular_velocity,
            queued_contacts_?p.contact_count!=0:p.terrain_contact,p.motion_state,age(p.last_car_contact));
        b.gravity=p.motion_state!=2;
        prop_rest_momenta(p.motion_state,b.momentum,b.angular_momentum);
        if(p.motion_state!=0)player_rigid_body_refresh(h,b);
        player_rigid_body_normalize(b);++steps;
        if(queued_contacts_)continue;
        V correction{};
        // Impulses below change momentum, not the integrated pose. Query
        // the immutable geometry together, then apply impulses in the same
        // vertex order as before. Preserve the two rounded fallback stores.
        for(std::size_t i=0;i<p.points.size();++i) {
            const auto local=p.points[i];auto& q=terrain_queries_[i];
            terrain_arms_[i]=rotated(b,sub(local,h.centre_of_mass));
            q.to=add(b.position,terrain_arms_[i]);
            q.from=add(before.position,rotated(before,sub(local,h.centre_of_mass)));
            q.fallback_from={q.to[0],q.to[1],q.to[2]+1};
            q.fallback_to={q.to[0],q.to[1],q.fallback_from[2]-2};
        }
        query_track_contact_pairs(world,terrain_queries_.data(),p.points.size(),
                                  terrain_hits_.data(),terrain_statuses_.data());
        for(std::size_t i=0;i<p.points.size();++i) {
            if(terrain_statuses_[i]!=TrackContactStatus::hit)continue;
            const auto& arm=terrain_arms_[i];const auto& point=terrain_queries_[i].to;
            const auto& hit=terrain_hits_[i];
            V normal{{hit.normal[0],hit.normal[1],hit.normal[2]}};
            const float depth=dot(sub({{hit.point[0],hit.point[1],hit.point[2]}},point),normal);
            if(depth<0)continue;
            prop_touch_ground(p.ground_contacts,static_cast<std::uint32_t>(hit.polygon),clock_);
            const float remaining=depth-dot(correction,normal);if(remaining>0)correction=add(correction,mul(normal,remaining));
            const auto velocity=add(b.velocity,cross(b.angular_velocity,arm));const float closing=dot(velocity,normal);
            if(closing>=0)continue;
            const auto moment=cross(arm,normal);V inverse{};
            for(unsigned axis=0;axis<3;++axis)inverse=add(inverse,mul(b.axes[axis],dot(moment,b.axes[axis])/h.inertia[axis]));
            const float impulse=-closing/(1/h.mass+dot(cross(inverse,arm),normal));
            V j=mul(normal,impulse);const auto tangent=sub(velocity,mul(normal,closing));const float speed=std::sqrt(dot(tangent,tangent));
            if(speed>0)j=sub(j,mul(tangent,std::min(.3F*impulse,h.mass*speed)/speed));
            // Ground adapter: non-bouncing vertex support plus Coulomb
            // friction. Original prop manifold/contact caching remains open.
            ContactBody body;body.identity=1;body.car_scaling=false;body.inverse_mass=1/h.mass;
            body.linear_momentum=b.momentum;body.angular_momentum=b.angular_momentum;
            if(!apply_car_contact_impulse(body,j,arm)){error="prop ground impulse refused";return false;}
            b.momentum=body.linear_momentum;b.angular_momentum=body.angular_momentum;player_rigid_body_refresh(h,b);
        }
        b.position=add(b.position,correction);
        // Preserve individual polygon-pair creation/last-touch clocks in
        // first-admission order, independently of this frame's ray hits.
        p.terrain_contact=!p.ground_contacts.empty();
    }
    publish(p.state);
    // A falling body completely below every static collision surface cannot
    // return under gravity. Retire it instead of simulating invisible debris.
    float highest=b.position[2];
    for(const auto& point:p.points)highest=std::max(highest,b.position[2]+rotated(b,sub(point,h.centre_of_mass))[2]);
    if(highest<lowest_world_ && b.velocity[2]<0){p.expired=true;p.awake=false;}
    const bool contact=queued_contacts_?p.contact_count!=0:p.terrain_contact;
    p.quiet=queued_contacts_?age(p.first_contact_tick):p.terrain_contact?age(p.ground_contacts.front().created):0;
    // 597B80 ->597C70 only clears active flags, retaining pose and momentum.
    if(prop_can_settle(b.velocity,b.angular_velocity,contact,
                       age(p.last_car_contact),p.quiet))p.awake=false;
    // Both original scheduler paths expire stale records after body update,
    // at 0.3 s since last touch, not on the first missed vertex projection.
    p.render_support_lift=p.support_lift;
    p.support_lift=prop_next_lift(p.support_lift,p.awake,p.impact.activation,dt);
    prop_expire_ground(p.ground_contacts,clock_);
    p.terrain_contact=!p.ground_contacts.empty();
    for(float v:b.position)if(!std::isfinite(v)){error="prop body became nonfinite";return false;}
    error.clear();return true;
}
bool RaceProps::refresh_contact(RaceProp& p,VehicleState& state) {
    if(!state.rigid_body_ready)vehicle_import_rigid(p.spec.handling,state);
    auto& b=state.rigid_body;
    player_rigid_body_refresh(p.spec.handling,b);
    p.motion_state=prop_motion_state(b.velocity,b.angular_velocity,
        queued_contacts_?p.contact_count!=0:p.terrain_contact,p.motion_state,prop_clock_age(clock_,p.last_car_contact));
    b.gravity=p.motion_state!=2;
    prop_rest_momenta(p.motion_state,b.momentum,b.angular_momentum);
    if(p.motion_state!=0)player_rigid_body_refresh(p.spec.handling,b);
    publish(state);return true;
}
void RaceProps::restore_proxy(RaceProp& p) {
    if(!p.placement_ready)return;
    p.state={};p.state.position=p.placement_pose.position;p.state.orientation=p.placement_pose.orientation;
    vehicle_import_rigid(p.spec.handling,p.state);
    p.state.rigid_body.axes=p.placement_pose.axes;
    player_rigid_body_refresh(p.spec.handling,p.state.rigid_body);
    p.history=race_contact_pose(p.state);p.motion_state=0;
    p.support_lift=p.render_support_lift=0;
    p.contact_count=0;p.ground_contacts.clear();p.terrain_contact=false;p.last_car_contact=0;
}
}
