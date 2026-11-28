#pragma once
#include "nfsu/race_particles.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <stdexcept>
namespace nfsu::test {
using ParticleIdentity=std::tuple<unsigned,unsigned,unsigned,std::uint64_t,unsigned,unsigned,unsigned,unsigned,double,double,double>;
inline ParticleIdentity particle_identity(const RaceParticle& p) {
    return {unsigned(p.kind),unsigned(p.effect),p.water_owner,p.born_step,p.life_ticks,p.initial_cell,
            p.water_node,p.environment_source,p.origin.x,p.origin.y,p.origin.z};
}
struct OwnerResetWitness {std::multiset<ParticleIdentity> others;std::size_t owned_vehicle=0,owned_water=0;};
inline OwnerResetWitness before_owner_reset(const RaceParticles& particles,unsigned owner,double dt) {
    OwnerResetWitness result;
    const auto delta=std::trunc(float(dt)*1024)/1024;
    const auto take=[&](const RaceParticle& p,bool water,bool environment=false) {
        if(!environment && p.water_owner==owner) {if(water)++result.owned_water;else ++result.owned_vehicle;return;}
        if(p.life_ticks>unsigned((p.elapsed_seconds+delta)*1024))result.others.insert(particle_identity(p));
    };
    for(std::size_t i=0;i<particles.smoke_count();++i)take(particles.smoke(i),false);
    for(std::size_t i=0;i<particles.spark_count();++i)take(particles.spark(i),false);
    for(std::size_t i=0;i<particles.water_count();++i)take(particles.water(i),true);
    particles.visit_environment([&](const auto& p){take(p,false,true);});
    return result;
}
inline void check_owner_reset(const RaceParticles& particles,unsigned owner,const OwnerResetWitness& before) {
    std::multiset<ParticleIdentity> after;
    const auto take=[&](const RaceParticle& p,bool environment=false) {
        if(!environment && p.water_owner==owner)throw std::runtime_error("reset retained the selected owner's particles");
        after.insert(particle_identity(p));
    };
    for(std::size_t i=0;i<particles.smoke_count();++i)take(particles.smoke(i));
    for(std::size_t i=0;i<particles.spark_count();++i)take(particles.spark(i));
    for(std::size_t i=0;i<particles.water_count();++i)take(particles.water(i));
    particles.visit_environment([&](const auto& p){take(p,true);});
    if(!std::includes(after.begin(),after.end(),before.others.begin(),before.others.end()))
        throw std::runtime_error("reset deleted another owner's live particles or environment");
    if(after.size()>kRaceWaterPool)throw std::runtime_error("owner reset exceeded the common native pool");
}
} // namespace nfsu::test
