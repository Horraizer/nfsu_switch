#include "nfsu/race_effect_fast.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
namespace equations {
inline float floor(float x) {return std::floor(x);}
inline float clamp(float x,float low,float high) {return std::clamp(x,low,high);}
inline std::uint32_t min(std::uint32_t a,std::uint32_t b) {return std::min(a,b);}
#include "race_effect_equations.h"
}
namespace {
using namespace nfsu;
namespace sm=shader_manifest;
void check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
struct Image {unsigned width=0,height=0;std::vector<unsigned char> bytes;};
using Images=std::map<std::size_t,Image>;
const Image& image(const UploadTable& table,std::size_t handle,Images& cache) {
    auto found=cache.find(handle);if(found!=cache.end())return found->second;
    const auto& resource=table.resources.at(handle);
    Image result;result.width=resource.texture.width;result.height=resource.texture.height;
    if(resource.texture.format==TextureUploadFormat::rgba8)
        result.bytes.assign(resource.data,resource.data+resource.bytes);
    else {
        check(resource.texture.format==TextureUploadFormat::dxt1 || resource.texture.format==TextureUploadFormat::dxt3,"unexpected native texture format");
        std::string error;
        check(decode_level(resource.texture.format==TextureUploadFormat::dxt1?PixelFormat::dxt1:PixelFormat::dxt3,
                          result.width,result.height,resource.data,resource.bytes,result.bytes,error),error);
    }
    check(result.width && result.height && result.bytes.size()==std::size_t(result.width)*result.height*4,"real texture was not decoded");
    return cache.emplace(handle,std::move(result)).first->second;
}
std::array<unsigned,4> sample(const Image& texture,float u,float v,bool linear) {
    const auto wrap=[](int x,unsigned n){return unsigned((x%int(n)+int(n))%int(n));};
    std::array<unsigned,4> result{};
    const float x=(u-std::floor(u))*texture.width-(linear?.5F:0.F);
    const float y=(v-std::floor(v))*texture.height-(linear?.5F:0.F);
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const float fx=linear?x-std::floor(x):0,fy=linear?y-std::floor(y):0;
    for(unsigned k=0;k<4;++k) {
        const auto at=[&](int dx,int dy){return float(texture.bytes[(std::size_t(wrap(iy+dy,texture.height))*texture.width+wrap(ix+dx,texture.width))*4+k]);};
        result[k]=unsigned(std::floor((at(0,0)*(1-fx)+at(1,0)*fx)*(1-fy)+(at(0,1)*(1-fx)+at(1,1)*fx)*fy+.5F));
    }
    return result;
}
std::array<unsigned,4> original(const std::array<unsigned,4>& texel,const DrawMaterialUniform& m,
                                unsigned shade,bool modulate,const std::array<float,3>& vertex) {
    const float vdotn=.37F;
    const float alpha_scale=m.alpha[0]+vdotn*m.alpha[1];
    std::array<unsigned,4> result{};
    result[3]=unsigned(std::clamp(float(texel[3])*alpha_scale+.5F,0.F,255.F));
    for(unsigned k=0;k<3;++k) {
        unsigned color=texel[k];
        if(modulate)color=(color*unsigned(std::clamp(std::floor(vertex[k]+.5F),0.F,255.F))+127U)/255U;
        unsigned value=std::min(255U,(color*shade+128U)>>8U);
        if(m.alpha[2]!=0)value=0;
        const float environment=4.F*alpha_scale*m.environment_color[k]*(m.environment_min[k]+std::pow(vdotn,m.environment_color[3])*m.environment_range[k]);
        value=unsigned(std::min(float(value)+std::floor(environment*float(texel[3])/255.F+.5F),255.F));
        result[k]=value; // Every eligible material has original lamp mode zero.
    }
    return result;
}
// CPU material frames execute the actual scalar GLSL after the unchanged
// nearest/linear sample. These are equation/blend panels of real race draws,
// not a claim of GPU camera-frame equivalence or a console capture.
void material_frame(const RecordedDraw& d,const DrawMaterialUniform& m,const DrawFrameUniform& frame,
                    const UploadTable& resources,Images& cache) {
    const auto marker=race_effect_fast_marker(d,m);check(marker!=0,"panel draw not eligible");
    const auto& texture=image(resources,d.texture_resource,cache);
    const bool linear=d.program->texture_count && d.program->textures[0].filter==sm::Filter::linear;
    std::vector<unsigned char> baseline,candidate;
    for(unsigned flags:{0U,1U,2U,3U})for(unsigned cutoff:{0U,1U,170U,255U}) {
        auto active=frame;active.options[1]=flags&1U;active.options[2]=(active.options[2]&~1U)|(flags>>1U);active.options[3]=cutoff;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            const float u=(float(x)-1.25F)/6.F,v=(float(y)+.25F)/6.F;
            const auto texel=sample(texture,u,active.options[0]?1.F-v:v,linear);
            const unsigned shade=marker==5 && (active.options[2]&1U)?90U+x*23U:256U;
            const bool modulate=active.options[1]!=0 || m.environment_range[3]>.5F;
            const std::array<float,3> vertex=m.environment_range[3]>1.5F?
                std::array<float,3>{m.environment_color[0],m.environment_color[1],m.environment_color[2]}:
                std::array<float,3>{float(x)*36.5F,float(y)*36.5F,127.5F};
            const auto old=original(texel,m,shade,modulate,vertex);
            std::array<unsigned,4> fast{};
            for(unsigned k=0;k<3;++k)fast[k]=equations::race_effect_channel(texel[k],vertex[k],modulate,shade);
            fast[3]=equations::race_effect_alpha(texel[3],m.alpha[0]);
            check(old==fast,"real race sampled source equation differs");
            const bool alpha_pass=d.pipeline.alpha_test==sm::CompareOp::always || old[3]>d.pipeline.alpha_ref;
            const bool writes=texel[3]>=cutoff && alpha_pass;
            for(unsigned k=0;k<4;++k) {
                const unsigned destination=k==3?231U:(x*31U+y*17U+k*67U)%256U;
                const auto blend=[&](unsigned source) {
                    if(!writes || (k==3 && d.pipeline.color_write==sm::ColorWrite::rgb))return destination;
                    if(!d.pipeline.blend)return source;
                    const unsigned weight=d.pipeline.blend_dst==sm::BlendFactor::one?255U:255U-old[3];
                    return std::min(255U,(source*old[3]+destination*weight+127U)/255U);
                };
                baseline.push_back(static_cast<unsigned char>(blend(old[k])));
                candidate.push_back(static_cast<unsigned char>(blend(fast[k])));
            }
        }
    }
    check(!baseline.empty() && baseline==candidate,"CPU race material RGBA frames differ");
}
void inspect(const RecordedCommands& commands,std::uint64_t tick,bool shaded,std::size_t static_count) {
    std::size_t statics=0,dynamics=0,skipped=0,smoke=0,sparks=0,live=0,glows=0,lamps=0;
    Images images;
    for(std::size_t i=0;i<commands.draw_count();++i) {
        const auto view=commands.draw_view(i);const auto& d=view.definition;
        const auto before=view.material;const auto gpu=race_effect_gpu_material(d,before);
        auto restored=gpu;restored.fallback[3]=before.fallback[3];
        check(std::memcmp(&before,&view.material,sizeof before)==0 && std::memcmp(&before,&restored,sizeof before)==0,"GPU payload changed another material lane");
        const auto marker=race_effect_fast_marker(d,before);
        if(marker) {
            if(i<static_count)++statics;else ++dynamics;
            material_frame(d,before,commands.frame,commands.resource_table(),images);
        }
        if(d.name.find("particle/")!=std::string::npos) {
            check(marker==4,"particle lost unlit specialization");
            if(before.alpha[0]>0)++live;
            if(race_effect_no_output(view)) {
                ++skipped;if(d.name.find("smoke")!=std::string::npos)++smoke;else ++sparks;
            }
        }
        if(d.name.find("race-glow/")==0) {++glows;check(marker==5,"glow flat shade was removed");}
        if(d.name.find("race-shadow/")==0)check(!marker && !race_effect_no_output(view),"ground shadow entered generic optimization");
        if(shaded && std::string(d.program->name).find("car_shaded")==0)check(!marker,"car surface shader entered generic optimization");
    }
    for(std::size_t i=0;i<commands.reflection_draw_count();++i) {
        const auto& draw=commands.reflection_draw(i);
        if(draw.name.find("wet lamp ")==0) {
            ++lamps;check(race_effect_fast_marker(draw,draw.material)==4,"wet lamp lost unlit specialization");
            material_frame(draw,draw.material,commands.frame,commands.resource_table(),images);
        }
    }
        std::cout<<"RACE_EFFECT tick="<<tick<<" static="<<statics<<" dynamic="<<dynamics<<" zero_particles="<<skipped
             <<" smoke="<<smoke<<" sparks="<<sparks<<" live_particles="<<live<<" glows="<<glows<<" reflected_lamps="<<lamps<<'\n';
    check(statics>0 && dynamics>0 && smoke>0 && sparks>0 && glows==4 && lamps>0,"real race eligibility witnesses missing");
    if(tick==1200)check(live>0,"live smoke/spark witness missing");
}
}
int main(int argc,char** argv) {
    using namespace nfsu;
    try {
        check(argc==5,"expected recording root filter shading");std::string error;
        check(check_arguments(argc,argv,1,error),error);
        const auto root=argument_path(argv[2]);TextureFilterMode filter=TextureFilterMode::legacy;
        check(parse_texture_filter(argv[3],filter),"bad filter");const bool shaded=std::string(argv[4])=="shaded";
        std::ifstream input(argument_path(argv[1]),std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)),{});RaceReplay replay;
        check(bool(input) && replay.load(text,error),error);
        RaceRun race;RaceGpuScene scene;scene.set_particles_enabled(true);scene.set_wet_road_reflections(true);
        check(scene.set_texture_filter(filter),"filter selection failed");check(race.prepare_metadata(root,error),error);
        check(scene.load(posix_file_system(),root,error,nullptr,nullptr,false,true,shaded),error);
        check(race.start(root,scene.track(),error),error);
        for(unsigned tick=0;tick<=1200;++tick) {
            if(tick==0 || tick==1 || tick==1200) {
                RaceGpuFrame cached,reference;check(scene.frame(race.session(),cached,error),error);
                check(scene.frame(race.session(),reference,error,nullptr,true),error);
                inspect(cached.commands,tick,shaded,cached.commands.static_track_draw_count());
                inspect(reference.commands,tick,shaded,cached.commands.static_track_draw_count());
            }
            if(tick==1200)break;
            GamepadFrame pad;check(replay.next(race.session().timing().step,pad,error),error);
            check(race.simulate(pad,error) && race.commit(error),error);scene.advance_particles(race.session());
        }
        std::cout<<"PASS: real cached/reference race eligibility and CPU material-frame parity, static/dynamic uniforms, wet lamps and live effects\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
