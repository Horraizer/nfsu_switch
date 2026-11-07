#include "nfsu/ai_action.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nfsu {
bool ai_speed_configuration(const unsigned char* data,std::size_t size,int context,
                             AiSpeedConfiguration& output) noexcept {
    if(!data||size!=0x82c||context< -1||context>=10)return false;
    const auto scalar=[&](std::size_t offset) {
        const std::uint32_t bits=std::uint32_t(data[offset])|(std::uint32_t(data[offset+1])<<8)|
            (std::uint32_t(data[offset+2])<<16)|(std::uint32_t(data[offset+3])<<24);
        float value;std::memcpy(&value,&bits,4);return value;
    };
    const auto row=std::size_t(0x380+std::max(0,context)*0x54);
    AiSpeedConfiguration next;
    for(unsigned k=0;k<3;++k)next.road_probabilities[k]=scalar(row+4*k);
    next.maneuver_probability=scalar(row+0xc);
    next.context_scale=scalar(row+0x14);next.offset_amplitude=data[row+0x50];
    next.offset_interval=scalar(0x820);
    next.drag_launch_min=scalar(row+0x1c);next.drag_launch_max=scalar(row+0x20);
    //476E0D..476E2E: C8/CC are UP bounds (+2C/+30); D0/D4
    //are DOWN bounds (+24/+28). Serialized order is not consumer order.
    next.drag_shift_offsets={scalar(row+0x2c),scalar(row+0x30),scalar(row+0x24),scalar(row+0x28)};
    for(float p:next.drag_shift_offsets)if(!std::isfinite(p))return false;
    if(next.drag_shift_offsets[0]>next.drag_shift_offsets[1] ||
       next.drag_shift_offsets[2]>next.drag_shift_offsets[3])return false;
    for(float p:next.road_probabilities)if(!std::isfinite(p))return false;
    if(!std::isfinite(next.maneuver_probability)||!std::isfinite(next.context_scale)||!std::isfinite(next.offset_interval)||
       !std::isfinite(next.drag_launch_min)||!std::isfinite(next.drag_launch_max)||
       next.drag_launch_min>next.drag_launch_max)return false;
    output=next;return true;
}
bool ai_action_offset_step(const AiActionOffsetInput& in,float& result) noexcept {
    if(!std::isfinite(in.absolute_speed)||in.absolute_speed<0||!std::isfinite(in.race_seconds))return false;
    float value=0;
    if(in.mode==2)value=0;
    else if(in.mode==3||in.mode==4)value=.5F;
    else if(in.mode==1)value=1;
    else if(in.special_context&&(in.physics_selector==2||in.physics_selector==3)) {
        // Original583F20 order: deltaY * ((x-x0)/deltaX) + y0.
        const float mph=in.absolute_speed*2.236999988555908F;
        value=mph>=100?1.F:(mph<=0?5.F:(-4.F*(mph/100.F))+5.F);
    }else if(in.physics_selector==4)value=1;
    else if(in.race_seconds<5.F||in.absolute_speed<=26.821636199951172F)value=0;
    else value=(1.100000023841858F*26.821636199951172F)>=in.absolute_speed?.25F:.5F;
    result=value;return true;
}
bool ai_action_continue(const AiPathRoadPoint* roads,std::size_t count,std::size_t selected,
                        float initial_offset,const AiActionOffsetInput& policy,
                        const AiPathExtension& input,AiPlannedPath& path) noexcept {
    auto next=input;
    if(policy.mode!=input.mode||policy.absolute_speed!=input.absolute_speed||
       !ai_action_offset_step(policy,next.offset_step))return false;
    return ai_path_continue(roads,count,selected,initial_offset,next,path);
}
bool ai_action_forward_path(const AiPlannedPath& previous,const AiPathRoadPoint* roads,
                            std::size_t count,std::size_t selected,float initial_offset,
                            const AiActionOffsetInput& policy,const AiPathExtension& input,
                            float signed_speed,std::uint32_t tick,const std::array<float,3>& parameters,
                            AiPlannedPath& output,bool rebuild,bool category_available) noexcept {
    auto next=rebuild?AiPlannedPath{}:previous;auto extension=input;auto offset_policy=policy;
    if(rebuild&&!category_available)extension.target_offset=0;
    extension.mode=std::fabs(extension.target_offset)<=.0010000000474974513F?input.mode:1;
    offset_policy.mode=extension.mode;
    if((!rebuild&&!ai_path_retain(next,signed_speed,input.actor_position,input.actor_velocity,tick,
                       parameters[0],parameters[1],parameters[2]))||
       !ai_action_continue(roads,count,selected,initial_offset,offset_policy,extension,next))return false;
    output=next;return true;
}
bool ai_action_plan_path(const AiContactManeuver& descriptor,const AiActionPathInput& in,
                          AiManeuverResult& result) noexcept {
    if(descriptor.kind<1||descriptor.kind>3||!std::isfinite(descriptor.parameter))return false;
    auto next=in.previous;
    if(in.rebuild) {next={};next.transition_end=255;}
    if(in.rebuild&&in.category_available&&descriptor.kind!=1) {
        if(!in.target||descriptor.target==0||descriptor.target!=in.resolved_target)return false;
        auto maneuver=*in.target;
        maneuver.side=descriptor.kind;maneuver.extra_clearance=descriptor.flag;
        if(!ai_path_maneuver(in.roads,in.road_count,in.extension,maneuver,next))return false;
    } else {
        auto extension=in.extension;extension.target_offset=descriptor.parameter;
        if(!ai_action_forward_path(in.previous.path,in.roads,in.road_count,in.selected,
            in.initial_offset,in.policy,extension,in.signed_speed,in.tick,in.retain_parameters,
            next.path,in.rebuild,in.category_available))return false;
        next.maneuver=!in.rebuild||in.category_available;
    }
    result=next;return true;
}
bool ai_action_publish_speed(const AiSpeedPolicyInput& in,AiSpeedPolicyState& state,
                             std::uint32_t& rng,AiSpeedPublication& result) noexcept {
    for(float v:{in.offset_interval,in.context_scale,in.catchup_scale,in.strategy_cap,
                 in.action_speed,in.duration,in.auxiliary,in.debug_auxiliary})
        if(!std::isfinite(v))return false;
    auto next=state;auto random=rng;AiSpeedPublication output;
    if(in.route) {
        if(in.point>=in.route->points.size()||in.category<0||in.category>=23||
           in.context>=10||in.offset_amplitude>255)return false;
        const int slot=in.direction==0?in.category:
            (in.category<5?in.category+5:((in.category>=11&&in.category<17)?in.category+6:in.category));
        if(slot>=23)return false;
        if(in.debug_constant){output.speed=15;output.auxiliary=in.debug_auxiliary;}
        else if(in.debug_route)output.speed=in.route->points[in.point].speed[slot];
        else {
            output.auxiliary=in.auxiliary;output.enabled=in.enabled;
            if(!in.route_speed){output.speed=in.action_speed;output.duration=in.duration;}
            else {
                float speed;
                if(in.marker_point)speed=in.marker_point->speed[in.category];
                else if(in.queued_override)speed=99999;
                else {
                    const std::uint32_t elapsed_bits=in.tick-next.offset_tick;
                    std::int32_t elapsed;std::memcpy(&elapsed,&elapsed_bits,4);
                    const float seconds=static_cast<float>(elapsed)*.0002500000118743628F;
                    if(in.context>=0&&seconds>in.offset_interval) {
                        const unsigned amplitude=in.offset_amplitude+1;
                        next.point_offset=static_cast<int>(random%(2*amplitude))-static_cast<int>(amplitude);
                        const auto x=random^0x1d872b41U,y=x^(x>>5);
                        random=(y<<27)^y^x;next.offset_tick=in.tick;
                    }
                    const auto index=std::clamp(static_cast<long long>(in.point)+next.point_offset,
                                              0LL,static_cast<long long>(in.route->points.size()-1));
                    speed=in.route->points[static_cast<std::size_t>(index)].speed[slot];
                    if(speed==0)speed=30;
                    speed*=in.context_scale;
                }
                output.speed=speed*in.catchup_scale;
            }
        }
    }
    output.speed=std::min(in.strategy_cap,output.speed);
    if(!std::isfinite(output.speed))return false;
    state=next;rng=random;result=output;return true;
}

