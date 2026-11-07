#include "nfsu/ai_powertrain.h"
#include "nfsu/player_powertrain.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nfsu {
namespace {
float norm(const PlayerRigidVector& v) noexcept {
    return std::sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
}
PlayerRigidVector rotate(const PlayerRigidMatrix& m,const PlayerRigidVector& v) noexcept {
    PlayerRigidVector out{};
    for(unsigned k=0;k<3;++k) out[k]=((v[2]*m[2][k]+v[1]*m[1][k])+v[0]*m[0][k])+0.F;
    return out;
}
PlayerRigidVector arm(const VehicleHandling& h,unsigned k,const PlayerRigidMatrix& m) noexcept {
    const auto& w=h.wheels[k];
    return rotate(m,{{w.longitudinal-h.centre_of_mass[0],
                      w.lateral-h.centre_of_mass[1],w.vertical-h.centre_of_mass[2]}});
}
std::array<float,2> sincos(float radians) noexcept {
    const float scaled=radians*10430.3779296875F;
    const auto convert=[](float v){return static_cast<std::uint16_t>(static_cast<std::int64_t>(v));};
    return {{player_angle_sin(convert(scaled)),player_angle_sin(convert(scaled+16383.9990234375F))}};
}
template<std::size_t N> float curve(const std::array<float,N>& values,float low,float high,float x) noexcept {
    const float scale=float(N-1)/(high-low);
    const float index=(x-low)*scale;
    if(index<=-1)return values[0];
    if(index>=N-1)return values[N-1];
    const auto i=static_cast<unsigned>(static_cast<int>(index));
    float base=float(i);if(base>index)base-=1;
    const float f=index-base;
    return (1-f)*values[i]+f*values[i+1];
}
float feedback_speed_curve(float speed) noexcept {
    //705E78 ->705E60,583F20 interpolation with the source operation order.
    if(speed<=0)return .05F;
    if(speed>=20)return .5F;
    const float x0=speed<=2?0.F:2.F,x1=speed<=2?2.F:20.F;
    const float y0=speed<=2?.05F:.322F,y1=speed<=2?.322F:.5F;
    return ((speed-x0)/(x1-x0))*(y1-y0)+y0;
}
}

bool ai_setup_acceleration_curve(const VehicleHandling& h,std::array<float,64>& acceleration) noexcept {
    if(h.forward_gears<1||h.forward_gears+2>kVehicleGearSlots||!(h.mass>0))return false;
    AiPowertrainTables tables;
    if(!player_request_shift_tables(h,tables.up,tables.down))return false;
    const float front=1.F-h.rear_drive_fraction;
    const float radius=h.rear_drive_fraction*h.wheels[3].radius+front*h.wheels[0].radius;
    const float final=front*h.final_drive_rear+h.rear_drive_fraction*h.final_drive_front;
    if(!(radius>0)||!std::isfinite(final))return false;
    const float inverse=1.F/radius;
    constexpr float step=1.3410818576812744F,maximum=98.34600067138672F;
    std::array<float,75> samples{};
    unsigned count=0,gear=2;float speed=0;
    while(count<samples.size()) {
        const float rpm=(((inverse*speed)*h.gear_ratios[gear])*final)*9.549304008483887F;
        if(!std::isfinite(rpm))return false;
        if(rpm>=tables.up[gear]) {
            if(++gear>=h.forward_gears+2)break;
            //581C04 advances the gear but retains this sample's old RPM.
        }
        const float torque=curve(h.torque,h.idle_rpm,h.max_rpm,rpm);
        const float force=(((torque*h.gear_ratios[gear])*final)*inverse)/h.mass;
        if(!std::isfinite(force))return false;
        samples[count++]=force;speed+=step;
    }
    //581C64 decrements the count before clearing the remaining table.
    if(count)for(unsigned i=count-1;i<samples.size();++i)samples[i]=0;
    for(unsigned i=0;i<acceleration.size();++i) {
        const float x=(maximum*float(i))/63.F;
        acceleration[i]=curve(samples,0.F,maximum,x);
    }
    return true;
}

