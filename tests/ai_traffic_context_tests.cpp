#include "nfsu/ai_road.h"
#include "nfsu/arg_path.h"
#include "nfsu/sha256.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace nfsu;
namespace {
#include "traffic_context_native.inc"
unsigned failures=0;
void check(bool value,const char* message){if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
float value(unsigned word){float f;std::memcpy(&f,&word,4);return f;}
unsigned bits(float f){unsigned word;std::memcpy(&word,&f,4);return word;}
AiRoadContext restore(const NativeTrafficContextSnapshot& b,const RacingLineTrack& track) {
    AiRoadContext s;s.selected=b.selected;s.road=b.road;s.category=b.category;s.nearest=b.nearest;
    s.chain.count=b.count;s.chain.search_origin=b.origin;
    std::copy(b.flags,b.flags+7,s.flags.enabled.begin());
    s.flags.first_limited=b.limited;s.flags.first_obstacle=b.obstacle;s.flags.event_flag=b.event;
    for(unsigned j=0;j<b.count;++j) {
        const auto& saved=b.cells[j];auto& cell=s.chain.cells[j];
        cell.point=track.find(short(saved.route))->points[saved.point];cell.point_index=std::uint16_t(saved.point);
        cell.distance=value(saved.distance);s.chain.junction[j]=saved.junction;
        for(unsigned k=0;k<7;++k){cell.offset[k]=value(saved.offset[k]);cell.lower[k]=value(saved.lower[k]);cell.upper[k]=value(saved.upper[k]);}
    }
    return s;
}
}
int main(int argc,char** argv) {
    std::string error;if(argc!=2||!check_arguments(argc,argv,1,error)){std::cerr<<"FAIL: retail root argument "<<error<<'\n';return 1;}
    const auto path=argument_path(argv[1])/"Tracks/TRACKB1003.lzc";
    std::ifstream file(path,std::ios::binary);std::vector<unsigned char> stored(std::istreambuf_iterator<char>(file),{});
    if(!file||sha256_hex(sha256(stored.data(),stored.size()))!="ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA") {
        std::cerr<<"FAIL: authenticated retail track input\n";return 1;
    }
    std::vector<unsigned char> image;const auto route=load_race_route_track(path,image);RacingLineTrack track;
    if(!route.ok()||!parse_racing_line_image(image.data(),image.size(),{},track)){std::cerr<<"FAIL: retail road parse\n";return 1;}
    // The original loader permutes connections. Keep its paired IDs together;
    // geometry/bounds remain external retail input, not embedded track assets.
    for(auto& r:track.routes)for(auto& p:r.points)p.event_a=p.event_b=255;
    for(const auto& p:kTrafficContextPointEvents) {
        auto r=std::find_if(track.routes.begin(),track.routes.end(),[&](const Route& v){return v.index==p.route;});
        if(r==track.routes.end()||p.point>=r->points.size()){std::cerr<<"FAIL: native point reference\n";return 1;}
        r->points[p.point].event_a=std::uint8_t(p.a);r->points[p.point].event_b=std::uint8_t(p.b);
    }
    std::array<bool,128> reverse{};for(const auto& r:track.routes)reverse[unsigned(r.index)]=r.reverse;
    std::vector<AiRoadEvent> events(std::begin(kTrafficContextEvents),std::end(kTrafficContextEvents));
    unsigned index=0,resets=0,passages=0;
    for(const auto& c:kNativeTrafficContext) {
        const auto& b=c.before;const auto& e=c.after;auto s=restore(b,track);AiRoadPolicy policy;
        std::copy(b.cache,b.cache+8,policy.cache.begin());policy.marker=b.marker;auto rng=b.rng;
        AiRoadPolicyInput input;input.direction=c.input.selection.direction;input.role=3;input.policy=AiRoadDecisionPolicy::traffic;
        AiRoadContextResult result;const auto before=failures;
        const bool ok=ai_road_update_context(track,events,reverse,c.input,input,policy,rng,s,result);
        check(ok,"actual isolated traffic context executes");
        check(s.selected==e.selected&&(!e.selected||(s.road.route==e.road.route&&s.road.point==e.road.point))&&
            s.category==e.category&&s.nearest==e.nearest&&s.chain.count==e.count&&s.chain.search_origin==e.origin,
            "original selected road/lane, nearest cell, compact origin and length");
        check(rng==e.rng&&policy.marker==e.marker&&std::equal(policy.cache.begin(),policy.cache.end(),e.cache),
            "original traffic decisions, no-op passage notification and shared RNG consumption");
        check(std::equal(s.flags.enabled.begin(),s.flags.enabled.end(),e.flags)&&s.flags.first_limited==e.limited&&
            s.flags.first_obstacle==e.obstacle&&s.flags.event_flag==e.event,"original ride category and six-lane flags");
        check(result.reselected==c.reselected&&result.clear_width==c.clear&&result.category_available==c.available&&
            result.reset_maneuver==c.reset&&result.passed_count==c.passed_count&&
            std::equal(result.passed_events.begin(),result.passed_events.begin()+result.passed_count,c.passed),
            "original road/width/lane-reset/passage publications");
        for(unsigned j=0;j<std::min(s.chain.count,e.count);++j) {
            const auto& a=s.chain.cells[j];const auto& v=e.cells[j];
            check(a.point.route_index==v.route&&a.point_index==v.point&&bits(a.distance)==v.distance&&
                s.chain.junction[j]==v.junction,"original compact point/distance/junction identity");
            for(unsigned k=0;k<7;++k)check(bits(a.offset[k])==v.offset[k]&&bits(a.lower[k])==v.lower[k]&&bits(a.upper[k])==v.upper[k],
                "original all-slot offsets and bounds, including ride slot0 independent of lane");
        }
        resets+=result.reset_maneuver;passages+=result.passed_count;
        const auto saved=s;const auto old_policy=policy;const auto old_rng=rng;auto invalid=c.input;invalid.ride_category=7;
        check(!ai_road_update_context(track,events,reverse,invalid,input,policy,rng,s,result)&&s.selected==saved.selected&&
            s.category==saved.category&&s.chain.count==saved.chain.count&&policy.cache==old_policy.cache&&
            policy.marker==old_policy.marker&&rng==old_rng,"invalid ride category refuses before state/RNG publication");
        if(failures!=before)std::cerr<<"traffic context case "<<index<<'\n';
        ++index;
    }
    check(resets>0&&passages>0,"native corpus reaches lane-reset and junction notification branches");
    {
        //44CF30/5A75C0 have no traffic-role guard on this branch. A drag
        //rival (controller3) consumes the same future lane mask as traffic4.
        const auto& c=kNativeTrafficContext[1];auto state=restore(c.before,track);
        AiRoadPolicy policy;std::copy(c.before.cache,c.before.cache+8,policy.cache.begin());
        policy.marker=c.before.marker;auto rng=c.before.rng;auto query=c.input;query.controller_kind=3;
        AiRoadPolicyInput input;input.direction=query.selection.direction;input.role=2;
        AiRoadContextResult result;
        check(ai_road_update_context(track,events,reverse,query,input,policy,rng,state,result)&&
            result.reset_maneuver&&state.category==c.after.category,
            "drag rival leaves the closed lane in the original adjacent candidate order");
        state=restore(c.before,track);policy={};std::copy(c.before.cache,c.before.cache+8,policy.cache.begin());
        policy.marker=c.before.marker;rng=c.before.rng;query.controller_kind=2;
        check(ai_road_update_context(track,events,reverse,query,input,policy,rng,state,result)&&
            result.reset_maneuver&&state.category==c.after.category,
            "human owner2 still obeys an unavailable current lane header");
    }
    std::cout<<"Actual traffic spatial context, failures "<<failures<<'\n';return failures?1:0;
}
