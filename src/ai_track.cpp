#include "nfsu/vehicle_track.h"
#include "nfsu/ai_path.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/car_contact.h"
#include "nfsu/race_contacts.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

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
        s.published_wheel_slip[k]=w.slip;
        out.prepared_force_long=w.feedback[0];
    }
    s.published_support=0;for(const auto& w:a.engine.wheels)s.published_support+=w.contact?1U:0U;
    s.owner_speeds=player_publish_owner_speeds(s.velocity,
        {a.body.axes[0][0],a.body.axes[0][1],a.body.axes[0][2]});
    s.owner_motion.velocity=a.body.velocity;
    s.owner_motion.angular_velocity=a.body.angular_velocity;
    s.owner_motion.axes=a.body.axes;
    // 474801 passes the two float components directly to 564DB0.
    s.owner_motion.heading=track_angle(a.body.axes[0][0],a.body.axes[0][1]);
}
void vehicle_effect_telemetry(const VehicleState& s, double frame_dt, VehicleTelemetry& t) {
    t.simulation_seconds=s.simulation_seconds;
    t.solver_dt=frame_dt;
    t.owner_speeds=s.owner_speeds;
    for(unsigned i=0;i<s.wheels.size();++i) {
        auto& w=t.wheels[i];
        w.contact=s.wheels[i].contact;
        w.compression=s.wheels[i].compression;
        w.effect_slip_velocity=s.published_wheel_slip[i];
    }
}
}

