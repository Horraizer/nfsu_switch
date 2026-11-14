// Camera-only pins refreshed after native chase surge recovery.
// Full stock/upgraded replays retain all noncamera states and physics.
// Native camera fixtures remain the independent behavior oracle.
#include "nfsu/race_setup.h"
#include "nfsu/handling_telemetry.h"
#include "nfsu/race_events.h"
#include "nfsu/part_placement.h"
#include "nfsu/arg_path.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace nfsu;
namespace {
using Record = std::array<unsigned char,kVehicleRecordSize>;
void require(bool pass, const std::string& label) {
    if (!pass) throw std::runtime_error("FAIL: " + label);
}
float f32(const Record& record, std::size_t offset) {
    std::uint32_t bits = 0;
    for (unsigned i=0;i<4;++i) bits |= std::uint32_t(record[offset+i])<<(i*8);
    float value; std::memcpy(&value,&bits,4); return value;
}
Record read_record(const std::filesystem::path& global, const char* name) {
    const auto table=load_car_type_table(global); const auto index=find_car_type(table,name);
    require(table.ok() && index!=kNoCarType,"retail car table");
    Record record{}; std::ifstream in(global,std::ios::binary);
    in.seekg(table.records[index].offset); in.read(reinterpret_cast<char*>(record.data()),record.size());
    require(bool(in),"retail original record read"); return record;
}
VehicleSpec setup(const Record& record, const VehicleUpgradeLevels& levels) {
    VehicleSpec spec; std::string error;
    require(make_race_vehicle_spec(record.data(),record.size(),levels,spec,error),error);
    return spec;
}
constexpr float runtime_aero_pins[20][3]={
#include "race_setup_aero_native.inc"
};
void check_aero_setup(const Record& original,unsigned car=0) {
    for(int weight=0;weight<=3;++weight)for(int suspension=0;suspension<=3;++suspension)
    for(int tyres=0;tyres<=3;++tyres)for(int visual=0;visual<2;++visual) {
        VehicleUpgradeLevels levels;levels.weight=weight;levels.suspension=suspension;levels.tyres=tyres;
        levels.aero_a=visual?3:0;levels.aero_b=visual?1:0;
        const auto spec=setup(original,levels);Record encoded{};
        encode_vehicle_handling(spec.handling,encoded.data(),encoded.size());
        const float fields[]{spec.handling.downforce_front,spec.handling.downforce_rear,spec.handling.aero_preload};
        const auto& expected=runtime_aero_pins[car*10+weight+suspension+tyres];
        for(unsigned i=0;i<3;++i)
            require(fields[i]==expected[i]&&f32(encoded,0x374+4*i)==expected[i],
                    "original57F2B0 continuous aero, independent of visual-part levels");
    }
    const auto spec=setup(original,{});auto omitted=spec;
    omitted.handling.downforce_front=omitted.handling.downforce_rear=omitted.handling.aero_preload=0;
    VehicleModel full(spec),without(omitted);VehicleState x,y;VehiclePose pose;pose.speed=20;
    VehicleTelemetry tx,ty;VehicleInput input;
    require(full.reset(x,pose)&&without.reset(y,pose)&&
            full.step(x,input,VehicleFlatGround{},tx)&&without.step(y,input,VehicleFlatGround{},ty),
            "runtime aero force callbacks complete");
    require(x.prepared_force.z!=y.prepared_force.z&&x.aero_preloads!=y.aero_preloads,
            "runtime aero reaches both model force and moving preload");
    std::cout<<"original continuous aero128 cases passed\n";
}
constexpr float runtime_inertia_pins[8][4]={
#include "race_inertia_native.inc"
};
void check_runtime_inertia(const Record& original) {
    for(int weight=0;weight<4;++weight)for(int item=0;item<2;++item) {
        VehicleUpgradeLevels levels;levels.weight=weight;levels.mass_item=item!=0;
        const auto h=setup(original,levels).handling;
        const auto& pin=runtime_inertia_pins[2*weight+item];
        require(h.mass==pin[0]&&h.inertia==std::array<float,3>{pin[1],pin[2],pin[3]},
                "original runtime mass/inertia producer");
    }
}
void check_levels(const Record& original) {
    VehicleHandling stock; VehicleTuning tuning;
    std::vector<VehicleHandlingIssue> hi; std::vector<VehicleTuningIssue> ti;
    require(decode_vehicle_handling(original.data(),original.size(),stock,hi),"stock handling");
    require(decode_vehicle_tuning(original.data(),original.size(),tuning,ti),"retail tuning tables");
    const auto zero=setup(original,{});
    auto expected_zero=stock_240sx_handling();
    expected_zero.inertia={runtime_inertia_pins[0][1],runtime_inertia_pins[0][2],runtime_inertia_pins[0][3]};
    require(compare_vehicle_handling(zero.handling,expected_zero)==kVehicleRecordSize,
            "zero-level runtime inertia with all other stock field bits preserved");
    using Member = int VehicleUpgradeLevels::*;
    const Member fields[] = {&VehicleUpgradeLevels::weight,&VehicleUpgradeLevels::suspension,
        &VehicleUpgradeLevels::engine_a,&VehicleUpgradeLevels::engine_b,&VehicleUpgradeLevels::induction,
        &VehicleUpgradeLevels::gearbox,&VehicleUpgradeLevels::tyres,&VehicleUpgradeLevels::nitrous,
        &VehicleUpgradeLevels::aero_a,&VehicleUpgradeLevels::aero_b,&VehicleUpgradeLevels::grip};
    for (auto field : fields) for (int level=1;level<=3;++level) {
        VehicleUpgradeLevels levels; levels.*field=level;
        const auto spec=setup(original,levels); const auto& h=spec.handling;
        const auto n=static_cast<std::size_t>(level-1);
        Record applied{};
        require(apply_vehicle_tuning(original.data(),original.size(),levels,applied.data(),applied.size(),ti),
                "apply isolated level");
        const auto& aero=runtime_aero_pins[levels.weight+levels.suspension+levels.tyres];
        require(h.downforce_front==aero[0]&&h.downforce_rear==aero[1]&&h.aero_preload==aero[2],
                "normal live setup uses original continuous aero after raw tuning");
        if (field==&VehicleUpgradeLevels::weight) {
            require(h.mass==float(double(stock.mass)*tuning.weight[n].mass_scale),"weight mass");
            require(h.centre_of_mass[0]==float(double(stock.centre_of_mass[0])+
                    tuning.weight[n].centre_of_mass_longitudinal_delta),"weight CG");
            const auto& inertia=runtime_inertia_pins[2*level];
            require(h.inertia==std::array<float,3>{inertia[1],inertia[2],inertia[3]},"original runtime weight inertia");
            require(f32(applied,kVehicleYawInertiaScaleOffset)==tuning.weight[n].yaw_inertia_scale,
                    "decoded yaw scale (unused by solver)");
        } else if (field==&VehicleUpgradeLevels::suspension) {
            for (std::size_t a=0;a<2;++a) {
                const auto& s=h.struts[a]; const auto& t=tuning.suspension[n].axles[a];
                require(s.progressive==t.progressive && s.spring==t.spring && s.damping_bump==t.damping_bump &&
                    s.damping_rebound==t.damping_rebound && s.anti_roll==t.anti_roll &&
                    s.bump_limit==t.bump_limit && s.droop_limit==t.droop_limit,"suspension fields");
            }
            require(h.centre_of_mass[2]==float(double(stock.centre_of_mass[2])-
                tuning.suspension[n].centre_of_mass_vertical_drop),"suspension CG drop");
            for (std::size_t w=0;w<4;++w) require(h.wheels[w].vertical==float(double(stock.wheels[w].vertical)+
                (w<2 ? tuning.suspension[n].front_wheel_vertical_rise : tuning.suspension[n].rear_wheel_vertical_rise)),
                "suspension wheel rise");
        } else if (field==&VehicleUpgradeLevels::engine_a || field==&VehicleUpgradeLevels::engine_b) {
            const auto& e=field==&VehicleUpgradeLevels::engine_a ? tuning.engine_a[n] : tuning.engine_b[n];
            require(h.idle_rpm==float(double(stock.idle_rpm)+e.idle_rpm_delta) &&
                h.redline_rpm==float(double(stock.redline_rpm)+e.redline_rpm_delta) &&
                h.max_rpm==float(double(stock.max_rpm)+e.max_rpm_delta),"engine rpm fields");
            for (std::size_t i=0;i<9;++i) require(h.torque[i]==float(double(stock.torque[i])+e.torque_delta[i]),"engine torque");
            for (std::size_t i=0;i<3;++i) require(h.engine_brake[i]==float(double(stock.engine_brake[i])+e.engine_brake_delta[i]),"engine brake");
        } else if (field==&VehicleUpgradeLevels::induction) {
            for (std::size_t i=0;i<9;++i) require(h.torque[i]==float(double(stock.torque[i])+tuning.induction[n].torque[i]),"induction torque");
        } else if (field==&VehicleUpgradeLevels::gearbox) {
            const auto& g=tuning.gearbox[n];
            require(h.gear_ratios==g.gear_ratios && h.final_drive_rear==g.final_drive_rear &&
                h.final_drive_front==g.final_drive_front && h.shift_fraction==g.shift_fraction &&
                h.rear_drive_fraction==g.rear_drive_fraction &&
                h.forward_gears==vehicle_derived_forward_gears(g.gear_ratios),"gearbox fields");
        } else if (field==&VehicleUpgradeLevels::tyres) {
            for (std::size_t a=0;a<2;++a) {
                const auto& t=tuning.tyres[n].axles[a]; const auto& s=h.tyres[a];
                require(s.slip_scale==t.slip_scale && s.lateral_load_scale==t.lateral_load_scale &&
                    s.lateral_scale==t.lateral_scale && s.static_friction==t.static_friction &&
                    s.sliding_friction==t.sliding_friction,"tyre fields (three lateral scales unused)");
            }
        } else {
            auto expected=zero.handling;
            if(field==&VehicleUpgradeLevels::grip) {
                // The tuning write at raw384/388 aliases selector1 service
                // and parking setup. Check bytes AND actual brake response.
                expected.service_brake=f32(applied,kVehicleServiceBrakeOffset);
                expected.parking_brake=f32(applied,kVehicleParkingBrakeOffset);
                expected.brake_balance=f32(applied,kVehicleBrakeBalanceOffset);
                auto without=spec;
                without.handling.service_brake=zero.handling.service_brake;
                without.handling.parking_brake=zero.handling.parking_brake;
                without.handling.brake_balance=zero.handling.brake_balance;
                for(const bool parking:{false,true}) {
                    VehicleModel selected(spec),control(without);VehicleState a,b;
                    VehiclePose pose;pose.speed=20;VehicleInput input;
                    input.brake=parking?0:.5;input.handbrake=parking;
                    VehicleTelemetry ta,tb;
                    require(selected.reset(a,pose)&&control.reset(b,pose)&&
                        selected.step(a,input,VehicleFlatGround{},ta)&&
                        control.step(b,input,VehicleFlatGround{},tb),"decoded brake alias step");
                    const bool changed=parking?
                        expected.parking_brake!=zero.handling.parking_brake:
                        expected.service_brake!=zero.handling.service_brake;
                    if(changed)require(ta.wheel_prepared_torque!=tb.wheel_prepared_torque||
                        ta.wheel_prepared_force_long!=tb.wheel_prepared_force_long,
                        "selected brake alias changes actual prepared wheel response");
                }
            }
            require(compare_vehicle_handling(h,expected)==kVehicleRecordSize,
                    "isolated aero/brake alias changes preserve unrelated handling fields, offset " +
                    std::to_string(compare_vehicle_handling(h,expected)));
            if (field==&VehicleUpgradeLevels::nitrous) {
                require(f32(applied,kVehicleNitrousBlockOffset)==tuning.nitrous[n].capacity &&
                    f32(applied,kVehicleNitrousBlockOffset+12)==tuning.nitrous[n].rate,"nitrous decode only");
            } else for (std::size_t i=0;i<3;++i) {
                if (field==&VehicleUpgradeLevels::grip)
                    require(f32(applied,kVehicleGripBlockOffset+4*i)==tuning.grip[n].values[i],"grip decode only");
                else {
                    const auto delta=field==&VehicleUpgradeLevels::aero_a ? tuning.aero_a[n].downforce_delta[i] : tuning.aero_b[n].downforce_delta[i];
                    require(f32(applied,kVehicleDownforceOffset+4*i)==float(double(f32(original,kVehicleDownforceOffset+4*i))+delta),"original aero tuning addition");
                }
            }
        }
    }
}
void check_sessions(const Record& record) {
    std::string error; VehicleUpgradeLevels levels;
    require(parse_performance_levels("engine-a=1,gearbox=1,weight=1,suspension=1,tyres=1",levels,error),error);
    const auto upgraded=setup(record,levels);
    const auto saved=record;
    require(compare_vehicle_handling(upgraded.handling,setup(record,levels).handling)==kVehicleRecordSize &&
            saved==record,"repeated setup uses original bytes, no cumulative application");
    VehicleTrackCourse course; require(build_vehicle_proving_ground(course,error),error);
    auto runtime_stock=stock_240sx_vehicle_spec();
    runtime_stock.handling.inertia={runtime_inertia_pins[0][1],runtime_inertia_pins[0][2],runtime_inertia_pins[0][3]};
    RaceSession legacy(course,runtime_stock),zero(course,setup(record,{})),a(course,upgraded),b(course,upgraded);
    require(legacy.start(error) && zero.start(error) && a.start(error) && b.start(error),error);
    Sha256 hash; bool changed=false;
    for (unsigned tick=0;tick<1000;++tick) {
        const auto pad=race_input_frame({1,tick<180 ? 0U : gamepad_button_bit(GamepadButton::trigger_right),0},tick);
        require(legacy.advance(pad,error) && zero.advance(pad,error) && a.advance(pad,error) && b.advance(pad,error),error);
        require(legacy.record()==zero.record(),"original-inertia stock/explicit zero replay bytes");
        require(a.record()==b.record(),"upgraded replay bytes deterministic");
        changed |= a.record()!=legacy.record(); hash.update(a.record().data(),a.record().size());
    }
    require(changed,"upgraded replay differs for documented setup");
    // Portable regression after native selector1 phase recovery; original
    // operation fixtures stay separate. Runtime aero now uses57F2B0; the
    // native aero fixture pins that producer independently of this replay.
    const auto synthetic_hash=sha256_hex(hash.finish());
    require(synthetic_hash=="6554B0A5C6EB55B31C14E52CEB1E918F77C70063A450BC8D25CEA64FC9C5245A","synthetic upgraded replay pin: "+synthetic_hash);
    std::cout << "synthetic-upgrade-sha256 " << synthetic_hash << '\n';
    // Reset reuses the model and shifts; it must not apply tables again.
    const auto before=a.spec().handling;
    auto reset=race_input_frame({1,gamepad_button_bit(GamepadButton::select),0},1000);
    require(a.advance(reset,error) && a.telemetry().resets==1,error);
    require(compare_vehicle_handling(a.spec().handling,before)==kVehicleRecordSize,"reset leaves setup fixed");
    std::FILE* file=std::tmpfile(); require(file!=nullptr,"telemetry temporary stream");
    HandlingTelemetryLog log(file);
    reset.sample_clock_available=true; reset.sample_time_us=100; reset.display_frame=27;
    for (std::size_t i=0;i<HandlingTelemetryLog::kMaxRows*6;++i)
        require(log.write(a,reset,125,true),"telemetry stream");
    require(log.rows()==HandlingTelemetryLog::kMaxRows,"telemetry row cap");
    std::rewind(file); char line[4096]; require(std::fgets(line,sizeof line,file)!=nullptr,"telemetry header");
    require(std::fgets(line,sizeof line,file)!=nullptr,"telemetry row");
    require(std::string(line).find(",27,")!=std::string::npos && std::string(line).find(",25,")!=std::string::npos,
            "host sample age and display frame reach telemetry");
    std::fclose(file);
    // A wheel off the ground has no measured road slip. Raw input and
    // sensor age are also unavailable for a disconnected replay pad.
    VehicleTrackCourse airborne_course=course; airborne_course.start.ground.z+=20;    // A supplied moving spawn bypasses loading-time terrain settling.    // A stationary20m drop now lands during prepare_grid, as it should.    airborne_course.start.speed=1;
    RaceSession airborne(airborne_course);
    require(airborne.start(error),"airborne telemetry session");
    while (race_car_held(airborne.timing())) require(airborne.advance({},error),error);
    require(airborne.advance({},error),error);
    require(!airborne.telemetry().model.wheels[0].contact,"airborne wheel has no ground contact");
    file=std::tmpfile(); require(file!=nullptr,"airborne telemetry stream");
    HandlingTelemetryLog airborne_log(file); require(airborne_log.write(airborne,{}),"airborne log");
    std::rewind(file); require(std::fgets(line,sizeof line,file) && std::fgets(line,sizeof line,file),"airborne row");
    require(std::string(line).find(",0,unavailable,unavailable,unavailable,")!=std::string::npos &&
            std::string(line).find(",0,unavailable,unavailable,0,0,0")!=std::string::npos,
            "disconnected raw/sample age and airborne slip are unavailable");
    std::fclose(file);
    const VehicleModel stock(setup(record,{})),tuned(upgraded);
    require(stock.upshift_rpm()!=tuned.upshift_rpm(),"upgraded shift thresholds rebuilt");
    VehicleFlatGround ground; VehicleState s,t; require(stock.reset(s,{}) && tuned.reset(t,{}),"flat reset");
    VehicleTelemetry st,tt; VehicleTelemetryHash sh,th;
    std::string transitions;
    int sg=s.gear,tg=t.gear;
    for (unsigned i=0;i<3600;++i) {
        require(stock.step(s,{1,0,0},ground,st) && tuned.step(t,{1,0,0},ground,tt),"flat acceleration");
        sh.add(st); th.add(tt);
        if (s.gear!=sg) { transitions+="stock:"+std::to_string(i+1)+":"+std::to_string(s.gear)+";"; sg=s.gear; }
        if (t.gear!=tg) { transitions+="upgrade:"+std::to_string(i+1)+":"+std::to_string(t.gear)+";"; tg=t.gear; }
    }
    require(sg>kVehicleGearFirst && tg>kVehicleGearFirst,"automatic gear transitions");
    const auto stock_hash=sha256_hex(sh.finish()), upgraded_hash=sha256_hex(th.finish());
    std::cout<<"observed acceleration-stock "<<stock_hash<<"\nobserved acceleration-upgrade "
             <<upgraded_hash<<"\nobserved transitions "<<transitions<<'\n';
    require(stock_hash=="0FA3F61E2B02BEFC118DA3DF3A0755A8025334821F7CB68EF108EFA4D0840941","stock acceleration pin");
    require(upgraded_hash=="F03029C8D5783BEE116D3F7B188C781093E54368126CAAE953C4AAA140A01780","upgraded acceleration pin: "+upgraded_hash);
    require(transitions=="upgrade:488:3;stock:547:3;upgrade:1000:4;stock:1158:4;upgrade:1831:5;stock:2106:5;upgrade:3075:6;stock:3465:6;",
            "stock and upgraded automatic shift transitions: "+transitions);
    std::cout << "acceleration-stock " << stock_hash << "\nacceleration-upgrade " << upgraded_hash
              << "\ntransitions " << transitions << '\n';
}
void check_retail(const std::filesystem::path& root) {
    const auto global=root/"Global/GLOBALB.BUN";
    const auto record=read_record(global,"240SX");check_aero_setup(record);check_levels(record);check_sessions(record);
    const auto civic=read_record(global,"CIVIC");check_aero_setup(civic,1);
    VehicleUpgradeLevels drift_levels;drift_levels.weight=drift_levels.tyres=drift_levels.suspension=1;
    drift_levels.mass_item=true;drift_levels.aero_a=3;drift_levels.aero_b=1;
    const auto ordinary=setup(civic,drift_levels);VehicleSpec drift;std::string error;
    require(make_race_vehicle_spec(civic.data(),civic.size(),drift_levels,drift,error,true,1),error);
    require(drift.handling.mass==1.1761059761047363F&&drift.handling.inertia==ordinary.handling.inertia,
            "original57F2B0 drift mass blend keeps pre-blend inertia with the unique item");
    require(drift.handling.downforce_front==.14166666567325592F&&
            drift.handling.downforce_rear==.15458333492279053F&&
            drift.handling.aero_preload==.021166665479540825F,
            "original57F2B0 drift aero ignores visual-part levels");
    const auto second=read_record(global,"RX7"); VehicleHandling raw; std::vector<VehicleHandlingIssue> issues;
    require(decode_vehicle_handling(second.data(),second.size(),raw,issues),"RX7 stock decode");
    const auto stock=setup(second,{}); bool nonzero=false;
    for (std::size_t i=0;i<9;++i) {
        const auto induction=f32(second,kVehicleInductionTorqueOffset+4*i); nonzero |= induction!=0;
        require(stock.handling.torque[i]==float(double(raw.torque[i])+induction),"RX7 stock second curve added once");
    }
    require(nonzero,"retail second curve really nonzero");
}
}
int main(int argc,char** argv) {
    try {
        std::string error; require(check_arguments(argc,argv,1,error),error);
        VehicleUpgradeLevels levels;
        for (const auto* bad : {"","engine-a=4","weight=-1","gearbox=01","foo=1","weight=1,weight=2", "weight=1,", "mass-item=1"})
            require(!parse_performance_levels(bad,levels,error),std::string("CLI refusal: ")+bad);
        require(parse_performance_levels("engine-a=1,tyres=3",levels,error),error);
        require(parse_performance_levels(performance_levels_text(levels),levels,error),"canonical CLI roundtrip");
        Record original{}; encode_vehicle_handling(stock_240sx_handling(),original.data(),original.size());
        encode_vehicle_tuning(stock_240sx_tuning(),original.data(),original.size());
        const auto saved=original;check_runtime_inertia(original);check_aero_setup(original);
        if(argc==2&&std::string(argv[1])=="--aero-only")return 0;
        check_levels(original);
        if(argc==2&&std::string(argv[1])=="--aliases-only") {
            std::cout<<"SUMMARY isolated tuning and brake aliases failures=0\n";return 0;
        }
        check_sessions(original);
        require(original==saved,"original record never mutated");
        for(const auto member:{&VehicleUpgradeLevels::aero_a,&VehicleUpgradeLevels::aero_b}) {
            levels={};levels.*member=4;VehicleSpec refused=stock_240sx_vehicle_spec();
            require(!make_race_vehicle_spec(original.data(),original.size(),levels,refused,error),
                    "invalid visual aero levels still refused by runtime setup");
        }
        levels={};levels.weight=4; VehicleSpec untouched=stock_240sx_vehicle_spec();
        require(!make_race_vehicle_spec(original.data(),original.size(),levels,untouched,error),"invalid API levels refused");
        require(compare_vehicle_handling(untouched.handling,stock_240sx_handling())==kVehicleRecordSize,"failed setup leaves output untouched");
        if (argc==2) check_retail(argument_path(argv[1]));
        else require(argc==1,"unexpected arguments");
        std::cout << "performance levels tests: 0 failures\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
