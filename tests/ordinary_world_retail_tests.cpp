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
        std::set<std::string> appeared;bool car=false,sky=false,reflected=false;
        for(unsigned tick=0;tick<=4000;++tick) {
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
                    if(appeared.insert(mesh.name).second)std::cout<<"PASS actual ordinary emission tick="<<tick<<" mesh="<<mesh.name<<'\n';
                }
                for(std::size_t i=0;i<frame.commands.reflection_draw_count();++i) {
                    reflected=true;need(frame.commands.reflection_draw(i).material.fallback[3]!=6,"reflected material changed");
                }
            }
            if(tick<4000)need(run.simulate(driver.sample(run.session()),error)&&run.commit(error),error);
        }
        need(appeared.size()==3 && car && sky && reflected,"actual candidate/car/sky/reflection coverage missing");
        std::cout<<"PASS: ordinary event8 emissions, baked source colors and unaffected car/sky/reflected controls; reflected="<<reflected
                 <<". Route-driver camera, not original matched hardware frame.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
