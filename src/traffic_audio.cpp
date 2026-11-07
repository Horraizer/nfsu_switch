#include "nfsu/traffic_audio.h"
namespace nfsu::audio {
namespace {
#include "engine_balance.inc"
int distance_curve(float value)noexcept {
    const float scaled=value*511.999908447265625F;
    if(scaled>511.5F)return 0;
    const int index=int(scaled);
    const float fraction=index==511?0.F:scaled-float(index);
    const auto entry=[](int i){return ((32767-kEngineBalance[511-i])*kEngineBalance[i])>>15;};
    const int first=entry(index),second=fraction>0.F&&index<511?entry(index+1):first;
    return std::min(32767,first+int(fraction*float(second-first)));
}
float distance(const std::array<float,3>& a,const std::array<float,3>& b)noexcept {
    const float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];
    return std::sqrt((z*z+y*y)+x*x);
}
}
void TrafficAudioPosition::tick(const std::array<float,3>& source,const std::array<float,3>& human,
    const std::array<float,3>& eye,const std::array<float,3>& forward,float milliseconds)noexcept {
    const float d=distance(source,human);
    doppler=milliseconds==0.F?1.F:343.F/((((d-previous_distance)/milliseconds)*0.5F)*1000.F+343.F);
    previous_distance=d;
    auto delta=source;for(unsigned i=0;i<3;++i)delta[i]-=eye[i];delta[2]=0;
    auto axis=forward;axis[2]=0;
    delta=TrafficAudioController::normalized(delta);axis=TrafficAudioController::normalized(axis);
    pan=std::uint16_t(player_velocity_angle(axis[0],axis[1])-player_velocity_angle(delta[0],delta[1]));
    const int phase=(pan&0x3fff)*2;
    const int level=distance_curve(std::clamp((d-10.F)/40.F,0.F,1.F));
    const int shaped=(((32767-phase)*level)>>15)+((level*phase)>>15);
    gain=(18629*shaped)>>15;
}
bool CollisionAudioPosition::tick(const std::array<float,3>& point,const std::array<float,3>& actor,
    const std::array<float,3>& eye,const std::array<float,3>& forward)noexcept {
    for(const auto& v:{point,actor,eye,forward})for(float x:v)if(!std::isfinite(x))return false;
    const float d=distance(point,actor);if(!std::isfinite(d))return false;
    auto delta=point;for(unsigned k=0;k<3;++k)delta[k]-=eye[k];delta[2]=0;
    auto axis=forward;axis[2]=0;
    delta=TrafficAudioController::normalized(delta);axis=TrafficAudioController::normalized(axis);
    const int angle=std::uint16_t(player_velocity_angle(axis[0],axis[1])-player_velocity_angle(delta[0],delta[1]));
    const unsigned quadrant=unsigned(angle)>>14;const int phase=(angle&0x3fff)*2;
    static constexpr float far[4]={80.F,30.F,30.F,30.F};
    const auto level=[&](unsigned q){return distance_curve(std::clamp((d-10.F)/(far[q]-10.F),0.F,1.F));};
    const int shaped=(((32767-phase)*level(quadrant))>>15)+((level((quadrant+1)&3)*phase)>>15);
    gain=(32767*shaped)>>15;pan=angle;return true;
}
void TurboAudioPosition::tick(const std::array<float,3>& source,const std::array<float,3>& eye,
    const std::array<float,3>& forward)noexcept {
    const float d=distance(source,eye);
    auto delta=source;for(unsigned k=0;k<3;++k)delta[k]-=eye[k];delta[2]=0;
    auto axis=forward;axis[2]=0;
    delta=TrafficAudioController::normalized(delta);axis=TrafficAudioController::normalized(axis);
    pan=std::uint16_t(player_velocity_angle(axis[0],axis[1])-player_velocity_angle(delta[0],delta[1]));
    const unsigned quadrant=unsigned(pan)>>14;const int phase=(pan&0x3fff)*2;
    static constexpr float far[4]={25.F,15.F,25.F,15.F};
    const auto level=[&](unsigned q){return distance_curve(std::clamp((d-1.F)/(far[q]-1.F),0.F,1.F));};
    const int shaped=(((32767-phase)*level(quadrant))>>15)+((level((quadrant+1)&3)*phase)>>15);
    gain=(17414*shaped)>>15;
}
void RemoteAudioPosition::tick(const std::array<float,3>& source,const std::array<float,3>& human,
    const std::array<float,3>& eye,const std::array<float,3>& forward,float milliseconds)noexcept {
    const float d=distance(source,human),camera_distance=distance(source,eye);
    doppler=milliseconds==0.F?1.F:343.F/((((d-previous_distance)/milliseconds)*1.2F)*1000.F+343.F);
    previous_distance=d;
    auto delta=source;for(unsigned i=0;i<3;++i)delta[i]-=eye[i];delta[2]=0;
    auto axis=forward;axis[2]=0;
    delta=TrafficAudioController::normalized(delta);axis=TrafficAudioController::normalized(axis);
    pan=std::uint16_t(player_velocity_angle(axis[0],axis[1])-player_velocity_angle(delta[0],delta[1]));
    const int quadrant=pan>>14,phase=(pan&0x3fff)*2;
    static constexpr float far[4]={150.F,120.F,100.F,120.F};
    const auto level=[&](unsigned q){return distance_curve(std::clamp((camera_distance-2.F)/(far[q]-2.F),0.F,1.F));};
    const int shaped=(((32767-phase)*level(unsigned(quadrant)))>>15)+((level(unsigned((quadrant+1)&3))*phase)>>15);
    gain=(12149*shaped)>>15;
}

}
