#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "nfsu/draw_execution.h"
#include "nfsu/race_effect_fast.h"
#include <array>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void need(bool value,const std::string& why) { if(!value)throw std::runtime_error(why); }
void original_color_probe(const nfsu::TrackVertex& source) {
    using namespace nfsu;
    // Native declaration COLOR type4 expands D3DCOLOR packedARGB to RGBA.
    // Derive expected lanes from the original packed word, not port vc[] order.
    const std::uint32_t packed=std::uint32_t(source.color[0]) |
        (std::uint32_t(source.color[1])<<8) | (std::uint32_t(source.color[2])<<16) |
        (std::uint32_t(source.color[3])<<24);
    const unsigned rgb[3]={(packed>>16)&255U,(packed>>8)&255U,packed&255U};
    need(rgb[0]!=rgb[2] && rgb[0]<128 && rgb[2]<128,"non-gray unclamped original color witness missing");
    RenderOptions options;options.width=16;options.height=16;options.shading=ShadingMode::none;
    RenderTexture texture;texture.width=1;texture.height=1;texture.valid=true;texture.rgba={255,255,255,255};
    Frame frame;RenderStats stats;std::string error;unsigned char background[4]={0,0,0,255};
    need(frame.reset(16,16,background,{},error),error);
    ClipVertex vertices[3];const double xs[3]={-1,1,0},ys[3]={-1,-1,1};
    for(unsigned i=0;i<3;++i) {
        auto& v=vertices[i];v.x=xs[i];v.y=ys[i];v.z=.5;v.w=1;
        v.r=source.color[0];v.g=source.color[1];v.b=source.color[2];v.a=source.color[3];
    }
    RasterMaterial material;material.texture=&texture;material.ordinary_world=true;
    Rasterizer raster(frame,options,{},stats);need(raster.draw_triangle(vertices,material,0,error),error);
    need(frame.ids()[8*16+8]==0 && stats.fragments_written>0,"original color probe did not write");
    for(unsigned k=0;k<3;++k)need(frame.rgba()[(8*16+8)*4+k]==rgb[k]*2,"actual non-gray D3DCOLOR base channel differs");
    std::cout<<"PASS original packed D3DCOLOR="<<packed<<" native RGB="<<rgb[0]<<','<<rgb[1]<<','<<rgb[2]<<'\n';
}
bool candidate(const std::string& name) {
    return name=="XO_TRACKBARRIERA_1A_00" || name=="XO_CARWALLA_1B_00" ||
           name=="XW_CAR_STACKED_WALLS_1B_00";
}
}
int main(int argc,char** argv) {
    using namespace nfsu;
    try {
        need(argc==2,"usage: nfsu_ordinary_world_retail_tests ROOT");
        const auto root=argument_path(argv[1]);
        RaceGpuScene gpu;std::string error;
        gpu.set_wet_road_reflections(true);
        need(gpu.load(posix_file_system(),root,error,nullptr,nullptr,false,true,true,nullptr,nullptr,8),error);
        const auto& scene=gpu.track().scene;
        RaceRun run;need(run.set_event(8,error)&&run.start(root,gpu.track(),error),error);
        RaceRouteDriver driver(RaceDrivingLine::event8_forward);
        std::set<std::string> appeared;bool car=false,sky=false,reflected=false,original_color=false;
        for(unsigned tick=0;tick<=6000;++tick) {
            if(tick%30==0) {
                RaceGpuFrame frame;need(gpu.frame(run.session(),frame,error),error);
                need(validate_draw_update(frame.commands.resource_table(),frame.commands,kTrackSliceDrawLimit,error),error);
                for(std::size_t i=0;i<frame.commands.draw_count();++i) {
                    const auto view=frame.commands.draw_view(i);const auto& d=view.definition;
                    if(std::strcmp(d.program->name,"sky_textured")==0) {
                        sky=true;need(view.material.fallback[3]!=6,"sky received ordinary marker");continue;
                    }
                    if(i>=frame.track_draws) {
                        car=true;need(view.material.fallback[3]!=6,"car or effect received ordinary marker");continue;
                    }
                    const auto& mesh=scene.region.meshes.at(d.mesh);
                    if(!candidate(mesh.name))continue;
                    need(track_vertex_format(mesh.vertex_format_flags).mask==0,"candidate original effect selector changed");
                    need(view.material.fallback[3]==6,"actual candidate emission lost native baked-color base");
                    need(race_effect_gpu_material(d,view.material).fallback[3]==6,"static/dynamic upload rewrote world base");
                    need(d.program->textures[0].filter==shader_manifest::Filter::nearest,"unrequested sampler change");
                    need(!d.pipeline.blend && d.pipeline.cull==shader_manifest::Cull::none,"candidate alpha/cull changed");
                    const auto& resource=frame.commands.resource_table().resources.at(d.vertex_resource);
                    need(resource.data==reinterpret_cast<const unsigned char*>(mesh.vertices.data()),"candidate authored vertices replaced");
                    std::set<std::array<unsigned char,3>> colors;
                    for(const auto& vertex:mesh.vertices)colors.insert({vertex.color[0],vertex.color[1],vertex.color[2]});
                    need(colors.size()>1,"candidate authored variation absent");
                    if(mesh.name=="XO_CARWALLA_1B_00" && !original_color) {
                        for(const auto& vertex:mesh.vertices)if(vertex.color[0]!=vertex.color[2]) {
                            original_color_probe(vertex);original_color=true;break;
                        }
                    }
                    if(appeared.insert(mesh.name).second)std::cout<<"PASS actual ordinary emission tick="<<tick<<" mesh="<<mesh.name<<'\n';
                }
                for(std::size_t i=0;i<frame.commands.reflection_draw_count();++i) {
                    reflected=true;need(frame.commands.reflection_draw(i).material.fallback[3]!=6,"reflected material changed");
                }
            }
            if(tick<6000)need(run.simulate(driver.sample(run.session()),error)&&run.commit(error),error);
        }
        need(appeared.size()==3 && car && sky && reflected && original_color,"actual candidate/car/sky/reflection/non-gray coverage missing");
        std::cout<<"PASS: ordinary event8 emissions, baked source colors and unaffected car/sky/reflected controls; reflected="<<reflected
                 <<". Route-driver camera, not original matched hardware frame.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
