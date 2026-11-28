#include "nfsu/race_effect_fast.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "race_water_motion_native_cases.h"
#include "race_water_birth_native_cases.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
namespace nfsu {
struct RaceParticleBirthProbe {
    static unsigned native_water_index(const NativeWaterMotionCase& row) {
        // Motion fixtures sort names; birth fixtures retain production preset order.
        const auto birth=std::find_if(kNativeWaterBirthCases.begin(),kNativeWaterBirthCases.end(),
            [&](const auto& item){return std::string(item.name)==row.name;});
        if(birth==kNativeWaterBirthCases.end())throw std::runtime_error("native motion preset name not found");
        return birth->effect;
    }
    static void initial_water_motion(RaceParticles& particles,const NativeWaterMotionCase& row) {
        particles.reset();particles.water_count_=1;
        auto& p=particles.water_[0];p={};
        p.effect=static_cast<RaceParticleEffect>(unsigned(RaceParticleEffect::water_first)+native_water_index(row));
        const auto& preset=particles.presets_[unsigned(p.effect)];
        if(preset.gravity!=row.gravity || preset.drag!=row.drag)
            throw std::runtime_error("named native motion operands differ from loaded retail preset");
        p.origin=row.origin;p.velocity=row.velocity;p.life_ticks=32000;p.elapsed_seconds=0;
        p.water_owner=3;p.water_node=123;p.initial_cell=7;
    }
};
}
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

