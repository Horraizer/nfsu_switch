#include "nfsu/player_powertrain.h"
#include "nfsu/nitrous.h"
#include "nfsu/player_angle_data.h"
#include "nfsu/player_heading_data.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>

namespace nfsu {
#include "player_surfaces.inc"
namespace {

float absf(float x) noexcept { return x < 0.0F ? -x : x; }
//007F rounds to24 significant bits with an extended exponent. A subnormal
// binary32 operation can lose bits the source still retains. This is only a
// request-table representability check; it never substitutes an answer.
bool request_binary32(float value,double exact) noexcept {
    if(!std::isfinite(value)) return false;
    if(absf(value)>=std::numeric_limits<float>::min()||exact==0.0) return true;
    int exponent=0;
    const double units=std::ldexp(std::frexp(std::abs(exact),&exponent),24);
    double rounded=std::floor(units);
    const double fraction=units-rounded;
    if(fraction>0.5||(fraction==0.5&&(static_cast<unsigned>(rounded)&1U))) ++rounded;
    const double source=std::copysign(std::ldexp(rounded,exponent-24),exact);
    return static_cast<double>(value)==source;
}
float sqrtf(float x) noexcept { return static_cast<float>(vehicle_sqrt(x)); }
std::uint16_t converted_angle_word(float value) noexcept {
    //674898's signed integer conversion supplies the low word. Invalid
    // signed64 conversions yield the indefinite integer, whose low word is0.
    if(!(value>=-9223372036854775808.0F&&value<9223372036854775808.0F)) return 0;
    return static_cast<std::uint16_t>(static_cast<std::int64_t>(value));
}

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
    if (angle>3.141590118408203F) angle-=6.283180236816406F;
    if (longitudinal<0.0F) {
        angle+=3.141590118408203F;
        if (angle>3.141590118408203F) angle-=6.283180236816406F;
    }
    return angle;
}

float differential_share(float numerator, float denominator, bool both_contact,int owner_mode) noexcept {
    const float edge = both_contact ? 0.35F : 0.5F; // 700044
    if(owner_mode==1)return 1.0F-edge;
    if(owner_mode==2)return edge;
    if (absf(denominator) <= 1.0e-5F) return 0.5F;
    float share = numerator / denominator;
    if (share < edge) share = 1.0F - 0.35F * 0.5F;
    else if (share > 1.0F - edge) share = 0.35F * 0.5F;
    return share;
}

float shift_grip_limit(float drive,unsigned category) noexcept {
    //6F100C, four initialized ride classes in each27-float family.
    constexpr float limits[3][4]={{.55F,.7F,.85F,1.F},{5.F,4.6F,4.3F,4.F},
                                  {1.3F,1.35F,1.4F,1.4F}};
    return limits[drive>.8F?2U:drive>=.2F?1U:0U][category];
}

float owner_traction_curve(float slip) noexcept {
    //705E44 ->6FFEBC.454F30 interpolation, including its product order.
    constexpr float knots[6][2]={{0,1},{5,.8F},{9,.9F},{12.6F,.833F},{17.1F,.72F},{25,.65F}};
    if(slip<=knots[0][0])return knots[0][1];
    if(slip>=knots[5][0])return knots[5][1];
    for(unsigned i=0;i<5;++i)if(slip<knots[i+1][0]) {
        const float t=(slip-knots[i][0])/(knots[i+1][0]-knots[i][0]);
        return t*knots[i+1][1]+(1.F-t)*knots[i][1];
    }
    return knots[5][1];
}

float wheel_slip(const VehicleHandling& h,const std::array<PlayerWheelWork,4>& w) noexcept {
    float sum=0.0F;int count=0;
    if (h.rear_drive_fraction>=0.001F) {sum=w[3].cached_slip+w[2].cached_slip;count=2;}
    if (h.rear_drive_fraction<0.999F) {sum+=w[1].cached_slip+w[0].cached_slip;count+=2;}
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

PlayerRigidVector player_strut_attachment(const VehicleHandling& h,
                                          std::size_t wheel,
                                          const PlayerRigidBodyState& body) noexcept {
    const auto& w=h.wheels[wheel];
    const PlayerRigidVector local{{w.longitudinal-h.centre_of_mass[0],
                                  w.lateral-h.centre_of_mass[1],
                                  w.vertical-h.centre_of_mass[2]}};
    PlayerRigidVector point{};
    for(unsigned j=0;j<3;++j) {
        const float zy=local[2]*body.axes[2][j]+local[1]*body.axes[1][j];
        const float rotated=zy+local[0]*body.axes[0][j];
        point[j]=rotated+body.position[j];
    }
    return point;
}

bool player_strut_ground_projection(const std::array<float,4>& p,
    const PlayerRigidVector& hub,const PlayerRigidVector& up,
    float bump,float& gap) noexcept {
    gap=-100.0F;
    const float alignment=(up[1]*p[1]+up[2]*p[2])+up[0]*p[0];
    if(!std::isfinite(alignment)) {gap=alignment;return false;}
    if(alignment<.001F||p[2]==0.0F)return false;
    const float height=-((p[1]*hub[1]+p[0]*hub[0]-p[3])/p[2])+bump;
    gap=((height-hub[2])*p[2])/alignment;
    return true;
}

PlayerStrutCompression player_strut_compression(const VehicleAxleStrut& h,
    float raw,float previous,float cached_rate,float interval) noexcept {
    PlayerStrutCompression out;out.rate=cached_rate;
    if(raw>h.bump_limit) {
        out.compression=h.bump_limit;out.contact=true;
        const float excess=raw-h.bump_limit;
        out.overflow=excess>0.15000000596046448F?0.15000000596046448F:excess;
    } else if(raw<h.droop_limit) {
        out.compression=h.droop_limit;
    } else {
        out.compression=raw;out.contact=true;
    }
    if(interval>.0001F)out.rate=(out.compression-previous)/interval;
    return out;
}

float player_strut_nominal_force(const VehicleAxleStrut& h,
                                 float x, float rate, float partner,
                                 float preload, float overflow,
                                 float bump_rate) noexcept {
    const float positive=x>0.0F?x:0.0F;
    const float spring=((h.progressive*positive+1.0F)*h.spring)*x;
    float force=(rate>0.0F?h.damping_bump:h.damping_rebound)*rate+spring;
    force+=preload;
    const float extra=(x-partner)*h.anti_roll+bump_rate*overflow;
    force+=extra;
    return force>0.0F?force:0.0F;
}

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

std::int16_t player_unit_asin(float value)noexcept {
    if(!std::isfinite(value))return 0;
    const auto angle=asin_angle(value<0.F?-value:value);
    return static_cast<std::int16_t>(value<0.F?-int(angle):int(angle));
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

std::array<std::array<VehicleVec3,3>,2> player_front_steering_axes(
    const VehicleHandling& h,std::uint16_t word) noexcept {
    float angle=static_cast<float>(word)*9.58738019107841e-5F;
    if(angle>3.141590118408203F) angle-=6.283180236816406F;
    const bool negative=angle<0.0F;
    const float magnitude=negative?-angle:angle;
    const float wheelbase=h.wheels[0].longitudinal-h.wheels[3].longitudinal;
    const float width=h.wheels[0].lateral-h.wheels[1].lateral;
    const float outer=(magnitude*wheelbase)/(magnitude*width+wheelbase);
    const float angles[2]={negative?-outer:angle,negative?angle:outer};
    std::array<std::array<VehicleVec3,3>,2> axes{};
    for(unsigned k=0;k<2;++k) {
        const float scaled=angles[k]*10430.3779296875F;
        const float c=player_angle_sin(converted_angle_word(scaled+16383.9990234375F));
        const float s=player_angle_sin(converted_angle_word(scaled));
        axes[k]={{{c,s,0},{-s,c,0},{0,0,1}}};
    }
    return axes;
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

//478AA0 first truncates each orientation component times65536, then
//564FA0 uses integer division and564F00 table interpolation. The velocity
//angle deliberately remains564DB0; replacing both with one atan changes a word.
std::uint16_t player_owner_heading(const VehicleVec3& forward) noexcept {
    const auto component=[](double v) {
        const float scaled=float(v)*65536.F;
        if(!(scaled>=-2147483648.F&&scaled<2147483648.F))return std::int32_t(0);
        return static_cast<std::int32_t>(scaled);
    };
    const auto sx=component(forward.x),sy=component(forward.y);
    const std::int64_t x=sx<0?-std::int64_t(sx):sx,y=sy<0?-std::int64_t(sy):sy;
    unsigned quadrant=(sx<0?1U:0U)^(sy<0?3U:0U);std::uint32_t angle=0x4000;
    if(x>(y>>14)) {
        const auto ratio=static_cast<std::uint32_t>(((2147483647LL/x)*y)>>16)<<1;
        std::uint32_t lo=0,hi=0,weight=0;
        if(ratio<0x40000) {
            const auto index=ratio>>11;lo=kPlayerHeadingFine[index];hi=kPlayerHeadingFine[index+1];weight=(ratio&0x7ff)<<5;
        } else if(ratio<0x200000) {
            const auto index=ratio>>14;lo=kPlayerHeadingCoarse[index];hi=kPlayerHeadingCoarse[index+1];weight=(ratio&0x3fff)<<2;
        } else if(ratio<0x1000000) {lo=0x3eba;hi=0x3fd7;weight=ratio>>8;}
        else lo=hi=0x3fff;
        angle=lo+(((hi-lo)*weight)>>16);
    }
    if(quadrant==1)angle=0x8000-angle;
    else if(quadrant==2)angle-=0x8000;
    else if(quadrant==3)angle=0-angle;
    return static_cast<std::uint16_t>(angle);
}

float player_owner_slip_angle(const VehicleVec3& velocity,const VehicleVec3& forward) noexcept {
    return player_owner_slip_angle(velocity,player_owner_heading(forward));
}

float player_owner_slip_angle(const VehicleVec3& velocity,std::uint16_t heading) noexcept {
    const auto travel=player_velocity_angle(float(velocity.x),float(velocity.y));
    float angle=float(static_cast<std::uint16_t>(heading-travel))*9.58738019107841e-5F;
    if(angle>3.141590118408203F)angle-=6.283180236816406F;
    return angle;
}

void player_publish_owner_motion(PlayerOwnerMotion& owner,const PlayerRigidVector& velocity,
                                const PlayerRigidVector& angular,const PlayerRigidMatrix& axes) noexcept {
    owner.velocity=velocity;owner.angular_velocity=angular;owner.axes=axes;
    owner.heading=player_owner_heading({axes[0][0],axes[0][1],axes[0][2]});
}

void player_publish_owner_acceleration(PlayerOwnerMotion& owner,const VehicleVec3& before,
                                      const VehicleVec3& after,float world_dt) noexcept {
    if(!(world_dt>0.F))return;
    const float inverse=1.F/world_dt;
    owner.acceleration={{(float(after.x)-float(before.x))*inverse,
                         (float(after.y)-float(before.y))*inverse,
                         (float(after.z)-float(before.z))*inverse}};
}

void player_drag_body_torque(PlayerRigidBodyState& body,const PlayerOwnerMotion& owner,
                            unsigned support,unsigned state,bool nitrous) noexcept {
    if(support<4||state!=0)return;
    const auto& v=owner.velocity;
    if(!(absf(player_owner_slip_angle({v[0],v[1],v[2]},owner.heading)*57.2957763671875F)<5.F))return;
    const auto& a=owner.acceleration;const auto& forward=owner.axes[0];
    float factor=((a[0]*forward[0]+a[1]*forward[1])+a[2]*forward[2])*1.5F;
    const float upper=nitrous?7.F:4.F;
    if(factor>upper)factor=upper;
    if(factor< -3.F)factor=-3.F;
    factor=-factor;
    for(unsigned k=0;k<3;++k)body.torque[k]+=owner.axes[1][k]*factor;
}

float player_drag_nitrous_grip(bool drag,bool active,std::uint32_t clock,float activation) noexcept {
    if(!drag||!active)return 1.F;
    std::int32_t signed_clock;std::memcpy(&signed_clock,&clock,4);
    const float seconds=float(signed_clock)*.00025F;
    return seconds<=activation+.5F?.6F:1.F;
}

float player_shift_grip_scale(float quality,float drive,unsigned category) noexcept {
    if(!(quality>0.F)||category>3)return 1.F;
    const float reciprocal=1.F/shift_grip_limit(drive,category);
    const float ratio=reciprocal*quality;
    return 1.F+(ratio>1.F?1.F:ratio);
}

float player_owner_base_grip(float scale,unsigned mode,bool assisted) noexcept {
    return mode==0&&!assisted?.85F*scale:scale;
}

std::array<float,4> player_owner_traction_update(PlayerOwnerTraction& p,float drive,
    unsigned category,bool eligible,bool racing,float speed,float angle,float dt,
    const std::array<float,4>& load,const std::array<float,4>& slip) noexcept {
    std::array<float,4> factor{{1,1,1,1}};
    if(!eligible||!(speed>0.F)||category>3)return factor;
    if(p.mode==0) {
        if(racing&&speed*2.237F<15.F) {
            float largest=0;unsigned index=0;
            for(unsigned i=0;i<4;++i) {
                const float weighted=(load[i]*(1.F/3.F))*slip[i];
                const float value=weighted<=slip[i]?weighted:slip[i];
                if(largest<value){largest=value;index=i;}
            }
            if(largest>.5F) {
                const float value=owner_traction_curve(largest);
                p.scale=(1.F/shift_grip_limit(drive,category))*value;
                p.mode=static_cast<int>((1.5F-value)*6.F+static_cast<float>(index&1));
                p.timer=.5F;p.quiet=0;
            }
        }
    } else if((speed>=1.F?absf(angle):0.F)>.3F) {
        p.mode=0;
    } else if(std::any_of(slip.begin(),slip.end(),[](float value){return value>=2.5F;})) {
        p.quiet=0;p.timer-=dt;
        if(p.timer<0.F){--p.mode;p.timer=.5F;}
    } else {
        p.quiet+=dt;if(p.quiet>1.F)p.mode=0;
    }
    if(p.mode!=0) {
        if(p.mode%2==1){factor[0]=p.scale;factor[3]=p.scale;}
        if(p.mode%2==0){factor[1]=p.scale;factor[2]=p.scale;}
    }
    return factor;
}

float player_rear_grip(const VehicleHandling& h,float tilt,float slip,
                      float speed,float parking) noexcept {
    const float base=absf(tilt)+1.F;
    const float angle=absf(slip);
    //477431: handbrake plus a small slip angle bypasses the extra term.
    if(parking>.5F && angle<20.F*.01745329238474369F)return base;
    const float speed_scale=(speed/30.F);
    const float gain=h.rear_grip_base+h.rear_grip_slip;
    float extra=(speed_scale*angle)*gain;
    if(extra>.3499999940395355F)extra=.3499999940395355F;
    return base+extra;
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

PlayerRigidVector player_world_wheel_hub(const PlayerRigidBodyState& body,
                                         const PlayerRigidVector& local) noexcept {
    const auto& a=body.snapshot_axes;const auto& p=body.position;
    return {{((p[0]+local[0]*a[0][0])+local[1]*a[1][0])+local[2]*a[2][0],
             ((p[1]+local[2]*a[2][1])+local[1]*a[1][1])+local[0]*a[0][1],
             ((local[1]*a[1][2]+local[0]*a[0][2])+p[2])+local[2]*a[2][2]}};
}

PlayerRigidVector player_wheel_contact_point(const PlayerRigidVector& hub,
                                             const PlayerRigidVector& normal,
                                             float radius) noexcept {
    const float negative_radius=-radius;
    return {{negative_radius*normal[0]+hub[0],negative_radius*normal[1]+hub[1],
             negative_radius*normal[2]+hub[2]}};
}

void player_wheel_contact_point(PlayerRigidVector& point,
                                 const PlayerRigidVector& hub,
                                 const PlayerRigidVector& normal,float radius,
                                 bool contact) noexcept {
    if(contact) point=player_wheel_contact_point(hub,normal,radius);
}

float player_wheel_contact_load(const PlayerRigidVector& y,
                                 const PlayerRigidVector& n,float nominal) noexcept {
    if(!(nominal>0.0F)) return 0.0F;
    //586F7D..587029: (y cross n) cross y, then dot with n, z/y/x.
    // Keep the original products rather than assuming normalized axes.
    const float cx=n[2]*y[1]-y[2]*n[1];
    const float cy=y[2]*n[0]-n[2]*y[0];
    const float cz=n[1]*y[0]-y[1]*n[0];
    const float dy=cz*y[0]-cx*y[2];
    const float dz=cx*y[1]-cy*y[0];
    const float dx=cy*y[2]-cz*y[1];
    const float alignment=(dz*n[2]+dy*n[1])+dx*n[0];
    float factor=alignment*4.0F-3.0F;
    if(factor<0.30000001192092896F) factor=0.30000001192092896F;
    return factor*nominal;
}

void player_front_drive_torque(const VehicleHandling& h,PlayerRigidBodyState& body,
                               const std::array<PlayerWheelWork,4>& w,
                               unsigned published_support,bool assisted) noexcept {
    if(assisted||h.rear_drive_fraction>=.2F||published_support!=4)return;
    const auto& v=body.velocity;
    if(!(sqrtf((v[0]*v[0]+v[1]*v[1])+v[2]*v[2])<44.7F))return;
    //587DE0 retains only each front wheel's local longitudinal component.
    const float x0=w[0].force_long*w[0].heading_x+w[0].side_x*w[0].force_lat;
    const float x1=w[1].force_long*w[1].heading_x+w[1].side_x*w[1].force_lat;
    if(!(x0>0.F&&x1>0.F))return;
    const PlayerRigidVector lever{{0,0,-h.centre_of_mass[2]}};
    std::array<PlayerRigidVector,2> moment{};
    for(unsigned i=0;i<2;++i) {
        const float x=i?x1:x0;PlayerRigidVector world{};
        for(unsigned k=0;k<3;++k)world[k]=
            (0.F*body.axes[2][k]+0.F*body.axes[1][k])+x*body.axes[0][k];
        //567540 computes EDX cross ECX in this exact product order.
        moment[i]={{world[2]*lever[1]-world[1]*lever[2],
                    world[0]*lever[2]-world[2]*lever[0],
                    world[1]*lever[0]-lever[1]*world[0]}};
    }
    for(unsigned k=0;k<3;++k)body.torque[k]+=(moment[0][k]+moment[1][k])* -1.75F;
}

PlayerRigidVector player_low_support_apply(const VehicleHandling& h,
                                           PlayerRigidBodyState& body,
                                           unsigned support,unsigned state) noexcept {
    if(state==1||state==2||support>=2) return {};
    const auto& w=body.angular_velocity;
    //47922D..479245 accumulates Z squared, X squared, then Y squared.
    const float speed=sqrtf((w[2]*w[2]+w[0]*w[0])+w[1]*w[1]);
    //705E14 ->705DFC, the selector1 three-pair table. The selector3
    //705E98 table has a different zero-damping interval.
    const float damping=speed<=0.40000000596046448F?0.0F:speed>=2.0F?20.0F:
        ((speed-0.40000000596046448F)/(2.0F-0.40000000596046448F))*20.0F;
    const float factor=-(damping*h.mass);
    PlayerRigidVector applied{};
    for(unsigned k=0;k<3;++k) {
        applied[k]=factor*w[k];
        body.torque[k]+=applied[k]; //584E00, no contact moment cap here
    }
    return applied;
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

namespace {
float rigid_norm(const PlayerRigidVector& v) noexcept {
    return sqrtf((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
}
PlayerRigidVector rigid_cross(const PlayerRigidVector& a,const PlayerRigidVector& b) noexcept {
    return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};
}
PlayerRigidVector rigid_normalized(PlayerRigidVector v) noexcept {
    const float length=rigid_norm(v);
    if(length==0.0F)return {{1,0,0}}; //5673C8
    const float factor=1.0F/length;
    for(float& x:v)x*=factor;
    return v;
}
PlayerRigidMatrix rigid_product(const PlayerRigidMatrix& a,const PlayerRigidMatrix& b) noexcept {
    // Exact5AC6A2 accumulation orders, shared with world wheel axes.
    PlayerRigidMatrix result{};
    for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r) {
        const float x=b[c][0]*a[0][r],y=b[c][1]*a[1][r],z=b[c][2]*a[2][r];
        if(c==0)result[c][r]=r==1?((y+0.0F)+z)+x:((0.0F+x)+z)+y;
        else if(r==0)result[c][r]=((0.0F+x)+y)+z;
        else if(r==1)result[c][r]=((z+0.0F)+y)+x;
        else result[c][r]=((y+x)+0.0F)+z;
    }
    return result;
}
}

void player_rigid_body_add_contact_force(PlayerRigidBodyState& body,
                                        const VehicleVec3& force,const VehicleVec3& arm) noexcept {
    const PlayerRigidVector original{{static_cast<float>(force.x),static_cast<float>(force.y),static_cast<float>(force.z)}};
    const PlayerRigidVector point{{static_cast<float>(arm.x),static_cast<float>(arm.y),static_cast<float>(arm.z)}};
    auto applied=original;
    //584CF1..584D01 adds Z squared, Y squared, then X squared.
    const float magnitude=sqrtf((applied[2]*applied[2]+applied[1]*applied[1])+applied[0]*applied[0]);
    if(magnitude>1200.0F) {
        const float factor=1200.0F/magnitude;
        for(float& x:applied)x*=factor;
    }
    for(unsigned k=0;k<3;++k)body.force[k]+=applied[k];
    auto moment=rigid_cross(point,original);
    //584D84..584D98 adds Y squared, Z squared, then X squared.
    const float torque=sqrtf((moment[1]*moment[1]+moment[2]*moment[2])+moment[0]*moment[0]);
    if(torque>100.0F) {
        const float factor=100.0F/torque;
        for(float& x:moment)x*=factor;
    }
    for(unsigned k=0;k<3;++k)body.torque[k]+=moment[k];
}

float player_strut_bump_impulse(const VehicleHandling& h,PlayerRigidBodyState& body,
    const PlayerRigidVector& hub,const PlayerRigidVector& normal) noexcept {
    PlayerRigidVector arm{},velocity{};
    for(unsigned k=0;k<3;++k)
        arm[k]=(hub[2]*body.axes[2][k]+hub[1]*body.axes[1][k])+hub[0]*body.axes[0][k];
    for(unsigned k=0;k<3;++k) {
        const unsigned a=(k+1)%3,b=(k+2)%3;
        velocity[k]=(arm[b]*body.angular_velocity[a]-arm[a]*body.angular_velocity[b])+body.velocity[k];
    }
    const float inward=-((normal[2]*velocity[2]+normal[1]*velocity[1])+normal[0]*velocity[0]);
    if(inward<0.F)return 0.F;
    const float scalar=(.2F*inward)*h.mass; //6F073C, rigid144 mass;148 is inverse
    PlayerRigidVector impulse{};
    for(unsigned k=0;k<3;++k)impulse[k]=normal[k]*scalar;
    const auto magnitude=[](const PlayerRigidVector& v) {
        return sqrtf((v[0]*v[0]+v[2]*v[2])+v[1]*v[1]);
    };
    const float length=magnitude(impulse);
    if(length>1000.F)for(float& v:impulse)v*=1000.F/length;
    //584E30 takes arm cross impulse, with its distinct magnitude sum order.
    auto torque=rigid_cross(arm,impulse);
    const float torque_length=magnitude(torque);
    if(torque_length>10.F)for(float& v:torque)v*=10.F/torque_length;
    for(unsigned k=0;k<3;++k) {
        body.momentum[k]+=impulse[k];body.angular_momentum[k]+=torque[k];
    }
    return scalar;
}

PlayerRigidMatrix player_body_inverse_tensor(const VehicleHandling& h,
    const PlayerRigidMatrix& axes,bool alternate) noexcept {
    const float yaw=alternate?h.inertia[2]*h.alternate_yaw_scale:h.inertia[2];
    const float inverse_determinant=1.0F/((h.inertia[1]*yaw)*h.inertia[0]);
    PlayerRigidMatrix inverse{},transpose{};
    inverse[0][0]=(h.inertia[1]*yaw)*inverse_determinant;
    inverse[1][1]=(h.inertia[0]*yaw)*inverse_determinant;
    inverse[2][2]=(h.inertia[0]*h.inertia[1])*inverse_determinant;
    for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)transpose[c][r]=axes[r][c];
    return rigid_product(rigid_product(axes,inverse),transpose);
}

void player_rigid_body_refresh(const VehicleHandling& h,PlayerRigidBodyState& body) noexcept {
    const float inverse_mass=1.0F/h.mass;
    for(unsigned k=0;k<3;++k)body.velocity[k]=body.momentum[k]*inverse_mass;
    const float speed=rigid_norm(body.velocity);
    if(speed>200.0F&&static_cast<double>(h.mass)>0.7) { //585880/585893
        const float factor=200.0F/speed;
        for(unsigned k=0;k<3;++k) {
            body.momentum[k]*=factor;body.velocity[k]=body.momentum[k]*inverse_mass;
        }
    }
    const auto world=player_body_inverse_tensor(h,body.axes,body.alternate_yaw);
    for(unsigned k=0;k<3;++k)
        body.angular_velocity[k]=((body.angular_momentum[2]*world[2][k]+
            body.angular_momentum[1]*world[1][k])+body.angular_momentum[0]*world[0][k])+0.0F;
    const float omega=rigid_norm(body.angular_velocity);
    if(omega>250.0F) { //5859B3: cache limit, momentum is retained
        const float factor=250.0F/omega;
        for(float& x:body.angular_velocity)x*=factor;
    }
    if(body.adaptive_damping<0.0F)body.adaptive_damping=0.0F;
    body.snapshot_axes=body.axes;
    body.force={};body.torque={};
    if(body.gravity)body.force[2]=-(9.800000190734863F*h.mass); //7000AC
}

void player_rigid_body_integrate(const VehicleHandling& h,PlayerRigidBodyState& body,
                                 float dt) noexcept {
    //585410 common path with drive force control1E4 disabled. Damping
    // acts on momentum, independently from4791F0 low-support torque.
    static constexpr float angular_curve[6]={.05F,.05F,.25F,.7F,2.0F,5.0F}; //704478/6F08C8
    static constexpr float adaptive_curve[8]={1.5F,1.0F,.6F,.22F,.06F,.03F,.015F,0.0F}; //70448C
    const float omega=rigid_norm(body.angular_velocity);
    const float angular_factor=-curve(angular_curve,6,0.0F,10.0F,omega);
    const float velocity_parameter=rigid_norm(body.velocity)/4.0F;
    const float angular_parameter=omega/2.0F;
    const float parameter=angular_parameter>velocity_parameter?angular_parameter:velocity_parameter;
    const float target=curve(adaptive_curve,8,0.0F,1.0F,parameter);
    const float adaptive_rate=target>body.adaptive_damping?.1F:-10.0F;
    auto force=body.force,torque=body.torque;
    for(unsigned k=0;k<3;++k) {
        if(body.angular_damping)torque[k]+=angular_factor*body.angular_momentum[k];
        if(body.linear_damping) {
            torque[k]+=-(2.0F*body.adaptive_damping)*body.angular_momentum[k];
            force[k]+=-(8.0F*body.adaptive_damping)*body.momentum[k];
        }
    }
    if(body.yaw_torque_limit!=0.0F) {
        if(body.torque[2]>body.yaw_torque_limit)torque[2]=body.yaw_torque_limit;
        else if(body.torque[2]<-body.yaw_torque_limit)torque[2]=-body.yaw_torque_limit;
    }
    //46AA40 Euler integrates derivatives taken from the old caches.
    for(auto& axis:body.axes) {
        const auto rate=rigid_cross(body.angular_velocity,axis);
        for(unsigned k=0;k<3;++k)axis[k]+=dt*rate[k];
    }
    for(unsigned k=0;k<3;++k) {
        body.position[k]+=dt*body.velocity[k];
        body.momentum[k]+=dt*force[k];body.angular_momentum[k]+=dt*torque[k];
    }
    body.adaptive_damping+=dt*adaptive_rate;
    player_rigid_body_refresh(h,body);
}

void player_race_body_policy(PlayerRigidBodyState& body,bool racing,float throttle,
                             unsigned support,float forward_vertical) noexcept {
    if(racing&&(throttle>0.3F||(support==4&&absf(forward_vertical)>0.15F)))
        body.linear_damping=false;
}

void player_rigid_body_normalize(PlayerRigidBodyState& body) noexcept {
    const auto forward=rigid_normalized(body.axes[0]);
    const auto up=rigid_normalized(rigid_cross(forward,body.axes[1]));
    body.axes={{forward,rigid_cross(up,forward),up}};
}

void player_distribute_torque(const VehicleHandling& h, int gear,
                              PlayerEngineState& engine,
                              std::array<PlayerWheelWork,4>& w,
                              float transmitted,int owner_mode) noexcept {
    if (gear == kVehicleGearNeutral) return;
    engine.feedback_torque -= transmitted;
    const float geared = transmitted * h.gear_ratios[static_cast<std::size_t>(gear)];
    const float front = ((1.0F - h.rear_drive_fraction) * h.final_drive_rear) * geared;
    const float rear = (geared * h.final_drive_front) * h.rear_drive_fraction;
    const float front_right = differential_share(w[1].spin,w[0].spin+w[1].spin,
                                                  w[0].contact&&w[1].contact,owner_mode);
    const float rear_left = differential_share(w[2].spin,w[2].spin+w[3].spin,
                                                w[2].contact&&w[3].contact,owner_mode);
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
    w.cached_slip = w.slip; //58747D stores the same raw slip into160 and164
    // ABS attenuation587483..5874CF; declared ordinary owner flag0.
    if (w.slip <= -0.5F && w.torque < 0.0F && parking == 0.0F) w.torque *= 0.15F;
    const float friction=((w.surface.sliding_grip*h.tyres[k/2].sliding_friction)*w.friction_scale)*w.owner_grip;
    const float magnitude=sqrtf(w.velocity_lat*w.velocity_lat+w.slip*w.slip);
    w.sliding=false;
    w.force_long=0.0F; w.force_lat=0.0F;
    if (magnitude != 0.0F && (w.velocity_lat != 0.0F || w.velocity_long != 0.0F)) {
        const float scale=(1.0F/magnitude)*friction;
        const float force_scale=scale*w.load;
        const float capacity=(parking+service)*1.2F;
        //58759A..5875C7 supplies the LONGITUDINAL fraction of the
        // velocity magnitude to587220, independently of slip magnitude.
        // Its temporary push shifts stack operands by four bytes.
        const float velocity_magnitude=sqrtf(w.velocity_long*w.velocity_long+w.velocity_lat*w.velocity_lat);
        const float requested=((1.0F/velocity_magnitude)*friction*w.load)*absf(w.velocity_long);
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
            if (w.owner_speed*2.237F<1.0F&&friction>0.1F) {
                const float normalize=1.0F/friction;
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
    const float limit=(((w.surface.static_grip*h.tyres[k/2].static_friction)*w.friction_scale)*w.owner_grip)*w.load;
    if (demand>limit&&demand>0.001F) {
        const float factor=limit/demand;
        w.force_long*=factor; w.force_lat*=factor;
    } else if (boosted) w.force_long/=1.5F;
    //587948..5879AD publishes normalized friction utilization, not m/s.
    w.friction_usage=limit>0.001F?std::min(demand/limit,1.F):1.F;
    if(w.sliding||w.locked)w.friction_usage=1.F;
    if(!w.contact)w.friction_usage=0.F;
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
    const float surface=(0.002F*w.surface.rolling_drag)*w.spin; //5871AF:6EF2FC
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
    const float combined=engine.feedback_torque+crank;
    if(engine.feedback_torque<crank&&combined<0&&gearbox.feedback_scale!=0) {
        //705EA0/700048: preserve454F30's interpolation/store order.
        static constexpr float x[]{-10.F,-7.5F,-3.5F,-.3F,-.05F};
        static constexpr float y[]{1.F,.96F,.925F,.875F,0.F};
        float factor=y[0];
        if(combined>=x[4])factor=y[4];
        else if(combined>x[0])for(unsigned i=0;i<4;++i)if(combined<x[i+1]) {
            const float u=(combined-x[i])/(x[i+1]-x[i]);
            factor=u*y[i+1]+(1.F-u)*y[i];break;
        }
        engine.feedback_torque+=(factor*gearbox.feedback_scale)*combined;
    }
}

void player_gear_integrate(const VehicleHandling& h,PlayerGearState& gearbox,float dt,
                           float request_scale) noexcept {
    static constexpr float rates[4]={-3.0F,0.0F,1.5F,0.0F}; //700034
    float rate=h.clutch_rate*request_scale; //46DFAF..46DFB2, gearboxDC
    if(gearbox.shift_quality>0.F)rate*=1.F+gearbox.shift_quality;
    else if(gearbox.shift_quality<0.F&&gearbox.requested_gear>=3)rate*=.35F; //6B7204
    rate*=rates[gearbox.mode];
    gearbox.clutch+=dt*rate;
}

void player_gear_update(const VehicleHandling& h,int& gear,PlayerGearState& box,
                        PlayerEngineState& engine,std::array<PlayerWheelWork,4>& w,
                        float throttle,float owner_speed_magnitude,
                        const std::array<float,4>* offset_bounds,std::uint32_t* world_random,
                        bool assisted,bool clutch_override) noexcept {
    box.wheel_engine_speed=wheel_speed(h,gear,w);
    const auto needs_disengage=[&]() {
        return throttle<0.3F && box.wheel_engine_speed<=h.idle_rpm*kPlayerRpmToRadS+0.0001F;
    };
    switch (box.mode) {
    case 3:
        //46DB60 descriptor+60 ordinary, +64 assisted; +68 below for launch.
        box.feedback_scale=assisted?-.7F:0.F;
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
        if (gear!=box.requested_gear) {
            box.mode=0;box.shift_pending=true;
            if(gear==kVehicleGearNeutral&&box.requested_gear==kVehicleGearFirst)
                box.feedback_scale=-.9F; // descriptor+68, not ordinary+60
        }
        if(clutch_override)box.mode=1;
        break;
    case 2:
        engine.throttle_multiplier=clutch_override?1.F:.7F;
        if (box.clutch>=1.0F) {
            box.clutch=1.0F;
            if (absf(engine.angular_speed-box.wheel_engine_speed)<50.0F||gear==kVehicleGearNeutral)
                box.mode=3;
        }
        if (needs_disengage()&&gear!=kVehicleGearNeutral) box.mode=0;
        if(clutch_override)box.mode=1;
        break;
    case 0:
        engine.throttle_multiplier=clutch_override?1.F:0.F;
        if (box.clutch<=0.0F) {box.clutch=0.0F;box.mode=1;}
        if(clutch_override)box.mode=1;
        break;
    case 1:
        engine.throttle_multiplier=clutch_override?1.F:0.F;
        gear=box.requested_gear;
        if (box.shift_pending||!needs_disengage()) {
            box.shift_pending=false;
            if(!clutch_override)box.mode=2;
            //46D760 draws from the world, not one identical per-car sequence.
            // Publish only when this real phase consumes its two random draws.
            if(world_random)box.rng_state=*world_random;
            player_gear_randomize(box,offset_bounds);
            if(world_random)*world_random=box.rng_state;
        }
        break;
    }
}

void player_gear_seed(PlayerGearState& box,std::uint32_t seed) noexcept {
    box.rng_state=seed;
    random_unit(box.rng_state);random_unit(box.rng_state);
    box.up_offset=0.0F;box.down_offset=0.0F;
}

void player_gear_randomize(PlayerGearState& box,const std::array<float,4>* bounds) noexcept {
    static constexpr std::array<float,4> initial{{-100.F,0.F,-100.F,0.F}};
    const auto& b=bounds?*bounds:initial;
    box.up_offset=random_unit(box.rng_state)*(b[1]-b[0])+b[0]; //46D760
    box.down_offset=random_unit(box.rng_state)*(b[3]-b[2])+b[2];
}

void player_engine_cutoff_update(const VehicleHandling& h,int gear,
                                 PlayerEngineState& engine,float seconds) noexcept {
    static constexpr float duration[8]={0.05F,0.04F,0.055F,0.06F,0.07F,0.075F,0.075F,0.075F}; //700004
    if (engine.cutoff_until==0.0F) {
        if (engine.angular_speed*kPlayerRadSToRpm>h.max_rpm-50.0F)
            engine.cutoff_until=seconds+duration[gear];
    } else if (seconds>engine.cutoff_until) engine.cutoff_until=0.0F;
}

void player_engine_reset(PlayerEngineState& engine, float rpm) noexcept {
    engine = {};
    engine.angular_speed = rpm * kPlayerRpmToRadS;
}

void player_drag_heat_update(const VehicleHandling& h,PlayerEngineState& engine,
    const PlayerGearState& box,int gear,std::int32_t clock,bool held) noexcept {
    if(engine.blown)return;
    const float seconds=float(clock)*0.00025F; //73AD34,6CCA38
    if(held||(gear==kVehicleGearFirst&&box.shift_quality>0))engine.heat_time=seconds;
    const float dt=seconds-engine.heat_time;
    engine.heat_time=seconds;
    const float rpm=engine.angular_speed*kPlayerRadSToRpm;
    const float rate=rpm>=h.redline_rpm?0.35F:-0.1F; //6FFFF8,700000; nitro addend is zero
    if(rate<=0||engine.heat<=1.F)engine.heat+=rate*dt;
    if(engine.heat<=0)engine.heat=0;
    if(engine.heat>=0.95F) {
        if(engine.overheat_since==0)engine.overheat_since=seconds;
        if(seconds-engine.overheat_since>1.F)engine.blown=true;
    } else engine.overheat_since=0;
}

float player_drag_launch_rpm(const VehicleHandling& h) noexcept {
    const float base=(h.redline_rpm-h.idle_rpm)*h.shift_fraction+h.idle_rpm;
    const float factor=h.rear_drive_fraction<.001F?1.F:h.rear_drive_fraction>.999F?1.1F:1.2F;
    return base*factor;
}
float player_drag_shift_window(unsigned category,unsigned gear,float scale) noexcept {
    static constexpr float windows[4][9]={ //6FFF60; RideInfo performance category
        {0,560,360,175,160,140,140,0,0},{0,600,400,230,220,160,160,0,0},
        {0,850,550,300,250,200,180,0,0},{0,1100,750,400,325,290,230,0,0}};
    return category<4&&gear<9?windows[category][gear]*scale:0.F;
}
float player_drag_shift_quality(float rpm,float upshift,unsigned category,
    unsigned gear,float window_scale) noexcept {
    if(category>=4||gear>=9)return 0;
    const float difference=upshift-rpm,window=player_drag_shift_window(category,gear,window_scale);
    if(difference>=0) {
        const float quality=std::max(0.F,1.F-difference/(0.95F*window));
        return quality<=0?0.F:quality<0.5F?0.5F:0.85F;
    }
    //46EA09 compares the late window against abs(difference); equality misses.
    return -difference<(1.F-0.95F)*window?1.F:-1.F;
}

void player_drag_shift_boost(PlayerGearState& box,PlayerEngineState& engine,
    std::int32_t clock) noexcept {
    const float seconds=float(clock)*0.00025F;
    if(box.mode==3&&box.shift_quality>0&&box.shift_boost_since==0)
        box.shift_boost_since=seconds;
    if(box.mode!=3||box.shift_boost_since<=0)return;
    const float elapsed=seconds-box.shift_boost_since;
    const float bonus=((0.5F-elapsed)/0.5F)*box.shift_quality*2.F;
    if(bonus>0)engine.throttle_multiplier=1.F+bonus;
    if(elapsed>=0.5F){box.shift_boost_since=0;box.shift_quality=0;}
}

bool player_request_shift_tables(const VehicleHandling& h,
    std::array<float,kVehicleGearSlots>& up,
    std::array<float,kVehicleGearSlots>& down) noexcept {
    std::array<float,kVehicleGearSlots> next_up{},next_down{};
    const int top=kVehicleGearFirst+static_cast<int>(h.forward_gears)-1;
    if(top==kVehicleGearFirst) {
        next_up[static_cast<std::size_t>(top)]=h.redline_rpm;
        up=next_up;down=next_down;
        return true; // No search/curve operation is required for one gear.
    }
    //46D1F8 stores8/(max-idle); the descriptor's9 is its point count.
    const float scale=8.0F/(h.max_rpm-h.idle_rpm);
    if(!std::isfinite(scale)) return false;
    const auto torque=[&](float rpm,float& value) {
        // Reject an unrepresentable required583EA0 coordinate before its
        // integer conversion. The existing curve supplies the arithmetic.
        const float shifted=rpm-h.idle_rpm;
        const float u=shifted*scale;
        if(!request_binary32(shifted,double(rpm)-double(h.idle_rpm))||
           !request_binary32(u,double(shifted)*double(scale))) return false;
        // Check only the interpolation branch583EDA..F13 actually needs.
        if(u>-1.0F&&u<static_cast<float>(h.torque.size()-1)) {
            const int index=static_cast<int>(u);
            float base=static_cast<float>(index);
            if(base>u) base-=1.0F;
            const float fraction=u-base,weight=1.0F-fraction;
            const float left=weight*h.torque[static_cast<std::size_t>(index)];
            const float right=fraction*h.torque[static_cast<std::size_t>(index+1)];
            if(!request_binary32(left,double(weight)*double(h.torque[static_cast<std::size_t>(index)]))||
               !request_binary32(right,double(fraction)*double(h.torque[static_cast<std::size_t>(index+1)]))||
               !request_binary32(left+right,double(left)+double(right))) return false;
        }
        value=curve(h.torque.data(),static_cast<int>(h.torque.size()),
                    h.idle_rpm,h.max_rpm,rpm);
        return std::isfinite(value);
    };
    for(int g=kVehicleGearFirst;g<top;++g) {
        const auto slot=static_cast<std::size_t>(g);
        const float current=h.gear_ratios[slot],following=h.gear_ratios[slot+1];
        const float sum=h.redline_rpm+h.idle_rpm;
        if(!std::isfinite(sum)) return false;
        float rpm=sum*0.5F,shift=h.redline_rpm-100.0F;
        if(!request_binary32(rpm,double(sum)*0.5)) return false;
        if(rpm<h.redline_rpm) {
            for(;;) {
                float here=0.0F,there=0.0F;
                if(!torque(rpm,here)) return false;
                if(absf(current)>1.0e-5F) {
                    const float inverse=1.0F/current;
                    //46D2B2 FST retains the live wide-exponent inverse as
                    // well as its binary32 copy; require both to agree.
                    if(!request_binary32(inverse,1.0/double(current))) return false;
                    const float first_product=inverse*rpm;
                    const float projected=first_product*following;
                    if(!request_binary32(first_product,double(inverse)*double(rpm))||
                       !request_binary32(projected,double(first_product)*double(following))||
                       !torque(projected,there)) return false;
                    //583EA0 ret4 removes the projected-RPM argument.
                    //46D2C6 reads the stored inverse at restored esp+18;
                    //46D2CA then reads next ratio at restored esp+14.
                    const float scaled=there*inverse;
                    if(!request_binary32(scaled,double(there)*double(inverse))) return false;
                    const float next_torque=scaled*following;
                    if(!request_binary32(next_torque,double(scaled)*double(following))) return false;
                    there=next_torque;
                }
                if(there>here) {shift=rpm;break;}
                const float advanced=50.0F+rpm;
                if(!std::isfinite(advanced)||!(advanced>rpm)) return false;
                rpm=advanced;
                if(!(rpm<h.redline_rpm)) break;
            }
        }
        next_up[slot]=shift;
        if(absf(current)>1.0e-5F) {
            //46D338 rounds the division before46D33C multiplies the
            // stored up slot;46D340 stores the next down slot.
            const float ratio=following/current;
            const float threshold=ratio*shift;
            if(!request_binary32(ratio,double(following)/double(current))||
               !request_binary32(threshold,double(ratio)*double(shift))) return false;
            next_down[slot+1]=threshold;
        }
    }
    next_up[static_cast<std::size_t>(top)]=h.redline_rpm;
    up=next_up;down=next_down;
    return true;
}

float player_engine_torque(const VehicleHandling& h,
                          const PlayerEngineState& engine, int gear,
                          float throttle, const VehicleInput* input,
                          float nitrous_rate) noexcept {
    const float rpm = engine.angular_speed * kPlayerRadSToRpm;
    const float effective = engine.cutoff_until == 0.0F ?
        engine.throttle_multiplier * throttle : 0.0F;
    float drive = curve(h.torque.data(), 9, h.idle_rpm, h.max_rpm, rpm) * effective;
    if (input && input->nitrous_active) {
        NitrousCurveDescriptor descriptor;
        if (descriptor.initialize(h.idle_rpm,h.redline_rpm)) {
            NitrousTorqueContext context;
            context.rpm_origin=descriptor.rpm_origin;
            context.rpm_scale=descriptor.rpm_scale;
            context.clock=input->nitrous_clock;
            context.activation_seconds=input->nitrous_activation_seconds;
            context.flag_78a344=input->drag_mode;
            drive=nitrous_engine_torque(drive,engine.angular_speed,nitrous_rate,true,context);
        }
    }
    if(engine.heat>0.7F)drive*=((1.F-(engine.heat-0.7F)/(1.F-0.7F))/1.1F); //46D0C4
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

PlayerAeroForces player_aero_forces(const VehicleHandling& h,
                                  const PlayerRigidBodyState& body,
                                  const PlayerAeroCache& cache,
                                  const PlayerInitialAeroConfiguration& config) noexcept {
    PlayerAeroForces result;
    if (!cache.enabled) return result;
    const auto& v = body.velocity;
    const auto& axes = body.axes;
    const float speed = static_cast<float>(vehicle_sqrt(
        static_cast<double>((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]))) / cache.speed_divisor;
    const float gain = (config.drag_gain-1.0F)*(1.0F-cache.processed_throttle)+1.0F;
    const float drag = -(((cache.drag_scale*h.drag)*speed)*gain);
    for (unsigned j=0;j<3;++j) result.forces[0][j]=v[j]*drag;
    if (cache.support_kind>=0.5F)
        result.arms[0][2]=(1.0F-cache.processed_throttle)*config.drag_arm_z;
    float up=axes[2][2]>0.0F?axes[2][2]:0.0F;
    if (cache.support_kind>=0.5F) up=1.0F;
    PlayerRigidVector direction{{1,0,0}};
    if (speed>9.9999997473787516e-05F) {
        const float inverse=1.0F/speed;
        for (unsigned j=0;j<3;++j) direction[j]=v[j]*inverse;
    }
    float cosine=(direction[2]*axes[0][2]+direction[1]*axes[0][1])+direction[0]*axes[0][0];
    if (cosine<0.0F) cosine=0.0F;
    const float fourth=((cosine*cosine)*cosine)*cosine;
    float front=0.0F,rear=0.0F;
    if (cache.support_kind==0.0F) {
        const float total=(((h.downforce_rear+h.downforce_front)*fourth)*up)*speed;
        front=total*0.40000000596046448F;
        rear=total*0.40999999642372131F;
    } else {
        front=((fourth*h.downforce_front)*up)*speed;
        rear=((fourth*h.downforce_rear)*up)*speed;
    }
    const auto transform=[&axes](const PlayerRigidVector& local) {
        PlayerRigidVector world{};
        for (unsigned j=0;j<3;++j)
            world[j]=(((local[2]*axes[2][j]+local[1]*axes[1][j])+local[0]*axes[0][j])+0.0F)+0.0F;
        return world;
    };
    result.forces[1]=transform({{0,0,-front}});
    result.forces[2]=transform({{0,0,-rear}});
    result.arms[1]=transform({{1,0,0}});
    result.arms[2]=transform({{-1,0,0}});
    result.downforce=front+rear;
    return result;
}

float player_aero_apply(const VehicleHandling& h,PlayerRigidBodyState& body,
                       const PlayerAeroCache& cache,
                       const PlayerInitialAeroConfiguration& config,
                       PlayerRigidVector* cached_drag) noexcept {
    if (!cache.enabled) return 0.0F;
    const auto work=player_aero_forces(h,body,cache,config);
    if (cached_drag) *cached_drag=work.forces[0];
    for (unsigned k=0;k<3;++k) {
        const auto& f=work.forces[k];const auto& arm=work.arms[k];
        player_rigid_body_add_contact_force(body,{f[0],f[1],f[2]},
                                           {arm[0],arm[1],arm[2]});
    }
    return work.downforce;
}

std::array<float,4> player_aero_preloads(const VehicleHandling& h,
                                       float speed,bool enabled,
                                       unsigned actor_state,bool cg_enabled) noexcept {
    const float cg=cg_enabled?h.centre_of_mass[0]:0.0F;
    const float front=h.wheels[0].longitudinal-cg;
    const float rear=h.wheels[3].longitudinal-cg;
    const float rear_share=(front/(front-rear))*0.5F;
    const float front_share=0.5F-rear_share;
    float aero=0.0F;
    if (enabled) aero=(h.downforce_rear+h.downforce_front)*(speed>0.0F?speed:0.0F);
    const float gravity=actor_state==1?12.25F:9.8000001907348633F;
    const float total=aero*h.aero_preload+gravity*h.mass;
    return {{total*front_share,total*front_share,total*rear_share,total*rear_share}};
}

} // namespace nfsu
