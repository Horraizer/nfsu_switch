#include "nfsu/ai_strategy.h"
#include "nfsu/ai_rival_action.h"
#include <iostream>
#include <cstring>
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
        planner.target_count=1;planner.target_ids[0]=2;planner.target_available[0]=true;
        auto& moving=planner.targets[0];moving.self={a.position,a.velocity,0,1,0,0,2,true,false};
        moving.target={b.position,b.velocity,0,1,10,0,3,true,false};
        moving.policy={4,5,5,2,1.5F,5,2,.707099974155426F};moving.self_signed_speed=20;moving.target_path=b.path;
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
            in.recovery=&recovery;in.recovery_ahead=&ahead;
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