bool VehicleTrackSession::step_rival_path(const AiDriverPath& path,float desired_speed,float speed_duration,bool publication,
                                         const VehicleInput& input,VehicleTrackTelemetry& telemetry,
                                         bool& departure_predicted) {
    const float dt=input.world_frame_seconds;
    if(!started_||!ai_configured_||ai_active_||!std::isfinite(dt)||dt<=0||dt>.1F||
       !std::isfinite(desired_speed)||!std::isfinite(speed_duration))return false;
    const auto& h=model_->spec().handling;
    auto next=rival_path_;
    if(publication||!next.path.count) {
        AiPlannedPath resampled;
        resampled.path=path;
        // 463B30 resamples the already published driver path again and
        // 463910 uses that operation's length, before point redistribution.
        if(path.count<3||!ai_path_publish(resampled)||!(resampled.length>0.F))return false;
        next.path=resampled.path;
        next.parameter=ai_path_body_project(next.path,
            {{float(state_.position.x),float(state_.position.y),float(state_.position.z)}});
        next.points_per_metre=float(path.count-1)/resampled.length;
        next.elapsed=0;
        ai_path_body_publish(next,{{float(state_.position.x),float(state_.position.y),float(state_.position.z)}},
            {{float(state_.velocity.x),float(state_.velocity.y),float(state_.velocity.z)}});
        ai_path_body_speed_target(next,h,ai_acceleration_,
            {state_.owner_speeds.forward,desired_speed,speed_duration,ai_scale_,
             input.drafting,ai_gearing_speed_limit_});
    }

    const bool departure=ai_path_body_departure(next,h);
    const auto motion=ai_path_body_step(next,h,dt);
    const float new_speed=motion.speed;
    auto s=state_;
    s.position={motion.position[0],motion.position[1],motion.position[2]};
    const auto heading=motion.heading;
    PlayerRigidMatrix axes;
    std::array<VehicleTrackCast,4> casts;
    fit_road_pose(heading,s.position,axes,casts);
    VehicleTrackTelemetry t;
    for(unsigned k=0;k<casts.size();++k) {
        if(casts[k].surface==VehicleTrackSurface::refused)return false;
        t.wheel_surface[k]=casts[k].surface;
    }
    s.dynamic_ground_candidate=casts.back().hit.dynamic_candidate;
    for(unsigned i=0;i<casts.size();++i)t.model.wheels[i].surface=casts[i].hit.surface;
    const auto published_motion=ai_path_body_motion(
        {{float(state_.position.x),float(state_.position.y),float(state_.position.z)}},
        {{float(s.position.x),float(s.position.y),float(s.position.z)}},
        state_.owner_motion.heading,heading,new_speed,dt);
    const auto& velocity=published_motion.velocity;
    s.velocity={velocity[0],velocity[1],velocity[2]};
    const float yaw=published_motion.yaw;
    s.angular_velocity={0,0,yaw};
    s.orientation=vehicle_orientation_from_axes(axes);
    s.owner_speeds=player_publish_owner_speeds(s.velocity,{axes[0][0],axes[0][1],axes[0][2]});
    s.owner_speeds.forward=new_speed; //4643F3: target speed, not projected displacement.
    s.owner_speeds.magnitude=published_motion.magnitude;
    s.owner_slip_angle=player_owner_slip_angle(s.velocity,heading);
    s.steering_word=input.road_angle_word;s.steering_word_ready=true;s.steer_angle=input.road_angle;
    // 464470..46455B updates wheel motion/load/slip but retains Actor350
    // contact bytes and Actor38C support from the previous physical owner.
    s.body_rest_state=0;s.forces_prepared=false;
    const auto path_slip=ai_path_wheel_slip(new_speed,
        {{float(s.velocity.x),float(s.velocity.y),float(s.velocity.z)}},axes);
    for(unsigned i=0;i<s.wheels.size();++i) {
        auto& wheel=s.wheels[i];
        wheel.spin=new_speed/h.wheels[i].radius;
        wheel.prepared_slip=0;wheel.prepared_sliding=false;wheel.prepared_force_long=0;
        s.published_wheel_slip[i]=path_slip;
    }
    AiPathPowertrainState publication_state{s.path_powertrain_speed,s.published_gear,float(s.engine_rpm)};
    if(!ai_path_powertrain(s.owner_speeds.magnitude,publication_state))return false;
    s.path_powertrain_speed=publication_state.filtered_speed;
    s.gear=publication_state.gear;s.published_gear=s.gear;
    s.engine_rpm=publication_state.rpm;
    s.engine.angular_speed=float(s.engine_rpm)*kPlayerRpmToRadS;
    //Retain the raw ground-fitted basis. A quaternion round trip would
    //orthogonalize it and change the next contact/full-physics import.
    player_rigid_body_import(h,s.rigid_body,
        {{float(s.position.x),float(s.position.y),float(s.position.z)}},axes,velocity,{{0,0,yaw}});
    s.rigid_body.velocity=velocity;s.rigid_body.angular_velocity={0,0,yaw};
    s.rigid_body.snapshot_axes=axes;s.rigid_body_ready=true;s.rigid_orientation=s.orientation;
    player_publish_owner_motion(s.owner_motion,velocity,{{0,0,yaw}},axes);
    s.owner_motion.heading=heading;

    ++s.step;s.simulation_seconds+=dt==1.F/60.F?1.0/60.0:double(dt);
    ++steps_;++ticks_;t.step=steps_;t.tick=true;
    const auto previous_resets=resets_;
    run_tick(s,input,t,4);
    // A refused placement publishes a reason but retains the live solver.
    // A successful placement already reconstructed it in run_tick.
    if(resets_==previous_resets)rival_path_=next;
    state_=s;last_input_=input;
    t.pending=pending_;t.resets=resets_;t.position=s.position;t.orientation=s.orientation;
    t.velocity=s.velocity;t.angular_velocity=s.angular_velocity;
    t.model.rigid_body=s.rigid_body;t.model.rigid_body_ready=s.rigid_body_ready;
    t.model.rigid_orientation=s.rigid_orientation;t.model.steering_word=s.steering_word;
    t.model.steering_word_ready=s.steering_word_ready;
    vehicle_effect_telemetry(s,dt,t.model);
    t.lap=progress_.lap;t.percent=progress_.coordinate.fraction*100.F;
    t.zone=progress_.zone;t.off_path=progress_.off_path;telemetry=t;
    departure_predicted=departure;
    return true;
}

