#include "nfsu/race_hud.h"
#include "nfsu/race_setup.h"
#include "nfsu/player_powertrain.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <cstring>
using namespace nfsu;
void check(bool ok,const char* label) { if(!ok)throw std::runtime_error(label); }
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
        VehicleUpgradeLevels stock,levels;levels.induction=levels.nitrous=1;
        VehicleSpec before,after;
        const auto global=std::filesystem::path(argv[1])/"Global/GLOBALB.BUN";
        check(load_race_vehicle_spec(global,"SKYLINE",stock,before,error),error.c_str());
        check(load_race_vehicle_spec(global,"SKYLINE",levels,after,error),error.c_str());
        check(after.nitrous.installed&&after.nitrous.capacity_seconds>0&&after.nitrous.rate>0,"Skyline nitrous installed");
        check(after.nitrous.count==2,"level one selects original two-bar NOS indicator");
        check(after.handling.torque!=before.handling.torque,"Skyline turbo changes original torque curve");
        check(after.handling.mass==before.handling.mass&&after.handling.gear_ratios==before.handling.gear_ratios,
              "boost upgrades preserve mass and gearbox");
        RaceHudAtlas atlas;check(load_race_hud_atlas(argv[1],atlas,error,true,true),error.c_str());
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
