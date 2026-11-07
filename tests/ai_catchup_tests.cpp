#include "nfsu/ai_catchup.h"
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
using namespace nfsu;
namespace {
struct CatchupCase {AiCatchupInput input;AiCatchupTuning tuning;AiCatchupState before;std::uint32_t expected[4];int laps;bool point_to_point;unsigned traffic_selector;};
#include "ai_catchup_native.inc"
#include "ai_catchup_history_native.inc"
std::uint32_t bits(float f){std::uint32_t x;std::memcpy(&x,&f,4);return x;}
unsigned failures=0;
void check(bool ok,const char* what){if(!ok){++failures;std::cerr<<"FAIL: "<<what<<'\n';}}
}
int main(int argc,char** argv) {
    AiCatchupHistory history;
    for(const auto& c:catchup_history_native) {
        check(ai_catchup_history_step(1.F,float(c[0]),float(c[1]),bool(c[2]),true,history),"native history sample admitted");
        check(bits(history.sum)==c[3]&&bits(history.average)==c[4]&&history.count==c[5]&&history.cursor==c[6]&&
            static_cast<std::uint32_t>(history.timer)==c[7],"original history mean/ring/cadence match");
    }
    if(argc==2) {
        std::ifstream file(std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",std::ios::binary);
        std::vector<unsigned char> bytes(0x82c);bool found=false;
        const auto u32=[](const unsigned char* p){return unsigned(p[0])|unsigned(p[1])<<8|unsigned(p[2])<<16|unsigned(p[3])<<24;};
        unsigned char header[8];
        while(file.read(reinterpret_cast<char*>(header),8)) {
            if(u32(header)==0x34b00&&u32(header+4)==bytes.size()) {
                found=bool(file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));break;
            }
            file.seekg(u32(header+4),std::ios::cur);
        }
        check(found,"retail catch-up configuration read");
        if(found)for(const auto& c:catchups) {
            AiCatchupTuning tuning;
            check(ai_catchup_tuning(bytes.data(),bytes.size(),c.input.difficulty,c.laps,c.point_to_point,c.traffic_selector,unsigned(c.input.target_rank),tuning),"native table selection admitted");
            static_assert(sizeof(AiCatchupTuning)==84*sizeof(float),"native table scalar layout");
            check(std::memcmp(&tuning,&c.tuning,sizeof(tuning))==0,"original initialized tuning matches decoded retail data");
        }
    }
    unsigned index=0;
    for(const auto& c:catchups) {
        auto state=c.before;
        check(ai_catchup_update(c.input,c.tuning,state),"native catchup input admitted");
        const std::uint32_t actual[]={state.updated_tick,state.target_id,bits(state.speed_scale),bits(state.physical_scale)};
        for(unsigned j=0;j<4;++j)if(actual[j]!=c.expected[j]) {
            ++failures;std::cerr<<"FAIL: catchup case "<<index<<" word "<<j<<" actual "<<std::hex<<actual[j]
                <<" expected "<<c.expected[j]<<std::dec<<'\n';
        }
        ++index;
    }
    auto input=catchups[0].input;const auto& tuning=catchups[0].tuning;auto state=catchups[0].before;
    input.human_count=2;
    check(!ai_catchup_update(input,tuning,state)&&std::memcmp(&state,&catchups[0].before,sizeof(state))==0,
        "unimplemented multiple-human reference selection refuses transactionally");
    input.enabled=false;
    check(ai_catchup_update(input,tuning,state)&&std::memcmp(&state,&catchups[0].before,sizeof(state))==0,
        "disabled update preserves all publication");
    std::cout<<"Original catchup: "<<index<<" cases; history steps="<<sizeof(catchup_history_native)/sizeof(*catchup_history_native)<<"; failures="<<failures<<'\n';return failures?1:0;
}
