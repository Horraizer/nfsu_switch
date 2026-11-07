#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <vector>
namespace nfsu {
// Each first-use bucket has one layer. Preserve encounter order inside it.
// Scratch/cursor storage is owned and reserved by the scene, never global.
template<class Draw>
void stable_draw_order(std::vector<Draw>& draws,std::vector<Draw>& scratch,
    std::vector<std::size_t>& cursor,std::vector<unsigned char>& layers,
    std::size_t buckets) {
    if(draws.size()<2)return;
    std::fill(cursor.begin(),cursor.begin()+buckets,0);
    for(const auto& d:draws){++cursor[d.bucket_rank];layers[d.bucket_rank]=d.layer;}
    std::array<std::size_t,256> offsets{};
    for(std::size_t b=0;b<buckets;++b)offsets[layers[b]]+=cursor[b];
    std::size_t total=0;
    for(auto& offset:offsets){const auto count=offset;offset=total;total+=count;}
    for(std::size_t b=0;b<buckets;++b) {
        const auto count=cursor[b];cursor[b]=offsets[layers[b]];offsets[layers[b]]+=count;
    }
    scratch.resize(draws.size());
    for(const auto& d:draws)scratch[cursor[d.bucket_rank]++]=d;
    draws.swap(scratch);
}
} // namespace nfsu
