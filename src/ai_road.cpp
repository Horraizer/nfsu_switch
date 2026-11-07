#include "nfsu/ai_road.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace nfsu {
bool ai_road_orient(RacingLineTrack& track,int direction) noexcept {
    if(direction!=0 && direction!=1)return false;
    if(!direction)return true;
    for(auto& route:track.routes) {
        // Reverse virtual5A7020 traverses opposite to5A6F50; no reindexing
        // of event targets or authored route point identities is involved.
        route.reverse=!route.reverse;
        for(unsigned k=0;k<5;++k)std::swap(route.category_enabled[k],route.category_enabled[k+5]);
        for(unsigned k=0;k<6;++k)std::swap(route.lane_enabled[k],route.lane_enabled[k+6]);
        for(auto& p:route.points) {
            //4551E0/455490/4554B0,442330,5A7050 and4553D0 respectively.
            for(unsigned k=0;k<2;++k)std::swap(p.forward[k],p.backward[k]);
            std::swap(p.distance_to_next,p.distance_to_previous);
            std::swap(p.unknown_24,p.unknown_28);
            p.unknown_2c=-p.unknown_2c;
            std::swap(p.unknown_30,p.unknown_34);
            for(unsigned k=0;k<5;++k) {
                std::swap(p.driving_offset[k],p.driving_offset[k+5]);
                std::swap(p.speed[k],p.speed[k+5]);
            }
            for(unsigned k=0;k<6;++k) {
                std::swap(p.lane_offset[k],p.lane_offset[k+6]);
                std::swap(p.speed[k+11],p.speed[k+17]);
            }
        }
    }
    return true;
}
namespace {
float random_unit(std::uint32_t& state) {
    //4216F0 returns the old seed modulo INT_MAX before publishing the xor step.
    const auto sample=state%0x7fffffffU;
    const auto x=state^0x1d872b41U;
    const auto y=(x>>5)^x;
    state=(y<<27)^y^x;
    return static_cast<float>(sample)*4.656612873077393e-10F;
}
}
bool ai_road_events(const unsigned char* data,std::size_t size,std::vector<AiRoadEvent>& out) noexcept {
    if((size&&!data)||size%32||size/32>255)return false;
    try {
        std::vector<AiRoadEvent> next;next.reserve(size/32);
        for(std::size_t at=0;at<size;at+=32) {
            AiRoadEvent e;std::uint32_t bits=0;
            for(unsigned k=0;k<4;++k)bits|=std::uint32_t(data[at+k])<<(8*k);
            std::memcpy(&e.progress,&bits,4);
            e.target_point=std::uint16_t(data[at+4]|(unsigned(data[at+5])<<8));
            e.source_point=std::uint16_t(data[at+6]|(unsigned(data[at+7])<<8));
            e.source_route=data[at+10];
            e.marker=data[at+8];e.target_route=data[at+9];e.type=data[at+11];
            e.traffic_type=data[at+12];
            e.direction=data[at+13];e.disabled=data[at+15];
            e.context_flag=data[at+16];
            const auto word=[&](unsigned off){std::uint32_t value=0;for(unsigned k=0;k<4;++k)value|=std::uint32_t(data[at+off+k])<<(8*k);return value;};
            e.obstacle_type=word(20);
            const auto enter=word(24),leave=word(28);
            std::memcpy(&e.obstacle_enter,&enter,4);std::memcpy(&e.obstacle_leave,&leave,4);
            if(!std::isfinite(e.progress)||e.target_route>127)return false;
            next.push_back(e);
        }
        out=std::move(next);return true;
    }catch(...){return false;}
}
bool ai_road_initialize(const RaceRouteTrack& track,RacingLineTrack& lines,
                        std::vector<AiRoadEvent>& events,std::string& error) {
    TrackPathSpline spline;
    if(!build_track_path_spline(track,false,spline,error))return false;
    auto next_lines=lines;auto next_events=events;
    for(auto& route:next_lines.routes) {
        if(route.points.empty()){error="AI route has no points";return false;}
        TrackPathCoordinate coordinate; // native null node pointer
        update_track_path_coordinate(spline,track,coordinate,route.points.front().position);
        float previous=coordinate.distance;
        float low[2]={spline.path_length,spline.path_length},high[2]={0,0};
        unsigned bucket=0;bool wrapped=false;
        for(std::size_t i=1;i<route.points.size();++i) {
            update_track_path_coordinate(spline,track,coordinate,route.points[i].position);
            const float distance=coordinate.distance;
            if(!std::isfinite(distance)){error="AI road projection is not finite";return false;}
            if(std::abs(distance-previous)>spline.path_length*.5F){bucket^=1;wrapped=true;}
            previous=distance;low[bucket]=std::min(low[bucket],distance);high[bucket]=std::max(high[bucket],distance);
        }
        route.progress_begin=wrapped?std::max(low[0],low[1]):low[0];
        route.progress_end=wrapped?std::min(high[0],high[1]):high[0];
    }
    TrackPathCoordinate coordinate;
    for(auto& event:next_events) {
        const auto* route=next_lines.find(event.source_route);
        if(!route||event.source_point>=route->points.size()){error="AI event source point is absent";return false;}
        update_track_path_coordinate(spline,track,coordinate,route->points[event.source_point].position);
        if(!std::isfinite(coordinate.distance)){error="AI event projection is not finite";return false;}
        event.progress=coordinate.distance;
    }
    lines=std::move(next_lines);events=std::move(next_events);return true;
}
bool ai_traffic_road_decide(unsigned type,std::uint32_t& rng) noexcept {
    if(type==2)return true;
    return type==4&&random_unit(rng)<.5F;
}
bool ai_road_decide(const AiRoadEvent& event,unsigned id,const AiRoadPolicyInput& input,
                    AiRoadPolicy& state,std::uint32_t& rng,bool& take) noexcept {
    if(id>=255)return false;
    if(input.policy!=AiRoadDecisionPolicy::racer&&input.policy!=AiRoadDecisionPolicy::traffic)return false;
    const bool traffic=input.policy==AiRoadDecisionPolicy::traffic;
    if(!traffic) {
        if(!std::isfinite(input.race_seconds))return false;
        for(float v:input.probabilities)if(!std::isfinite(v))return false;
    }
    if((input.direction==1)!=(event.direction==1)||event.type==3){take=false;return true;}
    for(auto slot:state.cache)if((slot&1)&&((slot>>2)&255)==id){take=(slot&2)!=0;return true;}
    // Racer type9 creates a separate action. Traffic's accepted-event virtual
    //549130 is a no-op; its type9 callback is evaluated but never cached.
    if(event.type==9&&!traffic)return input.obstacle_decision&&input.obstacle_decision(input.obstacle_context,event,take);
    auto next=state;auto next_rng=rng;bool selected=false;
    if(traffic)selected=ai_traffic_road_decide(event.traffic_type,next_rng);
    else if(event.type==2||(event.type==8&&input.role==1))selected=true;
    else if(event.type==5||event.type==7||event.disabled)selected=false;
    else if(input.decision_start==-1?input.race_time_hold:
            static_cast<float>(input.decision_start)>=input.race_seconds)selected=false;
    else {
        const float sample=random_unit(next_rng);
        if(event.type==4)selected=sample<.5F;
        else if(sample<=input.probabilities[0])selected=event.type==1;
        else if(sample<=input.probabilities[1]||sample>input.probabilities[2])selected=event.type==0;
    }
    if(!traffic&&selected&&event.marker==2)next.marker=static_cast<int>(id);
    if(event.type==9){state=next;rng=next_rng;take=selected;return true;}
    auto slot=std::find_if(next.cache.begin(),next.cache.end(),[](auto value){return !(value&1);});
    // Source relies on expiration keeping a free slot; never write before the
    // cache array if that caller contract is violated.
    if(slot==next.cache.end())return false;
    *slot=(*slot&0xfffffc01U)|(id<<2)|(selected?2U:0U)|1U;
    state=next;rng=next_rng;take=selected;return true;
}
bool ai_road_expire(const std::vector<AiRoadEvent>& events,float progress,float length,
                    bool circuit,bool same_direction,float retain_distance,AiRoadPolicy& state) noexcept {
    if(!std::isfinite(progress)||!std::isfinite(length)||!std::isfinite(retain_distance)||
       (circuit&&length<=0))return false;
    auto next=state;
    for(auto& slot:next.cache)if(slot&1) {
        const auto id=(slot>>2)&255;if(id>=events.size())return false;
        float delta=events[id].progress-progress;
        if(circuit) {
            if(delta<length*-.5F)delta+=length;
            else if(delta>length*.5F)delta-=length;
        }
        if(!same_direction)delta=-delta;
        if(delta< -retain_distance)slot&=~1U;
    }
    state=next;return true;
}
AiRoadAdvance ai_road_next(const RacingLineTrack& track,const std::vector<AiRoadEvent>& events,
                          bool reverse,const AiRoadPolicyInput& input,AiRoadPolicy& policy,
                          std::uint32_t& rng,AiRoadCursor& cursor,bool& junction) noexcept {
    if(cursor.route<0||cursor.route>=128)return AiRoadAdvance::unsupported;
    const auto* route=track.find(cursor.route);
    if(!route||cursor.point>=route->points.size())return AiRoadAdvance::unsupported;
    const auto& point=route->points[cursor.point];auto next_policy=policy;auto next_rng=rng;
    for(auto id:{point.event_a,point.event_b}) {
        if(id==255)break; // original list ends at the first sentinel
        if(id>=events.size())return AiRoadAdvance::unsupported;
        const auto& event=events[id];
        // 5A53C0 compares the endpoint against the direction virtual.
        if(event.type==3&&event.direction==input.direction){policy=next_policy;rng=next_rng;return AiRoadAdvance::end;}
        bool take=false;
        if(!ai_road_decide(event,id,input,next_policy,next_rng,take))return AiRoadAdvance::unsupported;
        if(take) {
            if(event.target_route>=128)return AiRoadAdvance::unsupported;
            const auto* target=track.find(event.target_route);
            if(!target||event.target_point>=target->points.size())return AiRoadAdvance::unsupported;
            cursor={event.target_route,event.target_point};junction=true;
            policy=next_policy;rng=next_rng;return AiRoadAdvance::advanced;
        }
    }
    const int next=int(cursor.point)+(reverse?-1:1);
    policy=next_policy;rng=next_rng;
    if(next<0||next>=static_cast<int>(route->points.size()))return AiRoadAdvance::end;
    cursor.point=static_cast<std::uint16_t>(next);junction=false;return AiRoadAdvance::advanced;
}