// Original440A40 road and577060 object support shared with player struts.
class VehicleTrackSession::AiWorld final:public AiPhysicsGround {
public:
    AiWorld(const VehicleTrackSession& owner,VehicleState& car,VehicleTrackTelemetry& telemetry,bool publish=true)
        :owner_(owner),car_(car),telemetry_(telemetry),publish_(publish){}
    bool query(const VehicleHandling& h,const PlayerRigidBodyState& b,
               std::array<AiWheelGround,4>& samples) const override {
        for(auto& w:telemetry_.model.wheels){w.surface=0;w.ground_normal={};w.ground_point={};}
        if(owner_.ground_.uses_cached_struts()) {
        std::array<VehicleVec3,4> hubs;
        std::array<VehicleTrackCast,4> samples_out;
        for(unsigned k=0;k<4;++k) {
            samples[k].hub=player_strut_attachment(h,k,b);
            const auto& p=samples[k].hub;hubs[k]={p[0],p[1],p[2]};
        }
        owner_.ground_.probe_strut_heights(hubs,samples_out);
        car_.dynamic_ground_candidate=samples_out.back().hit.dynamic_candidate;
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
            auto& w=telemetry_.model.wheels[k];
            w.surface=sample.hit.surface;
            w.ground_normal={s.normal[0],s.normal[1],s.normal[2]};
            w.ground_point={s.hub[0],s.hub[1],s.height};
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
            auto& w=telemetry_.model.wheels[k];
            w.surface=owner_.course_->world.collision.polygons[hit.polygon].unknown0[2];
            w.ground_normal={s.normal[0],s.normal[1],s.normal[2]};
            w.ground_point={s.hub[0],s.hub[1],s.height};
            telemetry_.wheel_surface[k]=VehicleTrackSurface::road;
        }
        }
        return true;
    }
    bool resolve(const VehicleHandling& h,AiPhysicsState& candidate) const override {
        //426110 has not installed Actor34 while4740D0 constructs components.
        //42A860 returns before publication/world contact during both refreshes.
        if(!publish_)return true;
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
    bool publish_;
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
       !ai_setup_acceleration_curve(h,ai_acceleration_,&ai_gearing_speed_limit_))return false;
    ai_tables_=tables;ai_low_slip_=low_slip;ai_configured_=true;ai_active_=true;return true;
}

void VehicleTrackSession::select_rival_physics(unsigned selector,std::int32_t clock) {
    if(!ai_configured_&&selector!=1)return;
    if(selector==3&&started_) {
        auto placed=state_;VehicleTrackTelemetry telemetry;
        if(!reconstruct_ai_physics(placed,last_input_,last_ai_gear_request_,clock,telemetry))
            throw std::runtime_error(error_);
        state_=placed;
    } else {
        ai_started_=false;ai_import_={};
        if(selector==1&&!reconstruct_full_physics(state_,clock))
            throw std::runtime_error("full mover construction refused");
    }
    ai_active_=ai_configured_&&selector==3;
    rival_path_={};
    if(selector==4)ai_path_body_seed(rival_path_,state_.owner_motion.heading,
        state_.owner_speeds.magnitude,state_.owner_motion.angular_velocity[2]);
}

