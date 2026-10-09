#include "nfsu/race_hud.h"
#include "nfsu/race_setup.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/freeride_skyline.h"
#include "nfsu/posix_file_system.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <cstring>
using namespace nfsu;
void check(bool ok,const std::string& label) { if(!ok)throw std::runtime_error(label); }
// Mirror the prepared dynamic tail invariants enforced by deko_renderer
// open/update, using actual scene definitions across real input transitions.
void check_nitrous_gpu_package(const std::filesystem::path& root) {
    std::cout<<std::unitbuf;
    FreeDriveSkyline dress;std::string error;
    check(resolve_free_drive_skyline(root,dress,error),error);
    for(const unsigned event:{1U,8U}) {
        std::cout<<"event="<<event<<" loading actual package\n";
        RaceGpuScene gpu;gpu.set_race_visuals(true);gpu.set_wet_road_reflections(true);
        check(gpu.set_original_draw_distance(true),"original distance selection");
        check(gpu.load(posix_file_system(),root,error,nullptr,nullptr,false,true,true,
                       &dress.car,&dress.dress,event,true),error);
        RaceRun run;check(run.set_event(event,error),error);
        check(run.set_free_drive(true,error)&&run.set_car("SKYLINE",error)&&
              run.set_performance_levels(dress.performance,error),error);
        check(run.prepare_metadata(root,error)&&run.start(root,gpu.track(),error),error);
        RaceGpuFrame first;check(gpu.frame(run.session(),first,error),error);
        check(validate_recorded_commands(first.commands,error),error);
        const auto* package=first.commands.package_identity();
        const auto width=first.commands.width,height=first.commands.height;
        unsigned char clear[4];std::copy(std::begin(first.commands.clear_color),std::end(first.commands.clear_color),clear);
        std::vector<const RecordedDraw*> dynamic;
        for(auto i=first.commands.static_track_draw_count();i<first.commands.draw_count();++i)
            dynamic.push_back(&first.commands.draw_view(i).definition);
        unsigned flame_cells=0,checks=0;
        auto validate=[&](bool lit,const RaceSession* fresh=nullptr) {
            RaceGpuFrame frame;check(gpu.frame(fresh?*fresh:run.session(),frame,error),error);
            const auto& c=frame.commands;check(validate_recorded_commands(c,error),error);
            check(c.generation_valid()&&c.package_identity()==package&&c.width==width&&c.height==height&&
                  std::memcmp(c.clear_color,clear,4)==0,"prepared package identity/viewport changed");
            const auto track=c.static_track_draw_count();
            check(c.draw_count()<=gpu.draw_capacity()&&c.draw_count()-track==dynamic.size(),
                  "NOS activation changed prepared dynamic capacity");
            unsigned flames=0,visible=0;
            for(std::size_t i=0;i<dynamic.size();++i) {
                const auto view=c.draw_view(track+i);
                check(!view.track_instance && &view.definition==dynamic[i],"NOS changed prepared definition address/order");
                if(view.definition.name!="race-nitrous/flame")continue;
                ++flames;
                check(view.definition.first_index%6==0&&view.definition.first_index<=18,"flame cell index");
                if(view.material.alpha[0]>0){++visible;flame_cells|=1U<<(view.definition.first_index/6);}
            }
            check(flames==1&&visible==(lit?1U:0U),"one reserved authored pipe follows actual NOS state");
            ++checks;
        };
        validate(false);std::cout<<"initial inactive package PASS\n";
        unsigned tick=0;
        auto step=[&](std::uint32_t buttons) {
            const auto pad=race_input_frame({1,buttons,0},tick++);
            check(run.simulate(pad,error)&&run.commit(error),error);
        };
        while(race_car_held(run.session().timing())&&tick<300)step(0);
        check(!race_car_held(run.session().timing()),"countdown did not complete within 300 input frames");
        std::cout<<"countdown complete step="<<tick<<"\n";
        const auto gas=gamepad_button_bit(GamepadButton::trigger_right);
        const auto nos=gamepad_button_bit(GamepadButton::shoulder_left);
        step(gas);validate(false);std::cout<<"launch package PASS\n";
        for(unsigned i=0;i<8;++i){step(gas|nos);check(run.session().nitrous().active,"actual L activation");validate(true);}
        std::cout<<"activation/atlas package PASS\n";
        check(flame_cells==15,"all four atlas cells exercised without package change");
        step(gas);check(!run.session().nitrous().active,"actual L release");validate(false);
        step(gas|nos);validate(true);
        for(unsigned i=0;i<140;++i){step(gas|nos);validate(run.session().nitrous().active);}
        std::cout<<"depletion package PASS\n";
        check(run.session().nitrous().remaining==0&&!run.session().nitrous().active,"actual depletion");
        validate(false);
        step(gamepad_button_bit(GamepadButton::select));validate(false);
        RaceRun fresh;check(fresh.set_event(event,error)&&fresh.set_free_drive(true,error)&&
            fresh.set_car("SKYLINE",error)&&fresh.set_performance_levels(dress.performance,error),error);
        check(fresh.prepare_metadata(root,error)&&fresh.start(root,gpu.track(),error),error);
        validate(false,&fresh.session());
        std::cout<<"event="<<event<<" prepared NOS package: "<<checks
                 <<" updates PASS; inactive/active/4cells/release/depletion/reset/re-entry\n";
    }
}