bool ai_action_fallback_speed(const AiNeighborhood& neighbors,int category,
                              const AiActionNeighborState* actors,std::size_t actor_count,
                              float gap_limit,float desired_gap,AiActionSpeedState& state) noexcept {
    if(neighbors.count>neighbors.entries.size()||(!actors&&actor_count)||
       !std::isfinite(gap_limit)||!std::isfinite(desired_gap)||
       !std::isfinite(state.speed)||!std::isfinite(state.duration))return false;
    auto next=state;
    next.route_speed=true;next.auxiliary=0;next.enabled=true;next.catchup=true;next.ready=true;
    if(neighbors.self_index>0&&static_cast<unsigned>(neighbors.self_index)<neighbors.count) {
        const auto& self=neighbors.entries[static_cast<unsigned>(neighbors.self_index)];
        const auto& other=neighbors.entries[static_cast<unsigned>(neighbors.self_index)-1];
        const AiActionNeighborState* actor=nullptr;
        for(std::size_t i=0;i<actor_count;++i)if(actors[i].id==other.id) {
            if(actor)return false;
            actor=&actors[i];
        }
        if(!actor)return false;
        if(actor->race_actor&&(actor->role==1||actor->role==2)) {
            float lateral=0;
            if(category>=0&&category<5)lateral=other.lateral-self.lateral;
            else if(category>=11&&category<17)lateral=other.lane_lateral[category-11]-self.lane_lateral[category-11];
            const float gap=other.distance-self.distance;
            const float width=other.half_extent[0]+self.half_extent[0];
            if(!std::isfinite(lateral)||!std::isfinite(gap)||!std::isfinite(width))return false;
            if(std::fabs(lateral)<width&&gap<gap_limit) {
                const auto& v=actor->velocity;
                const float speed=std::sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
                const float desired=speed-(desired_gap-gap);
                if(!std::isfinite(desired))return false;
                if(desired<next.speed) {
                    next.speed=desired;next.duration=0;next.route_speed=false;
                }
            }
        }
    }
    state=next;return true;
}

