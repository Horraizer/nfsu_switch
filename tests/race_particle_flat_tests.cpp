#include "nfsu/race_particle_flat.h"
#include <array>
#include <iostream>
#include <stdexcept>
namespace {void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}}
int main()try{
    require(nfsu::race_particle_flat_material(9,1,0),"unit-alpha material refused");
    require(!nfsu::race_particle_flat_material(9,.5F,0),"fractional material alpha admitted");
    require(!nfsu::race_particle_flat_material(2,1,0),"other effect equation admitted");
    require(!nfsu::race_particle_flat_material(9,1,1),"lamp material admitted");
    require(nfsu::race_particle_flat_frame(0,51),"normal wet frame refused");
    require(!nfsu::race_particle_flat_frame(1,51),"V-flipped frame admitted");
    require(!nfsu::race_particle_flat_frame(0,1024),"HUD projection admitted");
    std::array<unsigned char,8*36> vertices{};
    for(unsigned i=0;i<8;++i)for(unsigned c=0;c<4;++c)vertices[i*36+24+c]=static_cast<unsigned char>((i/4)*10+c);
    require(nfsu::race_particle_flat_colors(vertices.data(),vertices.size(),36,24),"distinct constant quads refused");
    vertices[5*36+27]^=1;
    require(!nfsu::race_particle_flat_colors(vertices.data(),vertices.size(),36,24),"varying corner alpha admitted");
    vertices[5*36+27]^=1;vertices[1*36+24]^=1;
    require(!nfsu::race_particle_flat_colors(vertices.data(),vertices.size(),36,24),"varying corner RGB admitted");
    require(!nfsu::race_particle_flat_colors(nullptr,144,36,24),"null stream admitted");
    require(!nfsu::race_particle_flat_colors(vertices.data(),0,36,24),"empty stream admitted");
    require(!nfsu::race_particle_flat_colors(vertices.data(),7*36,36,24),"partial quad admitted");
    require(!nfsu::race_particle_flat_colors(vertices.data(),144,0,24),"zero stride admitted");
    require(!nfsu::race_particle_flat_colors(vertices.data(),144,36,35),"color outside stride admitted");
    require(!nfsu::race_particle_flat_colors(vertices.data(),3*1024*1024,36,24),"unbounded stream admitted");
    const std::uint16_t good[]{0,1,2,0,2,3,4,6,5,4,7,6};
    require(nfsu::race_particle_flat_indices(reinterpret_cast<const unsigned char*>(good),sizeof good),"both winding choices refused");
    const std::uint16_t bad[]{0,1,4};
    require(!nfsu::race_particle_flat_indices(reinterpret_cast<const unsigned char*>(bad),sizeof bad),"cross-quad triangle admitted");
    require(!nfsu::race_particle_flat_indices(reinterpret_cast<const unsigned char*>(good),sizeof good-1),"partial index admitted");
    require(!nfsu::race_particle_flat_indices(nullptr,sizeof good),"null index stream admitted");
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)
        require(nfsu::race_particle_stage_changed(a!=0,a==2,b!=0,b==2)==(a!=b),"ordinary/C1/flat shader transition lost");
    std::cout<<"PASS constant-quad colors, topology refusal and nine ordinary/C1/flat transitions\n";return 0;
}catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<'\n';return 1;}
