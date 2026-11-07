#include "nfsu/ai_path.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace nfsu {
namespace {
int slot(int category) {return category<5?0:category-10;}
float square(float x) {return x*x;}
bool finite(const AiDriverPoint& p) {
    return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);
}
float lane(const RoutePoint& p,int category) {
    return category<5?p.driving_offset[category]:p.lane_offset[category-11];
}
std::array<float,2> unit(float x,float y) {
    const float length=std::sqrt(y*y+x*x);
    if(length==0)return {1,0};
    const float inverse=1.F/length;
    return {inverse*x,inverse*y};
}
bool append(const AiDriverPoint& candidate,const AiPathRoadPoint& road,
            const AiPathExtension& in,AiPlannedPath& out) {
    const unsigned n=out.path.count;
    if(n>=20)return false;
    if(n) {
        const auto& last=out.path.points[n-1];
        const float dx=candidate[0]-last[0],dy=candidate[1]-last[1];
        const float distance=std::sqrt(square(dy)+square(dx));
        if(distance<1.F)return false;
        if(distance<20.F) {
            const auto& previous=n>=2?out.path.points[n-2]:in.actor_position;
            if((last[0]-previous[0])*dx+(last[1]-previous[1])*dy<0)return false;
        }
        const float dz=candidate[2]-last[2];
        out.length+=std::sqrt((square(dy)+square(dz))+square(dx));
    } else {
        const float distance=std::sqrt(square(candidate[0]-in.actor_position[0])+
                                       square(candidate[1]-in.actor_position[1]));
        //4556D0 is a forward-direction predicate, not a geometry raycast.
        const auto& direction=in.absolute_speed<1.F?in.actor_forward:in.actor_velocity;
        const auto a=unit(direction[0],direction[1]);
        const auto b=unit(candidate[0]-in.actor_position[0],candidate[1]-in.actor_position[1]);
        const bool ahead=a[0]*b[0]+a[1]*b[1]>0;
        if(distance<1.F||(!ahead&&distance<=20.F))return false;
    }
    out.path.points[n]=candidate;out.route[n]=road.point.route_index;
    out.point[n]=road.point_index;out.category[n]=static_cast<std::uint8_t>(in.category);
    ++out.path.count;return true;
}
}

bool ai_path_publish(AiPlannedPath& path) noexcept {
    const unsigned n=path.path.count;
    if(n>20||!std::isfinite(path.length))return false;
    for(unsigned i=0;i<n;++i)if(!finite(path.path.points[i]))return false;
    if(n<=2)return true;
    auto next=path;
    std::array<float,20> distances{};
    float total=0;
    for(unsigned i=1;i<n;++i) {
        const auto& a=path.path.points[i];const auto& b=path.path.points[i-1];
        const float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2];
        //4599ED..4599FB: the x87 sum is (dz^2+dx^2)+dy^2.
        total=std::sqrt((dz*dz+dx*dx)+dy*dy)+total;
        if(!std::isfinite(total))return false;
        distances[i]=total;
    }
    const float inverse=1.F/float(n-1);
    for(unsigned i=0;i<n;++i) {
        const float at=(float(i)*total)*inverse;
        float coordinate=0;
        if(at>distances[0]) {
            coordinate=float(n-1);
            if(at<distances[n-1])for(unsigned j=0;j+1<n;++j) {
                if(distances[j]<=at&&at<distances[j+1]) {
                    const float span=distances[j+1]-distances[j];
                    coordinate=std::fabs(span)<=.0000009999999974752427F?
                        .5F+float(j):(at-distances[j])/span+float(j);
                    break;
                }
            }
        }
        coordinate=std::clamp(coordinate,0.F,float(n-1));
        const auto first=static_cast<unsigned>(coordinate);
        if(coordinate==float(first))next.path.points[i]=path.path.points[first];
        else {
            const float fraction=coordinate-float(first),left=1.F-fraction;
            for(unsigned k=0;k<3;++k) {
                const float before=left*path.path.points[first][k];
                next.path.points[i][k]=fraction*path.path.points[first+1][k]+before;
            }
        }
        if(!finite(next.path.points[i]))return false;
    }
    next.length=total;next.cursor_tick=next.prediction_tick=0;
    path=next;return true;
}

bool ai_path_road_bounds(AiPathRoadPoint& road,int category) noexcept {
    if(!((category>=0&&category<5)||(category>=11&&category<=16)))return false;
    auto next=road;
    for(unsigned k=0;k<7;++k) {
        const float lateral=lane(road.point,k==0?category:static_cast<int>(k)+10)+road.offset[k];
        next.lower[k]=(-road.point.unknown_24)-lateral;
        next.upper[k]=road.point.unknown_28-lateral;
        if(!std::isfinite(next.lower[k])||!std::isfinite(next.upper[k]))return false;
    }
    road=next;return true;
}

