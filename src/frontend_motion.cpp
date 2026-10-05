#include "nfsu/frontend_motion.h"
#include <algorithm>
#include <cmath>
namespace nfsu {
float frontend_fixed_sine(std::uint16_t phase) noexcept {
    // Float constants are the original executable's binary32 values. Wider
    // arithmetic preserves its x87 intermediates until the returned float.
    double x = double(phase) * double(9.58738019107841e-5F);
    double sign = 1;
    if(x >= double(4.71238899230957F)) x -= double(6.2831854820251465F);
    else if(x >= double(1.5707963705062866F)) {
        x -= double(3.1415927410125732F); sign = -1;
    }
    const double x2=x*x, x5=x2*x*x2, x7=x5*x2;
    return float((x7*x2*double(2.7557318844628753e-6F) +
        ((x5*double(0.008333333767950535F) +
        (x-x2*x*double(0.1666666716337204F))) -
        x7*double(0.00019841270113829523F))) * sign);
}
std::uint16_t frontend_wheel_phase(std::uint16_t phase,float travel,float radius) noexcept {
    if(!std::isfinite(travel) || !std::isfinite(radius) || radius<=0) return phase;
    const double increment=double(travel)/(double(radius)*double(6.283180236816406F))*double(65535.9453125F);
    if(increment < -2147483648.0 || increment >= 2147483648.0) return phase;
    // Native CRT conversion truncates before the 16-bit accumulator wraps.
    return static_cast<std::uint16_t>(unsigned(phase)+static_cast<std::uint32_t>(static_cast<std::int32_t>(increment)));
}
bool frontend_road_glow(const float (&point)[3],const float (&toward_eye)[3],
    const float (&right)[3],const float (&up)[3],float facing,float depth_scale,
    unsigned mode,FrontendRoadGlow& out) noexcept {
    out={};
    if(mode>2 || !std::isfinite(facing) || !std::isfinite(depth_scale))return false;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(point[i]) || !std::isfinite(toward_eye[i]) ||
        !std::isfinite(right[i]) || !std::isfinite(up[i]))return false;
    // Type9 descriptor6F40D8: exponent1, size30..100, offset4,
    // screen-depth clamp .001.. .1. Native floor mode skips the1.5 scale.
    const double gain=std::min(1.0,double(std::max(0.0F,facing)));
    const double size=(30+70*gain)*std::clamp(double(depth_scale),
        double(.0010000000474974513F),double(.10000000149011612F))*(mode==2?1:1.5);
    if(size<=0)return false;
    const int signs[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
    for(unsigned corner=0;corner<4;++corner)for(unsigned axis=0;axis<3;++axis) {
        const double extent=size*(signs[corner][0]*double(right[axis])+
            signs[corner][1]*double(up[axis]))*(mode==2 && axis==2?10:1);
        out.corners[corner][axis]=float(double(point[axis])+4*double(toward_eye[axis])+extent);
    }
    out.rgba[0]=110;out.rgba[1]=100;out.rgba[2]=75;
    out.rgba[3]=static_cast<unsigned char>(110+40*gain);
    return true;
}
void FrontendRoad::update(float seconds,std::uint32_t ticks,bool forced) noexcept {
    if(!std::isfinite(seconds) || seconds<0) return;
    // 0x4AE1F0 uses the previous speed for this frame's velocity.
    float kmh=speed_kmh;
    if(speed_kmh<target_kmh) speed_kmh=std::min(target_kmh,speed_kmh+seconds*10);
    else if(speed_kmh>target_kmh) speed_kmh=std::max(target_kmh,speed_kmh-seconds*10);
    if(forced) kmh=50;
    else {
        const double time=double(ticks)*double(0.00025F);
        const auto phase=[](double value) {
            return static_cast<std::uint16_t>(static_cast<std::int64_t>(value));
        };
        const float wobble = float(2.0 * frontend_fixed_sine(phase((time*.125+51)*double(10430.3779296875F))) +
            3.0 * frontend_fixed_sine(phase((time*double(0.2F)+117)*double(10430.3779296875F))));
        const float attenuation=kmh<5?0:kmh<20?(kmh-5)/15:1;
        kmh += wobble*attenuation;
    }
    const double exact_velocity=double(kmh)*1000*double(0.00027777778450399637F);
    velocity=float(exact_velocity);
    const double exact_delta=double(seconds)*exact_velocity;
    delta=float(exact_delta); offset=float(double(offset)+exact_delta);
    while(offset>200) offset-=200; // The native comparison is strictly >.
}
unsigned FrontendRoad::placements(unsigned view,std::array<FrontendRoadPlacement,5>& out) const noexcept {
    unsigned extent=0,family=0;
    if(view>=1 && view<=3) extent=2;
    else if(view>=4 && view<=5) {extent=1;family=1;}
    else if(view>=10 && view<=15) {extent=1;family=2;}
    else return 0;
    const int segment=int(double(offset)/200*2);
    unsigned group=unsigned((segment-int(extent))&1);
    float start=float(double(offset)-double(segment)*100-double(extent)*100);
    for(unsigned i=0;i<extent*2+1;++i) {
        out[i]={group,family,-start,0.291F};group^=1;start+=100;
    }
    return extent*2+1;
}
}
