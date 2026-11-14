#include "nfsu/vehicle_track.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/car_contact.h"
#include "nfsu/race_contacts.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace nfsu {
namespace {
void publish(const AiPhysicsState& a,VehicleState& s) {
    s.body_rest_state=a.rest_state;s.body_gravity=a.body.gravity;
    s.rigid_body=a.body;
    vehicle_publish_rigid(s);
    s.engine_rpm=a.engine.rpm;s.gear=a.engine.gear;s.published_gear=a.engine.gear;
    s.engine.angular_speed=a.engine.rpm*kPlayerRpmToRadS;
    s.steering_word=a.engine.steering;s.steering_word_ready=true;
    s.steer_angle=double(static_cast<std::int16_t>(a.engine.steering))*(6.28318530717958647692/65536);
    for(unsigned k=0;k<4;++k) {
        const auto& w=a.engine.wheels[k];auto& out=s.wheels[k];
        out.previous_compression=out.compression;out.compression=w.compression;
        out.spin=w.spin;out.contact=w.contact;out.brake_locked=w.locked;
        out.prepared_slip=w.slip[0];out.prepared_sliding=w.sliding;
        out.prepared_force_long=w.feedback[0];
    }
    s.published_support=0;for(const auto& w:a.engine.wheels)s.published_support+=w.contact?1U:0U;
    s.owner_speeds=player_publish_owner_speeds(s.velocity,
        {a.body.axes[0][0],a.body.axes[0][1],a.body.axes[0][2]});
    player_publish_owner_motion(s.owner_motion,a.body.velocity,a.body.angular_velocity,a.body.axes);
}
}

namespace {
struct RivalPathSample {
    VehicleVec3 point, tangent;
};

RivalPathSample sample_rival_path(const AiDriverPath& path,float parameter) {
    const unsigned last=path.count-1;
    const unsigned i=std::min(static_cast<unsigned>(std::max(0.F,parameter)),last-1);
    const float t=parameter-float(i);
    const auto coordinate=[&](unsigned k) {
        const float b=path.points[i][k],c=path.points[i+1][k];
        if(t<0.F)return std::array<float,2>{b+t*(c-b),c-b};
        if(t>1.F)return std::array<float,2>{c+(t-1.F)*(c-b),c-b};
        const float a=i?path.points[i-1][k]:2.F*b-c;
        const float d=i+2<=last?path.points[i+2][k]:2.F*c-b;
        const float linear=c-a,quadratic=2.F*a-5.F*b+4.F*c-d,cubic=3.F*(b-c)+d-a;
        return std::array<float,2>{b+.5F*t*(linear+t*(quadratic+t*cubic)),
                                  .5F*(linear+t*(2.F*quadratic+3.F*t*cubic))};
    };
    const auto x=coordinate(0),y=coordinate(1),z=coordinate(2);
    return {{x[0],y[0],z[0]},{x[1],y[1],z[1]}};
}

float project_rival_path(const AiDriverPath& path,const VehicleVec3& position) {
    // 59D430 starts at node zero and stops when the distance first grows.
    const auto distance=[&](unsigned i) {
        const auto& p=path.points[i];
        const double dx=position.x-p[0],dy=position.y-p[1],dz=position.z-p[2];
        return dx*dx+dy*dy+dz*dz;
    };
    unsigned nearest=0;
    for(double best=distance(0);nearest+1<path.count;++nearest) {
        const double candidate=distance(nearest+1);
        if(candidate>=best)break;
        best=candidate;
    }
    // 59D540 finds the perpendicular on the 3D spline by secant iteration.
    float parameter=float(nearest),step=1.F,previous_move=0.F,previous_along=0.F;
    for(unsigned iteration=0;iteration<32;++iteration) {
        const auto sample=sample_rival_path(path,parameter);
        const auto& p=sample.point;
        const auto& d=sample.tangent;
        const float slope=float(vehicle_sqrt(d.x*d.x+d.y*d.y+d.z*d.z));
        if(!(slope>0.F))break;
        const float along=float(((position.z-p.z)*d.z+(position.y-p.y)*d.y+
                                (position.x-p.x)*d.x)/slope);
        if(std::abs(along)<.005F)break;
        const float rate=previous_move==0.F?slope:-(along-previous_along)/previous_move;
        if(rate==0.F)break;
        const float move=std::clamp(along/rate,-step,step);
        parameter+=move;
        if(move*previous_move<0.F)step*=.5F;
        previous_move=move;
        previous_along=along;
    }
    return parameter;
}

float rival_path_length(const AiDriverPath& path) {
    float length=0;
    for(unsigned i=1;i<path.count;++i) {
        const auto& a=path.points[i-1];const auto& b=path.points[i];
        const float dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2];
        length+=float(vehicle_sqrt((dz*dz+dx*dx)+dy*dy));
    }
    return length;
}
}

