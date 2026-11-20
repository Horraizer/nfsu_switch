#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace nfsu {

// The caller allocates the exact uncompressed size before decoding.
inline bool expand_lz4_block(const std::vector<unsigned char>& coded,
                             std::vector<unsigned char>& picture) {
    std::size_t in=0,out=0;
    const auto extend=[&](std::size_t& count) {
        if(count!=15)return true;
        unsigned next=0;
        do {
            if(in==coded.size()||count>picture.size())return false;
            next=coded[in++];count+=next;
        } while(next==255);
        return true;
    };
    while(in<coded.size()) {
        const auto token=coded[in++];
        std::size_t literal=token>>4,match=token&15U;
        if(!extend(literal)||literal>coded.size()-in||literal>picture.size()-out)return false;
        std::memcpy(picture.data()+out,coded.data()+in,literal);
        in+=literal;out+=literal;
        if(in==coded.size())return out==picture.size();
        if(coded.size()-in<2)return false;
        const auto distance=std::size_t(coded[in])+256U*coded[in+1];in+=2;
        if(!distance||distance>out||!extend(match)||match+4>picture.size()-out)return false;
        match+=4;
        const auto prefix=std::min(distance,match);
        std::memcpy(picture.data()+out,picture.data()+out-distance,prefix);
        std::size_t available=prefix;
        while(available<match) {
            const auto take=std::min(available,match-available);
            std::memcpy(picture.data()+out+available,picture.data()+out,take);
            available+=take;
        }
        out+=match;
    }
    return false;
}

} // namespace nfsu