bool ai_path_cursor(AiPlannedPath& result,float speed,const AiDriverPoint& position,
                    const AiDriverPoint& velocity,std::uint32_t tick,
                    float minimum_distance_squared,float fast_dot,float slow_dot) noexcept {
    if(result.path.count>20||!finite(position)||!finite(velocity)||
       !std::isfinite(speed)||!std::isfinite(minimum_distance_squared)||
       !std::isfinite(fast_dot)||!std::isfinite(slow_dot))return false;
    for(unsigned j=0;j<result.path.count;++j)if(!finite(result.path.points[j]))return false;
    auto next=result;
    if(next.cursor_tick!=tick) {
        if(next.path.count==0){next.cursor=0;next.cursor_tick=0;}
        else {
            const auto direction=unit(velocity[0],velocity[1]);
            const float threshold=speed>30.F?fast_dot:slow_dot;
            next.cursor=next.path.count-1;
            for(unsigned j=0;j<next.path.count;++j) {
                const float dx=next.path.points[j][0]-position[0];
                const float dy=next.path.points[j][1]-position[1];
                const float distance_squared=dy*dy+dx*dx;
                const auto delta=unit(dx,dy);
                if(direction[1]*delta[1]+direction[0]*delta[0]>threshold&&
                   distance_squared>minimum_distance_squared){next.cursor=j;break;}
            }
            next.cursor_tick=tick;
        }
    }
    result=next;return true;
}

bool ai_path_retain(AiPlannedPath& result,float speed,const AiDriverPoint& position,
                    const AiDriverPoint& velocity,std::uint32_t tick,
                    float minimum_distance_squared,float fast_dot,float slow_dot) noexcept {
    auto next=result;
    if(!ai_path_cursor(next,speed,position,velocity,tick,minimum_distance_squared,fast_dot,slow_dot))return false;
    // Original trim deliberately preserves the last point when all are behind.
    if(next.cursor<next.path.count) {
        const unsigned count=next.path.count-next.cursor;
        for(unsigned j=0;j<count;++j) {
            next.path.points[j]=next.path.points[j+next.cursor];
            next.route[j]=next.route[j+next.cursor];
            next.point[j]=next.point[j+next.cursor];
            next.category[j]=next.category[j+next.cursor];
        }
        next.path.count=count;next.cursor_tick=0;next.length=0;
        for(unsigned j=1;j<count;++j) {
            const float dx=next.path.points[j-1][0]-next.path.points[j][0];
            const float dy=next.path.points[j-1][1]-next.path.points[j][1];
            const float dz=next.path.points[j-1][2]-next.path.points[j][2];
            next.length+=std::sqrt((dz*dz+dy*dy)+dx*dx);
        }
    }
    if(!std::isfinite(next.length))return false;
    result=next;return true;
}

bool ai_path_continue(const AiPathRoadPoint* roads,std::size_t count,
                      std::size_t selected,float initial_offset,
                      const AiPathExtension& input,AiPlannedPath& result) noexcept {
    if((count&&!roads)||result.path.count>20||!std::isfinite(initial_offset)||
       !((input.category>=0&&input.category<5)||(input.category>=11&&input.category<17)))return false;
    if(count==0||result.path.count==20)return true;
    auto in=input;std::size_t cursor=0;
    if(result.path.count==0) {
        if(selected>=count)return false;
        cursor=std::min(selected+1,count-1);
        in.offset=std::fabs(initial_offset)<2.F?0.F:initial_offset;
    } else {
        const auto& last=result.path.points[result.path.count-1];
        if(!finite(last))return false;
        cursor=count;float nearest=1.0e14F;
        // 5A6150 flags1: nearest cell strictly ahead of the retained point.
        // Preserve the original distance-based scan skip, not a whole-track
        // nearest-point search or progress-spline lookup.
        for(std::size_t j=0;j<count;++j) {
            const auto& cell=roads[j];const auto& p=cell.point;
            const float dx=p.position[0]-last[0],dy=p.position[1]-last[1];
            const float distance=std::sqrt(dy*dy+dx*dx);
            const float projected=((last[1]-p.position[1])*p.forward[1]+
                (last[0]-p.position[0])*p.forward[0])+cell.distance;
            if(!std::isfinite(distance)||!std::isfinite(projected)||!std::isfinite(cell.distance))return false;
            if(projected<cell.distance&&distance<nearest){nearest=distance;cursor=j;}
            const float skip_distance=distance*-.0416666679084301F;
            if(skip_distance<=static_cast<float>(std::numeric_limits<int>::min()))return false;
            const int skip=-2-static_cast<int>(skip_distance);
            if(skip>0) {
                if(static_cast<std::size_t>(skip)>=count-j)break;
                j+=static_cast<std::size_t>(skip);
            }
        }
        if(cursor==count)return true;
        const auto& road=roads[cursor];const auto& p=road.point;
        // 4550F0/5A7390 store the cross projection before the lane subtraction.
        const float across=(last[0]-p.position[0])*p.forward[1]-(last[1]-p.position[1])*p.forward[0];
        in.offset=across-(lane(p,in.category)+road.offset[slot(in.category)]);
    }
    return ai_path_extend(roads+cursor,count-cursor,in,result);
}