bool VehicleTrackSession::step_rival_path(const AiDriverPath& path,float desired_speed,float speed_duration,bool publication,
                                         const VehicleInput& input,VehicleTrackTelemetry& telemetry) {
    const float dt=input.world_frame_seconds;
    if(!started_||!ai_configured_||ai_active_||!std::isfinite(dt)||dt<=0||dt>.1F||
       !std::isfinite(desired_speed)||!std::isfinite(speed_duration))return false;
    const auto& h=model_->spec().handling;
    auto next=rival_path_;
    if(publication||!next.path.count) {
        if(path.count<3||path.count>path.points.size())return false;
        for(unsigned i=0;i<path.count;++i)
            for(float value:path.points[i])if(!std::isfinite(value))return false;
        const float length=rival_path_length(path);
        next.path=path;
        next.parameter=project_rival_path(path,state_.position);
        if(!(length>0.F))return false;
        next.metres_per_point=length/float(path.count-1);
        const auto sample=sample_rival_path(path,next.parameter);
        const double tx=sample.tangent.x,ty=sample.tangent.y;
        const double horizontal=vehicle_sqrt(tx*tx+ty*ty);
        if(!(horizontal>0.00001))return false;
        const double ox=state_.position.x-sample.point.x,oy=state_.position.y-sample.point.y;
        next.vertical_offset=state_.position.z-sample.point.z;
        // Reject disconnected geometry before publishing a new path.
        if(ox*ox+oy*oy>25.0||std::abs(next.vertical_offset)>3.0)return false;
        // 463910 seeds the signed lateral channel with the projected offset
        // and velocity. 464244 advances its cubic toward zero over .5 s.
        next.lateral_offset=float((tx*oy-ty*ox)/horizontal);
        next.lateral_derivative=float(.5*(tx*state_.velocity.y-ty*state_.velocity.x)/horizontal);
        next.lateral_phase=0.F;
        next.speed=std::max(0.F,state_.owner_speeds.forward);
        // 463910 uses 456830's acceleration descriptor for the limit.
        constexpr float maximum_speed=98.34600067138672F;
        const float x=std::clamp(next.speed/ai_scale_/maximum_speed,0.F,1.F)*63.F;
        const unsigned i=std::min(static_cast<unsigned>(x),62U);
        const float engine=ai_acceleration_[i]+(x-float(i))*(ai_acceleration_[i+1]-ai_acceleration_[i]);
        const float drag=(1.F-input.drafting)*h.drag*next.speed*next.speed/h.mass;
        const float duration=speed_duration>0.F?speed_duration:.5F;
        next.acceleration=std::min(engine-drag,std::max(-20.F,(desired_speed-next.speed)/duration));
    }

    const float new_speed=std::max(0.F,next.speed+dt*next.acceleration);
    const float new_parameter=next.parameter+(next.speed+new_speed)*.5F*dt/next.metres_per_point;
    const float limit=float(next.path.count-1);
    if(new_parameter>=limit)return false;
    const auto sample=sample_rival_path(next.path,new_parameter);
    const auto& base=sample.point;
    const auto [dx,dy,dz]=sample.tangent;
    const double horizontal=vehicle_sqrt(dx*dx+dy*dy);
    const double norm=vehicle_sqrt(horizontal*horizontal+dz*dz);
    if(!(horizontal>0.00001))return false;
    next.lateral_phase=std::min(1.F,next.lateral_phase+dt/.5F);
    const float phase=next.lateral_phase,remaining=1.F-phase;
    const float lateral=remaining*remaining*
        (next.lateral_offset*(1.F+2.F*phase)+next.lateral_derivative*phase);
    auto s=state_;
    s.position={base.x-lateral*dy/horizontal,base.y+lateral*dx/horizontal,
                base.z+next.vertical_offset};
    // The retail check looks ahead for a departure from the current
    // vertical tangent, not just a discontinuity at the current tick.
    const float parameter_rate=(next.speed+new_speed)*.5F/next.metres_per_point;
    const auto future=sample_rival_path(next.path,
        std::min(limit,new_parameter+parameter_rate*9.F*dt)).point;
    const double linear_height=base.z+(dz/horizontal)*vehicle_sqrt(
        (future.x-base.x)*(future.x-base.x)+(future.y-base.y)*(future.y-base.y));
    if(future.z-linear_height>1.F||
       std::abs(s.position.z-state_.position.z-state_.velocity.z*dt)>1.F)return false;
    const auto& old=state_.position;
    const auto tx=s.position.x-old.x,ty=s.position.y-old.y,tz=s.position.z-old.z;
    const auto moved=vehicle_sqrt(tx*tx+ty*ty+tz*tz);
    // Keep position and momentum continuous when a new path crosses a
    // junction or changes its local point spacing abruptly.
    const double correction_speed=2.0*std::abs(next.lateral_derivative);
    if(moved>(std::max(next.speed,new_speed)+correction_speed+5.F)*dt)return false;
    s.velocity={tx/dt,ty/dt,tz/dt};
    const PlayerRigidVector forward{float(dx/norm),float(dy/norm),float(dz/norm)};
    const PlayerRigidVector side{float(-dy/horizontal),float(dx/horizontal),0};
    const PlayerRigidVector up{-side[1]*forward[2],side[0]*forward[2],
                                forward[0]*side[1]-forward[1]*side[0]};
    const auto heading=track_angle(forward[0],forward[1]);
    const auto previous=track_angle(s.rigid_body.axes[0][0],s.rigid_body.axes[0][1]);
    const auto turn=static_cast<std::int16_t>(heading-previous);
    // This adapter uses the spline tangent directly. Do not feed a
    // discontinuous heading and its derived yaw into the contact solver.
    // Keep the current body and let the caller continue with selector3.
    constexpr double maximum_heading_rate=8.0;
    const double turn_radians=double(turn)*(6.28318530717958647692/65536);
    if(std::abs(turn_radians)>maximum_heading_rate*dt)return false;
    s.angular_velocity={0,0,turn_radians/dt};
    s.orientation=vehicle_orientation_from_axes({{forward,side,up}});
    s.owner_speeds=player_publish_owner_speeds(s.velocity,{forward[0],forward[1],forward[2]});
    s.owner_slip_angle=player_owner_slip_angle(s.velocity,heading);
    s.steering_word=input.road_angle_word;s.steering_word_ready=true;s.steer_angle=input.road_angle;
    s.published_support=4;s.body_rest_state=0;s.forces_prepared=false;
    for(unsigned i=0;i<s.wheels.size();++i) {
        auto& wheel=s.wheels[i];
        wheel.spin=new_speed/h.wheels[i].radius;wheel.contact=true;
        wheel.prepared_slip=0;wheel.prepared_sliding=false;wheel.prepared_force_long=0;
    }
    unsigned gear=std::clamp(unsigned(std::max(kVehicleGearFirst,s.gear)),
        unsigned(kVehicleGearFirst),h.forward_gears+1);
    const float final=h.final_drive_front*h.rear_drive_fraction+h.final_drive_rear*(1.F-h.rear_drive_fraction);
    const float angular_speed=float(s.wheels[0].spin*(1.F-h.rear_drive_fraction)+
        s.wheels[3].spin*h.rear_drive_fraction)*final;
    const float rpm=angular_speed*h.gear_ratios[gear]*kPlayerRadSToRpm;
    if(rpm>=ai_tables_.up[gear]&&gear<h.forward_gears+1)++gear;
    else if(rpm<ai_tables_.down[gear]&&gear>unsigned(kVehicleGearFirst))--gear;
    s.gear=int(gear);s.published_gear=s.gear;
    s.engine_rpm=std::clamp(angular_speed*h.gear_ratios[gear]*kPlayerRadSToRpm,h.idle_rpm,h.max_rpm);
    s.engine.angular_speed=float(s.engine_rpm)*kPlayerRpmToRadS;
    vehicle_import_rigid(h,s);
    player_publish_owner_motion(s.owner_motion,s.rigid_body.velocity,s.rigid_body.angular_velocity,s.rigid_body.axes);

    next.parameter=new_parameter;next.speed=new_speed;
    ++s.step;s.simulation_seconds+=dt==1.F/60.F?1.0/60.0:double(dt);
    VehicleTrackTelemetry t;
    ++steps_;++ticks_;t.step=steps_;t.tick=true;
    run_tick(s,input,t);
    rival_path_=t.reset==VehicleTrackReset::none?next:RivalPathMotion{};
    state_=s;last_input_=input;
    t.pending=pending_;t.resets=resets_;t.position=s.position;t.orientation=s.orientation;
    t.velocity=s.velocity;t.angular_velocity=s.angular_velocity;
    t.model.rigid_body=s.rigid_body;t.model.rigid_body_ready=s.rigid_body_ready;
    t.model.rigid_orientation=s.rigid_orientation;t.model.steering_word=s.steering_word;
    t.model.steering_word_ready=s.steering_word_ready;
    t.lap=progress_.lap;t.percent=progress_.coordinate.fraction*100.F;
    t.zone=progress_.zone;t.off_path=progress_.off_path;telemetry=t;
    return true;
}

