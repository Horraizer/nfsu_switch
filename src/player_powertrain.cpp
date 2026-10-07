#include "nfsu/player_powertrain.h"
#include "nfsu/player_angle_data.h"
#include <cstring>

namespace nfsu {
namespace {

float absf(float x) noexcept { return x < 0.0F ? -x : x; }
float sqrtf(float x) noexcept { return static_cast<float>(vehicle_sqrt(x)); }

std::uint16_t asin_angle(float value) noexcept {
    if (value>=1.0F) return 0x4000U;
    const int scaled=static_cast<int>(value*65536.0F);
    int step=0x8000,base=step,segment=0;
    while (scaled>=base&&segment<11) {step>>=1;base+=step;++segment;}
    base-=step;
    const int bin=(scaled-base)>>(11-segment);
    const int sample_base=base+(step>>4)*bin;
    const auto sample=kPlayerAngleSamples[segment*16+bin];
    float slope=0.0F;std::memcpy(&slope,&sample.slope_bits,sizeof(slope));
    const float difference=value-static_cast<float>(sample_base)*1.52587890625e-5F;
    const int correction=static_cast<int>((difference*slope)*-65536.0F);
    return static_cast<std::uint16_t>(sample.angle-correction);
}

float slip_angle(float longitudinal,float lateral) noexcept {
    float angle=static_cast<float>(player_velocity_angle(longitudinal,lateral))*9.58738019107841e-5F;
    if (angle>3.1415927410125732F) angle-=6.2831854820251465F;
    if (longitudinal<0.0F) {
        angle+=3.1415927410125732F;
        if (angle>3.1415927410125732F) angle-=6.2831854820251465F;
    }
    return angle;
}

float differential_share(float numerator, float denominator, bool both_contact) noexcept {
    if (absf(denominator) <= 1.0e-5F) return 0.5F;
    float share = numerator / denominator;
    const float edge = both_contact ? 0.35F : 0.5F; // 700044
    if (share < edge) share = 1.0F - 0.35F * 0.5F;
    else if (share > 1.0F - edge) share = 0.35F * 0.5F;
    return share;
}

float wheel_slip(const VehicleHandling& h,const std::array<PlayerWheelWork,4>& w) noexcept {
    float sum=0.0F;int count=0;
    if (h.rear_drive_fraction>=0.001F) {sum=w[3].slip+w[2].slip;count=2;}
    if (h.rear_drive_fraction<0.999F) {sum+=w[1].slip+w[0].slip;count+=2;}
    return sum/static_cast<float>(count); //46D830: setup validation ensures an axle
}

float wheel_speed(const VehicleHandling& h,int gear,const std::array<PlayerWheelWork,4>& w) noexcept {
    return player_wheel_engine_speed(h,gear,{{w[0].spin,w[1].spin,w[2].spin,w[3].spin}});
}

float random_unit(std::uint32_t& state) noexcept {
    const auto remainder=state%0x7FFFFFFFU;
    const auto mixed=state^0x1D872B41U;
    const auto shifted=(mixed>>5)^mixed;
    state=(shifted<<27)^shifted^mixed;
    return static_cast<float>(remainder)*4.656612873077393e-10F; //6B5E7C
}

// Constructor stores (count-1)/(max-idle) as binary32. 583EA0 rounds its
// arithmetic at the witness's declared x87 precision, including the original
// truncation-versus-floor quirk between indices -1 and0.
float curve(const float* values, int count, float idle, float maximum,
            float rpm) noexcept {
    const float scale = static_cast<float>(count - 1) / (maximum - idle);
    const float u = (rpm - idle) * scale;
    if (u <= -1.0F) return values[0];
    if (u >= static_cast<float>(count - 1)) return values[count - 1];
    const int index = static_cast<int>(u);
    float base = static_cast<float>(index);
    if (base > u) base -= 1.0F;
    const float fraction = u - base;
    const float left = (1.0F - fraction) * values[index];
    const float right = fraction * values[index + 1];
    return left + right;
}

float standing_lateral(const VehicleHandling& h,std::size_t k,const PlayerWheelWork& w) noexcept {
    static constexpr float curves[7][6]={
        {0,0,0,0,0,0},{0,1.2F,2.3F,3,3,2.8F},{0,1.7F,3.2F,4.3F,5.1F,5.2F},
        {0,1.8F,3.5F,4.9F,5.8F,6.1F},{0,1.83F,3.6F,5,5.96F,6.4F},
        {0,1.86F,3.7F,5.1F,6.13F,6.7F},{0,1.9F,3.8F,5.2F,6.3F,7.1F}};
    const float angle=slip_angle(w.velocity_long,w.velocity_lat);
    const auto& tyre=h.tyres[k/2];
    //587779..587819: radians -> degrees / setup+0 *12 *0.5,
    // then two load curves and the original operation order.
    const float parameter=((absf(angle)*57.2957763671875F)/tyre.slip_scale*12.0F)*0.5F;
    const float load=(0.8F*w.load)/tyre.lateral_load_scale;
    const int index=static_cast<int>(parameter);
    float value=curve(curves[index<6?index:6],6,0.0F,10.0F,load);
    if (index<6) {
        const float next=curve(curves[index+1],6,0.0F,10.0F,load);
        value=(next-value)*(parameter-static_cast<float>(index))+value;
    }
    value=(value*tyre.lateral_scale)*2.5F;
    const float angle_sign=angle>0.0F?-1.0F:1.0F;
    const float forward_sign=w.velocity_long>0.0F?1.0F:-1.0F;
    return (value*angle_sign)*forward_sign;
}

} // namespace

std::uint16_t player_velocity_angle(float longitudinal,float lateral) noexcept {
    const float x=absf(longitudinal),y=absf(lateral);
    const float square=longitudinal*longitudinal+lateral*lateral;
    std::uint16_t angle=0;
    if (y<x) {
        const float normalized=square>1.0e-4F&&lateral!=0.0F?y/sqrtf(square):0.0F;
        angle=asin_angle(normalized);
    } else {
        const float normalized=square>1.0e-4F&&longitudinal!=0.0F?x/sqrtf(square):0.0F;
        angle=static_cast<std::uint16_t>(0x4000U-asin_angle(normalized));
    }
    if (longitudinal<0.0F) angle=static_cast<std::uint16_t>(0x8000U-angle);
    if (lateral<0.0F) angle=static_cast<std::uint16_t>(0U-angle);
    return angle;
}

float player_angle_sin(std::uint16_t angle) noexcept {
    float x=static_cast<float>(angle)*9.58738019107841e-5F;
    float sign=1.0F;
    if (x>=4.71238899230957F) x-=6.2831854820251465F;
    else if (x>=1.5707963705062866F) {x-=3.1415927410125732F;sign=-1.0F;}
    const float x2=x*x,x3=x2*x,x5=x3*x2,x7=x5*x2,x9=x7*x2;
    float result=x-x3*0.1666666716337204F;
    result+=x5*0.008333333767950535F;
    result-=x7*0.00019841270113829523F;
    result+=x9*2.7557318844628753e-6F;
    return result*sign;
}

PlayerOwnerSpeeds player_publish_owner_speeds(const VehicleVec3& velocity,
                                            const VehicleVec3& forward) noexcept {
    const float x=static_cast<float>(velocity.x),y=static_cast<float>(velocity.y),z=static_cast<float>(velocity.z);
    const float fx=static_cast<float>(forward.x),fy=static_cast<float>(forward.y),fz=static_cast<float>(forward.z);
    PlayerOwnerSpeeds result;
    result.magnitude=sqrtf((x*x+z*z)+y*y);
    result.forward=(z*fz+y*fy)+x*fx;
    return result;
}

VehicleVec3 player_hub_velocity(const std::array<VehicleVec3,3>& axes,
                               const VehicleVec3& local_hub,
                               const VehicleVec3& linear,
                               const VehicleVec3& angular) noexcept {
    const float x=static_cast<float>(local_hub.x),y=static_cast<float>(local_hub.y),
                z=static_cast<float>(local_hub.z);
    //5ABDB5: accumulate the z and y terms before x; matrix translation
    // is zero for the rotation used to resolve this relative hub arm.
    const float ax=(z*static_cast<float>(axes[2].x)+y*static_cast<float>(axes[1].x))+
                   x*static_cast<float>(axes[0].x);
    const float ay=(z*static_cast<float>(axes[2].y)+y*static_cast<float>(axes[1].y))+
                   x*static_cast<float>(axes[0].y);
    const float az=(z*static_cast<float>(axes[2].z)+y*static_cast<float>(axes[1].z))+
                   x*static_cast<float>(axes[0].z);
    const float ox=static_cast<float>(angular.x),oy=static_cast<float>(angular.y),
                oz=static_cast<float>(angular.z);
    return {(az*oy-ay*oz)+static_cast<float>(linear.x),
            (ax*oz-az*ox)+static_cast<float>(linear.y),
            (ay*ox-ax*oy)+static_cast<float>(linear.z)};
}

std::array<VehicleVec3,3> player_world_wheel_axes(
    const std::array<VehicleVec3,3>& snapshot,
    const std::array<VehicleVec3,3>& local) noexcept {
    std::array<VehicleVec3,3> result{};
    //5AC6A2 is a binary32 matrix product with distinct accumulation
    // orders per output component. Only its rotation columns are needed.
    const auto component=[](const VehicleVec3& v,unsigned k) {
        return static_cast<float>(k==0?v.x:k==1?v.y:v.z);
    };
    for(unsigned c=0;c<3;++c) {
        float row[3];
        for(unsigned r=0;r<3;++r) {
            const float x=component(local[c],0)*component(snapshot[0],r);
            const float y=component(local[c],1)*component(snapshot[1],r);
            const float z=component(local[c],2)*component(snapshot[2],r);
            if(c==0) row[r]=r==1?((y+0.0F)+z)+x:((0.0F+x)+z)+y;
            else if(r==0) row[r]=((0.0F+x)+y)+z;
            else if(r==1) row[r]=((z+0.0F)+y)+x;
            else row[r]=((y+x)+0.0F)+z;
        }
        result[c]={row[0],row[1],row[2]};
    }
    return result;
}

VehicleVec3 player_world_wheel_force(const std::array<VehicleVec3,3>& axes,
                                    float longitudinal,float lateral,float load) noexcept {
    //5ABDB5 transform used by587830 default6F0874=1. The wheel frame
    // rotation has zero translation; all three force components contribute.
    return {((load*static_cast<float>(axes[2].x)+lateral*static_cast<float>(axes[1].x))+
             longitudinal*static_cast<float>(axes[0].x))+0.0F,
            ((load*static_cast<float>(axes[2].y)+lateral*static_cast<float>(axes[1].y))+
             longitudinal*static_cast<float>(axes[0].y))+0.0F,
            ((load*static_cast<float>(axes[2].z)+lateral*static_cast<float>(axes[1].z))+
             longitudinal*static_cast<float>(axes[0].z))+0.0F};
}

unsigned player_body_rest_state(const VehicleVec3& velocity,const VehicleVec3& angular,
                               float forward_vertical,float left_vertical,
                               float throttle,int published_gear,
                               unsigned support,unsigned prior_state,
                               bool recovery_active) noexcept {
    if (throttle>0.30000001192092896F&&published_gear!=kVehicleGearNeutral&&support>0) return 0;
    const auto magnitude=[](const VehicleVec3& v) {
        const float x=static_cast<float>(v.x),y=static_cast<float>(v.y),z=static_cast<float>(v.z);
        return sqrtf((x*x+y*y)+z*z);
    };
    const float speed=magnitude(velocity),omega=magnitude(angular);
    if (speed>0.69999998807907104F||omega>0.69999998807907104F) return 0;
    if ((absf(forward_vertical)>0.15000000596046448F||
         absf(left_vertical)>0.15000000596046448F)&&support>0) return 0;
    if (recovery_active||(!support&&prior_state!=2)) return 0;
    return speed<=0.25F&&omega<=0.25F?2U:1U;
}

float player_body_inverse_yaw(const VehicleHandling& h,bool alternate) noexcept {
    //565060 inverts the full diagonal tensor by cofactors and determinant.
    // Simplifying this to1/yaw changes the authenticated binary32 result.
    const float yaw=alternate?h.inertia[2]*h.alternate_yaw_scale:h.inertia[2];
    const float determinant=(h.inertia[1]*yaw)*h.inertia[0];
    return (h.inertia[0]*h.inertia[1])*(1.0F/determinant);
}

void player_distribute_torque(const VehicleHandling& h, int gear,
                              PlayerEngineState& engine,
                              std::array<PlayerWheelWork,4>& w,
                              float transmitted) noexcept {
    if (gear == kVehicleGearNeutral) return;
    engine.feedback_torque -= transmitted;
    const float geared = transmitted * h.gear_ratios[static_cast<std::size_t>(gear)];
    const float front = ((1.0F - h.rear_drive_fraction) * h.final_drive_rear) * geared;
    const float rear = (geared * h.final_drive_front) * h.rear_drive_fraction;
    const float front_right = differential_share(w[1].spin,w[0].spin+w[1].spin,
                                                  w[0].contact&&w[1].contact);
    const float rear_left = differential_share(w[2].spin,w[2].spin+w[3].spin,
                                                w[2].contact&&w[3].contact);
    if (!w[0].locked) w[0].torque += (1.0F-front_right)*front;
    if (!w[1].locked) w[1].torque += front_right*front;
    if (!w[2].locked) w[2].torque += rear_left*rear;
    if (!w[3].locked) w[3].torque += (1.0F-rear_left)*rear;
}

float player_coupled_correction(const VehicleHandling& h, int gear,
                                const PlayerEngineState& engine,
                                const PlayerGearState& gearbox,
                                const std::array<PlayerWheelWork,4>& w,
                                float crank) noexcept {
    if (gearbox.mode != 3) return 0.0F;
    const float ratio = h.gear_ratios[static_cast<std::size_t>(gear)];
    float wheel_rate = 0.0F;
    const auto wheel_accel = [&](std::size_t k) {
        return (w[k].torque-h.wheels[k].radius*w[k].force_long)/kPlayerWheelInertia;
    };
    if (h.rear_drive_fraction >= 0.001F)
        wheel_rate = ((wheel_accel(3)+wheel_accel(2))*0.5F)*h.final_drive_front;
    if (h.rear_drive_fraction < 0.999F) {
        wheel_rate += ((wheel_accel(1)+wheel_accel(0))*0.5F)*h.final_drive_rear;
        if (h.rear_drive_fraction >= 0.001F) wheel_rate *= 0.5F;
    }
    wheel_rate *= ratio;
    const float difference = player_engine_acceleration(h,engine,crank)-wheel_rate;
    float inverse = 1.0F/h.engine_inertia;
    if (h.rear_drive_fraction < 0.999F) {
        const float factor = ratio*h.final_drive_rear;
        inverse += (((1.0F-h.rear_drive_fraction)/kPlayerWheelInertia)*factor*factor)*0.5F;
    }
    if (h.rear_drive_fraction >= 0.001F) {
        const float factor = ratio*h.final_drive_front;
        inverse += ((factor/kPlayerWheelInertia)*factor*h.rear_drive_fraction)*0.5F;
    }
    return difference/inverse;
}

void player_body_wheel_constraints(const VehicleHandling& h,
                                   std::array<PlayerWheelWork,4>& w,
                                   float external_x, float external_y) noexcept {
    //4785BC..4785E1 transforms rigid+110 force only. The first wheel
    // initializes the moment; rigid+120 external torque is not read here.
    float fx = external_x, fy = external_y, yaw = 0.0F;
    for (const auto& wheel:w) {
        const float x = wheel.heading_x*wheel.force_long+wheel.side_x*wheel.force_lat;
        const float y = wheel.heading_y*wheel.force_long+wheel.side_y*wheel.force_lat;
        fx += x; fy += y;
        //4786BA/4786C2: add the y moment before subtracting the x
        // moment. Grouping the cross product first changes binary32 bits.
        yaw = (y*wheel.position_x+yaw)-x*wheel.position_y;
    }
    const float inverse_mass = 1.0F/h.mass;
    const float ax = fx*inverse_mass, ay = inverse_mass*fy;
    const float inverse_yaw_inertia = player_body_inverse_yaw(h,false);
    const float alpha_yaw = yaw/(1.0F/inverse_yaw_inertia);
    for (std::size_t k=0;k<w.size();++k) {
        auto& wheel=w[k];
        if (wheel.sliding||wheel.locked) continue;
        const float point_x = ax-alpha_yaw*wheel.position_y;
        const float point_y = ay+alpha_yaw*wheel.position_x;
        const float radius=h.wheels[k].radius;
        const float rim = ((wheel.torque-radius*wheel.force_long)/kPlayerWheelInertia)*radius;
        //478809/478811 project with the source matrix's C0 and B0.
        // B4 also enters force rotation, but is not this operand. Identity
        // wheel matrices hide that distinction; a steered matrix exposes it.
        const float along=point_y*wheel.side_x+point_x*wheel.heading_x;
        wheel.force_long += ((rim-along)/radius)*kPlayerWheelInertia;
    }
}

void player_wheel_prepare(const VehicleHandling& h, std::size_t k,
                          PlayerWheelWork& w) noexcept {
    const float service = (h.service_brake*w.service)*4.0F;
    const float parking = (h.parking_brake*w.parking)*10.0F;
    if (!w.locked) {
        w.torque += w.spin > 0.0F ? -service : service;
        w.torque += w.spin > 0.0F ? -parking : parking;
    }
    w.slip = h.wheels[k].radius*w.spin-w.velocity_long;
    // ABS attenuation587483..5874CF; declared ordinary owner flag0.
    if (w.slip <= -0.5F && w.torque < 0.0F && parking == 0.0F) w.torque *= 0.15F;
    const float magnitude=sqrtf(w.velocity_lat*w.velocity_lat+w.slip*w.slip);
    w.sliding=false;
    w.force_long=0.0F; w.force_lat=0.0F;
    if (magnitude != 0.0F && (w.velocity_lat != 0.0F || w.velocity_long != 0.0F)) {
        const float scale=(1.0F/magnitude)*h.tyres[k/2].sliding_friction;
        const float force_scale=scale*w.load;
        const float capacity=(parking+service)*1.2F;
        //58759A..5875C7 supplies the LONGITUDINAL fraction of the
        // velocity magnitude to587220, independently of slip magnitude.
        // Its temporary push shifts stack operands by four bytes.
        const float velocity_magnitude=sqrtf(w.velocity_long*w.velocity_long+w.velocity_lat*w.velocity_lat);
        const float requested=((1.0F/velocity_magnitude)*h.tyres[k/2].sliding_friction*w.load)*absf(w.velocity_long);
        const float demand=absf(w.spin)*0.1F+h.wheels[k].radius*requested;
        if (capacity > demand) { w.spin=0.0F; w.locked=capacity>1.0F; }
        else w.locked=false;
        //587407 multiplies the16-bit angle by6CC980 (radians/unit).
        // The producer compares that radians value directly to setup+0.
        //587779 later converts radians to degrees for the standing curve.
        const float angle=absf(slip_angle(w.velocity_long,w.velocity_lat));
        if (absf(w.slip)>=0.5F || angle>=h.tyres[k/2].slip_scale || w.locked) {
            w.sliding=true;
            w.force_long=w.slip*force_scale;
            w.force_lat=-(force_scale*w.velocity_lat);
            //587637..58767B uses the separate owner speed, not wheel speed.
            if (w.owner_speed*2.237F<1.0F&&h.tyres[k/2].sliding_friction>0.1F) {
                const float normalize=1.0F/h.tyres[k/2].sliding_friction;
                w.force_long*=normalize;w.force_lat*=normalize;
            }
            return;
        }
    }
    w.force_long=w.torque/h.wheels[k].radius;
    w.force_lat=standing_lateral(h,k,w);
}

void player_wheel_limit(const VehicleHandling& h, std::size_t k,
                        PlayerWheelWork& w) noexcept {
    const bool boosted=w.torque>0.0F&&!w.locked;
    if (boosted) w.force_long*=1.5F; // 6F0878
    const float demand=sqrtf(w.force_long*w.force_long+w.force_lat*w.force_lat);
    const float limit=h.tyres[k/2].static_friction*w.load;
    if (demand>limit&&demand>0.001F) {
        const float factor=limit/demand;
        w.force_long*=factor; w.force_lat*=factor;
    } else if (boosted) w.force_long/=1.5F;
    //587A58..587A87 lateral drag uses the original16-bit angle and sine
    // polynomial after the source radians-to-angle truncation.
    if (absf(w.velocity_long)>1.0F&&w.velocity_lat!=0.0F) {
        const float radians=slip_angle(w.velocity_long,w.velocity_lat);
        const auto angle=static_cast<std::uint16_t>(static_cast<int>(radians*10430.3779296875F));
        const float projected=player_angle_sin(angle)*w.force_lat;
        w.force_long-=projected*0.5F;
    }
}

float player_wheel_acceleration(const VehicleHandling& h, std::size_t k,
                                PlayerWheelWork& w, float dt,
                                float rolling) noexcept {
    const float reaction=h.wheels[k].radius*w.force_long;
    const float filtered=((w.torque-reaction)+w.history_torque)*0.5F;
    w.history_torque=filtered;
    const float surface=(0.002F*1.0F)*w.spin; // surface0 at6EF2FC
    const float rolling_torque=(rolling*0.1F)*w.spin;
    float rate=((filtered-surface)-rolling_torque)/kPlayerWheelInertia;
    if (w.locked) rate=0.0F;
    if (!w.sliding&&dt>0.001F) rate-=w.slip/(h.wheels[k].radius*dt);
    return rate;
}

void player_wheel_integrate(PlayerWheelWork& w, float rate, float dt) noexcept {
    w.spin+=dt*rate;
    if ((w.spin_direction==0&&w.spin<0.0F)||(w.spin_direction==2&&w.spin>0.0F)) w.spin=0.0F;
    w.spin_direction=w.spin>1.0e-5F?0:(w.spin<-1.0e-5F?2:1);
}

void player_engine_feedback_finalize(const VehicleHandling& h,int gear,
                                     const PlayerGearState& gearbox,
                                     PlayerEngineState& engine,
                                     const std::array<PlayerWheelWork,4>& w,
                                     float throttle,float crank) noexcept {
    if (gear!=kVehicleGearNeutral&&gearbox.mode==3&&
        !(throttle>0.2F&&wheel_slip(h,w)>0.0F)) {
        // Uses pre-kernel cached4C, not the later gearbox update's speed.
        const float gap=gearbox.wheel_engine_speed-engine.angular_speed;
        if (gap<0.0F) {
            const float bound=absf(crank+crank);
            float correction=gap*h.clutch_stiffness;
            if (correction<-bound) correction=-bound;
            if (correction>bound) correction=bound;
            engine.feedback_torque+=correction;
        }
    }
    // Stock rear descriptor6F1098+60 is0; the subsequent curve feedback
    // term multiplied by gearbox+E4 is therefore zero in this common path.
}

void player_gear_integrate(const VehicleHandling& h,PlayerGearState& gearbox,float dt) noexcept {
    static constexpr float rates[4]={-3.0F,0.0F,1.5F,0.0F}; //700034
    gearbox.clutch+=dt*(h.clutch_rate*rates[gearbox.mode]);
}

void player_gear_update(const VehicleHandling& h,int& gear,PlayerGearState& box,
                        PlayerEngineState& engine,std::array<PlayerWheelWork,4>& w,
                        float throttle,float owner_speed_magnitude) noexcept {
    box.wheel_engine_speed=wheel_speed(h,gear,w);
    const auto needs_disengage=[&]() {
        return throttle<0.3F && box.wheel_engine_speed<=h.idle_rpm*kPlayerRpmToRadS+0.0001F;
    };
    switch (box.mode) {
    case 3:
        engine.throttle_multiplier=1.0F;
        if (gear!=kVehicleGearNeutral) {
            if (throttle>0.2F&&wheel_slip(h,w)>0.0F) {
                const float difference=engine.angular_speed-box.wheel_engine_speed;
                const float ratio=h.gear_ratios[static_cast<std::size_t>(gear)];
                const auto synchronize=[&](std::size_t left,std::size_t right,float final_drive) {
                    const float change=difference/(ratio*final_drive);
                    //46D8B0: average the driven pair below40 mph, or when
                    // neither wheel contacts. Above that gate preserve each
                    // wheel's differential speed. Owner+394 is the published
                    // magnitude, independently of owner+390's signed speed.
                    if (owner_speed_magnitude*2.237F<40.0F||(!w[left].contact&&!w[right].contact)) {
                        const float average=(w[right].spin+w[left].spin)*0.5F;
                        w[left].spin=average+change;w[right].spin=average+change;
                    } else {w[left].spin+=change;w[right].spin+=change;}
                };
                if (h.rear_drive_fraction>=0.001F) {
                    synchronize(2,3,h.final_drive_front);
                }
                if (h.rear_drive_fraction<0.999F) {
                    synchronize(0,1,h.final_drive_rear);
                }
                box.wheel_engine_speed=engine.angular_speed;
            } else {
                const float difference=box.wheel_engine_speed-engine.angular_speed;
                if (difference>0.0F) engine.angular_speed+=difference*0.04F;
            }
            if (needs_disengage()) box.mode=0;
        }
        if (gear!=box.requested_gear) {box.mode=0;box.shift_pending=true;}
        break;
    case 2:
        engine.throttle_multiplier=0.7F;
        if (box.clutch>=1.0F) {
            box.clutch=1.0F;
            if (absf(engine.angular_speed-box.wheel_engine_speed)<50.0F||gear==kVehicleGearNeutral)
                box.mode=3;
        }
        if (needs_disengage()&&gear!=kVehicleGearNeutral) box.mode=0;
        break;
    case 0:
        engine.throttle_multiplier=0.0F;
        if (box.clutch<=0.0F) {box.clutch=0.0F;box.mode=1;}
        break;
    case 1:
        engine.throttle_multiplier=0.0F;
        gear=box.requested_gear;
        if (box.shift_pending||!needs_disengage()) {
            box.shift_pending=false;
            box.mode=2;
            player_gear_randomize(box);
        }
        break;
    }
}

void player_gear_seed(PlayerGearState& box,std::uint32_t seed) noexcept {
    box.rng_state=seed;
    random_unit(box.rng_state);random_unit(box.rng_state);
    box.up_offset=0.0F;box.down_offset=0.0F;
}

void player_gear_randomize(PlayerGearState& box) noexcept {
    box.up_offset=random_unit(box.rng_state)*100.0F-100.0F;
    box.down_offset=random_unit(box.rng_state)*100.0F-100.0F;
}

void player_engine_cutoff_update(const VehicleHandling& h,int gear,
                                 PlayerEngineState& engine,float seconds) noexcept {
    static constexpr float duration[8]={0.05F,0.04F,0.055F,0.06F,0.07F,0.075F,0.075F,0.075F}; //700004
    if (engine.cutoff_until==0.0F) {
        if (engine.angular_speed*kPlayerRadSToRpm>=h.max_rpm-50.0F)
            engine.cutoff_until=seconds+duration[gear];
    } else if (seconds>=engine.cutoff_until) engine.cutoff_until=0.0F;
}

void player_engine_reset(PlayerEngineState& engine, float rpm) noexcept {
    engine = {};
    engine.angular_speed = rpm * kPlayerRpmToRadS;
}

float player_engine_torque(const VehicleHandling& h,
                          const PlayerEngineState& engine, int gear,
                          float throttle) noexcept {
    const float rpm = engine.angular_speed * kPlayerRadSToRpm;
    const float effective = engine.cutoff_until == 0.0F ?
        engine.throttle_multiplier * throttle : 0.0F;
    const float drive = curve(h.torque.data(), 9, h.idle_rpm, h.max_rpm, rpm) * effective;
    float brake = -curve(h.engine_brake.data(), 3, h.idle_rpm, h.max_rpm, rpm) *
                  (1.0F - effective);
    if (engine.cutoff_until > 0.0F && gear != kVehicleGearNeutral) brake *= 4.0F; // 6B7210
    float result = brake + drive;
    if (result < 0.0F && gear == kVehicleGearNeutral) result *= 0.85F; // 6B723C
    return result;
}

float player_wheel_engine_speed(const VehicleHandling& h, int gear,
                               const std::array<float, 4>& spins) noexcept {
    float speed = 0.0F;
    if (h.rear_drive_fraction >= 0.001F)
        speed = ((spins[3] + spins[2]) * 0.5F) * h.final_drive_front;
    if (h.rear_drive_fraction < 0.999F) {
        const float front = ((spins[1] + spins[0]) * 0.5F) * h.final_drive_rear;
        speed += front;
        if (h.rear_drive_fraction >= 0.001F) speed *= 0.5F;
    }
    return speed * h.gear_ratios[static_cast<std::size_t>(gear)];
}

float player_clutch_torque(const VehicleHandling& h,
                          const PlayerEngineState& engine,
                          PlayerGearState& gearbox, int gear,
                          float wheel_engine_speed, float crank) noexcept {
    if (gear == kVehicleGearNeutral || gearbox.mode == 1) return 0.0F;
    if (gearbox.mode == 3) return crank;
    float difference = engine.angular_speed - wheel_engine_speed;
    if (difference > 300.0F) difference = 300.0F; // 6FFF28
    if (difference < -300.0F) difference = -300.0F;
    float stiffness = h.clutch_stiffness;
    const float slip_rpm = (wheel_engine_speed - engine.angular_speed) * kPlayerRadSToRpm;
    if (gearbox.previous_slip_rpm != 0.0F && slip_rpm * gearbox.previous_slip_rpm < 0.0F &&
        absf(slip_rpm) > 2000.0F && absf(gearbox.previous_slip_rpm) > 2000.0F)
        stiffness *= 0.3F; // 6B7248, sign-crossing guard46E302..46E33D
    gearbox.previous_slip_rpm = slip_rpm;
    return (stiffness * gearbox.clutch) * difference;
}

float player_engine_acceleration(const VehicleHandling& h,
                                 const PlayerEngineState& engine,
                                 float crank) noexcept {
    // Mode0/2 inertia multiplier6B7240 is retail1.0.
    return (crank + engine.feedback_torque) / h.engine_inertia;
}

void player_engine_integrate(const VehicleHandling& h,
                             PlayerEngineState& engine, float crank,
                             float solver_dt) noexcept {
    const float change = solver_dt * player_engine_acceleration(h, engine, crank);
    float speed = engine.angular_speed + change;
    const float idle = h.idle_rpm * kPlayerRpmToRadS;
    const float maximum = h.max_rpm * kPlayerRpmToRadS;
    if (speed < idle) speed = idle;
    if (speed > maximum) speed = maximum;
    engine.angular_speed = speed;
    engine.feedback_torque = 0.0F;
}

float player_engine_publish_rpm(const VehicleHandling& h,
                               const PlayerEngineState& engine,
                               float previous, float dt) noexcept {
    const float target = engine.angular_speed * kPlayerRadSToRpm;
    if (!(dt > 0.0F)) return previous;
    const float floor = -2.5F / h.engine_inertia; // 6B7124
    if ((target - previous) / dt < floor) {
        const float limited = floor * dt + previous;
        return limited > target ? limited : target;
    }
    return target;
}

} // namespace nfsu
