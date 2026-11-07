#include "vehicle_runtime_oracles.h"
#include <cmath>
#include <iostream>

namespace candidate_oracles {
std::vector<GearCopy> gears;
std::vector<Strut> struts;
std::vector<Contact> contacts;
std::vector<Support> support;
bool capture_state=false,capture_support=false;
unsigned failures=0,distributions=0,parking_calls=0,service_calls=0;
void check(bool ok,const char* label) {
    if(!ok) {
        ++failures;
        if(failures<=12)std::cerr<<"oracle failure: "<<label<<'\n';
    }
}
void begin_step() { capture_state=true;gears.clear();struts.clear();contacts.clear();support.clear(); }
void end_step(const nfsu::VehicleState& before,const nfsu::VehicleState& after,
              const nfsu::VehicleInput& input) {
    int current=before.gear;
    for(const auto& event:gears) {
        check(event.before==current,"physical gear changes only in observed callbacks");
        check(event.after==(event.mode==1?event.request:event.before),
              "original46DE9A mode1 request copy; other phases retain gear");
        current=event.after;
    }
    check(!gears.empty()&&current==after.gear,"all step gear changes attributed");
    unsigned count=0;
    for(const auto& wheel:after.wheels)count+=wheel.contact?1U:0U;
    check(contacts.size()>=count,"completed force callback applies every contacting wheel");
    if(count&&contacts.size()>=count) {
        unsigned observed=0;
        for(unsigned k=0;k<4;++k) {
            if(after.wheels[k].contact) {
                const auto& event=contacts[contacts.size()-count+observed++];
                const auto& point=after.wheels[k].contact_point;
                // The accumulator passed to584CD0 contains force/torque
                // alone, so its default zero position is not the live body.
                const auto& position=after.rigid_body.position;
                check(event.arm.x==float(point[0]-position[0])&&
                      event.arm.y==float(point[1]-position[1])&&
                      event.arm.z==float(point[2]-position[2]),
                      "478480 uses actual cached hub-radius-normal contact arm");
            }
        }
    }
    if(struts.size()>=4 && after.wheels[0].contact && after.wheels[1].contact &&
       after.wheels[2].contact && after.wheels[3].contact) {
        for(unsigned k=0;k<4;++k) {
            const auto& event=struts[struts.size()-4+k];
            const auto& partner=after.wheels[k^1U];
            check(event.x==float(after.wheels[k].compression)&&event.partner==
                  float(k%2==0?partner.previous_compression:partner.compression),
                  "ascending strut callback consumes first-old/second-new partner");
            //588580 preserves the cached rate at dt0. A rest/bump refresh
            //has already copied the current compression into its previous
            //slot, so their difference is no longer the rate's producer.
            check(event.preload==after.aero_preloads[k]&&
                  event.rate==after.wheels[k].compression_rate,
                  "strut callback uses held preload and the completed cached rate");
        }
    }
    check(after.published_gear==after.gear,"outer publication copies physical gear");
    if(input.handbrake) check(parking_calls>0,"runtime exercises separate rear parking path");
    capture_state=false;
}
}