// Original440A40 road and577060 object support shared with player struts.
class VehicleTrackSession::AiWorld final:public AiPhysicsGround {
public:
    AiWorld(const VehicleTrackSession& owner,VehicleState& car,VehicleTrackTelemetry& telemetry)
        :owner_(owner),car_(car),telemetry_(telemetry){}
    bool query(const VehicleHandling& h,const PlayerRigidBodyState& b,
               std::array<AiWheelGround,4>& samples) const override {
        if(owner_.ground_.uses_cached_struts()) {
        std::array<VehicleVec3,4> hubs;
        std::array<VehicleTrackCast,4> samples_out;
        for(unsigned k=0;k<4;++k) {
            samples[k].hub=player_strut_attachment(h,k,b);
            const auto& p=samples[k].hub;hubs[k]={p[0],p[1],p[2]};
        }
        owner_.ground_.probe_strut_heights(hubs,samples_out);
        for(unsigned k=0;k<4;++k) {
            auto& s=samples[k];const auto& sample=samples_out[k];
            telemetry_.wheel_surface[k]=sample.surface;
            if(!sample.hit.hit) {
                if(sample.surface==VehicleTrackSurface::refused) {
                    failure_="invalid cached ground sample";return false;
                }
                ai_ground_clear(s);continue;
            }
            const auto& plane=sample.hit.plane;
            if(plane[2]==0) {ai_ground_clear(s);continue;}
            s.height=-((plane[1]*s.hub[1]+plane[0]*s.hub[0]-plane[3])/plane[2]);
            std::copy_n(plane.begin(),3,s.normal.begin());
        }
        } else {
        std::array<std::array<float,3>,4> origins,endpoints;
        std::array<TrackContact,4> hits;
        std::array<TrackContactStatus,4> statuses;
        for(unsigned k=0;k<4;++k) {
            auto& s=samples[k];s.hub=player_strut_attachment(h,k,b);
            origins[k]={s.hub[0],s.hub[1],s.hub[2]+1.F};
            endpoints[k]={s.hub[0],s.hub[1],s.hub[2]-2.F};
        }
        query_track_contact_segments(owner_.course_->world,origins.data(),endpoints.data(),4,hits.data(),statuses.data());
        for(unsigned k=0;k<4;++k) {
            auto& s=samples[k];const auto& hit=hits[k];const auto status=statuses[k];
            if(status==TrackContactStatus::clear||status==TrackContactStatus::outside) {
                ai_ground_clear(s);
                telemetry_.wheel_surface[k]=status==TrackContactStatus::outside?
                    VehicleTrackSurface::outside:VehicleTrackSurface::clear;
                continue;
            }
            if(status!=TrackContactStatus::hit) {
                failure_="ground wheel "+std::to_string(k)+" status "+std::to_string(int(status))+
                    " at "+std::to_string(s.hub[0])+","+std::to_string(s.hub[1])+","+std::to_string(s.hub[2]);
                return false;
            }
            const auto plane=vehicle_track_ground_plane(owner_.course_->world,hit.polygon);
            if(plane[2]==0) {
                ai_ground_clear(s);telemetry_.wheel_surface[k]=VehicleTrackSurface::clear;
                continue;
            }
            s.height=-((plane[1]*s.hub[1]+plane[0]*s.hub[0]-plane[3])/plane[2]);
            std::copy_n(plane.begin(),3,s.normal.begin());
            telemetry_.wheel_surface[k]=VehicleTrackSurface::road;
        }
        }
        return true;
    }
    bool resolve(const VehicleHandling& h,AiPhysicsState& candidate) const override {
        publish(candidate,car_);
        if(owner_.approx_.body_contact && !owner_.deferred_body_contacts_) {
            const auto before=candidate.body;
            if(!owner_.resolve_body(car_,telemetry_)) {
                failure_="track contact response refused";return false;
            }
            // resolve_body retains authoritative momenta when it applies an
            // impulse. Import also carries positional separation into the body.
            vehicle_import_rigid(h,car_);
            candidate.body=car_.rigid_body;
            if(before.position!=candidate.body.position||before.momentum!=candidate.body.momentum||
               before.angular_momentum!=candidate.body.angular_momentum)
                player_rigid_body_refresh(h,candidate.body);
        }
        return true;
    }
    const std::string& failure() const {return failure_;}
private:
    const VehicleTrackSession& owner_;
    VehicleState& car_;
    VehicleTrackTelemetry& telemetry_;
    mutable std::string failure_;
};