bool ai_action_contact_complete(const AiNeighborhood& n,const std::array<AiStrategyActor,16>& actors,
    const AiStrategyContact* contacts,std::size_t count,AiContactActionState& state,bool& complete) noexcept {
    std::uint32_t target=0;
    if(!ai_strategy_contact(n,actors,contacts,count,-1.F,true,target))return false;
    if(target)state.target=target;
    complete=target==0;return true;
}

bool ai_action_contact_update(const AiContactActionInput& in,const AiContactActionOps& ops,
                              void* context,AiContactActionState& state) noexcept {
    if(!in.neighbors||in.neighbors->count>16)return false;
    auto next=state;const auto& n=*in.neighbors;
    const AiNeighborEntry* target=nullptr;
    for(unsigned i=0;i<n.count;++i)if(n.entries[i].id==next.target) {
        if(target)return false;
        target=&n.entries[i];
    }
    next.speed.auxiliary=0;next.speed.route_speed=false;next.speed.enabled=true;
    if(!target) {
        if(!ops.missing_target_path||!ops.missing_target_path(context,next.maneuver,next.path))return false;
        next.speed.route_speed=true;
    } else {
        if(n.self_index<0||static_cast<unsigned>(n.self_index)>=n.count||
           !ops.forward_path||!ops.road_class)return false;
        const auto& self=n.entries[static_cast<unsigned>(n.self_index)];
        float lateral=0;
        if(in.category>=0&&in.category<5)lateral=target->lateral-self.lateral;
        else if(in.category>=11&&in.category<17)
            lateral=target->lane_lateral[in.category-11]-self.lane_lateral[in.category-11];
        const float gap=target->distance-self.distance;
        if(!std::isfinite(lateral)||!std::isfinite(gap))return false;
        if(next.target)next.maneuver={lateral>0?2:3,next.target,0,false};
        if(!ops.forward_path(context,next.maneuver,next.path))return false;
        int road=0;if(!ops.road_class(context,road))return false;
        if(road==1&&gap>0) {
            for(float value:in.slowdown)if(!std::isfinite(value))return false;
            if(in.slowdown[2]==0||!std::isfinite(in.signed_speed)||!std::isfinite(in.duration))return false;
            const auto& p=in.slowdown;
            const float rolling=std::fabs(in.signed_speed)*(p[1]+p[0])*.5F;
            const float gravity=p[5]*p[2];
            const float grip=(p[4]+p[3])*.5F;
            const float deceleration=((gravity+rolling)*grip)/p[2];
            next.speed.speed=(-deceleration*in.duration)+in.signed_speed;
            next.speed.duration=in.duration;
        } else {
            bool match_speed=false;float offset=in.other_speed_offset;
            if(in.target_human&&in.self_role!=3) {
                const float length=self.half_extent[1]+target->half_extent[1];
                if(!std::isfinite(length)||!std::isfinite(in.gap_margin))return false;
                if(gap>length-in.gap_margin){match_speed=true;offset=in.human_speed_offset;}
                else if(gap<in.gap_margin-length){next.speed.speed=in.controller_speed;next.speed.duration=0;}
                else next.speed.route_speed=true;
            } else if(gap<=0){next.speed.speed=in.controller_speed;next.speed.duration=0;}
            else {
                bool opposite=false;
                if(in.self_role==3) {
                    const float dot=in.self_direction[1]*in.target_direction[1]+
                                    in.self_direction[0]*in.target_direction[0];
                    if(!std::isfinite(dot))return false;
                    opposite=dot<0;
                }
                if(opposite){next.speed.enabled=false;next.speed.speed=in.signed_speed;next.speed.duration=0;}
                else match_speed=true;
            }
            if(match_speed) {
                const auto& v=in.target_velocity;
                const float speed=std::sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2])-offset;
                const float relative=target->relative_velocity[1];
                if(!std::isfinite(speed)||!std::isfinite(relative))return false;
                float duration=99999;
                if(std::fabs(relative)>.001F)
                    duration=((gap-self.half_extent[1])-target->half_extent[1])/relative;
                if(!std::isfinite(duration))return false;
                next.speed.speed=std::max(speed,0.F);next.speed.duration=std::max(duration,.1F);
            }
        }
    }
    next.speed.ready=true;
    if(!next.speed.route_speed) {
        if(!std::isfinite(next.speed.speed)||!std::isfinite(next.speed.duration)||!std::isfinite(in.minimum_speed))return false;
        if(next.speed.speed<in.minimum_speed)next.speed.speed=in.minimum_speed;
    }
    state=next;return true;
}