bool VehicleTrackSession::reconstruct_ai_physics(VehicleState& s,const VehicleInput& input,
                                                int gear_request,std::int32_t clock,VehicleTrackTelemetry& t) {
    const auto& spec=model_->spec();const auto& h=spec.handling;
    AiPhysicsState next;
    next.engine.low_slip=ai_low_slip_;next.engine.grip=0;
    next.engine.rpm=float(s.engine_rpm);next.engine.gear=s.published_gear;
    next.engine.throttle=float(input.throttle);next.engine.brake=float(input.brake);
    next.engine.steering=input.road_angle_word;next.engine.gear_request=gear_request;
    next.engine.world_time=clock;
    //4754E0 imports spin alone. New struts do not inherit old compression.
    const auto preload=player_aero_preloads(h,s.owner_speeds.forward,true);
    for(unsigned k=0;k<4;++k) {
        next.engine.wheels[k].spin=float(s.wheels[k].spin);
        next.engine.wheels[k].preload=preload[k];
    }
    player_rigid_body_import(h,next.body,
        {{float(s.position.x),float(s.position.y),float(s.position.z)}},s.owner_motion.axes,
        {{float(s.velocity.x),float(s.velocity.y),float(s.velocity.z)}},
        {{float(s.angular_velocity.x),float(s.angular_velocity.y),float(s.angular_velocity.z)}});
    next.body.yaw_torque_limit=0;
    next.collision_context_valid=true;
    next.collision_context={s.collision_state,s.published_support,s.owner_motion.axes,
        s.owner_motion.velocity,s.published_gear,float(input.throttle),preparing_grid_||owner_recovery_active(clock)};
    AiWorld world(*this,s,t,false);
    //474640 tail-calls474CA0;4743E0 calls it once more. Each refresh owns
    //struts/rest/bump/feedback/aero, without advancing or publishing the actor.
    for(unsigned pass=0;pass<2;++pass)
        if(!ai_physics_advance(h,ai_tables_,spec.aero,0,world,next,true)) {
            error_=world.failure().empty()?"selector3 constructor refused":"selector3 constructor "+world.failure();
            return false;
        }
    ai_import_={true,s.position,s.velocity,s.angular_velocity,s.orientation};
    s.rigid_body=next.body;s.rigid_body_ready=true;s.rigid_orientation=s.orientation;
    ai_=next;ai_started_=true;return true;
}

bool VehicleTrackSession::reconstruct_full_physics(VehicleState& state,std::int32_t clock) const {
    auto s=state;
    s.motion_before_first_move.reset();
    s.road_launch.reset();
    // 426110 constructs fresh components.478383/47839B restore only the
    // published RPM/gear, not the old solver's cutoff or clutch history.
    // This also applies to the human when47C9E0 releases the road NIS.
    s.forces_prepared=false;
    const auto& h=model_->spec().handling;
    // 476EA4..476EDF constructs the aero cache and loads the strut preloads.
    // Driver throttle and wheel support first enter this cache at 477BED.
    s.aero=PlayerAeroCache{};
    s.aero.enabled=model_->spec().aero.enabled;
    s.aero_preloads=player_aero_preloads(h,s.owner_speeds.forward,s.aero.enabled,s.collision_state);
    s.aero_ready=true;
    //4782C0 imports published wheel speeds, but constructs new struts and
    //wheel torque history. Old compression rates must not damp the new body.
    for(auto& wheel:s.wheels) {
        const float spin=float(wheel.spin);
        const bool contact=wheel.contact; // published Actor350, read by Driver
        wheel={};wheel.spin=spin;wheel.contact=contact;
        wheel.spin_direction=player_wheel_spin_direction(spin);
    }
    s.body_rest_state=0;s.body_gravity=true;s.grip_recovery=1.F;
    s.rigid_body={};s.alternate_yaw_active=false;
    player_rigid_body_import(h,s.rigid_body,
        {{float(s.position.x),float(s.position.y),float(s.position.z)}},
        s.owner_motion.axes,
        {{float(s.velocity.x),float(s.velocity.y),float(s.velocity.z)}},
        {{float(s.angular_velocity.x),float(s.angular_velocity.y),float(s.angular_velocity.z)}});
    player_rigid_body_refresh(h,s.rigid_body);
    const auto& v=s.rigid_body.velocity;const auto& w=s.rigid_body.angular_velocity;
    s.velocity={v[0],v[1],v[2]};s.angular_velocity={w[0],w[1],w[2]};
    s.rigid_body_ready=true;s.rigid_orientation=s.orientation;
    player_engine_reset(s.engine,float(s.engine_rpm));
    s.engine.angular_speed=std::clamp(s.engine.angular_speed,
        h.idle_rpm*kPlayerRpmToRadS,h.max_rpm*kPlayerRpmToRadS);
    s.engine.heat_time=float(clock)*.00025F;
    s.gear=s.published_gear;
    s.transmission={};
    s.transmission.requested_gear=s.gear;
    s.transmission.mode=3;
    s.transmission.clutch=1.F;
    // 476F13 refreshes the new engine before its first derivative.
    player_engine_cutoff_update(h,s.gear,s.engine,float(clock)*.00025F);

    // PC 476F13..476F2F and GC 80080898..800808A0 refresh the newly
    // constructed components before the driver's first steering input.
    s.steer_angle=0;
    s.steering_word=0;
    s.steering_word_ready=false;
    VehicleTelemetry telemetry;
    VehicleInput initial;
    initial.world_clock=clock;
    initial.world_clock_valid=true;
    if(!model_->step(s,initial,ground_,telemetry,0,0,false,true,true,true,0,0,
                     VehicleSteeringState::before_first_command))return false;
    // 476F13 prepares the mover. Only the following 4789D0 publishes motion.
    s.velocity=state.velocity;
    s.angular_velocity=state.angular_velocity;
    s.motion_before_first_move=VehiclePublishedMotion{s.velocity,s.angular_velocity};
    s.steering_word_ready=false;
    state=s;
    return true;
}

