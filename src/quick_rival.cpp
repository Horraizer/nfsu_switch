#include "nfsu/quick_rival.h"
#include "nfsu/texture.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace nfsu {
std::uint32_t quick_rival_random(std::uint32_t& state,unsigned bound) noexcept {
    const auto result=bound?state%bound:0U,x=state^0x1d872b41U,y=(x>>5)^x;
    state=(y<<27)^y^x;return result;
}
int quick_rival_context(const unsigned char* ai,unsigned difficulty,std::uint32_t& rng) noexcept {
    if(!ai||difficulty>2)return 0;
    const float value=float(quick_rival_random(rng,0x7fffffffU))*4.656612873077393e-10F;
    for(unsigned i=0;i<10;++i) {
        float threshold;std::memcpy(&threshold,ai+0x788+difficulty*40+i*4,4);
        if(value<=threshold)return int(i);
    }
    return 0;
}
namespace {
using Weights=std::array<std::array<unsigned,4>,4>;
constexpr Weights regular{{{{60,40,0,0}},{{20,70,10,0}},{{0,20,70,10}},{{0,0,20,80}}}};
constexpr Weights lamps{{{{50,50,0,0}},{{20,70,10,0}},{{0,20,70,10}},{{0,0,20,80}}}};
struct Rule {int slot;Weights weights;};
// Original 6F0D60, in native iteration order. Percent comparisons include equality.
constexpr Rule rules[]={
    {1,regular},{2,regular},{10,regular},{8,regular},{7,regular},{11,regular},
    {6,{{{{100,0,0,0}},{{100,0,0,0}},{{100,0,0,0}},{{60,0,0,40}}}}},
    {15,regular},{17,regular},{115,lamps},{14,lamps},{13,lamps},
    {116,{{{{90,10,0,0}},{{70,20,10,0}},{{40,30,20,10}},{{10,20,30,40}}}}},
    {31,{{{{80,20,0,0}},{{60,30,10,0}},{{50,30,10,10}},{{40,20,20,20}}}}},
    {32,{{{{70,30,0,0}},{{20,70,10,0}},{{20,30,40,10}},{{10,20,20,50}}}}},
    {33,{{{{70,30,0,0}},{{20,70,10,0}},{{20,30,40,10}},{{10,20,20,50}}}}},
    {34,{{{{90,10,0,0}},{{70,20,10,0}},{{50,30,20,10}},{{40,10,20,30}}}}},
    {67,{{{{70,30,0,0}},{{40,50,10,0}},{{20,40,30,10}},{{10,30,30,30}}}}},
    {75,{{{{70,30,0,0}},{{40,50,10,0}},{{20,40,30,10}},{{10,30,30,30}}}}},
    {30,{{{{70,30,0,0}},{{50,50,0,0}},{{40,40,20,0}},{{30,30,40,0}}}}},
    {41,{{{{70,30,0,0}},{{50,50,0,0}},{{40,40,20,0}},{{30,30,40,0}}}}}
};
int vinyl_group(unsigned kind) {
    switch(kind){case 1:case 3:case 6:case 8:case 10:case 13:return 1;
    case 15:return 4;case 16:case 18:return 3;case 17:return 2;default:return 0;}
}
}
bool quick_rival_visual(const CarPartDatabase& db,const std::string& car,
    const unsigned char* record,std::size_t size,const VehicleUpgradeLevels& levels,
    std::uint32_t& rng,InstalledPartSelection& out,std::string& error) {
    if(!record||size!=kVehicleRecordSize||!db.ok()) {error="quick rival appearance inputs invalid";return false;}
    const std::array<int,11> upgrades{{levels.weight,levels.suspension,levels.engine_a,levels.induction,
        levels.nitrous,levels.engine_b,levels.gearbox,levels.tyres,levels.grip,levels.aero_a,levels.aero_b}};
    int sum=0;for(auto v:upgrades){if(v<0||v>3){error="quick rival appearance level invalid";return false;}sum+=v;}
    const int average=int(float(sum)*(1.F/11.F)+.5F),tier=std::clamp(average-1,0,2);
    StockParts stock;if(!select_stock_parts(db,car,stock,error))return false;
    InstalledPartSelection next;
    for(unsigned i=0;i<kCarSlotCount;++i)next.records[i]=stock.slots[i].record;
    const auto family=bin_hash(std::string(reinterpret_cast<const char*>(record+0x20),
        std::find(record+0x20,record+0x40,0)-(record+0x20)));
    const auto find=[&](std::uint32_t group,int slot,int level=kAnyUpgradeLevel,std::uint32_t name=0) {
        return find_car_part(db,group,slot,name,kNoCarPart,level);
    };
    const auto install=[&](int slot,std::size_t part){if(part!=kNoCarPart)next.records[unsigned(slot)]=part;};
    const auto random_part=[&](std::uint32_t group,int slot,int level) {
        std::vector<std::size_t> candidates;
        for(auto at=find(group,slot,level);at!=kNoCarPart;at=find_car_part(db,group,slot,0,at,level))candidates.push_back(at);
        return candidates.empty()?kNoCarPart:candidates[quick_rival_random(rng,unsigned(candidates.size()))];
    };
    const auto upgraded=[&](unsigned slot) {
        const auto at=next.records[slot].value_or(kNoCarPart);
        return at!=kNoCarPart&&db.records[at].upgrade_level!=0;
    };
    install(12,find(family,12,levels.engine_a));install(16,find(family,16,levels.grip));
    bool wheels=false;int paint_style=0,wheel_paint_style=0;std::array<bool,4> vinyl{};
    for(const auto& rule:rules) {
        int level=average;
        switch(rule.slot){case 1:case 2:case 7:case 8:case 10:level=levels.aero_b;break;
            case 11:level=levels.aero_a;break;case 15:level=levels.engine_a;break;case 17:level=levels.tyres;break;}
        const auto sample=quick_rival_random(rng,100);unsigned weight=rule.weights[unsigned(level)][0];int style=0;
        while(sample>weight&&style<3)weight+=rule.weights[unsigned(level)][unsigned(++style)];
        if(!style)continue;
        std::uint32_t group=family;int selected_slot=rule.slot;
        switch(rule.slot) {
            case 6:if(style<3)continue;style=4;break;
            case 13:case 14:style=3;break;
            case 15:group=0xA5B28001U;break;
            case 17:wheels=true;continue;
            case 30:paint_style=style;continue;
            case 31:case 32:case 33:case 34:vinyl[unsigned(rule.slot-31)]=true;continue;
            case 41:wheel_paint_style=style;continue;
            case 67:case 75:group=0x9287U;selected_slot=58;style=kAnyUpgradeLevel;break;
            case 115:group=0x7C1A7208U;break;
            case 116:group=0x0019DD0FU;break;
        }
        install(rule.slot,random_part(group,selected_slot,style));
    }
    // PLATES lookup is not filtered by level. BRAKES uses the grip level.
    next.records[18]=find(0x7326CF08U,kAnySlot);
    install(16,find(0x52EB02F7U,16,levels.grip));
    if(wheels) {
        const auto style=random_part(0xA6E1B733U,118,kAnyUpgradeLevel);
        if(style!=kNoCarPart) {
            const int minimum=int(std::int8_t(record[0xdd])),maximum=int(std::int8_t(record[0xde]));
            if(maximum<=minimum){error="quick rival wheel diameter interval invalid";return false;}
            const int diameter=minimum+int(quick_rival_random(rng,unsigned(maximum-minimum)));
            std::array<std::size_t,9> candidates{};
            for(unsigned i=0;i<candidates.size();++i) {
                char suffix[64];std::snprintf(suffix,sizeof(suffix),"_STYLE%02d_%2d_%2d",int(i+1),diameter,int(std::int8_t(record[0xdc])));
                auto hash=db.records[style].part_name_hash;for(const char* p=suffix;*p;++p)hash=hash*33U+unsigned(*p);
                candidates[i]=find(0x83347C07U,17,kAnyUpgradeLevel,hash);
            }
            unsigned at=0;for(unsigned count=quick_rival_random(rng,8)+1;count;--count) {
                unsigned attempts=9;do{at=(at+1)%9;--attempts;}while(attempts&&candidates[at]==kNoCarPart);
            }
            install(17,candidates[at]);
        }
    }
    //57C8B0 caches a first record and count by class and clamped level-1.
    //57D860 walks subsequent records of the family/slot, preserving database order.
    const auto pool=[&](int slot,int bucket,std::uint32_t key,int group) {
        std::pair<std::size_t,unsigned> result{kNoCarPart,0};
        for(const auto& part:db.records)if(part.slot==slot&&std::clamp(int(part.upgrade_level)-1,0,2)==bucket&&
            (slot==30?part.hash_0c==key:vinyl_group(part.byte_12)==group)) {
            if(result.first==kNoCarPart)result.first=part.index;
            ++result.second;
        }
        return result;
    };
    const auto color=[&](std::uint32_t key,int bucket) {
        const auto p=pool(30,bucket,key,0);if(!p.second)return kNoCarPart;
        auto at=p.first;for(auto n=quick_rival_random(rng,p.second);n&&at!=kNoCarPart;--n)
            at=find_car_part(db,0x9287U,30,0,at,kAnyUpgradeLevel);
        return at;
    };
    for(unsigned layer=0;layer<4;++layer)if(vinyl[layer]) {
        auto at=kNoCarPart;
        if(layer==0) {
            const auto suffix="_BODY_"+std::to_string(quick_rival_random(rng,5)+1);
            auto hash=family;for(const auto c:suffix)hash=hash*33U+unsigned(c);
            at=find(0x15269AEFU,31,kAnyUpgradeLevel,hash);
        }else if(layer!=2||!upgraded(8)) {
            // Hood vinyl requires a non-null stock hood, not merely !upgraded.
            if(layer==2&&next.records[8].value_or(kNoCarPart)==kNoCarPart)continue;
            const int group=layer==1?0:layer==2?1:3;
            const auto p=pool(31,layer==3?0:tier,0,group);
            if(p.second) {
                at=p.first;const int level=db.records[at].upgrade_level;
                for(auto n=quick_rival_random(rng,p.second);n&&at!=kNoCarPart;) {
                    at=find_car_part(db,0x15269AEFU,31,0,at,level);
                    if(at!=kNoCarPart&&vinyl_group(db.records[at].byte_12)==group)--n;
                }
            }
        }
        if(at!=kNoCarPart) {
            const auto& part=db.records[at];bool accepted=true;
            for(int a=0;a<part.attribute_count;++a) {
                const auto& attribute=db.attributes[part.attribute_index+unsigned(a)];
                if(attribute.key==0x564B8CB6U){accepted=attribute.value<=16;break;}
            }
            if(accepted)next.records[31+layer]=at;
        }
    }
    next.records[30]=paint_style==1?color(0x03437A52U,std::clamp(tier,1,2)):
        paint_style==2?color(0x03797533U,2):color(0x02DAAB07U,tier);
    for(unsigned i=0;i<16;++i)next.records[42+i]=color(0x03E871F1U,tier);
    if(upgraded(11))next.records[38]=color(0x02DAAB07U,tier);
    if(upgraded(15))next.records[40]=color(0x066350F0U,tier);
    if(upgraded(16))next.records[39]=color(0xD6640DFFU,tier);
    if(wheel_paint_style&&upgraded(17))next.records[41]=color(0x0000DA27U,std::clamp(wheel_paint_style-1,0,2));
    //57EA80 updates widebody decal bindings as well as enable flags.
    StockParts resolved;if(!select_installed_parts(db,car,next,resolved,error))return false;
    for(unsigned i=0;i<kCarSlotCount;++i)next.records[i]=resolved.slots[i].record;
    out=std::move(next);error.clear();return true;
}
}
