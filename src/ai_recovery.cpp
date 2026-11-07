#include "nfsu/ai_recovery.h"
#include "nfsu/player_powertrain.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nfsu {
namespace {
using V=std::array<float,2>;
V normalized(V v) noexcept {
    const float n=std::sqrt(v[1]*v[1]+v[0]*v[0]);
    if(n==0)return {1,0};
    const float inverse=1.F/n;return {inverse*v[0],inverse*v[1]};
}
float lateral(V v,const RoutePoint& p) noexcept {
    return (v[0]-p.position[0])*p.forward[1]-(v[1]-p.position[1])*p.forward[0];
}
float elapsed(std::uint32_t tick,std::uint32_t last) noexcept {
    const auto difference=tick-last;std::int32_t signed_difference;
    std::memcpy(&signed_difference,&difference,4);return static_cast<float>(signed_difference)*.00025F;
}
bool finite(V v) noexcept {return std::isfinite(v[0])&&std::isfinite(v[1]);}
bool recovery_category(int category) noexcept {
    return (category>=0&&category<5)||(category>=11&&category<=16);
}
float recovery_offset(const RoutePoint& point,int category) noexcept {
    return category<5?point.driving_offset[category]:point.lane_offset[category-11];
}
AiDriverPoint normalized3(AiDriverPoint v) noexcept {
    const float n=std::sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
    if(n==0)return {1,0,0};
    const float inverse=1.F/n;for(float& x:v)x*=inverse;return v;
}
AiDriverPoint cross(const AiDriverPoint& a,const AiDriverPoint& b) noexcept {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
}
bool ai_recovery_contact_local(const V& contact,const V& position,const AiDriverPoint& movement,
    float signed_speed,std::uint16_t heading,AiRecoveryContactBasis& cache,V& result) noexcept {
    if(!finite(contact)||!finite(position))return false;
    auto next=cache;
    if(!next.valid) {
        if(!std::isfinite(signed_speed))return false;
        auto v=movement;for(float& x:v){if(!std::isfinite(x))return false;if(signed_speed<0)x=-x;}
        const float square=(v[2]*v[2]+v[0]*v[0])+v[1]*v[1];
        if(!std::isfinite(square))return false;
        AiDriverPoint side,forward;
        if(square>1){side={v[1],-v[0],0};forward={v[0],v[1],0};}
        else {
            const float c=player_angle_sin(static_cast<std::uint16_t>(heading+0x4000));
            const float s=player_angle_sin(heading);
            side={s,-c,0};forward={c,s,0};
        }
        // 40BD40 normalizes side, then side x forward, then rebuilds forward.
        side=normalized3(side);const auto up=normalized3(cross(side,forward));
        next.rows={side,cross(up,side),up};next.valid=true;
    }
    for(const auto& row:next.rows)for(float x:row)if(!std::isfinite(x))return false;
    const float x=contact[0]-position[0],y=contact[1]-position[1];
    V local{};
    // 4650D0 calls565230 twice, including in-place, before D3DX transform.
    for(unsigned k=0;k<2;++k)local[k]=((next.rows[2][k]*0.F+next.rows[1][k]*y)+next.rows[0][k]*x)+0.F;
    if(!finite(local))return false;
    cache=next;result=local;return true;
}
bool ai_recovery_road_class(const RoutePoint& road,const AiRecoveryMotion& m,bool target_override,int& result) noexcept {
    if(!finite(m.position)||!finite(m.prediction_vector)||!finite(m.body_direction)||!finite(m.target)||
       !std::isfinite(m.absolute_speed)||!std::isfinite(m.signed_speed)||
       !finite({road.position[0],road.position[1]})||!finite({road.forward[0],road.forward[1]})||
       !std::isfinite(road.unknown_24)||!std::isfinite(road.unknown_28))return false;
    const float current=lateral(m.position,road);
    const float left=current+road.unknown_24,right=road.unknown_28-current;
    bool recovery=false;
    if(std::fabs(m.absolute_speed)>1) {
        const V predicted={.1F*m.prediction_vector[0]+m.position[0],.1F*m.prediction_vector[1]+m.position[1]};
        const float future=lateral(predicted,road),future_left=future+road.unknown_24,future_right=road.unknown_28-future;
        const float rate=(future_left-left)/.1F;
        float time=99999;
        if(rate>.001F&&right>0&&left>0)time=right/rate;
        // The second original rate slot is identical to the first. Preserve
        // that source behavior rather than inventing a symmetric sign fix.
        recovery=time<=1||(right<0&&future_right<right)||(left<0&&future_left<left);
        if(recovery&&m.signed_speed<=0) {
            const auto velocity=normalized(m.body_direction),direction=normalized({road.forward[0],road.forward[1]});
            result=direction[1]*velocity[1]+direction[0]*velocity[0]>.342F?0:2;
            return true;
        }
    }else {
        const float c=player_angle_sin(static_cast<std::uint16_t>(m.heading+0x4000));
        const float s=player_angle_sin(m.heading);
        const float future=lateral({(c+c)+m.position[0],(s+s)+m.position[1]},road);
        const float future_right=road.unknown_28-future,future_left=future+road.unknown_24;
        recovery=(future_right<0&&future_right-right<0)||(future_left<0&&future_left-left<0);
    }
    int next=recovery?1:0;
    if(recovery&&target_override) {
        const auto direction=normalized(m.absolute_speed<1?m.body_direction:m.prediction_vector);
        const auto target=normalized({m.target[0]-m.position[0],m.target[1]-m.position[1]});
        if(direction[0]*target[0]+direction[1]*target[1]>.5F)next=0;
    }
    result=next;return true;
}
int ai_recovery_contact_class(const V& local,float minimum_x,float maximum_x) noexcept {
    // 44ECD0 treats contacts on either scaled side boundary as side hits.
    if(.95F*maximum_x>local[0]&&.95F*minimum_x<local[0])return local[1]>0?0:1;
    return local[0]>0?3:2;
}
bool ai_recovery_request(const AiRecoveryInput& in,AiRecoveryRequest& result) noexcept {
    AiRecoveryRequest next;
    if(in.race_time_hold||!in.selected_road||!in.supported){result=next;return true;}
    if((in.contact_count&&!in.contacts)||!std::isfinite(in.motion.absolute_speed))return false;
    if(in.road) {
        const float side=lateral(in.motion.position,*in.road);
        const float outside=side>0?side-in.road->unknown_28:-(side+in.road->unknown_24);
        if(!std::isfinite(outside))return false;
        next.requested=outside>=20;
    }
    if(!next.requested&&((in.physics_selector==3&&in.motion.absolute_speed<=25)||
       (in.physics_selector==4&&in.motion.absolute_speed<=20))) {
        if(!in.road)return false;
        int kind=0;if(!ai_recovery_road_class(*in.road,in.motion,true,kind))return false;
        next.requested=kind==1;
    }
    for(std::size_t j=0;j<in.contact_count&&!next.requested;++j) {
        const auto& contact=in.contacts[j];
        if(!finite(contact.local)||!std::isfinite(in.minimum_x)||!std::isfinite(in.maximum_x))return false;
        if(ai_recovery_contact_class(contact.local,in.minimum_x,in.maximum_x)!=0||
           elapsed(in.tick,contact.created_tick)<.4F)continue;
        const float speed=in.motion.absolute_speed;
        next.requested=speed<5||std::fabs(speed-5)<=.001F;
    }
    next.clear_road=next.requested;result=next;return true;
}
bool ai_recovery_race_hold(std::uint32_t tick,std::uint32_t race_start,int actor_id,
                           int track,bool bypass) noexcept {
    if(bypass||track==1099)return false;
    const float limit=actor_id>100&&actor_id<150?12.F+.5F*float(100-actor_id):12.F;
    return elapsed(tick,race_start)<limit;
}
const RoutePoint& ai_recovery_lookahead(const Route& route,std::size_t selected) noexcept {
    return route.points[selected+std::min<std::size_t>(3,route.points.size()-1-selected)];
}
bool ai_recovery_available(std::uint32_t tick,std::uint32_t last) noexcept {return elapsed(tick,last)>6.F;}
bool ai_recovery_history_step(float dt,float fractional_speed,float length,AiRecoveryHistory& state) noexcept {
    if(!std::isfinite(dt)||dt<0||dt>1||!std::isfinite(fractional_speed)||
       !std::isfinite(length)||length<=0||state.count>5||state.cursor>=5)return false;
    if(state.timer==0||state.timer==0x7fffffff)return true;
    auto next=state;next.timer-=static_cast<int>(dt*4000.F);
    if(next.timer<=0) {
        next.timer=6000;const float sample=std::abs(fractional_speed)*length;
        if(next.count<5)++next.count;
        next.sum+=sample;next.sum-=next.samples[next.cursor];next.samples[next.cursor]=sample;
        if(++next.cursor==5)next.cursor=0;
        next.average=next.sum/float(next.count);
        if(!std::isfinite(next.average))return false;
    }
    state=next;return true;
}
bool ai_recovery_progress_rate(float now,float previous,float dt,float& rate) noexcept {
    if(!std::isfinite(now)||!std::isfinite(previous)||!std::isfinite(dt)||dt<0||
       !std::isfinite(rate))return false;
    if(dt>0) {
        const float value=.667F*((now-previous)/dt)+(1.F-.667F)*rate;
        if(!std::isfinite(value))return false;
        rate=value;
    }
    return true;
}
float ai_recovery_hold(const RoutePoint* road,const V& position,const AiRecoveryHistory& history) noexcept {
    float outside=0;
    if(road) {
        const float side=lateral(position,*road);
        outside=side>0?side-road->unknown_28:-(side+road->unknown_24);
    }
    return outside>0&&history.count==5&&history.average<.2F?1.6F:1.3F;
}
bool ai_recovery_finish(const AiRecoveryLifetime& life,const AiRecoveryInput& in,
                        const RoutePoint& lookahead,int category,AiRecoveryExit& result) noexcept {
    if(!recovery_category(category)||!std::isfinite(life.duration)||!std::isfinite(life.hold_seconds)||
       !finite(in.motion.position)||!finite(in.motion.body_direction)||!std::isfinite(in.motion.absolute_speed)||
       (in.contact_count&&!in.contacts))return false;
    const float offset=recovery_offset(lookahead,category);
    const V goal={(lookahead.forward[1]*offset+lookahead.position[0])+lookahead.forward[0]*0.F,
                 ((-lookahead.forward[0])*offset+lookahead.position[1])+lookahead.forward[1]*0.F};
    if(!finite(goal))return false;
    const bool held=std::fabs(life.hold_seconds+1.F)>.001F&&elapsed(in.tick,life.start_tick)<life.hold_seconds;
    bool abort=!in.selected_road||!in.supported,complete=false;
    if(!held) {
        if(!abort) {
            int kind=0;if(!in.road||!ai_recovery_road_class(*in.road,in.motion,true,kind))return false;
            abort=kind==2;
        }
        const auto direction=normalized(in.motion.body_direction);
        const auto target=normalized({goal[0]-in.motion.position[0],goal[1]-in.motion.position[1]});
        complete=direction[1]*target[1]+direction[0]*target[0]>=.9F;
        for(std::size_t j=0;j<in.contact_count;++j) {
            const auto& contact=in.contacts[j];
            if(!finite(contact.local)||!std::isfinite(in.minimum_x)||!std::isfinite(in.maximum_x))return false;
            if(elapsed(in.tick,contact.created_tick)<.4F)continue;
            const int kind=ai_recovery_contact_class(contact.local,in.minimum_x,in.maximum_x);
            // Completion uses strict speed<5; abort includes the .001 equality.
            if(kind==0&&in.motion.absolute_speed<5)complete=false;
            if(kind==1&&(std::fabs(in.motion.absolute_speed)<5||std::fabs(in.motion.absolute_speed-5)<=.001F))abort=true;
        }
    }
    AiRecoveryExit next;
    if(life.duration>=0&&elapsed(in.tick,life.timeout_tick)>life.duration)next.status=3;
    if(abort)next.status=2;
    if(complete)next.status=1;
    next.clear_road=next.reset_driver=next.clear_actor_recovery=next.status!=0;
    result=next;return true;
}
bool ai_recovery_plan(const RoutePoint& lookahead,const AiPathRoadPoint& selected,
                      const AiPathRoadPoint* chain,std::size_t count,
                      const AiPathExtension& extension,int selector,AiRecoveryPlan& result) noexcept {
    if(!recovery_category(extension.category))return false;
    const float offset=recovery_offset(lookahead,extension.category);
    AiRecoveryPlan next;
    next.path.path.points[0]={(lookahead.forward[1]*offset+lookahead.position[0])+lookahead.forward[0]*0.F,
        ((-lookahead.forward[0])*offset+lookahead.position[1])+lookahead.forward[1]*0.F,
        (lookahead.unknown_2c*offset+lookahead.height)+lookahead.unknown_30*0.F};
    for(float value:next.path.path.points[0])if(!std::isfinite(value))return false;
    next.path.path.count=1;next.path.route[0]=selected.point.route_index;
    next.path.point[0]=selected.point_index;next.path.category[0]=static_cast<std::uint8_t>(extension.category);
    // 99999 at the call site requests45C550 policy, not a literal step.
    // That policy returns zero for recovery mode2 at every speed/context.
    auto input=extension;input.mode=2;input.offset_step=0;input.target_offset=0;input.margin=1;
    if(!ai_path_continue(chain,count,0,0,input,next.path))return false;
    next.speed=selector==3?-15.F:-6.F;next.enabled=true;
    result=next;return true;
}
} // namespace nfsu
