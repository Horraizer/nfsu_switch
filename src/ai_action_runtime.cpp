#include "nfsu/ai_action_runtime.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace nfsu {
AiActionRuntime::Record* AiActionRuntime::find(std::uint32_t handle) noexcept {
    for(auto& record:records_)if(handle&&record.action.handle==handle)return &record;
    return nullptr;
}
bool AiActionRuntime::construct(const AiActionCreation& in,std::uint32_t tick,AiOwnedAction& out) noexcept {
    if(!in.present){out={};return true;}
    if((in.candidate.kind!=4&&in.candidate.kind!=12)||!std::isfinite(in.duration)||
       (in.candidate.kind==12&&in.candidate.target==0)||next_handle_==0)return false;
    Record* slot=nullptr;
    for(auto& record:records_)if(!record.action.handle){slot=&record;break;}
    if(!slot)return false;
    std::int32_t signed_tick;std::memcpy(&signed_tick,&tick,4);
    //44A080: source binary32 world-seconds round trip and truncation.
    const float seconds=static_cast<float>(signed_tick)*.0002500000118743628F;
    const float rounded=(seconds*4000.F)+.5F;
    if(!std::isfinite(rounded)||rounded>=2147483648.F||rounded< -2147483648.F)return false;
    *slot={};slot->epoch=static_cast<std::uint32_t>(static_cast<std::int32_t>(rounded));
    slot->duration=in.duration;
    slot->action={next_handle_++,in.candidate.kind,in.candidate.priority,in.duration>=0,0};
    slot->state.target=in.candidate.target;
    slot->state.speed.enabled=true;slot->state.speed.route_speed=true;slot->state.speed.catchup=true;
    out=slot->action;++constructions_;return true;
}
bool AiActionRuntime::initialize(const AiActionCreation& in,std::uint32_t tick) noexcept {
    if(owner_.current.handle||failed_||!in.present)return false;
    return construct(in,tick,owner_.current);
}
bool AiActionRuntime::request(int kind) noexcept {
    if(failed_||!owner_.current.handle)return false;
    owner_.requested_kind=kind;return true;
}
bool AiActionRuntime::predicate(void* p,const AiOwnedAction& action,AiActionPredicate which,bool& yes) {
    auto& runtime=*static_cast<AiActionRuntime*>(p);auto* record=runtime.find(action.handle);
    if(!record||!runtime.input_)return false;
    const auto& in=*runtime.input_;yes=false;
    if(which==AiActionPredicate::timeout) {
        const auto bits=in.tick-record->epoch;std::int32_t delta;std::memcpy(&delta,&bits,4);
        yes=static_cast<float>(delta)*.0002500000118743628F>record->duration&&action.timed;
    } else if(which==AiActionPredicate::complete&&action.kind==12) {
        if(!in.neighbors||!in.actors)return false;
        if(!ai_action_contact_complete(*in.neighbors,*in.actors,in.contacts,in.contact_count,record->state,yes))return false;
    }
    return true; //452D10 is false for abort, and fallback completion.
}
bool AiActionRuntime::create(void* p,int requested,const AiOwnedAction& current,AiOwnedAction& out) {
    auto& runtime=*static_cast<AiActionRuntime*>(p);const auto& in=*runtime.input_;
    AiActionCreation selected;
    if(!in.select||!in.select(in.selection_context,requested,current,selected))return false;
    return runtime.construct(selected,in.tick,out);
}
bool AiActionRuntime::cleanup(void* p,const AiOwnedAction& action) {
    auto& runtime=*static_cast<AiActionRuntime*>(p);
    if(!runtime.find(action.handle))return false;
    runtime.clear_recovery_=true;return true;
}
void AiActionRuntime::release(void* p,const AiOwnedAction& action) {
    auto& runtime=*static_cast<AiActionRuntime*>(p);
    if(auto* record=runtime.find(action.handle))*record={};
}
bool AiActionRuntime::contact_path(void* p,const AiContactManeuver& descriptor,AiPlannedPath& path) {
    auto& runtime=*static_cast<AiActionRuntime*>(p);AiManeuverResult result;
    auto plan=runtime.input_->path;plan.extension.mode=0;plan.policy.mode=0;
    if(!ai_action_plan_path(descriptor,plan,result))return false;
    runtime.category_=plan.extension.category;
    path=result.path;++runtime.path_updates_;return true;
}
bool AiActionRuntime::road_class(void* p,int& result) {
    const auto& in=*static_cast<AiActionRuntime*>(p)->input_;
    return in.selected_road&&ai_recovery_road_class(*in.selected_road,in.recovery_motion,in.recovery_target_override,result);
}
bool AiActionRuntime::update(void* p,const AiOwnedAction& action,bool& ready) {
    auto& runtime=*static_cast<AiActionRuntime*>(p);const auto& in=*runtime.input_;
    auto* record=runtime.find(action.handle);if(!record||!in.neighbors)return false;
    if(action.kind==12) {
        auto contact=in.contact;contact.neighbors=in.neighbors;
        record->state.maneuver=runtime.maneuver_;
        // Same-epoch contact completion must run before this update. A target
        // missing despite that refuses;45B150 is not replaced by a fake path.
        const AiContactActionOps ops{contact_path,nullptr,road_class};
        if(!ai_action_contact_update(contact,ops,&runtime,record->state))return false;
        runtime.maneuver_=record->state.maneuver;
    } else if(action.kind==4) {
        auto plan=runtime.maneuver_.kind==1?in.path:in.fallback_path;
        plan.extension.mode=0;plan.policy.mode=0;
        runtime.maneuver_={1,0,0,false};AiManeuverResult result;
        if(!ai_action_plan_path(runtime.maneuver_,plan,result)||
           !ai_action_fallback_speed(*in.neighbors,plan.extension.category,
               in.motion,in.motion_count,in.fallback_gap,in.fallback_desired_gap,record->state.speed))return false;
        runtime.category_=plan.extension.category;
        record->state.path=result.path;++runtime.path_updates_;
    } else return false;
    ready=record->state.speed.ready;return true;
}
bool AiActionRuntime::step(const AiActionRuntimeInput& in,AiActionRuntimeOutput& result) noexcept {
    if(failed_||!owner_.current.handle||input_)return false;
    clear_recovery_=false;constructions_=0;path_updates_=0;input_=&in;
    const AiActionOwnerOps ops{predicate,create,cleanup,release,update};
    const bool ok=ai_action_owner_step(ops,this,owner_);input_=nullptr;
    if(!ok){failed_=true;return false;}
    auto* record=find(owner_.current.handle);
    if(!record){failed_=true;return false;}
    record->action=owner_.current;
    result={owner_.current,record->epoch,record->duration,category_,record->state.path,record->state.speed,clear_recovery_,constructions_,path_updates_};
    return true;
}
} // namespace nfsu
