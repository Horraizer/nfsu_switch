#include "nfsu/ai_neighbors.h"
#include <cstring>
#include <iostream>
using namespace nfsu;
namespace {
#include "ai_obstacle_query_native.inc"
float from_bits(std::uint32_t u){float x;std::memcpy(&x,&u,4);return x;}
struct Road {AiDriverPoint position;float forward[2],offset[5],lane[12],distance,compact[7];};
struct Case {
    unsigned roads;Road road[40];AiNeighborInput input;unsigned actors;AiNeighborActor actor[2];
    unsigned count;std::uint32_t entry[16][14];int self_index;
};
const Case cases[]={
#include "ai_neighbor_native.inc"
};
unsigned failures=0;
void check(bool ok,const char* what){if(!ok){++failures;std::cerr<<"FAIL: "<<what<<'\n';}}
std::uint32_t bits(float f){std::uint32_t x;std::memcpy(&x,&f,4);return x;}
AiRoadChain road(const Case& c) {
    AiRoadChain chain;chain.count=c.roads;
    for(unsigned j=0;j<c.roads;++j) {
        const auto& r=c.road[j];auto& cell=chain.cells[j];auto& p=cell.point;
        p.position[0]=r.position[0];p.position[1]=r.position[1];p.height=r.position[2];
        for(unsigned k=0;k<2;++k)p.forward[k]=r.forward[k];
        for(unsigned k=0;k<5;++k)p.driving_offset[k]=r.offset[k];
        for(unsigned k=0;k<12;++k)p.lane_offset[k]=r.lane[k];
        for(unsigned k=0;k<7;++k)cell.offset[k]=r.compact[k];
        cell.distance=r.distance;
    }
    return chain;
}
}
bool physical_query(void* opaque,std::uint32_t id,AiPhysicalObstacleResult& out) noexcept {
    if(id!=0x1102000&&id!=0x1102400)return false;
    return ai_physical_obstacle(*static_cast<AiPhysicalObstacleInput*>(opaque),out);
}
int main() {
    const float profiles[4][2][3]={{{35,0,0},{15,0,0}},{{35,3,-3},{15,0,0}},{{8,0,0},{30,0,0}},{{100,0,0},{50,0,0}}};
    unsigned query_index=0;
    for(const auto& row:kNativeObstacleQuery) {
        AiObstacleQueryInput in{int(row[1]),from_bits(row[3]),from_bits(row[4]),from_bits(row[5])};
        AiNeighborhood neighborhood;neighborhood.count=3;neighborhood.self_index=int(row[2]);
        for(unsigned j=0;j<3;++j) {
            auto& e=neighborhood.entries[j];e.id=0x1102000+j*0x400;
            const bool self=j>=row[2];const auto& values=profiles[row[0]][j<2?j:0];
            e.relative_velocity[1]=from_bits(row[6]);e.half_extent={self?1.F:.8F,2};
            e.distance=self?0:values[0];e.lateral=self?0:values[1];
            for(unsigned k=0;k<6;++k)e.lane_lateral[k]=(self?0:values[2])+float(k)*.25F;
        }
        AiPhysicalObstacleInput physical;physical.self_min={-2,-1,-1};physical.self_max={2,1,1};
        physical.velocity={20,0,0};physical.signed_speed=20;physical.absolute_speed=in.absolute_speed;
        physical.other_min=physical.self_min;physical.other_max=physical.self_max;
        physical.other_position={-20,0,0};physical.other_forward={1,0};
        AiObstacleQueryResult result;const bool ok=ai_obstacle_query(neighborhood,in,physical_query,&physical,result);
        const bool match=ok&&result.id==row[7]&&bits(result.time)==row[8]&&bits(result.distance)==row[9];
        if(!match)std::cerr<<"query "<<query_index<<" ok "<<ok<<" id "<<std::hex<<result.id<<"/"<<row[7]
            <<" time "<<bits(result.time)<<"/"<<row[8]<<" distance "<<bits(result.distance)<<"/"<<row[9]<<std::dec<<'\n';
        check(match,"original road and physical obstacle aggregation");
        const auto before=result;in.offset=std::numeric_limits<float>::quiet_NaN();
        check(!ai_obstacle_query(neighborhood,in,physical_query,&physical,result)&&result.id==before.id&&
            bits(result.time)==bits(before.time)&&bits(result.distance)==bits(before.distance),"query refusal preserves outputs");
        ++query_index;
    }
    std::cout<<"Original complete obstacle query witnesses: "<<query_index<<'\n';

    unsigned index=0;
    for(const auto& c:cases) {
        AiNeighborhood result;
        check(ai_neighbors(road(c),c.input,c.actor,c.actors,result),"native input admitted");
        check(result.count==c.count&&result.self_index==c.self_index,"native count/order/self");
        for(unsigned j=0;j<result.count&&j<c.count;++j) {
            const auto& e=result.entries[j];
            const std::uint32_t actual[]={bits(e.relative_velocity[0]),bits(e.relative_velocity[1]),
                bits(e.half_extent[0]),bits(e.half_extent[1]),bits(e.distance),bits(e.lateral),
                bits(e.lane_lateral[0]),bits(e.lane_lateral[1]),bits(e.lane_lateral[2]),
                bits(e.lane_lateral[3]),bits(e.lane_lateral[4]),bits(e.lane_lateral[5]),e.cell,e.id};
            for(unsigned k=0;k<14;++k)if(actual[k]!=c.entry[j][k]) {
                ++failures;std::cerr<<"FAIL: case "<<index<<" entry "<<j<<" word "<<k<<" actual "<<std::hex<<actual[k]
                    <<" expected "<<c.entry[j][k]<<std::dec<<'\n';
            }
        }
        ++index;
    }
    // Registration order, capacity and inactive-self behavior are semantic
    // checks independent of the two-actor native pose capture.
    const auto& c=cases[9];const auto chain=road(c);
    AiNeighborActor actors[20];
    AiNeighborActor self=c.actor[0];
    for(const auto& a:c.actor)if(a.id==c.input.self_id)self=a;
    for(unsigned j=0;j<20;++j){actors[j]=self;actors[j].id=j+1;actors[j].enabled=true;}
    actors[19]=self;actors[19].enabled=false;AiNeighborhood result;
    check(ai_neighbors(chain,c.input,actors,20,result),"capacity update");
    check(result.count==16&&result.self_index==15,"self slot reserved after first15");
    for(unsigned j=0;j<15;++j)check(result.entries[j].id==j+1,"equal-score registration stability");
    actors[0].enabled=false;
    check(ai_neighbors(chain,c.input,actors,20,result)&&result.entries[0].id==2,"disabled neighbor excluded");
    auto input=c.input;input.category=10;const auto before=result;
    check(!ai_neighbors(chain,input,actors,20,result)&&std::memcmp(&before,&result,sizeof(result))==0,"unsupported category leaves output");
    check(ai_neighbors({},c.input,nullptr,0,result)&&result.count==0&&result.self_index==-1,"empty context clears neighborhood");
    std::cout<<"AI neighborhood: "<<index<<" original cases; "<<failures<<" failures\n";
    return failures?1:0;
}