bool ai_path_transition(const AiPathRoadPoint* roads,std::size_t count,
                         int start_cell,int target_cell,float from,float to,
                         float half_width,bool extra_margin,const AiPathExtension& in,
                         AiPlannedPath& result,bool& clipped) noexcept {
    if(!roads||count==0||count>40||result.path.count>20||
       !((in.category>=0&&in.category<5)||(in.category>=11&&in.category<17))||
       !finite(in.actor_position)||!finite(in.actor_velocity)||!finite(in.actor_forward)||
       !std::isfinite(in.absolute_speed)||!std::isfinite(result.length))return false;
    for(float v:{from,to,half_width,in.margin})if(!std::isfinite(v))return false;
    const int first=std::clamp(start_cell,0,static_cast<int>(count)-1);
    // Subtract in a wider integer to avoid overflow on invalid caller indices.
    const int last=static_cast<int>(std::clamp(static_cast<long long>(target_cell)-2,
                                              static_cast<long long>(first),static_cast<long long>(count)-1));
    const float origin=roads[first].distance;
    const float span=roads[last].distance-origin;
    if(!std::isfinite(origin)||!std::isfinite(span))return false;
    float rate=0;
    if(std::fabs(span)>.001F)rate=(to-from)/span;
    else from=to;
    if(!std::isfinite(rate))return false;
    auto next=result;bool limited=clipped;const auto index=slot(in.category);
    for(int i=first;i<=last&&next.path.count<20;++i) {
        const auto& road=roads[i];const auto& p=road.point;
        for(float v:{road.distance,p.position[0],p.position[1],p.height,p.forward[0],p.forward[1],
                      p.unknown_2c,p.unknown_30,lane(p,in.category),road.offset[index],
                      road.lower[index],road.upper[index]})if(!std::isfinite(v))return false;
        const float desired=((road.distance-origin)*rate)+from;
        const float interpolated=to>from?std::min(desired,to):std::max(desired,to);
        float low=road.lower[index]+half_width,high=road.upper[index]-half_width;
        if(extra_margin){low+=in.margin;high-=in.margin;}
        low=std::min(low,0.F);high=std::max(high,0.F);
        const float offset=std::min(std::max(interpolated,low),high);
        const float lateral=(lane(p,in.category)+road.offset[index])+offset;
        AiDriverPoint point{(p.forward[1]*lateral+p.position[0])+p.forward[0]*0.F,
            ((-p.forward[0])*lateral+p.position[1])+p.forward[1]*0.F,
            (p.unknown_2c*lateral+p.height)+p.unknown_30*0.F};
        if(!finite(point)||!std::isfinite(interpolated)||!std::isfinite(offset))return false;
        append(point,road,in,next);limited=limited||offset!=interpolated;
    }
    if(!std::isfinite(next.length))return false;
    result=next;clipped=limited;return true;
}