namespace nfsu {
// Test-only redirections observe operands and forward unchanged. Accepted
// original operation evidence:46DE9A/5889EC/46E020/584CD0/587330 under007F.
// Existing Player tests pin original values; these check runtime wiring.
void nfsu_candidate_gear_update(const VehicleHandling& h,int& gear,PlayerGearState& box,
    PlayerEngineState& engine,std::array<PlayerWheelWork,4>& wheels,float gas,float speed,
    const std::array<float,4>* offset_bounds,std::uint32_t* world_random,
    bool assisted,bool clutch_override) noexcept {
    const candidate_oracles::GearCopy entry{gear,box.requested_gear,box.mode,gear};
    player_gear_update(h,gear,box,engine,wheels,gas,speed,offset_bounds,world_random,
                       assisted,clutch_override);
    auto result=entry;result.after=gear;if(candidate_oracles::capture_state)candidate_oracles::gears.push_back(result);
}
float nfsu_candidate_strut_force(const VehicleAxleStrut& h,float x,float rate,float partner,
    float preload,float overflow,float bump) noexcept {
    //5889EC performs damping+spring, preload, then the grouped partner+bump.
    // This independent expression consumes observed callback operands, not
    // post-step telemetry or the helper's answer as its expected value.
    float expected=(rate>0?h.damping_bump:h.damping_rebound)*rate+
                   ((h.progressive*(x>0?x:0)+1.0F)*h.spring)*x;
    expected+=preload;expected+=(x-partner)*h.anti_roll+bump*overflow;
    if(expected<0)expected=0;
    const float actual=player_strut_nominal_force(h,x,rate,partner,preload,overflow,bump);
    candidate_oracles::check(actual==expected,"original binary32 strut expression");
    if(candidate_oracles::capture_state)candidate_oracles::struts.push_back({x,rate,partner,preload,overflow,bump,actual});
    return actual;
}
void nfsu_candidate_distribute(const VehicleHandling& h,int gear,PlayerEngineState& engine,
    std::array<PlayerWheelWork,4>& wheels,float transmitted,int owner_mode) noexcept {
    auto expected=wheels;const float feedback=engine.feedback_torque;
    if(gear!=kVehicleGearNeutral) {
        const auto share=[owner_mode](float numerator,float sum,bool contact) {
            const float edge=contact?.35F:.5F;
            if(owner_mode==1)return 1.F-edge;
            if(owner_mode==2)return edge;
            if(std::fabs(sum)<=1.0e-5F)return .5F;
            float value=numerator/sum;
            if(value<edge)value=1.0F-.35F*.5F;
            else if(value>1.0F-edge)value=.35F*.5F;
            return value;
        };
        const float ratio=transmitted*h.gear_ratios[static_cast<unsigned>(gear)];
        const float front=((1.0F-h.rear_drive_fraction)*h.final_drive_rear)*ratio;
        const float rear=(ratio*h.final_drive_front)*h.rear_drive_fraction;
        const float right=share(wheels[1].spin,wheels[0].spin+wheels[1].spin,
                                wheels[0].contact&&wheels[1].contact);
        const float left=share(wheels[2].spin,wheels[2].spin+wheels[3].spin,
                               wheels[2].contact&&wheels[3].contact);
        const float add[]{(1.0F-right)*front,right*front,left*rear,(1.0F-left)*rear};
        for(unsigned k=0;k<4;++k)if(!expected[k].locked)expected[k].torque+=add[k];
    }
    player_distribute_torque(h,gear,engine,wheels,transmitted,owner_mode);
    for(unsigned k=0;k<4;++k)candidate_oracles::check(wheels[k].torque==expected[k].torque,
        "46E020 axle finals/differential/lock-aware transmitted torque");
    candidate_oracles::check(engine.feedback_torque==
        (gear==kVehicleGearNeutral?feedback:feedback-transmitted),"46E020 engine feedback");
    ++candidate_oracles::distributions;
}
void nfsu_candidate_contact_force(PlayerRigidBodyState& body,const VehicleVec3& force,
                                  const VehicleVec3& arm) noexcept {
    if(candidate_oracles::capture_state)candidate_oracles::contacts.push_back({arm});
    player_rigid_body_add_contact_force(body,force,arm);
}
void nfsu_candidate_wheel_prepare(const VehicleHandling& h,std::size_t k,PlayerWheelWork& wheel) noexcept {
    candidate_oracles::check(wheel.parking==0.0F|| (k>=2&&wheel.parking==1.5F),
                            "477953 parking input is rear-only1.5");
    if(wheel.parking)++candidate_oracles::parking_calls;
    if(wheel.service)++candidate_oracles::service_calls;
    float expected=wheel.torque;
    const float service=(h.service_brake*wheel.service)*4.0F;
    const float parking=(h.parking_brake*wheel.parking)*10.0F;
    if(!wheel.locked) {
        expected+=wheel.spin>0?-service:service;
        expected+=wheel.spin>0?-parking:parking;
    }
    const float slip=h.wheels[k].radius*wheel.spin-wheel.velocity_long;
    if(slip<=-.5F&&expected<0&&parking==0)expected*=.15F;
    //587330/587220 has a capacity lock and an independent raw-slip mode.
    // Fixed original material witnesses in Player tests pin these operations.
    const auto before=wheel;float spin=before.spin;bool locked=before.locked,sliding=false;
    const float magnitude=std::sqrt(before.velocity_lat*before.velocity_lat+slip*slip);
    if(magnitude!=0&&(before.velocity_lat!=0||before.velocity_long!=0)) {
        const float friction=((before.surface.sliding_grip*h.tyres[k/2].sliding_friction)*before.friction_scale)*before.owner_grip;
        const float velocity=std::sqrt(before.velocity_long*before.velocity_long+before.velocity_lat*before.velocity_lat);
        const float demand=std::fabs(before.spin)*.1F+h.wheels[k].radius*(((1.F/velocity)*friction*before.load)*std::fabs(before.velocity_long));
        const float capacity=(parking+service)*1.2F;
        if(capacity>demand){spin=0;locked=capacity>1;}else locked=false;
        float angle=float(player_velocity_angle(before.velocity_long,before.velocity_lat))*9.58738019107841e-5F;
        if(angle>3.141590118408203F)angle-=6.283180236816406F;
        if(before.velocity_long<0){angle+=3.141590118408203F;if(angle>3.141590118408203F)angle-=6.283180236816406F;}
        sliding=std::fabs(slip)>=.5F||std::fabs(angle)>=h.tyres[k/2].slip_scale||locked;
    }
    player_wheel_prepare(h,k,wheel);
    candidate_oracles::check(wheel.spin==spin&&wheel.locked==locked&&wheel.sliding==sliding&&wheel.slip==slip&&wheel.cached_slip==slip,
                            "587330 capacity pin and raw-slip producer classification");
    candidate_oracles::check(wheel.torque==expected,
        "587330 separate service/parking gain, sign and parking-excluded ABS");
}
float nfsu_candidate_wheel_speed(const VehicleHandling& h,int gear,
                                 const std::array<float,4>& spins) noexcept {
    float expected=0;
    if(h.rear_drive_fraction>=.001F)expected=((spins[3]+spins[2])*.5F)*h.final_drive_front;
    if(h.rear_drive_fraction<.999F) {
        expected+=((spins[1]+spins[0])*.5F)*h.final_drive_rear;
        if(h.rear_drive_fraction>=.001F)expected*=.5F;
    }
    expected*=h.gear_ratios[static_cast<unsigned>(gear)];
    const float actual=player_wheel_engine_speed(h,gear,spins);
    candidate_oracles::check(actual==expected,"46D8B0 binary32 driven-axle mechanical speed");
    return actual;
}
PlayerRigidVector nfsu_candidate_low_support(const VehicleHandling& h,PlayerRigidBodyState& body,
                                            unsigned support,unsigned state) noexcept {
    const auto value=player_low_support_apply(h,body,support,state);
    if(candidate_oracles::capture_state||candidate_oracles::capture_support)
        candidate_oracles::support.push_back({support,state,value});
    if(support>=2||state==1||state==2)
        candidate_oracles::check(value==PlayerRigidVector{},"4791F0 uses published support/state gate");
    return value;
}

}

