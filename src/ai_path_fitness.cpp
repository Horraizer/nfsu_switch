#include "nfsu/ai_path.h"
#include <cmath>

namespace nfsu {
namespace {
using Pair=std::array<float,2>;
constexpr float epsilon=.0010000000474974513F;
constexpr float small=9.999999747378752e-06F;
float distance(const AiDriverPoint& a,const AiDriverPoint& b) {
    const float x=a[0]-b[0],y=a[1]-b[1];return std::sqrt(y*y+x*x);
}
Pair delta(const AiDriverPoint& a,const AiDriverPoint& b){return {a[0]-b[0],a[1]-b[1]};}
Pair scaled(Pair v,float speed) {
    //567310: divide length by requested magnitude before reciprocal.
    const float factor=std::sqrt(v[1]*v[1]+v[0]*v[0])/speed;
    if(factor==0)return {1,0};
    const float inverse=1.F/factor;return {inverse*v[0],inverse*v[1]};
}
float travel_time(float length,float before,float after) {
    if(std::fabs(before)<=epsilon&&std::fabs(after)<=epsilon)return 9999999.F;
    return length/((after+before)*.5F);
}
bool acceleration(Pair after,Pair before,float length,float old_speed,float& speed,float limit) {
    //45A0B0: perpendicular acceleration after removing longitudinal component.
    const float time=travel_time(length,old_speed,speed);
    if(std::fabs(time)<=epsilon){speed=old_speed;return true;}
    const float inverse=1.F/time;
    Pair a{(after[0]-before[0])*inverse,(after[1]-before[1])*inverse};
    const float square=before[1]*before[1]+before[0]*before[0];
    Pair parallel{};
    if(std::fabs(square)>small) {
        const float projection=(a[0]*before[0]+a[1]*before[1])/square;
        parallel={projection*before[0],projection*before[1]};
    }
    a[0]-=parallel[0];a[1]-=parallel[1];
    const float lateral=std::sqrt(a[1]*a[1]+a[0]*a[0]);
    return lateral<limit||std::fabs(lateral-limit)<=epsilon;
}
}
bool ai_path_predict(AiPlannedPath& path,const AiPathPredictionInput& in) noexcept {
    if(path.path.count>20||in.first>path.path.count||!std::isfinite(in.horizon))return false;
    for(const auto& v:{in.position,in.velocity,in.acceleration})for(float x:v)if(!std::isfinite(x))return false;
    for(unsigned j=0;j<path.path.count;++j)for(float x:path.path.points[j])if(!std::isfinite(x))return false;
    if(path.prediction_tick==in.tick)return true;
    auto next=path;next.prediction_tick=in.tick;
    const float speed=std::sqrt((in.velocity[1]*in.velocity[1]+in.velocity[0]*in.velocity[0])+in.velocity[2]*in.velocity[2]);
    if(!std::isfinite(speed))return false;
    if(speed>1.F&&in.horizon>0) {
        Pair position{in.position[0],in.position[1]},velocity{in.velocity[0],in.velocity[1]};
        float elapsed=0;
        for(unsigned j=in.first;j<next.path.count;++j) {
            if(j!=next.cursor&&j==0)return false;
            const float dt=distance(j==next.cursor?in.position:next.path.points[j-1],next.path.points[j])/speed;
            elapsed+=dt;
            for(unsigned k=0;k<2;++k){velocity[k]+=in.acceleration[k]*dt;position[k]+=velocity[k]*dt;}
            const float ratio=elapsed/in.horizon;
            const float weight=ratio>1.F?1.F:ratio;
            for(unsigned k=0;k<2;++k)next.path.points[j][k]=weight*next.path.points[j][k]+(1.F-weight)*position[k];
            next.path.points[j][2]=0;next.cursor_tick=0;
            //45A8C0 is run after EACH point store, so later segment times use
            // the already modified preceding point. Do not batch the writes.
            next.length=0;
            for(unsigned k=1;k<next.path.count;++k) {
                const auto& a=next.path.points[k-1];const auto& b=next.path.points[k];
                const float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];
                next.length+=std::sqrt((z*z+y*y)+x*x);
            }
            if(!std::isfinite(elapsed)||!std::isfinite(next.length))return false;
            if(elapsed>in.horizon)break;
        }
    }
    path=next;return true;
}
bool ai_path_fitness(AiPlannedPath& path,const AiPathFitnessInput& in,float& output) noexcept {
    for(float v:{in.signed_speed,in.absolute_speed,in.minimum_time,in.initial_time,in.lateral_acceleration_limit})
        if(!std::isfinite(v))return false;
    if(in.absolute_speed<0||in.minimum_time<0||in.initial_time<0||in.lateral_acceleration_limit<0)return false;
    auto next=path;
    if(!ai_path_cursor(next,in.signed_speed,in.position,in.velocity,in.tick,
        in.retain_parameters[0],in.retain_parameters[1],in.retain_parameters[2]))return false;
    const unsigned n=next.path.count;
    if(!n){output=-1;return true;}
    if(next.cursor>=n)return false;
    const auto& points=next.path.points;
    std::array<float,20> speeds{},lengths{};
    const unsigned first=next.cursor;
    for(unsigned j=first;j<n;++j) {
        speeds[j]=in.absolute_speed;
        lengths[j]=distance(points[j],j==first?in.position:points[j-1]);
        if(!std::isfinite(lengths[j]))return false;
    }
    bool admitted=true;
    if(n>1) {
        unsigned i=first;
        float length=lengths[i];
        while(travel_time(length,in.absolute_speed,speeds[i])<=in.minimum_time) {
            //459CAD exits at n; subsequent acceleration stages are skipped.
            // Its speculative tail reads cannot affect the weighted score.
            if(++i>=n)break;
            length=distance(in.position,points[i]);
        }
        if(i+1<n) {
            const Pair after=std::fabs(speeds[i])>small?
                scaled(delta(points[i],in.position),speeds[i]):Pair{};
            admitted=acceleration(after,{in.velocity[0],in.velocity[1]},
                std::fabs(in.signed_speed)*in.initial_time,in.absolute_speed,speeds[i],in.lateral_acceleration_limit);
        }
        const int last=in.transition_end==255?static_cast<int>(n)-2:static_cast<int>(in.transition_end)-1;
        if(last>=static_cast<int>(n)-1)return false;
        for(unsigned j=i+1;admitted&&static_cast<int>(j)<=last;) {
            const unsigned previous=j-1;
            length=distance(points[previous],points[j]);
            while(travel_time(length,speeds[previous],speeds[j])<=in.minimum_time) {
                //459E20/459E93 finish this stage beyond the final segment.
                if(++j>=n)break;
                length=distance(points[previous],points[j]);
            }
            if(static_cast<int>(j)>last)break;
            if(std::fabs(speeds[previous])<=epsilon) {
                //45A000 returns success without writing this slot.
            } else if(std::fabs(length)<=epsilon) {
                speeds[j]=speeds[previous];
            } else {
                const auto before=scaled(delta(points[j],points[previous]),speeds[previous]);
                const auto after=scaled(delta(points[j+1],points[j]),speeds[j]);
                admitted=acceleration(after,before,length,speeds[previous],speeds[j],in.lateral_acceleration_limit);
            }
            ++j;
        }
    }
    float score=-1;
    if(admitted) {
        float weighted=((in.absolute_speed+speeds[first])*.5F)*lengths[first];
        float total=lengths[first];
        for(unsigned j=first+1;j<n;++j) {
            weighted+=((speeds[j-1]+speeds[j])*.5F)*lengths[j];total+=lengths[j];
        }
        if(!std::isfinite(weighted)||!std::isfinite(total))return false;
        if(total>0)score=weighted/total;
    }
    if(!std::isfinite(score))return false;
    path=next;output=score;return true;
}
} // namespace nfsu
