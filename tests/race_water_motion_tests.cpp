#include "nfsu/race_particles.h"
#include "race_water_motion_native_cases.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace nfsu {
struct RaceParticleBirthProbe {
    static RaceParticle motion(RaceParticleEffect effect,const NativeWaterMotionCase& row,bool environment=false,unsigned owner=0) {
        auto system=std::make_unique<RaceParticles>();
        RaceParticlePreset preset;preset.gravity=row.gravity;preset.drag=row.drag;
        system->presets_[unsigned(effect)]=preset;
        if(environment){system->environment_presets_.resize(1);system->environment_presets_[0].particle=preset;}
        system->water_.resize(1);system->water_count_=1;
        auto& p=system->water_[0];p.effect=effect;p.origin=row.origin;p.velocity=row.velocity;
        p.life_ticks=32000;p.elapsed_seconds=0;p.environment_preset=environment?1:0;p.water_owner=static_cast<std::uint8_t>(owner);p.size=1.25;
        for(unsigned frame=0;frame<row.frames;++frame){++system->step_;system->advance_water(row.dt);}
        if(system->water_count_!=1 || p.effect!=effect || p.life_ticks!=32000 || p.water_owner!=owner || p.size!=1.25)
            throw std::runtime_error("motion update changed lifetime, owner, count or authored size");
        return p;
    }
};
}
namespace {
double error(nfsu::VehicleVec3 a,nfsu::VehicleVec3 b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}
nfsu::VehicleVec3 center(const nfsu::RaceParticle& p){return {p.origin.x+p.displacement.x,p.origin.y+p.displacement.y,p.origin.z+p.displacement.z};}
}
int main(int argc,char** argv)try {
    std::cout<<std::setprecision(17);
    if(argc==2 && std::string(argv[1])=="--dump-controls") {
        const auto& row=kNativeWaterMotionCases[9];
        const nfsu::RaceParticleEffect effects[]{nfsu::RaceParticleEffect::road,nfsu::RaceParticleEffect::purge_first,
            nfsu::RaceParticleEffect::road_spray,nfsu::RaceParticleEffect::bottom_flash,nfsu::RaceParticleEffect::bottom_smoke,nfsu::RaceParticleEffect::bottom_sparks};
        for(const auto effect:effects){const auto p=nfsu::RaceParticleBirthProbe::motion(effect,row);const auto c=center(p);
            std::cout<<"CONTROL "<<unsigned(effect)<<" 0 "<<c.x<<' '<<c.y<<' '<<c.z<<' '<<p.velocity.x<<' '<<p.velocity.y<<' '<<p.velocity.z<<'\n';}
        const auto p=nfsu::RaceParticleBirthProbe::motion(nfsu::RaceParticleEffect::water_first,row,true);const auto c=center(p);
        std::cout<<"CONTROL "<<unsigned(nfsu::RaceParticleEffect::water_first)<<" 1 "<<c.x<<' '<<c.y<<' '<<c.z<<' '<<p.velocity.x<<' '<<p.velocity.y<<' '<<p.velocity.z<<'\n';
        return 0;
    }
    unsigned failed=0,index=0;double max_position=0,max_velocity=0;
    for(const auto& row:kNativeWaterMotionCases){
        const auto effect=static_cast<nfsu::RaceParticleEffect>(unsigned(nfsu::RaceParticleEffect::water_first)+row.effect);
        const auto p=nfsu::RaceParticleBirthProbe::motion(effect,row,false,index%4);
        const auto a=error(center(p),row.point),b=error(p.velocity,row.final_velocity);
        max_position=std::max(max_position,a);max_velocity=std::max(max_velocity,b);
        if(a>0.000008 || b>0.000008){++failed;if(failed<=8)std::cout<<"FAIL native motion "<<row.name<<" dt="<<row.dt<<" frames="<<row.frames<<" center_error="<<a<<" velocity_error="<<b<<'\n';}
        ++index;
    }
    std::cout<<(failed?"FAIL":"PASS")<<" original403A60 water motion cases="<<index<<" failures="<<failed<<" max_center_error_m="<<max_position<<" max_velocity_error="<<max_velocity<<'\n';
    unsigned control_failures=0;
    for(const auto& row:kUnchangedMotionControls){
        const auto p=nfsu::RaceParticleBirthProbe::motion(row.effect,kNativeWaterMotionCases[9],row.environment);
        const auto at=center(p);
        if(at.x!=row.point.x || at.y!=row.point.y || at.z!=row.point.z ||
           p.velocity.x!=row.velocity.x || p.velocity.y!=row.velocity.y || p.velocity.z!=row.velocity.z)++control_failures;
    }
    std::cout<<(control_failures?"FAIL":"PASS")<<" unchanged C1 road, purge, road spray, bottom-out and environment controls="<<kUnchangedMotionControls.size()<<" failures="<<control_failures<<'\n';
    failed+=control_failures;
    return failed?1:0;
}catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<'\n';return 1;}
