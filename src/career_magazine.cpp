#include "nfsu/career_magazine.h"
#include "nfsu/race_events.h"
#include "nfsu/texture.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nfsu { namespace {
std::uint32_t word(const unsigned char* p){return p[0]|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;}
unsigned choose(std::uint32_t& state,unsigned count){
    if(!count)return 0;
    const auto index=state%count;const auto x=state^0x1d872b41U;const auto y=(x>>5)^x;
    state=(y<<27)^y^x;return index;
}
}
std::uint32_t CareerMagazineCatalog::key(unsigned index) {
    if(index>=27)return 0;
    return bin_hash(std::string("MAGAZINE_")+(index<11?"RACE_":"TASK_")+std::to_string(index<11?index+1:index-10));
}
bool CareerMagazineCatalog::load(const std::vector<unsigned char>& globals,std::string& error){
    try {
        RaceChunkPayload payload;std::vector<RaceTableIssue> issues;
        if(!find_race_chunk(globals.data(),globals.size(),0x30230,payload,issues))throw std::runtime_error("magazine pool absent");
        if(payload.end-payload.begin<32)throw std::runtime_error("magazine counts truncated");
        constexpr unsigned sizes[]={12,16,168,16,20,56,360,80};
        CareerMagazineCatalog next;auto at=payload.begin+32;
        for(unsigned pool=0;pool<8;++pool){
            const auto count=word(globals.data()+payload.begin+4*pool);
            if(!count||count>256||at>payload.end||count>(payload.end-at)/sizes[pool])throw std::runtime_error("magazine records truncated");
            for(unsigned i=0;i<count;++i){
                std::vector<std::uint32_t> row(sizes[pool]/4);
                for(unsigned k=0;k<row.size();++k)row[k]=word(globals.data()+at+4*k);
                row[0]=row[1]=0;next.pools_[pool].push_back(std::move(row));at+=sizes[pool];
            }
        }
        if(at!=payload.end)throw std::runtime_error("magazine pool trailing bytes");
        for(const auto& row:next.pools_[7]){
            if(!row[3]||row[3]>16)throw std::runtime_error("magazine group count invalid");
            for(unsigned i=0;i<row[3];++i)if(row[4+i]>=next.pools_[6].size())throw std::runtime_error("magazine group index invalid");
        }
        constexpr unsigned counts[]={4,5,6,7,8},offsets[]={10,26,42,58,74};
        for(const auto& row:next.pools_[6])for(unsigned p=0;p<5;++p){
            if(!row[counts[p]]||row[counts[p]]>16)throw std::runtime_error("magazine choice count invalid");
            for(unsigned i=0;i<row[counts[p]];++i)if(row[offsets[p]+i]>=next.pools_[p+1].size() && row[offsets[p]+i]!=0xffffffffU)
                throw std::runtime_error("magazine choice index invalid");
        }
        *this=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool CareerMagazineCatalog::make(std::uint32_t key,std::uint32_t& random,CareerMagazineRecipe& out,std::string& error)const {
    try {
        const std::vector<std::uint32_t>* options=nullptr;
        for(const auto& row:pools_[7])if(row[2]==key){options=&row;break;}
        if(!options)throw std::runtime_error("magazine identity absent");
        auto state=random;const auto group=(*options)[4+((*options)[3]==1?0:choose(state,(*options)[3]))];
        const auto& row=pools_[6].at(group);CareerMagazineRecipe next;next.key=key;next.choices[5]=int(group);
        //51A8D0 order: front, caption, optional model, background, camera.
        constexpr unsigned counts[]={4,5,6,7,8},offsets[]={10,26,42,58,74};
        for(unsigned i=0;i<5;++i){
            const auto selected=row[offsets[i]+choose(state,row[counts[i]])];
            next.choices[i]=selected==0xffffffffU?(i==2?-1:0):int(selected);
        }
        next.front=pools_[1].at(unsigned(next.choices[0]))[3];
        const auto& caption=pools_[2].at(unsigned(next.choices[1]));
        std::copy(caption.begin()+2,caption.end(),next.caption.begin());
        next.chick=next.choices[2]<0?0:pools_[3].at(unsigned(next.choices[2]))[3];
        next.background=pools_[4].at(unsigned(next.choices[3]))[3];next.flags=pools_[4].at(unsigned(next.choices[3]))[4];
        const auto& camera=pools_[5].at(unsigned(next.choices[4]));
        for(unsigned i=0;i<11;++i){const auto bits=camera[3+i];std::memcpy(&next.camera[i],&bits,4);if(!std::isfinite(next.camera[i]))throw std::runtime_error("magazine camera invalid");}
        out=next;random=state;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
