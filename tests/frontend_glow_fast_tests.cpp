#include "nfsu/frontend_glow_fast.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
// Generated from the actual scalar functions in textured_opaque.frag.glsl.
inline float floor(float x) {return std::floor(x);}
inline float clamp(float x,float low,float high) {return std::clamp(x,low,high);}
#include "frontend_glow_equations.h"
namespace {
void check(bool b,const char* message) {if(!b)throw std::runtime_error(message);}
std::uint32_t reference_channel(unsigned texel,float vertex_byte) {
    const auto vertex=static_cast<unsigned>(std::clamp(std::floor(vertex_byte+.5F),0.F,255.F));
    const unsigned color=(texel*vertex+127U)/255U;
    const unsigned shaded=std::min(255U,(color*256U+128U)>>8U);
    // The original environment and lamp terms are zero under eligibility.
    return shaded;
}
std::uint32_t reference_alpha(unsigned texel,float scale) {
    const float vdotn=.01F;
    const float alpha_scale=scale+vdotn*0.F;
    return static_cast<unsigned>(std::clamp(float(texel)*alpha_scale+.5F,0.F,255.F));
}
}
int main() {
    using namespace nfsu;namespace sm=shader_manifest;
    try {
        RecordedDraw d;for(const auto& program:sm::kPrograms)
            if(std::string(program.name)=="car_textured")d.program=&program;
        check(d.program!=nullptr,"real generated car program");
        d.frontend_sprite_view=1;d.texture_resource=0;
        d.material.environment_range[3]=2;d.pipeline.alpha_test=sm::CompareOp::always;
        d.pipeline.depth_write=false;d.pipeline.color_write=sm::ColorWrite::rgb;
        d.pipeline.blend=true;d.pipeline.blend_src=sm::BlendFactor::src_alpha;
        d.pipeline.blend_dst=sm::BlendFactor::one;d.pipeline.blend_op=sm::BlendOp::add;
        DrawFrameUniform frame;
        auto view=[&]() {return RecordedDrawView{d,0,d.placement,d.material,false};};
        check(frontend_glow_fast(view(),frame),"validated main car glow selects fast path");
        d.material.alpha[0]=0;check(frontend_glow_no_output(view(),frame),"zero-alpha additive RGB without depth is no-op");
        const auto saved=d;
        for(unsigned field=0;field<19;++field) {
            d=saved;frame={};
            switch(field) {
            case 0:d.frontend_sprite_view=4;break;
            case 1:d.program=nullptr;break;
            case 2:d.texture_resource=kNoHandle;break;
            case 3:frame.options[3]=1;break;
            case 4:d.pipeline.alpha_test=sm::CompareOp::greater;break;
            case 5:d.material.environment_range[3]=0;break;
            case 6:d.material.environment_min[3]=1;break;
            case 7:d.material.alpha[1]=.001F;break;
            case 8:d.material.alpha[2]=1;break;
            case 9:d.material.alpha[3]=4;break;
            case 10:d.material.alpha[0]=-.1F;break;
            case 11:d.material.alpha[0]=1.1F;break;
            case 12:d.material.environment_color[3]=9;break;
            case 13:d.material.environment_color[3]=-1;break;
            case 14:d.material.environment_min[0]=.01F;break;
            case 15:d.material.environment_range[2]=.01F;break;
            case 16:d.material.environment_color[1]=256;break;
            case 17:d.material.environment_color[0]=std::numeric_limits<float>::quiet_NaN();break;
            default:d.placement.rows[0]=std::numeric_limits<float>::infinity();break;
            }
            check(!frontend_glow_fast(view(),frame),"unsafe material/state rejected");
            check(!frontend_glow_no_output(view(),frame),"unsafe no-output rejected");
        }
        for(unsigned field=0;field<7;++field) {
            d=saved;frame={};
            switch(field) {
            case 0:d.pipeline.depth_write=true;break;
            case 1:d.pipeline.color_write=sm::ColorWrite::rgba;break;
            case 2:d.pipeline.blend=false;break;
            case 3:d.pipeline.blend_op=sm::BlendOp::subtract;break;
            case 4:d.pipeline.blend_src=sm::BlendFactor::one;break;
            case 5:d.pipeline.blend_dst=sm::BlendFactor::zero;break;
            default:d.material.alpha[0]=std::nextafter(0.F,1.F);break;
            }
            check(!frontend_glow_no_output(view(),frame),"draw with a possible color/alpha/depth contribution retained");
        }
        // Exhaust all byte pairs, including adjacent floating-point values
        // at half-byte rounding boundaries. This executes the actual GLSL.
        for(unsigned texel=0;texel<=255;++texel)for(unsigned byte=0;byte<=255;++byte) {
            for(float vertex:{float(byte),float(byte)-.5F,
                std::nextafter(float(byte)-.5F,0.F),std::nextafter(float(byte)-.5F,256.F)})
                check(frontend_glow_channel(texel,vertex)==reference_channel(texel,vertex),"actual glow RGB rounding parity");
            for(float scale:{float(byte)/255.F,float(byte)/256.F,0.F,1.F})
                check(frontend_glow_alpha(texel,scale)==reference_alpha(texel,scale),"actual glow alpha rounding parity");
            if(texel) {
                const float boundary=(float(byte)-.5F)/float(texel);
                for(float scale:{boundary,std::nextafter(boundary,0.F),std::nextafter(boundary,1.F)})
                    if(scale>=0 && scale<=1)
                        check(frontend_glow_alpha(texel,scale)==reference_alpha(texel,scale),"alpha half-byte neighbor parity");
            }
            // GPU additive RGB equation leaves destination alpha masked.
            for(unsigned dst:{0U,1U,127U,254U,255U})
                check(std::min(255U,dst+((texel*frontend_glow_alpha(255,0.F)+127U)/255U))==dst,
                      "zero-source-alpha blend preserves every destination boundary");
        }
        std::cout<<"PASS: actual GLSL glow scalar RGB/alpha equations, rounding boundaries and strict material/blend/depth eligibility\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
