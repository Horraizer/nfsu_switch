#include "nfsu/ai_traffic_action.h"
#include <array>
#include <cstring>
#include <iostream>
struct NativeTrafficAvoidance {
    unsigned mode,category,present,raw[12],queries[6][3],expected[14],query_count,query_hash;
};
#include "traffic_avoidance_native.inc"
namespace {
float real(unsigned value){float result;std::memcpy(&result,&value,4);return result;}
unsigned bits(float value){unsigned result;std::memcpy(&result,&value,4);return result;}
struct Context {
    const NativeTrafficAvoidance* row;
    unsigned calls=0,hash=2166136261U;
};
bool query(void* opaque,int category,float offset,float clearance,nfsu::AiObstacleQueryResult& out) {
    if(category<11||category>16)return false;
    auto& c=*static_cast<Context*>(opaque);++c.calls;
    for(auto word:{unsigned(category),bits(offset),bits(clearance)})c.hash=(c.hash^word)*16777619U;
    const auto& q=c.row->queries[category-11];out={q[0],real(q[1]),real(q[2])};return true;
}
bool admitted(void* opaque,std::uint32_t,bool& value){value=static_cast<Context*>(opaque)->row->raw[10]!=0;return true;}
bool transition(void* opaque,int,int to,bool& value){
    value=to>=11&&to<=16&&(static_cast<Context*>(opaque)->row->raw[11]&(1U<<unsigned(to-11)));return true;
}
bool nearest(void* opaque,int& lane){lane=int(static_cast<Context*>(opaque)->row->raw[8]);return true;}
bool traffic(void* opaque,std::uint32_t,bool& value){value=static_cast<Context*>(opaque)->row->raw[9]!=0;return true;}
}
int main(){
    unsigned failures=0,index=0;
    for(const auto& row:kNativeTrafficAvoidance) {
        nfsu::AiNeighborhood n;n.count=row.present?2:1;n.self_index=row.present?1:0;
        if(row.present){auto& t=n.entries[0];t.id=0x110d000;t.distance=real(row.raw[0]);
            t.relative_velocity={real(row.raw[1]),real(row.raw[2])};t.half_extent={1,real(row.raw[7])};}
        auto& self=n.entries[unsigned(n.self_index)];self.id=0x1100000;self.distance=100;self.half_extent={1,real(row.raw[6])};
        for(unsigned j=0;j<6;++j)self.lane_lateral[j]=j*.125F;
        Context context{&row};nfsu::AiTrafficAvoidanceOps ops{query,admitted,transition,nearest,traffic};
        nfsu::AiTrafficAvoidanceInput input{&n,int(row.category),real(row.raw[3]),24.58650016784668F};
        nfsu::AiTrafficAvoidanceState state{0x110d000,real(row.raw[4]),real(row.raw[5])};
        nfsu::AiTrafficAvoidanceOutput output;bool flag=false,ok=false;
        if(row.mode==0)ok=nfsu::ai_traffic_avoidance_offer(input,ops,&context,state,flag);
        else if(row.mode==1)ok=nfsu::ai_traffic_avoidance_complete(input,ops,&context,state,flag);
        else ok=nfsu::ai_traffic_avoidance_update(input,ops,&context,state,output);
        std::array<unsigned,14> actual{{unsigned(flag),row.mode==2?unsigned(output.category):row.category,
            state.target,bits(state.time),bits(state.clearance)}};
        if(row.mode==2){actual[5]=bits(output.speed.speed);actual[6]=bits(output.speed.duration);actual[7]=bits(output.speed.auxiliary);
            actual[8]=unsigned(output.actor_override_enabled);for(unsigned j=0;j<4;++j)actual[9+j]=bits(output.actor_override[j]);actual[13]=unsigned(output.reset_maneuver);}
        for(unsigned i=0;i<actual.size();++i)if(actual[i]!=row.expected[i]){
            std::cerr<<"test failure: case "<<index<<" field "<<i<<" actual="<<std::hex<<actual[i]<<" expected="<<row.expected[i]<<std::dec<<'\n';++failures;}
        if(!ok||context.calls!=row.query_count||context.hash!=row.query_hash){
            std::cerr<<"test failure: case "<<index<<" status/query ordering "<<ok<<' '<<context.calls<<'/'<<row.query_count<<' '<<context.hash<<'/'<<row.query_hash<<'\n';++failures;}
        ++index;
    }
    nfsu::AiRoadChain chain;chain.count=4;
    for(unsigned i=0;i<4;++i)chain.cells[i].distance=float(i*10);
    for(const auto& row:kNativeTrafficStopEvent) {
        bool stopped=false;
        if(!nfsu::ai_road_stop_event_reached(chain,row[0]!=0,real(row[1]),stopped)||stopped!=(row[2]!=0)) {
            std::cerr<<"test failure: original5A6CE0 context-event stop boundary\n";++failures;
        }
    }
    std::cout<<"traffic avoidance tests: "<<(failures?"FAIL":"PASS")<<", native cases="<<index<<'\n';return failures?1:0;
}
