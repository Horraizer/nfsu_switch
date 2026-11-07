#include "nfsu/ai_road.h"
#include "nfsu/arg_path.h"
#include "nfsu/texture.h"
#include "nfsu/sha256.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <limits>
#include <cstring>
using namespace nfsu;
namespace {
struct Selection {AiRoadSelectionInput input;bool found;AiRoadCursor selected;};
#include "ai_selection_native.inc"
struct ContextCell {int route;unsigned point;float distance;bool junction;std::array<float,7> offset,lower,upper;};
struct ContextSnapshot {
    bool selected;AiRoadCursor road;int category,nearest,origin;unsigned count;ContextCell cells[40];
    std::uint32_t rng;std::array<std::uint32_t,8> cache;int marker;
    std::array<bool,7> flags;int limited,obstacle;bool event;
};
struct ContextPointEvent {int route;unsigned point;std::uint8_t a,b;};
struct ContextCase {ContextSnapshot before,after;AiRoadContextInput input;AiRoadPolicyInput policy;
    bool reselected,clear,category_available;unsigned passed_count;std::array<std::int16_t,40> passed;};
#include "ai_context_native.inc"
}
int main(int argc,char** argv) {
    std::string error;if(argc!=2||!check_arguments(argc,argv,1,error)){std::cerr<<"FAIL: retail root argument "<<error<<'\n';return 1;}
    std::ifstream file(argument_path(argv[1])/"Tracks"/"TRACKB1003.lzc",std::ios::binary);
    if(!file){std::cerr<<"FAIL: retail track read\n";return 1;}
    std::vector<unsigned char> stored(std::istreambuf_iterator<char>(file),{}),image;
    if(sha256_hex(sha256(stored.data(),stored.size()))!="ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA"){
        std::cerr<<"FAIL: retail input identity\n";return 1;}
    std::size_t used=0;RacingLineTrack track;
    if(!jdlz_decompress(stored.data(),stored.size(),64U*1024U*1024U,image,used,error)||
       !parse_racing_line_image(image.data(),image.size(),{},track)){std::cerr<<"FAIL: retail road parse "<<error<<'\n';return 1;}
    unsigned failed=0,index=0;
    for(const auto& c:selections){
        bool found=false;AiRoadCursor selected;
        if(!ai_road_select(track,c.input,found,selected)||found!=c.found||
           (found&&(selected.route!=c.selected.route||selected.point!=c.selected.point))){
            ++failed;std::cerr<<"FAIL: original spatial selection "<<index<<" got "<<found<<' '<<selected.route<<'/'<<selected.point
                <<" expected "<<c.found<<' '<<c.selected.route<<'/'<<c.selected.point<<'\n';
        }
        ++index;
    }
    std::cout<<"Original spatial selection "<<index<<" cases, failures "<<failed<<'\n';
    // The original loader permutes event IDs; retain its paired records and
    // rewritten point IDs together, without mutating the retail image.
    for(auto& r:track.routes)for(auto& p:r.points)p.event_a=p.event_b=255;
    for(const auto& p:context_point_events)for(auto& r:track.routes)if(r.index==p.route){
        r.points[p.point].event_a=p.a;r.points[p.point].event_b=p.b;
    }
    std::vector<AiRoadEvent> events(std::begin(context_events),std::end(context_events));
    std::array<bool,128> reverse{};for(const auto& r:track.routes)reverse[r.index]=r.reverse;
    unsigned context_index=0;
    for(const auto& c:context_cases){
        AiRoadContext context;const auto& b=c.before;const auto& expected=c.after;
        context.selected=b.selected;context.road=b.road;context.category=b.category;context.nearest=b.nearest;
        context.chain.count=b.count;context.chain.search_origin=b.origin;
        for(unsigned j=0;j<b.count;++j){
            const auto& saved=b.cells[j];auto& cell=context.chain.cells[j];
            cell.point=track.find(static_cast<std::int16_t>(saved.route))->points[saved.point];
            cell.point_index=static_cast<std::uint16_t>(saved.point);cell.distance=saved.distance;
            cell.offset=saved.offset;cell.lower=saved.lower;cell.upper=saved.upper;context.chain.junction[j]=saved.junction;
        }
        AiRoadPolicy policy;policy.cache=b.cache;policy.marker=b.marker;auto rng=b.rng;AiRoadContextResult output;
        const bool ok=ai_road_update_context(track,events,reverse,c.input,c.policy,policy,rng,context,output);
        bool same=ok&&context.selected==expected.selected&&(!context.selected||
            (context.road.route==expected.road.route&&context.road.point==expected.road.point))&&
            context.category==expected.category&&context.nearest==expected.nearest&&
            context.chain.count==expected.count&&context.chain.search_origin==expected.origin&&
            rng==expected.rng&&policy.cache==expected.cache&&policy.marker==expected.marker&&
            context.flags.enabled==expected.flags&&context.flags.first_limited==expected.limited&&
            context.flags.first_obstacle==expected.obstacle&&context.flags.event_flag==expected.event&&
            output.reselected==c.reselected&&output.clear_width==c.clear&&output.category_available==c.category_available&&output.passed_count==c.passed_count;
        for(unsigned j=0;j<std::min(output.passed_count,c.passed_count);++j)same=same&&output.passed_events[j]==c.passed[j];
        for(unsigned j=0;j<std::min(context.chain.count,expected.count);++j){
            const auto& a=context.chain.cells[j];const auto& e=expected.cells[j];
            same=same&&a.point.route_index==e.route&&a.point_index==e.point&&std::memcmp(&a.distance,&e.distance,4)==0&&
                std::memcmp(a.offset.data(),e.offset.data(),28)==0&&std::memcmp(a.lower.data(),e.lower.data(),28)==0&&
                std::memcmp(a.upper.data(),e.upper.data(),28)==0&&context.chain.junction[j]==e.junction;
        }
        if(!same){++failed;std::cerr<<"FAIL: original context "<<context_index<<" supported "<<ok<<" road "<<context.road.route<<'/'<<context.road.point
            <<" expected "<<expected.road.route<<'/'<<expected.road.point<<" count "<<context.chain.count<<'/'<<expected.count
            <<" nearest "<<context.nearest<<'/'<<expected.nearest<<" origin "<<context.chain.search_origin<<'/'<<expected.origin
            <<" clear "<<output.clear_width<<'/'<<c.clear<<" flags "<<(context.flags.enabled==expected.flags)<<'\n';}
        const auto saved=context;const auto saved_rng=rng;auto invalid=c.input;invalid.body_width=std::numeric_limits<float>::quiet_NaN();
        if(ai_road_update_context(track,events,reverse,invalid,c.policy,policy,rng,context,output)||
            rng!=saved_rng||context.chain.count!=saved.chain.count||context.road.point!=saved.road.point){++failed;std::cerr<<"FAIL: invalid context changed published state\n";}
        ++context_index;
    }
    std::cout<<"Original spatial context "<<context_index<<" cases, total failures "<<failed<<'\n';
    return failed?1:0;
}