bool ai_setup_predicted_speed(const VehicleHandling& h,float& speed) noexcept {
    if(!std::isfinite(h.drag)||h.drag<0.F)return false;
    std::array<float,64> acceleration{};
    if(!ai_setup_acceleration_curve(h,acceleration))return false;
    constexpr float step=1.3410818576812744F,maximum=98.34600067138672F;
    float candidate=0.F;
    for(;candidate<=maximum;candidate+=step) {
        const float engine=curve(acceleration,0.F,maximum,candidate);
        const float resistance=((candidate*h.drag)*candidate)/h.mass;
        if(engine<resistance)break;
    }
    speed=candidate;
    return true;
}

bool ai_setup_performance(const VehicleHandling& h,float& rating,float& low_slip) noexcept {
    std::array<float,64> acceleration{};
    if(!ai_setup_acceleration_curve(h,acceleration))return false;
    constexpr float step=1.3410818576812744F,maximum=98.34600067138672F;
    float area=0;
    for(float x=0;x<=maximum;x+=step) {
        const float engine=curve(acceleration,0.F,maximum,x);
        const float drag=((x*h.drag)*x)/h.mass;
        if(!(engine>drag))break;
        area=(engine-drag)*step+area;
    }
    float normalized=((area-140.F)*10.F)/(771.F-140.F);
    normalized=std::clamp(normalized,0.F,10.F);
    const float coefficient=((.5F-.25F)*normalized)*.10000000149011612F+.25F;
    if(!std::isfinite(area)||!std::isfinite(coefficient))return false;
    rating=area;low_slip=coefficient;return true;
}

void ai_ground_clear(AiWheelGround& ground) noexcept {
    ground.height=ground.hub[2];
    ground.normal={{0,0,-1}};
}

void ai_powertrain_struts(const VehicleHandling& h,const PlayerRigidBodyState& body,
                          const std::array<AiWheelGround,4>& ground,float dt,
                          AiPowertrainState& state) noexcept {
    for(unsigned k=0;k<4;++k) {
        auto& w=state.wheels[k];const auto& g=ground[k];const auto& s=h.struts[k/2];
        w.normal=g.normal;
        const float alignment=(g.normal[2]*body.axes[2][2]+body.axes[2][1]*g.normal[1])+
            g.normal[0]*body.axes[2][0];
        float compression=alignment<.001F?-100.F:(g.height-g.hub[2])/alignment;
        compression+=h.wheels[k].radius;
        const float old=w.compression;
        // 475A5A and 475A9B reject only values below the respective limits.
        if(compression<s.droop_limit) {w.compression=s.droop_limit;w.contact=false;w.excess=0;}
        else {
            w.contact=true;
            w.compression=std::min(compression,s.bump_limit);
            w.excess=compression>s.bump_limit?compression-w.compression:0.F;
        }
        w.compression_rate=(w.compression-old)/std::max(dt,.0001F);
        w.load=0;
        if(w.contact) {
            const float spring=(std::max(w.compression,0.F)*s.progressive+1.F)*s.spring;
            const float damp=w.compression_rate*(w.compression_rate>0?s.damping_bump:s.damping_rebound);
            float f=damp+spring*w.compression;
            f+=w.preload;
            f+=(w.compression-state.wheels[k^1U].compression)*s.anti_roll;
            w.load=std::max(f,0.F);
        }
    }
}

