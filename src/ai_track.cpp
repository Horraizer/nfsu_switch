#include "nfsu/vehicle_track.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/car_contact.h"
#include "nfsu/race_contacts.h"
#include <algorithm>
#include <cmath>

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
    if(!player_request_shift_tables(model_->spec().handling,tables.up,tables.down))return false;
    ai_tables_=tables;ai_low_slip_=low_slip;ai_configured_=true;return true;
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
        const auto preload=player_aero_preloads(h,s.owner_speeds.forward,true,s.collision_state);
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
        latch_aero();latch_preloads();
        ai_collision_policy(next.collision_context,next.body,next.aero);
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
