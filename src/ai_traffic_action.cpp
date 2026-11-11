#include "nfsu/ai_traffic_action.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace nfsu {
bool ai_traffic_spawn_point_allowed(const std::array<std::uint8_t,4>& flags,bool forward) noexcept {
    return (flags[3] & (forward ? 2U : 4U)) != 0;
}
bool ai_traffic_drag_spawn_margin(unsigned point,unsigned count,bool forward) noexcept {
    return forward ? count>10 && point<count-10 : point>=10;
}
AiRoadCursor ai_traffic_drag_grid_point(const RacingLineTrack& roads,const TrackPathSpline& spline,
                                      bool reverse,float target,bool forward) noexcept {
    AiRoadCursor result{-1,0};
    if(!std::isfinite(target)||target<0||!std::isfinite(spline.path_length)||spline.path_length<=0)return result;
    float best=999999.F;
    const unsigned first=forward?0:6;
    for(int id=0;id<99;++id) {
        const auto* road=roads.find(std::int16_t(id));
        if(!road||std::none_of(road->lane_enabled.begin()+first,road->lane_enabled.begin()+first+6,
                              [](bool enabled){return enabled;}))continue;
        const unsigned count=unsigned(road->points.size());
        float nearest=999999.F;
        unsigned selected=count;
        float selected_progress=0;
        for(unsigned i=0;i<count;) {
            const float raw=road->points[i].path_distance;
            float progress=reverse?spline.path_length-raw:raw;
            if(!spline.open) {
                const float half=.5F*spline.path_length;
                if(progress<-half)progress+=spline.path_length;
                else if(progress>half)progress-=spline.path_length;
            }
            const float delta=progress-target;
            if(progress>0&&delta>=0&&delta<nearest) {
                nearest=delta;selected=i;selected_progress=progress;
            }
            // 442917 advances faster across points far beyond the target.
            const float skip=std::clamp(-2.F-std::trunc(delta*-.0416666679084301F),0.F,float(count));
            i+=1+unsigned(skip);
        }
        if(selected==count||selected_progress>=best||
           !ai_traffic_spawn_point_allowed(road->points[selected].unknown_84,forward)||
           !ai_traffic_drag_spawn_margin(selected,count,forward))continue;
        best=selected_progress;result={std::int16_t(id),std::uint16_t(selected)};
    }
    return result;
}
bool ai_traffic_spawn_spacing(const TrackPathZone* zone,unsigned selector,
                              bool stopped,std::uint32_t& random,float& spacing) noexcept {
    if(selector<1||selector>3)return false;
    spacing=99999;
    if(!zone||stopped)return true;
    const auto sample=random%0x7fffffffU;
    const auto x=random^0x1d872b41U,y=(x>>5)^x;
    random=(y<<27)^y^x;
    const unsigned slot=2*(selector-1);
    const float low=float(zone->traffic_spacing[slot]);
    const float extent=float(float(zone->traffic_spacing[slot+1])-low);
    const float unit=float(sample)*4.656612873077393e-10F;
    const float offset=float(unit*extent);
    spacing=float(offset+low);return true;
}
bool ai_traffic_spawn_gap_blocked(float delta,float spacing,bool traffic) noexcept {
    return traffic?std::abs(delta)<spacing:delta>0&&delta<spacing;
}
bool ai_traffic_full_update(std::uint32_t frame,unsigned slot,unsigned count,
                            bool& full) noexcept {
    if(!count||count>0x3fffffffU||slot>=count)return false;
    std::int32_t signed_frame;std::memcpy(&signed_frame,&frame,4);
    const int selected=signed_frame%int(count*2);
    full=selected>=0&&unsigned(selected)==slot*2+1;
    return true;
}
bool ai_traffic_strategy_choose(int kind,int priority,
    AiTrafficStrategyEvaluator evaluate,void* context,AiActionCandidate& result) noexcept {
    if(!evaluate)return false;
    struct Gate {AiTrafficStrategyQuery query;int kind,priority;};
    const Gate gates[]={{AiTrafficStrategyQuery::obstacle,18,600},
                        {AiTrafficStrategyQuery::avoidance,3,300}};
    for(const auto& gate:gates) {
        if(kind==gate.kind||priority>gate.priority)continue;
        bool found=false;AiActionCandidate candidate{gate.kind,gate.priority,0};
        if(!evaluate(context,gate.query,found,candidate))return false;
        if(found){result=candidate;return true;}
    }
    result={8,0,0};return true;
}
bool ai_traffic_action_owner_step(const AiTrafficActionOwnerOps& ops,void* context,
                                  AiTrafficActionOwner& owner) noexcept {
    if(!ops.predicate||!ops.choose||!ops.initial||!ops.cleanup||!ops.release||
       !ops.promote||!ops.update)return false;
    AiOwnedAction candidate;
    if(owner.current.handle) {
        owner.current.status=0;
        for(auto predicate:{AiActionPredicate::timeout,AiActionPredicate::abort,AiActionPredicate::complete}) {
            bool yes=false;if(!ops.predicate(context,owner.current,predicate,yes))return false;
            if(yes)owner.current.status=predicate==AiActionPredicate::timeout?3:
                predicate==AiActionPredicate::abort?2:1;
        }
        const auto& current=owner.current;
        const bool done=current.status==1||current.status==2||(current.status==3&&current.timed);
        const int kind=done?20:current.kind,priority=done?-1:current.priority;
        if(!ops.choose(context,kind,priority,candidate)||!candidate.handle||candidate.handle==current.handle)return false;
        if(candidate.priority<=priority) {ops.release(context,candidate);candidate={};}
    }else if(!ops.initial(context,candidate)||!candidate.handle)return false;
    if(candidate.handle) {
        if(owner.current.handle) {
            if(!ops.cleanup(context,owner.current)){ops.release(context,candidate);return false;}
            ops.release(context,owner.current);
        }
        owner.current=candidate;
        if(owner.physical_selector==2&&(candidate.kind==1||candidate.kind==3||candidate.kind==9)) {
            if(!ops.promote(context))return false;
            owner.physical_selector=3;
        }
    }
    return ops.update(context,owner.current);
}
namespace {
void traffic_stop_mode_draw(std::uint32_t& state) noexcept {
    const auto x=state^0x1d872b41U,y=(x>>5)^x;
    state=(y<<27)^y^x;
}
}
bool ai_traffic_stop_offer(const AiTrafficIntersectionRef& ref,std::uint32_t& random,
                           AiTrafficStopCandidate& output) noexcept {
    if(ref.present&&ref.index>=31)return false;
    AiTrafficStopCandidate result;
    if(ref.present){traffic_stop_mode_draw(random);result={int(ref.index),1,600};}
    output=result;return true;
}
bool ai_traffic_stop_complete(const AiTrafficIntersectionRef& ref,std::uint32_t& random,
                              AiTrafficStopAction& state,bool& complete) noexcept {
    if(ref.present&&ref.index>=31)return false;
    if(!ref.present){complete=true;return true;}
    if(state.intersection!=int(ref.index)) {
        traffic_stop_mode_draw(random);state.intersection=int(ref.index);state.stopped=false;state.mode=1;
    }
    complete=false;return true;
}
bool ai_traffic_stop_update(const AiTrafficStopInput& in,AiTrafficIntersection& intersection,
                            AiTrafficStopState& state,AiTrafficStopOutput& output) noexcept {
    if(!std::isfinite(in.distance)||!std::isfinite(in.controller_cap)||
       intersection.selected< -1||intersection.selected>1)return false;
    float speed=in.controller_cap;
    if(in.obstacle.id&&in.obstacle_admitted) {
        if(!std::isfinite(in.obstacle.time)||!std::isfinite(in.obstacle.distance))return false;
        if(in.obstacle.time<1.5F||in.obstacle.distance<30.F) {
            const float limit=in.obstacle.distance<=15.F?0.F:6.F;
            if(limit<=speed)speed=limit;
        }
    }
    auto table=intersection;auto next=state;AiTrafficStopOutput result;
    bool wait=!state.stopped&&(table.kind==3||(table.kind==2&&in.route==table.routes[1]));
    if(wait)result.actor_override={0,0,255,0};
    else {
        int direction=-1;
        for(unsigned j=0;j<2;++j)if(table.routes[j]==in.route){direction=int(j);break;}
        bool open=false;
        if(direction!=-1&&!ai_traffic_intersection_open(table,unsigned(direction),open))return false;
        wait=!open;
        if(wait)result.actor_override={255,0,0,0};
        else {
            if(table.selected!=int(direction==0))table.selected=direction;
            result.actor_override={0,255,0,0};
        }
    }
    if(wait) {
        if(in.distance<=30.F&&6.F<=speed)speed=6.F;
        if(in.distance<0) {
            if(0.F<=speed)speed=0;
            if(!std::isfinite(in.absolute_speed))return false;
            if(in.absolute_speed<.5F)next.stopped=true;
        }
    }
    result.speed={speed,0,1,false,true,false,true};
    result.actor_override_enabled=true;
    intersection=table;state=next;output=result;return true;
}
namespace {
bool traffic_avoidance_input(const AiTrafficAvoidanceInput& in) {
    return in.neighbors&&in.neighbors->count<=16&&in.neighbors->self_index>=0&&
        unsigned(in.neighbors->self_index)<in.neighbors->count&&in.category>=11&&in.category<=16&&
        std::isfinite(in.absolute_speed)&&in.absolute_speed>=0&&std::isfinite(in.controller_cap);
}
const AiNeighborEntry* traffic_avoidance_target(const AiNeighborhood& n,std::uint32_t id) {
    for(unsigned i=0;i<n.count;++i)if(n.entries[i].id==id)return &n.entries[i];
    return nullptr;
}
float traffic_lane_offset(const AiNeighborhood& n,int category) {
    return n.entries[unsigned(n.self_index)].lane_lateral[unsigned(category-11)];
}
bool traffic_avoidance_lane(const AiTrafficAvoidanceInput& in,const AiTrafficAvoidanceOps& ops,
    void* opaque,const AiTrafficAvoidanceState& state,int category,bool& allowed) {
    allowed=false;
    if(category!=in.category) {
        bool transition=false;if(!ops.transition(opaque,in.category,category,transition))return false;
        if(!transition)return true;
    }
    AiObstacleQueryResult obstacle;
    if(!ops.query(opaque,category,0,state.clearance,obstacle))return false;
    bool admitted=false;if(obstacle.id&&!ops.admitted(opaque,obstacle.id,admitted))return false;
    allowed=!(obstacle.id&&admitted&&obstacle.time<=5.F&&
        (obstacle.time<=state.time||obstacle.id==state.target));
    return true;
}
}
bool ai_traffic_avoidance_offer(const AiTrafficAvoidanceInput& in,const AiTrafficAvoidanceOps& ops,
    void* opaque,AiTrafficAvoidanceState& state,bool& found) noexcept {
    if(!traffic_avoidance_input(in)||!ops.query||!ops.admitted)return false;
    AiObstacleQueryResult obstacle;
    if(!ops.query(opaque,in.category,traffic_lane_offset(*in.neighbors,in.category),99999.F,obstacle))return false;
    bool admitted=false;
    if(obstacle.id&&obstacle.time<=5.F&&!ops.admitted(opaque,obstacle.id,admitted))return false;
    found=obstacle.id&&obstacle.time<=5.F&&admitted;
    if(found)state={obstacle.id,obstacle.time,30.F};
    return true;
}
bool ai_traffic_avoidance_complete(const AiTrafficAvoidanceInput& in,const AiTrafficAvoidanceOps& ops,
    void* opaque,AiTrafficAvoidanceState& state,bool& complete) noexcept {
    if(!traffic_avoidance_input(in)||!ops.query||!ops.admitted)return false;
    const auto& n=*in.neighbors;const auto* target=traffic_avoidance_target(n,state.target);
    if(!target){complete=true;return true;}
    const auto& self=n.entries[unsigned(n.self_index)];
    if(target->distance-self.distance>=0&&target->relative_velocity[1]<=5.F){complete=false;return true;}
    AiObstacleQueryResult obstacle;
    if(!ops.query(opaque,in.category,traffic_lane_offset(n,in.category),state.clearance,obstacle))return false;
    if(!obstacle.id||obstacle.time>5.F){complete=true;return true;}
    bool admitted=false;if(!ops.admitted(opaque,obstacle.id,admitted))return false;
    complete=!admitted;
    if(admitted&&obstacle.id!=state.target)state={obstacle.id,obstacle.time,30.F};
    return true;
}
bool ai_traffic_avoidance_update(const AiTrafficAvoidanceInput& in,const AiTrafficAvoidanceOps& ops,
    void* opaque,AiTrafficAvoidanceState& state,AiTrafficAvoidanceOutput& output) noexcept {
    if(!traffic_avoidance_input(in)||!ops.query||!ops.admitted||!ops.transition||!ops.nearest_lane||!ops.is_traffic)return false;
    const auto& n=*in.neighbors;const auto& self=n.entries[unsigned(n.self_index)];
    auto next=state;AiTrafficAvoidanceOutput result;result.category=in.category;
    float gap=0;bool failed_lane=false;
    const auto* target=traffic_avoidance_target(n,state.target);
    if(target&&target->relative_velocity[1]>-40.F) {
        gap=((target->distance-self.distance)-target->half_extent[1])-self.half_extent[1];
        if(gap<=0)gap=0;
        const int side=target->relative_velocity[0]<=0?1:-1;
        const int candidates[]={in.category,in.category+side,in.category-side,
                                in.category+2*side,in.category-2*side};
        bool chosen=false;
        for(int category:candidates) {
            bool allowed=false;if(!traffic_avoidance_lane(in,ops,opaque,next,category,allowed))return false;
            if(allowed){result.category=category;result.reset_maneuver=true;chosen=true;break;}
        }
        failed_lane=!chosen;
    }
    AiObstacleQueryResult obstacle;
    if(!ops.query(opaque,result.category,traffic_lane_offset(n,result.category),next.clearance,obstacle))return false;
    bool brake=failed_lane;
    if(obstacle.id) {
        if(obstacle.id!=next.target){next.target=obstacle.id;next.clearance=30.F;}
        next.time=obstacle.time;
        if(in.absolute_speed<.5F) {
            next.clearance=std::max(std::max(next.clearance,30.F),obstacle.distance);
            int nearest=-1;if(!ops.nearest_lane(opaque,nearest))return false;
            if(nearest!=-1){if(nearest<11||nearest>16)return false;result.category=nearest;result.reset_maneuver=true;}
        }
        brake=brake||obstacle.time<1.5F||gap<=next.clearance;
    }
    if(brake) {
        const bool stop=obstacle.distance<=15.F;
        result.speed={stop?0.F:6.F,0,stop?0.F:2.F,false,true,true,true};
        bool traffic=false;if(next.target&&!ops.is_traffic(opaque,next.target,traffic))return false;
        result.actor_override_enabled=traffic;
        if(traffic)result.actor_override=stop?std::array<float,4>{{255,0,0,0}}:
            std::array<float,4>{{0,0,255,0}};
    }else {
        result.speed={in.controller_cap,0,2,false,true,true,true};
        result.actor_override_enabled=true;result.actor_override={0,255,0,0};
    }
    state=next;output=result;return true;
}
} // namespace nfsu
