#include "nfsu/custom_vinyl.h"
#include "nfsu/career_tuning.h"
#include <algorithm>
#include <stdexcept>
#include "nfsu/texture.h"
#include "nfsu/part_placement.h"

namespace nfsu { namespace {
std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|
        (std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
RaceChunkPayload chunk(const std::vector<unsigned char>& bytes,std::uint32_t id) {
    RaceChunkPayload span;std::vector<RaceTableIssue> issues;
    if(!find_race_chunk(bytes.data(),bytes.size(),id,span,issues))
        throw std::runtime_error("career tuning chunk unavailable: "+std::to_string(id));
    return span;
}
}
int CareerTuningCatalog::slot_category(int slot)noexcept {
    // Original5A03D0 compressed switch, not a menu-position assumption.
    switch(slot) {
    case 1:return 9;case 2:return 11;case 6:return 15;case 7:return 13;
    case 8:return 12;case 10:return 5;case 11:return 8;case 13:return 6;
    case 14:return 10;case 15:return 2;case 17:return 3;
    case 30:return 0;case 31:case 32:case 33:case 34:return 7;
    case 58:return 1;case 115:return 4;case 116:return 14;
    default:return slot>=37&&slot<=57?0:-1;
    }
}
int CareerTuningCatalog::performance_category(unsigned field)noexcept {
    // Original5A0350: VehicleUpgradeLevels field order. The last two are
    // visual-part aero dependencies, not independent performance packages.
    constexpr int categories[]={16,21,18,22,24,23,19,20,17,8,5};
    return field<11?categories[field]:-1;
}
bool CareerTuningCatalog::load(const std::vector<unsigned char>& front,
    const std::vector<unsigned char>& global,const RaceEventTable& events,std::string& error) {
    try {
        if(!events.ok())throw std::runtime_error("career tuning event table invalid");
        CareerTuningCatalog next;
        for(auto& category:next.events_)category.fill(-1);
        const auto prices=chunk(front,0x34a01);
        if(prices.begin==prices.end || (prices.end-prices.begin)%32)
            throw std::runtime_error("career tuning price records truncated");
        for(auto at=prices.begin;at<prices.end;at+=32) {
            const auto* p=front.data()+at;const auto category=u32(p);
            if(category>=next.prices_.size() || next.prices_[category].present)
                throw std::runtime_error("career tuning price category invalid");
            auto& row=next.prices_[category];row.present=true;
            for(unsigned n=0;n<3;++n)row.purchase[n]=u32(p+4+4*n);
            for(unsigned n=0;n<4;++n)row.visual[n]=u32(p+16+4*n);
        }
        for(unsigned category=0;category<25;++category)if(!next.prices_[category].present)
            throw std::runtime_error("career tuning price category missing");
        //5A1550 searches the original event pointer table by event number,
        // and uses the first matching kind0 category/level reward.
        for(unsigned id=0;id<256;++id) {
            const auto* event=find_race_event(events,id);if(!event)continue;
            if(event->offset>front.size() || front.size()-event->offset<kRaceEventRecordSize)
                throw std::runtime_error("career tuning reward outside event image");
            const auto* p=front.data()+event->offset;const auto count=u32(p+0x74);
            if(count>5)throw std::runtime_error("career tuning reward count invalid");
            for(unsigned n=0;n<count;++n) {
                const auto* reward=p+0x38+12*n;
                if(u32(reward)!=0)continue;
                const auto category=u32(reward+4),level=u32(reward+8);
                if(category>=32 || level>=5)throw std::runtime_error("career tuning reward category invalid");
                auto& first=next.events_[category][level];if(first<0)first=int(id);
            }
        }
        const auto style=chunk(global,0x34a08);
        //5A01D0 aligns the first record after the chunk header to16 bytes.
        const auto first=(style.begin+15)&~std::uint64_t(15);
        const auto count=(style.end-style.begin)/32;
        if(!count || first>style.end || count>(style.end-first)/32)
            throw std::runtime_error("career style records truncated");
        for(auto at=first;at<first+count*32;at+=32) {
            const auto* p=global.data()+at;CareerStyleUnlock reward;
            reward.points=u32(p);reward.kind=unsigned(p[4])|(unsigned(p[5])<<8);
            reward.group=p[6];reward.level=p[7];reward.key=u32(p+8);reward.message=u32(p+28);
            // Retail has direct vinyl families(kind0), sponsors(kind2),
            // and magazines(kind3). No guessed kind1 string matching.
            if(reward.kind!=0 && reward.kind!=2 && reward.kind!=3)
                throw std::runtime_error("unsupported original career style reward kind");
            next.style_.push_back(reward);
        }
        const auto decals=chunk(front,0x30240);
        const auto size=decals.end-decals.begin;
        if(size<4||size-4!=std::uint64_t(u32(front.data()+decals.begin))*28)
            throw std::runtime_error("career decal fitment records truncated");
        for(auto at=decals.begin+4;at<decals.end;at+=28) {
            const auto* p=front.data()+at;
            std::array<unsigned char,16> fitment{};
            std::copy_n(p+12,fitment.size(),fitment.begin());
            if(fitment[0]>6||fitment[1]>6||
               std::any_of(fitment.begin()+4,fitment.end(),[](auto i){return i>6;}))
                throw std::runtime_error("career decal fitment index invalid");
            next.decal_fitment_.emplace_back(u32(p+8),fitment);
        }
        next.parts_=parse_car_part_database(global.data(),global.size());
        if(!next.parts_.ok())throw std::runtime_error("career tuning part database invalid");
        const auto types=parse_car_type_table(global.data(),global.size());
        if(!types.ok())throw std::runtime_error("career wheel size table invalid");
        for(const auto& car:types.records) {
            if(car.offset+0xdc>=global.size())throw std::runtime_error("career wheel size record truncated");
            const int size=static_cast<signed char>(global[std::size_t(car.offset)+0xdc]);
            next.wheel_sizes_.emplace_back(bin_hash(car.name),size);
        }
        append_kr_vinyl_part(next.parts_);
        next.ready_=true;*this=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
std::uint32_t CareerTuningCatalog::price(unsigned category,unsigned level)const noexcept {
    //5A08B0/5A08F0: zero and unique level4 have no price-table read.
    return ready_&&category<prices_.size()&&prices_[category].present&&level>=1&&level<=3?
        prices_[category].purchase[level-1]:0;
}
std::uint32_t CareerTuningCatalog::part_price(const CarPartRecord& part)const noexcept {
    if(part.part_name_hash==kKrVinylPart)return 0;
    const auto category=slot_category(part.slot);
    return category<0?0:price(unsigned(category),part.upgrade_level);
}
std::uint32_t CareerTuningCatalog::ownership_key(const CarPartRecord& part)const noexcept {
    const int category=slot_category(part.slot);if(!ready_||category<0)return 0;
    //5A34D0/4BBD70: wheel sizes share the authored wheel-family key.
    if(category==3) {
        for(int n=0;n<part.attribute_count;++n) {
            const auto at=part.attribute_index+unsigned(n);if(at>=parts_.attributes.size())return 0;
            const auto& attribute=parts_.attributes[at];
            if(attribute.key==0x10c98090U)return attribute.value;
        }
        return 0;
    }
    if(category>=5 && category<=15 && category!=7 && category!=14)
        return unsigned(category)|(unsigned(part.byte_12)<<8)|(unsigned(part.upgrade_level)<<16);
    return part.part_name_hash;
}
const CarPartRecord* CareerTuningCatalog::find_part(const std::string& car,unsigned target_slot,
    std::uint32_t part_hash)const noexcept {
    if(!ready_ || target_slot>=120 || !part_hash)return nullptr;
    unsigned source_slot=target_slot;
    if(target_slot>=31&&target_slot<=34)source_slot=31;
    else if(target_slot>=37&&target_slot<=57)source_slot=30;
    else if(target_slot>=59&&target_slot<=114)source_slot=58;
    if(slot_category(int(source_slot))<0&&!(source_slot>=19&&source_slot<=25))return nullptr;
    const auto car_hash=bin_hash(car);
    for(const auto& part:parts_.records) {
        if(part.slot!=int(source_slot) || part.part_name_hash!=part_hash)continue;
        // Original4E2520 only accepts this car's BODY_1..5 records.
        if(source_slot==31&&part.byte_12==17) {
            bool matches=false;
            for(unsigned i=1;i<=5;++i)matches=matches||part.part_name_hash==bin_hash(car+"_BODY_"+std::to_string(i));
            if(!matches)continue;
        }
        if(source_slot==30) {
            //57C9C1 groups paint records by+0C; the color picker selects
            //the target family's palette, not every record in slot30.
            const auto family=part.hash_0c;
            if((target_slot==30||target_slot==38)&&family!=0x02DAAB07U&&family!=0x03437A52U&&family!=0x03797533U)continue;
            if(target_slot==37&&family!=0x0016BDCEU)continue;
            if(target_slot==39&&family!=0xD6640DFFU)continue;
            if(target_slot==40&&family!=0x066350F0U)continue;
            if(target_slot==41&&family!=0x0000DA27U)continue;
            if(target_slot>=42&&target_slot<=57&&family!=0x03E871F1U)continue;
        }
        if(source_slot>=30 || part.car_type_hash==car_hash ||
           (source_slot==17&&part.car_type_hash==bin_hash("WHEELS")) ||
           (source_slot==15&&part.car_type_hash==bin_hash("EXHAUST")) ||
           (source_slot==115&&part.car_type_hash==bin_hash("WINDOW_TINTS")) ||
           (source_slot==116&&part.car_type_hash==bin_hash("NEON")))return &part;
    }
    return nullptr;
}
const std::array<unsigned char,16>* CareerTuningCatalog::decal_fitment(std::uint32_t car)const noexcept {
    const auto found=std::find_if(decal_fitment_.begin(),decal_fitment_.end(),
        [&](const auto& entry){return entry.first==car;});
    return found==decal_fitment_.end()?nullptr:&found->second;
}
bool CareerTuningCatalog::wheel_fits(const std::string& car,const CarPartRecord& part,bool default_rim)const noexcept {
    if(!ready_||part.slot!=17||!part.upgrade_level)return false;
    const auto hash=bin_hash(car);
    const auto found=std::find_if(wheel_sizes_.begin(),wheel_sizes_.end(),
        [&](const auto& entry){return entry.first==hash;});
    if(found==wheel_sizes_.end())return false;
    const int outer=found->second;
    const int rim=part.upgrade_level==4?outer-6:outer+int(part.upgrade_level)-9;
    unsigned diameter=0,tire=0;
    for(int i=0;i<part.attribute_count;++i) {
        const auto at=part.attribute_index+unsigned(i);if(at>=parts_.attributes.size())return false;
        const auto& value=parts_.attributes[at];
        if(value.key==0xEB0101E2U)diameter=value.value;
        if(value.key==0xCE7D8DB5U)tire=value.value;
    }
    //50D4F3 reads CarTypeInfo+DC.50D5C1/5E0 derives rim diameter from
    //the level;50D779..50D89B requires both actual wheel attributes.
    //50E9F8/50EB58 in the size browser only filter the outer tyre.
    return (!default_rim||static_cast<signed char>(diameter&255)==rim)&&
           static_cast<signed char>(tire&255)==outer;
}
bool CareerTuningCatalog::part_available(const CarPartRecord& part,
    const std::array<std::uint8_t,256>& status,std::uint32_t earned_high_water)const {
    if(part.part_name_hash==kKrVinylPart)return ready_;
    const int category=slot_category(part.slot);
    if(category<0)return false;
    // Original4E3F90/4E4033 checks style kind2 by the actual part hash for
    // types15/16, including awarded level4 CUSTOM vinyls. No generic level
    // gate precedes that special branch; existing ownership is handled by caller.
    if(part.slot==31&&(part.byte_12==15||part.byte_12==16))
        return style_available(2,part.part_name_hash,earned_high_water);
    //4E3FD1..4E3FFF maps the current car's body number to the shared
    //ALLCARS_BODY_n style flag, independently of generic vinyl levels.
    if(part.slot==31&&part.byte_12==17) {
        for(unsigned i=1;i<=5;++i)if(part.name=="BODY "+std::to_string(i))
            return style_available(2,bin_hash("ALLCARS_BODY_"+std::to_string(i)),earned_high_water);
        return false;
    }
    if(!category_available(unsigned(category),part.upgrade_level,status))return false;
    // Unique level4 items are awarded by their own selectors. Their zero
    // price is not a normal-shop purchase; an owned item can be reinstalled.
    if(part.upgrade_level==4)return false;
    if(part.slot==58) {
        //51BEB0 baseline sponsors bypass earned-brand flags. Others use
        //the original kind2 brand unlock, not the display record hash.
        const auto name=part.hash_0c;
        for(const char* baseline:{"ALPINE","AUDIOBAHN","KENWOOD","ABS","JSP","EXTREMEDIMENSION","PIAA","EUROLITE","PLASMAGLOW","STREETGLOW","TYC"})
            if(name==bin_hash(baseline))return true;
        return style_available(2,name,earned_high_water);
    }
    if(part.slot==31) {
        //4E3D80: the normal vinyl family's type byte selects its original
        // prefix, then a level suffix1/2/3. Special whole-car vinyls follow
        // a separate policy; do not make them available by a level guess.
        constexpr const char* family[]={"TEAR","TEARHOODS","STRIPES","STRIPESHOOD","SPLASH",
            "MODERN","MODERNHOOD","FLAMES","FLAMESHOOD","LIGHTNING","LIGHTNINGHOODS",
            "FLAGS","TRIBAL","TRIBALHOOD","WILD"};
        if(part.byte_12<15 && part.upgrade_level>=1&&part.upgrade_level<=3)
            return style_available(0,bin_hash(std::string(family[part.byte_12])+std::to_string(part.upgrade_level)),earned_high_water);
        if(part.byte_12==15 || part.byte_12==16)return style_available(2,part.part_name_hash,earned_high_water);
        return false;
    }
    return true;
}
std::uint32_t CareerTuningCatalog::visual_points(unsigned category,unsigned level)const noexcept {
    return ready_&&category<prices_.size()&&prices_[category].present&&level>=1&&level<=4?
        prices_[category].visual[level-1]:0;
}
float CareerTuningCatalog::reputation(const InstalledPartSelection& installed)const noexcept {
    //5A05B0 and the complete category/slot/threshold-mask table6EE3D8.
    constexpr unsigned rules[][3]={{2,15,6},{3,17,7},{4,115,6},{5,10,7},{6,13,4},
        {8,11,7},{9,1,7},{11,2,7},{12,8,7},{13,7,6},{14,116,7},{10,14,4},
        {15,6,7},{0,30,7},{1,0,7},{7,0,7}};
    if(!ready_)return 0;
    const auto level=[&](unsigned slot) {
        const auto index=installed.records[slot];
        return index&&*index<parts_.records.size()&&parts_.records[*index].part_name_hash!=kKrVinylPart?unsigned(parts_.records[*index].upgrade_level):0U;
    };
    const auto present=[&](unsigned first,unsigned last) {
        for(auto i=first;i<=last;++i)if(installed.records[i]&&*installed.records[i]<parts_.records.size())return true;
        return false;
    };
    double score=0;unsigned bounds[3]{};
    for(const auto& rule:rules) {
        for(unsigned i=0;i<3;++i)if(rule[2]&(1U<<i))bounds[i]+=visual_points(rule[0],i+1);
        unsigned selected=0;
        if(rule[0]==7)for(unsigned slot=31;slot<=34;++slot)selected=std::max(selected,level(slot));
        else if(rule[0]==1)selected=present(59,64)?3:present(83,112)?2:present(67,80)?1:0;
        else selected=level(rule[1]);
        score+=visual_points(rule[0],selected);
    }
    if(score<bounds[0])return float(score/bounds[0]*2);
    if(score<bounds[1])return float((1+(score-bounds[0])/(bounds[1]-bounds[0]))*2);
    return float(std::min(5.,4+(score-bounds[1])/(bounds[2]-bounds[1])));
}
int CareerTuningCatalog::required_event(unsigned category,unsigned level)const noexcept {
    return ready_&&category<events_.size()&&level<5?events_[category][level]:-1;
}
bool CareerTuningCatalog::category_available(unsigned category,unsigned level,
    const std::array<std::uint8_t,256>& status)const noexcept {
    if(!ready_ || category>=prices_.size() || !prices_[category].present || level>=5)return false;
    const auto event=required_event(category,level);
    return event<0 || status[unsigned(event)]==kEventStatusWon;
}
bool CareerTuningCatalog::style_available(unsigned kind,std::uint32_t key,
    std::uint32_t earned_high_water)const noexcept {
    if(!ready_)return false;
    for(const auto& reward:style_)if(reward.kind==kind && reward.key==key)
        return reward.points<=earned_high_water;
    //4AA9D0 does not treat an absent vinyl/sponsor entry as unlocked.
    return kind>2;
}
}
