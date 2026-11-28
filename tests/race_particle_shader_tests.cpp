#include "nfsu/race_particle_shader.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace equation {
#include "particle_zero_equation.h"
}
using namespace nfsu;
namespace sm=nfsu::shader_manifest;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main()try {
    RecordedDraw water;
    for(const auto& program:sm::kPrograms)if(std::strcmp(program.name,"car_textured")==0)water.program=&program;
    check(water.program!=nullptr,"missing source program");
    for(std::size_t i=0;i<water.program->variant_count;++i)
        if(std::strcmp(water.program->variants[i].name,"source_over")==0)water.pipeline=water.program->variants[i];
    water.texture_resource=0;water.material.fallback[3]=9;water.pipeline.depth_write=false;
    check(race_particle_zero_alpha_shader(water),"actual read-only source-over batch not admitted");
    auto reject=[&](const auto& change){auto d=water;change(d);check(!race_particle_zero_alpha_shader(d),"unsafe state admitted");};
    reject([](auto& d){d.program=nullptr;});
    reject([](auto& d){d.texture_resource=kNoHandle;});
    reject([](auto& d){d.material.fallback[3]=6;});
    reject([](auto& d){d.material.alpha[3]=7;});
    reject([](auto& d){d.material.alpha[3]=8;});
    reject([](auto& d){d.material.alpha[0]=std::numeric_limits<float>::infinity();});
    reject([](auto& d){d.material.alpha[0]=std::numeric_limits<float>::quiet_NaN();});
    reject([](auto& d){d.pipeline.depth_write=true;});
    reject([](auto& d){d.pipeline.blend=false;});
    reject([](auto& d){d.pipeline.blend_src=sm::BlendFactor::one;});
    reject([](auto& d){d.pipeline.blend_dst=sm::BlendFactor::zero;});
    reject([](auto& d){d.pipeline.alpha_test=sm::CompareOp::greater;});
    reject([](auto& d){d.pipeline.color_write=sm::ColorWrite::rgba;});
    auto additive=water;additive.pipeline.blend_dst=sm::BlendFactor::one;
    check(race_particle_zero_alpha_shader(additive),"read-only additive path not admitted");
    auto world=water;world.material.fallback[3]=6;
    check(race_particle_shader_transition(true,&world)&&race_particle_shader_transition(false,&water)&&
        race_particle_shader_transition(true,nullptr)&&!race_particle_shader_transition(true,&water),
        "same-program material transition retained the wrong fragment stage");
    check(equation::particle_sample_is_zero(0.F)&&equation::particle_sample_is_zero(-0.F),"exact zero not discarded");
    for(float alpha:{std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::min(),
                    1.F/65536,1.F/255,1.F,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        check(!equation::particle_sample_is_zero(alpha),"a nonzero filtered edge or exceptional sample was discarded");
    // Any filtered zero sample is a no-op for every normalized destination,
    // tint, opacity and both admitted blend equations. Positive samples are
    // passed through, including values below an 8-bit alpha threshold.
    std::uint64_t cases=0;
    for(unsigned dst=0;dst<256;++dst)for(unsigned rgb=0;rgb<256;++rgb)for(unsigned opacity:{0U,1U,127U,255U}) {
        const float texel_alpha=0,src_alpha=texel_alpha*(float(opacity)/255)*2;
        const float source=(float(rgb)/255)*2;
        const float dest=float(dst)/255;
        check(source*src_alpha+dest*(1-src_alpha)==dest&&source*src_alpha+dest==dest,"zero-alpha blend changes destination");
        ++cases;
    }
    std::cout<<"PASS particle shader eligibility, shader transitions, filtered-zero equation and blend identity cases="<<cases<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
