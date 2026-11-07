#include "nfsu/ai_road.h"
#include <cstring>
#include <algorithm>
#include <iostream>
#include <limits>
#include "drag_steering_native.inc"
using namespace nfsu;
namespace {
struct Decision {
    unsigned id;AiRoadPolicyInput input;std::uint32_t seed;bool take;
    std::uint32_t rng;std::array<std::uint32_t,8> cache;int marker;
};
struct Topology {
    int route;unsigned point,count;bool reverse;std::array<std::uint8_t,2> events;
    AiRoadPolicyInput input;std::uint32_t seed;bool advanced;AiRoadCursor target;
    unsigned target_count;bool junction;std::uint32_t rng;std::array<std::uint32_t,8> cache;int marker;
};
struct Expiry {float progress,length;bool circuit,same_direction;float threshold;std::array<std::uint32_t,8> before,after;};
#include "ai_road_native.inc"
#include "traffic_junction_native.inc"
#include "traffic_road_policy_native.inc"
#include "traffic_speed_native.inc"
#include "ai_context_horizon_native.inc"
#include "drag_mechanics_native.inc"
#include "traffic_lane_permission_native.inc"
struct RawRoad {int route;unsigned point,count;bool reverse;std::array<std::uint8_t,2> events;float v[21];};
struct ChainCell {int route;unsigned point;std::uint32_t distance;bool junction;std::array<std::uint32_t,7> offset,lower,upper;};
struct ChainCase {int route;unsigned point;int category;float horizon;std::array<float,7> offset;AiRoadPolicyInput input;
                  unsigned count;ChainCell cells[40];std::uint32_t rng;std::array<std::uint32_t,8> cache;int marker;float curvature;};
struct NearQuery {unsigned source;AiDriverPoint position;unsigned flags;int selected;};
struct CurveQuery {unsigned source,first;int category;float distance;std::uint32_t value;};
struct LocateQuery {unsigned source;AiDriverPoint position;int category;unsigned cell;std::uint32_t distance,lateral;};
struct Refresh {unsigned source;bool cold;int route;unsigned point;float horizon;AiDriverPoint position;
    std::uint32_t before_rng;std::array<std::uint32_t,8> before_cache;int before_marker;
    unsigned count;ChainCell cells[40];std::uint32_t rng;std::array<std::uint32_t,8> cache;int marker;};
#include "ai_chain_native.inc"
std::uint32_t float_bits(float x){std::uint32_t v;std::memcpy(&v,&x,4);return v;}
unsigned failures=0;
void check(bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
}
int main(){
    const auto real=[](unsigned bits){float value;std::memcpy(&value,&bits,4);return value;};
    unsigned traffic_speed_row=0,traffic_speed_differences=0;
    for(const auto& row:kNativeTrafficSpeed) {
        const auto& profile=kNativeTrafficSpeedProfiles[row[0]];
        AiRoadChain chain;chain.count=4;
        for(unsigned i=0;i<4;++i) {
            chain.cells[i].distance=real(profile[i]);chain.cells[i].point.unknown_30=real(profile[4+i]);
            chain.cells[i].lower[4]=real(profile[8+i]); // decoy prevents wrong-object getter wiring
            chain.cells[i].point.speed[row[1]]=std::uint8_t(profile[12+i]);
        }
        AiTrafficSpeedInput in;in.category=int(row[1]);in.first=row[2];in.distance=real(row[3]);
        in.absolute_speed=real(row[4]);in.cap=real(row[5]);in.stopped=row[6]!=0;in.route_speed=row[7]!=0;
        in.action={{real(row[8]),real(row[9]),real(row[10])}};
        std::array<float,3> output{};
        bool equal=ai_traffic_speed(chain,in,output);
        for(unsigned i=0;i<3;++i)equal&=float_bits(output[i])==row[11+i];
        if(!equal&&traffic_speed_differences++<8) {
            std::cerr<<"traffic speed row "<<traffic_speed_row<<" profile "<<row[0]<<" first "<<in.first
                <<" d="<<in.distance<<" speed="<<in.absolute_speed<<" cap="<<in.cap<<" stop="<<in.stopped
                <<" route="<<in.route_speed<<" got "<<std::hex<<float_bits(output[0])<<','<<float_bits(output[1])<<','<<float_bits(output[2])
                <<" expected "<<row[11]<<','<<row[12]<<','<<row[13]<<std::dec<<'\n';
        }
        ++traffic_speed_row;
        check(equal,"actual583910 geometric curve, interpolated road speeds and action publication");
    }
    for(const auto& row:kNativeTrafficRoadPolicy) {
        AiRoadEvent event;event.direction=std::uint8_t(row[1]);event.type=std::uint8_t(row[2]);
        event.traffic_type=std::uint8_t(row[3]);event.disabled=1;event.marker=2;
        AiRoadPolicyInput in;in.direction=int(row[0]);in.policy=AiRoadDecisionPolicy::traffic;
        AiRoadPolicy state;state.marker=42;
        for(unsigned i=0;i<8;++i)state.cache[i]=0x43210000U+(i<<10);
        auto rng=row[4];bool take=true;
        for(unsigned call=0;call<2;++call) {
            const unsigned at=5+call*11;
            bool equal=ai_road_decide(event,3,in,state,rng,take)&&take==bool(row[at])&&
                rng==row[at+1]&&state.marker==int(row[at+10]);
            for(unsigned i=0;i<8;++i)equal&=state.cache[i]==row[at+2+i];
            check(equal,"actual traffic virtual policy, shared RNG, replay and cache side effects");
        }
    }
    for(const auto& row:kNativeTrafficJunctions) {
        auto rng=row[1];
        check(ai_traffic_road_decide(row[0],rng)==bool(row[2])&&rng==row[3],
            "original583BB0 traffic strict threshold and world RNG");
    }
    std::vector<AiRoadEvent> events(std::begin(original_events),std::end(original_events));
    for(const auto& c:decisions){
        AiRoadPolicy state;auto rng=c.seed;bool take=!c.take;
        check(ai_road_decide(events[c.id],c.id,c.input,state,rng,take)&&take==c.take&&
            rng==c.rng&&state.cache==c.cache&&state.marker==c.marker,"original decision, RNG and packed cache");
        check(ai_road_decide(events[c.id],c.id,c.input,state,rng,take)&&take==c.take&&
            rng==c.rng&&state.cache==c.cache&&state.marker==c.marker,"cached decision consumes no new random value");
    }
    for(const auto& c:topology){
        RacingLineTrack track;Route route;route.index=static_cast<std::int16_t>(c.route);
        route.points.resize(c.count);route.points[c.point].event_a=c.events[0];route.points[c.point].event_b=c.events[1];
        track.routes.push_back(route);
        if(c.target.route!=c.route){Route target;target.index=c.target.route;target.points.resize(c.target_count);track.routes.push_back(target);}
        AiRoadPolicy state;auto rng=c.seed;AiRoadCursor cursor{static_cast<std::int16_t>(c.route),static_cast<std::uint16_t>(c.point)};
        bool junction=false;const auto result=ai_road_next(track,events,c.reverse,c.input,state,rng,cursor,junction);
        check(result==(c.advanced?AiRoadAdvance::advanced:AiRoadAdvance::end)&&rng==c.rng&&
              state.cache==c.cache&&state.marker==c.marker,"original road topology and decision side effects");
        if(c.advanced)check(cursor.route==c.target.route&&cursor.point==c.target.point&&junction==c.junction,
                           "original destination road, point and junction");
    }
    for(const auto& c:expiry){
        AiRoadPolicy state;state.cache=c.before;
        check(ai_road_expire(events,c.progress,c.length,c.circuit,c.same_direction,c.threshold,state)&&state.cache==c.after,
              "original decision expiry across lap wrap and reverse direction");
    }
    RacingLineTrack world;std::array<bool,128> reverse{};
    for(const auto& r:raw_roads){
        auto it=std::find_if(world.routes.begin(),world.routes.end(),[&](const Route& route){return route.index==r.route;});
        if(it==world.routes.end()){Route route;route.index=static_cast<std::int16_t>(r.route);route.points.resize(r.count);
            world.routes.push_back(route);it=world.routes.end()-1;}
        reverse[r.route]=r.reverse;auto& p=it->points[r.point];p.route_index=static_cast<std::uint8_t>(r.route);
        p.position[0]=r.v[0];p.position[1]=r.v[1];p.height=r.v[2];p.forward[0]=r.v[3];p.forward[1]=r.v[4];
        p.distance_to_next=r.v[5];p.unknown_24=r.v[6];p.unknown_28=r.v[7];p.unknown_2c=r.v[8];p.unknown_30=r.v[9];
        for(unsigned j=0;j<5;++j)p.driving_offset[j]=r.v[10+j];
        for(unsigned j=0;j<6;++j)p.lane_offset[j]=r.v[15+j];
        p.event_a=r.events[0];p.event_b=r.events[1];
    }
    std::array<AiRoadChain,std::size(chains)> prepared;
    unsigned chain_index=0;
    for(const auto& c:chains){
        AiRoadChain chain;chain.count=1;chain.cells[0].point=world.find(static_cast<std::int16_t>(c.route))->points[c.point];
        chain.cells[0].point_index=static_cast<std::uint16_t>(c.point);chain.cells[0].offset=c.offset;
        AiRoadPolicy state;std::uint32_t rng=0x60000000U;
        check(ai_road_extend(world,events,reverse,c.category,c.horizon,c.input,state,rng,chain),"original chain growth supported");
        check(chain.count==c.count&&rng==c.rng&&state.cache==c.cache&&state.marker==c.marker,"original chain length and decision state");
        for(unsigned j=0;j<std::min(chain.count,c.count);++j){
            const auto& a=chain.cells[j];const auto& b=c.cells[j];
            bool same=a.point.route_index==b.route&&a.point_index==b.point&&float_bits(a.distance)==b.distance&&chain.junction[j]==b.junction;
            for(unsigned k=0;k<7;++k)same=same&&float_bits(a.offset[k])==b.offset[k];
            if(!same){for(unsigned k=0;k<7;++k)if(float_bits(a.offset[k])!=b.offset[k])std::cerr<<"offset "<<k<<" "<<std::hex<<float_bits(a.offset[k])<<" expected "<<b.offset[k]<<std::dec<<" ";}
            if(!same)std::cerr<<"chain "<<chain_index<<" cell "<<j<<" distance "<<std::hex<<float_bits(a.distance)<<" expected "<<b.distance<<std::dec<<'\n';
            check(same,"original emitted road coordinates, distance, offsets and junction");
        }
        check(ai_road_propagate_bounds(chain,c.category,c.curvature),"original backward width propagation supported");
        for(unsigned j=0;j<std::min(chain.count,c.count);++j)for(unsigned k=0;k<7;++k){
            const auto& a=chain.cells[j];const auto& b=c.cells[j];
            const bool same=float_bits(a.lower[k])==b.lower[k]&&float_bits(a.upper[k])==b.upper[k];
            if(!same)std::cerr<<"bounds chain "<<chain_index<<" cell "<<j<<" lane "<<k<<" curve "<<c.curvature<<'\n';
            check(same,"original ordinary and six-lane backward width bounds");
        }
        const auto saved=chain;
        check(!ai_road_extend(world,events,reverse,7,c.horizon,c.input,state,rng,chain)&&chain.count==saved.count,
              "unsupported category preserves the completed road chain");
        check(!ai_road_propagate_bounds(chain,c.category,std::numeric_limits<float>::quiet_NaN())&&
              float_bits(chain.cells[0].lower[0])==float_bits(saved.cells[0].lower[0]),"invalid curvature preserves prior bounds");
        prepared[chain_index]=chain;
        ++chain_index;
    }
    for(const auto& q:near_queries){
        int selected=-2;check(ai_road_nearest(prepared[q.source],q.position,0,q.flags,selected)&&selected==q.selected,
                             "original nearest road with forward/backward filters");
    }
    for(const auto& q:curve_queries){
        float value=0;const bool ok=ai_road_curvature(prepared[q.source],q.first,q.category,q.distance,value);
        if(!ok||float_bits(value)!=q.value)std::cerr<<"curve source "<<q.source<<" first "<<q.first<<" distance "<<q.distance<<" bits "<<std::hex<<float_bits(value)<<" expected "<<q.value<<std::dec<<'\n';
        check(ok&&float_bits(value)==q.value,"original signed road curvature and horizon selection");
    }
    for(const auto& q:locate_queries){
        AiRoadLocation location;
        const bool ok=ai_road_locate(prepared[q.source],q.position,q.category,location);
        const bool same=ok&&location.cell==q.cell&&float_bits(location.distance)==q.distance&&float_bits(location.lateral)==q.lateral;
        if(!same)std::cerr<<"locate source "<<q.source<<" cell "<<location.cell<<" expected "<<q.cell<<" distance "<<std::hex<<float_bits(location.distance)<<" expected "<<q.distance<<" lateral "<<float_bits(location.lateral)<<" expected "<<q.lateral<<std::dec<<'\n';
        check(same,"original retained-chain position and authored-lane projection");
    }
    for(const auto& c:refresh_cases){
        auto chain=c.cold?AiRoadChain{}:prepared[c.source];const auto& parent=chains[c.source];
        AiRoadPolicy state;state.cache=c.before_cache;state.marker=c.before_marker;auto rng=c.before_rng;
        check(ai_road_refresh(world,events,reverse,{static_cast<std::int16_t>(c.route),static_cast<std::uint16_t>(c.point)},
              parent.category,c.position,true,c.horizon,c.horizon,parent.input,state,rng,chain),"original road context refresh supported");
        check(chain.count==c.count&&rng==c.rng&&state.cache==c.cache&&state.marker==c.marker,"original refreshed chain and decision state");
        for(unsigned j=0;j<std::min(chain.count,c.count);++j){
            const auto& a=chain.cells[j];const auto& b=c.cells[j];
            bool same=a.point.route_index==b.route&&a.point_index==b.point&&float_bits(a.distance)==b.distance&&chain.junction[j]==b.junction;
            for(unsigned k=0;k<7;++k)same=same&&float_bits(a.offset[k])==b.offset[k]&&float_bits(a.lower[k])==b.lower[k]&&float_bits(a.upper[k])==b.upper[k];
            if(!same)std::cerr<<"refresh "<<c.source<<" cold "<<c.cold<<" cell "<<j<<'\n';
            check(same,"original full refreshed road cells, offsets and bounds");
        }
    }
    unsigned char raw[32]{};float progress=123.5F;std::uint32_t bits;std::memcpy(&bits,&progress,4);
    for(unsigned k=0;k<4;++k)raw[k]=static_cast<unsigned char>(bits>>(8*k));
    raw[4]=2;raw[5]=1;raw[8]=2;raw[9]=35;raw[11]=4;raw[12]=2;raw[13]=1;raw[15]=1;
    std::vector<AiRoadEvent> parsed;
    check(ai_road_events(raw,32,parsed)&&parsed.size()==1&&parsed[0].progress==progress&&parsed[0].target_point==258&&
          parsed[0].marker==2&&parsed[0].target_route==35&&parsed[0].type==4&&parsed[0].traffic_type==2&&parsed[0].direction==1&&parsed[0].disabled==1,
          "connection record decoding retains original fields");
    check(!ai_road_events(raw,31,parsed)&&parsed.size()==1,"truncated connection table is transactional");
    raw[9]=128;check(!ai_road_events(raw,32,parsed)&&parsed[0].target_route==35,"bad route index is transactional");
    AiRoadPolicy state;state.cache.fill(1);auto rng=123U;bool take=true;AiRoadPolicyInput input;AiRoadEvent event;event.type=4;
    const auto before=state.cache;
    check(!ai_road_decide(event,8,input,state,rng,take)&&state.cache==before&&rng==123&&take,"full cache refuses without partial RNG update");
    state={};event.type=9;
    check(!ai_road_decide(event,0,input,state,rng,take)&&state.cache[0]==0&&rng==123,"unimplemented obstacle action refuses");
    event.type=2;event.marker=2;state.cache[0]=0xa5a5fc00U;
    check(ai_road_decide(event,7,input,state,rng,take)&&state.cache[0]==0xa5a5fc1fU&&state.marker==7,"packed decision preserves unrelated bits");
    for(const auto& v:drag_nearest_native) {
        Route road;road.points.resize(1);road.points[0].forward[0]=1.F;
        for(unsigned k=0;k<6;++k) {
            road.lane_enabled[k]=(v[0]>>k)&1;
            road.points[0].lane_offset[k]=float(int(k)*3-9);
        }
        float y;const auto word=std::uint32_t(v[1]);std::memcpy(&y,&word,4);
        check(ai_road_nearest_lane(road,0,{0,y,0},int(v[2]),int(v[3]))==v[4],
              "original456E90 nearest lane and exclusion interval");
    }
    Route lane_road;lane_road.index=0;lane_road.points.resize(2);lane_road.lane_enabled.fill(true);
    for(unsigned j=0;j<2;++j) {
        auto& p=lane_road.points[j];p.position[0]=float(j)*10;p.forward[0]=1;p.backward[0]=-1;
        p.unknown_24=p.unknown_28=20;p.distance_to_next=p.distance_to_previous=10;
        for(unsigned i=0;i<6;++i){p.lane_offset[i]=float(i)*2-5;p.lane_offset[i+6]=-p.lane_offset[i];}
    }
    AiRoadChain lane_chain;lane_chain.count=2;
    for(unsigned j=0;j<2;++j){lane_chain.cells[j].point=lane_road.points[j];lane_chain.cells[j].distance=float(j)*10;}
    std::array<bool,7> available;available.fill(true);DragLaneState lane_state;
    for(const auto& row:kNativeDragInput) {
        float distance;std::memcpy(&distance,&row.distance,4);
        DragLaneState input;input.mode=0;
        check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,0,distance,input)&&
              input.mode==int(row.mode),"actual4605E0 release gate uses travelled distance, independently of speed");
    }
    check(ai_road_nearest_lane(lane_road,0,{0,3,0})==12,"nearest enabled drag lane");
    check(ai_road_nearest_lane(lane_road,0,{0,3,0},12,16)==11,"inclusive lane exclusion");
    check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,-1,10,lane_state)&&
          lane_state.category==11&&lane_state.exclude_first==12&&lane_state.exclude_last==16,"held left selects reachable lane");
    check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,0,10,lane_state)&&
          lane_state.category==11&&lane_state.mode==2,"release retains target until crossed lane is left");
    check(drag_lane_step(lane_road,0,{0,5,0},lane_chain,available,0,10,lane_state)&&
          lane_state.category==11&&lane_state.exclude_first==255,"centered destination clears lane exclusion");
    lane_state={};available[3]=false;lane_road.lane_enabled[2]=false;
    check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,1,10,lane_state)&&lane_state.category==12,
          "lane change cannot jump across a disabled raw route lane");
    lane_state={};
    check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,-1,1,lane_state)&&lane_state.mode==2,
          "drag lane input waits for original minimum travelled distance");
    lane_road.lane_enabled[2]=true;lane_state={};
    check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,1,10,lane_state)&&lane_state.category==16,
          "actual owner2 skips future chain flags after header admission");
    lane_state={};
    check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,1,10,lane_state,{0,0,0,4})&&lane_state.category==12,
          "other controllers still consume destination chain availability");
    available.fill(true);lane_state={};
    check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,-1,10,lane_state,{1206,0,1000,2})&&lane_state.category==12,
          "live drag input cannot cross the active left divider");
    lane_state={};
    check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,1,10,lane_state,{1206,0,1000,2})&&lane_state.category==13,
          "multi-lane hold stops at the next blocked adjacent divider");
    lane_state={};
    check(drag_lane_step(lane_road,0,{0,3,0},lane_chain,available,-1,10,lane_state,{1206,0,1300,2})&&lane_state.category==11,
          "same divider releases after its authored progress interval");
    for(const auto& c:kNativeRoadHorizons) {
        AiRoadHorizons h;
        check(ai_road_horizons(real(c[0]),real(c[1]),real(c[2]),h)&&
            float_bits(h.initial)==c[3]&&float_bits(h.retained)==c[4],
            "original racer initial and speed-dependent retained road horizons");
    }
    unsigned permission_index=0;
    for(const auto& c:kNativeLanePermission) {
        bool allowed=false;const bool ok=ai_road_lane_transition({c.track,c.direction,real(c.progress)},c.from,c.to,c.available!=0,allowed);
        if(!ok||allowed!=bool(c.allowed)){++failures;std::cerr<<"FAIL: actual lane permission case "<<permission_index<<'\n';}
        ++permission_index;
    }
    {
        bool allowed=false;const float nan=std::numeric_limits<float>::quiet_NaN();
        check(!ai_road_lane_transition({1206,0,nan},11,12,true,allowed)&&!allowed,
            "invalid consumed progress refuses transactionally");
        check(ai_road_lane_transition({1210,0,nan},11,12,true,allowed)&&allowed,
            "unrestricted track does not consume divider progress");
        check(ai_road_lane_transition({1206,0,nan},11,11,true,allowed)&&allowed,
            "retained lane does not consume divider progress");
        check(ai_road_lane_transition({1206,0,nan},11,12,false,allowed)&&!allowed,
            "destination admission runs before divider data consumption");
    }
    std::cout<<"AI road original decisions "<<std::size(decisions)<<", topology "<<std::size(topology)<<", chains "<<std::size(chains)<<", expiry "<<std::size(expiry)<<", failures "<<failures<<'\n';
    return failures?1:0;
}
