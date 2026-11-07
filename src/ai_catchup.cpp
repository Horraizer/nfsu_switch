#include "nfsu/ai_catchup.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nfsu {
namespace {
float elapsed(std::uint32_t now,std::uint32_t before) noexcept {
    const auto delta=now-before;std::int32_t value;std::memcpy(&value,&delta,4);
    return float(value)*float(.00025F);
}
// Original583F20 under the initialized007F binary32 precision control.
float curve(const std::array<AiCatchupPoint,11>& p,float x) noexcept {
    if(x<=p.front().x)return p.front().y;
    if(x>=p.back().x)return p.back().y;
    for(unsigned j=0;j+1<p.size();++j)if(x>=p[j].x&&x<p[j+1].x) {
        const float dx=float(p[j+1].x)-p[j].x,dy=float(p[j+1].y)-p[j].y;
        return dy*(std::fabs(dx)>9.999999974752427e-7F?(float(x)-p[j].x)/dx:.5F)+p[j].y;
    }
    return p.front().y;
}
}
bool ai_catchup_update(const AiCatchupInput& in,const AiCatchupTuning& t,AiCatchupState& state) noexcept {
    if(!in.has_race||!in.enabled||in.finished||in.special_context)return true;
    if(!std::isfinite(t.interval)||!std::isfinite(t.grace))return false;
    if(elapsed(in.tick,state.updated_tick)<=t.interval)return true;
    auto next=state;next.updated_tick=in.tick;next.target_id=0;
    float scale=1;
    const bool grace=float(in.elapsed_ticks)*float(.00025F)<t.grace;
    if(!in.suppressed&&in.human_count>0&&in.action_allows&&!grace) {
        if(in.human_count!=1||!in.target_id)return false;
        for(float v:{in.self_progress,in.target_progress,in.route_length,in.self_speed,in.target_speed,
                     in.target_weight,in.target_history,t.maximum_gap,t.difficulty_scale})if(!std::isfinite(v))return false;
        next.target_id=in.target_id;
        const auto difficulty=std::max(in.difficulty,0);
        float distance_scale=0,rank_scale=0;
        if(in.comparison_enabled) {
            float delta=float(in.self_progress)-in.target_progress;
            if(in.wrap_progress){const float shifted=static_cast<float>(delta+.5F);delta=float(shifted)-std::floor(shifted)-.5F;}
            const float gap=static_cast<float>(delta*in.route_length);
            if(gap<=t.maximum_gap) {
                for(const auto* points:{&t.lower,&t.upper})for(unsigned j=0;j<points->size();++j)
                    if(!std::isfinite((*points)[j].x)||!std::isfinite((*points)[j].y)||
                       (j&&(*points)[j].x<(*points)[j-1].x))return false;
                const float low=static_cast<float>(curve(t.lower,gap));
                const float slope=static_cast<float>((curve(t.upper,gap)-low)*float(.1111111119389534F));
                const float base=float(slope)*difficulty+low;
                const float speed=(float(in.self_speed)-in.target_speed)*float(-.004999999888241291F);
                const float divisor=(float(.009999999776482582F)*gap)*gap+1;
                const float correction=std::max(-float(.05000000074505806F),
                    std::min(float(.05000000074505806F),speed/divisor));
                distance_scale=static_cast<float>(base+correction);
                const int difference=in.target_rank-in.self_rank;
                const int slot=difference+(difference<0?8:7);
                if(slot<0||slot>=16||!std::isfinite(t.rank_lower[slot])||!std::isfinite(t.rank_upper[slot]))return false;
                const float rank_slope=(float(t.rank_upper[slot])-t.rank_lower[slot])*float(.1111111119389534F);
                rank_scale=static_cast<float>(float(difficulty)*rank_slope+t.rank_lower[slot]);
            }
        }
        scale=static_cast<float>((float(rank_scale)*in.target_weight+1)+distance_scale);
        const auto& history=in.target_history>=1?t.history_above:t.history_below;
        if(!std::isfinite(history[0])||!std::isfinite(history[1]))return false;
        const float phase=static_cast<float>(difficulty)*(.1111111119389534F);
        const float gain=phase>=1?history[1]:phase<0?history[0]:
            (1-float(phase))*history[0]+float(phase)*history[1];
        const float factor=static_cast<float>((float(in.target_history)-1)*gain+1);
        scale=std::max(factor,t.difficulty_scale)*scale;
    }
    if(!std::isfinite(scale))return false;
    next.speed_scale=scale;next.physical_scale=std::max(scale,1.F);state=next;return true;
}
bool ai_catchup_tuning(const unsigned char* data,std::size_t size,int context,int laps,
    bool point_to_point,unsigned traffic_selector,unsigned rank,AiCatchupTuning& out) noexcept {
    if(!data||size!=0x82c||context< -1||context>=10||rank>=4||traffic_selector>3)return false;
    const auto scalar=[&](std::size_t at){float f;std::memcpy(&f,data+at,4);return f;};
    const unsigned length=!point_to_point&&laps>=4?1U:0U;
    const unsigned row=(unsigned(traffic_selector>=2)+2*length)*0x58;
    AiCatchupTuning next;
    for(unsigned i=0;i<11;++i) {
        next.lower[i]={scalar(row+8*i),scalar(row+8*i+4)};
        next.upper[i]={scalar(0x160+row+8*i),scalar(0x164+row+8*i)};
    }
    for(unsigned i=0;i<16;++i){next.rank_lower[i]=scalar(0x2c0+4*i);next.rank_upper[i]=scalar(0x300+4*i);}
    for(unsigned i=0;i<2;++i){next.history_below[i]=scalar(0x358+8*rank+4*i);next.history_above[i]=scalar(0x338+8*rank+4*i);}
    const auto base=0x380+unsigned(std::max(context,0))*0x54;
    next.interval=scalar(0x824);next.maximum_gap=scalar(0x828);
    next.grace=scalar(base+0x4c);next.difficulty_scale=scalar(base+0x18);
    out=next;return true;
}
bool ai_catchup_history_step(float dt,float speed,float road_speed,bool enabled,
                             bool has_road,AiCatchupHistory& state) noexcept {
    if(!std::isfinite(dt)||dt<0||dt>1||!std::isfinite(speed)||speed<0||!std::isfinite(road_speed)||road_speed<0||
       state.count>10||state.cursor>=10)return false;
    auto next=state;next.timer-=static_cast<int>(dt*4000.F+.5F);
    if(next.timer<0) {
        if(has_road) {
            const float sample=!enabled?1.F:road_speed>0?speed/road_speed:speed>.009999999776482582F?1.F:0.F;
            if(next.count<10)++next.count;
            next.sum+=sample;next.sum-=next.samples[next.cursor];
            next.samples[next.cursor]=sample;
            if(++next.cursor==10)next.cursor=0;
            next.average=next.sum/float(next.count);
            if(!std::isfinite(next.average))return false;
        }
        next.timer=4000;
    }
    state=next;return true;
}
} // namespace nfsu
