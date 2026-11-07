#include "nfsu/ai_strategy.h"
#include <cmath>
#include <cstring>

namespace nfsu {
bool ai_rival_full_update(std::uint32_t frame,std::uint32_t tick,
                          std::uint32_t previous,unsigned slot,unsigned count,
                          bool have_action,bool& full) noexcept {
    if(!count||count>3||slot>=count)return false;
    std::int32_t signed_frame,elapsed;
    std::memcpy(&signed_frame,&frame,4);
    const auto delta=tick-previous;std::memcpy(&elapsed,&delta,4);
    const int selected=signed_frame%8;
    full=!have_action||(selected>=0&&unsigned(selected)==slot&&
        float(elapsed)*.0002500000118743628F>=.20000000298023224F);
    return true;
}
bool ai_strategy_rebuild_path(AiStrategyKind kind,bool dirty) noexcept {
    return dirty||kind==AiStrategyKind::ordinary||kind==AiStrategyKind::drag||
        kind==AiStrategyKind::traffic||kind==AiStrategyKind::special;
}
bool ai_strategy_traffic_admitted(bool target,bool actor_reference,int role,
                                   AiStrategyKind strategy,bool force) noexcept {
    return target&&(force||(actor_reference&&role==3&&strategy==AiStrategyKind::special));
}
bool ai_strategy_contact(const AiNeighborhood& n,const std::array<AiStrategyActor,16>& actors,
                         const AiStrategyContact* contacts,std::size_t count,float minimum,
                         bool racers_only,std::uint32_t& target) noexcept {
    if(n.count>n.entries.size()||(!contacts&&count)||!std::isfinite(minimum))return false;
    if(!count){target=0;return true;}
    if(n.self_index<0||static_cast<unsigned>(n.self_index)>=n.count)return false;
    const auto& self=n.entries[static_cast<unsigned>(n.self_index)];
    int chosen=-1;
    for(std::size_t j=0;j<count;++j) {
        const auto& c=contacts[j];
        if(!std::isfinite(c.value))return false;
        if(c.value<minimum)continue;
        std::uint32_t id=0;
        if(c.first_kind==0&&c.second_kind==0)id=c.first==self.id?c.second:c.first;
        else if(c.first_kind==0&&c.second_kind==4)id=c.second;
        else if(c.first_kind==4&&c.second_kind==0)id=c.first;
        else continue;
        int index=-1;
        for(unsigned i=0;i<n.count;++i)if(n.entries[i].id==id) {
            if(index!=-1)return false;
            index=static_cast<int>(i);
        }
        if(index<0||(racers_only&&!actors[index].race_actor))continue;
        if(chosen<0){chosen=index;continue;}
        const bool human=actors[index].human,old_human=actors[chosen].human;
        if(human&&!old_human){chosen=index;continue;}
        if(human!=old_human)continue;
        const float old_gap=n.entries[chosen].distance-self.distance;
        const float gap=n.entries[index].distance-self.distance;
        if(!std::isfinite(old_gap)||!std::isfinite(gap))return false;
        if((old_gap<0&&gap>0)||gap<old_gap)chosen=index;
    }
    target=chosen<0?0:n.entries[chosen].id;return true;
}
bool ai_strategy_blocking_mode(unsigned mode,AiBlockingMode& output) noexcept {
    if(mode>2)return false;
    output={mode==2?400:200,true};return true;
}
bool ai_strategy_blocking(const AiRoadChain& chain,const AiNeighborhood& neighbors,
                          const AiBlockingInput& in,AiActionCandidate& result) noexcept {
    if(chain.count>40||neighbors.count>16||neighbors.self_index<0||
       static_cast<unsigned>(neighbors.self_index)>=neighbors.count)return false;
    AiActionCandidate next;next.kind=5;next.priority=-1;
    if(in.race_time_hold){result=next;return true;}
    const auto& self=neighbors.entries[static_cast<unsigned>(neighbors.self_index)];
    for(unsigned i=static_cast<unsigned>(neighbors.self_index)+1;i<neighbors.count;++i) {
        const auto& meta=in.actors[i];
        if(!std::isfinite(meta.speed))return false;
        if(!meta.race_actor||meta.speed<10.F)continue;
        const auto& e=neighbors.entries[i];
        if(e.cell>=chain.count)return false;
        const auto& cell=chain.cells[e.cell];
        const float outside=e.lateral>0?e.lateral-cell.upper[0]:cell.lower[0]-e.lateral;
        float distance=e.distance-self.distance;
        if(in.subtract_lengths)distance-=e.half_extent[1]+self.half_extent[1];
        const float relative=e.relative_velocity[1];
        if(!std::isfinite(outside)||!std::isfinite(distance)||!std::isfinite(relative))return false;
        if(outside<0&&distance<0&&e.distance>chain.cells[0].distance&&distance>-50.F&&relative>-5.F) {
            bool predicted=false;
            if(relative>.001F) {
                const float seconds=std::fabs(distance/relative);
                predicted=seconds<=(meta.human?30.F:2.F)&&seconds<99999.F;
            }
            if(predicted||(meta.human&&distance>-35.F)){next.priority=in.mode_priority;next.target=e.id;}
        }
        // Original jumps to the decision with the FIRST eligible actor.
        // It does not continue scanning when that actor fails the geometry.
        break;
    }
    result=next;return true;
}
bool ai_strategy_rollover(int contacts,const AiDriverPoint& up,bool& request) noexcept {
    for(float x:up)if(!std::isfinite(x))return false;
    const float length=std::sqrt((up[0]*up[0]+up[1]*up[1])+up[2]*up[2]);
    float z=0;
    if(length!=0)z=up[2]*(1.F/length);
    if(!std::isfinite(z))return false;
    request=contacts<2&&z<=.707F;return true;
}
bool ai_strategy_allow_target(std::uint32_t target,std::uint32_t tick,
                              float cooldown,float probability,
                              AiManeuverGate& state,std::uint32_t& rng,bool& allow) noexcept {
    if(!std::isfinite(cooldown)||!std::isfinite(probability))return false;
    auto next=state;auto random=rng;
    const std::uint32_t delta=tick-next.rejected_tick;std::int32_t signed_delta;
    std::memcpy(&signed_delta,&delta,4);
    const float seconds=static_cast<float>(signed_delta)*.00025F;
    if(seconds>=cooldown)next.rejected_target=0;
    bool accepted=false;
    if(target!=next.rejected_target) {
        const auto sample=random%0x7fffffffU;
        const auto x=random^0x1d872b41U,y=(x>>5)^x;
        random=(y<<27)^y^x;
        const float value=static_cast<float>(sample)*4.656612873077393e-10F;
        accepted=value<probability;
        if(!accepted){next.rejected_target=target;next.rejected_tick=tick;}
    }
    state=next;rng=random;allow=accepted;return true;
}
namespace {
float reaction_random(std::uint32_t& state,float extent) noexcept {
    const auto sample=state%0x7fffffffU;
    const auto x=state^0x1d872b41U,y=(x>>5)^x;
    state=(y<<27)^y^x;
    return (float(sample)*0x1p-31F)*extent;
}
float reaction_elapsed(std::uint32_t now,std::uint32_t then) noexcept {
    const auto bits=now-then;std::int32_t delta;std::memcpy(&delta,&bits,4);
    return float(delta)*.00025F;
}
}
bool ai_strategy_react(const AiReactionInput& in,AiReactionState& state,std::uint32_t& rng) noexcept {
    if(!std::isfinite(in.relative))return false;
    if(in.target_disabled||in.target_role==3||(!in.racing&&in.target_role!=1)||
       (in.self_selector==3?!in.eligible:in.self_selector!=4))return true;
    AiReactionState next;next.tick=in.tick;
    next.first=(reaction_random(rng,.4F)+.3F)-.2F;
    next.gap=(reaction_random(rng,.2F)+.15F)-.1F;
    next.last=(reaction_random(rng,.4F)+.3F)-.2F;
    next.direction=reaction_random(rng,1.F)<.5F;
    float probability;
    if(in.relative>50.F){next.direction=true;next.last+=.3F+.3F;probability=1.F;}
    else if(in.self_role==3&&in.relative< -3.F)probability=1.F;
    else probability=in.target_role==1?.75F:in.neighbors>5?.05F:.075F;
    next.horn=reaction_random(rng,1.F)<probability;
    if(next.horn)next.flash=reaction_random(rng,1.F)<(in.relative>50.F?.9F:.5F);
    if((state.last+state.first)+state.gap<reaction_elapsed(in.tick,state.tick))state=next;
    return true;
}
AiReactionOutput ai_reaction_output(const AiReactionState& s,std::uint32_t tick,
                                    bool manual,bool human) noexcept {
    if(human)return {};
    float t=reaction_elapsed(tick,s.tick);bool active=false;
    if(t<s.first)active=true;
    else {
        t-=s.first;
        //429190 intentionally compares first twice, not the gap duration.
        if(t<s.first)active=!s.direction;
        else active=t-s.gap<s.last;
    }
    return {(active&&s.flash)||manual,active&&s.horn};
}
bool ai_strategy_choose(const AiStrategySelection& in,AiStrategyEvaluator evaluate,
                        void* context,AiActionCandidate& result) noexcept {
    if(!evaluate)return false;
    struct Gate {AiStrategyQuery query;int kind,priority;};
    const Gate gates[]={{AiStrategyQuery::recovery,10,500},{AiStrategyQuery::obstacle,19,600},
        {AiStrategyQuery::contact,12,400},{AiStrategyQuery::traffic,2,400},
        {AiStrategyQuery::passing,1,300},{AiStrategyQuery::rollover,14,500},
        {AiStrategyQuery::blocking,5,in.mode_priority},{AiStrategyQuery::following,0,200}};
    for(const auto& gate:gates) {
        if(in.current_kind==gate.kind||in.current_priority>gate.priority||
           (gate.query==AiStrategyQuery::recovery&&!in.recovery_enabled))continue;
        bool found=false;AiActionCandidate candidate;candidate.kind=gate.kind;candidate.priority=gate.priority;
        if(!evaluate(context,gate.query,found,candidate))return false;
        if(found){result=candidate;return true;}
    }
    result={7,0,0};return true;
}
bool ai_strategy_preempts(bool have_current,int priority,const AiActionCandidate& candidate) noexcept {
    return have_current&&candidate.priority>priority;
}
bool ai_drag_strategy_choose(const AiStrategySelection& in,AiStrategyEvaluator evaluate,
                             void* context,AiActionCandidate& result) noexcept {
    if(!evaluate)return false;
    const auto query=[&](AiStrategyQuery which,int kind,int priority,bool& found,AiActionCandidate& candidate) {
        candidate={kind,priority,0};found=false;return evaluate(context,which,found,candidate);
    };
    bool found=false;AiActionCandidate next;
    if(in.current_kind!=10&&in.current_priority<=500&&in.recovery_enabled) {
        if(!query(AiStrategyQuery::recovery,10,500,found,next))return false;
        if(found){result=next;return true;}
    }
    if(!query(AiStrategyQuery::obstacle,19,600,found,next))return false;
    if(found){result=next;return true;}
    if(!query(AiStrategyQuery::wall_contact,16,300,found,next))return false;
    if(found){result=next;return true;}
    if(!query(AiStrategyQuery::contact,12,400,found,next))return false;
    if(found){result=next;return true;}
    if(in.current_kind!=2&&in.current_priority<=400) {
        if(!query(AiStrategyQuery::traffic,2,400,found,next))return false;
        if(found){result=next;return true;}
    }
    result={8,0,0};return true;
}
} // namespace nfsu
