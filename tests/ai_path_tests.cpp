#include "nfsu/ai_path.h"
#include "nfsu/ai_action.h"
#include "nfsu/ai_action_runtime.h"
#include "nfsu/ai_rival_action.h"
#include "nfsu/ai_collision_prediction.h"
#include "nfsu/ai_path_choice.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <limits>
using namespace nfsu;
namespace {
#include "ai_physical_obstacle_native.inc"
#include "motion_prediction_native.inc"
#include "human_world_path_native.inc"
#include "world_pair_native.inc"
#include "traffic_world_path_native.inc"
#include "path_query_selection_native.inc"
struct ChoiceRow {int kind;std::uint32_t target;bool flag;float score,gap;std::uint32_t obstacle;};
struct ChoiceCase {
    AiContactManeuver current;bool prior,available,backup;unsigned count;AiPathChoiceNeighbor neighbors[3];
    unsigned rows;ChoiceRow values[5];AiContactManeuver expected;bool side,found;float marker;unsigned calls;
};
const ChoiceCase choice_cases[]={
#include "ai_path_choice_native.inc"
};
struct SideCase {float self,target,distance,low,high;bool eligible;unsigned sides;};
const SideCase side_cases[]={
#include "ai_path_side_native.inc"
};
struct ChoiceContext {
    const ChoiceCase& c;unsigned calls=0;
    static bool evaluate(void* p,AiPathCandidate& out) {
        auto& s=*static_cast<ChoiceContext*>(p);out.score=-1;out.collision_time=99999;out.obstacle=0;
        for(unsigned i=0;i<s.c.rows;++i) {
            const auto& r=s.c.values[i];
            if(r.kind==out.descriptor.kind&&r.target==out.descriptor.target&&r.flag==out.descriptor.flag) {
                out.score=r.score;out.collision_time=r.gap;out.obstacle=r.obstacle;break;
            }
        }
        out.accepted=out.score> -1&&out.collision_time>4;
        out.geometry.path.path.count=1;out.geometry.path.path.points[0][0]=float(++s.calls);return true;
    }
    static bool available(void* p,const AiContactManeuver& d,bool& out) {
        out=static_cast<ChoiceContext*>(p)->c.backup&&d.kind>=1&&d.kind<=3;return true;
    }
    static bool fallback(void*,AiPlannedPath& out){out.path.count=1;out.path.points[0][0]=-99;return true;}
};
struct TargetChoiceCase {
    AiContactManeuver current;bool fast;unsigned sides;bool room;int accepted;
    AiContactManeuver expected;bool side,found;float marker;unsigned calls;ChoiceRow rows[4];
};
const TargetChoiceCase target_choice_cases[]={
#include "ai_target_path_native.inc"
};
struct TargetChoiceContext {
    const TargetChoiceCase& c;unsigned calls=0;bool order=true;
    static bool evaluate(void* opaque,AiPathCandidate& out) {
        auto& s=*static_cast<TargetChoiceContext*>(opaque);
        if(s.calls>=s.c.calls)return false;
        const auto& row=s.c.rows[s.calls];
        s.order=s.order&&out.descriptor.kind==row.kind&&out.descriptor.target==row.target;
        out.score=row.score;out.collision_time=row.gap;out.obstacle=row.obstacle;
        out.accepted=out.score> -1&&out.collision_time>2;
        out.geometry.path.path.count=1;out.geometry.path.path.points[0][0]=float(++s.calls);return true;
    }
    static bool available(void*,const AiContactManeuver&,bool& out){out=true;return true;}
    static bool room(void* opaque,const AiContactManeuver&,bool& out){out=static_cast<TargetChoiceContext*>(opaque)->c.room;return true;}
    static bool fallback(void*,const AiContactManeuver&,AiPlannedPath& out){out.path.count=1;out.path.points[0][0]=-99;return true;}
};
struct ShapeCase {
    unsigned count;AiDriverPoint points[20],low,high;unsigned heading;float scale;
    std::uint32_t expected[20][10];
};
const ShapeCase shape_cases[]={
#include "ai_prediction_shapes_native.inc"
};
struct WalkCase {
    unsigned a,b;std::array<float,2> pa,pb;float sa,sb;unsigned ia,ib;float acceleration,horizon;std::uint32_t expected;
};
const WalkCase walk_cases[]={
#include "ai_prediction_walk_native.inc"
};
struct OverlapCase {
    float axes_a[4],extent_a[2],axes_b[4],extent_b[2],position[2],velocity[2];std::uint32_t expected;
};
const OverlapCase overlap_cases[]={
#include "ai_collision_prediction_native.inc"
};
struct PredictionCase {
    unsigned count;AiDriverPoint position,velocity,acceleration;float horizon;
    unsigned first;std::uint32_t tick,cached_tick,length,after[3];
    AiDriverPoint points[20];std::uint32_t expected[20][3];
};
const PredictionCase prediction_cases[]={
#include "ai_path_prediction_native.inc"
};
struct FitnessCase {
    unsigned count;float position[3],velocity[3],speed;std::uint32_t tick;
    float params[6];unsigned transition_end,cached_cursor;std::uint32_t cached_tick,score,cursor,cursor_tick;
    float points[20][3];
};
const FitnessCase fitness_cases[]={
#include "ai_path_fitness_native.inc"
};
struct Road {float v[14];unsigned route,index;};
struct Point {std::uint32_t bits[3];unsigned route,index;};
struct Case {
    int category,mode;float config[5],actor[10];unsigned roads;Road road[20];
    unsigned count;std::uint32_t length;Point point[20];
};
const Case cases[]={
#include "ai_path_native.inc"
};
struct Retained {
    unsigned source;float speed,position[3],velocity[3],params[3];
    unsigned count;std::uint32_t length;Point point[20];
};
const Retained retained[]={
#include "ai_path_retained_native.inc"
};
struct Context {
    int category;std::uint32_t tick;unsigned selected;float offset,clearance,actor[10],params[3];
    unsigned roads;Road road[64];unsigned count;std::uint32_t length;Point point[20];AiActionOffsetInput policy{};float target=0;bool rebuild=false,category_available=true;
};
const Context contexts[]={
#include "ai_path_context_native.inc"
};
const Context action_contexts[]={
#include "ai_action_path_native.inc"
};
const Context ordinary_actions[]={
#include "ai_action_forward_native.inc"
};
const Context rival_defaults[]={
#include "ai_rival_default_native.inc"
};
struct Transition {
    int category,start,target;float from,to,half_width,margin;bool extra_margin;
    float actor[10];unsigned roads;Road road[64];
    unsigned count;std::uint32_t length;Point point[20];bool clipped;
};
const Transition transitions[]={
#include "ai_transition_native.inc"
};
struct ManeuverRoad {float v[18];unsigned route,index;};
struct Maneuver {
    int category,side;bool extra;int self_role,target_role;bool alternate;
    float margin,policy[8],actor[10],position[3],velocity[3];
    int self_cell,target_cell;float self_distance,target_distance,lateral;
    unsigned target_cursor,target_count;float target_points[20][3];
    unsigned roads;ManeuverRoad road[40];bool maneuver,clipped;unsigned end,count;
    std::uint32_t length;Point point[20];
};
const Maneuver maneuvers[]={
#include "ai_maneuver_native.inc"
};
struct DispatchPath {unsigned count;std::uint32_t length;Point point[20];};
struct Dispatch {
    unsigned source;bool rebuild,available;std::uint32_t tick;float params[3],speed;
    bool maneuver,clipped;unsigned end,count;std::uint32_t length;Point point[20];
    DispatchPath previous;
};
const Dispatch dispatches[]={
#include "ai_maneuver_dispatch_native.inc"
};
struct CycleNative {
    std::uint32_t tick;int category,kind,priority;unsigned status;bool timed;
    std::uint32_t epoch,duration,speed[3];bool flags[4],cleanup;
    unsigned count;std::uint32_t length;Point point[20];bool contact_update,fallback_update,missing_path;
};
#include "ai_action_cycle_native.inc"
struct CycleSelection {
    AiActionCreation next;
    unsigned calls=0;
    static bool select(void* p,int,const AiOwnedAction&,AiActionCreation& output) {
        auto& state=*static_cast<CycleSelection*>(p);++state.calls;output=state.next;return true;
    }
};
struct ContactGeometry {
    AiActionPathInput input;
    unsigned calls=0;
    static bool forward(void* pointer,const AiContactManeuver& descriptor,AiPlannedPath& path) {
        auto& binding=*static_cast<ContactGeometry*>(pointer);++binding.calls;
        AiManeuverResult result;
        if(!ai_action_plan_path(descriptor,binding.input,result))return false;
        path=result.path;return true;
    }
    static bool classify(void*,int& value){value=0;return true;}
};
float from_bits(std::uint32_t bits){float f;std::memcpy(&f,&bits,4);return f;}
std::uint32_t bits(float x){std::uint32_t v;std::memcpy(&v,&x,4);return v;}
unsigned failures=0;
void check(bool pass,const char* what){if(!pass){++failures;std::cerr<<"FAIL: "<<what<<'\n';}}
template<class T> bool same(const AiPlannedPath& p,const T& c) {
    if(p.path.count!=c.count||bits(p.length)!=c.length)return false;
    for(unsigned j=0;j<c.count;++j) {
        if(p.route[j]!=c.point[j].route||p.point[j]!=c.point[j].index)return false;
        for(unsigned k=0;k<3;++k)if(bits(p.path.points[j][k])!=c.point[j].bits[k])return false;
    }
    return true;
}
}
int main() {
    unsigned target_index=0;
    for(const auto& c:target_choice_cases) {
        AiTargetPathChoiceInput input;input.current=c.current;input.current_available=true;
        input.target=0x1140000;input.fast=c.fast;input.sides=c.sides;
        TargetChoiceContext context{c};AiPathChoiceResult result;
        const AiTargetPathChoiceOps ops{TargetChoiceContext::evaluate,TargetChoiceContext::available,TargetChoiceContext::room,TargetChoiceContext::fallback};
        const bool okay=ai_path_choose_target(input,ops,&context,result);
        const unsigned before=failures;
        check(okay&&context.order&&context.calls==c.calls&&result.found==c.found&&
            result.selected.descriptor.kind==c.expected.kind&&result.selected.descriptor.target==c.expected.target&&
            result.selected.descriptor.parameter==c.expected.parameter&&result.selected.side_admitted==c.side&&
            result.selected.geometry.path.path.points[0][0]==c.marker,"original following target choice and order");
        if(failures>before&&target_index<40)std::cerr<<"TARGET "<<target_index<<" okay "<<okay<<" order "<<context.order<<" calls "<<context.calls<<"/"<<c.calls<<" kind "<<result.selected.descriptor.kind<<"/"<<c.expected.kind<<" offset "<<result.selected.descriptor.parameter<<"/"<<c.expected.parameter<<" found "<<result.found<<"/"<<c.found<<" side "<<result.selected.side_admitted<<"/"<<c.side<<" marker "<<result.selected.geometry.path.path.points[0][0]<<"/"<<c.marker<<'\n';
        ++target_index;
    }

    {
        struct Tail {float signed_speed,absolute_speed;AiDriverPoint position,velocity;
            unsigned count;std::uint32_t score;std::array<AiDriverPoint,25> points;};
        const Tail cases[]={
#include "ai_fitness_tail_native.inc"
        };
        for(const auto& c:cases) {
            AiPlannedPath path;path.path.count=static_cast<std::uint8_t>(c.count);path.path.points=c.points;
            AiPathFitnessInput in{c.position,c.velocity,c.signed_speed,c.absolute_speed,22666,{16,0,0},.4F,.5F,30,255};
            float score=-1;
            check(ai_path_fitness(path,in,score)&&bits(score)==c.score,"original tail score independent of speculative memory");
        }
    }

    unsigned maneuver_index=0,moving_witnesses=0,contact_witnesses=0;
    for(const auto& c:maneuvers) {
        AiPathExtension ex;ex.category=c.category;ex.margin=c.margin;ex.absolute_speed=c.actor[9];
        AiManeuverInput in;in.side=c.side;in.extra_clearance=c.extra;
        in.self.cell=c.self_cell;in.target.cell=c.target_cell;in.self.role=c.self_role;
        in.self.distance=c.self_distance;in.target.distance=c.target_distance;
        in.self.half_width=1;in.target.half_width=1;in.target.lateral=c.lateral;
        in.target.role=c.target_role;in.target.racer=true;in.target.alternate_controller=c.alternate;
        in.self_signed_speed=c.actor[9];in.target_cursor=c.target_cursor;
        in.target_path.path.count=c.target_count;
        for(unsigned k=0;k<8;++k)in.policy[k]=c.policy[k];
        for(unsigned k=0;k<3;++k) {
            ex.actor_position[k]=c.actor[k];ex.actor_velocity[k]=c.actor[k+3];ex.actor_forward[k]=c.actor[k+6];
            in.self.position[k]=c.actor[k];in.self.velocity[k]=c.actor[k+3];
            in.target.position[k]=c.position[k];in.target.velocity[k]=c.velocity[k];
            for(unsigned j=0;j<c.target_count;++j)in.target_path.path.points[j][k]=c.target_points[j][k];
        }
        AiPathRoadPoint roads[40];const int index=c.category<5?0:c.category-10;
        for(unsigned j=0;j<c.roads;++j) {
            const auto& r=c.road[j];auto& road=roads[j];auto& p=road.point;
            p.position[0]=r.v[0];p.position[1]=r.v[1];p.height=r.v[2];p.forward[0]=r.v[3];p.forward[1]=r.v[4];
            p.unknown_2c=r.v[5];p.unknown_30=r.v[6];
            (c.category<5?p.driving_offset[c.category]:p.lane_offset[c.category-11])=r.v[7];
            road.offset[index]=r.v[8];road.lower[index]=r.v[9];road.upper[index]=r.v[10];
            p.unknown_24=r.v[11];p.unknown_28=r.v[12];road.distance=r.v[13];
            p.driving_offset[0]=r.v[14];road.offset[0]=r.v[15];road.lower[0]=r.v[16];road.upper[0]=r.v[17];
            p.route_index=static_cast<std::uint8_t>(r.route);road.point_index=static_cast<std::uint16_t>(r.index);
        }
        AiManeuverResult output;
        const bool ok=ai_path_maneuver(roads,c.roads,ex,in,output);
        if(!ok||!same(output.path,c)||output.clipped!=c.clipped||output.maneuver!=c.maneuver||output.transition_end!=c.end) {
            ++failures;std::cerr<<"FAIL: original complete maneuver "<<maneuver_index<<" ok "<<ok
                <<" count "<<unsigned(output.path.path.count)<<'/'<<c.count<<" length "<<bits(output.path.length)<<'/'<<c.length
                <<" chosen "<<output.maneuver<<'/'<<c.maneuver<<" clip "<<output.clipped<<'/'<<c.clipped
                <<" end "<<unsigned(output.transition_end)<<'/'<<c.end<<'\n';
            for(unsigned j=0;j<std::min<unsigned>(output.path.path.count,c.count);++j)for(unsigned k=0;k<3;++k)
                if(bits(output.path.path.points[j][k])!=c.point[j].bits[k]) {
                    std::cerr<<" first point mismatch "<<j<<','<<k<<' '<<bits(output.path.path.points[j][k])<<'/'<<c.point[j].bits[k]<<'\n';j=c.count;break;
                }
        }
        if(c.maneuver) {
            auto no_path=in;no_path.target_path.path.count=0;AiManeuverResult omitted;
            check(ai_path_maneuver(roads,c.roads,ex,no_path,omitted),"target path omission measurement completes");
            if(!same(omitted.path,c))++moving_witnesses;
        }
        auto bad=in;bad.target.cell=-1;const auto saved=output;
        check(!ai_path_maneuver(roads,c.roads,ex,bad,output)&&
              output.path.path.count==saved.path.path.count&&bits(output.path.length)==bits(saved.path.length),
              "missing target cell refuses without partial publication");
        if(c.maneuver&&c.category==0&&c.target_role==1&&!c.extra&&c.lateral==0&&c.velocity[0]!=0&&!c.alternate) {
            auto wrong=in;wrong.side=wrong.side==2?3:2;AiManeuverResult opposite;
            check(ai_path_maneuver(roads,c.roads,ex,wrong,opposite)&&!same(opposite.path,c),
                  "wrong side changes complete geometry");
        }
        for(const auto& d:dispatches)if(d.source==maneuver_index) {
            AiActionPathInput plan;
            plan.roads=roads;plan.road_count=c.roads;plan.selected=c.self_cell;
            plan.extension=ex;plan.extension.mode=2;plan.extension.clearance=c.margin;
            plan.policy.mode=2;plan.policy.absolute_speed=ex.absolute_speed;
            plan.signed_speed=d.speed;plan.tick=d.tick;
            plan.rebuild=d.rebuild;plan.category_available=d.available;
            plan.target=&in;plan.resolved_target=42;
            for(unsigned j=0;j<3;++j)plan.retain_parameters[j]=d.params[j];
            plan.previous.path.path.count=d.previous.count;
            plan.previous.path.length=from_bits(d.previous.length);
            for(unsigned j=0;j<d.previous.count;++j) {
                for(unsigned k=0;k<3;++k)plan.previous.path.path.points[j][k]=from_bits(d.previous.point[j].bits[k]);
                plan.previous.path.route[j]=d.previous.point[j].route;
                plan.previous.path.point[j]=d.previous.point[j].index;
            }
            AiManeuverResult planned;const AiContactManeuver descriptor{c.side,42,0,c.extra};
            const bool dispatched=ai_action_plan_path(descriptor,plan,planned);
            if(!dispatched||!same(planned.path,d)||planned.maneuver!=d.maneuver||
               planned.clipped!=d.clipped||planned.transition_end!=d.end) {
                ++failures;std::cerr<<"FAIL: native side action dispatch "<<maneuver_index
                    <<" rebuild "<<d.rebuild<<" count "<<unsigned(planned.path.path.count)<<'/'<<d.count
                    <<" end "<<unsigned(planned.transition_end)<<'/'<<d.end<<'\n';
            }
            if(!c.extra&&c.side==(c.lateral>0?2:3)) {
                AiNeighborhood neighbors;neighbors.count=2;neighbors.self_index=0;
                neighbors.entries[0].id=7;neighbors.entries[1].id=42;
                neighbors.entries[0].distance=c.self_distance;
                neighbors.entries[1].distance=c.target_distance;
                neighbors.entries[0].half_extent={1,2};neighbors.entries[1].half_extent={1,2};
                neighbors.entries[1].lateral=c.lateral;
                for(auto& value:neighbors.entries[1].lane_lateral)value=c.lateral;
                AiContactActionInput contact;contact.neighbors=&neighbors;contact.category=c.category;
                contact.self_role=c.self_role;contact.target_human=c.target_role==1;
                contact.target_velocity=in.target.velocity;contact.controller_speed=ex.absolute_speed;
                contact.signed_speed=d.speed;
                AiContactActionState state;state.target=42;
                ContactGeometry binding{plan};
                const AiContactActionOps ops{ContactGeometry::forward,nullptr,ContactGeometry::classify};
                check(ai_action_contact_update(contact,ops,&binding,state)&&binding.calls==1&&
                      same(state.path,d)&&state.speed.ready,"contact action uses real original path dispatch");
                if(c.category==0&&c.target_role==1&&c.lateral==0&&c.velocity[0]!=0&&
                   !c.alternate&&d.rebuild) {
                    AiActionRuntime runtime;
                    check(runtime.initialize({{4,100,0},-1,true},cycle_native[0].tick),"initialize real fallback storage");
                    std::array<AiStrategyActor,16> metadata{};
                    metadata[0]={true,false,ex.absolute_speed};metadata[1]={true,true,20};
                    AiStrategyContact hit{1,0,0,7,42};
                    AiActionRuntimeInput frame;frame.tick=d.tick;frame.neighbors=&neighbors;frame.actors=&metadata;
                    frame.contacts=&hit;frame.contact_count=1;frame.path=plan;frame.fallback_path=plan;
                    frame.contact=contact;frame.selected_road=&roads[c.self_cell].point;
                    frame.recovery_motion.position={ex.actor_position[0],ex.actor_position[1]};
                    frame.recovery_motion.prediction_vector={ex.actor_velocity[0],ex.actor_velocity[1]};
                    frame.recovery_motion.body_direction={ex.actor_forward[0],ex.actor_forward[1]};
                    frame.recovery_motion.signed_speed=d.speed;frame.recovery_motion.absolute_speed=ex.absolute_speed;
                    for(unsigned j=0;j<c.roads;++j)for(unsigned k=0;k<5;++k)roads[j].point.driving_offset[k]=cycle_lanes[j][k];
                    frame.fallback_path.extension.category=cycle_native[2].category;
                    frame.contact.duration=cycle_config[0];frame.contact.human_speed_offset=cycle_config[1];
                    frame.contact.other_speed_offset=cycle_config[2];frame.contact.gap_margin=cycle_config[3];
                    frame.contact.minimum_speed=cycle_config[4];frame.contact.controller_speed=cycle_config[5];
                    frame.fallback_gap=cycle_config[6];frame.fallback_desired_gap=cycle_config[7];
                    CycleSelection selection{{{12,400,42},-1,true}};
                    frame.select=CycleSelection::select;frame.selection_context=&selection;
                    AiActionRuntimeOutput output;std::uint32_t contact_handle=0;
                    {
                        AiRivalActionController connected;
                        AiPathPlannerInput planner;planner.path=frame.path;planner.path.tick=cycle_native[0].tick;
                        planner.target_count=1;planner.target_ids[0]=42;planner.targets[0]=in;
                        planner.target_available[0]=plan.category_available;planner.choice.horizon=2;
                        planner.side_score_scale=1.2F;planner.fallback_margin=1;
                        AiRoadChain chain;chain.count=c.roads;
                        for(unsigned j=0;j<c.roads;++j)chain.cells[j]=roads[j];
                        AiPathCollisionWorld world;world.count=2;world.tick=cycle_native[0].tick;
                        world.actors[0].id=7;world.actors[0].role=2;
                        world.actors[0].position=ex.actor_position;world.actors[0].velocity=ex.actor_velocity;
                        world.actors[0].forward=ex.actor_forward;world.actors[0].signed_speed=c.actor[9];
                        world.actors[0].absolute_speed=c.actor[9];
                        AiRecoveryInput recovery;recovery.road=frame.selected_road;recovery.motion=frame.recovery_motion;
                        recovery.tick=world.tick;recovery.supported=true;
                        std::uint32_t random=12345;AiRivalActionInput original_input;
                        original_input.tick=world.tick;original_input.random=&random;original_input.planner=&planner;
                        original_input.world=&world;original_input.neighbors=&neighbors;original_input.road=&chain;
                        original_input.actors=metadata;original_input.motion[0]={7,true,2,ex.actor_velocity};
                        original_input.motion[1]={42,true,1,in.target.velocity};
                        original_input.contacts=&hit;original_input.contact_count=1;original_input.ride_category=0;
                        original_input.recovery=&recovery;original_input.slowdown=contact.slowdown;
                        AiRivalActionOutput result;const auto& expected=cycle_native[0];
                        const bool admitted=connected.step(original_input,result);
                        check(admitted&&result.action.kind==expected.kind&&result.action.priority==expected.priority&&
                            same(result.path,expected)&&bits(result.speed.speed)==expected.speed[0]&&
                            bits(result.speed.duration)==expected.speed[1]&&bits(result.speed.auxiliary)==expected.speed[2]&&
                            result.speed.route_speed==expected.flags[0]&&result.speed.enabled==expected.flags[1],
                            "native contact selected by connected strategy and action owner");
                    }
                    for(unsigned phase=0;phase<std::size(cycle_native);++phase) {
                        const auto& expected=cycle_native[phase];frame.tick=expected.tick;
                        frame.path.tick=frame.tick;frame.fallback_path.tick=frame.tick;
                        selection.next=phase==0?AiActionCreation{{12,400,42},-1,true}:AiActionCreation{{4,100,0},-1,true};
                        if(phase==2) {
                            frame.contact_count=0;neighbors.count=1;frame.path.target=nullptr;
                        }
                        const bool ok=runtime.step(frame,output);
                        bool equal=ok&&same(output.path,expected)&&output.action.kind==expected.kind&&
                            output.category==expected.category&&output.action.priority==expected.priority&&output.action.status==expected.status&&
                            output.action.timed==expected.timed&&output.created_tick==expected.epoch&&
                            bits(output.duration)==expected.duration&&output.clear_actor_recovery==expected.cleanup&&
                            bits(output.speed.speed)==expected.speed[0]&&bits(output.speed.duration)==expected.speed[1]&&
                            bits(output.speed.auxiliary)==expected.speed[2]&&output.speed.route_speed==expected.flags[0]&&
                            output.speed.enabled==expected.flags[1]&&output.speed.catchup==expected.flags[2]&&
                            output.speed.ready==expected.flags[3]&&output.path_updates==1&&output.constructions==1;
                        if(!equal)std::cerr<<"cycle phase "<<phase<<" success "<<ok<<" kind "<<output.action.kind
                            <<" points "<<unsigned(output.path.path.count)<<'/'<<expected.count<<" speed "
                            <<bits(output.speed.speed)<<'/'<<expected.speed[0]<<" duration "<<bits(output.speed.duration)<<'/'<<expected.speed[1]<<'\n';
                        if(!equal) {
                            std::cerr<<"epoch "<<output.created_tick<<'/'<<expected.epoch<<" length "<<bits(output.path.length)<<'/'<<expected.length
                                <<" flags "<<output.speed.route_speed<<output.speed.enabled<<output.speed.catchup<<output.speed.ready
                                <<" cleanup "<<output.clear_actor_recovery<<'/'<<expected.cleanup<<'\n';
                            for(unsigned j=0;j<output.path.path.count;++j)for(unsigned k=0;k<3;++k)
                                if(bits(output.path.path.points[j][k])!=expected.point[j].bits[k])
                                    std::cerr<<"point "<<j<<','<<k<<' '<<bits(output.path.path.points[j][k])<<'/'<<expected.point[j].bits[k]<<'\n';
                        }
                        check(equal,"connected original ownership, constructor, path and speed outputs");
                        if(phase==0)contact_handle=output.action.handle;
                        if(phase==1)check(output.action.handle==contact_handle,"lower priority candidate discarded without replacing current");
                        if(phase==2)check(!expected.missing_path&&!expected.contact_update&&expected.fallback_update,
                            "native lost contact never reaches stale target update");
                        frame.path.previous.path=output.path;frame.fallback_path.previous.path=output.path;
                    }
                    check(!runtime.failed()&&selection.calls==3,"continuous ownership cycle remains usable");
                    const auto saved_output=output;selection.next={{7,0,0},-1,true};
                    check(!runtime.step(frame,output)&&runtime.failed()&&output.action.handle==saved_output.action.handle,
                          "unsupported action refuses instead of inventing behavior");
                    neighbors.count=2;
                }
                ++contact_witnesses;
            }
            if(d.rebuild&&d.available) {
                auto mismatched=plan;mismatched.resolved_target=43;const auto saved=planned;
                check(!ai_action_plan_path(descriptor,mismatched,planned)&&same(planned.path,d)&&
                      planned.maneuver==saved.maneuver,"mismatched target identity refuses publication");
            }
        }
        ++maneuver_index;
    }
    check(moving_witnesses>0,"moving target continuation affects a native witness");
    std::cout<<"Contact geometry witnesses: "<<contact_witnesses<<'\n';
    std::cout<<"Moving target witnesses: "<<moving_witnesses<<'\n';
    unsigned transition_index=0;
    for(const auto& c:transitions) {
        AiPathExtension in;in.category=c.category;in.margin=c.margin;in.absolute_speed=c.actor[9];
        for(unsigned k=0;k<3;++k) {
            in.actor_position[k]=c.actor[k];in.actor_velocity[k]=c.actor[k+3];in.actor_forward[k]=c.actor[k+6];
        }
        AiPathRoadPoint roads[64];const int slot=c.category<5?0:c.category-10;
        for(unsigned j=0;j<c.roads;++j) {
            const auto& r=c.road[j];auto& road=roads[j];auto& p=road.point;
            p.position[0]=r.v[0];p.position[1]=r.v[1];p.height=r.v[2];p.forward[0]=r.v[3];p.forward[1]=r.v[4];
            p.unknown_2c=r.v[5];p.unknown_30=r.v[6];
            (c.category<5?p.driving_offset[c.category]:p.lane_offset[c.category-11])=r.v[7];
            road.offset[slot]=r.v[8];road.lower[slot]=r.v[9];road.upper[slot]=r.v[10];road.distance=r.v[13];
            p.route_index=static_cast<std::uint8_t>(r.route);road.point_index=static_cast<std::uint16_t>(r.index);
        }
        AiPlannedPath path;bool clipped=false;
        const bool ok=ai_path_transition(roads,c.roads,c.start,c.target,c.from,c.to,c.half_width,c.extra_margin,in,path,clipped);
        if(!ok||!same(path,c)||clipped!=c.clipped) {
            ++failures;std::cerr<<"FAIL: maneuver transition "<<transition_index<<" count "<<path.path.count
                <<" expected "<<c.count<<" length "<<bits(path.length)<<" expected "<<c.length
                <<" clipped "<<clipped<<" expected "<<c.clipped<<'\n';
        }
        // Removing the requested side change must affect emitted geometry.
        if(c.start==1&&c.target==8&&c.from==0&&c.to==3) {
            AiPlannedPath straight;bool unused=false;
            check(ai_path_transition(roads,c.roads,c.start,c.target,0,0,c.half_width,c.extra_margin,in,straight,unused)&&
                  !same(straight,c),"side-change witness detects omitted lateral maneuver");
        }
        ++transition_index;
    }
    AiPlannedPath native[sizeof(cases)/sizeof(cases[0])];unsigned case_index=0;
    for(const auto& c:cases) {
        AiPathExtension in;in.category=c.category;in.mode=c.mode;
        in.offset=c.config[0];in.offset_step=c.config[1];in.target_offset=c.config[2];
        in.clearance=c.config[3];in.margin=c.config[4];
        for(unsigned k=0;k<3;++k) {
            in.actor_position[k]=c.actor[k];in.actor_velocity[k]=c.actor[k+3];
            in.actor_forward[k]=c.actor[k+6];
        }
        in.absolute_speed=c.actor[9];
        AiPathRoadPoint roads[20];
        const int index=c.category<5?0:c.category-10;
        for(unsigned j=0;j<c.roads;++j) {
            const auto& r=c.road[j];auto& road=roads[j];auto& p=road.point;
            p.position[0]=r.v[0];p.position[1]=r.v[1];p.height=r.v[2];
            p.forward[0]=r.v[3];p.forward[1]=r.v[4];
            p.unknown_2c=r.v[5];p.unknown_30=r.v[6];
            p.unknown_24=r.v[11];p.unknown_28=r.v[12];
            (c.category<5?p.driving_offset[c.category]:p.lane_offset[c.category-11])=r.v[7];
            road.offset[index]=r.v[8];road.lower[index]=r.v[9];road.upper[index]=r.v[10];
            p.route_index=static_cast<std::uint8_t>(r.route);road.point_index=static_cast<std::uint16_t>(r.index);
        }
        for(unsigned j=0;j<c.roads;++j) {
            auto bounded=roads[j];
            check(ai_path_road_bounds(bounded,c.category)&&bits(bounded.lower[index])==bits(roads[j].lower[index])&&
                  bits(bounded.upper[index])==bits(roads[j].upper[index]),"original road cell bounds from raw widths and lane");
        }
        AiPlannedPath path;
        check(ai_path_extend(roads,c.roads,in,path)&&same(path,c),"original forward path coordinates, IDs and length");
        native[case_index++]=path;
        for(unsigned j=0;j<path.path.count;++j)check(path.category[j]==c.category,"path category retained");
        auto invalid=in;invalid.category=7;
        check(!ai_path_extend(roads,c.roads,invalid,path)&&same(path,c),"unsupported category is transactional");
        const auto saved=roads[1].point.height;
        roads[1].point.height=std::numeric_limits<float>::quiet_NaN();
        AiPlannedPath empty;
        check(!ai_path_extend(roads,c.roads,in,empty)&&empty.path.count==0,"bad later road refuses partial path");
        roads[1].point.height=saved;
        // Removing actual authored lateral offsets must change this witness.
        for(unsigned j=0;j<c.roads;++j)
            (c.category<5?roads[j].point.driving_offset[c.category]:roads[j].point.lane_offset[c.category-11])=0;
        check(ai_path_extend(roads,c.roads,in,empty)&&!same(empty,c),"authored lanes are observable");
    }
    for(const auto& r:retained) {
        auto path=native[r.source];
        const AiDriverPoint position{r.position[0],r.position[1],r.position[2]};
        const AiDriverPoint velocity{r.velocity[0],r.velocity[1],r.velocity[2]};
        check(ai_path_retain(path,r.speed,position,velocity,12345,r.params[0],r.params[1],r.params[2])&&same(path,r),
              "original moving cursor, trimming and 3D retained length");
        check(path.cursor_tick==0,"trimming invalidates selection cache");
    }
    auto continuation=[&](const Context* data,std::size_t count,bool action_policy,bool ordinary=false) {
    AiPlannedPath continuing;
    for(std::size_t i=0;i<count;++i) {
        const auto& c=data[i];
        AiPathRoadPoint roads[64];
        for(unsigned j=0;j<c.roads;++j) {
            const auto& r=c.road[j];auto& road=roads[j];auto& p=road.point;
            p.position[0]=r.v[0];p.position[1]=r.v[1];p.height=r.v[2];
            p.forward[0]=r.v[3];p.forward[1]=r.v[4];p.unknown_2c=r.v[5];p.unknown_30=r.v[6];
            p.driving_offset[c.category]=r.v[7];road.offset[0]=r.v[8];
            p.unknown_24=r.v[11];p.unknown_28=r.v[12];road.distance=r.v[13];
            p.route_index=static_cast<std::uint8_t>(r.route);road.point_index=static_cast<std::uint16_t>(r.index);
            check(ai_path_road_bounds(road,c.category),"build context bounds");
        }
        AiPathExtension in;in.category=c.category;in.offset_step=1.25F;in.clearance=c.clearance;
        if(action_policy){in.mode=c.policy.mode;in.target_offset=c.target;}
        for(unsigned k=0;k<3;++k){in.actor_position[k]=c.actor[k];in.actor_velocity[k]=c.actor[k+3];in.actor_forward[k]=c.actor[k+6];}
        in.absolute_speed=c.actor[9];
        if(!ordinary)check(ai_path_retain(continuing,20,in.actor_position,in.actor_velocity,c.tick,c.params[0],c.params[1],c.params[2]),
              "retain moving context");
        bool ok=false;
        if(ordinary) {
            AiActionPathInput plan;plan.previous.path=continuing;plan.roads=roads;plan.road_count=c.roads;
            plan.selected=c.selected;plan.initial_offset=c.offset;plan.policy=c.policy;plan.extension=in;
            plan.signed_speed=20;plan.tick=c.tick;plan.retain_parameters={c.params[0],c.params[1],c.params[2]};
            plan.rebuild=c.rebuild;plan.category_available=c.category_available;
            AiManeuverResult output;
            ok=ai_action_plan_path({1,0,c.target,false},plan,output);
            if(i==0) {
                AiPathCollisionWorld world;world.count=4;world.tick=c.tick;
                std::array<AiPathPlannerInput,3> inputs;
                for(unsigned car=0;car<3;++car){
                    world.actors[car].id=car+1;world.actors[car].role=2;
                    auto& p=inputs[car];p.path=plan;p.choice.current={1,0,c.target,false};
                    p.choice.current_available=true;p.choice.horizon=4;p.side_score_scale=1.2F;p.fallback_margin=1;
                    p.fitness={in.actor_position,in.actor_velocity,20,in.absolute_speed,c.tick,{16,0,0},.4F,.5F,30,255};
                }
                std::array<AiPathChoiceResult,3> chosen;unsigned queries=123;
                check(ai_plan_three_paths(inputs,world,{0,1,2},{},{},chosen,queries)&&queries==0,
                      "three real geometry/fitness choices consume one frozen epoch");
                for(const auto& choice:chosen)check(choice.found&&same(choice.selected.geometry.path,c),
                      "batch retains the original ordinary geometry for every actor");
                for(auto& choice:chosen)choice.evaluations=77;
                ++inputs[2].path.tick;queries=123;
                check(!ai_plan_three_paths(inputs,world,{0,1,2},{},{},chosen,queries)&&queries==123&&
                      chosen[0].evaluations==77&&chosen[1].evaluations==77&&chosen[2].evaluations==77,
                      "stale third actor refuses entire batch without partial publication");
            }
            if(ok)continuing=output.path;
        } else ok=action_policy?ai_action_continue(roads,c.roads,c.selected,c.offset,c.policy,in,continuing):
            ai_path_continue(roads,c.roads,c.selected,c.offset,in,continuing);
        if(!ok||!same(continuing,c))std::cerr<<"context tick "<<c.tick<<" got "<<unsigned(continuing.path.count)<<" expected "<<c.count<<" length "<<std::hex<<bits(continuing.length)<<"/"<<c.length<<std::dec<<'\n';
        check(ok&&same(continuing,c),"original continuation across moving road contexts");
    }};
    continuation(contexts,std::size(contexts),false);
    continuation(action_contexts,std::size(action_contexts),true);
    continuation(ordinary_actions,std::size(ordinary_actions),true,true);
    {
        // Real450210 outputs across moving supplied poses and five distinct
        // ride categories. Exercise the connected owner, not just45AE10.
        std::array<AiRivalActionController,5> controllers;
        std::array<AiPlannedPath,5> publications;
        std::array<std::uint32_t,5> handles{};
        for(const auto& c:rival_defaults) {
            AiRoadChain road;road.count=c.roads;
            for(unsigned j=0;j<c.roads;++j) {
                const auto& data=c.road[j];auto& cell=road.cells[j];auto& point=cell.point;
                point.position[0]=data.v[0];point.position[1]=data.v[1];point.height=data.v[2];
                point.forward[0]=data.v[3];point.forward[1]=data.v[4];point.unknown_2c=data.v[5];point.unknown_30=data.v[6];
                point.driving_offset[c.category]=data.v[7];cell.offset[0]=data.v[8];
                point.unknown_24=data.v[11];point.unknown_28=data.v[12];cell.distance=data.v[13];
                point.route_index=std::uint8_t(data.route);cell.point_index=std::uint16_t(data.index);
                check(ai_path_road_bounds(cell,c.category),"default-action native bounds");
            }
            AiPathPlannerInput planner;auto& plan=planner.path;
            plan.roads=road.cells.data();plan.road_count=road.count;plan.selected=c.selected;
            plan.initial_offset=c.offset;plan.policy=c.policy;plan.extension.category=c.category;
            plan.extension.clearance=c.clearance;plan.extension.absolute_speed=c.actor[9];
            for(unsigned k=0;k<3;++k) {
                plan.extension.actor_position[k]=c.actor[k];plan.extension.actor_velocity[k]=c.actor[k+3];
                plan.extension.actor_forward[k]=c.actor[k+6];
            }
            plan.signed_speed=20;plan.tick=c.tick;plan.retain_parameters={c.params[0],c.params[1],c.params[2]};
            plan.rebuild=ai_strategy_rebuild_path(AiStrategyKind::ordinary,false);
            plan.category_available=c.category_available;plan.previous.path=publications[c.category];
            planner.choice.horizon=2.F;planner.side_score_scale=1.2F;planner.fallback_margin=1;
            planner.fitness={plan.extension.actor_position,plan.extension.actor_velocity,20,c.actor[9],c.tick,
                plan.retain_parameters,.4F,.5F,30,255};
            AiPathCollisionWorld world;world.count=1;world.tick=c.tick;world.retain_parameters=plan.retain_parameters;
            auto& self=world.actors[0];self.id=1;self.role=2;self.path=publications[c.category];
            self.position=plan.extension.actor_position;self.velocity=plan.extension.actor_velocity;
            self.forward=plan.extension.actor_forward;self.signed_speed=20;self.absolute_speed=c.actor[9];
            AiNeighborhood neighbors;neighbors.count=1;neighbors.self_index=0;neighbors.entries[0].id=1;
            neighbors.entries[0].cell=c.selected;neighbors.entries[0].half_extent={c.clearance,2};
            std::uint32_t random=12345;AiRivalActionInput input;
            input.planner=&planner;input.world=&world;input.neighbors=&neighbors;input.road=&road;
            input.tick=c.tick;input.random=&random;input.ride_category=c.category;input.race_seconds=c.policy.race_seconds;
            input.actors[0]={true,false,c.actor[9]};input.motion[0]={1,true,2,self.velocity};
            AiRivalActionOutput result;
            const bool okay=controllers[c.category].step(input,result);
            check(okay&&result.action.kind==7&&result.action.priority==0&&result.speed.route_speed&&
                result.speed.enabled&&result.speed.ready&&same(result.path,c),"native default action and per-car lane geometry");
            if(!handles[c.category])handles[c.category]=result.action.handle;
            check(result.action.handle==handles[c.category],"equal-priority candidate does not replace retained action");
            if(okay)publications[c.category]=result.path;
        }
        check(!ai_strategy_rebuild_path(AiStrategyKind::scripted_human,false)&&
            ai_strategy_rebuild_path(AiStrategyKind::scripted_human,true)&&
            ai_strategy_rebuild_path(AiStrategyKind::drag,false),"original strategy virtual and controller dirty request");
    }
    {
        AiPlannedPath empty;empty.cursor_tick=55;empty.cursor=3;
        AiPathFitnessInput in;in.tick=66;float score=123;
        check(ai_path_fitness(empty,in,score)&&score==-1&&empty.cursor_tick==55&&empty.cursor==3,
              "empty fitness preserves original untouched cursor cache");
        AiPlannedPath short_path;short_path.path.count=2;
        short_path.path.points[0]={1,0,0};short_path.path.points[1]={2,0,0};
        in.signed_speed=in.absolute_speed=70;in.velocity={70,0,0};
        in.minimum_time=.4F;in.initial_time=.5F;in.lateral_acceleration_limit=30;
        score=123;
        check(ai_path_fitness(short_path,in,score)&&score==in.absolute_speed,
              "short original lookahead exits acceleration and retains weighted speed");
    }
    unsigned choice_index=0;
    for(const auto& c:choice_cases) {
        AiPathChoiceInput in;in.current=c.current;in.current_available=c.available;in.try_current=c.prior;in.horizon=4;in.count=c.count;
        for(unsigned i=0;i<c.count;++i)in.neighbors[i]=c.neighbors[i];
        ChoiceContext context{c};AiPathChoiceResult out;
        const AiPathChoiceOps ops{ChoiceContext::evaluate,ChoiceContext::available,ChoiceContext::fallback};
        const bool ok=ai_path_choose(in,ops,&context,out);
        const auto& d=out.selected.descriptor;
        const bool match=ok&&d.kind==c.expected.kind&&d.target==c.expected.target&&d.parameter==c.expected.parameter&&d.flag==c.expected.flag&&
            out.selected.side_admitted==c.side&&out.found==c.found&&out.selected.geometry.path.path.points[0][0]==c.marker&&context.calls==c.calls;
        if(!match)std::cerr<<"choice "<<choice_index<<" kind "<<d.kind<<'/'<<c.expected.kind<<" side "<<out.selected.side_admitted<<'/'<<c.side
            <<" found "<<out.found<<'/'<<c.found<<" marker "<<out.selected.geometry.path.path.points[0][0]<<'/'<<c.marker<<" calls "<<context.calls<<'/'<<c.calls<<'\n';
        check(match,"original candidate order, score ties, backup and fallback");++choice_index;
    }
    std::cout<<"Original choice witnesses (supplied query results): "<<choice_index<<'\n';
    for(const auto& c:side_cases) {
        AiRoadChain chain;chain.count=3;
        for(unsigned i=0;i<3;++i){auto& r=chain.cells[i];r.point.position[0]=float(i)*10;r.point.forward[0]=1;r.lower[0]=c.low;r.upper[0]=c.high;}
        AiNeighborhood neighbors;neighbors.count=2;neighbors.self_index=0;
        auto& a=neighbors.entries[0];auto& b=neighbors.entries[1];a.id=1;b.id=2;
        a.half_extent=b.half_extent={1,2};a.lateral=c.self;b.lateral=c.target;b.distance=c.distance;
        bool eligible=false;unsigned sides=0;
        check(ai_path_neighbor_policy(chain,neighbors,1,2,{0,5,1,2,50,5,.12F},eligible,sides)&&
              eligible==c.eligible&&sides==c.sides,"original side admission and strict eligibility boundary");
    }
    std::cout<<"Original side/eligibility witnesses: "<<std::size(side_cases)<<'\n';
    {
        AiPathCollisionWorld world;world.count=2;world.self=0;world.eligible_count=1;world.eligible[0]=1;
        world.tick=1;world.retain_parameters={16,0,0};world.prediction_horizon=.75F;
        world.normal_scale=world.human_scale=world.slow_scale=1;world.slow_speed=2;
        auto& a=world.actors[0];auto& b=world.actors[1];a.id=1;b.id=2;
        a.bound_min=b.bound_min={-2,-1,-.5F};a.bound_max=b.bound_max={2,1,.5F};
        a.velocity={10,0,0};a.signed_speed=a.absolute_speed=10;b.position={15,0,0};
        a.path.path.count=b.path.path.count=6;
        for(unsigned i=0;i<6;++i){a.path.path.points[i]={float(i)*10,0,0};b.path.path.points[i]={15+float(i)*10,0,0};}
        std::uint32_t obstacle=123;float gap=99999;
        check(ai_path_collision_query(&world,a.path,0,4,obstacle,gap)&&obstacle==2&&gap<4&&world.pair_queries==1,
              "real pair query detects stationary car in the intended path");
        b.position[1]=20;for(unsigned i=0;i<6;++i)b.path.path.points[i][1]=20;
        check(ai_path_collision_query(&world,a.path,0,4,obstacle,gap)&&obstacle==0&&gap==99999,
              "same pair shifted clear does not manufacture a collision");
        b.path={};
        check(ai_path_collision_query(&world,a.path,0,4,obstacle,gap)&&obstacle==0&&gap==99999,
              "unpublished actor path is empty geometry, not a refused world snapshot");
        gap=42;obstacle=123;world.eligible[0]=3;
        check(!ai_path_collision_query(&world,a.path,0,4,obstacle,gap)&&obstacle==123&&gap==42,
              "invalid world query preserves caller publication");
    }
    {
        // Authentic456DE0 supplied-body witnesses; includes speed==1 branch.
        struct TrafficCase {AiDriverPoint velocity;std::uint16_t heading;AiDriverPoint expected;};
        const TrafficCase rows[]={{{0,0,0},0,{60,20,3}},
            {{0,0,0},16000,{11.840341567993164F,69.96625518798828F,3}},
            {{0,0,0},45000,{-9.381904602050781F,-26.090587615966797F,3}},
            {{1,0,0},12000,{60,20,3}},{{1.0001F,0,0},12000,{60,20,3}},
            {{12,3,2},2000,{58,32,11}},{{-20,4,0},30000,{-70,36,3}}};
        for(const auto& c:rows) {
            AiPlannedPath p;bool matches=ai_traffic_prediction({10,20,3},c.velocity,c.heading,p)&&p.path.count==1;
            for(unsigned k=0;k<3;++k)matches=matches&&std::abs(p.path.points[0][k]-c.expected[k])<.00004F;
            check(matches,"original bounded traffic linear prediction");
        }
        AiPathCollisionWorld world;world.count=10;world.self=0;world.eligible_count=1;world.eligible[0]=9;
        world.tick=1;world.retain_parameters={16,0,0};world.prediction_horizon=.75F;
        world.normal_scale=world.human_scale=world.slow_scale=1;world.slow_speed=2;
        auto& a=world.actors[0];auto& b=world.actors[9];a.id=1;b.id=10;b.role=3;
        a.bound_min=b.bound_min={-2,-1,-.5F};a.bound_max=b.bound_max={2,1,.5F};
        a.velocity={10,0,0};a.signed_speed=a.absolute_speed=10;b.position={15,0,0};
        a.path.path.count=6;
        for(unsigned i=0;i<6;++i)a.path.path.points[i]={float(i)*10,0,0};
        check(ai_traffic_prediction(b.position,b.velocity,b.heading,b.path),"prepare traffic obstacle forecast");
        std::uint32_t obstacle=123;float gap=42;
        check(!ai_path_collision_query(&world,a.path,0,4,obstacle,gap)&&obstacle==123&&gap==42,
              "unprepared traffic role refuses transactionally");
        b.traffic_prediction_ready=true;
        check(ai_path_collision_query(&world,a.path,0,4,obstacle,gap)&&obstacle==10&&gap<4,
              "last traffic pool slot is a real planner obstacle");
        b.position[1]=20;check(ai_traffic_prediction(b.position,b.velocity,b.heading,b.path),"prepare clear forecast");
        check(ai_path_collision_query(&world,a.path,0,4,obstacle,gap)&&obstacle==0&&gap==99999,
              "traffic beside the lane does not block the path");
    }
    std::array<AiPredictionPath,std::size(shape_cases)> shapes;
    unsigned shape_index=0;
    for(const auto& c:shape_cases) {
        AiDriverPath path;path.count=static_cast<std::uint8_t>(c.count);
        for(unsigned i=0;i<c.count;++i)path.points[i]=c.points[i];
        auto& output=shapes[shape_index];
        bool matches=ai_prediction_shapes(path,c.low,c.high,static_cast<std::uint16_t>(c.heading),c.scale,output)&&output.count==c.count;
        for(unsigned i=0;i<c.count;++i) {
            const auto& n=output.nodes[i];
            const float fields[]={n.position[0],n.position[1],n.position[2],n.box.axes[0][0],n.box.axes[0][1],
                n.box.axes[1][0],n.box.axes[1][1],n.box.extent[0],n.box.extent[1],n.height_extent};
            for(unsigned k=0;k<10;++k)if(bits(fields[k])!=c.expected[i][k]) {
                std::cerr<<"shape "<<shape_index<<" node "<<i<<" field "<<k<<" "<<std::hex<<bits(fields[k])<<"/"<<c.expected[i][k]<<std::dec<<'\n';matches=false;
            }
        }
        check(matches,"original path box producer and heading polynomial");
        const auto before=output;path.count=21;
        check(!ai_prediction_shapes(path,c.low,c.high,0,c.scale,output)&&output.count==before.count&&
              output.nodes[0].position==before.nodes[0].position,"oversized shape path refuses transactionally");
        ++shape_index;
    }
    unsigned walk_index=0;
    for(const auto& c:walk_cases) {
        float time=123;
        const AiTrajectoryMotion a{c.pa,c.sa,c.ia},b{c.pb,c.sb,c.ib};
        const bool ok=ai_prediction_walk(shapes[c.a],a,shapes[c.b],b,c.acceleration,c.horizon,time);
        if(!ok||bits(time)!=c.expected)std::cerr<<"walk "<<walk_index<<" ok "<<ok<<" "<<std::hex<<bits(time)<<"/"<<c.expected<<std::dec<<'\n';
        check(ok&&bits(time)==c.expected,"original segment traversal with source-generated shapes");
        const float before=time;
        check(!ai_prediction_walk(shapes[c.a],a,shapes[c.b],b,std::numeric_limits<float>::quiet_NaN(),c.horizon,time)&&
              bits(time)==bits(before),"walk refusal is not a no-collision verdict");
        ++walk_index;
    }
    unsigned overlap_index=0;
    for(const auto& c:overlap_cases) {
        AiPredictionBox a,b;
        for(unsigned k=0;k<2;++k) {
            a.extent[k]=c.extent_a[k];b.extent[k]=c.extent_b[k];
            for(unsigned j=0;j<2;++j){a.axes[k][j]=c.axes_a[2*k+j];b.axes[k][j]=c.axes_b[2*k+j];}
        }
        float time=123;
        const bool ok=ai_prediction_overlap(a,b,{c.position[0],c.position[1]},{c.velocity[0],c.velocity[1]},time);
        if(!ok||bits(time)!=c.expected)std::cerr<<"overlap "<<overlap_index<<" got "<<std::hex<<bits(time)<<"/"<<c.expected<<std::dec<<'\n';
        check(ok&&bits(time)==c.expected,"original future overlap time and sentinels");
        const float before=time;a.axes[0][0]=std::numeric_limits<float>::quiet_NaN();
        check(!ai_prediction_overlap(a,b,{0,0},{0,0},time)&&bits(time)==bits(before),"overlap refusal preserves output");
        ++overlap_index;
    }
    unsigned prediction_index=0;
    for(const auto& c:prediction_cases) {
        AiPlannedPath path;path.path.count=c.count;path.cursor=c.first;path.cursor_tick=3000;
        path.prediction_tick=c.cached_tick;path.length=from_bits(c.length);
        for(unsigned j=0;j<c.count;++j){path.path.points[j]=c.points[j];path.route[j]=static_cast<std::uint8_t>(j);
            path.point[j]=static_cast<std::uint16_t>(100+j);path.category[j]=static_cast<std::uint8_t>(j%5);}
        AiPathPredictionInput in{c.position,c.velocity,c.acceleration,c.first,c.tick,c.horizon};
        bool matches=ai_path_predict(path,in)&&bits(path.length)==c.after[0]&&path.cursor_tick==c.after[1]&&path.prediction_tick==c.after[2];
        for(unsigned j=0;j<c.count;++j) {
            for(unsigned k=0;k<3;++k)matches=matches&&bits(path.path.points[j][k])==c.expected[j][k];
            matches=matches&&path.route[j]==j&&path.point[j]==100+j&&path.category[j]==j%5;
        }
        if(!matches)std::cerr<<"prediction "<<prediction_index<<" length "<<std::hex<<bits(path.length)<<"/"<<c.after[0]<<std::dec<<'\n';
        check(matches,"original predicted points, metadata, length and caches");
        const auto before=path;in.acceleration={999,999,0};
        check(ai_path_predict(path,in)&&path.path.points==before.path.points&&bits(path.length)==bits(before.length),
              "same tick does not predict twice");
        in.tick++;in.horizon=std::numeric_limits<float>::quiet_NaN();
        check(!ai_path_predict(path,in)&&path.path.points==before.path.points&&path.prediction_tick==before.prediction_tick,
              "prediction refusal is transactional");
        ++prediction_index;
    }
    unsigned fitness_index=0;
    for(const auto& c:fitness_cases) {
        AiPlannedPath path;path.path.count=c.count;path.cursor=c.cached_cursor;path.cursor_tick=c.cached_tick;
        AiPathFitnessInput in;in.signed_speed=c.speed;in.absolute_speed=std::fabs(c.speed);in.tick=c.tick;
        for(unsigned k=0;k<3;++k){in.position[k]=c.position[k];in.velocity[k]=c.velocity[k];in.retain_parameters[k]=c.params[k];}
        in.minimum_time=c.params[3];in.initial_time=c.params[4];in.lateral_acceleration_limit=c.params[5];
        in.transition_end=static_cast<std::uint8_t>(c.transition_end);
        for(unsigned j=0;j<c.count;++j)for(unsigned k=0;k<3;++k)path.path.points[j][k]=c.points[j][k];
        const auto geometry=path.path.points;float score=123;
        const bool ok=ai_path_fitness(path,in,score);
        const bool matches=ok&&bits(score)==c.score&&path.cursor==c.cursor&&path.cursor_tick==c.cursor_tick;
        if(!matches)std::cerr<<"fitness "<<fitness_index<<" ok "<<ok<<" score "<<std::hex<<bits(score)<<"/"<<c.score
            <<std::dec<<" cursor "<<path.cursor<<"/"<<c.cursor<<" tick "<<path.cursor_tick<<"/"<<c.cursor_tick<<'\n';
        check(matches,"original path fitness and cursor");check(path.path.points==geometry,"fitness preserves geometry");
        const auto before=path;const float before_score=score;
        in.velocity[1]=std::numeric_limits<float>::quiet_NaN();
        check(!ai_path_fitness(path,in,score)&&bits(score)==bits(before_score)&&path.cursor==before.cursor&&
            path.cursor_tick==before.cursor_tick&&path.path.points==before.path.points,"fitness refusal preserves outputs");
        ++fitness_index;
    }
    unsigned physical_index=0;
    for(const auto& row:kNativePhysicalObstacle) {
        AiPhysicalObstacleInput in;in.heading=static_cast<std::uint16_t>(row[0]);in.other_shape=row[1]!=0;
        unsigned k=2;
        for(auto* p:{&in.self_min,&in.self_max,&in.position,&in.velocity})for(float& x:*p)x=from_bits(row[k++]);
        in.signed_speed=from_bits(row[k++]);in.absolute_speed=from_bits(row[k++]);
        for(auto* p:{&in.other_min,&in.other_max,&in.other_position})for(float& x:*p)x=from_bits(row[k++]);
        for(float& x:in.other_forward)x=from_bits(row[k++]);
        for(float& x:in.other_velocity)x=from_bits(row[k++]);
        AiPhysicalObstacleResult result;const bool ok=ai_physical_obstacle(in,result);
        const bool matches=ok&&bits(result.distance)==row[k]&&bits(result.time)==row[k+1];
        if(!matches)std::cerr<<"physical "<<physical_index<<" ok "<<ok<<" distance "<<std::hex<<bits(result.distance)<<"/"<<row[k]
            <<" time "<<bits(result.time)<<"/"<<row[k+1]<<std::dec<<'\n';
        check(matches,"original physical obstacle and scalar motion frame");
        const auto before=result;in.position[0]=std::numeric_limits<float>::quiet_NaN();
        check(!ai_physical_obstacle(in,result)&&bits(result.distance)==bits(before.distance)&&bits(result.time)==bits(before.time),
            "physical obstacle refusal preserves output");
        ++physical_index;
    }
    std::cout<<"Original physical obstacle witnesses: "<<physical_index<<'\n';
    std::cout<<"Original overlap witnesses: "<<overlap_index<<'\n';
    std::cout<<"Original shape/walk witnesses: "<<shape_index<<'/'<<walk_index<<'\n';
    std::cout<<"Original prediction witnesses: "<<prediction_index<<'\n';
    std::cout<<"Original fitness witnesses: "<<fitness_index<<'\n';
    const auto motion_equal=[](const AiPlannedPath& path,const auto& c) {
        if(path.path.count!=c.count||bits(path.length)!=c.length)return false;
        for(unsigned j=0;j<c.count;++j) {
            for(unsigned k=0;k<3;++k)if(bits(path.path.points[j][k])!=c.points[j][k])return false;
            if(path.route[j]!=c.route[j]||path.point[j]!=c.point[j]||path.category[j]!=c.category[j])return false;
        }
        return true;
    };
    unsigned motion_index=0,motion_failures=0;
    for(const auto& c:kNativeMotionPrediction) {
        AiDriverPoint origin,velocity,acceleration;
        for(unsigned k=0;k<3;++k){origin[k]=from_bits(c.origin[k]);velocity[k]=from_bits(c.velocity[k]);acceleration[k]=from_bits(c.acceleration[k]);}
        AiPlannedPath path;
        if(!ai_motion_prediction(origin,velocity,acceleration,std::uint16_t(c.heading),from_bits(c.limit),path)||!motion_equal(path,c)) {
            ++failures;
            if(motion_failures++<10)std::cerr<<"FAIL: actual45A2F0 case "<<motion_index<<" count "<<unsigned(path.path.count)<<'/'<<c.count<<" length "<<bits(path.length)<<'/'<<c.length<<'\n';
        }
        ++motion_index;
    }
    unsigned human_world_index=0;
    for(const auto& c:kNativeHumanWorldPath) {
        AiDriverPoint origin,velocity,forward;
        for(unsigned k=0;k<3;++k){origin[k]=from_bits(c.origin[k]);velocity[k]=from_bits(c.velocity[k]);forward[k]=from_bits(c.forward[k]);}
        AiPlannedPath path;
        bool same=ai_human_world_prediction(origin,velocity,forward,from_bits(c.magnitude),path)&&path.path.count==1&&bits(path.length)==c.length;
        for(unsigned k=0;k<3;++k)same=same&&bits(path.path.points[0][k])==c.point[k];
        same=same&&path.route[0]==c.route&&path.point[0]==c.index&&path.category[0]==c.category;
        if(!same){++failures;std::cerr<<"FAIL: original57A530 ordinary human world path "<<human_world_index<<'\n';}
        ++human_world_index;
    }
    std::cout<<"Original human world-path witnesses: "<<human_world_index<<'\n';
    unsigned linear_index=0;
    for(const auto& c:kNativeLinearPrediction) {
        AiDriverPoint velocity;for(unsigned k=0;k<3;++k)velocity[k]=from_bits(c.velocity[k]);
        AiPlannedPath path;
        if(!ai_traffic_prediction({10,20,3},velocity,std::uint16_t(c.heading),path)||!motion_equal(path,c)) {
            ++failures;std::cerr<<"FAIL: actual456DE0 normalization/heading case "<<linear_index<<'\n';
        }
        ++linear_index;
    }
    {
        AiPlannedPath path;path.path.count=1;path.path.points[0]={7,8,9};
        const float nan=std::numeric_limits<float>::quiet_NaN();
        check(!ai_motion_prediction({0,0,0},{1,0,0},{nan,0,0},0,50,path)&&path.path.count==1&&path.path.points[0][0]==7,
            "invalid motion producer refuses without publishing a clear forecast");
        check(!ai_motion_prediction({0,0,0},{1,0,0},{0,0,0},0,nan,path)&&path.path.points[0][0]==7,
            "invalid length preserves prior geometry");
    }
    unsigned selection_index=0;
    for(const auto& c:kNativePathSelection) {
        AiPathSelectionInput in;in.ignored=c.ignored;in.path_count=std::uint8_t(c.path_count);
        in.road_selected=c.selected!=0;in.absolute_speed=from_bits(c.speed);
        for(unsigned j=0;j<3;++j)if(c.eligible&(1U<<j))in.ids[in.count++]=j+1;
        struct Witness {const NativePathSelection& row;const AiPathSelectionInput& input;unsigned count=0;std::array<unsigned,3> calls{};};
        Witness witness{c,in};
        const auto evaluate=[](void* p,unsigned ordinal,bool& hit,float& time) noexcept {
            auto& w=*static_cast<Witness*>(p);const auto id=w.input.ids[ordinal];
            w.calls[w.count++]=id;hit=true;time=from_bits(w.row.times[id-1]);return true;
        };
        AiPathSelectionResult result;
        const bool ok=ai_path_select_collision(in,evaluate,&witness,result);
        bool equal=ok&&result.target==c.target&&(result.time_written?bits(result.time):bits(42.F))==c.time&&
            result.visits==c.eligible_visits&&result.queries==c.call_count&&witness.count==c.call_count;
        for(unsigned j=0;j<witness.count;++j)equal=equal&&witness.calls[j]==c.calls[j];
        if(!equal){++failures;std::cerr<<"FAIL: original457590 caller selection "<<selection_index<<'\n';}
        ++selection_index;
    }
    {
        AiPathSelectionInput in;in.ids[0]=1;in.count=1;in.path_count=1;in.absolute_speed=20;
        AiPathSelectionResult result;result.target=99;result.time=7;
        check(!ai_path_select_collision(in,nullptr,nullptr,result)&&result.target==99&&result.time==7,
            "absent pair producer cannot become a clear path");
        const auto refuse=[](void*,unsigned,bool&,float&) noexcept {return false;};
        check(!ai_path_select_collision(in,refuse,nullptr,result)&&result.target==99,
            "refused pair result is not an admitted miss");
        const auto miss=[](void*,unsigned,bool& hit,float& time) noexcept {hit=false;time=std::numeric_limits<float>::quiet_NaN();return true;};
        check(ai_path_select_collision(in,miss,nullptr,result)&&result.target==0&&result.time==99999&&result.time_written,
            "false pair result does not consume its unused time");
    }
    {
        AiPathCollisionWorld world;world.count=3;world.self=0;world.eligible_count=2;world.eligible[0]=1;world.eligible[1]=2;
        world.tick=1;world.retain_parameters={16,0,0};world.prediction_horizon=.75F;
        world.normal_scale=world.human_scale=world.slow_scale=1;world.slow_speed=2;
        auto& a=world.actors[0];auto& b=world.actors[1];a.id=1;b.id=2;
        a.bound_min=b.bound_min={-2,-1,-.5F};a.bound_max=b.bound_max={2,1,.5F};
        a.velocity={10,0,0};a.signed_speed=a.absolute_speed=10;b.position={15,0,0};
        a.path.path.count=b.path.path.count=6;
        for(unsigned j=0;j<6;++j){a.path.path.points[j]={float(j)*10,0,0};b.path.path.points[j]={15+float(j)*10,0,0};}
        world.actors[2]=b;world.actors[2].id=3;
        std::uint32_t target=99;float time=42;
        check(ai_path_collision_query(&world,a.path,0,4,target,time)&&target==2&&time<4&&world.pair_queries==2,
            "real equal-time pairs retain the first admitted world actor");
        world.eligible[0]=2;world.eligible[1]=1;
        check(ai_path_collision_query(&world,a.path,0,4,target,time)&&target==3,
            "original registered order controls equal-time selection");
        world.eligible_count=0;time=42;
        check(ai_path_collision_query(&world,a.path,0,4,target,time)&&target==0&&time==99999,
            "known empty eligible roster writes the original sentinel");
        world.road_selected=false;time=42;
        check(ai_path_collision_query(&world,a.path,0,4,target,time)&&target==0&&time==42,
            "known absent road leaves original caller time untouched");
        world.road_selected=true;auto empty=a.path;empty.path.count=0;
        check(ai_path_collision_query(&world,empty,0,4,target,time)&&target==0&&time==42,
            "empty candidate path retains the distinct original guard");
    }
    unsigned world_pair_index=0,world_pair_failures=0;
    for(const auto& c:kNativeWorldPair) {
        AiPathCollisionWorld world;world.count=2;world.eligible_count=1;world.eligible[0]=1;world.tick=80;
        world.retain_parameters={16,0,0};world.prediction_horizon=.75F;
        world.normal_scale=world.human_scale=world.slow_scale=1;world.slow_speed=2;
        for(unsigned j=0;j<2;++j) {
            auto& a=world.actors[j];a.id=j+1;a.role=j?int(c.role):2;a.selector=j&&c.role==1?1:3;
            a.position={j?from_bits(c.motion[0]):0.F,j?from_bits(c.motion[1]):0.F,0};
            a.signed_speed=from_bits(c.motion[2+j]);a.absolute_speed=j?from_bits(c.motion[4]):20.F;
            a.velocity={a.signed_speed,0,0};a.action=j?int(c.action):(a.signed_speed<0?10:7);
            for(unsigned k=0;k<3;++k){a.bound_min[k]=from_bits(c.boxes[j][0][k]);a.bound_max[k]=from_bits(c.boxes[j][1][k]);}
            a.path.path.count=2;a.path.length=100;
            for(unsigned k=0;k<2;++k){a.path.path.points[k]={a.position[0]+100.F*float(k+1),a.position[1],0};a.path.route[k]=255;a.path.point[k]=65535;a.path.category[k]=255;}
            if(j&&c.role==1)check(ai_human_world_prediction(a.position,a.velocity,a.forward,a.absolute_speed,a.path),"publish original human world-pair forecast");
        }
        float time=42;std::uint32_t target=0;
        const bool ok=ai_path_collision_query(&world,world.actors[0].path,0,4,target,time);
        if(!ok||target!=(c.hit?2U:0U)||bits(time)!=c.time) {
            ++failures;
            if(world_pair_failures++<20)std::cerr<<"FAIL: whole4576B0 pair "<<world_pair_index<<" ok "<<ok<<" target "<<target<<'/'<<c.hit<<" time "<<std::hex<<bits(time)<<'/'<<c.time<<std::dec<<'\n';
        }
        ++world_pair_index;
    }
    unsigned traffic_world_index=0,traffic_world_failures=0,legacy_traffic_differences=0,legacy_traffic_decisions=0;
    for(const auto& c:kNativeTrafficWorldPair) {
        AiPathCollisionWorld world;world.count=2;world.eligible_count=1;world.eligible[0]=1;world.tick=c.tick;
        world.retain_parameters={16,0,0};world.prediction_horizon=.75F;
        world.normal_scale=world.human_scale=world.slow_scale=1;world.slow_speed=2;
        for(unsigned j=0;j<2;++j) {
            auto& a=world.actors[j];a.id=j+1;a.role=j?3:2;a.selector=j?4:3;
            a.position={j?from_bits(c.position[0]):0.F,j?from_bits(c.position[1]):0.F,0};a.velocity={j?from_bits(c.speed):20.F,0,0};
            a.signed_speed=a.velocity[0];a.absolute_speed=j?std::abs(a.velocity[0]):20.F;a.action=j?int(c.action):7;
            for(unsigned k=0;k<3;++k){a.bound_min[k]=from_bits(c.boxes[j][0][k]);a.bound_max[k]=from_bits(c.boxes[j][1][k]);}
            a.path.path.count=2;a.path.length=100;
            for(unsigned k=0;k<2;++k){a.path.path.points[k]={a.position[0]+100.F*float(k+1),a.position[1],0};a.path.route[k]=255;a.path.point[k]=65535;a.path.category[k]=255;}
        }
        auto& b=world.actors[1];
        if(c.curve) {
            b.path.path.count=3;b.path.length=40;
            b.path.path.points[0]={b.position[0]+20,b.position[1],0};b.path.path.points[1]={b.position[0]+40,b.position[1],0};b.path.path.points[2]={b.position[0]+40,b.position[1]+20,0};
            b.path.route[2]=255;b.path.point[2]=65535;b.path.category[2]=255;
        }
        AiPlannedPath actual;
        bool okay=ai_traffic_world_prediction(b,b.path,c.outside,c.action,c.tick,.75F,actual);
        bool matches=okay&&actual.path.count==c.count&&bits(actual.length)==c.length&&actual.cursor==c.cursor&&
            actual.cursor_tick==c.cursor_tick&&actual.prediction_tick==c.prediction_tick;
        for(unsigned i=0;i<std::min(unsigned(actual.path.count),c.count);++i)
            for(unsigned k=0;k<3;++k)matches=matches&&bits(actual.path.points[i][k])==c.points[i][k];
        if(!matches) {
            ++failures;if(traffic_world_failures++<12)std::cerr<<"FAIL traffic forecast "<<traffic_world_index
                <<" count="<<unsigned(actual.path.count)<<'/'<<c.count<<" length="<<bits(actual.length)<<'/'<<c.length
                <<" cursor="<<actual.cursor<<'/'<<c.cursor<<" ticks="<<actual.cursor_tick<<'/'<<c.cursor_tick
                <<","<<actual.prediction_tick<<'/'<<c.prediction_tick<<'\n';
        }
        AiPlannedPath legacy;check(ai_traffic_prediction(b.position,b.velocity,b.heading,legacy),"legacy diagnostic forecast");
        bool old_matches=legacy.path.count==c.count&&bits(legacy.length)==c.length;
        for(unsigned i=0;i<std::min(unsigned(legacy.path.count),c.count);++i)
            for(unsigned k=0;k<3;++k)old_matches=old_matches&&bits(legacy.path.points[i][k])==c.points[i][k];
        legacy_traffic_differences+=!old_matches;
        b.path=legacy;b.traffic_prediction_ready=true;float legacy_time=42;std::uint32_t legacy_target=0;
        const bool legacy_pair=ai_path_collision_query(&world,world.actors[0].path,0,4,legacy_target,legacy_time);
        legacy_traffic_decisions+=!legacy_pair||legacy_target!=(c.hit?2U:0U)||bits(legacy_time)!=c.time;
        b.path=actual;float time=42;std::uint32_t target=0;
        const bool pair=ai_path_collision_query(&world,world.actors[0].path,0,4,target,time);
        if(!pair||target!=(c.hit?2U:0U)||bits(time)!=c.time) {
            ++failures;if(traffic_world_failures++<12)std::cerr<<"FAIL traffic whole pair "<<traffic_world_index
                <<" target="<<target<<'/'<<c.hit<<" time="<<bits(time)<<'/'<<c.time<<'\n';
        }
        ++traffic_world_index;
    }
    check(legacy_traffic_differences>0,"native traffic caller cases expose replaced straight adapter");
    std::cout<<"Original role3 world forecast/pair witnesses: "<<traffic_world_index
             <<" legacy differences: "<<legacy_traffic_differences<<" pair differences: "<<legacy_traffic_decisions<<'\n';
    std::cout<<"Original whole world pair witnesses: "<<world_pair_index<<'\n';
    std::cout<<"Original caller selection witnesses: "<<selection_index<<'\n';
    std::cout<<"Original sampled/straight motion witnesses: "<<motion_index<<'/'<<linear_index<<'\n';
    std::cout<<"AI path: "<<failures<<" failures\n";return failures?1:0;
}
