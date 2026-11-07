#include "nfsu/ai_traffic_intersection.h"
#include "nfsu/ai_neighbors.h"
#include <cmath>
#include <cstring>
#include <utility>

namespace nfsu {
namespace {
bool slot(int id){return id>=50&&id<72;}
std::uint32_t word(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
int signed_word(const unsigned char* p){const auto raw=word(p);std::int32_t v;std::memcpy(&v,&raw,4);return v;}
float number(const unsigned char* p){const auto raw=word(p);float v;std::memcpy(&v,&raw,4);return v;}
int signed_byte(unsigned char v){return v<128?int(v):int(v)-256;}
bool closest_valid(const AiTrafficIntersection& i){return (i.closest[0]==-1||slot(i.closest[0]))&&(i.closest[1]==-1||slot(i.closest[1]));}
bool entries_valid(const AiTrafficIntersection& i){
    for(const auto& a:i.actors)if(a.active&&((a.direction!=0&&a.direction!=1)||!std::isfinite(a.distance)||!std::isfinite(a.time)))return false;
    return true;
}
void refresh(AiTrafficIntersection& i,unsigned direction){
    i.closest[direction]=-1;float best=999999.F;
    for(unsigned j=0;j<i.actors.size();++j){const auto& a=i.actors[j];
        if(a.active&&a.direction==int(direction)&&a.time<best){best=a.time;i.closest[direction]=int(j)+50;}}
}
}
bool ai_traffic_intersections(const unsigned char* data,std::size_t size,
                              std::vector<AiTrafficIntersection>& output) noexcept {
    if((size&&!data)||size%0x170)return false;
    try {
        std::vector<AiTrafficIntersection> next(size/0x170);
        for(std::size_t j=0;j<next.size();++j){const auto* p=data+j*0x170;auto& i=next[j];
            i.kind=signed_word(p);i.selected=signed_word(p+12);
            for(unsigned k=0;k<2;++k){const unsigned v=unsigned(p[8+2*k])|(unsigned(p[9+2*k])<<8);i.points[k]=v<32768?int(v):int(v)-65536;}
            for(unsigned k=0;k<2;++k){i.routes[k]=signed_byte(p[4+k]);i.closest[k]=signed_byte(p[6+k]);}
            for(unsigned k=0;k<i.actors.size();++k){auto& a=i.actors[k];const auto* row=p+0x10+k*0x10;
                a.active=row[0]!=0;a.direction=signed_word(row+4);a.distance=number(row+8);a.time=number(row+12);}
            if(!closest_valid(i)||i.selected< -1||i.selected>1||!entries_valid(i))return false;
        }
        output=std::move(next);return true;
    }catch(...){return false;}
}
bool ai_traffic_intersection_register(AiTrafficIntersection& i,int id,int role,
                                     int route,float distance,float time) noexcept {
    if(role!=3)return true;
    if(!slot(id)||!std::isfinite(distance)||!std::isfinite(time)||!closest_valid(i))return false;
    int direction=-1;for(unsigned k=0;k<2;++k)if(i.routes[k]==route){direction=int(k);break;}
    auto next=i;auto& a=next.actors[unsigned(id-50)];
    a.active=direction!=-1;a.direction=direction;a.distance=distance;a.time=time;
    if(direction!=-1){float previous=99999.F;const int old=next.closest[unsigned(direction)];
        if(old!=-1){previous=next.actors[unsigned(old-50)].time;if(!std::isfinite(previous))return false;}
        if(time<previous)next.closest[unsigned(direction)]=id;
    }
    i=next;return true;
}
bool ai_traffic_intersection_clear(AiTrafficIntersection& i,int id) noexcept {
    if(!slot(id)||!closest_valid(i)||!entries_valid(i))return false;
    auto next=i;next.actors[unsigned(id-50)].active=false;
    for(unsigned k=0;k<2;++k)if(next.closest[k]==id)refresh(next,k);
    i=next;return true;
}
bool ai_traffic_intersection_step(AiTrafficIntersection& i) noexcept {
    if(i.selected==-1)return true;
    if(i.selected<0||i.selected>1||!entries_valid(i))return false;
    bool occupied=false;for(const auto& a:i.actors)if(a.active&&a.direction==i.selected&&a.distance<12.F)occupied=true;
    if(!occupied)i.selected=-1;
    return true;
}
bool ai_traffic_intersection_time(const AiTrafficIntersection& i,unsigned direction,float& output) noexcept {
    if(direction>1)return false;
    const int id=i.closest[direction];if(id!=-1&&!slot(id))return false;
    const float time=id==-1?99999.F:i.actors[unsigned(id-50)].time;if(!std::isfinite(time))return false;
    output=time;return true;
}
bool ai_traffic_intersection_open(const AiTrafficIntersection& i,unsigned direction,bool& output) noexcept {
    if(direction>1)return false;
    bool allowed=false;
    if(i.kind==1||i.kind==2){
        if(i.selected==int(direction))allowed=true;
        else if(i.selected!=int(direction==0)){
            if(direction==0)allowed=true;
            else {float time;if(!ai_traffic_intersection_time(i,0,time))return false;allowed=time>=3.F;}
        }
    }
    output=allowed;return true;
}
bool ai_traffic_intersection_pair(const AiTrafficIntersection& i,int first,int first_role,
                                 int second,int second_role,bool& output) noexcept {
    if(first_role!=3||second_role!=3){output=true;return true;}
    if(!slot(first)||!slot(second))return false;
    const auto& a=i.actors[unsigned(first-50)];const auto& b=i.actors[unsigned(second-50)];
    if((a.active&&!std::isfinite(a.distance))||(b.active&&!std::isfinite(b.distance)))return false;
    output=a.active&&b.active&&a.distance>=0&&b.distance>=0&&a.direction!=b.direction;return true;
}
bool ai_traffic_intersection_extent(const RacingLineTrack& raw,const AiTrafficIntersection& i,
                                    int route,unsigned direction,float& output) noexcept {
    if(direction>1)return false;
    int side=-1;for(unsigned k=0;k<2;++k)if(i.routes[k]==route){side=int(k);break;}
    if(side==-1){output=0;return true;}
    const auto* first=raw.find(short(route));const auto* second=raw.find(short(i.routes[unsigned(side==0)]));
    const int p=i.points[unsigned(side)],q=i.points[unsigned(side==0)];
    if(!first||!second||p<0||q<0||std::size_t(p)>=first->points.size()||std::size_t(q)>=second->points.size())return false;
    const auto& a=first->points[unsigned(p)];const auto& b=second->points[unsigned(q)];
    const float* basis=direction?a.backward:a.forward;
    for(float v:{a.position[0],a.position[1],basis[0],basis[1],b.position[0],b.position[1],
                 b.forward[0],b.forward[1],b.unknown_24,b.unknown_28})if(!std::isfinite(v))return false;
    const float left=-b.unknown_24;
    const float x0=(b.forward[0]*0.F+b.forward[1]*left)+b.position[0];
    const float y0=(b.forward[1]*0.F+(-b.forward[0])*left)+b.position[1];
    const float x1=(b.forward[0]*0.F+b.forward[1]*b.unknown_28)+b.position[0];
    const float y1=(b.forward[1]*0.F+(-b.forward[0])*b.unknown_28)+b.position[1];
    float extent=(x0-a.position[0])*basis[0]+(y0-a.position[1])*basis[1];
    const float other=(x1-a.position[0])*basis[0]+(y1-a.position[1])*basis[1];
    if(other<=extent)extent=other;
    if(!std::isfinite(extent))return false;
    output=-extent;return true;
}
bool ai_traffic_intersection_reference(const RacingLineTrack& raw,const AiRoadChain& chain,
    int limited,std::size_t count,AiTrafficIntersectionRef& output) noexcept {
    if(limited==-1){output={};return true;}
    if(limited<0||unsigned(limited)>=chain.count||chain.count>chain.cells.size())return false;
    const auto& c=chain.cells[unsigned(limited)];const auto* route=raw.find(short(c.point.route_index));
    if(!route||c.point_index>=route->points.size())return false;
    const auto& flags=route->points[c.point_index].unknown_84;
    const unsigned bits=unsigned(flags[0])|(unsigned(flags[1])<<8)|(unsigned(flags[2])<<16)|(unsigned(flags[3])<<24);
    const unsigned index=(bits>>20)&31;
    if(index==31||index>=count)return false;
    output={true,index};return true;
}
bool ai_traffic_intersection_distance(const RacingLineTrack& raw,
    const std::vector<AiTrafficIntersection>& intersections,const AiRoadChain& chain,int limited,
    const AiNeighborEntry& self,unsigned direction,float& output) noexcept {
    AiTrafficIntersectionRef ref;
    if(!ai_traffic_intersection_reference(raw,chain,limited,intersections.size(),ref)||!ref.present)return false;
    const auto& c=chain.cells[unsigned(limited)];
    if(!std::isfinite(c.distance)||!std::isfinite(self.distance)||!std::isfinite(self.half_extent[1]))return false;
    float extent;if(!ai_traffic_intersection_extent(raw,intersections[ref.index],c.point.route_index,direction,extent))return false;
    const float distance=(((c.distance-extent)-12.F)-self.distance)-self.half_extent[1];
    if(!std::isfinite(distance))return false;
    output=distance;return true;
}
bool ai_traffic_intersection_arrival(float distance,float speed,float& output) noexcept {
    if(!std::isfinite(distance))return false;
    if(distance<=0){output=0;return true;}
    if(!std::isfinite(speed))return false;
    if(speed<=.001F){output=99999.F;return true;}
    const float time=distance/speed;if(!std::isfinite(time))return false;
    output=time>0?time:0;return true;
}
} // namespace nfsu
