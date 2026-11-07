// PC simulation audit. Rendering/GPU and original selector3 AI are NOT timed.
// Usage: ug0_actor_audit <retail-root> [ticks]
#include "nfsu/intro_grid.h"
#include "nfsu/sha256.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace nfsu;
namespace {
struct Result {
    std::array<std::string,4> mechanical{},record{};
    RaceSessionWork work{};
    std::uint64_t impulses=0;
    double total=0,p95=0,maximum=0;
};
Result run(IntroGrid& grid,unsigned count,unsigned ticks,bool reference) {
#ifdef NFSU_AUDIT_BEGIN
    NFSU_AUDIT_BEGIN();
#endif
    std::string error;const auto& player=grid.actors[1];
    RaceSession session(grid.course,player.placement_spec,RaceSteering::recovered_predicate1,player.hull);
    std::vector<RaceRivalSetup> setup;
    for(const auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent&&setup.size()<count) {
        auto spec=actor.placement_spec;spec.nitrous={};
        setup.push_back({spec,actor.pose,actor.hull,actor.config.event_label});
    }
    if(count&&!session.set_rivals(setup,error,reference?RacePresentation::player:RacePresentation::none))
        throw std::runtime_error(error);
    if(!session.start(error,&player.pose))throw std::runtime_error(error);
    RaceRouteDriver driver(RaceDrivingLine::event1_forward);
    std::array<Sha256,4> mechanical,records;std::vector<double> times;times.reserve(ticks);
    for(unsigned i=0;i<ticks;++i) {
        const auto begin=std::chrono::steady_clock::now();
        if(!session.advance(driver.sample(session),error))throw std::runtime_error(error);
        // RaceRun consumes the player record each frame. The prior prototype
        // also unconditionally generated every rival's unused record.
        session.record();
        if(reference)for(unsigned a=0;a<count;++a)session.rival(a).record();
        const auto end=std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
        // Comparison cost is outside the timed interval in BOTH modes.
        for(unsigned a=0;a<=count;++a) {
            const auto& car=a?session.rival(a-1):session;
            for(const auto& t:car.physics_telemetry()) {
                std::array<std::uint8_t,kVehicleTrackTelemetryRecordBytes> bytes{};
                encode_vehicle_track_telemetry(t,bytes);
                mechanical[a].update(bytes.data(),bytes.size());
            }
            if(!a||reference)records[a].update(car.record().data(),car.record().size());
        }
    }
    Result result;result.impulses=session.pair_impulses();
    for(unsigned a=0;a<=count;++a) {
        const auto& car=a?session.rival(a-1):session;const auto& w=car.work();
        result.mechanical[a]=sha256_hex(mechanical[a].finish());
        result.record[a]=(!a||reference)?sha256_hex(records[a].finish()):"not-requested";
        result.work.frames+=w.frames;result.work.pad_updates+=w.pad_updates;
        result.work.direct_updates+=w.direct_updates;result.work.physics_steps+=w.physics_steps;
        result.work.pair_candidates+=w.pair_candidates;result.work.camera_updates+=w.camera_updates;
        result.work.record_encodes+=w.record_encodes;
    }
    for(double t:times)result.total+=t;
    std::sort(times.begin(),times.end());result.p95=times[(times.size()-1)*95/100];result.maximum=times.back();
    const auto& w=result.work;
    std::cout<<(reference?"REFERENCE":"HEADLESS")<<" rivals="<<count<<" ticks="<<ticks
        <<" sim_ms="<<result.total<<" p95_ms="<<result.p95<<" max_ms="<<result.maximum
        <<" frames="<<w.frames<<" physics="<<w.physics_steps<<" cameras="<<w.camera_updates
        <<" encodes="<<w.record_encodes<<" pairs="<<w.pair_candidates<<" impulses="<<result.impulses<<'\n';
    for(unsigned a=0;a<=count;++a)std::cout<<"ACTOR "<<a<<" mechanical="<<result.mechanical[a]
        <<" record="<<result.record[a]<<'\n';
#ifdef NFSU_AUDIT_END
    NFSU_AUDIT_END();
#endif
    return result;
}
}
int main(int argc,char** argv) {try {
    if(argc<2||argc>4)throw std::runtime_error("expected retail root, optional ticks and rival count");
    unsigned ticks=argc>=3?unsigned(std::stoul(argv[2])):3600;
    unsigned only=argc==4?unsigned(std::stoul(argv[3])):4;
    if(argc==4&&only!=0&&only!=1&&only!=3)throw std::runtime_error("rival count must be 0, 1 or 3");
    if(ticks<240||ticks>120000)throw std::runtime_error("audit ticks outside 240..120000");
    IntroGrid grid;std::string error;
    if(!load_intro_grid(argv[1],grid,error))throw std::runtime_error(error);
    std::cout<<"CONTACT_MEMORY resident="<<grid.course.world.memory.resident_bytes
        <<" triangle_bytes="<<grid.course.world.memory.triangle_bytes
        <<" triangles="<<grid.course.world.triangles.size()<<'\n';
    grid.course.start=grid.actors[1].pose;
    for(unsigned rivals:{0U,1U,3U}) {
        if(only!=4&&rivals!=only)continue;
        const auto old=run(grid,rivals,ticks,true),now=run(grid,rivals,ticks,false);
        if(old.mechanical!=now.mechanical||old.record[0]!=now.record[0]||old.impulses!=now.impulses)
            throw std::runtime_error("presentation change altered simulation or player record");
        if(now.work.camera_updates!=ticks||now.work.record_encodes!=ticks||
           old.work.physics_steps!=now.work.physics_steps||old.work.pair_candidates!=now.work.pair_candidates)
            throw std::runtime_error("unexpected work count");
    }
    std::cout<<"PASS identical mechanical records/player bytes/pair impulses; rivals=";
    if(only==4)std::cout<<"0,1,3\n";else std::cout<<only<<'\n';
    return 0;
}catch(const std::exception& ex){std::cerr<<"FAIL "<<ex.what()<<'\n';return 1;}}
