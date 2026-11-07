#pragma once
#include "nfsu/vehicle_track.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

namespace nfsu {
// Shared parser for the original named *_CV collision solid.
inline std::uint32_t hull_u32(const unsigned char* p) {
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
inline bool decode_car_collision_hull(const unsigned char* bytes,std::size_t length,std::uint32_t hash,VehicleHull& hull,
    std::size_t begin=0) {
    if((!bytes&&length)||begin>length)return false;
    VehicleHull decoded;
    std::function<bool(std::size_t,std::size_t,unsigned)> walk;
    walk=[&](std::size_t begin,std::size_t end,unsigned depth) {
        if(depth>16)return false;
        for(auto p=begin;p+8<=end;) {
            const auto id=hull_u32(bytes+p),n=hull_u32(bytes+p+4);const auto start=p+8;
            if(n>end-start)return false;
            if(id==0x80034020) {
                bool selected=false;
                std::vector<std::pair<unsigned,unsigned>> ranges;
                std::vector<unsigned> indices;
                for(auto q=start;q+8<=start+n;) {
                    const auto child=hull_u32(bytes+q),size=hull_u32(bytes+q+4);const auto data=q+8;
                    if(size>start+n-data)return false;
                    if(child==0x39200) {
                        // 472B81 aligns every header independently. 472B8B
                        // appends shapes in file order; 469C40 takes the first
                        // matching hash. CIVIC has two same-name shapes here.
                        const auto aligned=(data+15)&~std::size_t(15);
                        if(aligned>data+size || data+size-aligned<0x58)return false;
                        selected=hull_u32(bytes+aligned+24)==hash;
                        decoded={};ranges.clear();indices.clear();
                    }
                    if(child==0x39201 && selected) {
                        const auto aligned=(data+15)&~std::size_t(15);
                        if(aligned>data+size)return false;
                        const auto payload=data+size-aligned;
                        if(!payload || payload%48 || payload/48>256)return false;
                        for(unsigned k=0;k<payload/48;++k) {
                            std::array<float,3> point{};std::memcpy(point.data(),bytes+aligned+48*k+16,12);
                            for(float v:point)if(!std::isfinite(v))return false;
                            decoded.points.push_back(point);
                            const auto* record=bytes+aligned+48*k;
                            const unsigned count=unsigned(record[8])|(unsigned(record[9])<<8);
                            const unsigned first=unsigned(record[10])|(unsigned(record[11])<<8);
                            if(count>256)return false;
                            ranges.emplace_back(first,count);
                        }
                    }
                    if(child==0x39202 && selected) {
                        const auto aligned=(data+15)&~std::size_t(15);
                        if(aligned>data+size || (data+size-aligned)%2)return false;
                        for(auto k=aligned;k<data+size;k+=2)
                            indices.push_back(unsigned(bytes[k])|(unsigned(bytes[k+1])<<8));
                        if(decoded.points.empty())return false;
                        decoded.neighbors.resize(ranges.size());
                        for(unsigned k=0;k<ranges.size();++k) {
                            const auto [first,count]=ranges[k];
                            if(first>indices.size() || count>indices.size()-first)return false;
                            for(unsigned j=0;j<count;++j) {
                                const auto index=indices[first+j];
                                if(index>=decoded.points.size())return false;
                                decoded.neighbors[k].push_back(index);
                            }
                        }
                        return true;
                    }
                    q=data+size;
                }
            } else if((id&0x80000000U) && walk(start,start+n,depth+1))return true;
            p=start+n;
        }
        return false;
    };
    if(!walk(begin,length,0))return false;
    hull=std::move(decoded);return true;
}
inline bool decode_car_collision_hull(const std::vector<unsigned char>& bytes,std::uint32_t hash,VehicleHull& hull) {
    return decode_car_collision_hull(bytes.data(),bytes.size(),hash,hull);
}
} // namespace nfsu