bool VehicleTrackSession::rival_nitrous_rpm_allowed(bool drag) const noexcept {
    const auto gear=unsigned(state_.published_gear);
    if(gear>=kVehicleGearSlots)return false;
    const auto& handling=model_->spec().handling;
    const float down=ai_active_?ai_tables_.down[gear]:
        model_->request_downshift_rpm()[gear]+state_.transmission.down_offset;
    const float up=ai_active_?ai_tables_.up[gear]:
        std::min(drag?handling.max_rpm:handling.redline_rpm,
            model_->request_upshift_rpm()[gear]+state_.transmission.up_offset);
    // 44CB90 refuses the engine outside the lower shift threshold and
    // 95 percent of the way to the upper threshold.
    const float rpm=float(state_.engine_rpm);
    return rpm>=down&&rpm<=(up-down)*.949999988079071F+down;
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
       input.shift!=VehicleShift::none||(input.nitrous_active&&!model_->spec().nitrous.installed)||
       (gear_request!=-1&&gear_request!=0))return false;
    const auto& spec=model_->spec();const auto& h=spec.handling;
    auto s=state_;auto next=ai_;VehicleTrackTelemetry t;
    if(!refresh_only)update_car_collision_state(s.collision_state,s.collision_support_since,
        s.published_support,s.collision_body_flag,double(input.world_clock_valid?input.world_clock:static_cast<std::int32_t>((ticks_*4000)/60))*double(.00025F));
    const auto world_tick=input.world_clock_valid?input.world_clock:static_cast<std::int32_t>((ticks_*4000)/60);
    if(!ai_started_) {
        if(!reconstruct_ai_physics(s,input,gear_request,world_tick,t))return false;
        next=ai_;
    }
    const auto actor_snapshot=published_actor?*published_actor:s;
    const auto old=s.rigid_body;
    const auto same=[](const VehicleVec3& a,const VehicleVec3& b) {
        return a.x==b.x&&a.y==b.y&&a.z==b.z;
    };
    const auto& q=s.orientation;const auto& prior_q=ai_import_.orientation;
    const bool unchanged_import=ai_import_.pending&&!published_actor&&
        same(s.position,ai_import_.position)&&same(s.velocity,ai_import_.velocity)&&
        same(s.angular_velocity,ai_import_.angular_velocity)&&
        q.w==prior_q.w&&q.x==prior_q.x&&q.y==prior_q.y&&q.z==prior_q.z;
    //474A90 begins with the constructor's prepared derivative, without
    //publishing/restoring the old actor velocity. Explicit contact poses and
    //real external edits still pass through the ordinary public-body adapter.
    if(unchanged_import)s.rigid_body=next.body;
    else vehicle_import_rigid(h,s);
    next.body=s.rigid_body;
    //The selector3 constructor/outer path leaves rigid+1E0 at zero.
    //477C32's player-only yaw torque cap must not leak through the shared
    //VehicleState import into traffic or rivals after an impact.
    next.body.yaw_torque_limit=0;
    next.collision_context_valid=true;
    //469390 rewinds the rigid body alone.474DD0 still reads actor+70/+90
    //from the endpoint, until474730 publishes the completed continuation.
    const auto& actor=actor_snapshot;
    if(continuation_dt<0)extend_owner_recovery(world_tick);
    next.collision_context={s.collision_state,s.published_support,race_contact_axes(actor),
        {{float(actor.velocity.x),float(actor.velocity.y),float(actor.velocity.z)}},s.published_gear,float(input.throttle),
        preparing_grid_||owner_recovery_active(world_tick)};
    if(continuation_dt<0)ai_collision_policy(next.collision_context,next.body,next.aero);
    s.alternate_yaw_active=next.body.alternate_yaw;
    // Pair impulses and teleports occur outside this component's completion.
    if(!unchanged_import&&(old.axes!=next.body.axes||ai_.body.axes!=next.body.axes||
       old.position!=next.body.position||ai_.body.position!=next.body.position||old.momentum!=next.body.momentum||
       old.angular_momentum!=next.body.angular_momentum||
       ai_.body.momentum!=next.body.momentum||ai_.body.angular_momentum!=next.body.angular_momentum))
        next.prepared=false;
    next.engine.throttle=float(input.throttle);
    next.engine.extra_torque=input.nitrous_active;
    next.engine.extra_torque_scale=input.nitrous_active?spec.nitrous.rate:0.F;
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
            ai_=next;ai_started_=true;ai_import_={};state_=refreshed;telemetry=t;return true;
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
        ai_=next;ai_started_=true;ai_import_={};state_=s;t.position=s.position;t.orientation=s.orientation;
        t.velocity=s.velocity;t.angular_velocity=s.angular_velocity;telemetry=t;return true;
    }
    const unsigned slices=unsigned(frame_dt/.00839999970048666F)+1;
    s.step+=slices;s.simulation_seconds+=frame_dt==1.F/60.F?1.0/60.0:double(frame_dt);
    last_input_=input;last_ai_gear_request_=gear_request;
    // Reuse the port's route, reset zones and timing publication once per frame.
    steps_+=slices;++ticks_;t.step=steps_;t.tick=true;
    const auto generation=resets_;
    if(ai_route_tracking_)run_tick(s,input,t,3);
    if(ai_route_tracking_ && progress_.finished) {
        // Route location still feeds the live driver after timing has latched
        // the finish; do not emit another lap or overwrite the race result.
        const float xy[]={float(s.position.x),float(s.position.y)};
        update_track_path_coordinate(course_->route.spline,course_->track,progress_.coordinate,xy);
    }
    if(resets_==generation) {ai_started_=true;ai_=next;ai_import_={};}
    // A successful run_tick placement constructed its own prepared components.
    // Do not overwrite them with the solver that just left the old location.
    state_=s;
    t.pending=pending_;t.resets=resets_;t.position=s.position;t.orientation=s.orientation;
    t.velocity=s.velocity;t.angular_velocity=s.angular_velocity;
    t.model.rigid_body=s.rigid_body;t.model.rigid_body_ready=s.rigid_body_ready;
    t.model.rigid_orientation=s.rigid_orientation;t.model.steering_word=s.steering_word;
    t.model.steering_word_ready=s.steering_word_ready;
    vehicle_effect_telemetry(s,frame_dt,t.model);
    t.lap=progress_.lap;t.percent=progress_.coordinate.fraction*100.F;t.zone=progress_.zone;
    t.off_path=progress_.off_path;telemetry=t;return true;
}
} // namespace nfsu
