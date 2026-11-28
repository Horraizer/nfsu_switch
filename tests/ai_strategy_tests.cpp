#include "nfsu/ai_strategy.h"
#include "nfsu/ai_rival_action.h"
#include <iostream>
#include <cstring>
#include <cmath>
using namespace nfsu;
namespace {
struct ModeCase {int priority;bool subtract_lengths;};
struct CadenceCase {std::uint32_t frame,tick,previous;unsigned slot,count;bool have_action,full;};
const CadenceCase cadence_cases[]={
#include "ai_cadence_native.inc"
};
struct StrategyVirtualCase {int kind;bool traffic,rebuild;};
struct TrafficAdmissionCase {int kind,role;bool force,present;int priority;bool target;};
struct BlockCase {
    float gap,relative,lateral,speed;bool human;int mode_priority;
    bool subtract_lengths,hold;int priority;bool found;
};
struct RollCase {int contacts;AiDriverPoint up;bool request;};
struct GateCase {
    std::uint32_t target,tick;float cooldown,probability;
    std::uint32_t previous,last,seed;bool allow;
    std::uint32_t after_target,after_tick,after_seed;
};
struct RetainedPassCase {AiDriverPoint position;float relative,gap;bool admitted;float time;};
struct PassingLifetimeCase {std::uint32_t start;bool found;std::uint32_t delta;bool complete;};
const PassingLifetimeCase passing_lifetime_cases[]={
#include "ai_passing_lifecycle_native.inc"
};
const RetainedPassCase retained_pass_cases[]={
#include "ai_retained_pass_native.inc"
};
#include "ai_strategy_native.inc"
struct ReactionCase {
    AiReactionInput input;AiReactionState before;std::uint32_t seed;
    AiReactionState after;std::uint32_t after_seed;
};
struct ReactionOutputCase {AiReactionState state;std::uint32_t tick;bool manual,flash,horn;};
#include "ai_reaction_native.inc"
#include "drag_strategy_native.inc"

unsigned failures=0;
void check(bool ok,const char* what){if(!ok){++failures;std::cerr<<"FAIL: "<<what<<'\n';}}
struct Queries {
    std::array<AiStrategyQuery,8> calls{};unsigned count=0;
    AiStrategyQuery found=AiStrategyQuery::following;bool refuse=false;
};
bool evaluate(void* opaque,AiStrategyQuery q,bool& found,AiActionCandidate&) {
    auto& s=*static_cast<Queries*>(opaque);s.calls[s.count++]=q;
    found=q==s.found;return !s.refuse;
}
struct DragQueries {int mask=0;std::array<int,5> calls{};unsigned count=0;};
bool drag_evaluate(void* opaque,AiStrategyQuery query,bool& found,AiActionCandidate&) {
    auto& s=*static_cast<DragQueries*>(opaque);const int q=int(query);s.calls[s.count++]=q;
    const int bit=query==AiStrategyQuery::recovery?1:query==AiStrategyQuery::obstacle?2:
                  query==AiStrategyQuery::wall_contact?4:query==AiStrategyQuery::contact?8:16;
    found=(s.mask&bit)!=0;return true;
}
}
int main() {
    for(const auto& row:kNativeDragStrategy) {
        AiStrategySelection input;input.current_kind=row.kind;input.current_priority=row.priority;input.recovery_enabled=row.enabled!=0;
        DragQueries queries;queries.mask=row.mask;AiActionCandidate selected;
        check(ai_drag_strategy_choose(input,drag_evaluate,&queries,selected)&&selected.kind==row.selected,
              "native drag policy2 selected action");
        check(queries.count==row.count,"native drag policy2 query count");
        for(unsigned i=0;i<std::min(queries.count,row.count);++i)
            check(queries.calls[i]==row.calls[i],"native drag policy2 lazy probe order");
    }
    for(const auto& c:cadence_cases) {
        bool full=!c.full;
        check(ai_rival_full_update(c.frame,c.tick,c.previous,c.slot,c.count,c.have_action,full)&&full==c.full,
              "native frame slot and minimum rival decision age");
    }
    bool unchanged=true;
    check(!ai_rival_full_update(1,2000,0,0,0,true,unchanged)&&unchanged&&
          !ai_rival_full_update(1,2000,0,3,3,true,unchanged)&&unchanged,
          "missing world rival range refuses cadence transactionally");
    for(bool special:{false,true}) {
        AiRoadChain road;road.count=4;
        for(unsigned j=0;j<4;++j) {
            auto& c=road.cells[j];c.point.position[0]=20.F*(j+1);c.point.forward[0]=1;
            c.point.unknown_24=c.point.unknown_28=30;c.point.distance_to_next=c.point.distance_to_previous=20;
            c.distance=20.F*j;c.lower[0]=-25;c.upper[0]=25;
        }
        AiNeighborhood n;n.count=2;n.self_index=0;
        n.entries[0].id=1;n.entries[0].half_extent={1,2};
        n.entries[1].id=2;n.entries[1].distance=10;n.entries[1].half_extent={1,2};
        n.entries[1].relative_velocity[1]=20;
        AiPathCollisionWorld world;world.count=2;world.tick=100000;world.eligible_count=1;world.eligible[0]=1;
        world.prediction_horizon=.75F;world.retain_parameters={16,0,0};
        world.normal_scale=world.human_scale=world.slow_scale=1;world.slow_speed=2;
        auto& a=world.actors[0];auto& b=world.actors[1];a.id=1;b.id=2;b.role=3;
        a.velocity={20,0,0};a.signed_speed=a.absolute_speed=20;b.position={10,0,0};
        a.bound_min=b.bound_min={-2,-1,-.5F};a.bound_max=b.bound_max={2,1,.5F};
        a.path.path.count=4;for(unsigned j=0;j<4;++j)a.path.path.points[j]={20.F*(j+1),0,0};
        check(ai_traffic_prediction(b.position,b.velocity,b.heading,b.path),"traffic query fixture prediction");
        b.traffic_prediction_ready=true;
        AiPathPlannerInput planner;planner.path.roads=road.cells.data();planner.path.road_count=4;
        planner.path.category_available=true;planner.path.rebuild=true;planner.path.tick=world.tick;
        planner.path.signed_speed=20;planner.path.policy.absolute_speed=20;
        planner.path.extension.actor_velocity=a.velocity;planner.path.extension.absolute_speed=20;
        planner.path.extension.clearance=1;planner.choice.horizon=2;planner.choice.current_available=true;
        planner.choice.count=1;planner.choice.neighbors[0]={2,true,1,0};
        planner.target_count=1;planner.target_ids[0]=2;
        auto& moving=planner.targets[0];moving.self={a.position,a.velocity,0,1,0,0,2,true,false};
        moving.target={b.position,b.velocity,0,1,10,0,3,true,false};
        moving.policy={4,5,5,2,1.5F,5,2,.707099974155426F};moving.target_path=b.path;
        planner.fitness={a.position,a.velocity,20,20,world.tick,{16,0,0},.4F,.5F,30,255};
        planner.side_score_scale=1.2F;planner.fallback_margin=1;
        AiRivalActionInput in;in.planner=&planner;in.world=&world;in.neighbors=&n;in.road=&road;
        in.tick=world.tick;in.race_seconds=10;in.target_probability=1;
        in.motion[0]={1,true,2,a.velocity,AiStrategyKind::ordinary};
        in.motion[1]={2,false,3,b.velocity,special?AiStrategyKind::special:AiStrategyKind::traffic};
        in.actors[0]={true,false,20};std::uint32_t rng=12345;in.random=&rng;
        AiRivalActionController controller;AiRivalActionOutput result;
        check(controller.step(in,result)&&result.action.kind==(special?2:1),
            "ordinary traffic passes through the native admission gate to passing action");
        if(!special) {
            auto input=in;auto plan=planner;auto nearby=n;auto snapshot=world;auto chain=road;
            for(auto& cell:chain.cells){cell.lower.fill(-25);cell.upper.fill(25);}
            plan.path.roads=chain.cells.data();snapshot.eligible_count=0;
            std::swap(nearby.entries[0],nearby.entries[1]);nearby.self_index=1;
            std::swap(input.motion[0],input.motion[1]);std::swap(input.actors[0],input.actors[1]);
            input.road=&chain;input.planner=&plan;input.neighbors=&nearby;input.world=&snapshot;
            input.category_context=&plan;
            input.set_category=[](void* context,int value) {
                auto& p=*static_cast<AiPathPlannerInput*>(context);
                p.path.extension.category=value;p.path.category_available=value>=0;return true;
            };
            auto& ahead=nearby.entries[0];
            const auto first=[&](int category,float street,float lane,float closing,int expected) {
                AiRivalActionController driver;AiRivalActionOutput action;
                plan.path.extension.category=category;ahead.lateral=street;
                ahead.lane_lateral[0]=lane;ahead.relative_velocity[1]=closing;
                check(driver.step(input,action)&&action.action.kind==expected,
                      "following compares lateral distance in the active road category");
                if(expected==0)check(action.target==2&&action.check_nitrous==(closing<=5.F),
                    "following nitrous producer runs only at up to five metres per second relative speed");
                return driver;
            };
            first(0,25,0,20,7);first(11,25,0,20,0);first(11,0,25,20,7);
            first(0,0,0,5,0);
            auto lost=first(0,0,0,20,0);
            nearby.entries[0]=nearby.entries[1];nearby.self_index=0;nearby.count=1;
            input.motion[0]=input.motion[1];input.actors[0]=input.actors[1];
            snapshot.count=1;plan.target_count=plan.choice.count=0;
            ++input.tick;snapshot.tick=plan.path.tick=plan.fitness.tick=input.tick;
            AiRivalActionOutput replacement;
            check(lost.step(input,replacement)&&replacement.action.kind==7&&replacement.target==0,
                  "missing follow target immediately returns to ordinary strategy");
        }
        if(special) {
            // PC 453060 clears the crossing offset before the next goal is queried.
            auto crossing_world=world;
            auto crossing_plan=planner;
            auto crossing_neighbors=n;
            auto crossing=in;
            auto& traffic=crossing_world.actors[1];
            traffic.position={25,1.5F,0};
            check(ai_traffic_prediction(traffic.position,traffic.velocity,traffic.heading,traffic.path),
                  "predict crossing traffic for action exit");
            crossing_plan.targets[0].target_path=traffic.path;
            crossing_plan.targets[0].target.position=traffic.position;
            crossing_plan.targets[0].target.distance=crossing_neighbors.entries[1].distance=25;
            crossing_neighbors.entries[1].lateral=1.5F;
            crossing.world=&crossing_world;crossing.neighbors=&crossing_neighbors;crossing.planner=&crossing_plan;
            AiRivalActionController crossing_driver;AiRivalActionOutput crossing_result;
            check(crossing_driver.step(crossing,crossing_result)&&crossing_result.action.kind==2&&
                  crossing_result.maneuver.parameter!=0,"crossing action selects an offset to pass traffic");
            crossing_world.eligible_count=0;
            crossing_world.tick=crossing_plan.path.tick=crossing_plan.fitness.tick=++crossing.tick;
            struct ExitProbe {const AiRivalActionController& driver;bool seen=false;};
            ExitProbe exit_probe{crossing_driver};
            crossing.obstacle_context=&exit_probe;
            crossing.obstacle_read=[](void* ctx,WorldObstacleStatus& status) {
                auto& probe=*static_cast<ExitProbe*>(ctx);
                const auto& m=probe.driver.maneuver();
                probe.seen=m.kind==1&&!m.target&&m.parameter==0&&!m.flag;
                status={};return true;
            };
            check(crossing_driver.step(crossing,crossing_result)&&crossing_result.action.kind!=2&&exit_probe.seen,
                  "cross-traffic exit restores straight driving before selecting the replacement action");
            //452D84 starts from scratch after44C850 publication if its target
            //has no neighborhood entry. The old Driver still predicts contact,
            //so the action remains alive for this update.
            auto lost=controller;auto lost_input=in;auto lost_neighbors=n;
            auto lost_plan=planner;lost_neighbors.count=1;
            lost_input.neighbors=&lost_neighbors;lost_input.planner=&lost_plan;
            lost_plan.path.previous={};
            ++lost_input.tick;
            AiPlannedPath expected;auto extension=lost_plan.path.extension;
            extension.mode=0;extension.margin=0;extension.target_offset=0;extension.offset_step=99999;
            check(ai_path_continue(lost_plan.path.roads,lost_plan.path.road_count,
                lost_plan.path.selected,lost_plan.path.initial_offset,extension,expected),
                "missing traffic target direct continuation reference");
            AiRivalActionOutput continued;
            check(lost.step(lost_input,continued)&&continued.action.kind==2&&
                  continued.path.path.count==expected.path.count&&
                  continued.path.path.points==expected.path.points,
                  "missing traffic target fills cleared action scratch instead of reusing published path");
        }
        for(int category:{-1,0,13})for(bool window_clear:{false,true}) {
            auto obstacle_input=in;auto obstacle_plan=planner;auto obstacle_world=world;
            obstacle_input.planner=&obstacle_plan;obstacle_input.world=&obstacle_world;
            obstacle_input.obstacle_available=true;obstacle_input.obstacle_blocked=!window_clear;
            obstacle_input.road_reset=true;obstacle_plan.path.extension.category=category;
            obstacle_plan.choice.count=0;obstacle_plan.target_count=0;
            //An impassable ordinary traffic hull is not a race actor57A6F0.
            obstacle_world.actors[1].bound_min={-100,-100,-100};
            obstacle_world.actors[1].bound_max={100,100,100};
            AiPathCandidate trial;
            check(ai_path_evaluate(obstacle_plan.path,obstacle_plan.fitness,
                {&obstacle_world,ai_path_collision_query},obstacle_plan.side_score_scale,
                obstacle_plan.choice.horizon,trial)&&!trial.accepted,
                "obstacle window fixture has a rejected current path");
            AiRivalActionController obstacle_controller;AiRivalActionOutput obstacle_result;
            check(obstacle_controller.step(obstacle_input,obstacle_result)&&obstacle_result.action.kind==19&&
                  obstacle_result.speed.speed==((category==0||!window_clear)?0.F:1000.F)&&
                  obstacle_result.speed.route_speed==(category!=0&&window_clear),
                  "obstacle action separates race-line search failure from lane collision rejection");
            if(category!=0)check(obstacle_result.evaluations==1&&
                obstacle_result.path.path.count==trial.geometry.path.path.count&&
                obstacle_result.path.path.points==trial.geometry.path.path.points,
                "obstacle lane path evaluates retained descriptor once without all-neighbor search");
        }
        if(!special) {
            for(const auto& c:passing_lifetime_cases) {
                auto input=in;auto plan=planner;auto snapshot=world;auto nearby=n;
                input.tick=c.start;input.world=&snapshot;input.planner=&plan;input.neighbors=&nearby;
                snapshot.tick=plan.path.tick=plan.fitness.tick=input.tick;
                if(!c.found) {
                    plan.choice.count=0;plan.target_count=0;
                    snapshot.actors[1].bound_min={-100,-100,-100};
                    snapshot.actors[1].bound_max={100,100,100};
                }
                auto trial_plan=plan;trial_plan.choice.current={0,0,0,false};
                trial_plan.choice.current_available=false;
                AiPathChoiceResult trial;
                check(ai_plan_path(trial_plan,snapshot,trial)&&trial.found==c.found,
                      "passing lifetime fixture selects the native path-result byte");
                AiRivalActionController passing;AiRivalActionOutput entered;
                check(passing.step(input,entered)&&entered.action.kind==1,
                      "passing lifetime fixture enters the real ordinary factory");
                const auto handle=entered.action.handle;
                input.tick+=c.delta;snapshot.tick=plan.path.tick=plan.fitness.tick=input.tick;
                AiRivalActionOutput after;
                auto partial=passing;
                check(partial.refresh(input,after)&&after.action.handle==handle,
                      "partial driver publication never expires a passing action");
                check(passing.step(input,after)&&(after.action.handle!=handle)==c.complete,
                      "original passing factory argument and strict wrapping-clock deadline");
            }
            // Expire a real passing action with a retained side descriptor.
            // The old Driver path is empty, as in the native44F8A0 vectors.
            for(const auto& c:retained_pass_cases)for(float speed:{0.F,25.F}) {
                auto retained=controller;auto input=in;auto snapshot=world;
                auto nearby=n;auto plan=planner;
                snapshot.actors[0].path={};snapshot.eligible_count=0;
                snapshot.actors[1].position=c.position;
                snapshot.actors[0].velocity=snapshot.actors[1].velocity={speed,0,0};
                snapshot.actors[0].absolute_speed=snapshot.actors[0].signed_speed=speed;
                snapshot.actors[1].absolute_speed=snapshot.actors[1].signed_speed=speed;
                input.motion[0].velocity=input.motion[1].velocity={speed,0,0};
                nearby.entries[1].distance=c.gap;nearby.entries[1].relative_velocity[1]=c.relative;
                plan.targets[0].target.position=c.position;plan.targets[0].target.distance=c.gap;
                plan.targets[0].self.velocity=plan.targets[0].target.velocity={speed,0,0};
                input.tick+=30000;input.race_seconds=0;input.world=&snapshot;input.neighbors=&nearby;
                input.planner=&plan;snapshot.tick=plan.path.tick=plan.fitness.tick=input.tick;
                AiRivalActionOutput next;
                check(retained.step(input,next)&&next.action.kind==(c.admitted?1:7),
                      "native retained pass uses XYZ separation and inclusive time limit, not target speed");
                if(c.admitted) {
                    const auto handle=next.action.handle;
                    input.tick+=8000;snapshot.tick=plan.path.tick=plan.fitness.tick=input.tick;
                    check(retained.step(input,next)&&next.action.handle==handle,
                          "retained pass lifetime uses successful path search, not signed approach time");
                }
            }
            //450210 resets category after an accepted ordinary trial, but
            //before the fallback path when that trial is obstructed.
            for(bool clear:{false,true}) {
                auto ordinary=controller;auto input=in;auto snapshot=world;
                auto nearby=n;auto plan=planner;auto cells=road;
                snapshot.actors[0].path={};snapshot.count=clear?1:2;
                snapshot.eligible_count=clear?0:1;
                snapshot.actors[1].bound_min={-100,-100,-100};
                snapshot.actors[1].bound_max={100,100,100};
                nearby.count=1;plan.choice.count=0;plan.target_count=0;
                plan.path.previous={};plan.path.extension.category=-1;
                plan.path.category_available=true;plan.path.initial_offset=0;
                plan.path.roads=cells.cells.data();
                for(auto& cell:cells.cells) {
                    cell.point.path_distance=0;cell.point.driving_offset[2]=8;
                }
                input.tick+=30000;input.race_seconds=0;input.ride_category=2;
                snapshot.tick=plan.path.tick=plan.fitness.tick=input.tick;
                input.world=&snapshot;input.neighbors=&nearby;input.planner=&plan;input.road=&cells;
                AiPathCandidate trial;
                check(ai_path_evaluate(plan.path,plan.fitness,{&snapshot,ai_path_collision_query},
                    plan.side_score_scale,plan.choice.horizon,trial)&&trial.accepted==clear,
                    "ordinary return fixture distinguishes accepted and rejected trials");
                unsigned writes=0;
                const auto category=[&](int value) {
                    ++writes;plan.path.extension.category=value;plan.path.initial_offset=0;return true;
                };
                input.category_context=const_cast<void*>(static_cast<const void*>(&category));
                input.set_category=[](void* p,int value){return (*static_cast<const decltype(category)*>(p))(value);};
                AiRivalActionOutput next;
                check(ordinary.step(input,next)&&next.action.kind==7&&writes==1&&
                      plan.path.extension.category==2&&next.maneuver.kind==1,
                      "return to ordinary action synchronously restores ride category");
                AiManeuverResult expected;
                check(ai_action_plan_path({},plan.path,expected),"restored-category reference path");
                const auto& wanted=clear?trial.geometry.path:expected.path;
                check(next.path.path.count==wanted.path.count&&
                      std::memcmp(next.path.path.points.data(),wanted.path.points.data(),
                          wanted.path.count*sizeof(AiDriverPoint))==0,
                      "accepted ordinary trial keeps old-category geometry; rejected trial rebuilds with ride category");
                plan.path.extension.category=-1;writes=0;
                ++input.tick;snapshot.tick=plan.path.tick=plan.fitness.tick=input.tick;
                check(ordinary.step(input,next)&&writes==0&&plan.path.extension.category==-1,
                      "retained ordinary descriptor does not overwrite a null road category");
            }
            // 44B7E5 updates the road before 44B7F9 resolves a human target.
            // 44D337 can clear that descriptor during this partial update.
            auto partial=controller;auto human=in;auto human_world=world;
            human_world.actors[1].role=1;human.world=&human_world;
            human.motion[1].role=1;
            planner.path.previous.path=result.path;
            AiRivalActionOutput refreshed;
            check(result.maneuver.kind==2||result.maneuver.kind==3,
                  "partial refresh witness starts with a retained side maneuver");
            check(partial.refresh(human,refreshed)&&refreshed.path_published,
                  "present human side target refreshes before road admission changes");
            auto absent=human;AiRoadChain absent_road;AiNeighborhood absent_neighbors;
            absent.road=&absent_road;absent.neighbors=&absent_neighbors;
            const auto absent_seed=rng;const auto side=refreshed.maneuver;
            check(partial.refresh(absent,refreshed)&&!refreshed.path_published&&
                  refreshed.maneuver.kind==side.kind&&refreshed.maneuver.target==side.target&&
                  rng==absent_seed,"missing partial side target keeps descriptor without publishing or consuming RNG");
            human.road_reset=true;
            const auto seed=rng;
            check(partial.refresh(human,refreshed)&&!refreshed.path_published&&
                  refreshed.maneuver.kind==1&&refreshed.maneuver.target==0&&rng==seed,
                  "road reset clears partial side target without a new search or publication");
            auto edge=controller;auto edge_world=world;auto edge_input=in;
            auto edge_planner=planner;auto edge_road=road;
            for(auto& cell:edge_road.cells){cell.lower[0]=-3;cell.upper[0]=3;}
            edge_world.actors[0].absolute_speed=5;
            edge_world.actors[0].signed_speed=5;edge_world.actors[0].velocity={5,0,0};
            edge_world.actors[1].bound_min={-5,-5,-1};
            edge_world.actors[1].bound_max={5,5,1};
            edge_planner.path.roads=edge_road.cells.data();edge_planner.path.extension.margin=1;
            edge_planner.path.extension.absolute_speed=edge_planner.path.policy.absolute_speed=5;
            edge_planner.path.extension.actor_velocity={5,0,0};edge_planner.path.signed_speed=5;
            edge_input.planner=&edge_planner;edge_input.world=&edge_world;edge_input.road=&edge_road;
            ++edge_input.tick;edge_world.tick=edge_planner.path.tick=edge_planner.fitness.tick=edge_input.tick;
            AiRivalActionOutput escaped;
            check(edge.step(edge_input,escaped),"failed pass search produces a road-edge escape");
            check(escaped.action.kind==1&&escaped.maneuver.kind==1&&escaped.path.path.count==3&&
                  escaped.path.path.points[0][1]==-.5F&&escaped.path.path.points[1][1]==-1.F&&
                  escaped.path.path.points[2][1]==-1.5F,
                  "failed low-speed pass uses direct zero-margin road-edge continuation");
        }
        if(special) {
            auto lane_road=road;
            auto lane_planner=planner;
            lane_planner.path.roads=lane_road.cells.data();
            lane_planner.path.policy.special_context=true;
            lane_planner.choice.horizon=3;
            auto lane_world=world;lane_world.prediction_horizon=3;
            lane_world.actors[1].position={50,0,0};
            check(ai_traffic_prediction(lane_world.actors[1].position,b.velocity,b.heading,
                                       lane_world.actors[1].path),"drag crossing fixture prediction");
            lane_planner.targets[0].target.position={50,0,0};
            lane_planner.targets[0].target.distance=50;
            lane_planner.targets[0].target_path=lane_world.actors[1].path;
            auto lane_neighbors=n;lane_neighbors.entries[1].distance=50;
            lane_neighbors.entries[0].lane_lateral[0]=10;
            lane_neighbors.entries[0].lane_lateral[1]=-20;
            auto drag=in;drag.planner=&lane_planner;drag.road=&lane_road;
            drag.world=&lane_world;drag.neighbors=&lane_neighbors;
            drag.drag_strategy=true;drag.nearest_lane=12;
            //454E92 and452E1E choose by neighbor lateral VELOCITY. Give
            //position the opposite sign; both adjacent lanes are clear.
            for(bool obstacle_action:{false,true})for(float side_velocity:{-1.F,1.F}) {
                auto steering_plan=lane_planner;auto steering_world=lane_world;
                auto steering_neighbors=lane_neighbors;auto steering_input=drag;
                auto steering_road=lane_road;AiRoadFlags flags;
                for(auto& enabled:flags.enabled)enabled=true;
                for(auto& cell:steering_road.cells)for(unsigned lane=0;lane<6;++lane) {
                    cell.lower[lane+1]=-100;cell.upper[lane+1]=100;
                    cell.point.lane_offset[lane]=float(int(lane)-2)*20.F;
                    steering_neighbors.entries[0].lane_lateral[lane]=-cell.point.lane_offset[lane];
                    steering_neighbors.entries[1].lane_lateral[lane]=-cell.point.lane_offset[lane];
                }
                steering_neighbors.entries[1].lateral=-side_velocity;
                steering_neighbors.entries[1].relative_velocity[0]=side_velocity;
                steering_plan.path.extension.category=13;
                steering_plan.path.roads=steering_road.cells.data();
                steering_input.road=&steering_road;steering_input.planner=&steering_plan;
                steering_input.world=&steering_world;steering_input.neighbors=&steering_neighbors;
                steering_input.lane_flags=&flags;steering_input.road_reset=true;
                steering_input.force_traffic_action=true;
                steering_input.obstacle_available=obstacle_action;
                if(obstacle_action) {
                    steering_world.actors[1].role=2;
                    steering_input.motion[1].role=2;steering_input.motion[1].race_actor=true;
                }
                AiRivalActionController avoiding;AiRivalActionOutput response;
                check(avoiding.step(steering_input,response)&&response.action.kind==(obstacle_action?19:2)&&
                      response.lane_category==(side_velocity>0?12:14),
                      "traffic and obstacle lane order follows target velocity, not position");
            }
            {
                //45D130 returns false after45CFF0 accepts its nearest lane;
                //454ED6 must see that new category while rebuilding fallback.
                auto plan=lane_planner;auto input=drag;auto snapshot=lane_world;auto cells=lane_road;
                for(auto& cell:cells.cells) {
                    cell.lower[2]=-25;cell.upper[2]=25;cell.point.lane_offset[1]=20;
                }
                plan.path.extension.category=-1;plan.path.roads=cells.cells.data();
                snapshot.actors[1].role=2;
                input.motion[1].role=2;input.motion[1].race_actor=true;
                input.world=&snapshot;input.planner=&plan;input.road=&cells;
                input.obstacle_available=true;input.road_reset=true;
                unsigned writes=0;
                const auto category=[&](int value) {
                    ++writes;plan.path.extension.category=value;plan.path.category_available=true;
                    plan.path.initial_offset=lane_neighbors.entries[0].lane_lateral[unsigned(value-11)];
                    return true;
                };
                input.category_context=const_cast<void*>(static_cast<const void*>(&category));
                input.set_category=[](void* p,int value){return (*static_cast<const decltype(category)*>(p))(value);};
                AiRivalActionController obstacle;AiRivalActionOutput response;
                check(obstacle.step(input,response)&&response.action.kind==19&&
                      writes==1&&response.lane_category==12&&response.speed.speed==0,
                      "obstacle fallback observes synchronous nearest-lane publication");
                AiManeuverResult wanted;
                check(ai_action_plan_path({},plan.path,wanted)&&
                      response.path.path.count==wanted.path.path.count&&
                      response.path.path.points==wanted.path.path.points,
                      "obstacle fallback geometry belongs to its newly admitted lane");
                plan.path.extension.category=-1;plan.path.initial_offset=0;
                input.set_category=nullptr;AiRivalActionController missing_publication;
                check(!missing_publication.step(input,response),
                      "missing category consumer refuses mixed-category obstacle fallback");
            }
            for(bool path_clear:{false,true}) {
                lane_world.actors[1].bound_min[1]=path_clear?-1.F:-5.F;
                lane_world.actors[1].bound_max[1]=path_clear?1.F:5.F;
                lane_neighbors.entries[1].half_extent[0]=path_clear?1.F:5.F;
                lane_planner.targets[0].target.half_width=path_clear?1.F:5.F;
                for(auto& cell:lane_road.cells) {
                    cell.lower[2]=-25;cell.upper[2]=25;
                    cell.point.lane_offset[1]=20.F;
                }
                AiRivalActionController drag_controller;AiRivalActionOutput avoidance;
                check(drag_controller.step(drag,avoidance)&&avoidance.action.kind==2&&
                      avoidance.lane_category==(path_clear?12:-1)&&avoidance.maneuver.kind==1,
                      "traffic avoidance only restores a drag lane when its path is clear");
                if(path_clear)check(avoidance.path.path.count>0&&
                    avoidance.path.path.points[0][1]>-4.F&&avoidance.path.path.points[0][1]<-3.F,
                    "lane candidate starts from body projection, not a teleport to the lane centre");
                // A fresh Driver has no published path, so the strategy's
                // old-path collision query must not invent an avoidance.
                // Action8 must still test its newly generated nearest lane.
                auto fresh_world=lane_world;fresh_world.actors[0].path={};
                auto fresh_input=drag;fresh_input.world=&fresh_world;
                auto fresh_planner=lane_planner;
                fresh_planner.path.extension.category=11;
                for(auto& cell:lane_road.cells) {
                    cell.lower[1]=-25;cell.upper[1]=25;
                    cell.point.lane_offset[0]=-10;
                }
                fresh_input.planner=&fresh_planner;
                AiRivalActionController fresh;AiRivalActionOutput initialized;
                check(fresh.step(fresh_input,initialized)&&initialized.action.kind==8&&
                      initialized.lane_category==(path_clear?12:-1)&&
                      initialized.evaluations==1&&initialized.maneuver.kind==1&&
                      initialized.path.path.count>0,
                      "fresh drag driver probes nearest lane before committing, or keeps its fallback lane");
            }
            {
                auto bend_road=lane_road;
                for(unsigned j=0;j<4;++j)bend_road.cells[j].point.position[0]=20.F*j;
                auto bend_world=lane_world;bend_world.count=1;bend_world.eligible_count=0;
                bend_world.actors[0].position={4,0,0};bend_world.actors[0].path={};
                auto bend_neighbors=lane_neighbors;bend_neighbors.count=1;bend_neighbors.entries[0].cell=0;
                auto bend_planner=lane_planner;bend_planner.path.roads=bend_road.cells.data();
                bend_planner.path.extension.actor_position={4,0,0};
                bend_planner.path.selected=1; // first-ahead locator, not owner entry
                auto bend_input=drag;bend_input.world=&bend_world;bend_input.neighbors=&bend_neighbors;
                bend_input.road=&bend_road;bend_input.planner=&bend_planner;
                AiRivalActionController bend;AiRivalActionOutput path;
                check(bend.step(bend_input,path)&&path.action.kind==8&&path.path.path.count>0&&
                      path.path.path.points[0][0]==20.F,
                      "nearest-lane path starts after neighborhood cell, not after first-ahead cell");
                bend_input.wall_contact=true;bend_input.nearest_owner_lane=11;
                AiRivalActionController wall;
                check(wall.step(bend_input,path)&&path.action.kind==16&&path.lane_category==11&&
                      path.path.path.count>0&&path.path.path.points[0][0]==20.F,
                      "wall action uses neighborhood raw-point lane rather than context raw-point lane");
            }
            in.finished=true;
            in.traffic_continuation=false;
            ++in.tick;world.tick=planner.path.tick=planner.fitness.tick=in.tick;
            check(controller.step(in,result)&&result.action.kind==17&&result.speed.ready&&result.speed.speed==0,
                  "finishing on the action completion tick installs the stop action");
            ++in.tick;world.tick=planner.path.tick=planner.fitness.tick=in.tick;
            check(controller.step(in,result)&&result.action.kind==17&&result.speed.speed==0,
                  "finished rival keeps its stop action on subsequent updates");
        }
        if(!special)for(int category=11;category<=16;++category) {
            for(auto& cell:road.cells)for(unsigned slot=0;slot<7;++slot) {
                cell.lower[slot]=-100;cell.upper[slot]=100;
            }
            RoutePoint ahead=road.cells[3].point;ahead.position[0]=10;
            ahead.lane_offset[unsigned(category-11)]=50;
            AiRecoveryContact contact{{0,1},0};AiRecoveryInput recovery;
            recovery.tick=100000;recovery.road=&road.cells[0].point;
            recovery.minimum_x=-1;recovery.maximum_x=1;
            recovery.contacts=&contact;recovery.contact_count=1;
            recovery.motion.body_direction={1,0};
            in.drag_strategy=true;in.nearest_lane=category;in.tick=100000;
            in.recovery=&recovery;in.recovery_ahead=&ahead;in.recovery_selected=&road.cells[0];
            planner.path.extension.category=category;planner.path.tick=in.tick;world.tick=in.tick;
            AiRivalActionController stuck;AiRivalActionOutput reversing;
            check(stuck.step(in,reversing)&&reversing.action.kind==10&&reversing.speed.speed==-15&&
                  !reversing.speed.catchup&&!reversing.speed.route_speed,
                  "stuck drag rival can construct a reverse path on every lane");
            // A ride-grade goal points ahead, but this lane goal is50m to
            // the side. Only the road category can decide completion here.
            recovery.contacts=nullptr;recovery.contact_count=0;
            in.tick+=6000;world.tick=in.tick;planner.path.tick=in.tick;recovery.tick=in.tick;
            check(stuck.step(in,reversing)&&reversing.action.kind==10,
                  "drag recovery completion uses its lane rather than ride grade");
            if(category==11) {
                auto input=in;auto plan=planner;auto snapshot=world;auto state=recovery;
                snapshot.eligible_count=0;input.world=&snapshot;input.planner=&plan;input.recovery=&state;
                input.finished=false;input.tick=state.tick=snapshot.tick=plan.path.tick=plan.fitness.tick=100000;
                state.contacts=&contact;state.contact_count=1;
                AiPathRoadPoint raw=road.cells[0];raw.point.route_index=42;raw.point_index=7;
                RoutePoint new_goal=raw.point;new_goal.position[0]=50;
                input.recovery_selected=&raw;input.recovery_ahead=&new_goal;
                AiRivalActionController resetting;AiRivalActionOutput publication;
                unsigned resets=0;bool order=true;int selected=-1;
                const auto reset=[&]() {
                    order=order&&resetting.current().kind==(resets?10:8)&&
                        resetting.road_requires_lane(true)==(resets==0);
                    ++resets;input.recovery_hold_seconds=2.25F;
                    if(!resetting.reselect_category(input,selected))return false;
                    order=order&&selected==(resets==1?11:0);
                    plan.path.extension.category=selected;
                    return true;
                };
                input.road_context=const_cast<void*>(static_cast<const void*>(&reset));
                input.reset_road=[](void* p){return (*static_cast<const decltype(reset)*>(p))();};
                check(resetting.step(input,publication)&&resets==1&&order&&
                      publication.action.kind==10&&publication.path.path.points[0][0]==50&&
                      publication.path.route[0]==42&&publication.path.point[0]==7,
                      "recovery query selects new raw road before construction and path publication");
                state.contacts=nullptr;state.contact_count=0;
                input.tick=state.tick=106000;
                check(resetting.step(input,publication)&&publication.action.kind==10&&resets==1,
                      "recovery constructor consumes hold duration after synchronous reselection");
                input.tick=state.tick=110000;
                check(resetting.step(input,publication)&&publication.action.kind!=10&&resets==2&&order,
                      "recovery cleanup uses old action ride category before replacement factory");

                //A finish can release recovery just as44C250 loses the raw
                //road.17 must still evaluate retained-neighbor side paths.
                AiRivalActionController finished_reset;unsigned finish_resets=0;
                input.finished=false;input.tick=state.tick=100000;
                state.contacts=&contact;state.contact_count=1;state.selected_road=true;
                input.recovery_ahead=&new_goal;input.recovery_selected=&raw;
                plan.path.extension.category=11;plan.path.extension.raw_road_available=true;
                plan.path.category_available=true;
                const auto finish_road=[&]() {
                    if(++finish_resets==2) {
                        state.selected_road=false;input.recovery_ahead=nullptr;input.recovery_selected=nullptr;
                        plan.path.extension.category=-1;plan.path.extension.raw_road_available=false;
                        plan.path.category_available=false;
                        for(unsigned j=0;j<plan.target_count;++j) {
                            plan.targets[j].self.lateral=plan.targets[j].target.lateral=0;
                        }
                    }
                    return true;
                };
                input.road_context=const_cast<void*>(static_cast<const void*>(&finish_road));
                input.reset_road=[](void* p){return (*static_cast<const decltype(finish_road)*>(p))();};
                check(finished_reset.step(input,publication)&&publication.action.kind==10&&finish_resets==1,
                      "finished recovery fixture constructs action10 with a road");
                input.finished=true;state.contacts=nullptr;state.contact_count=0;
                input.tick=state.tick=snapshot.tick=plan.path.tick=plan.fitness.tick=110000;
                check(finished_reset.step(input,publication)&&publication.action.kind==17&&
                      publication.speed.ready&&publication.speed.speed==0&&finish_resets==2&&publication.evaluations>1,
                      "finished recovery with missing raw road selects a ready stop action through retained neighbors");

                AiRivalActionController absent;unsigned absent_resets=0;
                state.contacts=&contact;state.contact_count=1;state.selected_road=true;
                input.finished=false;input.tick=state.tick=snapshot.tick=plan.path.tick=plan.fitness.tick=100000;
                input.recovery_ahead=&new_goal;input.recovery_selected=&raw;
                plan.path.extension.category=11;plan.path.extension.raw_road_available=true;
                plan.path.category_available=true;
                const auto no_road=[&]() {
                    ++absent_resets;state.selected_road=false;input.recovery_ahead=nullptr;
                    input.recovery_selected=nullptr;plan.path.extension.category=-1;
                    plan.path.extension.raw_road_available=false;plan.path.category_available=false;
                    return true;
                };
                input.road_context=const_cast<void*>(static_cast<const void*>(&no_road));
                input.reset_road=[](void* p){return (*static_cast<const decltype(no_road)*>(p))();};
                unsigned category_writes=0;
                const auto write_category=[&](int value) {
                    ++category_writes;plan.path.extension.category=value;return true;
                };
                input.category_context=const_cast<void*>(static_cast<const void*>(&write_category));
                input.set_category=[](void* p,int value){return (*static_cast<const decltype(write_category)*>(p))(value);};
                check(absent.step(input,publication)&&absent_resets==1&&publication.action.kind==4&&
                      category_writes==1&&plan.path.extension.category==input.ride_category&&
                      publication.speed.ready&&publication.path.path.count==0,
                      "failed recovery raw search updates fallback once without cleanup or invalid-category failure");
            }
        }
    }

    for(const auto& c:strategy_virtuals) {
        const auto kind=static_cast<AiStrategyKind>(c.kind);
        check(ai_strategy_rebuild_path(kind,false)==c.rebuild&&
            ai_strategy_traffic_admitted(true,true,3,kind,false)==c.traffic,
            "actual original strategy virtuals, not a role-based replacement");
    }
    for(const auto& c:traffic_admission) {
        const auto kind=static_cast<AiStrategyKind>(c.kind);
        check(ai_strategy_traffic_admitted(c.present,true,c.role,kind,c.force)==(c.priority>=0)&&
            c.target==(c.priority>=0),"native452B40 traffic query admission");
    }

    // Player-driven races enter flow 4. Only a human target can trigger
    // the horn/flash draws in this flow, including when the pass succeeds.
    for(int role:{1,2}) {
        AiReactionInput input;
        input.tick=500000;
        input.target_role=role;
        AiReactionState state;
        std::uint32_t seed=12345;
        check(ai_strategy_react(input,state,seed)&&
              (role==1 ? state.tick==input.tick&&seed!=12345 : state.tick==0&&seed==12345),
              "playable race consumes horn/flash RNG only for a human target");
    }
    for(const auto& c:reactions) {
        auto state=c.before;auto rng=c.seed;
        check(ai_strategy_react(c.input,state,rng)&&state.tick==c.after.tick&&
            std::memcmp(&state.first,&c.after.first,3*sizeof(float))==0&&
            state.flash==c.after.flash&&state.horn==c.after.horn&&state.direction==c.after.direction&&
            rng==c.after_seed,"native reaction state and shared random sequence");
    }
    for(const auto& c:reaction_outputs) {
        const auto out=ai_reaction_output(c.state,c.tick,c.manual);
        check(out.flash==c.flash&&out.horn==c.horn,"native post-physics reaction output");
    }

    for(unsigned mode=0;mode<3;++mode) {
        AiBlockingMode selected;
        check(ai_strategy_blocking_mode(mode,selected)&&selected.priority==modes[mode].priority&&
            selected.subtract_lengths==modes[mode].subtract_lengths,"native per-rival configuration736 mode");
    }
    AiBlockingMode bad_mode{77,false};
    check(!ai_strategy_blocking_mode(3,bad_mode)&&bad_mode.priority==77,"invalid mode preserves output");
    unsigned index=0;
    for(const auto& c:blocks) {
        AiRoadChain chain;chain.count=1;chain.cells[0].lower[0]=-5;chain.cells[0].upper[0]=5;
        AiNeighborhood n;n.count=2;n.self_index=0;n.entries[0].distance=100;n.entries[0].half_extent[1]=2;
        auto& e=n.entries[1];e.distance=100+c.gap;e.relative_velocity[1]=c.relative;
        e.lateral=c.lateral;e.half_extent[1]=2;e.id=2;
        AiBlockingInput in;in.race_time_hold=c.hold;in.mode_priority=c.mode_priority;
        in.subtract_lengths=c.subtract_lengths;in.actors[1]={true,c.human,c.speed};
        AiActionCandidate result;
        check(ai_strategy_blocking(chain,n,in,result),"blocking input admitted");
        if(result.priority!=c.priority||result.target!=(c.found?2U:0U)) {
            ++failures;std::cerr<<"FAIL: blocking case "<<index<<" priority "<<result.priority<<" expected "
                <<c.priority<<" target "<<result.target<<" expected "<<c.found<<'\n';
        }
        ++index;
    }
    for(const auto& c:rolls) {
        bool request=false;
        check(ai_strategy_rollover(c.contacts,c.up,request)&&request==c.request,"original rollover admission");
    }
    for(const auto& c:blocks) {
        // Original450EC0 inputs above, now entering through the real lazy
        // strategy and action owner. No eligible predicted collision is
        // supplied, so the earlier avoidance queries produce no candidate.
        AiRoadChain road;road.count=4;
        for(unsigned j=0;j<road.count;++j) {
            auto& cell=road.cells[j];cell.point.position[0]=20.F+20.F*j;
            cell.point.forward[0]=1;cell.point.unknown_24=cell.point.unknown_28=10;
            cell.point.distance_to_next=cell.point.distance_to_previous=20;
            cell.distance=20.F*j;cell.lower[0]=-5;cell.upper[0]=5;
        }
        AiNeighborhood neighbors;neighbors.count=2;neighbors.self_index=0;
        neighbors.entries[0].id=1;neighbors.entries[0].distance=100;neighbors.entries[0].half_extent={1,2};
        auto& other=neighbors.entries[1];other.id=2;other.distance=100+c.gap;
        other.lateral=c.lateral;other.half_extent={1,2};other.relative_velocity[1]=c.relative;
        AiPathCollisionWorld world;world.count=2;world.tick=500000;
        world.actors[0].id=1;world.actors[0].role=2;world.actors[0].signed_speed=20;
        world.actors[0].absolute_speed=20;world.actors[0].forward={1,0,0};world.actors[0].velocity={20,0,0};
        world.actors[1].id=2;world.actors[1].role=c.human?1:2;
        world.actors[1].absolute_speed=c.speed;world.actors[1].velocity={c.speed,0,0};
        AiPathPlannerInput planner;planner.path.roads=road.cells.data();planner.path.road_count=road.count;
        planner.path.tick=world.tick;planner.path.rebuild=true;planner.path.category_available=true;
        planner.path.signed_speed=20;planner.path.policy.absolute_speed=20;
        planner.path.extension.actor_velocity={20,0,0};planner.path.extension.absolute_speed=20;
        planner.path.extension.clearance=1;planner.choice.horizon=2;planner.fallback_margin=1;
        AiRivalActionInput input;input.planner=&planner;input.world=&world;
        input.neighbors=&neighbors;input.road=&road;input.tick=world.tick;
        input.race_seconds=10;input.hold=c.hold;input.mode_priority=c.mode_priority;
        input.subtract_lengths=c.subtract_lengths;input.actors[0]={true,false,20};
        input.actors[1]={true,c.human,c.speed};input.motion[0]={1,true,2,{20,0,0}};
        input.motion[1]={2,true,c.human?1:2,{c.speed,0,0}};
        std::uint32_t random=12345;input.random=&random;
        AiRivalActionController controller;AiRivalActionOutput result;
        check(controller.step(input,result)&&result.action.kind==(c.found?5:7)&&
            result.action.priority==(c.found?c.priority:0)&&result.target==(c.found?2U:0U),
            "native blocking decision through connected production controller");
        const auto handle=controller.current().handle,seed=random;
        planner.path.previous.path=result.path;
        const auto retained=result.path.path;
        ++input.tick;world.tick=input.tick;planner.path.tick=input.tick;
        check(controller.refresh(input,result)&&controller.current().handle==handle&&random==seed,
              "partial controller update does not select actions or consume strategy RNG");
        if(!c.found)check(result.path.path.count==retained.count&&result.path.path.points==retained.points,
                         "ordinary default keeps last driver path between full updates");
        else {
            for(int category:{-1,13}) {
                auto lane_controller=controller;auto lane_input=input;auto lane_plan=planner;
                auto lane_neighbors=neighbors;
                lane_plan.path.extension.category=category;
                lane_neighbors.entries[1].lateral=-9;
                lane_neighbors.entries[1].lane_lateral[2]=9;
                lane_input.planner=&lane_plan;lane_input.neighbors=&lane_neighbors;
                AiRivalActionOutput blocking;
                check(lane_controller.refresh(lane_input,blocking)&&blocking.path_published,
                    "blocking refresh admits null and lane categories");
                auto extension=lane_plan.path.extension;
                extension.mode=1;extension.offset_step=1;
                extension.target_offset=category<0?0.F:9.F;
                extension.margin=input.subtract_lengths?1.F:0.F;
                AiPlannedPath expected;
                const float gap=other.distance-neighbors.entries[0].distance;
                if(gap>=10.F&&std::fabs(other.relative_velocity[1])>.001F) {
                    const float time=gap/other.relative_velocity[1];
                    extension.offset_step=time<=-20.F?.1F:time>=20.F?-.1F:.1F+(time+20.F)*(-.2F/40.F);
                }
                check(ai_path_continue(lane_plan.path.roads,lane_plan.path.road_count,
                    lane_plan.path.selected,lane_plan.path.initial_offset,extension,expected)&&
                    blocking.path.path.count==expected.path.count&&blocking.path.path.points==expected.path.points,
                    "blocking path uses active category lateral instead of cached ride-grade lateral");
            }
            AiRoadChain empty_road;AiNeighborhood empty_neighbors;
            auto empty_input=input;empty_input.road=&empty_road;empty_input.neighbors=&empty_neighbors;
            check(controller.refresh(empty_input,result)&&result.path_published&&result.path.path.count==0&&
                  result.speed.route_speed&&result.speed.enabled&&result.speed.catchup&&
                  result.speed.auxiliary==2&&controller.current().handle==handle&&random==seed,
                  "blocking partial update publishes empty scratch path and retains route speed policy without RNG");
        }
    }
    index=0;
    for(const auto& c:gates) {
        AiManeuverGate s{c.previous,c.last};auto seed=c.seed;bool allow=false;
        check(ai_strategy_allow_target(c.target,c.tick,c.cooldown,c.probability,s,seed,allow),"gate admitted");
        if(allow!=c.allow||seed!=c.after_seed||s.rejected_target!=c.after_target||s.rejected_tick!=c.after_tick) {
            ++failures;std::cerr<<"FAIL: gate case "<<index<<" allow "<<allow<<" seed "<<seed
                <<" target "<<s.rejected_target<<" tick "<<s.rejected_tick<<'\n';
        }
        ++index;
    }
    // Actual44D8B0 call order, not a descending-priority sort. Evaluators here
    // exercise dispatch only; unported geometric producers are not simulated.
    AiStrategySelection in;in.recovery_enabled=true;in.mode_priority=250;
    AiActionCandidate selected;Queries q;q.found=AiStrategyQuery::recovery;
    check(ai_strategy_choose(in,evaluate,&q,selected)&&selected.kind==10&&q.count==1,"recovery runs before obstacle");
    q={};q.found=AiStrategyQuery::obstacle;in.current_kind=10;
    check(ai_strategy_choose(in,evaluate,&q,selected)&&selected.kind==19&&q.count==1,"current action query skipped");
    q={};q.found=AiStrategyQuery::blocking;in.current_kind=20;in.recovery_enabled=false;
    check(ai_strategy_choose(in,evaluate,&q,selected)&&selected.kind==5&&q.count==6,"lazy blocking selection");
    const AiStrategyQuery order[]={AiStrategyQuery::obstacle,AiStrategyQuery::contact,AiStrategyQuery::traffic,
        AiStrategyQuery::passing,AiStrategyQuery::rollover,AiStrategyQuery::blocking};
    for(unsigned j=0;j<6;++j)check(q.calls[j]==order[j],"native dispatch order");
    q={};in.current_priority=501;
    check(ai_strategy_choose(in,evaluate,&q,selected)&&selected.kind==7&&q.count==1,"lower priority queries not evaluated");
    q={};q.refuse=true;in.current_priority=-1;selected={12,400,77};
    check(!ai_strategy_choose(in,evaluate,&q,selected)&&selected.kind==12&&selected.target==77,"missing producer does not fabricate fallback");
    check(!ai_strategy_preempts(true,400,{12,400,0})&&ai_strategy_preempts(true,399,{12,400,0})&&
        !ai_strategy_preempts(false,-1,{12,400,0}),"automatic replacement strict priority/current ownership");
    // A geometrically rejected first eligible actor must not allow selection
    // of a later actor that would independently meet the blocking criteria.
    AiRoadChain chain;chain.count=1;chain.cells[0].lower[0]=-5;chain.cells[0].upper[0]=5;
    AiNeighborhood n;n.count=3;n.self_index=0;n.entries[0].distance=100;
    n.entries[1].distance=90;n.entries[1].lateral=6;n.entries[1].id=2;
    n.entries[2].distance=95;n.entries[2].id=3;
    AiBlockingInput block;block.mode_priority=250;block.actors[1]={true,true,20};block.actors[2]={true,true,20};
    check(ai_strategy_blocking(chain,n,block,selected)&&selected.priority==-1,"first eligible actor retained on geometric rejection");
    block.actors[1].race_actor=false;
    check(ai_strategy_blocking(chain,n,block,selected)&&selected.target==3,"non-race entry skipped");
    std::cout<<"Original strategy and reaction: 15 blocking, 8 rollover, 7 target gate cases; failures="<<failures<<'\n';
    return failures?1:0;
}