bool VehicleTrackSession::set_ai_scale(float scale) noexcept {
    if(!ai_configured_||!std::isfinite(scale)||scale<1.F)return false;
    ai_scale_=scale;return true;
}
bool VehicleTrackSession::configure_ai(float low_slip) {
    if(!valid_||started_||!std::isfinite(low_slip)||low_slip<=0)return false;
    AiPowertrainTables tables;
    const auto& h=model_->spec().handling;
    if(!player_request_shift_tables(h,tables.up,tables.down)||
       !ai_setup_acceleration_curve(h,ai_acceleration_))return false;
    ai_tables_=tables;ai_low_slip_=low_slip;ai_configured_=true;ai_active_=true;return true;
}

void VehicleTrackSession::select_rival_physics(unsigned selector) noexcept {
    if(!ai_configured_)return;
    ai_active_=selector==3;
    ai_started_=false;
    rival_path_={};
    if(selector==1) {
        // 426110 replaces the component solver while keeping the actor pose.
        // Force a refresh of selector1's private force and gearbox state.
        state_.forces_prepared=false;
        state_.aero_ready=false;
        state_.transmission.requested_gear=state_.gear;
        state_.transmission.mode=3;
        state_.transmission.clutch=1.F;
        state_.transmission.shift_pending=false;
        state_.engine.feedback_torque=0.F;
    }
}

