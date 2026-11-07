#include "nfsu/stable_draw_order.h"
#include <cstdint>
#include <iostream>
struct Draw {std::size_t ordinal,bucket_rank;unsigned char layer;};
int main() {
    std::vector<Draw> actual,scratch;
    std::vector<std::size_t> cursor(700);
    std::vector<unsigned char> layers(700);
    std::uint32_t rng=73;
    for(unsigned trial=0;trial<150;++trial) {
        const std::size_t buckets=1+trial%700;
        const std::size_t count=trial<3?trial:trial*37;
        actual.clear();
        for(std::size_t i=0;i<count;++i) {
            rng=rng*1664525U+1013904223U;const auto b=rng%buckets;
            actual.push_back({i,b,static_cast<unsigned char>((b*23)%256)});
        }
        auto expected=actual;
        std::sort(expected.begin(),expected.end(),[](const Draw& a,const Draw& b){
            if(a.layer!=b.layer)return a.layer<b.layer;
            if(a.bucket_rank!=b.bucket_rank)return a.bucket_rank<b.bucket_rank;
            return a.ordinal<b.ordinal;
        });
        nfsu::stable_draw_order(actual,scratch,cursor,layers,buckets);
        for(std::size_t i=0;i<count;++i)if(actual[i].ordinal!=expected[i].ordinal) {
            std::cerr<<"FAIL stable draw order\n";return 1;
        }
    }
    std::cout<<"PASS stable draw order: 150 empty/single/dense/multilayer comparisons\n";
}
