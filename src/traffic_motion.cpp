#include "nfsu/traffic_motion.h"
#include "nfsu/det_sqrt.h"
#include <algorithm>
#include <cmath>
namespace nfsu {
namespace {
//564B10: the mover deliberately uses the quantized sine for both axes.
// Double intermediates mirror the x87 expression; the caller stores float.
double sine(std::uint16_t word) {
    double x=double(word)*double(9.58738019107841e-5F),sign=1;
    if(x>=double(4.71238899230957F))x-=double(6.2831854820251465F);
    else if(x>=double(1.5707963705062866F)){x-=double(3.1415927410125732F);sign=-1;}
    const double x2=x*x,x3=x2*x,x5=x3*x2,x7=x5*x2,x9=x7*x2;
    return ((((x-x3*double(.1666666716337204F))+x5*double(.008333333767950535F))-
        x7*double(.00019841270113829523F))+x9*double(2.7557318844628753e-6F))*sign;
}
std::uint16_t angle(double value) {
    // x87 conversion helper674898 truncates, then564B10 consumes AX only.
    return static_cast<std::uint16_t>(static_cast<std::int32_t>(value));
}
double curve(const float* values,float speed) {
    const double coordinate=double(speed)*double(.12201818078756332F);
    const int index=int(coordinate);
    if(index<0)return values[0];
    if(index>=3)return values[3];
    const float stored=float(coordinate);
    const double fraction=double(stored)-std::floor(double(stored));
    return (1-fraction)*double(values[index])+fraction*double(values[index+1]);
}
}
bool traffic_motion_step(const TrafficMotionInput& in,TrafficMotion& state) noexcept {
    for(float v:{state.x,state.y,state.heading,state.speed,in.front_axle,in.rear_axle,in.throttle,in.brake,in.dt})
        if(!std::isfinite(v))return false;
    if(in.dt<0||in.throttle<0||in.throttle>1||in.brake<0||in.brake>1||
       state.speed<0||state.speed>24.58650016784668F||in.front_axle<=in.rear_axle)return false;
    const double scaled=double(state.heading)*double(10430.3779296875F);
    if(scaled < -2147483648.0 || scaled+double(16383.9990234375F) > 2147483647.0)return false;
    auto next=state;
    const float stored_angle=float(scaled);
    if(double(stored_angle)<-2147483648.0||double(stored_angle)+double(16383.9990234375F)>2147483647.0)return false;
    const float cosine=float(sine(angle(double(stored_angle)+double(16383.9990234375F))));
    const float sn=float(sine(angle(stored_angle)));
    double steering=double(in.steering)*double(9.58738019107841e-5F);
    if(steering>double(3.141590118408203F))steering-=double(6.283180236816406F);
    const float wheelbase=float(-double(in.rear_axle)+double(in.front_axle));
    const float distance=float(double(in.dt)*double(state.speed));
    if(steering<=double(wheelbase)*double(.009999999776482582F)&&
       steering>=-double(wheelbase)*double(.009999999776482582F)) {
        const float dy=float(double(sn)*distance);
        next.x=float(double(cosine)*distance+state.x);next.y=float(double(state.y)+dy);
    }else {
        const float rear_x=float(double(in.rear_axle)*cosine+state.x);
        const float rear_y=float(double(in.rear_axle)*sn+state.y);
        const double radius=double(wheelbase)/steering;
        const float center_x=float(-double(sn)*radius+rear_x);
        const float center_y=float(double(cosine)*radius+rear_y);
        const float delta=float(double(distance)/radius);
        const double rotated=double(delta)*double(10430.3779296875F);
        const float stored=float(rotated);
        if(!std::isfinite(stored)||double(stored)<-2147483648.0||double(stored)+double(16383.9990234375F)>2147483647.0)return false;
        const float c=float(sine(angle(double(stored)+double(16383.9990234375F))));
        const double s=sine(angle(stored));
        const double dx=double(state.x)-center_x,dy=double(state.y)-center_y;
        const float rx=float(dx*c-dy*s),ry=float(dx*s+dy*c);
        next.x=float(double(center_x)+rx);next.y=float(double(center_y)+ry);
        next.heading=float(double(state.heading)+delta);
    }
    constexpr float accelerate[]={3.5762181282043457F,2.682163715362549F,2.011622667312622F,1.3410818576812744F};
    constexpr float brake[]={6.705409049987793F,13.410818099975586F,20.116228103637695F,26.821636199951172F};
    const float gain=float(curve(accelerate,state.speed)*double(in.throttle)*double(in.dt));
    next.speed=std::clamp(float(double(state.speed)+(double(gain)-curve(brake,state.speed)*double(in.brake)*double(in.dt))),0.F,24.58650016784668F);
    for(float v:{next.x,next.y,next.heading,next.speed})if(!std::isfinite(v))return false;
    state=next;return true;
}
bool traffic_motion_publish(const std::array<float,3>& previous,
    const std::array<float,3>& current,float heading,float speed,float dt,
    std::uint16_t previous_heading,TrafficBodyPublication& output) noexcept {
    for(float x:previous)if(!std::isfinite(x))return false;
    for(float x:current)if(!std::isfinite(x))return false;
    if(!std::isfinite(heading)||!std::isfinite(speed)||speed<0||
       !std::isfinite(dt)||dt<=0)return false;
    const float scaled=heading*10430.3779296875F;
    if(!std::isfinite(scaled)||scaled< -2147483648.F||scaled>=2147483648.F)return false;
    TrafficBodyPublication next;next.forward=speed;
    next.heading=static_cast<std::uint16_t>(static_cast<std::int32_t>(scaled));
    if(std::abs(speed)>1.e-5F) {
        std::array<float,3> delta;
        for(unsigned k=0;k<3;++k)delta[k]=current[k]-previous[k];
        //5673E0 divides length by the requested magnitude before reciprocal.
        const float length=float(det_sqrt(double((delta[0]*delta[0]+delta[1]*delta[1])+delta[2]*delta[2])));
        const float denominator=length/speed;
        if(denominator==0)next.velocity={speed,0,0};
        else {
            const float inverse=1.F/denominator;
            for(unsigned k=0;k<3;++k)next.velocity[k]=delta[k]*inverse;
        }
    }
    const auto& v=next.velocity;
    next.magnitude=float(det_sqrt(double((v[0]*v[0]+v[1]*v[1])+v[2]*v[2])));
    int difference=static_cast<std::uint16_t>(next.heading-previous_heading);
    if(difference>=32768)difference-=65536;
    next.yaw_rate=(float(difference)*9.587379463482648e-5F)/dt;
    if(!std::isfinite(next.magnitude)||!std::isfinite(next.yaw_rate))return false;
    for(float x:next.velocity)if(!std::isfinite(x))return false;
    output=next;return true;
}

}
