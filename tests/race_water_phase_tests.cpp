#include "nfsu/race_particles.h"
#include "nfsu/sha256.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
bool same(nfsu::VehicleVec3 a,nfsu::VehicleVec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
}
namespace nfsu {
struct RaceParticleBirthProbe {
    static void phase(RaceParticles& system,const VehicleSpec& spec,double dt,unsigned surface){
        system.reset();
        VehicleTrackTelemetry t;t.step=1;t.orientation.w=1;t.velocity={20,30,0};
        t.model.solver_dt=dt;t.model.owner_speeds.magnitude=20;
        for(auto& wheel:t.model.wheels)wheel.contact=true;
        system.advance(t,spec,0,surface);
        check(system.water_count_>0,"first public water frame emitted no particles");
        std::vector<RaceParticle> born(system.water_.begin(),system.water_.begin()+system.water_count_);
        for(const auto& p:born)check(p.born_step==1&&p.elapsed_seconds==0&&same(p.displacement,{}),
            "newborn water aged or moved before its first draw");
        system.advance(t,spec,0,surface);
        check(system.water_count_==born.size(),"repeated telemetry emitted a second burst");
        for(std::size_t i=0;i<born.size();++i)check(system.water_[i].elapsed_seconds==0&&same(system.water_[i].displacement,{}),
            "repeated frame moved a newborn");
        auto expected=std::make_unique<RaceParticles>(system);
        expected->step_=2;expected->advance_water(dt);
        ++t.step;system.advance(t,spec,150,surface);
        check(system.water_count_==expected->water_count_,"second-frame lifetime differs from one native update");
        bool moved=false;
        for(std::size_t i=0;i<system.water_count_;++i){const auto& a=system.water_[i];const auto& b=expected->water_[i];
            check(a.elapsed_seconds==b.elapsed_seconds&&same(a.displacement,b.displacement)&&same(a.velocity,b.velocity),
                "old water did not receive exactly one packed update before the next births");
            moved=moved||!same(a.displacement,{});
        }
        check(moved,"motion control did not exercise a moving child");
        system.reset();
        system.water_count_=system.water_.size();
        for(auto& p:system.water_){p=born.front();p.life_ticks=1;p.elapsed_seconds=1;}
        t.step=1;system.advance(t,spec,0,surface);
        check(system.water_count_>0,"expired global pool blocked the same-frame car birth");
        for(std::size_t i=0;i<system.water_count_;++i)check(system.water_[i].elapsed_seconds==0&&system.water_[i].life_ticks!=1,
            "expired children survived or replacement births were prematurely aged");
        // Unrelated authored effects retain their existing post-emission update.
        system.reset();RaceParticle p=born.front();p.effect=RaceParticleEffect::road_spray;
        p.water_owner=0;p.life_ticks=10000;p.born_step=1;p.elapsed_seconds=0;
        check(system.emit_vehicle(p),"unrelated control could not be inserted");
        auto ordinary=std::make_unique<RaceParticles>(system);
        ordinary->step_=1;ordinary->finish_vehicle_frame(dt);
        system.advance(t,spec,150,surface);
        check(system.vehicle_count_==ordinary->vehicle_count_&&system.vehicle_count_==1,
            "water phase changed ordinary effect occupancy");
        const auto& a=system.vehicle_[0];const auto& b=ordinary->vehicle_[0];
        check(a.elapsed_seconds==b.elapsed_seconds&&same(a.velocity,b.velocity)&&same(a.displacement,b.displacement),
            "water phase changed ordinary effect timing or motion");
    }
};
}
int main(int argc,char** argv)try{
    check(argc==2,"original GLOBALB.BUN path required");
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
    check(nfsu::sha256_hex(nfsu::sha256(bytes.data(),bytes.size()))=="39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9",
        "original GLOBALB.BUN identity mismatch");
    auto system=std::make_unique<nfsu::RaceParticles>();std::string error;
    check(system->load_presets(bytes,error),error.c_str());
    nfsu::VehicleSpec spec;spec.handling.centre_of_mass={0,0,0};
    for(auto& wheel:spec.handling.wheels)wheel.longitudinal=wheel.lateral=wheel.vertical=0;
    for(double dt:{1.0/60.0,1.0/30.0})for(unsigned surface:{13U,14U})
        nfsu::RaceParticleBirthProbe::phase(*system,spec,dt,surface);
    std::cout<<"PASS native water phase: both water materials at60/30Hz; newborn draw, next update, duplicate frame, full-pool expiry; unrelated road effect unchanged\n";
    return 0;
}catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<'\n';return 1;}