bool ai_action_owner_step(const AiActionOwnerOps& ops,void* context,AiActionOwner& owner) noexcept {
    if(!owner.current.handle||!ops.predicate||!ops.create||!ops.cleanup||!ops.release||!ops.update)return false;
    // All three predicates execute. Completion overrides abort and timeout.
    owner.current.status=0;
    for(auto predicate:{AiActionPredicate::timeout,AiActionPredicate::abort,AiActionPredicate::complete}) {
        bool yes=false;if(!ops.predicate(context,owner.current,predicate,yes))return false;
        if(yes)owner.current.status=predicate==AiActionPredicate::timeout?3:
            predicate==AiActionPredicate::abort?2:1;
    }
    const bool done=owner.current.status==1||owner.current.status==2||
        (owner.current.status==3&&owner.current.timed);
    AiOwnedAction candidate;
    if(done) {
        if(!ops.cleanup(context,owner.current))return false;
        ops.release(context,owner.current);owner.current={};
        if(!ops.create(context,20,owner.current,candidate)||!candidate.handle)return false;
        // This branch does not consume an outstanding explicit3F4 request.
        owner.current=candidate;
    }else {
        const int request=owner.requested_kind;
        if(!ops.create(context,request,owner.current,candidate))return false;
        if(request!=20)owner.requested_kind=20;
        if(candidate.handle) {
            if(candidate.handle==owner.current.handle)return false;
            if(request==20&&candidate.priority<=owner.current.priority)ops.release(context,candidate);
            else {
                if(!ops.cleanup(context,owner.current)) {
                    ops.release(context,candidate);return false;
                }
                ops.release(context,owner.current);owner.current=candidate;
            }
        }
    }
    bool ready=false;if(!ops.update(context,owner.current,ready))return false;
    if(!ready) {
        // 44BBD0 drops an unusable path without the ordinary cleanup callback.
        // Fallback4 updates exactly once; it is not a retry loop.
        ops.release(context,owner.current);owner.current={};
        candidate={};if(!ops.create(context,4,owner.current,candidate)||!candidate.handle)return false;
        owner.current=candidate;
        if(!ops.update(context,owner.current,ready))return false;
    }
    return true;
}
} // namespace nfsu
