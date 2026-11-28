#include "nfsu/race_particles.h"
#include "nfsu/sha256.h"
#include "race_water_birth_native_cases.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace nfsu {
struct RaceParticleBirthProbe {
    static RaceParticle water(RaceParticles& system,const NativeWaterBirthCase& row,bool transparent=false){
        auto& emitter=system.water_emitters_[row.effect];const auto old_basis=emitter.basis;
        if(!row.placement)emitter.basis={{{1,0,0},{0,1,0},{0,0,1}}};
        auto& preset=system.presets_[unsigned(RaceParticleEffect::water_first)+row.effect];
        const auto rgba=preset.rgba;if(transparent)for(auto& color:preset.rgba)color={0,0,0,0};
        VehicleQuat identity;identity.w=1;identity.x=identity.y=identity.z=0;
        auto result=system.spawn_bottom_particle(3+row.effect,{},identity,{},{20,30,0},row.seed);
        emitter.basis=old_basis;preset.rgba=rgba;return result;
    }
    static RaceParticle bottom(RaceParticles& system,unsigned part,unsigned seed){
        VehicleQuat rotated;rotated.w=0;rotated.x=rotated.y=0;rotated.z=1;
        return system.spawn_bottom_particle(part,{},rotated,{},{20,30,0},seed);
    }
    static void dump_bottom(RaceParticles& system){
        for(unsigned part=0;part<3;++part)for(unsigned seed:{121U,4567U,12345678U}){
            const auto p=bottom(system,part,seed);
            std::cout<<"BOTTOM "<<part<<' '<<seed<<' '<<p.life_ticks<<' '<<p.initial_cell<<' '
                <<p.origin.x<<' '<<p.origin.y<<' '<<p.origin.z<<' '<<p.velocity.x<<' '<<p.velocity.y<<' '<<p.velocity.z<<'\n';
        }
    }
    static unsigned dispatch(RaceParticles& system){
        std::array<bool,kRaceWaterPresetCount> seen{};unsigned visits=0;
        for(unsigned actor=0;actor<4;++actor)for(unsigned surface:{13U,14U}){
            system.water_count_=0;system.step_=7;system.last_solver_dt_=kVehicleStepSeconds;
            VehicleTrackTelemetry t;t.step=7;t.orientation.w=1;t.velocity={20,30,0};t.model.owner_speeds.magnitude=20;
            for(auto& wheel:t.model.wheels)wheel.contact=true;
            VehicleSpec spec;spec.handling.centre_of_mass={0,0,0};
            for(auto& wheel:spec.handling.wheels)wheel.longitudinal=wheel.lateral=wheel.vertical=0;
            RaceParticles::SpraySource source;
            system.emit_driving_spray(t,spec,0,surface,nullptr,source,actor);
            if(!system.water_count_)throw std::runtime_error("water dispatch produced no births");
            std::array<unsigned,kRaceWaterPresetCount> children{};
            for(std::size_t n=0;n<system.water_count_;++n){const auto actual=system.water_[n];
                const unsigned index=unsigned(actual.effect)-unsigned(RaceParticleEffect::water_first);
                if(index>=kRaceWaterPresetCount)throw std::runtime_error("driving water dispatched a non-water effect");
                const unsigned child=children[index]++;
                std::uint32_t random=7U^(unsigned(actual.effect)+1)*0x9E3779B9U^(actor+4*child+1)*0x85EBCA6BU;
                random^=random>>16;random*=0x7FEB352DU;random^=random>>15;random*=0x846CA68BU;random^=random>>16;
                const NativeWaterBirthCase control{"dispatch",index,random,true,0,{},{}};
                const auto expected=water(system,control);
                if(actual.origin.x!=expected.origin.x || actual.origin.y!=expected.origin.y || actual.origin.z!=expected.origin.z ||
                   actual.velocity.x!=expected.velocity.x || actual.velocity.y!=expected.velocity.y || actual.velocity.z!=expected.velocity.z ||
                   actual.life_ticks!=expected.life_ticks || actual.initial_cell!=expected.initial_cell || actual.water_owner!=actor ||
                   actual.born_step!=7 || actual.solver_dt!=kVehicleStepSeconds || actual.elapsed_seconds!=0 || !actual.water_node)
                    throw std::runtime_error("ordinary driving-water path bypassed verified native birth or owner metadata");
            }
            for(unsigned i=0;i<children.size();++i)if(children[i]){seen[i]=true;++visits;}
        }
        if(!std::all_of(seen.begin(),seen.end(),[](bool value){return value;}))throw std::runtime_error("not every driving-water preset reached production dispatch");
        return visits;
    }
    static std::size_t resident(const RaceParticles& system){return system.water_.capacity()*sizeof(RaceParticle)+system.vehicle_.capacity()*sizeof(RaceParticle)+system.environment_bytes();}
};
}
namespace {
double error(nfsu::VehicleVec3 a,nfsu::VehicleVec3 b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}
}
int main(int argc,char** argv)try{
    if(argc<2)throw std::runtime_error("original GLOBALB.BUN path required");
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
    if(nfsu::sha256_hex(nfsu::sha256(bytes.data(),bytes.size()))!="39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9")
        throw std::runtime_error("original GLOBALB.BUN identity mismatch");
    auto system=std::make_unique<nfsu::RaceParticles>();std::string message;
    if(!system->load_presets(bytes,message))throw std::runtime_error(message);
    std::cout<<std::setprecision(17);
    if(argc==3 && std::string(argv[2])=="--dump-bottom-controls"){nfsu::RaceParticleBirthProbe::dump_bottom(*system);return 0;}
    const auto resident=nfsu::RaceParticleBirthProbe::resident(*system);
    unsigned failed=0,transparent_failures=0;double max_point=0,max_velocity=0;
    for(const auto& row:kNativeWaterBirthCases){
        const auto p=nfsu::RaceParticleBirthProbe::water(*system,row);
        const auto a=error(p.origin,row.point),b=error(p.velocity,row.velocity);
        max_point=std::max(max_point,a);max_velocity=std::max(max_velocity,b);
        if(p.life_ticks!=row.life || a>0.000002 || b>0.000002){++failed;if(failed<=8)std::cout<<"FAIL native birth "<<row.name<<" seed="<<row.seed<<" placement="<<row.placement<<" life="<<p.life_ticks<<" expected_life="<<row.life<<" point_error="<<a<<" velocity_error="<<b<<'\n';}
        const auto hidden=nfsu::RaceParticleBirthProbe::water(*system,row,true);
        if(p.life_ticks!=hidden.life_ticks || p.initial_cell!=hidden.initial_cell || error(p.origin,hidden.origin)!=0 || error(p.velocity,hidden.velocity)!=0)++transparent_failures;
    }
    unsigned burst_failures=0;for(const auto& row:kNativeWaterBurstCases)
        if(nfsu::race_particle_burst_size(row.strength,row.interval)!=row.count)++burst_failures;
    unsigned bottom_failures=0;for(const auto& row:kBottomBirthControls){const auto p=nfsu::RaceParticleBirthProbe::bottom(*system,row.part,row.seed);
        if(p.life_ticks!=row.life || p.initial_cell!=row.cell || error(p.origin,row.point)!=0 || error(p.velocity,row.velocity)!=0)++bottom_failures;}
    const auto dispatch=nfsu::RaceParticleBirthProbe::dispatch(*system);
    if(nfsu::RaceParticleBirthProbe::resident(*system)!=resident)throw std::runtime_error("birth or transparent control changed resource residency");
    std::cout<<(failed?"FAIL":"PASS")<<" original402D30 births="<<kNativeWaterBirthCases.size()<<" failures="<<failed<<" max_point_error_m="<<max_point<<" max_velocity_error_mps="<<max_velocity<<'\n';
    std::cout<<(transparent_failures?"FAIL":"PASS")<<" transparent equal-seed controls="<<kNativeWaterBirthCases.size()<<" failures="<<transparent_failures<<'\n';
    std::cout<<(burst_failures?"FAIL":"PASS")<<" native burst-count controls="<<kNativeWaterBurstCases.size()<<" failures="<<burst_failures<<" resident_bytes="<<resident<<'\n';
    std::cout<<(bottom_failures?"FAIL":"PASS")<<" unchanged C1 bottom-out controls="<<kBottomBirthControls.size()<<" failures="<<bottom_failures<<'\n';
    std::cout<<"PASS production driving-water dispatch emitter/owner cases="<<dispatch<<" player_and_rivals=4\n";
    return failed||transparent_failures||burst_failures||bottom_failures?1:0;
}catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<'\n';return 1;}
