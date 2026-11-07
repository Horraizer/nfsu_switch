#include "nfsu/world_motion.h"
#include <cmath>
namespace nfsu {
namespace {
float animation_time(bool present,const WorldMotionClock& clock,float phase,float target) {
    if(!present||phase>clock.end||target>clock.end)return 9999999.F;
    return ((target-phase)/clock.rate)/30.F;
}
float boundary_time(bool present,const WorldMotionClock& clock,float target,float delay) {
    return animation_time(present,clock,present?clock.phase:-1.F,target)+
        ((!present||(clock.flags&0x80U))?delay:0.F);
}
}
float world_obstacle_distance(float target,float current,float length,bool circuit) noexcept {
    float distance=target-current;
    if(circuit){if(distance<length*-.5F)distance+=length;else if(distance>length*.5F)distance-=length;}
    return distance;
}
bool world_obstacle_clear(const WorldObstacleWindow& in,float distance,float speed,float acceleration) noexcept {
    if(!in.bound||!(distance>0))return true;
    const float arrival=world_train_arrival(distance,speed,acceleration)-.3F;
    const float first=boundary_time(in.head_present,in.head,in.enter,in.delay);
    const float last=boundary_time(in.tail_present,in.tail,in.leave,in.delay);
    const bool clear=arrival<first||last<arrival;
    if(first<0&&last>0&&std::abs(speed)<=.001F&&distance<30.F)return false;
    return clear;
}
} // namespace nfsu
