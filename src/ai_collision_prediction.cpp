#include "nfsu/ai_collision_prediction.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/race_route.h"
#include <algorithm>
#include <cmath>
namespace nfsu {
namespace {
using Pair=std::array<float,2>;
float dot(const Pair& a,const Pair& b){return a[1]*b[1]+a[0]*b[0];}
Pair interval(float velocity,float position,float radius) {
    if(std::fabs(velocity)<9.999999747378752e-05F)
        return std::fabs(position)>radius?Pair{9999,9999}:Pair{-9999,9999};
    const float inverse=1.F/velocity;
    const float high=(radius-position)*inverse;
    const float low=-((position+radius)*inverse);
    return velocity<0?Pair{high,low}:Pair{low,high};
}
bool intersect(Pair& current,const Pair& other) {
    //4584F0 sorts four endpoints and tests the lower middle against BOTH
    // input intervals. Preserve equal-endpoint admission and signed values.
    std::array<float,4> v{{current[0],current[1],other[0],other[1]}};
    for(unsigned n=3;n>0;--n)for(unsigned k=0;k<n;++k)if(v[k+1]<=v[k])std::swap(v[k],v[k+1]);
    if(v[1]<current[0]||v[1]>current[1]||v[1]<other[0]||v[1]>other[1])return false;
    current={v[1],v[2]};return true;
}
bool finite(const AiDriverPoint& p){for(float v:p)if(!std::isfinite(v))return false;return true;}
//564BD0 computes cosine independently, not by shifting the sine argument.
Pair heading_axes(std::uint16_t heading) {
    float x=static_cast<float>(heading)*9.58738019107841e-5F,sign=1.F;
    if(x>=4.71238899230957F)x-=6.2831854820251465F;
    else if(x>=1.5707963705062866F){x-=3.1415927410125732F;sign=-1.F;}
    const float x2=x*x,x3=x2*x,x4=x2*x2,x5=x3*x2,x6=x4*x2,x7=x5*x2,x8=x6*x2;
    float sine=x-x3*0.1666666716337204F;
    sine+=x5*0.008333333767950535F;
    sine-=x7*0.00019841270113829523F;
    sine+=(x7*x2)*2.7557318844628753e-6F;
    float cosine=1.F-.5F*x2;
    cosine+=x4*0.041666667908430099F;
    cosine-=x6*0.0013888889225199819F;
    cosine+=x8*2.4801587642286904e-05F;
    cosine-=(x8*x2)*2.755731998149713e-07F;
    return {cosine*sign,sine*sign};
}
}
bool ai_prediction_overlap(const AiPredictionBox& a,const AiPredictionBox& b,
    const Pair& position,const Pair& velocity,float& output) noexcept {
    for(const auto* box:{&a,&b}) {
        for(const auto& axis:box->axes)for(float v:axis)if(!std::isfinite(v))return false;
        for(float v:box->extent)if(!std::isfinite(v))return false;
    }
    for(float v:position)if(!std::isfinite(v))return false;
    for(float v:velocity)if(!std::isfinite(v))return false;
    const float r00=dot(a.axes[0],b.axes[0]),r01=dot(a.axes[0],b.axes[1]);
    const float r10=dot(a.axes[1],b.axes[0]),r11=dot(a.axes[1],b.axes[1]);
    const std::array<float,4> radii{{
        (std::fabs(r11*b.extent[1])+std::fabs(r10*b.extent[0]))+std::fabs(a.extent[1]),
        (std::fabs(r01*b.extent[1])+std::fabs(r00*b.extent[0]))+std::fabs(a.extent[0]),
        (std::fabs(r11*a.extent[1])+std::fabs(r01*a.extent[0]))+std::fabs(b.extent[1]),
        (std::fabs(r10*a.extent[1])+std::fabs(r00*a.extent[0]))+std::fabs(b.extent[0])}};
    const std::array<Pair,4> axes{{a.axes[1],a.axes[0],b.axes[1],b.axes[0]}};
    std::array<Pair,4> windows;
    for(unsigned k=0;k<4;++k) {
        const float v=dot(velocity,axes[k]),p=dot(position,axes[k]);
        if(!std::isfinite(radii[k])||!std::isfinite(v)||!std::isfinite(p))return false;
        windows[k]=interval(v,p,radii[k]);
        if(!std::isfinite(windows[k][0])||!std::isfinite(windows[k][1]))return false;
    }
    auto overlap=windows[0];float result=99999;
    if(intersect(overlap,windows[1])&&intersect(overlap,windows[2])&&intersect(overlap,windows[3])) {
        if(overlap[0]>0)result=overlap[0];
        else if(overlap[0]<0&&overlap[1]>0)result=0;
    }
    output=result;return true;
}
bool ai_prediction_shapes(const AiDriverPath& path,const AiDriverPoint& low,
    const AiDriverPoint& high,std::uint16_t heading,float scale,AiPredictionPath& output) noexcept {
    if(path.count>20||!finite(low)||!finite(high)||!std::isfinite(scale))return false;
    AiDriverPoint extent;
    for(unsigned k=0;k<3;++k){extent[k]=std::fabs((high[k]-low[k])*.5F)*scale;if(!std::isfinite(extent[k]))return false;}
    AiPredictionPath next;next.count=path.count;const auto first_axis=heading_axes(heading);
    for(unsigned i=0;i<path.count;++i) {
        if(!finite(path.points[i]))return false;
        Pair axis=first_axis;
        if(i) {
            const float x=path.points[i-1][0]-path.points[i][0],y=path.points[i-1][1]-path.points[i][1];
            const float length=std::sqrt(x*x+y*y);
            if(!std::isfinite(length))return false;
            if(length!=0){const float inv=1.F/length;axis={x*inv,y*inv};}
            else axis={1,0};
        }
        auto& node=next.nodes[i];node.position=path.points[i];node.box.axes={axis,Pair{-axis[1],axis[0]}};
        node.box.extent={extent[0],extent[1]};node.height_extent=extent[2];
        for(float v:axis)if(!std::isfinite(v))return false;
    }
    output=next;return true;
}
bool ai_prediction_walk(const AiPredictionPath& a,const AiTrajectoryMotion& ma,
    const AiPredictionPath& b,const AiTrajectoryMotion& mb,float acceleration,
    float horizon,float& output) noexcept {
    if(a.count>20||b.count>20||ma.first>a.count||mb.first>b.count||
       !std::isfinite(acceleration)||!std::isfinite(horizon)||
       !std::isfinite(ma.speed)||!std::isfinite(mb.speed)||ma.speed<=0||mb.speed<=0)return false;
    for(float v:ma.position)if(!std::isfinite(v))return false;
    for(float v:mb.position)if(!std::isfinite(v))return false;
    for(const auto* path:{&a,&b})for(unsigned i=0;i<path->count;++i) {
        const auto& node=path->nodes[i];
        if(!finite(node.position))return false;
        for(const auto& axis:node.box.axes)for(float v:axis)if(!std::isfinite(v))return false;
        for(float v:node.box.extent)if(!std::isfinite(v))return false;
    }
    auto pa=ma.position,pb=mb.position;float sa=ma.speed,sb=mb.speed,elapsed=0,result=99999;
    unsigned ia=ma.first,ib=mb.first;
    while(ia<a.count&&ib<b.count&&elapsed<=horizon) {
        const auto& na=a.nodes[ia];const auto& nb=b.nodes[ib];
        const Pair da{na.position[0]-pa[0],na.position[1]-pa[1]},db{nb.position[0]-pb[0],nb.position[1]-pb[1]};
        const float la=std::sqrt(da[1]*da[1]+da[0]*da[0]),lb=std::sqrt(db[1]*db[1]+db[0]*db[0]);
        const float inv_a=1.F/sa,inv_b=1.F/sb,ta=la*inv_a,tb=lb*inv_b,dt=std::min(ta,tb);
        if(!std::isfinite(dt)||!std::isfinite(ta)||!std::isfinite(tb))return false;
        auto velocity=[](const Pair& d,float duration) {
            if(duration==0)return Pair{1,0};
            const float inv=1.F/duration;return Pair{d[0]*inv,d[1]*inv};
        };
        const auto va=velocity(da,la*inv_a),vb=velocity(db,lb*inv_b);
        float hit;
        if(!ai_prediction_overlap(na.box,nb.box,{pb[0]-pa[0],pb[1]-pa[1]},
                                   {vb[0]-va[0],vb[1]-va[1]},hit))return false;
        if(hit<=dt){result=hit+elapsed;break;}
        auto advance=[](Pair& p,const Pair& d,float distance,float speed,float step) {
            float x=0,y=0;
            if(std::fabs(distance)>9.999999747378752e-06F) {
                const float factor=(step*speed)/distance;x=d[0]*factor;y=d[1]*factor;
            }
            p[0]=x+p[0];p[1]=y+p[1];
        };
        advance(pa,da,la,sa,dt);advance(pb,db,lb,sb,dt);
        const float dv=dt*acceleration;sa+=dv;sb+=dv;
        if(std::fabs(ta-dt)<=.0010000000474974513F)++ia;else ++ib;
        elapsed=dt+elapsed;
        if(!std::isfinite(elapsed)||!std::isfinite(sa)||!std::isfinite(sb))return false;
    }
    if(!std::isfinite(result))return false;
    output=result;return true;
}
bool ai_physical_obstacle(const AiPhysicalObstacleInput& in,AiPhysicalObstacleResult& output) noexcept {
    for(const auto* p:{&in.self_min,&in.self_max,&in.position,&in.velocity,
                       &in.other_min,&in.other_max,&in.other_position})if(!finite(*p))return false;
    for(float x:in.other_forward)if(!std::isfinite(x))return false;
    for(float x:in.other_velocity)if(!std::isfinite(x))return false;
    if(!std::isfinite(in.signed_speed)||!std::isfinite(in.absolute_speed)||in.absolute_speed<0)return false;
    for(unsigned j=0;j<3;++j)if(in.self_min[j]>in.self_max[j]||
        (in.other_shape&&in.other_min[j]>in.other_max[j]))return false;
    const float self_width=(in.self_max[1]-in.self_min[1])*.5F;
    const float self_length=(in.self_max[0]-in.self_min[0])*.5F;
    const float other_length=in.other_shape?(in.other_max[0]-in.other_min[0])*.5F:0.F;
    const float other_width=in.other_shape?(in.other_max[1]-in.other_min[1])*.5F:0.F;
    const auto phase=static_cast<std::uint16_t>(track_angle(in.other_forward[0],in.other_forward[1])+0x4000-in.heading);
    const float sine=player_angle_sin(phase),square=sine*sine;
    const float lateral_extent=(1.F-square)*other_length+other_width*square;
    PlayerRigidBodyState frame;
    const float velocity_square=(in.velocity[0]*in.velocity[0]+in.velocity[2]*in.velocity[2])+in.velocity[1]*in.velocity[1];
    if(!std::isfinite(velocity_square))return false;
    if(velocity_square>1.F) {
        const float sign=in.signed_speed<0?-1.F:1.F;
        const float x=in.velocity[0]*sign,y=in.velocity[1]*sign;
        frame.axes={{{{y,-x,0}},{{x,y,0}},{{0,0,1}}}};
    } else {
        const float sine_self=player_angle_sin(in.heading);
        const float cosine_self=player_angle_sin(static_cast<std::uint16_t>(in.heading+0x4000));
        frame.axes={{{{sine_self,-cosine_self,0}},{{cosine_self,sine_self,0}},{{0,0,1}}}};
    }
    player_rigid_body_normalize(frame);
    const float dx=in.other_position[0]-in.position[0],dy=in.other_position[1]-in.position[1];
    const float across=((frame.axes[2][0]*0.F+frame.axes[1][0]*dy)+dx*frame.axes[0][0])+0.F;
    const float along=((frame.axes[2][1]*0.F+frame.axes[1][1]*dy)+dx*frame.axes[0][1])+0.F;
    AiPhysicalObstacleResult next;
    if(along>=0&&std::fabs(across)<=lateral_extent+self_width) {
        const float distance=(along-other_length)-self_length;
        next.distance=distance>0?distance:0;
    }
    if(in.absolute_speed>.1F) {
        const float x=in.velocity[0],y=in.velocity[1];
        const float denominator=x*x+y*y;
        if(!(denominator>0)||!std::isfinite(denominator))return false;
        const float factor=((in.other_velocity[0]-x)*x+y*(in.other_velocity[1]-y))/denominator;
        const float vx=x*factor,vy=y*factor;
        const float speed=std::sqrt(vx*vx+vy*vy);
        if(vx*x+y*vy<0&&speed>.001F)next.time=next.distance/speed;
    }
    if(!std::isfinite(next.distance)||!std::isfinite(next.time))return false;
    output=next;return true;
}
} // namespace nfsu
