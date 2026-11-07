#include "nfsu/drift.h"
#include <algorithm>
#include <cmath>
namespace nfsu {
float drift_score_factor(const DriftScoreSample& s,float& highwater) noexcept {
    if(s.grade>3 || !std::isfinite(s.speed) || !std::isfinite(s.slip) ||
       !std::isfinite(s.yaw) || !std::isfinite(s.progress) || !std::isfinite(highwater))return 0;
    // 6B7880/6B7890. 43E606 selects the stricter threshold when the car
    // rotates out of the slide or the style phase is not qualified (zero).
    constexpr float strict[]={15.277778625488281F,13.94444465637207F,12.777777671813965F,10.083333015441895F};
    constexpr float qualified[]={12.5F,11.111111640930176F,9.722222328186035F,8.333333969116211F};
    const bool counter=std::abs(s.yaw)>.10000000149011612F && double(s.slip)*s.yaw<0;
    const float minimum=(s.phase!=0||counter?strict:qualified)[s.grade];
    float result=0;
    if(s.progress>highwater && !s.blocked &&
       std::abs(s.slip)>double(5.0F)*.01745329238474369F && s.speed>minimum) {
        // The original stores the quotient, then squares that float.
        const float ratio=float(double(s.speed)/10.083333015441895F);
        result=float(double(ratio)*ratio*std::abs(s.slip));
    }
    highwater=std::max(highwater,s.progress);
    return result;
}
DriftForces drift_forces(std::uint16_t word,float yaw,float split,unsigned difficulty) noexcept {
    DriftForces out;
    if(!std::isfinite(yaw)||!std::isfinite(split)||split<0||split>1||difficulty>2)return out;
    // 479350 uses the original runtime x87 precision (CW007F).
    float angle=float(word)*9.58738019107841e-5F;
    if(angle>3.141590118408203F)angle-=6.283180236816406F;
    const float magnitude=std::abs(angle);
    // 68A130 initializes the authored angle/response curve.
    constexpr float x[]={0,.7853981852531433F,1.3089969158172607F,1.5533430576324463F,1.5707963705062866F};
    constexpr float y[]={0,.699999988079071F,.8999999761581421F,1,0};
    constexpr float damping[3][3]={{-14,-11,-7},{-9,-7.5F,-5},{-10,-9,-8}};
    const unsigned drive=split<.20000000298023224F?0:split>.800000011920929F?2:1;
    if(angle*yaw>0) {
        float gain=0;
        for(unsigned i=0;i<4;++i)if(magnitude>=x[i] && magnitude<x[i+1]) {
            const float t=(magnitude-x[i])/(x[i+1]-x[i]);
            gain=(1.F-t)*y[i]+t*y[i+1];break;
        }
        out.torque=(damping[drive][difficulty]*gain)*yaw;
    }
    // 705E1C, ten samples over [0,1]. Original 583EA0 interpolation.
    constexpr float grip[]={1.100000023841858F,.9300000071525574F,.8600000143051147F,
        .8199999928474426F,.800000011920929F,.7919999957084656F,.7799999713897705F,
        .7699999809265137F,.7649999856948853F,.7599999904632568F};
    constexpr float yaw_weight=.550000011920929F;
    const float input=magnitude*(1.F-yaw_weight)+std::abs(yaw)*yaw_weight;
    const float scaled=input*9.F;
    if(scaled>=9)out.wheel_scale=grip[9];
    else {
        const unsigned i=unsigned(scaled);
        const float fraction=scaled-float(i);
        out.wheel_scale=(1.F-fraction)*grip[i]+fraction*grip[i+1];
    }
    return out;
}
}
