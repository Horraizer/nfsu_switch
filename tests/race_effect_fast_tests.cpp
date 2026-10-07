#include "nfsu/race_effect_fast.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
inline float floor(float x) {return std::floor(x);}
inline float clamp(float x,float low,float high) {return std::clamp(x,low,high);}
inline std::uint32_t min(std::uint32_t a,std::uint32_t b) {return std::min(a,b);}
#include "race_effect_equations.h"
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
unsigned reference_channel(unsigned texel,float vertex,bool modulate,unsigned shade) {
    unsigned color=texel;
    if(modulate)color=(color*static_cast<unsigned>(std::clamp(std::floor(vertex+.5F),0.F,255.F))+127U)/255U;
    auto value=std::min(255U,(color*shade+128U)>>8U);
    // The original pow/environment and lamp expressions have zero terms.
    for(float vdotn:{.01F,.5F,1.F}) {
        const float environment=4.F*.7F*255.F*(0.F+std::pow(vdotn,8.F)*0.F);
        check(environment==0,"original eliminated environment term");
    }
    return value;
}
}
int main() {
    using namespace nfsu;namespace sm=shader_manifest;
    try {
        RecordedDraw draw;draw.texture_resource=0;
        for(const auto& p:sm::kPrograms)if(std::string(p.name)=="car_textured")draw.program=&p;
        check(draw.program,"generated car program");
        draw.pipeline.depth_write=false;draw.pipeline.color_write=sm::ColorWrite::rgb;
        draw.pipeline.blend=true;draw.pipeline.blend_src=sm::BlendFactor::src_alpha;
        draw.pipeline.blend_dst=sm::BlendFactor::one;draw.pipeline.blend_op=sm::BlendOp::add;
        draw.material.alpha[0]=0;
        auto view=[&](){return RecordedDrawView{draw,0,draw.placement,draw.material,false};};
        check(race_effect_fast_marker(draw,draw.material)==5,"flat race glow retains lighting");
        check(race_effect_no_output(view()),"zero additive glow is no output");
        draw.material.fallback[3]=2;
        check(race_effect_fast_marker(draw,draw.material)==4,"particle is already unlit");
        draw.pipeline.blend_dst=sm::BlendFactor::one_minus_src_alpha;
        check(race_effect_no_output(view()),"zero source-over smoke is no output");
        const auto saved=draw;
        for(unsigned field=0;field<22;++field) {
            draw=saved;
            switch(field) {
            case 0:draw.frontend_sprite_view=1;break;
            case 1:draw.program=nullptr;break;
            case 2:draw.texture_resource=kNoHandle;break;
            case 3:draw.material.fallback[3]=3;break;
            case 4:draw.material.fallback[3]=-1;break;
            case 5:draw.material.environment_range[3]=3;break;
            case 6:draw.material.environment_min[3]=1;break;
            case 7:draw.material.alpha[1]=.001F;break;
            case 8:draw.material.alpha[2]=1;break;
            case 9:draw.material.alpha[3]=1;break;
            case 10:draw.material.alpha[3]=4;break;
            case 11:draw.material.alpha[0]=-.1F;break;
            case 12:draw.material.alpha[0]=1.1F;break;
            case 13:draw.material.environment_color[3]=9;break;
            case 14:draw.material.environment_color[3]=-1;break;
            case 15:draw.material.environment_min[0]=.01F;break;
            case 16:draw.material.environment_range[2]=.01F;break;
            case 17:draw.material.environment_color[1]=256;break;
            case 18:draw.material.environment_color[0]=std::numeric_limits<float>::quiet_NaN();break;
            case 19:draw.material.alpha[0]=std::numeric_limits<float>::quiet_NaN();break;
            case 20:draw.material.environment_color[3]=std::numeric_limits<float>::infinity();break;
            default:draw.material.fallback[3]=std::numeric_limits<float>::quiet_NaN();break;
            }
            check(!race_effect_fast_marker(draw,draw.material),"unsafe specialization rejected");
            check(!race_effect_no_output(view()),"unsafe elision rejected");
        }
        for(unsigned field=0;field<8;++field) {
            draw=saved;
            switch(field) {
            case 0:draw.pipeline.depth_write=true;break;
            case 1:draw.pipeline.color_write=sm::ColorWrite::rgba;break;
            case 2:draw.pipeline.blend=false;break;
            case 3:draw.pipeline.blend_op=sm::BlendOp::subtract;break;
            case 4:draw.pipeline.blend_src=sm::BlendFactor::one;break;
            case 5:draw.pipeline.blend_dst=sm::BlendFactor::zero;break;
            case 6:draw.material.alpha[0]=std::nextafter(0.F,1.F);break;
            default:draw.placement.rows[0]=std::numeric_limits<float>::infinity();break;
            }
            check(!race_effect_no_output(view()),"contributing draw retained");
        }
        // Static payloads never encode frame-dependent shade, vertex-color,
        // reflection-pass or cutoff flags. Dynamic alpha is copied afresh.
        draw=saved;draw.material.fallback[3]=0;
        const auto initial=race_effect_gpu_material(draw,draw.material);
        check(draw.material.fallback[3]==0 && initial.fallback[3]==5,"private static material copy");
        draw.material.alpha[0]=.25F;
        check(race_effect_gpu_material(draw,draw.material).alpha[0]==.25F && !race_effect_no_output(view()),"dynamic alpha becomes visible");
        for(const auto& p:sm::kPrograms) {
            draw.program=&p;
            const std::string name=p.name;
            const bool generic=name=="car_textured" || name=="track_textured" || name=="track_linear_textured";
            check(bool(race_effect_fast_marker(draw,draw.material))==generic,"sky, fallback and shaded car programs excluded");
        }
        for(unsigned texel=0;texel<=255;++texel)for(unsigned byte=0;byte<=255;++byte) {
            for(unsigned shade:{0U,90U,127U,128U,255U,256U})
                for(bool modulate:{false,true})
                    for(float vertex:{float(byte),float(byte)-.5F,std::nextafter(float(byte)-.5F,0.F),std::nextafter(float(byte)-.5F,256.F)})
                        check(race_effect_channel(texel,vertex,modulate,shade)==reference_channel(texel,vertex,modulate,shade),"actual GLSL RGB parity");
            for(float scale:{0.F,1.F,float(byte)/255.F,float(byte)/256.F}) {
                const auto expected=static_cast<unsigned>(std::clamp(float(texel)*(scale+.01F*0.F)+.5F,0.F,255.F));
                check(race_effect_alpha(texel,scale)==expected,"actual GLSL alpha parity");
            }
            if(texel) {
                const float half=(float(byte)-.5F)/float(texel);
                for(float scale:{half,std::nextafter(half,0.F),std::nextafter(half,1.F)})
                    if(scale>=0 && scale<=1)check(race_effect_alpha(texel,scale)==static_cast<unsigned>(std::clamp(float(texel)*scale+.5F,0.F,255.F)),"alpha boundary parity");
            }
        }
        std::cout<<"PASS: actual GLSL race RGB/alpha, flat shade, modulation, rounding and unsafe-state controls\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
