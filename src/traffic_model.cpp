#include "nfsu/traffic_model.h"
#include "nfsu/quick_rival.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace nfsu {
std::uint32_t authored_race_roster_seed(std::uint32_t global) noexcept {
    // 431F13 saves and advances the settings state; 4B3A20 draws the names.
    quick_rival_random(global,1);
    std::array<bool,48> used{};
    for(unsigned name=0;name<4;++name) {
        for(unsigned attempt=0;attempt<100;++attempt) {
            const auto index=quick_rival_random(global,unsigned(used.size()));
            if(used[index])continue;
            used[index]=true;
            break;
        }
    }
    return global;
}
RaceStartupRandom race_startup_random(std::uint32_t global) noexcept {
    const auto sound=global;
    quick_rival_random(global,1);
    const auto before_world=global;
    quick_rival_random(global,1);
    return {sound,before_world,global};
}
bool decode_traffic_model_record(const unsigned char* bytes,std::size_t size,TrafficModelRecord& out) noexcept {
    if(!bytes||size<0xc90)return false;
    const auto u32=[](const unsigned char* p) {return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);};
    TrafficModelRecord next;next.id=int(u32(bytes+0xc50));next.category=int(u32(bytes+0xc54));
    next.enabled=bytes[0x40]!=0;next.variants=int(static_cast<std::int8_t>(bytes[0xd9]));
    if(next.id<0||next.id>=35||next.variants>16)return false;
    for(unsigned i=0;i<5;++i) {
        const auto bits=u32(bytes+0xc60+4*i);std::memcpy(&next.weights[i],&bits,4);
        if(!std::isfinite(next.weights[i]))return false;
    }
    for(unsigned i=0;i<16;++i)next.skins[i]=int(static_cast<std::int8_t>(bytes[0xc80+i]));
    out=next;return true;
}
TrafficModelChoice choose_traffic_model(const TrafficModelRecord* records,
    std::size_t count,unsigned track,const int* used,std::size_t used_count,
    std::uint32_t& random) noexcept {
    TrafficModelChoice result;
    const unsigned region=track>=1000?0:track/10==6?4:track/10-1;
    if(region>=5||!records||count>35||(used_count&&!used))return result;
    const auto eligible=[](const TrafficModelRecord& record) {
        return record.category==2&&record.enabled&&record.variants>0&&record.variants<=16;
    };
    float total=0;
    for(std::size_t i=0;i<count;++i)if(eligible(records[i]))total=total+records[i].weights[region];
    if(!std::isfinite(total)||std::fabs(total)<0.001F)return result;
    const TrafficModelRecord* selected=nullptr;
    for(;;) {
        float sample=(float(quick_rival_random(random,0x7fffffffU))*0x1p-31F)*total;
        selected=nullptr;
        for(std::size_t i=0;i<count;++i)if(eligible(records[i])) {
            sample=sample-records[i].weights[region];
            // Native accepts an exact zero, including a leading zero-weight
            // record when the RNG sample itself is zero.
            if(sample<=0) {selected=records+i;break;}
        }
        if(!selected)return result;
        if(!used_count||std::find(used,used+used_count,selected->id)==used+used_count)break;
        if(++result.retries>=100)break;
    }
    result.id=selected->id;
    result.skin=selected->skins[quick_rival_random(random,unsigned(selected->variants))];
    return result;
}
}