namespace {
bool road_category(int category) {return (category>=0&&category<5)||(category>=11&&category<=16);}
unsigned road_slot(int category) {return category<5?0:unsigned(category-10);}
float authored_lane(const RoutePoint& p,int category) {
    return category<5?p.driving_offset[category]:p.lane_offset[category-11];
}
float clamp_offset(const RoutePoint& p,int category,float value) {
    const float margin=category<5?3.F:1.F;
    const float lateral=authored_lane(p,category);
    const float lower=static_cast<float>(margin-(p.unknown_24+lateral));
    const float upper=static_cast<float>((p.unknown_28-lateral)-margin);
    return std::min(upper,std::max(lower,value));
}
float junction_offset(const RoutePoint& from,const RoutePoint& to,int category) {
    // 5A5F80 projects the old authored lane into the new point's basis.
    const float lane=authored_lane(from,category);
    const float x=static_cast<float>(from.position[0]+lane*from.forward[1]);
    const float y=static_cast<float>(from.position[1]-lane*from.forward[0]);
    return (x-to.position[0])*to.forward[1]-
           (y-to.position[1])*to.forward[0]-authored_lane(to,category);
}
float projected_distance(const RoutePoint& from,const RoutePoint& to) {
    return static_cast<float>((to.position[1]-from.position[1])*from.forward[1]+
                             (to.position[0]-from.position[0])*from.forward[0]);
}
}
int ai_road_nearest_lane(const Route& route,unsigned point,const AiDriverPoint& position,
                         int first,int last) noexcept {
    if(point>=route.points.size())return -1;
    for(float value:position)if(!std::isfinite(value))return -1;
    const auto& p=route.points[point];
    const float lateral=(position[0]-p.position[0])*p.forward[1]-(position[1]-p.position[1])*p.forward[0];
    int selected=-1;float best=9.0e9F; //native50061C46
    for(unsigned i=0;i<6;++i) {
        const int category=int(i)+11;
        if(!route.lane_enabled[i]||(first!=255&&last!=255&&category>=first&&category<=last))continue;
        const float distance=std::abs(p.lane_offset[i]-lateral);
        if(distance<best){best=distance;selected=category;}
    }
    return selected;
}
bool ai_road_lane_transition(const AiLaneTransitionInput& in,int from,int to,
                              bool available,bool& output) noexcept {
    if(!((to>=0&&to<=4)||(to>=11&&to<=16))||!available){output=false;return true;}
    if(from<11||from>16||to<11||to>16||(in.track_number!=1206&&in.track_number!=1214)||from==to||
       (in.direction!=0&&in.direction!=1)){output=true;return true;}
    if(!std::isfinite(in.progress))return false;
    struct Barrier {int from,to;float low,high;int direction;};
    static constexpr Barrier rows1206[]={
        {11,12,895.F,1122.F,0},
        {12,11,895.F,1122.F,0},
        {13,14,895.F,1122.F,0},
        {14,13,895.F,1122.F,0},
        {12,13,1277.F,1508.F,0},
        {13,12,1277.F,1508.F,0},
        {13,14,1046.F,1285.F,1},
        {14,13,1046.F,1285.F,1},
        {14,15,1434.F,1663.F,1},
        {15,14,1434.F,1663.F,1},
        {12,13,1434.F,1663.F,1},
        {13,12,1434.F,1663.F,1},
    };
    static constexpr Barrier rows1214[]={
        {11,12,882.F,1128.F,0},
        {12,11,882.F,1128.F,0},
        {13,14,882.F,1128.F,0},
        {14,13,882.F,1128.F,0},
        {12,13,1262.F,1517.F,0},
        {13,12,1262.F,1517.F,0},
        {13,14,1038.F,1294.F,1},
        {14,13,1038.F,1294.F,1},
        {14,15,1426.F,1665.F,1},
        {15,14,1426.F,1665.F,1},
        {12,13,1426.F,1665.F,1},
        {13,12,1426.F,1665.F,1},
    };
    const auto* rows=in.track_number==1206?rows1206:rows1214;
    int current=from;
    while(current!=to) {
        const int next=current+(current<to?1:-1);
        for(unsigned j=0;j<std::size(rows1206);++j) {
            const auto& row=rows[j];
            if(row.from==current&&row.to==next&&row.direction==in.direction&&
               row.low<=in.progress&&in.progress<=row.high){output=false;return true;}
        }
        current=next;
    }
    output=true;return true;
}
bool drag_lane_step(const Route& route,unsigned point,const AiDriverPoint& position,
                    const AiRoadChain& chain,const std::array<bool,7>& available,
                    int held,float travelled_distance,DragLaneState& state,const AiLaneTransitionInput& transition) noexcept {
    if(held< -1||held>1||!std::isfinite(travelled_distance)||chain.count==0)return false;
    const int nearest=ai_road_nearest_lane(route,point,position);
    if(nearest<11)return false;
    auto next=state;
    const auto shifted=[&](int direction) {
        int lane=nearest;
        for(unsigned i=0;i<6;++i) {
            const int candidate=lane+direction;
            if(candidate<11||candidate>16)break;
            const bool destination=route.lane_enabled[unsigned(candidate-11)]&&
                (transition.controller_kind==2||available[unsigned(candidate-10)]);
            bool allowed=false;
            if(!ai_road_lane_transition(transition,lane,candidate,destination,allowed))return -1;
            if(!allowed)break;
            lane=candidate;
        }
        return lane;
    };
    //Input events run before the controller update. Original4605F9..46060D
    //reads actor3B8 travel distance, not actor3D4 speed. Equality admits input.
    //Neutral does not reset the bounds.
    if(travelled_distance>=2) {
        const int mode=held<0?0:held>0?1:2;
        if(mode!=next.mode) {
            next.mode=mode;
            if(held) {
                next.exclude_first=next.exclude_last=255;
                next.category=shifted(held);
                if(next.category!=nearest) {
                    next.exclude_first=held<0?nearest:11;
                    next.exclude_last=held<0?16:nearest;
                }
            }
        }
    }
    if(next.exclude_first!=255||next.exclude_last!=255) {
        if(nearest<next.exclude_first||nearest>next.exclude_last) {
            AiRoadLocation at;
            if(!ai_road_locate_actor(chain,position,nearest,at))return false;
            if(std::abs(at.lateral)<.5F)next.exclude_first=next.exclude_last=255;
        }
    }
    next.category=next.mode==2?ai_road_nearest_lane(route,point,position,next.exclude_first,next.exclude_last):
        shifted(next.mode==0?-1:1);
    if(next.category<11)return false;
    state=next;return true;
}
bool ai_road_extend(const RacingLineTrack& track,const std::vector<AiRoadEvent>& events,
                    const std::array<bool,128>& reverse,int category,float horizon,
                    const AiRoadPolicyInput& input,AiRoadPolicy& policy,std::uint32_t& rng,
                    AiRoadChain& chain) noexcept {
    if(!road_category(category)||chain.count==0||chain.count>40||!std::isfinite(horizon))return false;
    auto next=chain;auto decisions=policy;auto random=rng;
    auto last=next.cells[next.count-1];
    AiRoadCursor cursor{last.point.route_index,last.point_index};
    if(cursor.route<0||cursor.route>=128||!std::isfinite(last.distance))return false;
    auto offsets=last.offset;unsigned attempts=0;
    while((last.distance<=horizon||next.count<10)&&next.count<40) {
        auto old=cursor;bool junction=false;
        auto advance=[&](){return ai_road_next(track,events,reverse[static_cast<unsigned>(cursor.route)],
                                             input,decisions,random,cursor,junction);};
        auto result=advance();if(result==AiRoadAdvance::unsupported)return false;
        if(result==AiRoadAdvance::end)break;
        const auto* from_route=track.find(old.route);const auto& from=from_route->points[old.point];
        const RoutePoint* to=&track.find(cursor.route)->points[cursor.point];
        float distance=from.distance_to_next;
        const bool connected=junction;
        if(connected) {
            distance=projected_distance(from,*to);
            while(distance<=.1F) {
                result=advance();if(result==AiRoadAdvance::unsupported)return false;
                if(result==AiRoadAdvance::end){policy=decisions;rng=random;chain=next;return true;}
                to=&track.find(cursor.route)->points[cursor.point];
                distance=projected_distance(from,*to);
                if(++attempts>100){policy=decisions;rng=random;chain=next;return true;}
            }
        }
        for(unsigned k=0;k<7;++k) {
            const int lane=k==0?category:static_cast<int>(k)+10;
            if(connected)offsets[k]=static_cast<float>(junction_offset(from,*to,lane)+offsets[k]);
            else {
                const float delta=.04F*distance;
                offsets[k]=static_cast<float>(offsets[k]>0?std::max(0.F,offsets[k]-delta):std::min(0.F,offsets[k]+delta));
            }
            offsets[k]=clamp_offset(*to,lane,offsets[k]);
            if(!std::isfinite(offsets[k]))return false;
        }
        AiPathRoadPoint cell;cell.point=*to;cell.point_index=cursor.point;
        cell.offset=offsets;cell.distance=last.distance+distance;
        if(!std::isfinite(cell.distance)||!ai_path_road_bounds(cell,category))return false;
        next.junction[next.count]=connected;next.cells[next.count++]=cell;last=cell;
        if(++attempts>100)break;
    }
    policy=decisions;rng=random;chain=next;return true;
}
bool ai_road_stop_event_reached(const AiRoadChain& chain,bool event_flag,float distance,bool& reached) noexcept {
    if(chain.count>chain.cells.size()||!std::isfinite(distance))return false;
    unsigned first=chain.count;
    for(unsigned i=0;i<chain.count;++i) {
        if(!std::isfinite(chain.cells[i].distance))return false;
        if(first==chain.count&&distance<=chain.cells[i].distance)first=i;
    }
    reached=event_flag&&(first==chain.count||chain.count-first<2);return true;
}
bool ai_road_propagate_bounds(AiRoadChain& chain,int category,float curvature) noexcept {
    if(chain.count==0||chain.count>40||!road_category(category)||!std::isfinite(curvature))return false;
    auto next=chain;
    if(!ai_path_road_bounds(next.cells[next.count-1],category))return false;
    for(unsigned n=next.count-1;n>0;--n) {
        auto& cell=next.cells[n-1];const auto& ahead=next.cells[n];
        if(!ai_path_road_bounds(cell,category))return false;
        const float distance=(ahead.distance-cell.distance)*.1F;
        if(!std::isfinite(distance))return false;
        float lower_delta=distance,upper_delta=distance;
        const float width=cell.upper[0]-cell.lower[0];
        if(curvature<=-.12F) {
            if(cell.lower[0]>-4.F&&width>=15.F)cell.lower[0]=0;
            upper_delta=3.F*distance;
        }
        if(curvature>=.12F) {
            if(cell.upper[0]<4.F&&width>=15.F)cell.upper[0]=0;
            lower_delta=3.F*distance;
        }
        for(unsigned k=0;k<7;++k) {
            cell.lower[k]=std::max(cell.lower[k],ahead.lower[k]-(k==0?lower_delta:distance));
            cell.upper[k]=std::min(cell.upper[k],ahead.upper[k]+(k==0?upper_delta:distance));
        }
    }
    chain=next;return true;
}
bool ai_road_nearest(const AiRoadChain& chain,const AiDriverPoint& position,int start,
                     unsigned flags,int& result) noexcept {
    if(chain.count>40||!std::isfinite(position[0])||!std::isfinite(position[1]))return false;
    int selected=-1;float best=1.0e14F;
    for(unsigned j=static_cast<unsigned>(std::max(0,start));j<chain.count;++j) {
        const auto& cell=chain.cells[j];const auto& p=cell.point;
        const float dx=p.position[0]-position[0],dy=p.position[1]-position[1];
        const float distance=std::sqrt(dy*dy+dx*dx);
        const float projected=(position[1]-p.position[1])*p.forward[1]+(position[0]-p.position[0])*p.forward[0]+cell.distance;
        if(!std::isfinite(distance)||!std::isfinite(projected))return false;
        if((!(flags&1)||projected<cell.distance)&&(!(flags&2)||projected>cell.distance)&&distance<best) {
            best=distance;selected=static_cast<int>(j);
        }
        const float skip_distance=distance*-.0416666679084301F;
        if(skip_distance<=static_cast<float>(std::numeric_limits<int>::min()))return false;
        const int skip=-2-static_cast<int>(skip_distance);
        if(skip>0) {
            if(static_cast<unsigned>(skip)>=chain.count-j)break;
            j+=static_cast<unsigned>(skip);
        }
    }
    result=selected;return true;
}
bool ai_road_curvature(const AiRoadChain& chain,unsigned first,int category,
                       float lookahead,float& result) noexcept {
    if(chain.count>40||first>=chain.count||!road_category(category)||!std::isfinite(lookahead))return false;
    if(chain.count-first<2){result=0;return true;}
    unsigned middle=chain.count,end=chain.count;
    if(lookahead>=0) {
        const float base=chain.cells[first].distance;
        for(unsigned j=first;j<chain.count;++j)if(chain.cells[j].distance-base>=lookahead*.5F){middle=j;break;}
        for(unsigned j=middle==chain.count?0:middle+1;j<chain.count;++j)
            if(chain.cells[j].distance-base>=lookahead){end=j;break;}
    }
    if(end==chain.count||middle==chain.count) {middle=(chain.count-first)/2;end=chain.count-1;}
    auto sample=[&](unsigned i) {
        const auto& c=chain.cells[i];const auto& p=c.point;
        const float offset=authored_lane(p,category)+c.offset[road_slot(category)];
        return std::array<float,2>{p.position[0]+offset*p.forward[1],p.position[1]-offset*p.forward[0]};
    };
    const auto a=sample(first),b=sample(middle),c=sample(end);
    auto unit=[](float x,float y) {
        const float length=std::sqrt(x*x+y*y);
        if(length==0)return std::array<float,2>{1,0};
        const float inverse=1.F/length;return std::array<float,2>{inverse*x,inverse*y};
    };
    const auto u=unit(a[0]-b[0],a[1]-b[1]);const auto v=unit(b[0]-c[0],b[1]-c[1]);
    const float cross=u[0]*v[1]-u[1]*v[0];
    float value=std::sqrt(cross*cross);if(cross<0)value=-value;
    if(!std::isfinite(value))return false;
    result=value;return true;
}
bool ai_road_refresh(const RacingLineTrack& track,const std::vector<AiRoadEvent>& events,
                     const std::array<bool,128>& reverse,AiRoadCursor selected,int category,
                     const AiDriverPoint& position,bool keep_previous,float initial_horizon,float retained_horizon,
                     const AiRoadPolicyInput& input,AiRoadPolicy& policy,std::uint32_t& rng,AiRoadChain& chain,
                     int bounds_category) noexcept {
    if(chain.count>40||!road_category(category)||selected.route<0||selected.route>=128||
       !std::isfinite(initial_horizon)||!std::isfinite(retained_horizon))return false;
    if(bounds_category==-1)bounds_category=category;
    if(!road_category(bounds_category))return false;
    const auto* route=track.find(selected.route);
    if(!route||selected.point>=route->points.size())return false;
    auto next=chain;auto decisions=policy;auto random=rng;
    unsigned found=next.count;
    for(unsigned j=0;j<next.count;++j)if(next.cells[j].point.route_index==selected.route&&next.cells[j].point_index==selected.point){found=j;break;}
    float horizon=initial_horizon;
    if(found<next.count) {
        const unsigned remove=keep_previous?(found>20?found-20:0):found;
        const float origin=next.cells[remove].distance;
        const float base=next.cells[found].distance-origin;
        next.count-=remove;
        for(unsigned j=0;j<next.count;++j) {
            next.cells[j]=next.cells[j+remove];next.junction[j]=next.junction[j+remove];
            next.cells[j].distance-=origin;
        }
        horizon=base+retained_horizon;
        next.search_origin=static_cast<int>(found);
    }else{
        next={};next.count=1;next.cells[0].point=route->points[selected.point];next.cells[0].point_index=selected.point;
    }
    if(!ai_road_extend(track,events,reverse,bounds_category,horizon,input,decisions,random,next))return false;
    int nearest=-1;float curvature=0;
    if(!ai_road_nearest(next,position,0,1,nearest))return false;
    if(nearest>=0&&!ai_road_curvature(next,static_cast<unsigned>(nearest),bounds_category,-1,curvature))return false;
    if(!ai_road_propagate_bounds(next,bounds_category,curvature))return false;
    chain=next;policy=decisions;rng=random;return true;
}
bool ai_road_select(const RacingLineTrack& track,const AiRoadSelectionInput& in,
                    bool& found,AiRoadCursor& selected) noexcept {
    if(in.first_route<0||in.last_route>=128||in.first_route>in.last_route||
       !std::isfinite(in.progress)||!std::isfinite(in.maximum_distance)||in.maximum_distance<0)return false;
    for(float v:in.position)if(!std::isfinite(v))return false;
    float best=in.maximum_distance;bool have=false;AiRoadCursor result;
    for(unsigned pass=0;pass<2&&!have;++pass)for(int rid=in.first_route;rid<=in.last_route;++rid) {
        const auto* route=track.find(static_cast<std::int16_t>(rid));if(!route)continue;
        if(pass==0) {
            const bool after=in.progress>=route->progress_begin,before=in.progress<=route->progress_end;
            if(route->progress_begin<route->progress_end?!(after&&before):!(after||before))continue;
        }
        if(in.require_enabled_lane) {
            bool enabled=false;const unsigned first=in.direction==0?0:6;
            for(unsigned k=first;k<first+6;++k)enabled|=route->lane_enabled[k];
            if(!enabled)continue;
        }
        const auto& b=route->bounds;const float range=in.maximum_distance;
        if(b[0]-range>in.position[0]||b[2]+range<in.position[0]||
           b[1]-range>in.position[1]||b[3]+range<in.position[1])continue;
        float nearest=range;int point=-1;
        for(std::size_t j=0;j<route->points.size();++j) {
            const auto& p=route->points[j];
            const float dx=p.position[0]-in.position[0],dy=p.position[1]-in.position[1];
            const float distance=std::sqrt(dy*dy+dx*dx);
            const float ahead=-((in.position[1]-p.position[1])*p.forward[1]+(in.position[0]-p.position[0])*p.forward[0]);
            const std::uint32_t flags=p.unknown_84[0]|(std::uint32_t(p.unknown_84[1])<<8)|
                (std::uint32_t(p.unknown_84[2])<<16)|(std::uint32_t(p.unknown_84[3])<<24);
            const bool eligible=(in.role!=2||(flags&0x40))&&(in.role!=3||(flags&0x2000000))&&(in.role!=1||(flags&0x8000000));
            if(!std::isfinite(distance)||!std::isfinite(ahead))return false;
            if(ahead>.1F&&distance<nearest&&eligible){nearest=distance;point=static_cast<int>(j);}
            const float skip_distance=distance*-.0416666679084301F;
            if(skip_distance<=static_cast<float>(std::numeric_limits<int>::min()))return false;
            const int skip=-2-static_cast<int>(skip_distance);
            if(skip>0){if(static_cast<std::size_t>(skip)>=route->points.size()-j)break;j+=static_cast<std::size_t>(skip);}
        }
        if(point<0||rid==in.excluded_route)continue;
        const auto& p=route->points[static_cast<unsigned>(point)];
        const float dx=in.position[0]-p.position[0],dy=in.position[1]-p.position[1],dz=in.position[2]-p.height;
        const float distance=std::sqrt((dx*dx+dy*dy)+dz*dz);
        const float lateral=dx*p.forward[1]-dy*p.forward[0];
        if(!in.allow_outside_width&&(lateral>0?lateral-p.unknown_28:-(lateral+p.unknown_24))>=0)continue;
        if(distance<best){best=distance;result={static_cast<std::int16_t>(rid),static_cast<std::uint16_t>(point)};have=true;}
    }
    found=have;if(have)selected=result;return true;
}
bool ai_road_locate(const AiRoadChain& chain,const AiDriverPoint& position,int category,
                    AiRoadLocation& result) noexcept {
    if(chain.count>40||!road_category(category))return false;
    for(float v:position)if(!std::isfinite(v))return false;
    if(!chain.count){result={};return true;}
    auto along=[&](unsigned i) {
        const auto& c=chain.cells[i];const auto& p=c.point;
        return ((position[1]-p.position[1])*p.forward[1]+
                (position[0]-p.position[0])*p.forward[0])+c.distance;
    };
    unsigned selected=chain.count-1,previous=selected;
    for(unsigned j=0;j<chain.count;++j) {
        const float projection=along(j);
        if(!std::isfinite(projection))return false;
        if(projection<chain.cells[j].distance){selected=j;previous=j?j-1:0;break;}
    }
    const auto& c=chain.cells[selected];const auto& p=c.point;
    const float cross=(position[0]-p.position[0])*p.forward[1]-
                      (position[1]-p.position[1])*p.forward[0];
    const float lateral=cross-(authored_lane(p,category)+c.offset[road_slot(category)]);
    const float distance=along(previous);
    if(!std::isfinite(distance)||!std::isfinite(lateral))return false;
    result={selected,distance,lateral};return true;
}
bool ai_road_locate_actor(const AiRoadChain& chain,const AiDriverPoint& position,int category,
                          AiRoadLocation& result) noexcept {
    AiRoadLocation location;
    if(!ai_road_locate(chain,position,category,location)||!chain.count)return false;
    const auto& c=chain.cells[location.cell?location.cell-1:0];const auto& p=c.point;
    const float cross=(position[0]-p.position[0])*p.forward[1]-(position[1]-p.position[1])*p.forward[0];
    location.lateral=cross-(authored_lane(p,category)+c.offset[road_slot(category)]);
    if(!std::isfinite(location.lateral))return false;
    result=location;return true;
}
bool ai_traffic_speed(const AiRoadChain& chain,const AiTrafficSpeedInput& in,
                      std::array<float,3>& output) noexcept {
    if(!chain.count||chain.count>chain.cells.size()||in.first>=chain.count||!road_category(in.category)||
       !std::isfinite(in.distance)||!std::isfinite(in.absolute_speed)||in.absolute_speed<0||!std::isfinite(in.cap))return false;
    for(float value:in.action)if(!std::isfinite(value))return false;
    for(unsigned i=0;i<chain.count;++i) {
        const auto& cell=chain.cells[i];
        if(!std::isfinite(cell.distance)||!std::isfinite(cell.point.unknown_30)||
           (i&&cell.distance<=chain.cells[i-1].distance))return false;
    }
    float limit=std::min(in.cap,24.58650016784668F);
    const float scaled=in.absolute_speed*1.3F;
    if(!std::isfinite(scaled))return false;
    const float shape_horizon=std::max(scaled,20.F);
    float drop=99999.F,drop_at=99999.F;
    //5A6BC0 passes the RAW road record to virtual18.4550C0 reads its+30
    // longitudinal slope; compact-cell+30 is unrelated temporary geometry.
    for(unsigned i=in.first;i+1<chain.count;++i) {
        if(shape_horizon<chain.cells[i].distance)break;
        const float delta=chain.cells[i+1].point.unknown_30-chain.cells[i].point.unknown_30;
        if(!std::isfinite(delta))return false;
        if(delta<drop){drop=delta;drop_at=chain.cells[i].distance;}
    }
    if(drop<-.1F) {
        // Original7044A0/6F0908 and454F30, then583910's cap blend.
        const float curve=drop_at<10.F?1.F:drop_at>=40.F?0.F:
            (1.F-(drop_at-10.F)/30.F);
        limit=(1.F-curve)*(limit-10.F)+10.F;
    }
    const auto speed_at=[&](float distance) {
        unsigned next=0;
        while(next<chain.count&&chain.cells[next].distance<=distance)++next;
        if(!next)return float(chain.cells[0].point.speed[unsigned(in.category)]);
        if(next==chain.count)return float(chain.cells[chain.count-1].point.speed[unsigned(in.category)]);
        const auto& a=chain.cells[next-1];const auto& b=chain.cells[next];
        const float blend=(b.distance-distance)/(b.distance-a.distance);
        return float(a.point.speed[unsigned(in.category)])*blend+
            (1.F-blend)*float(b.point.speed[unsigned(in.category)]);
    };
    const float ahead=in.distance+std::max(scaled,8.F);
    if(!std::isfinite(ahead))return false;
    limit=std::min(limit,speed_at(in.distance));
    const float forward=speed_at(ahead);if(forward<=limit)limit=forward;
    if(in.stopped)limit=0;
    auto next=in.route_speed?std::array<float,3>{{limit,0,0}}:in.action;
    if(limit<=next[0])next[0]=limit;
    if(!std::isfinite(next[0]))return false;
    output=next;return true;
}