namespace candidate_oracles {
unsigned limits=0,binding_limits=0,derivatives=0,integrations=0;
}
namespace nfsu {
void nfsu_candidate_wheel_limit(const VehicleHandling& h,std::size_t k,PlayerWheelWork& w) noexcept {
    const auto before=w;
    float x=w.force_long,y=w.force_lat;
    const bool boosted=w.torque>0&&!w.locked;
    if(boosted)x*=1.5F;
    const float length=std::sqrt(x*x+y*y);
    const float cap=(((w.surface.static_grip*h.tyres[k/2].static_friction)*w.friction_scale)*w.owner_grip)*w.load;
    if(length>cap&&length>.001F) {const float factor=cap/length;x*=factor;y*=factor;++candidate_oracles::binding_limits;}
    else if(boosted)x/=1.5F;
    if(std::fabs(w.velocity_long)>1.F&&w.velocity_lat!=0) {
        float radians=float(player_velocity_angle(w.velocity_long,w.velocity_lat))*9.58738019107841e-5F;
        if(radians>3.141590118408203F)radians-=6.283180236816406F;
        if(w.velocity_long<0) {radians+=3.141590118408203F;if(radians>3.141590118408203F)radians-=6.283180236816406F;}
        const auto word=std::uint16_t(int(radians*10430.3779296875F));
        x-=(player_angle_sin(word)*y)*.5F;
    }
    player_wheel_limit(h,k,w);
    candidate_oracles::check(w.force_long==x&&w.force_lat==y,"587830 limits combined forces before lateral drag");
    candidate_oracles::check(w.torque==before.torque&&w.spin==before.spin&&w.history_torque==before.history_torque&&w.locked==before.locked&&w.sliding==before.sliding,"force limit does not clamp torque or change producer mode");
    ++candidate_oracles::limits;
}
float nfsu_candidate_wheel_acceleration(const VehicleHandling& h,std::size_t k,PlayerWheelWork& w,float dt,float rolling) noexcept {
    const float old_spin=w.spin;
    const float history=((w.torque-h.wheels[k].radius*w.force_long)+w.history_torque)*.5F;
    const float material=(.002F*w.surface.rolling_drag)*w.spin;
    const float drag=(rolling*.1F)*w.spin;
    float expected=((history-material)-drag)/kPlayerWheelInertia;
    if(w.locked)expected=0;
    if(!w.sliding&&dt>.001F)expected-=w.slip/(h.wheels[k].radius*dt);
    const float result=player_wheel_acceleration(h,k,w,dt,rolling);
    candidate_oracles::check(result==expected&&w.history_torque==history&&w.spin==old_spin,"587170 derivative consumes previous prepared force and filtered torque history");
    ++candidate_oracles::derivatives;return result;
}
void nfsu_candidate_wheel_integrate(PlayerWheelWork& w,float acceleration,float dt) noexcept {
    float expected=w.spin+dt*acceleration;
    if((w.spin_direction==0&&expected<0)||(w.spin_direction==2&&expected>0))expected=0;
    const int direction=expected>1.e-5F?0:expected< -1.e-5F?2:1;
    player_wheel_integrate(w,acceleration,dt);
    candidate_oracles::check(w.spin==expected&&w.spin_direction==direction,"586710 wheel update preserves original sign crossing clamp");
    ++candidate_oracles::integrations;
}
}
