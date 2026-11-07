#include "nfsu/intro_grid.h"
#include "nfsu/race_hud.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/arg_path.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace nfsu;
void need(bool ok,const std::string& s){if(!ok)throw std::runtime_error(s);}
int main(int argc,char** argv){try {
    need(argc==3,"usage: nfsu_race_powertrain_check retail-root fresh-directory");
    std::filesystem::path root,out;std::string error;
    need(argument_to_path(argv[1],root,error)&&argument_to_path(argv[2],out,error),error);
    need(!std::filesystem::exists(out),"output exists");std::filesystem::create_directories(out);
    IntroGrid grid;need(load_intro_grid(root,grid,error),error);
    const auto& actor=grid.actors[1];need(actor.config.role==Event0Role::human,"player identity");
    const auto& spec=actor.placement_spec;const auto& h=spec.handling;
    std::array<unsigned char,kVehicleRecordSize> record{};encode_vehicle_handling(h,record.data(),record.size());
    std::ofstream fields(out/"handling.bin",std::ios::binary);fields.write(reinterpret_cast<const char*>(record.data()),record.size());fields.close();
    std::ofstream samples(out/"engine.csv");samples<<std::setprecision(17);
    samples<<"rpm,gear,throttle,multiplier,cutoff,feedback,derivative,next_rad_s,published_rpm\n";
    for(float rpm:{h.idle_rpm,2500.F,4500.F,6500.F,h.max_rpm-50.F,h.max_rpm})
    for(int gear:{kVehicleGearNeutral,kVehicleGearFirst,3})for(float throttle:{0.F,.5F,1.F})
    for(float cutoff:{0.F,1.F}) {
        PlayerEngineState e;player_engine_reset(e,rpm);e.cutoff_until=cutoff;e.throttle_multiplier=.7F;
        e.feedback_torque=-.05F;
        const auto crank=player_engine_torque(h,e,gear,throttle);
        const auto derivative=player_engine_acceleration(h,e,crank);
        player_engine_integrate(h,e,crank,1.F/240);
        samples<<rpm<<','<<gear<<','<<throttle<<",0.7,"<<cutoff<<",-0.05,"<<derivative<<','<<e.angular_speed<<','<<player_engine_publish_rpm(h,e,rpm,1.F/60)<<'\n';
    }
    std::ofstream speeds(out/"speed.csv");speeds<<std::setprecision(17)<<"published,mph\n";
    for(float speed:{0.F,1.F,-1.F,12.F,20.F,-20.F,44.702728271484375F,44.70273208618164F,99.F,150.F}) {
        VehicleState c;c.velocity={999,999,999};c.owner_speeds.magnitude=speed;RaceHudFrame f;
        build_race_hud_frame_for_car(c,h.max_rpm,h.redline_rpm,1280,720,f);
        speeds<<speed<<','<<f.readout.mph<<'\n';
    }
    std::ofstream cuts(out/"cutoff.csv");cuts<<std::setprecision(17)<<"rad_s,gear,ticks,seconds,before,after\n";
    for(int ticks:{3999,4000,4001})for(int gear:{1,2,3})for(float before:{0.F,1.F})
    for(float rpm:{h.max_rpm-50.F,h.max_rpm}) {
        PlayerEngineState e;player_engine_reset(e,rpm);e.cutoff_until=before;
        const float seconds=float(ticks)*.00025F;
        player_engine_cutoff_update(h,gear,e,seconds);
        cuts<<e.angular_speed<<','<<gear<<','<<ticks<<','<<seconds<<','<<before<<','<<e.cutoff_until<<'\n';
    }
    for(int scenario=0;scenario<3;++scenario){
        RaceSession session(grid.course,spec,RaceSteering::recovered_predicate1,actor.hull);
        need(session.start(error,&actor.pose),error);RaceRouteDriver driver(RaceDrivingLine::event1_forward);
        std::ofstream csv(out/("run-"+std::to_string(scenario)+".csv"));csv<<std::setprecision(17);
        csv<<"frame,held,gas,applied,rpm,physical_rpm,kmh,gear,mode,clutch,cutoff,wheel_rpm,boost,torque,reset\n";
        int drive=0;double max_speed=0,peak=0;
        for(int frame=0;frame<900;++frame){
            const bool held=race_car_held(session.timing());auto pad=driver.sample(session);
            pad.pads[0].buttons=0;
            const bool gas=held?(frame>0&&frame<90):!(scenario==1&&drive>=120&&drive<240);
            if(gas)pad.pads[0].buttons|=gamepad_button_bit(GamepadButton::trigger_right);
            if(scenario==2&&!held&&drive>=120&&drive<300)pad.pads[0].buttons|=gamepad_button_bit(GamepadButton::shoulder_left);
            need(session.advance(pad,error),error);
            const auto& c=session.car();const auto& t=session.telemetry();
            const double speed=vehicle_sqrt(c.velocity.x*c.velocity.x+c.velocity.y*c.velocity.y)*3.6;
            csv<<frame<<','<<held<<','<<gas<<','<<t.model.throttle<<','<<c.engine_rpm<<','<<c.engine.angular_speed*kPlayerRadSToRpm<<','<<speed<<','<<c.gear<<','<<c.transmission.mode<<','<<c.transmission.clutch<<','<<c.engine.cutoff_until<<','<<c.wheel_rpm<<','<<session.nitrous().active<<','<<t.model.engine_torque<<','<<t.resets<<'\n';
            max_speed=std::max(max_speed,speed);peak=std::max(peak,c.engine_rpm);if(!held)++drive;
        }
        need(bool(csv),"telemetry write");std::cout<<"RUN "<<scenario<<" peak_rpm "<<peak<<" max_kmh "<<max_speed<<" resets "<<session.telemetry().resets<<'\n';
    }
    need(bool(samples),"sample write");std::cout<<"PASS UG0 "<<actor.config.visual.car<<" selected tuning; controlled inputs, road steering; not original full trajectory parity\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