bool ai_path_extend(const AiPathRoadPoint* roads,std::size_t count,
                    const AiPathExtension& in,AiPlannedPath& result) noexcept {
    if((count&&!roads)||result.path.count>20||
       !((in.category>=0&&in.category<5)||(in.category>=11&&in.category<17))||
       !finite(in.actor_position)||!finite(in.actor_velocity)||!finite(in.actor_forward)||
       !std::isfinite(in.absolute_speed)||!std::isfinite(result.length))return false;
    for(float v:{in.offset,in.offset_step,in.target_offset,in.clearance,in.margin})
        if(!std::isfinite(v))return false;
    for(unsigned j=0;j<result.path.count;++j)if(!finite(result.path.points[j]))return false;
    auto next=result;float offset=in.offset;const auto index=slot(in.category);
    for(std::size_t j=0;j<count&&next.path.count<20;++j) {
        const auto& road=roads[j];const auto& p=road.point;
        for(float v:{p.position[0],p.position[1],p.height,p.forward[0],p.forward[1],
                      p.unknown_2c,p.unknown_30,lane(p,in.category),road.offset[index],
                      road.lower[index],road.upper[index]})if(!std::isfinite(v))return false;
        //45C640: move the retained offset, then include clearance and margin
        // in the per-category interval. The original includes zero in it.
        if(in.mode==4)offset+=in.offset_step;
        else if(in.mode==3)offset-=in.offset_step;
        else if(offset<in.target_offset)offset=std::min(offset+in.offset_step,in.target_offset);
        else offset=std::max(offset-in.offset_step,in.target_offset);
        const float low=std::min((road.lower[index]+in.clearance)+in.margin,0.F);
        const float high=std::max((road.upper[index]-in.clearance)-in.margin,0.F);
        offset=std::clamp(offset,low,high);
        //5A7270/4422B0, using this road point's own distance: longitudinal0.
        const float lateral=(lane(p,in.category)+road.offset[index])+offset;
        AiDriverPoint point{(p.forward[1]*lateral+p.position[0])+p.forward[0]*0.F,
            ((-p.forward[0])*lateral+p.position[1])+p.forward[1]*0.F,
            (p.unknown_2c*lateral+p.height)+p.unknown_30*0.F};
        if(!finite(point))return false;
        append(point,road,in,next);
    }
    if(!std::isfinite(next.length))return false;
    result=next;return true;
}

namespace {
float road_distance(const AiPathRoadPoint& road,const AiDriverPoint& p) {
    return ((p[1]-road.point.position[1])*road.point.forward[1]+
            (p[0]-road.point.position[0])*road.point.forward[0])+road.distance;
}
float road_lateral(const AiPathRoadPoint& road,const AiDriverPoint& p,int category) {
    const float across=(p[0]-road.point.position[0])*road.point.forward[1]-
                       (p[1]-road.point.position[1])*road.point.forward[0];
    return across-(lane(road.point,category)+road.offset[slot(category)]);
}
bool next_road(const AiPathRoadPoint* roads,std::size_t count,int start,
               const AiDriverPoint& point,int& selected) {
    selected=-1;float nearest=1.0e14F;
    for(std::size_t j=static_cast<std::size_t>(std::max(start,0));j<count;++j) {
        const auto& road=roads[j];
        const float dx=road.point.position[0]-point[0],dy=road.point.position[1]-point[1];
        const float distance=std::sqrt(dy*dy+dx*dx);
        const float projected=road_distance(road,point);
        if(!std::isfinite(distance)||!std::isfinite(projected))return false;
        if(projected<road.distance&&distance<nearest){nearest=distance;selected=static_cast<int>(j);}
        const float skip_distance=distance*-.0416666679084301F;
        if(skip_distance<=static_cast<float>(std::numeric_limits<int>::min()))return false;
        const int skip=-2-static_cast<int>(skip_distance);
        if(skip>0) {
            if(static_cast<std::size_t>(skip)>=count-j)break;
            j+=static_cast<std::size_t>(skip);
        }
    }
    return true;
}
}

