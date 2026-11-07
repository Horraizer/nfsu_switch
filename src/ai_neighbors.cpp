#include "nfsu/ai_neighbors.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/race_route.h"
#include <cmath>
#include <utility>

namespace nfsu {
namespace {
bool finite(const AiDriverPoint& p) {for(float x:p)if(!std::isfinite(x))return false;return true;}
std::array<float,2> rotated(const AiDriverPoint& v,const RoutePoint& road) {
    const auto phase=static_cast<std::uint16_t>(0x4000-track_angle(road.forward[0],road.forward[1]));
    const float c=player_angle_sin(static_cast<std::uint16_t>(phase+0x4000));
    const float s=player_angle_sin(phase);
    return {{((0.F*v[2]+(-s)*v[1])+c*v[0])+0.F,((0.F*v[2]+c*v[1])+s*v[0])+0.F}};
}
float lateral(const AiPathRoadPoint& c,const AiDriverPoint& position,int category) {
    const auto& p=c.point;
    const float cross=(position[0]-p.position[0])*p.forward[1]-(position[1]-p.position[1])*p.forward[0];
    const float authored=category<5?p.driving_offset[category]:p.lane_offset[category-11];
    return cross-(authored+c.offset[category<5?0:category-10]);
}
float projected_vector(const AiDriverPoint& v,const AiDriverPoint& vector) {
    const float speed=std::sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
    if(speed<.1F)return std::sqrt((vector[0]*vector[0]+vector[1]*vector[1])+vector[2]*vector[2]);
    const float inv=1.F/speed;
    const AiDriverPoint unit{{v[0]*inv,v[1]*inv,v[2]*inv}};
    return (unit[1]*vector[1]+unit[2]*vector[2])+unit[0]*vector[0];
}
}
bool ai_neighbors(const AiRoadChain& chain,const AiNeighborInput& in,
                  const AiNeighborActor* actors,std::size_t count,AiNeighborhood& result) noexcept {
    if(chain.count>40||(!actors&&count)||!((in.category>=0&&in.category<5)||(in.category>=11&&in.category<=16))||
       in.ride_category<0||in.ride_category>=5||!finite(in.position)||!finite(in.velocity)||
       !finite(in.forward)||!finite(in.acceleration)||!finite(in.projection_vector)||
       !std::isfinite(in.progress)||!std::isfinite(in.horizon)||in.horizon<0||
       !std::isfinite(in.track_length)||(in.circuit&&in.track_length<=0))return false;
    AiNeighborhood next;
    if(!chain.count){result=next;return true;}
    AiRoadLocation self;
    if(!ai_road_locate_actor(chain,in.position,in.category,self))return false;
    const auto velocity=rotated(in.velocity,chain.cells[self.cell].point);
    const float acceleration=(in.acceleration[2]*in.forward[2]+in.acceleration[1]*in.forward[1])+in.acceleration[0]*in.forward[0];
    unsigned capacity=15;
    std::array<float,16> scores{};
    for(std::size_t i=0;i<count;++i) {
        const auto& actor=actors[i];const bool is_self=actor.id==in.self_id;
        if(is_self)capacity=16;
        if((!actor.enabled&&!is_self)||next.count>=capacity)continue;
        if(!finite(actor.position)||!finite(actor.velocity)||!finite(actor.forward)||!std::isfinite(actor.progress))return false;
        const float dx=actor.position[0]-in.position[0],dy=actor.position[1]-in.position[1],dz=actor.position[2]-in.position[2];
        const float distance=std::sqrt((dz*dz+dy*dy)+dx*dx);
        const float vx=actor.velocity[0]-in.velocity[0],vy=actor.velocity[1]-in.velocity[1],vz=actor.velocity[2]-in.velocity[2];
        float relative=std::sqrt((vz*vz+vy*vy)+vx*vx);
        const float direction=actor.forward[1]*in.forward[1]+actor.forward[0]*in.forward[0];
        if(direction<0)relative=-relative;
        float progress=actor.progress-in.progress;
        if(in.circuit) {
            if(progress<in.track_length*-.5F)progress+=in.track_length;
            else if(progress>in.track_length*.5F)progress-=in.track_length;
        }
        if(distance>in.horizon) {
            const float projected=projected_vector(actor.velocity,in.projection_vector);
            const float delta=direction>0?acceleration-projected:acceleration+projected;
            // Original global7006F0, not the caller's road horizon.
            const float prediction=((3.F*delta)*.5F+relative)*3.F;
            if(!(prediction>distance))continue;
        }
        if(std::fabs(progress)>in.horizon*3.F)continue;
        AiRoadLocation location;
        if(!ai_road_locate(chain,actor.position,0,location))return false;
        unsigned nearest=0;float best=std::fabs(chain.cells[0].distance-location.distance);
        for(unsigned j=1;j<chain.count;++j) {
            const float delta=std::fabs(chain.cells[j].distance-location.distance);
            if(delta<best){nearest=j;best=delta;}
        }
        auto& entry=next.entries[next.count];entry.id=actor.id;entry.cell=nearest;entry.distance=location.distance;
        const auto& preceding=chain.cells[nearest?nearest-1:0];
        entry.lateral=lateral(preceding,actor.position,in.ride_category);
        for(int j=0;j<6;++j)entry.lane_lateral[j]=lateral(preceding,actor.position,j+11);
        const auto local_velocity=rotated(actor.velocity,chain.cells[nearest].point);
        for(unsigned j=0;j<2;++j)entry.relative_velocity[j]=local_velocity[j]-velocity[j];
        float half[2];
        for(unsigned j=0;j<2;++j) {
            if(!std::isfinite(actor.bounds_min[j])||!std::isfinite(actor.bounds_max[j])||actor.bounds_min[j]>actor.bounds_max[j])return false;
            half[j]=(actor.bounds_max[j]-actor.bounds_min[j])*.5F;
        }
        const auto phase=static_cast<std::uint16_t>(track_angle(actor.forward[0],actor.forward[1])-in.heading+0x4000);
        const float cosine=player_angle_sin(phase),c2=cosine*cosine,s2=1.F-c2;
        entry.half_extent={{half[0]*s2+c2*half[1],c2*half[0]+s2*half[1]}};
        const float along=entry.distance-self.distance,across=entry.lateral-self.lateral;
        float score=across*across+along*along;
        if(along<0)score=-score;
        if(!std::isfinite(score))return false;
        scores[next.count++]=score;
    }
    // Original stable descending sort. Avoid heap-backed sorting or a second
    // world query; admission preserves original registration order.
    for(unsigned pass=0;pass<next.count;++pass)for(unsigned j=1;j<next.count;++j)
        if(scores[j]>scores[j-1]){std::swap(scores[j],scores[j-1]);std::swap(next.entries[j],next.entries[j-1]);}
    for(unsigned j=0;j<next.count;++j)if(next.entries[j].id==in.self_id)next.self_index=static_cast<int>(j);
    result=next;return true;
}
bool ai_obstacle_query(const AiNeighborhood& neighbors,const AiObstacleQueryInput& in,
    AiPhysicalObstacleQuery query,void* opaque,AiObstacleQueryResult& result) noexcept {
    if(neighbors.count>16||neighbors.self_index<0||unsigned(neighbors.self_index)>=neighbors.count||
       !((in.category>=0&&in.category<5)||(in.category>=11&&in.category<=16))||
       !std::isfinite(in.offset)||!std::isfinite(in.clearance)||!std::isfinite(in.absolute_speed)||in.absolute_speed<0)return false;
    const auto& self=neighbors.entries[unsigned(neighbors.self_index)];
    for(float x:self.half_extent)if(!std::isfinite(x)||x<0)return false;
    if(!std::isfinite(self.distance))return false;
    AiObstacleQueryResult next;
    for(int j=neighbors.self_index-1;j>=0;--j) {
        const auto& other=neighbors.entries[unsigned(j)];
        const float lateral=in.category<5?other.lateral:other.lane_lateral[unsigned(in.category-11)];
        for(float x:other.half_extent)if(!std::isfinite(x)||x<0)return false;
        if(!std::isfinite(lateral)||!std::isfinite(other.distance)||!std::isfinite(other.relative_velocity[1]))return false;
        if(std::fabs(lateral-in.offset)>self.half_extent[0]+.5F+other.half_extent[0])continue;
        const float raw_gap=((other.distance-other.half_extent[1])-self.distance)-self.half_extent[1];
        const float road_gap=raw_gap>0?raw_gap:0;
        const float relative=other.relative_velocity[1];
        const float road_time=relative<-.01F?road_gap/std::fabs(relative):99999.F;
        AiPhysicalObstacleResult physical;
        if(!query||!query(opaque,other.id,physical)||!std::isfinite(physical.time)||!std::isfinite(physical.distance))return false;
        float time=road_time<physical.time?road_time:physical.time;
        const float distance=physical.distance<=road_gap?physical.distance:road_gap;
        if(road_gap<=in.clearance||physical.distance<=in.clearance||
           ((road_gap<=20.F||physical.distance<=20.F)&&in.absolute_speed<.1F))time=0;
        else if(time>=99999.F)continue;
        next={other.id,time,distance};break;
    }
    result=next;return true;
}
} // namespace nfsu