void ai_powertrain_feedback(const VehicleHandling& h,PlayerRigidBodyState& body,
                            AiPowertrainState& state) noexcept {
    bool traction_override=false;
    const float body_heading=float(player_velocity_angle(body.axes[0][0],body.axes[0][1]))*9.58738019107841e-5F;
    for(unsigned k=0;k<4;++k) {
        auto& w=state.wheels[k];const auto point=arm(h,k,body.axes);
        w.sliding=false;PlayerRigidVector local{};
        if(w.contact) {
            const float vx=body.velocity[0]-point[1]*body.angular_velocity[2];
            const float vy=point[0]*body.angular_velocity[2]+body.velocity[1];
            const float steer=k<2?float(state.steering)*9.58738019107841e-5F:0.F;
            const auto sc=sincos(body_heading+steer);
            const float lat=sc[1]*vy-sc[0]*vx;
            const float longitudinal=(sc[0]*vy+sc[1]*vx)-h.wheels[k].radius*w.spin;
            w.slip={{longitudinal,lat}};
            const float magnitude=std::sqrt(longitudinal*longitudinal+lat*lat);
            float scale=magnitude>=.05F*state.grip?state.low_slip*state.grip:1.F;
            // Original steep-normal multiplier6B71DC is1, retained explicitly.
            if(w.normal[2]<=.5) scale*=1.F;
            const auto difference=static_cast<std::int32_t>(static_cast<std::uint32_t>(state.traction_until)-
                                                          static_cast<std::uint32_t>(state.world_time));
            const float remaining=float(difference)*.0002500000118743628F;
            if(remaining>0) {scale*=std::min(remaining,1.F)*-.6000000238418579F+1.F;traction_override=true;}
            w.sliding=magnitude>6.F;
            scale*=w.load;
            const float fx=-((1.25F*scale)*longitudinal);
            const float fy=-((scale*1.25F)*lat);
            const auto steering=sincos(steer);
            local[0]=fx*steering[1]-fy*steering[0];
            local[1]=fx*steering[0]+fy*steering[1];
        }
        local[2]=w.load;
        auto force=rotate(body.axes,local);
        if(w.contact) {
            const float magnitude=std::sqrt(force[1]*force[1]+force[0]*force[0]);
            const float limit=(4.F*w.load)*state.grip;
            if(magnitude>limit) {const float scale=limit/magnitude;force[0]*=scale;force[1]*=scale;}
            const float boost=state.boost>1?(state.boost-1.F)*2.5F+1.F:1.F;
            force[0]*=boost;force[1]*=boost;
            if(w.locked||(state.brake>.4F&&norm(body.velocity)<=.1F)) {
                const float f=w.load/body.axes[2][2];
                force[0]+=-(f*body.axes[2][0]);force[1]+=-(f*body.axes[2][1]);
            } else if(magnitude<=limit) {
                const float f=w.load/body.axes[2][2];
                force[0]+=-(std::fabs(body.axes[1][0])*body.axes[2][0]*f);
                force[1]+=-(std::fabs(body.axes[1][1])*body.axes[2][1]*f);
            }
        }
        if(traction_override) {force[0]=0;force[1]=0;}
        player_rigid_body_add_contact_force(body,{force[0],force[1],force[2]},
                                                {point[0],point[1],point[2]});
        w.feedback={{local[0],local[1]}};
        const float magnitude=std::sqrt(w.feedback[1]*w.feedback[1]+w.feedback[0]*w.feedback[0]);
        if(magnitude>50) {const float f=50.F/magnitude;for(auto& x:w.feedback)x*=f;}
    }
}

std::array<float,4> ai_powertrain_derivative(const VehicleHandling& h,
                                             const PlayerRigidBodyState& body,
                                             const AiPowertrainState& state) noexcept {
    std::array<float,4> result{};
    for(unsigned k=0;k<4;++k) {
        const auto& w=state.wheels[k];if(w.locked)continue;
        const float share=k<2?1.F-h.rear_drive_fraction:h.rear_drive_fraction;
        const float rpm=state.rpm/state.boost;
        const float torque=curve(h.torque,h.idle_rpm,h.max_rpm,rpm)*state.throttle;
        float net=torque-(1.F-state.throttle)*curve(h.engine_brake,h.idle_rpm,h.max_rpm,rpm);
        if(state.extra_torque) {
            constexpr std::array<float,9> extra{{.3F,.4F,.45F,.5F,.55F,.6F,.7F,1.F,.7F}};
            net+=curve(extra,h.idle_rpm,h.redline_rpm,rpm)*state.extra_torque_scale;
        }
        if(state.throttle<.05F&&state.rpm==h.idle_rpm)net=0;
        const float axle=k<2?h.final_drive_front:h.final_drive_rear;
        const float drive=(((axle*h.gear_ratios[state.gear])*net)*share)*.5F;
        const float brake=((2.5F*state.brake)*std::clamp(w.spin,-20.F,20.F))*-.2F;
        const float scale=state.boost>1?(state.boost-1.F)*3.F+1.F:1.F;
        const float feedback_scale=state.boost>1?((state.boost-1.F)*2.5F+1.F)*1.25F:1.25F;
        float reaction=feedback_speed_curve(norm(body.velocity));
        if(share>0)reaction=(reaction/(scale*feedback_scale))*share;
        result[k]=((scale*drive+brake)-reaction*w.feedback[0])/.008F;
    }
    return result;
}