bool ai_path_maneuver(const AiPathRoadPoint* roads,std::size_t count,const AiPathExtension& extension,
                      const AiManeuverInput& in,AiManeuverResult& result) noexcept {
    if(!roads||count==0||count>40||result.path.path.count>20||in.target_path.path.count>20||
       in.self.cell<0||in.target.cell<0||static_cast<std::size_t>(in.self.cell)>=count||
       static_cast<std::size_t>(in.target.cell)>=count||(in.side!=2&&in.side!=3)||
       !((extension.category>=0&&extension.category<5)||(extension.category>=11&&extension.category<17)))return false;
    for(const auto* car:{&in.self,&in.target}) {
        if(!finite(car->position)||!finite(car->velocity))return false;
        for(float v:{car->lateral,car->half_width,car->distance})if(!std::isfinite(v))return false;
    }
    for(float v:in.policy)if(!std::isfinite(v))return false;
    if(!std::isfinite(in.self_signed_speed)||!std::isfinite(extension.margin))return false;
    const auto index=slot(extension.category);
    for(std::size_t j=0;j<count;++j) {
        const auto& road=roads[j];const auto& p=road.point;
        for(float v:{road.distance,p.position[0],p.position[1],p.height,p.forward[0],p.forward[1],
                      p.unknown_2c,p.unknown_30,lane(p,extension.category),road.offset[index],
                      road.lower[index],road.upper[index],p.driving_offset[0],road.offset[0],
                      road.lower[0],road.upper[0]})if(!std::isfinite(v))return false;
    }
    auto next=result;auto ex=extension;ex.mode=2;ex.target_offset=0;
    // 45C370 resolves sentinel99999 through45C550; mode2 keeps offset.
    ex.offset_step=0;
    ex.clearance=in.self.half_width;
    const auto& target_road=roads[in.target.cell];
    const float physical_lateral=road_lateral(target_road,in.target.position,0);
    const float outside=physical_lateral>0?physical_lateral-target_road.upper[0]:
                                             target_road.lower[0]-physical_lateral;
    if(!std::isfinite(outside))return false;
    next.maneuver=outside<5.F; // original6CC954
    if(next.maneuver) {
        const float target_speed=std::sqrt((in.target.velocity[0]*in.target.velocity[0]+
            in.target.velocity[1]*in.target.velocity[1])+in.target.velocity[2]*in.target.velocity[2]);
        float clearance=0;
        if(in.self.role==3)clearance=in.policy[6];
        else if((!(in.target.racer&&in.target.role==3)&&target_speed<=in.policy[1])||
                (in.target.racer&&in.target.role==3&&target_speed<=in.policy[2]))clearance=in.policy[5];
        else clearance=in.target.racer&&in.target.role==1?in.policy[4]:in.policy[3];
        clearance=std::max(clearance,std::fabs(in.target.lateral-in.self.lateral));
        if(in.extra_clearance)clearance+=in.policy[0];
        const float separation=((in.target.half_width+in.self.half_width)+clearance)*(in.side==2?-1.F:1.F);
        const float desired=separation+in.target.lateral;
        const float upper=(target_road.upper[index]-extension.margin)-in.self.half_width;
        const float lower=(target_road.lower[index]+in.self.half_width)+extension.margin;
        const float chosen=std::min(std::max(desired,lower),upper);
        if(!std::isfinite(target_speed)||!std::isfinite(desired)||!std::isfinite(chosen))return false;
        if(!ai_path_transition(roads,count,std::min(in.self.cell+1,static_cast<int>(count)-1),
            in.target.cell,in.self.lateral,chosen,in.self.half_width,false,extension,next.path,next.clipped))return false;
        next.clipped=next.clipped||chosen!=desired;
        next.transition_end=static_cast<std::uint8_t>(next.path.path.count-1);
        const bool use_target_path=in.target.racer&&
            (in.target.role==1||(in.target.role==3&&in.self.role!=3))&&!in.target.alternate_controller;
        if(use_target_path&&target_speed>5.F&&in.self_signed_speed>5.F) {
            const auto self_direction=unit(extension.actor_velocity[0],extension.actor_velocity[1]);
            const auto target_direction=unit(in.target.velocity[0],in.target.velocity[1]);
            if(target_direction[1]*self_direction[1]+target_direction[0]*self_direction[0]>in.policy[7]) {
                float covered=in.self.distance;int cell=in.self.cell;
                if(next.path.path.count) {
                    const auto& last=next.path.path.points[next.path.path.count-1];
                    int found=-1;if(!next_road(roads,count,cell,last,found))return false;
                    if(found!=-1)covered=std::max(covered,road_distance(roads[found],last));
                }
                const float limit=in.target.lateral+separation;
                for(unsigned j=in.target_cursor;j<in.target_path.path.count&&next.path.path.count<20;++j) {
                    const auto& p=in.target_path.path.points[j];if(!finite(p))return false;
                    int found=-1;if(!next_road(roads,count,cell,p,found))return false;
                    if(found==-1)break;
                    cell=found;const auto& road=roads[cell];const auto& r=road.point;
                    const float distance=road_distance(road,p);
                    if(distance<=covered)continue;
                    const float offset=road_lateral(road,p,extension.category)+separation;
                    const float bounded=in.side==2?std::min(offset,limit):std::max(offset,limit);
                    const float low=std::min(road.lower[index]+in.self.half_width,0.F);
                    const float high=std::max(road.upper[index]-in.self.half_width,0.F);
                    const float lateral=(lane(r,extension.category)+road.offset[index])+std::min(std::max(bounded,low),high);
                    const float along=distance-road.distance;
                    AiDriverPoint point{(r.forward[1]*lateral+r.position[0])+r.forward[0]*along,
                        ((-r.forward[0])*lateral+r.position[1])+r.forward[1]*along,
                        (r.unknown_2c*lateral+r.height)+r.unknown_30*along};
                    if(!finite(point))return false;
                    append(point,road,extension,next.path);
                }
            }
        }
    }
    if(!ai_path_continue(roads,count,static_cast<std::size_t>(in.self.cell),in.self.lateral,ex,next.path))return false;
    result=next;return true;
}

} // namespace nfsu