bool ai_road_category_available(const RacingLineTrack& track,AiRoadCursor cursor,int category) noexcept {
    const auto* route=track.find(cursor.route);
    return route&&cursor.point<route->points.size()&&road_category(category)&&(category<5?route->category_enabled[category]:route->lane_enabled[unsigned(category-11)]);
}
bool ai_road_flags(const RacingLineTrack& track,const std::vector<AiRoadEvent>& events,
                   const AiRoadChain& chain,int category,AiRoadFlags& result) noexcept {
    if(chain.count>40||!road_category(category))return false;
    AiRoadFlags next;
    for(unsigned j=static_cast<unsigned>(std::max(0,chain.search_origin));j<chain.count;++j) {
        const auto& p=chain.cells[j].point;
        if(j==0||p.route_index!=chain.cells[j-1].point.route_index) {
            const auto* route=track.find(p.route_index);if(!route)return false;
            next.enabled[0]=next.enabled[0]&&(category<5?route->category_enabled[category]:route->lane_enabled[unsigned(category-11)]);
            for(unsigned k=0;k<6;++k)next.enabled[k+1]=next.enabled[k+1]&&route->lane_enabled[k];
        }
        const auto flags=std::uint32_t(p.unknown_84[0])|(std::uint32_t(p.unknown_84[1])<<8)|
            (std::uint32_t(p.unknown_84[2])<<16)|(std::uint32_t(p.unknown_84[3])<<24);
        if((flags&0x1f00000U)!=0x1f00000U&&next.first_limited<0)next.first_limited=static_cast<int>(j);
        for(auto id:{p.event_a,p.event_b}) {
            if(id==255||next.first_obstacle>=0)break;
            if(id>=events.size())return false;
            if(events[id].type==9)next.first_obstacle=static_cast<int>(j);
            if(events[id].context_flag)next.event_flag=true;
        }
    }
    result=next;return true;
}
bool ai_road_horizons(float speed,float maneuver,float prediction,AiRoadHorizons& out) noexcept {
    if(!std::isfinite(speed)||speed<0||!std::isfinite(maneuver)||!std::isfinite(prediction))return false;
    const float seconds=std::max(std::max(maneuver,prediction),2.F);
    const AiRoadHorizons next{std::max(50.F,seconds*20.F),std::max(50.F,seconds*speed)};
    if(!std::isfinite(next.initial)||!std::isfinite(next.retained))return false;
    out=next;return true;
}
bool ai_road_update_context(const RacingLineTrack& track,const std::vector<AiRoadEvent>& events,
    const std::array<bool,128>& reverse,const AiRoadContextInput& in,const AiRoadPolicyInput& input,
    AiRoadPolicy& policy,std::uint32_t& rng,AiRoadContext& context,AiRoadContextResult& result) noexcept {
    const bool traffic=input.policy==AiRoadDecisionPolicy::traffic;
    const bool unselected_traffic=traffic&&!context.selected&&!context.chain.count&&context.category==-1;
    if((!road_category(context.category)&&!unselected_traffic)||context.chain.count>40||
       (in.ride_category!=-1&&!road_category(in.ride_category))||
       !std::isfinite(in.body_width)||!std::isfinite(in.back_margin))return false;
    auto next=context;auto decisions=policy;auto random=rng;AiRoadContextResult output;
    bool admitted=false;const auto& position=in.selection.position;
    for(float v:position)if(!std::isfinite(v))return false;
    if(next.chain.count) {
        AiRoadLocation location;if(!ai_road_locate_actor(next.chain,position,next.category,location))return false;
        unsigned nearest=0;float best=0;
        for(unsigned j=0;j<next.chain.count;++j) {
            const float distance=std::fabs(next.chain.cells[j].distance-location.distance);
            if(!std::isfinite(distance))return false;
            if(j==0||distance<best){nearest=j;best=distance;}
        }
        const auto& c=next.chain.cells[nearest];
        const float outside=location.lateral>0?location.lateral-c.upper[road_slot(next.category)]:c.lower[road_slot(next.category)]-location.lateral;
        admitted=location.distance>=next.chain.cells[0].distance-in.back_margin&&
            location.distance<=next.chain.cells[next.chain.count-1].distance&&
            (outside<=in.body_width||in.allow_wide);
        if(admitted) {
            next.selected=true;next.road={c.point.route_index,c.point_index};
            for(unsigned j=static_cast<unsigned>(std::max(0,next.nearest));j<=nearest;++j)if(j&&next.chain.junction[j]) {
                // 5A6D20 returns the first prior-point event targeting this
                // route, not the prior route itself. Null is also notified.
                const auto& previous=next.chain.cells[j-1].point;std::int16_t selected_event=-1;
                for(auto id:{previous.event_a,previous.event_b}) {
                    if(id==255)break;
                    if(id>=events.size())return false;
                    if(events[id].target_route==next.chain.cells[j].point.route_index){selected_event=id;break;}
                }
                output.passed_events[output.passed_count++]=selected_event;
                // 462A00 clears a consumed marker before refreshing decisions.
                if(!traffic&&selected_event>=0&&decisions.marker==selected_event)decisions.marker=-1;
            }
        }
    }else if(next.selected) {
        const auto* route=track.find(next.road.route);
        if(!route||next.road.point>=route->points.size())return false;
        const auto& p=route->points[next.road.point];
        const float dx=position[0]-p.position[0],dy=position[1]-p.position[1];
        const float along=dy*p.forward[1]+dx*p.forward[0];
        const float lateral=dx*p.forward[1]-dy*p.forward[0];
        const float outside=lateral>0?lateral-p.unknown_28:-(lateral+p.unknown_24);
        admitted=outside<=in.body_width&&along<=p.distance_to_next&&along>=-p.distance_to_previous;
    }
    if(!admitted) {
        for(auto& slot:decisions.cache)slot&=~1U;
        next.selected=false;
        if(!ai_road_select(track,in.selection,next.selected,next.road))return false;
        if(traffic) {
            next.category=-1;
            if(next.selected) {
                const auto* route=track.find(next.road.route);
                if(!route)return false;
                next.category=std::max(0,ai_road_nearest_lane(*route,next.road.point,position));
            }
        }
        next.nearest=-1;decisions.marker=-1;output.reselected=true;
    }
    if(next.selected) {
        // 44ACB0 is evaluated AFTER publishing the selected raw point.
        const auto* route=track.find(next.road.route);
        if(!route||next.road.point>=route->points.size())return false;
        const auto& p=route->points[next.road.point];
        const float lateral=(position[0]-p.position[0])*p.forward[1]-(position[1]-p.position[1])*p.forward[0];
        const float outside=lateral>0?lateral-p.unknown_28:-(lateral+p.unknown_24);
        output.clear_width=outside>in.body_width;
        const int bounds_category=in.ride_category==-1?next.category:in.ride_category;
        if(!ai_road_refresh(track,events,reverse,next.road,next.category,position,in.keep_previous,
            in.initial_horizon,in.retained_horizon,input,decisions,random,next.chain,bounds_category))return false;
        if(!ai_road_flags(track,events,next.chain,bounds_category,next.flags)||
           !ai_road_nearest(next.chain,position,0,1,next.nearest))return false;
        if(next.nearest<0){next.chain={};next.flags={};}
        else {
            const auto& c=next.chain.cells[static_cast<unsigned>(next.nearest)];
            const AiRoadCursor current{c.point.route_index,c.point_index};
            const auto available=[&](int category) {
                //44CF30 calls5A75C0 for every controller. Its owner2 bypass
                //does not extend to role2/controller3 drag rivals. Manual
                //5A7700 divider commands belong to45D970, not this query.
                return ai_road_category_available(track,current,category)&&
                    (category<11||in.controller_kind==2||next.flags.enabled[unsigned(category-10)]);
            };
            output.category_available=available(next.category);
            if(!output.category_available&&next.category>=11&&next.category<=16) {
                bool admitted_lane=false;
                for(int distance=1;distance<6&&!admitted_lane;++distance)for(int sign:{1,-1}) {
                    const int category=next.category+sign*distance;
                    if(available(category)) {
                        next.category=category;output.reset_maneuver=true;admitted_lane=true;break;
                    }
                }
                if(!admitted_lane) {
                    const auto* route=track.find(current.route);if(!route)return false;
                    const int category=ai_road_nearest_lane(*route,current.point,position);
                    if(category!=-1)next.category=category;
                }
            }
        }
    }else{next.chain={};next.flags={};}
    context=next;policy=decisions;rng=random;result=output;return true;
}
} // namespace nfsu
