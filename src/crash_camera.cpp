#include "nfsu/crash_camera.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/race_contacts.h"
#include <algorithm>
#include <cmath>
namespace nfsu {
namespace {
float length(const AiDriverPoint& p) {return float(vehicle_sqrt((p[2]*p[2]+p[1]*p[1])+p[0]*p[0]));}
AiDriverPoint subtract(const AiDriverPoint& a,const AiDriverPoint& b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
AiDriverPoint point(const VehicleVec3& p) {return {float(p.x),float(p.y),float(p.z)};}
float cubic(float t,float a,float b,float c,float d) {
    const float t2=t*t,t3=t2*t;
    const float w0=(((t2+t2)-t3)-t)*.5F;
    const float w1=((t3*3.F-t2*5.F)+2.F)*.5F;
    const float w2=((t2*4.F-t3*3.F)+t)*.5F,w3=(t3-t2)*.5F;
    return w3*d+(w2*c+(w1*b+w0*a));
}
AiDriverPoint edge(const AiPathRoadPoint& r,bool upper) {
    const auto& p=r.point;const float lateral=upper?p.unknown_28-2.F:-p.unknown_24+2.F;
    return {p.forward[1]*lateral+p.position[0],(-p.forward[0])*lateral+p.position[1],p.unknown_2c*lateral+p.height};
}
float curve(unsigned intensity,float a,float b) {return (float(intensity)/255.F)*(b-a)+a;}
float age(std::int32_t now,std::int32_t then) {return float(std::int64_t(now)-then)*.00025F;}
bool unit(AiDriverPoint& p) {const float n=length(p);if(!(n>0)||!std::isfinite(n))return false;const float inv=1.F/n;for(float& f:p)f*=inv;return true;}
}
bool crash_camera_road_visible(const AiRoadChain& road,const AiDriverPoint& from,const AiDriverPoint& to,
    unsigned first,unsigned last) noexcept {
    if(first>last||last>=road.count||road.count>road.cells.size())return false;
    const float dx=from[0]-to[0],dy=from[1]-to[1];
    for(unsigned i=first;i<=last;++i) {
        const auto& c=road.cells[i];const auto& p=c.point;
        const float lane=p.driving_offset[0]+c.offset[0];
        const float lower=lane+c.lower[0],upper=lane+c.upper[0];
        const float ax=p.forward[1]*lower+p.position[0],ay=(-p.forward[0])*lower+p.position[1];
        const float bx=p.forward[1]*upper+p.position[0],by=(-p.forward[0])*upper+p.position[1];
        const float rx=bx-ax,ry=by-ay,det=ry*dx-rx*dy;
        if(std::abs(det)<.0001F)return false;
        const float x=to[0]-ax,y=to[1]-ay,inverse=1.F/det;
        const float t=(y*rx-x*ry)*inverse,u=(y*dx-x*dy)*inverse;
        if(!(t>=0&&t<=1&&u>=0&&u<=1))return false;
    }
    return true;
}
bool crash_camera_road_shot(const AiRoadChain& road,int category,unsigned track,
    const VehicleState& car,const CrashCameraWorld& world,CrashCameraShot& output) noexcept {
    if(category<0||category>=5||road.count>road.cells.size())return false;
    const auto actor=point(car.position);
    if(track==1001||track==1002) {
        const float x=actor[0]-(track==1001?1415.F:1290.9F),y=actor[1]+(track==1001?920.F:905.F);
        if(float(vehicle_sqrt(x*x+y*y))<40.F)return false;
    }
    int first=-1;
    if(!ai_road_nearest(road,actor,0,1,first)||first<0)return false;
    unsigned end=unsigned(first);
    while(end<road.count&&road.cells[end].distance-road.cells[unsigned(first)].distance<75.F)++end;
    if(end==road.count||end+1-unsigned(first)<=4)return false;
    // Original subtracts two from the end used for the mover progress range.
    const unsigned endpoint=end-unsigned(first)>=5?end-2:end;
    const auto& start=road.cells[endpoint];
    const auto& second=road.cells[unsigned(first)+2];
    const auto left=edge(start,true);
    const auto right=edge(start,false);
    const auto v=point(car.velocity);const auto a=subtract(left,actor),b=subtract(right,actor);
    const bool choose_left=(a[2]*v[2]+a[1]*v[1])+a[0]*v[0]<=(b[2]*v[2]+b[1]*v[1])+b[0]*v[0];
    // The dot order is verified against the native constructor before acceptance.
    auto eye=choose_left?left:right;
    bool left_side=choose_left;
    if(!crash_camera_road_visible(road,actor,eye,unsigned(first),endpoint-1)){eye=choose_left?right:left;left_side=!choose_left;}
    eye[2]+=1.F;
    float ground=0;
    if(!world.ground(eye,ground))return false;
    if(eye[2]<ground)eye[2]=ground+1.F;
    CrashCameraShot next;next.eye=eye;
    next.end_point={start.point.position[0],start.point.position[1],start.point.height};
    next.side=left_side?-.4F:.4F;
    next.alternate=edge(second,!left_side);
    next.alternate[2]+=1.F;
    output=next;return true;
}
bool crash_camera_view(const CrashCameraShot& shot,unsigned event,const VehicleState& car,
    float progress,const ChaseCameraView& normal,ChaseCameraView& result) noexcept {
    if(event!=6&&event!=7)return false;
    const float span=shot.end_progress-shot.start_progress;
    const float f=span==0?0:std::clamp((progress-shot.start_progress)/span,0.F,1.F);
    const float at=f*31.F;const int index=int(at);const float remainder=at-float(index);
    const auto field=[&](int i){return event==7?20.F-.25F*float(i):20.F;};
    const float degrees=cubic(remainder,field(index-1),field(index),field(index+1),field(index+2));
    const auto fov=static_cast<std::uint16_t>(static_cast<int>(degrees*182.0444488525390625F));
    const auto half=std::uint16_t(fov>>1);
    const float tangent=player_angle_sin(half)/player_angle_sin(std::uint16_t(half+0x4000));
    auto eye=event==6?shot.eye:shot.alternate;
    for(float& value:eye)value=cubic(f,value,value,value,value);
    auto target=point(car.position);
    const auto toward=subtract(eye,target);const float distance=std::max(1.F,length(toward));
    const float inverse=1.F/distance;
    const float vertical=tangent*.2F*distance;
    const float horizontal=tangent*(event==6?shot.side:-shot.side)*distance;
    target[2]=vertical+target[2];
    target[0]=(toward[1]*inverse)*horizontal+target[0];
    target[1]=(-toward[0]*inverse)*horizontal+target[1];
    auto forward=subtract(target,eye);if(!unit(forward))return false;
    //486371..486395 stores eye+unit as the mover target;483860 and40B780
    //subtract the world-space eye again. Preserve that binary32 quantization.
    for(unsigned i=0;i<3;++i)forward[i]=(eye[i]+forward[i])-eye[i];
    if(!unit(forward))return false;
    AiDriverPoint right{forward[1],-forward[0],0};if(!unit(right))return false;
    const AiDriverPoint up{right[1]*forward[2],-right[0]*forward[2],right[0]*forward[1]-right[1]*forward[0]};
    auto out=normal;out.eye={eye[0],eye[1],eye[2]};out.forward={forward[0],forward[1],forward[2]};
    out.up={up[0],up[1],up[2]};out.pivot=car.position;out.fov=fov;out.near_plane=.5;out.far_plane=10000;
    out.heading=chase_angle(forward[0],forward[1]);out.target=out.heading;out.lag=0;
    out.pitch=chase_angle(vehicle_sqrt(double(forward[0])*forward[0]+double(forward[1])*forward[1]),-forward[2]);
    out.reach=1;
    result=out;return true;
}
bool crash_camera_predict(const VehicleState& car,const VehicleHandling& h,float horizon,
    const CrashCameraWorld& world,CrashCameraFlight& result,float wheel_phase) noexcept {
    if(!std::isfinite(horizon)||horizon<=0||horizon>4||!(h.mass>0))return false;
    //429D6F obtains matrix0 once, then uses all four radii with that matrix.
    //465150 publishes its spin/steer transform and wheel0 raw setup translation.
    //Do not replace this source behavior by averaging four attachment points.
    const auto spin=std::uint16_t(int(wheel_phase*10430.37890625F));
    const float si=player_angle_sin(spin),co=player_angle_sin(std::uint16_t(spin+0x4000));
    const auto steer=car.steering_word_ready?car.steering_word:std::uint16_t(int(float(car.steer_angle)*10430.37890625F));
    const float ss=player_angle_sin(steer),cs=player_angle_sin(std::uint16_t(steer+0x4000));
    const auto axes=race_contact_axes(car);const auto position=point(car.position);
    AiDriverPoint start{};
    const float travel=vehicle_visual_strut_compression(h.struts[0],car.wheels[0].compression);
    for(const auto& wheel:h.wheels) {
        const float x=(-wheel.radius*(si*cs))+h.wheels[0].longitudinal;
        const float y=(-wheel.radius*(si*ss))+h.wheels[0].lateral;
        const float z=(-wheel.radius*co)+(h.wheels[0].vertical+travel);
        for(unsigned k=0;k<3;++k) {
            const float world_point=((x*float(axes[0][k])+y*float(axes[1][k]))+z*float(axes[2][k]))+position[k];
            start[k]+=world_point*.25F;
        }
    }
    const float speed=car.owner_speeds.magnitude;
    const float gravity=((h.downforce_rear+h.downforce_front)*.3F*speed)/h.mass+9.8F;
    const int count=int((speed/3.F)*horizon); //429E23..429E30, spacing3 world units.
    if(count<0)return false;
    float ground=0;if(!world.ground(start,ground))return false;
    CrashCameraFlight next;next.height=std::max(start[2]-ground,0.F);
    float since=next.height>0?-float(count)*horizon:0;
    next.duration=next.height>0?float(count)*horizon:0;
    for(int j=1;j<count;++j){const float t=(float(j)*horizon)/float(count),elapsed=t-since;
        AiDriverPoint p{float(car.velocity.x)*elapsed+start[0],float(car.velocity.y)*elapsed+start[1],
            (-gravity*elapsed*.5F+float(car.velocity.z))*elapsed+start[2]};
        if(!world.ground(p,ground))return false;
        const float height=p[2]-ground;if(height<=0){next.duration=elapsed;break;}
        next.height=std::max(next.height,height);next.duration=std::max(next.duration,elapsed);
    }
    result=next;return true;
}
void RaceCrashCamera::configure(unsigned track,unsigned intensity) noexcept {track_=track;intensity_=std::min(intensity,255U);reset();}
void RaceCrashCamera::reset() noexcept {event_=0;remaining_=0;crash_valid_=false;last_valid_[0]=last_valid_[1]=false;flight_deadline_=0;shot_={};activations=returns=0;damage_reset_=false;}
void RaceCrashCamera::stop(bool clear_damage) noexcept {
    if(event_) {
        ++returns;
        // Retail 42DDC0(1) clears the player's damage channels.
        damage_reset_=damage_reset_||clear_damage;
    }
    event_=0;
    remaining_=0;
}
bool RaceCrashCamera::consume_damage_reset() noexcept {
    const bool reset=damage_reset_;
    damage_reset_=false;
    return reset;
}
float RaceCrashCamera::scale(float real_dt) const noexcept {return event_&&remaining_-real_dt>=0?.4F:1.F;}
void RaceCrashCamera::begin_frame(float real_dt) noexcept {if(event_){remaining_-=real_dt;if(remaining_<0)stop();}}
bool RaceCrashCamera::request(std::int32_t tick,float progress,const VehicleState& car,const CrashCameraRoadSource& source,const CrashCameraWorld& world) {
    if(event_||!intensity_||(last_valid_[0]&&age(tick,last_[0])<5.F))return false;
    AiRoadChain road;int category=0;CrashCameraShot shot;
    if(!source.prepare(car,progress,road,category)||!crash_camera_road_shot(road,category,track_,car,world,shot))return false;
    shot.start_progress=progress;
    if(!source.progress_at(shot.end_point,shot.end_progress))return false;
    shot_=shot;last_[0]=tick;last_valid_[0]=true;event_=6;remaining_=8.F;++activations;return true;
}
bool RaceCrashCamera::contact(float magnitude,bool entering,std::int32_t tick,bool allowed,
    const VehicleState& car,float progress,const CrashCameraRoadSource& source,const CrashCameraWorld& world) {
    if(!entering||magnitude<30.F)return false;
    crash_tick_=tick;crash_valid_=true;
    return allowed&&magnitude>=35.F&&request(tick,progress,car,source,world);
}
bool RaceCrashCamera::update(std::int32_t tick,std::uint64_t frame,bool allowed,
    const VehicleState& car,const VehicleHandling& h,float wheel_phase,float progress,const CrashCameraRoadSource& source,
    const CrashCameraWorld& world,const ChaseCameraView& normal,ChaseCameraView& view,bool finishing) {
    if(!allowed&&!finishing){stop();return false;}
    const float crash_age=crash_valid_?age(tick,crash_tick_):0;
    if(event_&&((car.collision_state==1&&car.published_support==4&&crash_age>.5F)||
        (car.collision_state!=1&&car.published_support>0)))stop();
    // The normal return above is evaluated every frame. 426C6E gates
    // both airborne admission and the transition to the second shot.
    const bool sampled=((frame+3)&7)==0;
    if(sampled&&allowed&&!event_&&intensity_&&car.published_support==0&&
        car.owner_speeds.magnitude>curve(intensity_,50,5)) {
        CrashCameraFlight flight;
        if(crash_camera_predict(car,h,curve(intensity_,4,.5F),world,flight,wheel_phase)) {
            flight_deadline_=float(tick)*.00025F+(flight.duration>=.9F?flight.duration*.5F:5.F);
            if(flight.height>curve(intensity_,2.5F,.5F)&&flight.duration>curve(intensity_,2.5F,.5F))
                request(tick,progress,car,source,world);
        }
    }
    if(sampled&&event_==6&&car.velocity.z<0)
        shot_.alternate[2]=std::max(shot_.alternate[2],float(car.position.z));
    if(sampled&&event_==6&&((car.collision_state==1&&crash_age>3.F)||
        (car.collision_state!=1&&float(tick)*.00025F>flight_deadline_))) {
        stop(false); // 42DDC0(0) preserves damage through the second shot.
        if(intensity_&&(!last_valid_[1]||age(tick,last_[1])>=5.F)) {
            float ground=0;shot_.alternate[2]=std::max(shot_.alternate[2],float(car.position.z));
            if(world.ground(shot_.alternate,ground)&&shot_.alternate[2]<ground)shot_.alternate[2]=ground+1.F;
            shot_.start_progress=progress;
            event_=7;remaining_=8.F;last_[1]=tick;last_valid_[1]=true;
        }
    }
    return event_&&crash_camera_view(shot_,event_,car,progress,normal,view);
}
} // namespace nfsu