bool VehicleTrackSession::step_ai_body(VehicleState& external,const VehicleInput& input,
                                       VehicleTrackTelemetry& telemetry,float continuation_dt,
                                       const VehicleState* published_actor,bool refresh_only,bool contact_refreshed,float refresh_interval,int gear_request) {
    if(!started_) {if(!start(external))return false;}
    const auto previous=state_;
    state_=external;ai_route_tracking_=false;
    const bool ok=step_ai(input,telemetry,gear_request,continuation_dt,published_actor,refresh_only,contact_refreshed,refresh_interval);
    ai_route_tracking_=true;
    if(ok)external=state_;
    else state_=previous;
    return ok;
}

bool VehicleTrackSession::step_ai(const VehicleInput& input,VehicleTrackTelemetry& telemetry,int gear_request,float continuation_dt,
                                const VehicleState* published_actor,bool refresh_only,bool contact_refreshed,float refresh_interval) {
    if(!std::isfinite(continuation_dt)||(continuation_dt<0&&continuation_dt!=-1))return false;
    if(!valid_||!started_||!ai_configured_||!input.road_steering||!input.road_angle_word_valid||
       !std::isfinite(input.throttle)||input.throttle<0||input.throttle>1||
       !std::isfinite(input.brake)||input.brake<0||input.brake>1||
       input.shift!=VehicleShift::none||input.nitrous_active||(gear_request!=-1&&gear_request!=0))return false;
    const auto& spec=model_->spec();const auto& h=spec.handling;
    auto s=state_;auto next=ai_;VehicleTrackTelemetry t;
    if(!refresh_only)update_car_collision_state(s.collision_state,s.collision_support_since,
        s.published_support,s.collision_body_flag,double(input.world_clock_valid?input.world_clock:static_cast<std::int32_t>((ticks_*4000)/60))*double(.00025F));
    if(!ai_started_) {
        next={};next.engine.low_slip=ai_low_slip_;
        //475000 leaves grip zero until474D40 in the first outer update.
        //A traffic body activated by contact is refreshed before that update.
        next.engine.grip=0;
        next.engine.rpm=float(s.engine_rpm);next.engine.gear=s.gear;
        for(unsigned k=0;k<4;++k) {
            next.engine.wheels[k].spin=float(s.wheels[k].spin);
            next.engine.wheels[k].compression=float(s.wheels[k].compression);
            next.engine.wheels[k].contact=s.wheels[k].contact;
        }
    }
    const auto old=s.rigid_body;
    vehicle_import_rigid(h,s);
    next.body=s.rigid_body;
    //The selector3 constructor/outer path leaves rigid+1E0 at zero.
    //477C32's player-only yaw torque cap must not leak through the shared
    //VehicleState import into traffic or rivals after an impact.
    next.body.yaw_torque_limit=0;
    next.collision_context_valid=true;
    //469390 rewinds the rigid body alone.474DD0 still reads actor+70/+90
    //from the endpoint, until474730 publishes the completed continuation.
    const auto& actor=published_actor?*published_actor:s;
    const auto world_tick=input.world_clock_valid?input.world_clock:static_cast<std::int32_t>((ticks_*4000)/60);
    if(continuation_dt<0)extend_owner_recovery(world_tick);
    next.collision_context={s.collision_state,s.published_support,race_contact_axes(actor),
        {{float(actor.velocity.x),float(actor.velocity.y),float(actor.velocity.z)}},s.published_gear,float(input.throttle),
        preparing_grid_||owner_recovery_active(world_tick)};
    if(continuation_dt<0)ai_collision_policy(next.collision_context,next.body,next.aero);
    s.alternate_yaw_active=next.body.alternate_yaw;
    // Pair impulses and teleports occur outside this component's completion.
    if(old.position!=next.body.position||old.momentum!=next.body.momentum||
       old.angular_momentum!=next.body.angular_momentum||
       ai_.body.momentum!=next.body.momentum||ai_.body.angular_momentum!=next.body.angular_momentum)
        next.prepared=false;
    next.engine.throttle=float(input.throttle);
    //474B45..474B72 gives the service brake full range and halves the parking brake.
    next.engine.brake=std::max(float(input.brake),input.handbrake?.5F:0.F);
    next.engine.steering=input.road_angle_word;
    next.engine.gear_request=gear_request;
    next.engine.world_time=input.world_clock_valid?input.world_clock:static_cast<std::int32_t>((ticks_*4000)/60);
    const auto latch_aero=[&]() {
        next.engine.grip=1; //474D40:1 minus ordinary owner traction-loss (zero).
        next.engine.boost=ai_scale_; //474D58 actor+404, independent of6FFE54.
        next.aero.drag_scale=1.F-std::clamp(input.drafting,0.F,.8999999761581421F);
        next.aero.processed_throttle=next.engine.throttle;
        //474B87 reads the published actor-state+38C, not the freshly
        //queried component wheels.474730 publishes those after completion.
        next.aero.support_kind=float(next.collision_context.support)*.25F;
    };
    if(continuation_dt<0)latch_aero();
    const auto latch_preloads=[&]() {
        // Selector3 writes the collision aero flag before 4745B0 samples it.
        const auto preload=player_aero_preloads(h,s.owner_speeds.forward,next.aero.enabled,s.collision_state);
        for(unsigned k=0;k<4;++k)next.engine.wheels[k].preload=preload[k];
    };
    //474640 initializes the load;474A90 updates it at the outer boundary.
    //A post-impulse474CA0 refresh must retain the pre-hit preload even after
    //465D00 has changed actor state, until the resumed outer update.
    if(!ai_started_||continuation_dt<0)latch_preloads();
    AiWorld world(*this,s,t);
    if(!ai_started_) {
        //4743D9 calls474640, which ends in the component refresh46ABD0.
        //4743E0 then calls474CA0, whose first operation refreshes again.
        // The first query seeds strut compression before the second computes
        // the force used for integration. A single zero-dt refresh at spawn
        // turns the initial terrain-height difference into a damper impulse.
        std::array<AiWheelGround,4> samples;
        if(!world.query(h,next.body,samples)) {
            error_="selector3 initial "+world.failure();return false;
        }
        next.ground_queries+=4;
        ai_powertrain_struts(h,next.body,samples,0,next.engine);
        ai_powertrain_complete(h,ai_tables_,next.engine);
    }
    if(continuation_dt>=0) {
        //469460 component refresh precedes474A90's policy change. Its forces
        //are retained for the first resumed derivative, including state1.
        if(refresh_only)next.component_interval=refresh_interval;
        if(!contact_refreshed&&!ai_physics_advance(h,ai_tables_,spec.aero,0,world,next,true)) {
            error_="selector3 contact force refresh refused";return false;
        }
        if(refresh_only) {
            //46B744 refreshes components without publishing actor fields or
            //running the outer policy. Preserve wheel history for the next
            //refresh after the impulse; do not advance time or normalize axes.
            auto refreshed=state_;refreshed.rigid_body=next.body;vehicle_publish_rigid(refreshed);
            ai_=next;ai_started_=true;state_=refreshed;telemetry=t;return true;
        }
        latch_aero();
        ai_collision_policy(next.collision_context,next.body,next.aero);
        latch_preloads();
        s.alternate_yaw_active=next.body.alternate_yaw;
    }
    const float frame_dt=input.world_frame_seconds;
    if(!std::isfinite(frame_dt)||frame_dt<=0||frame_dt>.1F){error_="invalid selector3 frame interval";return false;}
    if(!ai_physics_advance(h,ai_tables_,spec.aero,continuation_dt>=0?continuation_dt:frame_dt,world,next)) {
        error_=world.failure().empty()?"selector3 component refused":"selector3 "+world.failure();return false;
    }
    publish(next,s);
    if(continuation_dt>=0) {
        ai_=next;ai_started_=true;state_=s;t.position=s.position;t.orientation=s.orientation;
        t.velocity=s.velocity;t.angular_velocity=s.angular_velocity;telemetry=t;return true;
    }
    const unsigned slices=unsigned(frame_dt/.00839999970048666F)+1;
    s.step+=slices;s.simulation_seconds+=frame_dt==1.F/60.F?1.0/60.0:double(frame_dt);
    last_input_=input;last_ai_gear_request_=gear_request;
    // Reuse the port's route, reset zones and timing publication once per frame.
    steps_+=slices;++ticks_;t.step=steps_;t.tick=true;
    if(ai_route_tracking_)run_tick(s,input,t);
    if(ai_route_tracking_ && progress_.finished) {
        // Route location still feeds the live driver after timing has latched
        // the finish; do not emit another lap or overwrite the race result.
        const float xy[]={float(s.position.x),float(s.position.y)};
        update_track_path_coordinate(course_->route.spline,course_->track,progress_.coordinate,xy);
    }
    ai_started_=t.reset==VehicleTrackReset::none;
    ai_=next;state_=s;
    t.pending=pending_;t.resets=resets_;t.position=s.position;t.orientation=s.orientation;
    t.velocity=s.velocity;t.angular_velocity=s.angular_velocity;
    t.model.rigid_body=s.rigid_body;t.model.rigid_body_ready=s.rigid_body_ready;
    t.model.rigid_orientation=s.rigid_orientation;t.model.steering_word=s.steering_word;
    t.model.steering_word_ready=s.steering_word_ready;
    t.lap=progress_.lap;t.percent=progress_.coordinate.fraction*100.F;t.zone=progress_.zone;
    t.off_path=progress_.off_path;telemetry=t;return true;
}
} // namespace nfsu