int main(int argc,char** argv) { try {
    if (argc==2 && std::string(argv[1])=="--turbo") {
        float induction,rpm,idle,maximum,effective;bool cutoff;std::uint32_t clock;
        TurboDisplayState t;
        while(std::cin>>induction>>rpm>>idle>>maximum>>effective>>cutoff>>clock
              >>t.mode>>t.value>>t.previous_seconds>>t.decay_seconds>>t.decay_start>>t.crossing_value) {
            t.update(induction,rpm,idle,maximum,effective,cutoff,clock);
            std::cout<<std::setprecision(9)<<t.mode<<' '<<t.value<<' '<<t.previous_seconds<<' '
                     <<t.decay_seconds<<' '<<t.decay_start<<' '<<t.crossing_value<<'\n';
        }
        return 0;
    }
    auto spec=stock_240sx_vehicle_spec();
    spec.nitrous={true,2,0.5F,2};
    PlayerEngineState engine;engine.angular_speed=4000.0F/9.549304F;
    VehicleInput input;input.nitrous_active=true;input.nitrous_clock=20000;
    const auto base=player_engine_torque(spec.handling,engine,kVehicleGearFirst,1);
    const auto boost=player_engine_torque(spec.handling,engine,kVehicleGearFirst,1,&input,spec.nitrous.rate);
    check(boost>base,"active nitrous must add crank torque");
    const auto idle=player_engine_torque(spec.handling,engine,kVehicleGearFirst,0,&input,spec.nitrous.rate);
    check(idle>player_engine_torque(spec.handling,engine,kVehicleGearFirst,0),"original additive term survives throttle release");
    input.nitrous_active=false;
    check(player_engine_torque(spec.handling,engine,kVehicleGearFirst,1,&input,spec.nitrous.rate)==base,
          "inactive torque must be unchanged");
    VehicleTrackCourse course;std::string error;
    check(build_vehicle_proving_ground(course,error),"course");
    RaceSession a(course,spec),b(course,spec);
    check(a.start(error)&&b.start(error),"boost sessions");
    unsigned tick=0;
    while(race_car_held(a.timing())) {
        const auto p=race_input_frame({1,0,0},tick++);
        check(a.advance(p,error)&&b.advance(p,error),"countdown");
        check(a.nitrous().remaining==8000 && !a.nitrous().active,"countdown preserves charge");
    }
    const auto gas=gamepad_button_bit(GamepadButton::trigger_right),nos=gamepad_button_bit(GamepadButton::shoulder_left);
    auto p=race_input_frame({1,gas,0},tick++);check(a.advance(p,error)&&b.advance(p,error),"launch");
    p=race_input_frame({1,gas,0},tick++);auto boosted=p;boosted.pads[0].buttons|=nos;
    check(a.advance(boosted,error)&&b.advance(p,error),"L activation");
    check(a.nitrous().active,"L enables nitrous");
    check(a.physics_telemetry()[0].model.engine_torque>b.physics_telemetry()[0].model.engine_torque,
          "boost must reach actual physics crank torque");
    check(a.car().gear==b.car().gear,"L must not also downshift");
    std::cout<<"crank before="<<b.physics_telemetry()[0].model.engine_torque
             <<" boosted="<<a.physics_telemetry()[0].model.engine_torque<<" Nm\n";
    check(a.advance(boosted,error),"held boost");
    check(a.nitrous().remaining<8000,"held charge consumption");
    check(a.advance(p,error)&&!a.nitrous().active,"release stops boost");
    const auto left=a.nitrous().remaining;
    auto reset=p;reset.pads[0].buttons=gamepad_button_bit(GamepadButton::select);
    check(a.advance(reset,error)&&a.nitrous().remaining==left,"pose reset must not refill nitrous");
    RaceSession fresh(course,spec);check(fresh.start(error)&&fresh.nitrous().remaining==8000,"new session fills charge");
    for(unsigned i=0;i<160;++i)check(a.advance(boosted,error),"depletion");
    check(!a.nitrous().active&&a.nitrous().remaining==0,"empty bottle cannot boost");
    if(argc==2) {
        check_nitrous_gpu_package(std::filesystem::path(argv[1]));
        VehicleUpgradeLevels stock,levels;levels.induction=levels.nitrous=1;
        VehicleSpec before,after;
        const auto global=std::filesystem::path(argv[1])/"Global/GLOBALB.BUN";
        check(load_race_vehicle_spec(global,"SKYLINE",stock,before,error),error);
        check(load_race_vehicle_spec(global,"SKYLINE",levels,after,error),error);
        check(after.nitrous.installed&&after.nitrous.capacity_seconds>0&&after.nitrous.rate>0,"Skyline nitrous installed");
        check(after.nitrous.count==2,"level one selects original two-bar NOS indicator");
        check(after.handling.torque!=before.handling.torque,"Skyline turbo changes original torque curve");
        check(after.handling.mass==before.handling.mass&&after.handling.gear_ratios==before.handling.gear_ratios,
              "boost upgrades preserve mass and gearbox");
        RaceHudAtlas atlas;check(load_race_hud_atlas(argv[1],atlas,error,true,true),error);
        check(atlas.boost_ready&&atlas.lap_ready,"retail boost artwork loads");
        RaceHudFrame empty,full;
        build_race_hud_frame_for_car(a.car(),spec.handling.max_rpm,spec.handling.redline_rpm,1280,720,empty);full=empty;
        append_race_boost_hud(atlas,{8000,0,false,false},true,0,1280,720,empty,after.nitrous.count);
        append_race_boost_hud(atlas,{8000,8000,false,true},true,0,1280,720,full,after.nitrous.count);
        check(empty.quads==full.quads&&std::memcmp(empty.vertices.data(),full.vertices.data(),sizeof(empty.vertices))!=0,
              "charge must visibly change original NOS artwork");
        std::cout<<"Skyline capacity="<<after.nitrous.capacity_seconds<<" s count="<<after.nitrous.count
                 <<" rate="<<after.nitrous.rate<<" turbo max="<<*std::max_element(after.induction.begin(),after.induction.end())<<'\n';
    }
    std::cout<<"PASS: integrated boost, resource lifecycle and HUD\n";return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;} }