void ai_powertrain_complete(const VehicleHandling& h,const AiPowertrainTables& tables,
                            AiPowertrainState& state) noexcept {
    for(auto& w:state.wheels) {
        if(state.brake>.95F&&std::fabs(w.spin)<=30.F){w.locked=true;w.spin=0;}
        if(state.brake<=.2F)w.locked=false;
    }
    // Preserve475858/875/8A4's axle choices and order; do not infer a new
    // formula from the descriptive names of the two setup offsets.
    const auto front=[&]{return ((state.wheels[1].spin+state.wheels[0].spin)*h.final_drive_rear)*.5F*9.549304008483887F;};
    const auto rear=[&]{return ((state.wheels[3].spin+state.wheels[2].spin)*h.final_drive_front)*.5F*9.549304008483887F;};
    const float wheel=h.rear_drive_fraction<=.2F?front():h.rear_drive_fraction>.8F?rear():(front()+rear())*.5F;
    float rpm=wheel*h.gear_ratios[state.gear];
    if(state.gear_request==-1) {
        if(state.gear<2)state.gear=2;
        const int top=int(h.forward_gears)+1;
        while(state.gear<top&&rpm>tables.up[state.gear]) {++state.gear;rpm=wheel*h.gear_ratios[state.gear];}
        while(state.gear>2&&rpm<=tables.down[state.gear]) {--state.gear;rpm=wheel*h.gear_ratios[state.gear];}
    } else if(state.gear_request==0) {state.gear=0;rpm=wheel*h.gear_ratios[0];}
    state.rpm=std::clamp(rpm,h.idle_rpm,h.max_rpm);
}

void ai_collision_policy(const AiCollisionContext& c,PlayerRigidBodyState& b,
                         PlayerAeroCache& aero) noexcept {
    //474ABF/474C64; other states retain the existing policy flags.
    if(c.state<=2) {
        b.linear_damping=b.angular_damping=b.alternate_yaw=c.state!=1;
        aero.enabled=c.state!=1;
    }
    if(c.support==0)b.alternate_yaw=false;
}