void check_national_rail_wire(const std::filesystem::path& root) {
    std::vector<unsigned char> image;
    std::vector<RaceTableIssue> issues;
    check(load_race_image(root/"Tracks/TRACKB1008.LZC",image,issues),"National Rail data");
    {
        RaceParticles site;
        std::string error;
        check(site.load_environment(image,error),error);
        unsigned emitted=0, peak=0;
        bool saw_mixed_source_order=false;
        bool released_after_saturation=false;
        for(unsigned frame=1;frame<=420;++frame) {
            const auto before=site.environment_alpha_count()+site.environment_additive_count();
            site.advance_environment(frame,{-270,-948,33});
            constexpr double first_age=17.0/1024;
            std::size_t born=0;
            for(std::size_t i=0;i<site.environment_additive_count();++i)
                born+=site.environment_additive(i).elapsed_seconds==first_age;
            std::size_t visited=0;
            bool seen_additive=false;
            site.visit_environment([&](const RaceParticle& child) {
                ++visited;
                if(child.kind==RaceParticleKind::spark)seen_additive=true;
                else if(seen_additive)saw_mixed_source_order=true;
            });
            check(visited==site.environment_alpha_count()+site.environment_additive_count(),
                  "mixed blend traversal lost track children");
            unsigned alive=0;
            for(std::size_t i=0;i<site.environment_alpha_count();++i) {
                const auto& puff=site.environment_alpha(i);
                born+=puff.elapsed_seconds==first_age;
                if(std::abs(puff.origin.x+270.28)>.8 || std::abs(puff.origin.y+924.68)>.8 ||
                   std::abs(puff.origin.z-49.05)>.5)continue;
                ++alive;
                emitted+=puff.elapsed_seconds==first_age;
            }
            peak=std::max(peak,alive);
            const auto after=site.environment_alpha_count()+site.environment_additive_count();
            check(before+born<=kRaceWaterPool,"track sources reused slots due to expire later in this frame");
            released_after_saturation|=before+born==kRaceWaterPool && after<kRaceWaterPool;
        }
        check(saw_mixed_source_order,"all additive sources were moved after the smoke");
        check(released_after_saturation,"canal fixture did not leave room for the next vehicle update");
        check(emitted>=40 && peak>8,"other track emitters starved the overhead steam outlet");
    }
    RaceChunkPayload placements;
    check(find_race_chunk(image.data(),image.size(),0x35020,placements,issues),"National Rail emitters");
    const auto header=(placements.begin+15)&~std::uint64_t{15};
    constexpr std::size_t stride=0xc0;
    const auto first=header+16;
    const std::string name="XE_SparkSpraySmall_08";
    auto row=first;
    while(row+stride<=placements.end && std::memcmp(image.data()+row+0x80,name.c_str(),name.size()+1)!=0)
        row+=stride;
    check(row+stride<=placements.end,"missing tunnel wire");
    // Isolate the authored emitter so other in-range effects cannot consume its slots.
    std::memmove(image.data()+first,image.data()+row,stride);
    std::fill_n(image.data()+header+12,4,0);
    std::memcpy(image.data()+first+stride,image.data()+first,stride);
    image[header+12]=2;
    const VehicleVec3 eye{-262,-861,45};
    {
        RaceParticles pair;
        std::string error;
        check(pair.load_environment(image,error),error);
        pair.advance_environment(1,eye);
        pair.advance_environment(2,eye);
        bool saw_first=false;
        std::array<bool,2> saw_older{},saw_new{};
        for(std::size_t i=0;i<pair.environment_additive_count();++i) {
            const auto& child=pair.environment_additive(i);
            const auto id=child.environment_source;
            check(id<2 && (!saw_first || id==0),"track placements interleaved their children");
            saw_first|=id==0;
            if(child.elapsed_seconds>17.0/1024) {
                check(!saw_new[id],"new track particles preceded their older siblings");
                saw_older[id]=true;
            } else saw_new[id]=true;
        }
        check(saw_older[0] && saw_older[1] && saw_new[0] && saw_new[1],"two-source wire order was not exercised");
    }
    image[header+12]=1;
    RaceParticles particles;
    std::string error;
    check(particles.load_environment(image,error),error);
    bool resumed=false;
    for(unsigned frame=1;frame<=420;++frame) {
        particles.advance_environment(frame,eye);
        unsigned born=0;
        for(std::size_t i=0;i<particles.environment_additive_count();++i)
            born+=particles.environment_additive(i).elapsed_seconds==17.0/1024;
        if(frame==1)check(born==2,"100 Hz wire lost its initial burst at 60 Hz");
        if(frame>=17 && frame<=160)check(!born,"wire emitted during its authored off interval");
        if(frame>160 && born)resumed=true;
    }
    check(resumed,"wire did not start its next burst");
    particles.reset();
    for(unsigned frame=1;frame<=30;++frame)particles.advance_environment(frame,{10000,10000,0});
    particles.advance_environment(31,eye);
    check(!particles.environment_additive_count(),"out-of-view wire restarted on camera entry");

    // With a stationary carrier, a scaled placement still affects the
    // launch speed. Native packing stores speed separately from direction.
    RaceChunkPayload definitions;
    check(find_race_chunk(image.data(),image.size(),0x35021,definitions,issues),"wire presets");
    auto def=((definitions.begin+15)&~std::uint64_t{15})+16;
    while(def+0x220<=definitions.end && std::memcmp(image.data()+def+8,image.data()+first+12,4))def+=0x220;
    check(def+0x220<=definitions.end,"wire preset for scaled launch");
    const auto field=[&](std::uint64_t at,float v){std::memcpy(image.data()+at,&v,sizeof v);};
    field(first+0x50,.875F);field(first+0x54,0);field(first+0x58,0);
    field(def+0x40,8);field(def+0x44,0);field(def+0x4c,0);
    field(def+0x50,.25F);field(def+0x54,0);field(def+0x84,0);field(def+0x88,0);
    image[def+0x18]=0;
    for(unsigned deferred=0;deferred<=1;++deferred) {
        image[def+0x1e]=static_cast<unsigned char>(deferred);
        check(particles.load_environment(image,error),error);
        particles.advance_environment(1,eye);
        check(particles.environment_additive_count()>0,"no scaled wire birth");
        const auto& v=particles.environment_additive(0).velocity;
        const double expected=deferred?8.*111/127:7.;
        check(std::abs(v.x-expected)<1e-6 && v.y==0 && v.z==0,"scaled wire lost native speed/direction packing");
    }
}
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
void inspect(const RecordedCommands& commands,std::uint64_t tick,bool shaded,std::size_t static_count,bool expect_live=false) {
    std::size_t statics=0,world_statics=0,dynamics=0,skipped=0,smoke=0,sparks=0,live=0,glows=0,lamps=0;
    Images images;
    for(std::size_t i=0;i<commands.draw_count();++i) {
        const auto view=commands.draw_view(i);const auto& d=view.definition;
        const auto before=view.material;const auto gpu=race_effect_gpu_material(d,before);
        auto restored=gpu;restored.fallback[3]=before.fallback[3];
        check(std::memcmp(&before,&view.material,sizeof before)==0 && std::memcmp(&before,&restored,sizeof before)==0,"GPU payload changed another material lane");
        const auto marker=race_effect_fast_marker(d,before);
        // Ordinary static world draws use native baked RGB, not the old flat
        // specialization. Preserve their material and prohibit no-output skips.
        if(i<static_count && before.fallback[3]==6) {
            ++world_statics;
            check(d.program && (std::strcmp(d.program->name,"track_textured")==0 ||
                std::strcmp(d.program->name,"track_linear_textured")==0),"ordinary world escaped track program");
            check(marker==0 && !race_effect_no_output(view),"ordinary world entered generic optimization");
            check(std::memcmp(&before,&gpu,sizeof before)==0,"ordinary world GPU payload changed");
        }
        if(marker) {
            if(i<static_count)++statics;else ++dynamics;
            material_frame(d,before,commands.frame,commands.resource_table(),images);
        }
        if(d.name.find("particle/")!=std::string::npos) {
            if(commands.resource_table().resources[d.vertex_resource].streamed_vertices) {
                check(!marker && before.fallback[3]==9 && commands.resource_table().resources[d.vertex_resource].streamed_vertices,
                      "particle lost its packed world-color vertex stream");
                if(tick==0) {
                    const auto& resources=commands.resource_table().resources;
                    const auto& packed=resources.at(d.texture_resource);
                    check(packed.texture.width==512 && packed.texture.height==512,"water atlas dimensions");
                    const std::array<std::array<unsigned,2>,9> origins{{
                        {0,128},{0,256},{256,0},{0,384},{0,0},{128,384},{64,384},{128,128},{256,256}
                    }};
                    for(std::size_t layer=0;layer<origins.size();++layer) {
                        const auto& layout=kRaceParticleAtlases[layer];
                        const std::string name=std::string("particle/")+layout.name;
                        const auto src=std::find_if(resources.begin(),resources.end(),
                            [&](const auto& r){return r.texture_hash==layout.hash && r.name.find(name)!=std::string::npos;});
                        if(src==resources.end() && layer>=kRaceVehicleAtlasCount &&
                           d.name.find("particle/environment/")==std::string::npos)continue;
                        check(src!=resources.end() && src->texture.width==layout.width,"missing original particle sheet layer");
                        const auto origin=origins[layer];
                        for(unsigned row=0;row<src->texture.height;++row)
                            check(!std::memcmp(packed.data+4*((origin[1]+row)*512+origin[0]),
                                               src->data+4*row*layout.width,4*layout.width),
                                  "packed particle region differs from original image");
                    }
                }
            } else if(before.alpha[3]==8 || before.alpha[0]>0) {
                check(!marker && before.alpha[3]==8 && before.environment_range[3]==2,
                      "authored particle lost world.efx color and alpha modulation");
            } else check(marker==4,"empty particle lost unlit specialization");
            if(before.alpha[0]>0)++live;
            if(race_effect_no_output(view))++skipped;
            // A full live batch needs no extra empty per-puff slot.
            if(d.name.find("smoke")!=std::string::npos)++smoke;else ++sparks;
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
        std::cout<<"RACE_EFFECT tick="<<tick<<" static="<<statics<<" static_world="<<world_statics<<" dynamic="<<dynamics<<" zero_particles="<<skipped
             <<" smoke="<<smoke<<" sparks="<<sparks<<" live_particles="<<live<<" glows="<<glows<<" reflected_lamps="<<lamps<<'\n';
    check(world_statics>0 && dynamics>0 && smoke>0 && sparks>0 && glows==4 && lamps>0,"real race eligibility witnesses missing");
    if(expect_live)check(live>0,"live smoke/spark witness missing");
}
}
int main(int argc,char** argv) {
    using namespace nfsu;
    try {
        check(argc==5,"expected recording root filter shading");std::string error;
        check(check_arguments(argc,argv,1,error),error);
        const auto root=argument_path(argv[2]);TextureFilterMode filter=TextureFilterMode::legacy;
        check(parse_texture_filter(argv[3],filter),"bad filter");const bool shaded=std::string(argv[4])=="shaded";
        check_national_rail_wire(root);
        std::ifstream input(argument_path(argv[1]),std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)),{});RaceReplay replay;
        check(bool(input) && replay.load(text,error),error);
        RaceRun race;RaceGpuScene scene;scene.set_particles_enabled(true);scene.set_wet_road_reflections(true);
        check(scene.set_texture_filter(filter),"filter selection failed");check(race.prepare_metadata(root,error),error);
        check(scene.load(posix_file_system(),root,error,nullptr,nullptr,false,true,shaded),error);
        check(race.start(root,scene.track(),error),error);
        // Exercise material variants with the same retail data loaded by the scene.
        const std::array<unsigned,7> surfaces{1,4,5,6,8,15,16};
        const std::array<RaceParticleEffect,7> effects{RaceParticleEffect::road,
            RaceParticleEffect::blue,RaceParticleEffect::yellow,RaceParticleEffect::red,
            RaceParticleEffect::dirt,RaceParticleEffect::grass,RaceParticleEffect::rough_road};
        const std::array<RaceParticleAtlas,7> atlases{RaceParticleAtlas::smoke,
            RaceParticleAtlas::colored,RaceParticleAtlas::colored,RaceParticleAtlas::colored,
            RaceParticleAtlas::dust,RaceParticleAtlas::dust,RaceParticleAtlas::smoke};
        for (unsigned i=0; i<surfaces.size(); ++i) {
            auto probe=scene.particles();
            VehicleTrackTelemetry step;
            step.orientation.w=1;
            auto& wheel=step.model.wheels[0];
            wheel.contact=true;
            wheel.surface=surfaces[i];
            wheel.effect_slip_velocity={10,0};
            for (step.step=1; step.step<=48; ++step.step) probe.advance(step,race.session().spec());
            check(probe.smoke_count()>0,"retail surface did not produce particles");
            const auto& particle=probe.smoke(0);
            check(particle.effect==effects[i] && particle.atlas==atlases[i],
                  "retail surface selected the wrong particle preset or atlas");
            const auto& layout=kRaceParticleAtlases[static_cast<unsigned>(atlases[i])];
            const auto cell=race_particle_atlas_cell(particle,probe.step());
            check(cell>=layout.first_cell && cell<layout.first_cell+layout.columns*layout.rows,
                  "particle cell escaped its authored atlas grid");
            const double base_life=atlases[i]==RaceParticleAtlas::dust ? 2.5 : 4.0;
            check(particle.life_ticks>=unsigned(.7*base_life*1024) &&
                  particle.life_ticks<=unsigned(base_life*1024),"particle life escaped its retail range");
            VehicleVec3 at;
            double diameter=0,opacity=0;
            std::array<unsigned,3> tint{};
            check(probe.visual(particle,at,diameter,opacity,tint) && opacity>0 &&
                  opacity<=.5 && at.z>particle.origin.z,
                  "retail smoke curve or upward motion missing");
        }
        for (unsigned surface : {1U,2U,3U,7U,13U,14U}) {
            auto wet=scene.particles(), dry=scene.particles();
            VehicleTrackTelemetry step;
            step.orientation.w=1;
            step.velocity.x=35;
            step.model.owner_speeds.magnitude=35.F;
            for (auto& wheel : step.model.wheels) {
                wheel.contact=true;
                wheel.surface=surface;
            }
            bool spray=false;
            for (step.step=1; step.step<=2; ++step.step) {
                wet.advance(step,race.session().spec(),10,surface);
                dry.advance(step,race.session().spec(),10,0);
                check(dry.smoke_count()==0 && dry.water_count()==0,"spray emitted without an authored driving surface");
                for (unsigned i=0; i<wet.smoke_count(); ++i)
                    spray|=wet.smoke(i).atlas==RaceParticleAtlas::water_spray;
                for (unsigned i=0; i<wet.water_count(); ++i)
                    spray|=wet.water(i).atlas==RaceParticleAtlas::water_spray;
            }
            check(spray,"driving spray did not start immediately with the authored DUSTA_CS atlas");
        }
        {
            auto canal=scene.particles();
            VehicleTrackTelemetry step;
            step.orientation.w=1;
            step.model.owner_speeds.magnitude=50.F;
            for (auto& wheel:step.model.wheels) wheel.contact=true;
            for (step.step=1;step.step<=2;++step.step)
                canal.advance(step,race.session().spec(),10,14);
            constexpr unsigned drive=unsigned(RaceParticleEffect::water_first)+13;
            std::array<unsigned,3> jets{};
            for (std::size_t i=0;i<canal.water_count();++i) {
                const auto& p=canal.water(i);
                if (p.atlas!=RaceParticleAtlas::water_spray) continue;
                const auto side=unsigned(p.effect)-drive;
                check(side<3,"unexpected high speed canal jet");
                ++jets[side];
                const bool direction=side==0 ? p.velocity.x>0 :
                    (side==1 ? p.velocity.y>0 && p.origin.y>0 : p.velocity.y<0 && p.origin.y<0);
                check(direction && p.velocity.z>0,"canal jet lost its marker or placement basis");
            }
            check(jets[0]>1 && jets[1]>1 && jets[2]>1,"canal entry lost the original burst");
            {
                auto drying=canal;
                auto dry_step=step;
                dry_step.model.solver_dt=kDriveFrameSeconds/4;
                std::vector<unsigned> lives;
                // Native421012/403A60 precedes car402D30: first draw is the
                // unaged birth point, followed by one17-tick world update.
                std::vector<RaceParticle> newborns;
                for(std::size_t i=0;i<drying.water_count();++i) {
                    const auto& child=drying.water(i);
                    check(child.elapsed_seconds==0 && child.displacement.x==0 &&
                          child.displacement.y==0 && child.displacement.z==0,
                          "new water did not retain its native first-draw birth state");
                    lives.push_back(child.life_ticks);newborns.push_back(child);
                }
                // Compare the same public telemetry/world boundary directly
                // with independent original403A60 outputs for all19 presets.
                std::vector<const NativeWaterMotionCase*> native_first;
                for(const auto& row:kNativeWaterMotionCases)if(row.frames==1 && row.dt<.02)native_first.push_back(&row);
                unsigned native_updates=0;
                for(unsigned order=0;order<2;++order) {
                    if(order)std::sort(native_first.begin(),native_first.end(),[](const auto* a,const auto* b) {
                        return RaceParticleBirthProbe::native_water_index(*a)<RaceParticleBirthProbe::native_water_index(*b);
                    });
                    std::array<bool,kRaceWaterPresetCount> seen{};
                    for(const auto* answer:native_first) {
                    const auto& row=*answer;const auto id=RaceParticleBirthProbe::native_water_index(row);
                    check(id<seen.size() && !seen[id],"named native motion controls repeat or exceed a loaded preset");seen[id]=true;
                    auto witness=scene.particles();
                    RaceParticleBirthProbe::initial_water_motion(witness,row);
                    VehicleTrackTelemetry next;next.step=1;next.model.solver_dt=row.dt;
                    witness.advance(next,race.session().spec(),150,0);
                    check(witness.water_count()==1,"native next-step witness expired or emitted");
                    const auto& child=witness.water(0);
                    const auto near=[](double a,double b){return std::abs(a-b)<.000008;};
                    check(near(child.origin.x+child.displacement.x,row.point.x) &&
                          near(child.origin.y+child.displacement.y,row.point.y) &&
                          near(child.origin.z+child.displacement.z,row.point.z) &&
                          near(child.velocity.x,row.final_velocity.x) &&
                          near(child.velocity.y,row.final_velocity.y) &&
                          near(child.velocity.z,row.final_velocity.z),
                          "first public water update differs from original403A60");
                    check(child.elapsed_seconds==17.0/1024 && child.life_ticks==32000 &&
                          child.born_step==0 && child.water_owner==3 && child.water_node==123 && child.initial_cell==7,
                          "first public update changed native lifetime or child identity");
                    ++native_updates;
                    }
                    check(std::all_of(seen.begin(),seen.end(),[](bool value){return value;}),
                          "native-name or production-order traversal missed a preset");
                }
                check(native_updates==2*kRaceWaterPresetCount,"next-step native controls missed either preset order");
                for(unsigned frame=1;frame<=120;++frame) {
                    for(unsigned substep=0;substep<4;++substep,++dry_step.step)
                        drying.advance(dry_step,race.session().spec(),10,0);
                    if(frame==1) {
                        check(drying.water_count()==newborns.size(),"first dry world step changed the burst count");
                        bool moved=false;
                        for(std::size_t i=0;i<newborns.size();++i) {
                            const auto& a=drying.water(i);const auto& b=newborns[i];
                            check(a.elapsed_seconds==17.0/1024 && a.life_ticks==b.life_ticks &&
                                  a.effect==b.effect && a.born_step==b.born_step && a.water_owner==b.water_owner &&
                                  a.water_node==b.water_node && a.initial_cell==b.initial_cell,
                                  "next world step changed birth identity or advanced its clock more than once");
                            moved|=a.displacement.x!=0 || a.displacement.y!=0 || a.displacement.z!=0;
                        }
                        check(moved,"next world step did not move the actual canal birth");
                    }
                    const auto expected=std::count_if(lives.begin(),lives.end(),[&](unsigned life){return life>17*frame;});
                    check(drying.water_count()==std::size_t(expected),"water lifetime changed with the number of solver steps");
                }
            }
            const auto first=canal.water_count();
            for (;step.step<=4;++step.step) canal.advance(step,race.session().spec(),10,14);
            check(canal.water_count()>first,"negative burst budget incorrectly retained its delay");
            // The surviving DR nodes stay in place when slower motion first
            // activates DRSL. The native lists append new nodes at the tail.
            auto changed=canal;
            auto slower=step;
            slower.model.owner_speeds.magnitude=22.F;
            for(;slower.step<=14;++slower.step)changed.advance(slower,race.session().spec(),10,14);
            std::vector<unsigned> runs;
            std::uint64_t last_birth=0;
            for(std::size_t i=0;i<changed.water_count();++i) {
                const auto& p=changed.water(i);
                const unsigned id=unsigned(p.effect)-unsigned(RaceParticleEffect::water_first);
                if(runs.empty() || runs.back()!=id) {
                    runs.push_back(id);
                    last_birth=0;
                }
                check(p.born_step>=last_birth,"water emitter children left FIFO order");
                last_birth=p.born_step;
            }
            check(runs==std::vector<unsigned>({13,14,15,16,17,18,5,6,7,8,9,10,11,12}),
                  "later water nodes displaced or interleaved surviving emitters");
            for (auto& wheel:step.model.wheels) wheel.contact=false;
            for (;step.step<=800;++step.step) {
                canal.advance(step,race.session().spec(),10,step.step<8?14:0);
                for (std::size_t i=0;i<canal.water_count();++i)
                    check(canal.water(i).born_step<=4,"airborne or dry car kept emitting canal spray");
            }
            check(canal.water_count()==0,"water trail did not expire after exit");
            for(auto& wheel:step.model.wheels)wheel.contact=true;
            for(;step.step<960;++step.step)canal.advance(step,race.session().spec(),10,14);
            CameraFrame camera{};
            camera.eye={0,0,50};camera.forward={0,0,-1};camera.right={1,0,0};camera.up={0,1,0};
            camera.focal_x=1;camera.focal_y=1;camera.near_plane=.1;camera.far_plane=200;
            const auto view=canal.visible_water(camera);
            std::size_t expected=0;
            for(std::size_t i=0;i<canal.water_count();++i) {
                VehicleVec3 at;double size=0,opacity=0;std::array<unsigned,3> tint{};
                if(!canal.visual(canal.water(i),at,size,opacity,tint)||opacity<=0)continue;
                const auto clip=project_point(camera,{at.x,at.z,-at.y});
                if(clip.w<=camera.near_plane||clip.w>camera.far_plane||
                   std::abs(clip.x)>clip.w+size*.5||std::abs(clip.y)>clip.w+size*.5)continue;
                check(expected<view.count && view.indices[expected++]==i,"camera discarded or reordered visible spray");
            }
            check(expected==view.count && expected>96,"dense canal view was not exercised");
        }
        {
            auto sparks=scene.particles(), gentle=scene.particles();
            TrackContactWorld world;
            world.collision.polygons.resize(1);
            world.collision.polygons[0].unknown0[2]=20;
            VehicleTrackTelemetry step;
            step.step=1;
            step.audio_contact_count=1;
            step.audio_contacts[0].normal={0,1,0};
            step.audio_contacts[0].point={3,4,5};
            step.audio_contacts[0].slide_mps=25;
            sparks.advance(step,race.session().spec(),10,0,&world);
            ++step.step;sparks.advance(step,race.session().spec(),10,0,&world);
            step.audio_contacts[0].slide_mps=15;
            gentle.advance(step,race.session().spec(),10,0,&world);
            ++step.step;gentle.advance(step,race.session().spec(),10,0,&world);
            check(sparks.spark_count()>2 && gentle.spark_count()==0,
                  "solid wall scrape lost its authored burst at zero post-contact speed");
            check(sparks.spark(0).origin.z==5,"wall spark lost the collision witness");
            RacePairFrame contact;
            contact.count=1;
            auto& bumper=contact.pairs[0].contact;
            bumper.applied=true;bumper.point={3,4,5};bumper.impulse={10,0,0};
            gentle.emit_car_impact_sparks(contact,{});
            check(gentle.spark_count()==0,"car impact threshold is strict");
            bumper.impulse={15,0,0};
            gentle.emit_car_impact_sparks(contact,{100,0,0});
            check(gentle.spark_count()==0,"car sparks respect camera distance");
            gentle.emit_car_impact_sparks(contact,{});
            check(gentle.spark_count()==10&&
                  gentle.spark(0).effect==RaceParticleEffect::car_impact&&
                  gentle.spark(0).atlas==RaceParticleAtlas::sparks&&gentle.spark(0).origin.z==5,
                  "bumper impact must emit the authored bounded spark burst at the collision point");
            check(gentle.smoke_count()>0&&gentle.smoke(0).effect==RaceParticleEffect::car_impact_smoke,
                  "car impact lost the smoke in the authored collision group");
        }
        RaceParticles expected=scene.particles();bool saw_live=false;
        for(unsigned tick=0;tick<kRaceInputMaxTicks;++tick) {
            const bool live_witness=!saw_live && expected.smoke_count()>=4 && expected.spark_count()==0;
            if(tick==0 || tick==1 || tick==1200 || live_witness) {
                RaceGpuFrame cached,reference;check(scene.frame(race.session(),cached,error),error);
                check(scene.frame(race.session(),reference,error,nullptr,true),error);
                inspect(cached.commands,tick,shaded,cached.commands.static_track_draw_count(),live_witness);
                inspect(reference.commands,tick,shaded,cached.commands.static_track_draw_count(),live_witness);
                saw_live=saw_live||live_witness;
            }
            if(tick>=1200&&saw_live)break;
            if(race.session().timing().outcome!=RaceOutcome::running)break;
            GamepadFrame pad;check(replay.next(race.session().timing().step,pad,error),error);
            check(race.simulate(pad,error) && race.commit(error),error);scene.advance_particles(race.session());
            expected.advance(race.session());
        }
        check(saw_live,"input replay did not exercise required live smoke/spark coverage");
        std::cout<<"PASS: real cached/reference race eligibility and CPU material-frame parity, static/dynamic uniforms, wet lamps and live effects\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
