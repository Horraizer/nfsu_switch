#include "nfsu/ai_rival_action.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nfsu {
namespace {
float seconds(std::uint32_t now,std::uint32_t then) noexcept {
    const auto bits=now-then;std::int32_t delta;std::memcpy(&delta,&bits,4);
    return float(delta)*.0002500000118743628F;
}
float magnitude(const AiDriverPoint& p) noexcept {
    return std::sqrt((p[2]*p[2]+p[1]*p[1])+p[0]*p[0]);
}
bool same(const AiContactManeuver& a,const AiContactManeuver& b) noexcept {
    if(a.kind!=b.kind)return false;
    return a.kind==2||a.kind==3?a.target==b.target:std::fabs(a.parameter-b.parameter)<=.001F;
}
}
AiRivalActionController::Record* AiRivalActionController::find(std::uint32_t handle) noexcept {
    for(auto& record:records_)if(handle&&record.owned.handle==handle)return &record;
    return nullptr;
}
const AiNeighborEntry* AiRivalActionController::neighbor(std::uint32_t id) const noexcept {
    for(unsigned i=0;i<input_->neighbors->count;++i)
        if(input_->neighbors->entries[i].id==id)return &input_->neighbors->entries[i];
    return nullptr;
}
const AiActionNeighborState* AiRivalActionController::motion(std::uint32_t id) const noexcept {
    for(unsigned i=0;i<input_->neighbors->count;++i)if(input_->motion[i].id==id)return &input_->motion[i];
    return nullptr;
}
bool AiRivalActionController::path(const AiContactManeuver& descriptor,AiManeuverResult& result,int mode) const {
    auto p=input_->planner->path;p.policy.mode=p.extension.mode=mode;p.target=nullptr;p.resolved_target=0;
    if(descriptor.kind==2||descriptor.kind==3) {
        for(unsigned i=0;i<input_->planner->target_count;++i)if(input_->planner->target_ids[i]==descriptor.target) {
            p.target=&input_->planner->targets[i];p.resolved_target=descriptor.target;
            p.category_available=input_->planner->target_available[i];break;
        }
    }
    return ai_action_plan_path(descriptor,p,result);
}
bool AiRivalActionController::evaluate(AiPathCandidate& candidate) const {
    auto p=input_->planner->path;p.target=nullptr;p.resolved_target=0;
    if(candidate.descriptor.kind==2||candidate.descriptor.kind==3) {
        for(unsigned i=0;i<input_->planner->target_count;++i)if(input_->planner->target_ids[i]==candidate.descriptor.target) {
            p.target=&input_->planner->targets[i];p.resolved_target=candidate.descriptor.target;
            p.category_available=input_->planner->target_available[i];break;
        }
    }
    return ai_path_evaluate(p,input_->planner->fitness,{input_->world,ai_path_collision_query},
        input_->planner->side_score_scale,input_->planner->choice.horizon,candidate);
}
bool AiRivalActionController::choose(AiPathChoiceResult& result) const {
    auto p=*input_->planner;p.choice.current=maneuver_;
    p.choice.current_available=maneuver_.kind==1?p.path.category_available:neighbor(maneuver_.target)!=nullptr;
    return ai_plan_path(p,*input_->world,result);
}
bool AiRivalActionController::collision(std::uint32_t& target,float& time) const {
    const auto& world=*input_->world;
    return ai_path_collision_query(&world,world.actors[world.self].path,0,
        input_->planner->choice.horizon,target,time);
}
bool AiRivalActionController::room(const AiContactManeuver& descriptor) const {
    if(descriptor.kind==1)return true;
    const auto* target=neighbor(descriptor.target);if(!target||target->cell>=input_->road->count)return false;
    const auto& self=input_->neighbors->entries[unsigned(input_->neighbors->self_index)];
    const auto& cell=input_->road->cells[target->cell];
    const float clearance=self.half_extent[0]+target->half_extent[0]+5.F;
    const float lateral=target->lateral-self.lateral;
    return descriptor.kind==2?cell.lower[0]<=lateral-clearance:
        descriptor.kind==3&&lateral+clearance<cell.upper[0];
}
//45BAE0: one approaching target, not the all-neighbor45B150 search.
bool AiRivalActionController::target_path(std::uint32_t id,AiPathChoiceResult& result) const {
    const auto* target=neighbor(id);const auto* moving=motion(id);if(!target||!moving)return false;
    const auto& p=*input_->planner;AiTargetPathChoiceInput choice;
    choice.target=id;choice.current=maneuver_;
    for(unsigned i=0;i<p.choice.count;++i)if(p.choice.neighbors[i].id==id){choice.sides=p.choice.neighbors[i].sides;break;}
    const auto& self=input_->world->actors[input_->world->self];
    choice.fast=self.absolute_speed>20.F||magnitude(moving->velocity)>20.F;
    choice.current_available=maneuver_.kind==1?p.path.category_available:
        (maneuver_.kind==2||maneuver_.kind==3)&&neighbor(maneuver_.target);
    const AiTargetPathChoiceOps ops{
        [](void* context,AiPathCandidate& candidate){return static_cast<const AiRivalActionController*>(context)->evaluate(candidate);},
        [](void* context,const AiContactManeuver& d,bool& value){
            const auto& c=*static_cast<const AiRivalActionController*>(context);
            value=d.kind==1?c.input_->planner->path.category_available:
                (d.kind==2||d.kind==3)&&c.neighbor(d.target);return true;},
        [](void* context,const AiContactManeuver& d,bool& value){value=static_cast<const AiRivalActionController*>(context)->room(d);return true;},
        [](void* context,const AiContactManeuver& d,AiPlannedPath& output){
            AiManeuverResult geometry;if(!static_cast<const AiRivalActionController*>(context)->path(d,geometry,2))return false;
            output=geometry.path;return true;}
    };
    return ai_path_choose_target(choice,ops,const_cast<AiRivalActionController*>(this),result);
}
bool AiRivalActionController::predictive(float distance,float relative) const {
    //4569B0, ordinary zero7372A0 projection domain.
    const auto& self=input_->world->actors[input_->world->self];
    const float vertical=self.acceleration[2]*self.forward[2]+self.acceleration[1]*self.forward[1]+self.acceleration[0]*self.forward[0];
    // The original parameter4 only triggers an unused acceleration query.
    //57A5F0 projects global7372A0; it is zero in the ordinary domain.
    return distance<((3.F*vertical)*.5F+relative)*3.F;
}
bool AiRivalActionController::following(AiActionCandidate& candidate) {
    if(input_->race_seconds<=5.F)return true;
    const auto& n=*input_->neighbors;const auto& self=n.entries[unsigned(n.self_index)];
    for(int j=0;j<n.self_index;++j) {
        const auto& e=n.entries[unsigned(j)];const float gap=e.distance-self.distance;
        const float lateral=e.lateral-self.lateral;
        if(!(gap>0&&lateral<gap+gap&&predictive(gap,e.relative_velocity[1])))continue;
        AiPathChoiceResult result;if(!target_path(e.id,result))return false;
        if(!result.found)continue;
        const auto* other=motion(e.id);if(!other)return false;
        const bool admit=other->role==3||magnitude(other->velocity)<=20.F||
            result.selected.descriptor.kind==1||result.selected.side_admitted;
        if(admit){candidate={0,200,e.id};candidate_path_=result;return true;}
    }
    return true;
}
bool AiRivalActionController::blocked(const AiNeighborEntry& e,AiActionSpeedState& speed,float offset) const {
    const auto* other=motion(e.id);if(!other)return false;
    const auto& self=input_->neighbors->entries[unsigned(input_->neighbors->self_index)];
    speed.speed=std::max(0.F,magnitude(other->velocity)-offset);
    const float relative=e.relative_velocity[1];
    speed.duration=std::fabs(relative)<=.01F?99999.F:
        ((e.distance-self.distance)-self.half_extent[1]-e.half_extent[1])/relative;
    speed.duration=std::max(speed.duration,.1F);speed.route_speed=false;return true;
}
//45D250 preserves the incoming descriptor if every offset is rejected.
bool AiRivalActionController::offset_path(const AiNeighborEntry* target,AiPathCandidate& output,bool& clear) const {
    AiContactManeuver initial=maneuver_;
    if(initial.kind==2||initial.kind==3)initial={};
    AiPathCandidate candidate;candidate.descriptor=initial;
    if(!evaluate(candidate))return false;
    if(candidate.accepted){clear=true;output=candidate;return true;}
    candidate={};if(!evaluate(candidate))return false;
    if(candidate.accepted){clear=true;output=candidate;return true;}
    const auto& n=*input_->neighbors;
    const auto& cell=input_->road->cells[n.entries[unsigned(n.self_index)].cell];
    for(float offset=4.F;offset<cell.upper[0]||-offset>cell.lower[0];offset+=4.F) {
        const bool reverse=target&&target->relative_velocity[0]>0;
        for(float side:{reverse?-offset:offset,reverse?offset:-offset}) {
            if(!(side>cell.lower[0]&&side<cell.upper[0]))continue;
            candidate={};candidate.descriptor.parameter=side;
            if(!evaluate(candidate))return false;
            if(candidate.accepted){clear=true;output=candidate;return true;}
        }
    }
    output={};output.descriptor=initial;AiManeuverResult geometry;
    if(!path(initial,geometry))return false;
    output.geometry=geometry;clear=false;return true;
}
bool AiRivalActionController::react(std::uint32_t id) {
    const auto* e=neighbor(id);const auto* other=motion(id);if(!e||!other)return false;
    const auto& n=*input_->neighbors;const auto& self=n.entries[unsigned(n.self_index)];
    AiReactionInput in;in.tick=input_->tick;in.relative=e->relative_velocity[1];
    in.neighbors=n.count;in.target_role=other->role;
    //456780, the same direction/length eligibility used by ordinary actor24=3.
    in.eligible=e->distance-self.distance> -(self.half_extent[1]+e->half_extent[1]+(other->role==1?-.5F:0.F));
    return ai_strategy_react(in,reaction_,*input_->random);
}
bool AiRivalActionController::query(void* opaque,AiStrategyQuery which,bool& found,AiActionCandidate& candidate) {
    auto& c=*static_cast<AiRivalActionController*>(opaque);const auto& in=*c.input_;
    const auto& n=*in.neighbors;found=false;
    if(which==AiStrategyQuery::recovery) {
        if(!in.recovery||!ai_recovery_available(in.tick,c.last_recovery_))return true;
        AiRecoveryRequest request;if(!ai_recovery_request(*in.recovery,request))return false;
        found=request.requested;c.pending_.clear_road|=request.clear_road;
    } else if(which==AiStrategyQuery::obstacle){WorldObstacleStatus obstacle;if(!c.read_obstacle(obstacle))return false;found=obstacle.available;}
    else if(which==AiStrategyQuery::wall_contact)found=in.wall_contact;
    else if(which==AiStrategyQuery::contact) {
        const float minimum=in.drag_strategy?8.33329963684082F:-1.F; //44DD30:41055532
        if(!ai_strategy_contact(n,in.actors,in.contacts,in.contact_count,minimum,true,candidate.target))return false;
        found=candidate.target!=0;
        if(found&&!c.react(candidate.target))return false;
    } else if(which==AiStrategyQuery::traffic||which==AiStrategyQuery::passing) {
        float time=99999;std::uint32_t target=0;
        if(!c.collision(target,time))return false;
        if(which==AiStrategyQuery::traffic) {
            const auto* other=c.motion(target);
            if(target&&other&&other->role==3&&!in.force_traffic_action&&other->strategy==AiStrategyKind::none)return false;
            found=ai_strategy_traffic_admitted(target!=0,other!=nullptr,other?other->role:0,
                other?other->strategy:AiStrategyKind::none,in.force_traffic_action);
            if(found){candidate.target=target;c.candidate_time_=time;if(!c.react(target))return false;}
        } else if(target&&time<in.planner->choice.horizon) {
            if(!c.choose(c.candidate_path_))return false;
            if(!c.react(target))return false;
            bool allowed=false;
            if(!ai_strategy_allow_target(target,in.tick,1.F,in.target_probability,c.gate_,*in.random,allowed))return false;
            found=allowed;
            if(found){candidate.target=target;c.candidate_time_=time;if(!c.react(target))return false;}
        } else if(c.maneuver_.kind==2||c.maneuver_.kind==3) {
            const auto* e=c.neighbor(c.maneuver_.target);const auto* other=c.motion(c.maneuver_.target);
            bool eligible=false;for(unsigned i=0;i<in.planner->choice.count;++i)
                if(in.planner->choice.neighbors[i].id==c.maneuver_.target)eligible=in.planner->choice.neighbors[i].eligible;
            if(e&&other&&eligible) {
                const float relative=e->relative_velocity[1];
                const float gap=e->distance-n.entries[unsigned(n.self_index)].distance;
                const float predicted=std::fabs(relative)>.01F?gap/relative+.3F:
                    magnitude(other->velocity)<10.F?in.planner->choice.horizon:99999.F;
                if(predicted<in.planner->choice.horizon+2.F) {
                    if(!c.react(e->id))return false;
                    bool allowed=false;
                    if(!ai_strategy_allow_target(e->id,in.tick,1.F,in.target_probability,c.gate_,*in.random,allowed))return false;
                    found=allowed;if(found){candidate.target=e->id;c.candidate_time_=predicted;
                        c.candidate_path_.selected.descriptor=c.maneuver_;c.candidate_path_.found=true;
                        for(unsigned j=0;j<in.planner->choice.count;++j)if(in.planner->choice.neighbors[j].id==e->id) {
                            const unsigned side=in.planner->choice.neighbors[j].sides;
                            c.candidate_path_.selected.side_admitted=side==1||side==unsigned(c.maneuver_.kind);break;}}
                }
            }
        }
    } else if(which==AiStrategyQuery::rollover) {
        if(!ai_strategy_rollover(int(in.support),in.up,found))return false;
    } else if(which==AiStrategyQuery::blocking) {
        AiBlockingInput block;block.race_time_hold=in.hold;block.subtract_lengths=in.subtract_lengths;
        block.mode_priority=in.mode_priority;block.actors=in.actors;
        if(!ai_strategy_blocking(*in.road,n,block,candidate))return false;
        found=candidate.priority>=0;
    } else if(which==AiStrategyQuery::following) {
        candidate.target=0;if(!c.following(candidate))return false;
        if(candidate.target) {
            bool allowed=false;
            if(!ai_strategy_allow_target(candidate.target,in.tick,1.F,in.target_probability,c.gate_,*in.random,allowed))return false;
            found=allowed;
        }
    } else return false;
    if(found)c.candidate_=candidate;
    return true;
}
bool AiRivalActionController::read_obstacle(WorldObstacleStatus& out)const {
    const auto& in=*input_;
    if(in.obstacle_read)return in.obstacle_read(in.obstacle_context,out);
    out={in.obstacle_available,in.obstacle_passed,!in.obstacle_blocked};return true;
}
bool AiRivalActionController::construct(const AiActionCandidate& candidate,float duration,AiOwnedAction& output) {
    Record* slot=nullptr;for(auto& r:records_)if(!r.owned.handle){slot=&r;break;}
    if(!slot||!next_handle_)return false;
    *slot={};slot->owned={next_handle_++,candidate.kind,candidate.priority,duration>=0,0};
    std::int32_t signed_tick;std::memcpy(&signed_tick,&input_->tick,4);
    const float rounded=(float(signed_tick)*.0002500000118743628F)*4000.F+.5F;
    if(!std::isfinite(rounded)||rounded>=2147483648.F||rounded< -2147483648.F)return false;
    slot->duration=duration;slot->start=input_->tick;
    if(candidate.kind==10)slot->recovery_hold_seconds=input_->recovery_hold_seconds;
    slot->epoch=std::uint32_t(std::int32_t(rounded));slot->state.target=candidate.target;
    slot->state.speed={0,0,0,true,true,true,false};
    if(candidate.kind==1||candidate.kind==2) {
        slot->collision_time=candidate_time_;slot->complete_after=candidate_time_+2.F;
        slot->last_update=input_->tick;slot->found=candidate_path_.found;
        slot->side_admitted=candidate_path_.selected.side_admitted;
    }
    if(candidate.kind==0)slot->last_update=input_->tick;
    if(candidate.kind==0||candidate.kind==1)maneuver_=candidate_path_.selected.descriptor;
    if(candidate.kind==10)last_recovery_=input_->tick;
    output=slot->owned;return true;
}
bool AiRivalActionController::create(void* opaque,int kind,const AiOwnedAction& current,AiOwnedAction& output) {
    auto& c=*static_cast<AiRivalActionController*>(opaque);
    AiActionCandidate selected{kind,0,0};c.candidate_path_={};c.candidate_time_=99999;
    if(kind==20) {
        if(c.input_->finished){output={};return true;}
        AiStrategySelection s;s.current_kind=current.handle?current.kind:20;
        s.current_priority=current.handle?current.priority:-1;s.mode_priority=c.input_->mode_priority;
        s.recovery_enabled=ai_recovery_available(c.input_->tick,c.last_recovery_);
        if(!(c.input_->drag_strategy?ai_drag_strategy_choose(s,query,&c,selected):
            ai_strategy_choose(s,query,&c,selected)))return false;
    }
    if(selected.kind==4)selected.priority=100;
    if(selected.kind==7)selected.priority=0;
    return c.construct(selected,selected.kind==0||selected.kind==10?5.F:-1.F,output);
}
bool AiRivalActionController::predicates(Record& r,AiActionPredicate which,bool& yes) {
    const auto& in=*input_;yes=false;const int kind=r.owned.kind;
    if(which==AiActionPredicate::timeout){yes=r.owned.timed&&seconds(in.tick,r.epoch)>r.duration;return true;}
    if(kind==12&&which==AiActionPredicate::complete)
        return ai_action_contact_complete(*in.neighbors,in.actors,in.contacts,in.contact_count,r.state,yes);
    if(kind==10) {
        if(!in.recovery||!in.recovery_ahead)return false;
        AiRecoveryExit exit;
        if(!ai_recovery_finish({r.start,r.epoch,r.recovery_hold_seconds,r.duration},*in.recovery,*in.recovery_ahead,
            in.planner->path.extension.category,exit))return false;
        yes=which==AiActionPredicate::abort?exit.status==2:exit.status==1;return true;
    }
    if(kind==1&&which==AiActionPredicate::complete)yes=seconds(in.tick,r.start)>=r.complete_after;
    if(kind==14&&which==AiActionPredicate::complete)yes=r.reset||(in.support==4&&magnitude(in.up)>0&&in.up[2]/magnitude(in.up)>.707F);
    if(kind==19&&which==AiActionPredicate::complete){WorldObstacleStatus obstacle;if(!read_obstacle(obstacle))return false;yes=obstacle.passed;}
    if(kind==16&&which==AiActionPredicate::complete)yes=!in.wall_contact;
    if(kind==5) {
        if(which==AiActionPredicate::complete) {
            AiBlockingInput block;block.race_time_hold=in.hold;block.subtract_lengths=in.subtract_lengths;
            block.mode_priority=in.mode_priority;block.actors=in.actors;AiActionCandidate next;
            if(!ai_strategy_blocking(*in.road,*in.neighbors,block,next))return false;
            yes=next.priority<0;if(!yes)r.state.target=next.target;
        }else {const auto* e=neighbor(r.state.target);yes=e&&e->distance>0;}
    }
    if(kind==0) {
        if(which==AiActionPredicate::complete&&in.neighbors->count&&
           in.neighbors->entries[0].id!=r.state.target) {
            AiActionCandidate candidate{0,-1,0};
            if(!following(candidate))return false;
            if(candidate.target&&!same(maneuver_,candidate_path_.selected.descriptor)) {
                maneuver_=candidate_path_.selected.descriptor;
                r.state.target=candidate.target;r.last_update=in.tick;
            }
        }
        const auto* e=neighbor(r.state.target);yes=false;
        if(which==AiActionPredicate::complete&&seconds(in.tick,r.last_update)>=3.F&&!e)yes=true;
        if(e&&seconds(in.tick,r.last_update)>=3.F) {
            const auto& self=in.neighbors->entries[unsigned(in.neighbors->self_index)];
            const float gap=e->distance-self.distance;
            yes=which==AiActionPredicate::complete?gap< -(self.half_extent[1]+e->half_extent[1]+10.F):
                gap>0&&!predictive(gap,e->relative_velocity[1]);
        }else if(e)yes=false;
    }
    if(kind==2&&which==AiActionPredicate::complete) {
        std::uint32_t target=0;float time=99999;if(!collision(target,time))return false;
        yes=!target||!in.traffic_continuation;if(!yes){r.state.target=target;r.collision_time=time;}
    }
    return true;
}
bool AiRivalActionController::predicate(void* opaque,const AiOwnedAction& action,AiActionPredicate which,bool& yes) {
    auto& c=*static_cast<AiRivalActionController*>(opaque);auto* r=c.find(action.handle);
    return r&&c.predicates(*r,which,yes);
}
bool AiRivalActionController::cleanup(void* opaque,const AiOwnedAction& action) {
    auto& c=*static_cast<AiRivalActionController*>(opaque);if(!c.find(action.handle))return false;
    if(action.kind==10){c.pending_.clear_road=true;c.pending_.reset_driver=true;}
    if(action.kind==19&&c.input_->obstacle_cleanup)c.input_->obstacle_cleanup(c.input_->obstacle_context);
    return true;
}
void AiRivalActionController::release(void* opaque,const AiOwnedAction& action) {
    auto& c=*static_cast<AiRivalActionController*>(opaque);if(auto* r=c.find(action.handle))*r={};
}
bool AiRivalActionController::contact_path(void* opaque,const AiContactManeuver& descriptor,AiPlannedPath& result) {
    auto& c=*static_cast<AiRivalActionController*>(opaque);AiManeuverResult geometry;
    if(!c.path(descriptor,geometry))return false;
    result=geometry.path;c.maneuver_=descriptor;return true;
}
bool AiRivalActionController::contact_class(void* opaque,int& result) {
    const auto& in=*static_cast<AiRivalActionController*>(opaque)->input_;
    return in.recovery&&in.recovery->road&&ai_recovery_road_class(*in.recovery->road,in.recovery->motion,false,result);
}
bool AiRivalActionController::update_record(Record& r) {
    const auto& in=*input_;const auto& n=*in.neighbors;const auto& self=n.entries[unsigned(n.self_index)];
    const auto& actor=in.world->actors[in.world->self];const int kind=r.owned.kind;
    r.state.speed.ready=true;r.state.speed.enabled=true;r.state.speed.catchup=true;
    if(kind==8||kind==16) {
        auto p=in.planner->path;bool copied=false;
        const bool refresh=kind==16||!r.forward_initialized||seconds(in.tick,r.last_update)>=3.F||maneuver_.kind!=1;
        if(refresh) {
            r.last_update=in.tick;r.forward_initialized=true;
            //45CFF0 retains a forward descriptor already on lane11..16.
            //453600 always reselects the nearest unrestricted header lane.
            const bool keep=kind==8&&maneuver_.kind==1&&p.extension.category>=11&&p.extension.category<=16;
            if(!keep&&in.nearest_lane>=11&&in.nearest_lane<=16) {
                p.extension.category=in.nearest_lane;p.rebuild=true;p.previous={};p.category_available=true;
                AiManeuverResult geometry;bool accepted=true;
                if(kind==16) {
                    if(!ai_action_plan_path({},p,geometry))return false;
                }else {
                    AiPathCandidate candidate;candidate.descriptor={};
                    if(!ai_path_evaluate(p,in.planner->fitness,{in.world,ai_path_collision_query},
                        in.planner->side_score_scale,in.planner->choice.horizon,candidate))return false;
                    ++pending_.evaluations;geometry=candidate.geometry;accepted=candidate.accepted;
                }
                if(accepted) {
                    r.state.path=geometry.path;maneuver_={};copied=true;
                    pending_.lane_category=in.nearest_lane;
                }
            }
        }
        if(!copied){AiManeuverResult geometry;if(!path(maneuver_,geometry))return false;r.state.path=geometry.path;}
        r.state.speed={1000.F,0,kind==8?2.F:0.F,false,true,true,true};return true;
    }
    if(kind==17) {
        AiPathChoiceResult result;if(!choose(result))return false;
        maneuver_=result.selected.descriptor;r.state.path=result.selected.geometry.path;pending_.evaluations+=result.evaluations;
        r.state.speed={0,0,0,false,true,true,true};return true;
    }
    if(kind==7||kind==4) {
        bool copied=false;
        if(kind==7&&seconds(in.tick,r.last_update)>=3.F) {
            r.last_update=in.tick;
            if(maneuver_.kind!=1) {
                AiPathCandidate candidate;if(!evaluate(candidate))return false;
                ++pending_.evaluations;
                if(candidate.accepted){maneuver_={};r.state.path=candidate.geometry.path;copied=true;}
            }
        }
        if(maneuver_.kind!=1)maneuver_={};
        if(!copied){AiManeuverResult geometry;if(!path(maneuver_,geometry))return false;r.state.path=geometry.path;}
        r.state.speed.route_speed=true;r.state.speed.auxiliary=0;
        return kind!=4||ai_action_fallback_speed(n,in.ride_category,in.motion.data(),n.count,6.F,20.F,r.state.speed);
    }
    if(kind==10) {
        if(!in.recovery_ahead||!in.road->count)return false;
        AiRecoveryPlan plan;
        if(!ai_recovery_plan(*in.recovery_ahead,in.road->cells[self.cell],in.road->cells.data(),in.road->count,
            in.planner->path.extension,3,plan))return false;
        r.state.path=plan.path;r.state.speed={plan.speed,0,0,plan.route_speed,plan.enabled,plan.catchup,true};maneuver_={};return true;
    }
    if(kind==14) {
        AiManeuverResult geometry;maneuver_={};if(!path(maneuver_,geometry,2))return false;
        r.state.path=geometry.path;r.state.speed={0,0,0,false,false,true,true};
        if(seconds(in.tick,r.start)>=5.F&&!r.reset){pending_.rollover_reset=true;r.reset=true;}
        return true;
    }
    const auto* target=neighbor(r.state.target);const auto* moving=motion(r.state.target);
    if(kind==12) {
        if(!target||!moving)return false;
        AiContactActionInput c;c.neighbors=&n;c.category=in.ride_category;c.self_role=2;
        c.target_human=moving->role==1;c.target_velocity=moving->velocity;c.self_direction=actor.forward;
        c.signed_speed=actor.signed_speed;c.controller_speed=1000.F;c.duration=.2F;
        c.human_speed_offset=6.F;c.other_speed_offset=.1F;c.gap_margin=2.8F;c.minimum_speed=20.F;c.slowdown=in.slowdown;
        r.state.maneuver=maneuver_;const AiContactActionOps ops{contact_path,nullptr,contact_class};
        return ai_action_contact_update(c,ops,this,r.state);
    }
    if(kind==5) {
        auto p=in.planner->path;auto extension=p.extension;float step=1.F,offset=0;
        if(target) {
            offset=target->lateral;const float gap=target->distance-self.distance;
            const float relative=target->relative_velocity[1];
            if(gap>=10.F&&std::fabs(relative)>.001F) {
                const float time=gap/relative;
                step=time<=-20.F?.1F:time>=20.F?-.1F:.1F+(time+20.F)*(-.2F/40.F);
            }
        }
        maneuver_={};extension.mode=1;extension.offset_step=step;extension.target_offset=offset;
        extension.margin=in.subtract_lengths?1.F:0.F;AiPlannedPath path;
        if(!ai_path_continue(p.roads,p.road_count,p.selected,p.initial_offset,extension,path))return false;
        r.state.path=path;r.state.speed={0,0,2,true,true,true,true};return true;
    }
    if(kind==0) {
        if(!target||!moving){maneuver_={};AiManeuverResult geometry;if(!path(maneuver_,geometry,2))return false;
            r.state.path=geometry.path;r.state.speed.ready=false;return true;}
        const float gap=target->distance-self.distance;
        if(gap<=0) {
            if(maneuver_.kind!=1)maneuver_={};
            AiPathCandidate candidate;candidate.descriptor=maneuver_;if(!evaluate(candidate))return false;
            r.state.path=candidate.geometry.path;r.state.speed.route_speed=true;return true;
        }
        AiPathChoiceResult result;if(!target_path(target->id,result))return false;
        maneuver_=result.selected.descriptor;r.state.path=result.selected.geometry.path;r.state.speed.route_speed=true;
        pending_.evaluations+=result.evaluations;return true;
    }
    if(kind==1) {
        bool replanned=false;
        if(seconds(in.tick,r.last_update)>0&&(!in.hold||(moving&&moving->role==1))) {
            if(maneuver_.kind==1&&target&&moving&&actor.absolute_speed<10.F&&magnitude(moving->velocity)<5.F) {
                maneuver_={2,target->id,0,false};if(!room(maneuver_))maneuver_.kind=3;
            }
            AiPathChoiceResult result;if(!choose(result))return false;
            maneuver_=result.selected.descriptor;r.state.path=result.selected.geometry.path;
            r.found=result.found;r.side_admitted=result.selected.side_admitted;r.last_update=in.tick;
            pending_.evaluations+=result.evaluations;replanned=true;
            if(!r.found&&actor.absolute_speed<=20.F) {
                const auto& cell=in.road->cells[self.cell];
                const int mode=std::fabs(cell.upper[0]-self.lateral)<=std::fabs(cell.lower[0]-self.lateral)?4:3;
                auto p=in.planner->path;AiPlannedPath path;auto extension=p.extension;
                extension.mode=mode;extension.offset_step=99999.F;extension.margin=0;extension.target_offset=0;
                if(!ai_path_continue(p.roads,p.road_count,p.selected,p.initial_offset,extension,path))return false;
                r.state.path=path;r.found=true;maneuver_={};
            }
        }
        if(!replanned||!r.state.path.path.count||maneuver_.kind==0) {
            if(maneuver_.kind==0||!((maneuver_.kind==1&&in.planner->path.category_available)||
               ((maneuver_.kind==2||maneuver_.kind==3)&&neighbor(maneuver_.target))))maneuver_={};
            AiPathCandidate candidate;candidate.descriptor=maneuver_;candidate.side_admitted=r.side_admitted;
            if(!evaluate(candidate))return false;
            r.state.path=candidate.geometry.path;r.found=candidate.accepted;
        }
        r.state.speed.route_speed=true;r.state.speed.auxiliary=0;
        if(!r.found&&target&&moving) {
            const float gap=target->distance-self.distance;
            const float lateral=target->lateral-self.lateral;
            const bool ahead=moving->role==1?target->relative_velocity[1]*.1F+gap>self.half_extent[1]*.5F:gap>0;
            if(ahead&&std::fabs(lateral)<self.half_extent[0]+target->half_extent[0]+1.F&&
               !(actor.absolute_speed<10.F&&magnitude(moving->velocity)<5.F)) {
                if(!blocked(*target,r.state.speed,.1F))return false;
            }
        }
        return true;
    }
    if(kind==19) {
        WorldObstacleStatus obstacle;if(!read_obstacle(obstacle))return false;
        //454C90 distinguishes an ahead annotation from the actual window.
        bool blocked=!in.obstacle_marker_present&&!r.obstacle_marker&&!obstacle.clear;
        if(in.obstacle_marker_present)r.obstacle_marker=in.obstacle_marker_animation;
        AiPathChoiceResult result;if(!choose(result))return false;
        maneuver_=result.selected.descriptor;r.state.path=result.selected.geometry.path;
        pending_.evaluations+=result.evaluations;
        r.state.speed={1000,0,2,true,true,true,true};
        r.state.speed.route_speed=!in.drag_strategy;
        if(!result.found||blocked)r.state.speed={0,0,2,false,true,true,true};
        return true;
    }
    if(kind==2) {
        AiPathCandidate candidate;bool clear=true;
        if(target) {
            if(!offset_path(target,candidate,clear))return false;
            maneuver_=candidate.descriptor;r.state.path=candidate.geometry.path;
        }else {
            AiManeuverResult geometry;if(!path(maneuver_,geometry))return false;r.state.path=geometry.path;
        }
        r.state.speed={1000.F,0,2,false,true,true,true};
        if(!clear&&target) {
            const float x=r.collision_time;
            const float gap=target->distance-self.distance;
            const float relative=target->relative_velocity[1];
            const float acceleration=(actor.acceleration[2]*actor.forward[2]+actor.acceleration[1]*actor.forward[1])+actor.acceleration[0]*actor.forward[0];
            //452DDA..452E4B: urgent obstruction may demand twice the
            //controller speed; this precedes the four-knot slowdown curve.
            if(std::fabs(x)>.001F&&((acceleration*x*.5F+relative)*x+gap)<=3.F) {
                r.state.speed={2000.F,0,0,false,true,true,true};return true;
            }
            if(std::fabs(x)<=.001F&&gap<=0)return true;
            const float t=2.F,k=t*.3333333432674408F;float scale;
            if(x<=0)scale=0;
            else if(x<k)scale=x/k*.9F;
            else if(x<t)scale=.9F+(x-k)/(t-k)*(.999F-.9F);
            else if(x<t+.001F)scale=.999F+(x-t)/((t+.001F)-t)*(1.F-.999F);
            else scale=1.F;
            if(scale<1){r.state.speed.speed=actor.absolute_speed*scale;r.state.speed.auxiliary=scale+scale;}
            r.state.speed.speed=std::max(20.F,r.state.speed.speed);
        }
        return true;
    }
    return false;
}
bool AiRivalActionController::update(void* opaque,const AiOwnedAction& action,bool& ready) {
    auto& c=*static_cast<AiRivalActionController*>(opaque);auto* r=c.find(action.handle);
    if(!r||!c.update_record(*r))return false;
    ready=r->state.speed.ready;return true;
}
bool AiRivalActionController::step(const AiRivalActionInput& in,AiRivalActionOutput& result) noexcept {
    if(input_||!in.planner||!in.world||!in.neighbors||!in.road||!in.random||
       in.neighbors->self_index<0||unsigned(in.neighbors->self_index)>=in.neighbors->count||
       in.neighbors->count>16||in.world->self>=in.world->count||in.ride_category<0||in.ride_category>4)return false;
    input_=&in;pending_={};
    if(in.road_reset)maneuver_={};
    if(!owner_.current.handle) {
        if(!construct({in.drag_strategy?8:7,0,0},-1,owner_.current)){input_=nullptr;return false;}
    }
    if(in.finished&&owner_.current.kind!=17)owner_.requested_kind=17;
    const AiActionOwnerOps ops{predicate,create,cleanup,release,update};
    const bool okay=ai_action_owner_step(ops,this,owner_);
    auto* r=find(owner_.current.handle);
    if(!okay||!r){input_=nullptr;return false;}
    pending_.action=owner_.current;pending_.path=r->state.path;pending_.speed=r->state.speed;
    pending_.target=r->state.target;
    pending_.maneuver=maneuver_;pending_.pair_queries=in.world->pair_queries;
    pending_.path_published=true;
    input_=nullptr;result=pending_;return true;
}
bool AiRivalActionController::refresh(const AiRivalActionInput& in,AiRivalActionOutput& result) noexcept {
    if(input_||!in.planner||!in.world||!in.neighbors||!in.road||!in.random||
       in.neighbors->self_index<0||unsigned(in.neighbors->self_index)>=in.neighbors->count||
       in.neighbors->count>16||in.world->self>=in.world->count||in.ride_category<0||in.ride_category>4)return false;
    auto* r=find(owner_.current.handle);if(!r)return false;
    input_=&in;pending_={};
    pending_.action=owner_.current;pending_.path=in.planner->path.previous.path;pending_.speed=r->state.speed;
    pending_.target=r->state.target;pending_.maneuver=maneuver_;
    bool okay=true;
    if(owner_.current.kind==5) {
        okay=update_record(*r);
        if(okay){pending_.path=r->state.path;pending_.speed=r->state.speed;pending_.maneuver=maneuver_;pending_.path_published=true;}
    }else if(maneuver_.kind==2||maneuver_.kind==3) {
        const auto* target=neighbor(maneuver_.target);const auto* moving=motion(maneuver_.target);
        if(target&&moving&&moving->role==1) {
            AiManeuverResult geometry;okay=path(maneuver_,geometry);
            if(okay){pending_.path=geometry.path;pending_.path_published=true;}
        }
    }
    pending_.pair_queries=in.world->pair_queries;
    input_=nullptr;if(!okay)return false;
    result=pending_;return true;
}
} // namespace nfsu
