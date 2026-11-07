#include "nfsu/ai_traffic_action.h"
#include <array>
#include <cstring>
#include <limits>
#include <iostream>
#include <vector>

using namespace nfsu;
namespace {
#include "traffic_owner_native.inc"
#include "traffic_density_native.inc"
#include "traffic_stop_action_native.inc"
unsigned failures=0;
void check(bool ok,const char* message) {
    if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
}
struct OwnerWitness {
    NativeTrafficOwnerCase row;
    std::vector<std::array<int,3>> calls;
    bool cleanup_ok=true,promote_ok=true,update_ok=true,same_handle=false,absent=false;
    void record(int op,int first=0,int second=0){calls.push_back({op,first,second});}
    static bool predicate(void* opaque,const AiOwnedAction& a,AiActionPredicate p,bool& yes) {
        auto& w=*static_cast<OwnerWitness*>(opaque);
        const int op=p==AiActionPredicate::timeout?0:p==AiActionPredicate::abort?1:2;
        w.record(op,int(a.handle));yes=(w.row.input[0]&(1<<op))!=0;return true;
    }
    AiOwnedAction candidate()const {
        return {absent?0U:same_handle?1U:2U,row.input[2],row.input[3],false,0};
    }
    static bool choose(void* opaque,int kind,int priority,AiOwnedAction& a) {
        auto& w=*static_cast<OwnerWitness*>(opaque);w.record(3,kind,priority);a=w.candidate();return true;
    }
    static bool initial(void* opaque,AiOwnedAction& a) {
        auto& w=*static_cast<OwnerWitness*>(opaque);w.record(6);a=w.candidate();return true;
    }
    static bool cleanup(void* opaque,const AiOwnedAction& a) {
        auto& w=*static_cast<OwnerWitness*>(opaque);w.record(4,int(a.handle));return w.cleanup_ok;
    }
    static void release(void* opaque,const AiOwnedAction& a) {
        static_cast<OwnerWitness*>(opaque)->record(5,int(a.handle));
    }
    static bool promote(void* opaque) {
        auto& w=*static_cast<OwnerWitness*>(opaque);w.record(7,2,3);return w.promote_ok;
    }
    static bool update(void* opaque,const AiOwnedAction& a) {
        auto& w=*static_cast<OwnerWitness*>(opaque);w.record(8,int(a.handle));return w.update_ok;
    }
};
struct Queries {
    const NativeTrafficStrategyCase& row;
    std::vector<int> calls;
    bool refuse=false;
    static bool evaluate(void* opaque,AiTrafficStrategyQuery query,bool& found,AiActionCandidate&) {
        auto& w=*static_cast<Queries*>(opaque);
        const bool obstacle=query==AiTrafficStrategyQuery::obstacle;
        w.calls.push_back(obstacle?18:3);found=w.row.input[obstacle?2:3]!=0;return !w.refuse;
    }
};
const AiTrafficActionOwnerOps ops{OwnerWitness::predicate,OwnerWitness::choose,OwnerWitness::initial,
    OwnerWitness::cleanup,OwnerWitness::release,OwnerWitness::promote,OwnerWitness::update};
AiTrafficActionOwner state(const NativeTrafficOwnerCase& row) {
    AiTrafficActionOwner s;s.physical_selector=unsigned(row.input[5]);
    if(row.input[4])s.current={1,8,100,row.input[1]!=0,0};
    return s;
}
}
int main() {
    auto bits=[](float f){unsigned word;std::memcpy(&word,&f,4);return word;};
    for(const auto& row:kNativeTrafficSpawnMargin)
        check(ai_traffic_drag_spawn_margin(row[0],row[1],row[2]!=0)==bool(row[3]),
              "original drag spawn margin at inclusive ten-point boundary");
    for(const auto& row:kNativeTrafficSpawnPoint) {
        const std::array<std::uint8_t,4> flags{{std::uint8_t(row[0]),std::uint8_t(row[0]>>8),
            std::uint8_t(row[0]>>16),std::uint8_t(row[0]>>24)}};
        check(ai_traffic_spawn_point_allowed(flags,row[1]!=0)==bool(row[2]),
              "original direction-specific point admission, not header lane availability");
    }
    for(const auto& row:kNativeTrafficSpacing) {
        TrackPathZone zone;
        for(unsigned k=0;k<6;++k)std::memcpy(&zone.traffic_spacing[k],&row[k],4);
        std::uint32_t random=row[9];float spacing=-1;
        check(ai_traffic_spawn_spacing(row[7]?&zone:nullptr,row[6],row[8]!=0,random,spacing),
              "native traffic spacing inputs admitted");
        check(random==row[10]&&bits(spacing)==row[11],"original traffic spacing and world RNG bits");
    }
    for(float delta:{-101.F,-100.F,-99.F,0.F,99.F,100.F,101.F}) {
        check(ai_traffic_spawn_gap_blocked(delta,100,true)==(delta>-100&&delta<100),"traffic spacing is symmetric and strict");
        check(ai_traffic_spawn_gap_blocked(delta,100,false)==(delta>0&&delta<100),"racer spacing protects only a spawn ahead");
    }
    {std::uint32_t random=123;float spacing=7;
     check(!ai_traffic_spawn_spacing(nullptr,0,false,random,spacing)&&random==123&&spacing==7,"invalid density cannot mutate state");}
    unsigned case_index=0;
    for(const auto& row:kNativeTrafficOwner) {
        OwnerWitness w{row,{}};auto s=state(row);const auto before=failures;
        check(ai_traffic_action_owner_step(ops,&w,s),"original traffic owner executes");
        const auto& a=s.current;
        check(int(a.handle)==row.result[0]&&a.kind==row.result[1]&&a.priority==row.result[2]&&
            int(a.status)==row.result[3]&&int(s.physical_selector)==row.result[4]&&
            a.timed==(a.handle==1&&row.input[1]!=0),"original retained/replaced action and physical selector");
        check(w.calls.size()==row.count,"original traffic call count");
        for(unsigned j=0;j<w.calls.size()&&j<row.count;++j)
            check(w.calls[j]==std::array<int,3>{row.calls[j][0],row.calls[j][1],row.calls[j][2]},
                "original candidate/predicate/cleanup/release/promotion/update order");
        if(before!=failures)std::cerr<<"owner case "<<case_index<<'\n';
        ++case_index;
    }
    for(const auto& row:kNativeTrafficGate) {
        bool full=false;check(ai_traffic_full_update(row[2],row[1],row[0],full)&&full==bool(row[3]),
            "actual role3 odd-frame group gate");
    }
    for(const auto& row:kNativeTrafficStrategy) {
        Queries w{row,{}};AiActionCandidate candidate;
        check(ai_traffic_strategy_choose(row.input[0],row.input[1],Queries::evaluate,&w,candidate),
            "original lazy traffic strategy executes");
        check(candidate.kind==row.kind&&candidate.priority==(row.kind==18?600:row.kind==3?300:0),
            "original traffic default/obstacle/avoidance selection");
        std::vector<int> expected;
        for(unsigned j=0;j<row.count;++j)if(row.calls[j]==18||row.calls[j]==3)expected.push_back(row.calls[j]);
        check(w.calls==expected,"original query order, same-kind skip and equal-priority admission");
    }
    {
        bool full=true;
        check(!ai_traffic_full_update(0,0,0,full)&&full,"empty group refuses without publication");
        check(!ai_traffic_full_update(0,2,2,full)&&full,"invalid slot refuses without publication");
        check(!ai_traffic_full_update(0,0,0x80000000U,full)&&full,"invalid signed period refuses");
        Queries w{kNativeTrafficStrategy[0],{}};w.refuse=true;AiActionCandidate c{9,99,77};
        check(!ai_traffic_strategy_choose(8,-1,Queries::evaluate,&w,c)&&c.kind==9&&c.priority==99&&c.target==77,
            "missing query prevents a fabricated default candidate");
        check(!ai_traffic_strategy_choose(8,-1,nullptr,&w,c),"absent geometry producer refuses");
    }
    {
        NativeTrafficOwnerCase row{};row.input[0]=4;row.input[2]=3;row.input[3]=300;row.input[4]=1;row.input[5]=2;
        OwnerWitness w{row,{}};auto s=state(row);w.cleanup_ok=false;
        check(!ai_traffic_action_owner_step(ops,&w,s)&&s.current.handle==1&&s.current.status==1&&
            w.calls.back()==std::array<int,3>{5,2,0},"failed cleanup releases candidate without publishing/promotion");
        w={row,{}};s=state(row);w.promote_ok=false;
        check(!ai_traffic_action_owner_step(ops,&w,s)&&s.current.handle==2&&s.physical_selector==2&&
            w.calls.back()[0]==7,"failed physical construction prevents path/speed update");
        w={row,{}};s=state(row);w.same_handle=true;
        check(!ai_traffic_action_owner_step(ops,&w,s)&&s.current.handle==1&&w.calls.back()[0]==3,
            "factory cannot alias current storage");
        w={row,{}};s=state(row);w.absent=true;
        check(!ai_traffic_action_owner_step(ops,&w,s)&&s.current.handle==1&&w.calls.back()[0]==3,
            "absent replacement is failure rather than racer fallback4");
        auto missing=ops;missing.choose=nullptr;w={row,{}};s=state(row);
        check(!ai_traffic_action_owner_step(missing,&w,s)&&w.calls.empty(),"missing callback refuses before side effects");
        w={row,{}};s=state(row);w.update_ok=false;
        check(!ai_traffic_action_owner_step(ops,&w,s)&&s.current.handle==2&&s.physical_selector==3&&
            w.calls.back()[0]==8,"failed path publication has no fallback4 retry");
    }
    auto value=[](unsigned word){float f;std::memcpy(&f,&word,4);return f;};
    for(const auto& c:kNativeTrafficStopOffer) {
        AiTrafficIntersectionRef ref{c.available!=0,c.available?unsigned(c.index):0};
        auto random=c.seed;AiTrafficStopCandidate result;
        check(ai_traffic_stop_offer(ref,random,result)&&result.priority==c.priority&&result.intersection==c.index&&
            result.mode==int(c.mode)&&random==c.random,"actual4543D0/454330 offer, priority and shared random draw");
    }
    for(const auto& c:kNativeTrafficStopComplete) {
        AiTrafficIntersectionRef ref{c.available!=0,unsigned(c.intersection)};auto random=c.seed;
        AiTrafficStopAction state{c.previous,c.mode,c.stopped!=0};bool complete=false;
        check(ai_traffic_stop_complete(ref,random,state,complete)&&complete==bool(c.complete)&&
            state.intersection==c.current&&state.mode==int(c.current_mode)&&state.stopped==bool(c.current_stopped)&&random==c.random,
            "actual454420 absent/retained/changed lifetime and stopped reset");
    }
    {
        AiTrafficIntersectionRef invalid{true,31};auto random=0x13572468U;AiTrafficStopCandidate c{3,3,7};
        check(!ai_traffic_stop_offer(invalid,random,c)&&random==0x13572468U&&c.priority==7&&c.intersection==3,
            "invalid producer cannot consume WORLD randomness or offer a synthetic crossing");
        AiTrafficStopAction state{3,3,true};bool complete=true;
        check(!ai_traffic_stop_complete(invalid,random,state,complete)&&random==0x13572468U&&state.intersection==3&&state.stopped&&complete,
            "invalid changed intersection cannot reset an active stop action");
    }
    unsigned stop_index=0;
    for(const auto& c:kNativeTrafficStop) {
        AiTrafficIntersection table;table.kind=c.kind;table.routes={c.first_route,c.second_route};table.selected=c.selected;
        AiTrafficStopInput in;in.route=c.route;in.distance=value(c.distance);in.absolute_speed=value(c.absolute);
        in.controller_cap=value(c.cap);in.obstacle={c.found?2U:0U,value(c.time),value(c.gap)};in.obstacle_admitted=c.admitted!=0;
        AiTrafficStopState state{c.stopped!=0};AiTrafficStopOutput out;
        const auto before=failures;
        check(ai_traffic_stop_update(in,table,state,out),"original stop-action control stage executes");
        check(bits(out.speed.speed)==c.speed&&bits(out.speed.duration)==c.duration&&bits(out.speed.auxiliary)==c.auxiliary,
            "actual454490 final cap, duration and overwritten auxiliary");
        check(!out.speed.route_speed&&out.speed.enabled&&!out.speed.catchup&&out.speed.ready,
            "actual stop-action path/speed publication flags");
        check(state.stopped==bool(c.stopped_after)&&table.selected==c.selected_after,"actual stopped latch and shared direction reservation");
        check(out.actor_override_enabled,"original actor override enabled");
        for(unsigned j=0;j<4;++j)check(bits(out.actor_override[j])==c.actor_override[j],"original unclassified four-float override");
        if(before!=failures)std::cerr<<"stop action case "<<stop_index<<'\n';
        ++stop_index;
    }
    {
        AiTrafficStopInput in;in.route=36;in.controller_cap=20;in.distance=10;
        in.absolute_speed=std::numeric_limits<float>::quiet_NaN();
        AiTrafficIntersection table;table.kind=1;table.routes={101,36};
        AiTrafficStopState state;AiTrafficStopOutput out;out.speed.speed=7;
        check(ai_traffic_stop_update(in,table,state,out)&&table.selected==1&&out.speed.speed==20,
            "unconsumed absolute speed does not reject an admitted crossing");
        table.selected=0;in.distance=-1;out.speed.speed=7;
        check(!ai_traffic_stop_update(in,table,state,out)&&table.selected==0&&!state.stopped&&out.speed.speed==7,
            "stopped latch consumes finite absolute speed and refuses transactionally");
        in.absolute_speed=0;in.obstacle={2,std::numeric_limits<float>::quiet_NaN(),10};
        check(ai_traffic_stop_update(in,table,state,out),"inadmitted obstacle fields are not consumed");
        in.obstacle_admitted=true;const auto selected=table.selected;const auto stopped=state.stopped;out.speed.speed=7;
        check(!ai_traffic_stop_update(in,table,state,out)&&table.selected==selected&&state.stopped==stopped&&out.speed.speed==7,
            "invalid admitted prediction cannot publish a successful clear road");
    }
    std::cout<<"Traffic owner/strategy/gate native comparisons, failures "<<failures<<'\n';
    return failures?1:0;
}