PlayerRigidVector ai_collision_stabilize(const AiCollisionContext& c,float mass,
                                         PlayerRigidBodyState& body) noexcept {
    PlayerRigidVector total{};
    if(c.state==1)return total;
    const auto apply=[&](const PlayerRigidVector& torque) {
        for(unsigned k=0;k<3;++k){body.torque[k]+=torque[k];total[k]+=torque[k];}
    };
    if(c.support<2) {
        const auto& w=body.angular_velocity;
        const float speed=std::sqrt((w[0]*w[0]+w[1]*w[1])+w[2]*w[2]);
        //705E98 ->705E80, selector3's own three-pair curve.
        const float damping=speed<=.1F?0.F:speed>=2.F?20.F:((speed-.1F)/(2.F-.1F))*20.F;
        const float factor=-(damping*mass);
        apply({factor*w[0],factor*w[1],factor*w[2]});
    }
    if(c.support!=0||!(c.actor_axes[2][2]>.1F))return total;
    const auto& v=c.actor_velocity;
    const float speed=std::sqrt(v[0]*v[0]+v[1]*v[1]);
    if(!(speed>.00001F))return total;
    const float inverse=1.F/speed;
    const PlayerRigidVector direction{{v[0]*inverse,v[1]*inverse,0.F*inverse}};
    const auto cross=[](const PlayerRigidVector& a,const PlayerRigidVector& b) {
        return PlayerRigidVector{{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};
    };
    const auto alignment=[&](const PlayerRigidVector& target,const PlayerRigidVector& axis) {
        const auto turn=cross(target,axis);
        const float projection=(target[1]*axis[1]+target[2]*axis[2])+target[0]*axis[0];
        const float factor=((projection-1.F)*mass)*100.F;
        if(factor<.00001F)apply({});
        else {
            //5673E0 normalizes to the requested magnitude, including its
            //zero-vector fallback; it is not simple scalar multiplication.
            const float length=std::sqrt((turn[0]*turn[0]+turn[1]*turn[1])+turn[2]*turn[2]);
            const float ratio=length/factor;
            if(ratio==0)apply({factor,0,0});
            else {const float scale=1.F/ratio;apply({turn[0]*scale,turn[1]*scale,turn[2]*scale});}
        }
    };
    alignment(direction,c.actor_axes[0]);
    auto left=cross(PlayerRigidVector{{0,0,1}},c.actor_axes[0]);
    const float length=std::sqrt((left[0]*left[0]+left[1]*left[1])+left[2]*left[2]);
    if(length!=0){const float scale=1.F/length;for(float& x:left)x*=scale;}
    else left={1,0,0};
    alignment(left,c.actor_axes[1]);
    return total;
}

bool ai_physics_advance(const VehicleHandling& h,const AiPowertrainTables& tables,
                        const PlayerInitialAeroConfiguration& config,float world_dt,
                        const AiPhysicsGround& ground,AiPhysicsState& state,bool refresh_only) noexcept {
    if(!std::isfinite(world_dt)||world_dt<0||(refresh_only&&world_dt!=0))return false;
    // Component methods require a decoded valid setup. Refuse invalid public
    // state before any gear-index access or float-to-integer curve lookup.
    if(h.forward_gears<1||h.forward_gears+2>kVehicleGearSlots||
       state.engine.gear<0||state.engine.gear>int(h.forward_gears)+1||
       !(state.engine.boost>0)||!std::isfinite(state.engine.boost))return false;
    for(float x:{state.engine.rpm,state.engine.throttle,state.engine.brake,
                 state.engine.grip,state.engine.low_slip,state.engine.extra_torque_scale,
                 state.body.adaptive_damping,state.body.yaw_torque_limit})
        if(!std::isfinite(x))return false;
    for(const auto& v:{state.body.position,state.body.momentum,state.body.angular_momentum,
                      state.body.velocity,state.body.angular_velocity,state.body.force,state.body.torque})
        for(float x:v)if(!std::isfinite(x))return false;
    for(const auto& axis:state.body.axes)for(float x:axis)if(!std::isfinite(x))return false;
    for(const auto& w:state.engine.wheels) {
        for(float x:{w.spin,w.compression,w.compression_rate,w.load,w.feedback[0],w.feedback[1],
                     w.slip[0],w.slip[1],w.preload,w.excess})if(!std::isfinite(x))return false;
        for(float x:w.normal)if(!std::isfinite(x))return false;
    }
    if(!std::isfinite(state.component_interval)||state.component_interval<0)return false;
    const float count=world_dt/.00839999970048666F;
    if(!std::isfinite(count)||count>=float(std::numeric_limits<int>::max()))return false;
    const int slices=static_cast<int>(count)+1;
    const float dt=world_dt/float(slices);
    auto next=state;
    const auto prepare=[&](float interval) {
        std::array<AiWheelGround,4> samples;
        if(!ground.query(h,next.body,samples))return false;
        for(const auto& s:samples) {
            if(!std::isfinite(s.height))return false;
            for(float x:s.hub)if(!std::isfinite(x))return false;
            for(float x:s.normal)if(!std::isfinite(x))return false;
        }
        next.ground_queries+=4;
        ai_powertrain_struts(h,next.body,samples,interval,next.engine);
        ai_powertrain_complete(h,tables,next.engine);
        if(!ground.resolve(h,next))return false;
        if(next.collision_context_valid) {
            //474CA0 calls shared47AC00 with EAX=0: rigid rest only;
            //selector1 wheel-spin mutations are deliberately not requested.
            const auto& c=next.collision_context;const auto& b=next.body;
            next.rest_state=player_body_rest_state(
                {b.velocity[0],b.velocity[1],b.velocity[2]},
                {b.angular_velocity[0],b.angular_velocity[1],b.angular_velocity[2]},
                b.axes[0][2],b.axes[1][2],c.throttle,c.published_gear,
                c.support,next.rest_state,c.recovery_active);
            next.body.gravity=next.rest_state!=2;
            if(next.rest_state!=0) {
                for(unsigned k=0;k<3;++k) {
                    next.body.momentum[k]=next.rest_state==2?0.F:next.body.momentum[k]*.95F;
                    next.body.angular_momentum[k]=next.rest_state==2?0.F:next.body.angular_momentum[k]*.95F;
                }
            }
            //474CA0 calls476240 after the rest mutation. Only excess strut
            //travel with inward hub motion produces an impulse.
            float bump_impulse=0.F;
            for(unsigned k=0;k<4;++k) {
                const auto& w=next.engine.wheels[k];
                if(w.excess<=0.F)continue;
                const auto& mount=h.wheels[k];
                const PlayerRigidVector hub{{mount.longitudinal-h.centre_of_mass[0],
                    mount.lateral-h.centre_of_mass[1],
                    (mount.vertical-h.centre_of_mass[2])+w.compression}};
                bump_impulse+=player_strut_bump_impulse(h,next.body,hub,w.normal);
            }
            if(next.rest_state!=0||bump_impulse>0.F) {
                //474CE8 refreshes components once after all impulses.
                player_rigid_body_refresh(h,next.body);
                if(!ground.query(h,next.body,samples))return false;
                next.ground_queries+=4;
                ai_powertrain_struts(h,next.body,samples,interval,next.engine);
                ai_powertrain_complete(h,tables,next.engine);
            }
            if(next.rest_state==2)next.engine.brake=1.F;
        }
        //474D10: feedback first, then462F90 aero. Both use the refreshed
        // body snapshot, before the outer40BD40 normalization.
        ai_powertrain_feedback(h,next.body,next.engine);
        player_aero_apply(h,next.body,next.aero,config);
        if(next.collision_context_valid)ai_collision_stabilize(next.collision_context,h.mass,next.body);
        next.prepared=true;
        next.component_interval=interval;
        return true;
    };
    if(refresh_only||!next.prepared) {
        player_rigid_body_refresh(h,next.body);
        if(!prepare(next.component_interval))return false;
    }
    if(refresh_only){state=next;return true;}
    for(int i=0;i<slices;++i) {
        //46AB70 collects every derivative BEFORE46AA40 integrates any state.
        // The constructor list puts the rigid component before the engine;
        //46ABD0 refreshes body caches before4757D0 queries the new pose.
        const auto rates=ai_powertrain_derivative(h,next.body,next.engine);
        player_rigid_body_integrate(h,next.body,dt);
        for(unsigned k=0;k<4;++k)next.engine.wheels[k].spin+=dt*rates[k];
        if(!prepare(dt))return false;
        player_rigid_body_normalize(next.body);
        ++next.slices;
    }
    for(const auto& v:{next.body.position,next.body.momentum,next.body.angular_momentum,
                      next.body.velocity,next.body.angular_velocity,next.body.force,next.body.torque})
        for(float x:v)if(!std::isfinite(x))return false;
    for(const auto& axis:next.body.axes)for(float x:axis)if(!std::isfinite(x))return false;
    for(const auto& w:next.engine.wheels)
        for(float x:{w.spin,w.compression,w.compression_rate,w.load,w.feedback[0],w.feedback[1],w.slip[0],w.slip[1]})
            if(!std::isfinite(x))return false;
    if(!std::isfinite(next.engine.rpm))return false;
    state=next;return true;
}
} // namespace nfsu
