#include "nfsu/intro_grid.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace nfsu;
namespace {
void require(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
struct Visual {PreparedCarScene scene;std::vector<PreparedInstance> base;CarGlowAnchors anchors;};
}
int main(int argc,char** argv) {try {
    require(argc>=2&&argc<=3,"expected retail root and optional event5/6");
    const auto root=argument_path(argv[1]);
    const unsigned event=argc>2?unsigned(std::stoul(argv[2])):6;
    require(event==5||event==6,"expected event5/6");
    const unsigned frames=9000;
    IntroGrid grid;std::string error;
    require(load_race_grid(root,event,"CIVIC",1,grid,error),error);
    IntroGridActor* human=nullptr;std::vector<RaceRivalSetup> rivals;
    std::array<int,3> contexts{};
    for(auto& actor:grid.actors) {
        if(actor.config.role==Event0Role::opponent) {
            contexts[rivals.size()]=actor.config.selected_context;
            rivals.push_back({actor.placement_spec,actor.pose,actor.hull,actor.config.event_label,
                actor.assets.car_type_name,actor.config.ai_mode,actor.config.config_id});
        }else human=&actor;
    }
    require(human!=nullptr&&!grid.traffic.empty(),"missing authored player or traffic");
    RaceRun run;
    require(run.set_event(event,error)&&run.set_car("CIVIC",error),error);
    require(run.set_intro_grid(human->pose,human->hull,rivals,contexts,error,human->config.final_grid),error);
    std::vector<RaceTrafficSetup> setup;
    for(const auto& actor:grid.traffic)setup.push_back({actor.placement_spec,actor.hull,actor.assets.car_type_name,unsigned(actor.assets.car_type)});
    require(run.set_intro_traffic(setup,error),error);
    TrackSlice track;require(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,event),error);
    require(run.start(root,track,error),error);
    auto scene=std::make_unique<RaceGpuScene>();scene->set_start_scenes_enabled(false);
    scene->set_particles_enabled(false);scene->set_race_visuals(true);scene->set_original_draw_distance(true);
    require(scene->load(posix_file_system(),root,error,nullptr,nullptr,false,true,false,&human->assets,nullptr,event),error);
    std::size_t rival_glows=0,traffic_glows=0;std::vector<Visual> visuals;
    for(auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent) {
        rival_glows+=original_car_glow_count(actor.glow_anchors);
        require(scene->add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),
            actor.frame->skin_binding.source,std::move(actor.shadow),error,false,actor.glow_anchors,&actor.hull),error);
    }
    for(auto& actor:grid.traffic) {
        visuals.push_back({actor.frame->scene,actor.base_instances,actor.glow_anchors});
        traffic_glows+=original_car_glow_count(actor.glow_anchors);
        require(scene->add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),
            kNoHandle,std::move(actor.shadow),error,true,actor.glow_anchors,&actor.hull),error);
    }
    unsigned rendered=0,weak_kinematic=0,weak_rigid=0,visible_weak_tails=0,compared=0;
    RaceRouteDriver driver(race_event_driving_line(event));
    for(unsigned frame=0;frame<frames;++frame) {
        auto pad=driver.sample(run.session());
        // The ordinary rear-view control changes observation, not the driver
        // or any traffic state. Alternate views to cover passed road cars.
        if(frame%240>=120)pad.pads[0].buttons|=gamepad_button_bit(GamepadButton::stick_right);
        require(run.simulate(pad,error)&&run.commit(error),error);
        const auto& actors=run.session().traffic()->actors();bool weak=false;
        for(const auto& actor:actors)if(actor.active&&actor.driver.pedals.brake>0&&
                actor.driver.pedals.brake<=.05000000074505806F) {
            weak=true;if(actor.impacted)++weak_rigid;else ++weak_kinematic;
        }
        if(frame!=0&&!weak)continue;
        RaceGpuFrame packet;require(scene->frame(run.session(),packet,error),error);++rendered;
        OrbitCamera orbit;
        require(drive_render_camera(run.session().camera(),ChaseFovMode::retail_widescreen,1280,720,orbit,error,640./480),error);
        orbit.focal_x_override=orbit.focal_y_override*720./1280;
        orbit.far_plane=packet.far_plane;
        CameraFrame camera;require(make_camera_frame(orbit,1280,720,camera,error),error);
        require(packet.commands.draw_count()>=rival_glows+traffic_glows,"missing actor glow suffix");
        std::size_t last_glow=0;
        for(std::size_t draw=0;draw<packet.commands.draw_count();++draw) {
            const auto& name=packet.commands.draw_view(draw).definition.name;
            if(name=="race-glow/head"||name=="race-glow/tail")last_glow=draw+1;
        }
        require(last_glow>=rival_glows+traffic_glows,"missing persistent actor light slots");
        auto draw=last_glow-traffic_glows;
        for(unsigned i=0;i<actors.size();++i) {
            const auto& actor=actors[i];auto& visual=visuals[i];
            require(drive_place_instances(visual.base,actor.car,actor.setup.spec,visual.scene.instances,&actor.animation),"traffic placement refused");
            const auto lights=place_original_car_glows(visual.anchors,visual.scene,camera,1280,actor.driver.pedals.brake,true);
            const auto tail_first=2*(std::max(std::size_t(1),visual.anchors[0].size())+
                                     std::max(std::size_t(1),visual.anchors[1].size()));
            for(unsigned lamp=0;lamp<lights.size();++lamp,++draw) {
                const auto view=packet.commands.draw_view(draw);
                const char* texture=lights[lamp].texture==0?"race-glow/original-head":
                    lights[lamp].texture==1?"race-glow/original-tail":"race-glow/original-head-outer";
                require(packet.commands.resource_table().resources.at(view.definition.texture_resource).name==texture,
                    "service brake changed authored traffic glow texture");
                const float alpha=actor.active?float(lights[lamp].rgba[3])/255:0;
                if(view.material.alpha[0]!=alpha) {
                    std::cerr<<"event="<<event<<" frame="<<frame<<" actor="<<i<<" lamp="<<lamp
                        <<" brake="<<actor.driver.pedals.brake<<" actual_alpha="<<view.material.alpha[0]
                        <<" analog_alpha="<<alpha<<'\n';
                    require(false,"GPU traffic light lost analog service brake");
                }
                if(actor.active&&lamp>=tail_first&&alpha>0&&actor.driver.pedals.brake>0&&
                   actor.driver.pedals.brake<=.05000000074505806F)++visible_weak_tails;
                ++compared;
            }
        }
    }
    require(weak_rigid>0&&rendered>1&&visible_weak_tails>0,"missing visible natural weak-brake regression");
    if(event==6)require(weak_kinematic>0,"missing natural kinematic weak brake");
    std::cout<<"PASS traffic lights event="<<event<<" frames="<<frames<<" rendered="<<rendered
        <<" weak_kinematic="<<weak_kinematic<<" weak_rigid="<<weak_rigid
        <<" visible_weak_tails="<<visible_weak_tails<<" compared="<<compared<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL traffic lights retail: "<<e.what()<<'\n';return 1;}}
