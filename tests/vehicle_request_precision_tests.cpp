#include "nfsu/player_powertrain.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
using namespace nfsu;
struct SetupWitness {
    const char* name;
    float idle,redline,maximum;
    unsigned forward_gears;
    std::array<float,kVehicleGearSlots> ratios;
    std::array<float,kVehicleTorquePoints> torque;
    std::array<float,kVehicleGearSlots> original_up,original_down;
};
struct RequestWitness {
    const char* name;
    unsigned setup;
    int gear;
    float engine_rad,up_offset,down_offset;
    int original_request;
};
#include "request_precision_native.inc"
int failures=0;
void check(bool pass,const char* label) {
    if(!pass){std::cerr<<"FAIL: "<<label<<'\n';++failures;}
}
bool same_bits(float a,float b) {return std::memcmp(&a,&b,sizeof(a))==0;}
VehicleSpec spec_for(const SetupWitness& p) {
    auto spec=stock_240sx_vehicle_spec();auto& h=spec.handling;
    h.idle_rpm=p.idle;h.redline_rpm=p.redline;h.max_rpm=p.maximum;
    h.forward_gears=p.forward_gears;h.gear_ratios=p.ratios;h.torque=p.torque;
    return spec;
}
void producer_cases() {
    unsigned slots=0;
    for(const auto& p:setups) {
        const auto spec=spec_for(p);const VehicleModel model(spec);
        check(model.valid(),"selected/tuned/directed producer setup remains valid");
        std::array<float,kVehicleGearSlots> up{},down{};
        check(player_request_shift_tables(spec.handling,up,down),"original forward table builder completes");
        const unsigned top=kVehicleGearFirst+p.forward_gears-1;
        //46D43E/447 scales only the unused neutral slot after46D1C0.
        // These pins cover forward requests; do not assert neutral policy.
        for(unsigned g=kVehicleGearFirst;g<=top;++g) {
            const bool match=same_bits(up[g],p.original_up[g])&&same_bits(down[g],p.original_down[g]);
            if(!match)std::cerr<<"producer "<<p.name<<" slot "<<g<<'\n';
            check(match,"bit-exact original forward up/down stores");++slots;
        }
    }
    check(slots>0,"forward producer coverage is nonempty");
}
void request_cases() {
    unsigned changes=0,holds=0;
    for(const auto& p:requests) {
        const VehicleModel model(spec_for(setups[p.setup]));VehicleState state;
        check(model.reset(state,{}),"request state reset");
        state.gear=p.gear;state.transmission.requested_gear=p.gear;
        state.transmission.mode=3;state.transmission.clutch=1.0F;
        state.transmission.up_offset=p.up_offset;state.transmission.down_offset=p.down_offset;
        state.engine.angular_speed=p.engine_rad;state.engine_rpm=1000;
        for(auto& w:state.wheels){w.contact=true;w.prepared_slip=0.0F;w.spin=0.0;}
        VehicleInput input;input.throttle=1;VehicleTelemetry t;
        check(model.step(state,input,VehicleFlatGround{},t),"request operation completes");
        if(state.transmission.requested_gear!=p.original_request)
            std::cerr<<"request "<<p.name<<" expected "<<p.original_request
                     <<" actual "<<state.transmission.requested_gear<<'\n';
        check(state.transmission.requested_gear==p.original_request,"original rounded/inclusive request");
        changes+=p.original_request!=p.gear?1U:0U;holds+=p.original_request==p.gear?1U:0U;
    }
    check(changes>0&&holds>0,"rounding neighbours exercise changed and held requests");
}
void compatibility_case() {
    const auto spec=spec_for(setups[5]);const VehicleModel model(spec);
    // Saved dd55 moving-pose impact context. Source forward request up2 is
    //4150; legacy placement up2 is4200. Replacing shared arrays changes gear.
    check(model.upshift_rpm()[2]==4200.0,"legacy placement getter remains unchanged");
    VehiclePose pose;pose.speed=11.189782199838891;VehicleState state;
    check(model.reset(state,pose),"directed moving-pose reset completes");
    check(state.gear==2&&state.transmission.requested_gear==2,
          "request tables do not change moving-pose placement");
}
void domain_cases() {
    auto spec=stock_240sx_vehicle_spec();auto& h=spec.handling;
    h.idle_rpm=2147483392.0F;h.redline_rpm=h.max_rpm=2147483648.0F;
    h.gear_ratios[2]=2;h.gear_ratios[3]=1;h.torque.fill(.2F);
    std::array<float,kVehicleGearSlots> up,down;up.fill(32.25F);down.fill(-16.5F);
    const auto old_up=up,old_down=down;
    check(!player_request_shift_tables(h,up,down),"source no-progress search is rejected explicitly");
    check(up==old_up&&down==old_down,"failed builder does not publish partial tables");
    const VehicleModel invalid(spec);
    check(!invalid.valid()&&invalid.error().find("binary32 precision")!=std::string::npos,
          "unrepresentable source search produces precise model error");
    VehicleState state;state.step=17;state.position.x=42;state.engine_rpm=1234;
    VehicleTelemetry t;t.step=99;
    check(!invalid.reset(state,{})&&!invalid.step(state,{},VehicleFlatGround{},t),
          "invalid precision model refuses reset and step");
    check(state.step==17&&state.position.x==42&&state.engine_rpm==1234&&t.step==99,
          "invalid model refusal preserves caller state and telemetry");

    h.idle_rpm=1.0e20F;h.redline_rpm=h.max_rpm=2.0e20F;
    const bool source_refused=!player_request_shift_tables(h,up,down);
    check(source_refused,"high-RPM source search is unsupported explicitly");
    // A wrong-success builder negative already fails above; do not enter its
    // unsupported legacy search and misclassify a timeout as a mutation kill.
    if(source_refused) {
        const VehicleModel high_invalid(spec);
        check(!high_invalid.valid(),"constructor refuses before nonadvancing legacy search");
    }

    h.idle_rpm=1073741568.0F;h.redline_rpm=h.max_rpm=1073741824.0F;
    check(player_request_shift_tables(h,up,down)&&VehicleModel(spec).valid(),
          "finite advancing large-RPM search remains accepted");
    h.idle_rpm=2147483392.0F;h.redline_rpm=h.max_rpm=2147483648.0F;
    h.torque[4]=.02F;
    check(player_request_shift_tables(h,up,down)&&VehicleModel(spec).valid(),
          "same large RPM accepted when actual crossing needs no advance");

    spec=stock_240sx_vehicle_spec();h=spec.handling;
    h.idle_rpm=1.0e-38F;h.redline_rpm=2.0e-38F;h.max_rpm=3.0e-38F;
    check(!player_request_shift_tables(h,up,down)&&!VehicleModel(spec).valid(),
          "required curve-scale store cannot be represented as finite binary32");
    h.forward_gears=1;
    check(player_request_shift_tables(h,up,down)&&VehicleModel(spec).valid(),
          "single forward gear does not reject unused curve/search operations");

    spec=stock_240sx_vehicle_spec();h=spec.handling;
    h.gear_ratios[2]=2.0e-5F;h.gear_ratios[3]=std::numeric_limits<float>::max();
    check(!player_request_shift_tables(h,up,down)&&!VehicleModel(spec).valid(),
          "required projected-RPM store overflow is rejected");
    h.gear_ratios[3]=1.0F;h.torque.fill(std::numeric_limits<float>::max()*.5F);
    check(!player_request_shift_tables(h,up,down)&&!VehicleModel(spec).valid(),
          "required torque times stored inverse overflow is rejected");
    h.gear_ratios[2]=1.0F;h.gear_ratios[3]=3.0F;
    check(!player_request_shift_tables(h,up,down)&&!VehicleModel(spec).valid(),
          "required scaled torque times next ratio overflow is rejected");
    h.gear_ratios[2]=2.0e-5F;h.gear_ratios[3]=std::numeric_limits<float>::max();
    h.idle_rpm=1.0e-8F;h.redline_rpm=2.0e-8F;h.max_rpm=1.0F;h.torque.fill(0.0F);
    check(!player_request_shift_tables(h,up,down)&&!VehicleModel(spec).valid(),
          "required down-ratio/down-slot overflow is rejected");
    h.idle_rpm=2.1e38F;h.redline_rpm=h.max_rpm=2.2e38F;
    h.gear_ratios[2]=2.0F;h.gear_ratios[3]=1.0F;h.torque.fill(.2F);h.torque[4]=.02F;
    check(!player_request_shift_tables(h,up,down)&&!VehicleModel(spec).valid(),
          "required initial RPM sum exceeds binary32 exponent range");

    spec=stock_240sx_vehicle_spec();h=spec.handling;
    h.gear_ratios[2]=h.gear_ratios[3]=std::numeric_limits<float>::max();
    h.torque.fill(.2F);h.torque[3]=.7F;h.torque[4]=.007692418061196804F;
    check(!player_request_shift_tables(h,up,down)&&!VehicleModel(spec).valid(),
          "source wide-exponent inverse/subnormal torque precision is unsupported explicitly");
    h.forward_gears=2;h.torque.fill(16.0F);
    check(!player_request_shift_tables(h,up,down)&&!VehicleModel(spec).valid(),
          "unrepresentable live inverse is rejected even if other products are normal");
    h.gear_ratios[2]=h.gear_ratios[3]=std::ldexp(1.0F,127);h.torque.fill(1.0F);
    check(player_request_shift_tables(h,up,down)&&VehicleModel(spec).valid(),
          "exact subnormal source arithmetic remains accepted");
    float ordinary_ratio=0.0F;const std::uint32_t ordinary_bits=0x7F000002U;
    std::memcpy(&ordinary_ratio,&ordinary_bits,sizeof(ordinary_ratio));
    h.gear_ratios[2]=h.gear_ratios[3]=ordinary_ratio;
    check(player_request_shift_tables(h,up,down)&&VehicleModel(spec).valid(),
          "ordinary inexact division accepted when source24-bit result fits binary32");
    h.gear_ratios[2]=h.gear_ratios[3]=3.321000099182129F;
    float tiny_torque=0.0F;const std::uint32_t tiny_bits=0x00800001U;
    std::memcpy(&tiny_torque,&tiny_bits,sizeof(tiny_torque));h.torque.fill(tiny_torque);
    check(!player_request_shift_tables(h,up,down)&&!VehicleModel(spec).valid(),
          "actual required interpolation product loses source wide-exponent precision");
    h.idle_rpm=std::numeric_limits<float>::quiet_NaN();
    check(!VehicleModel(spec).valid(),"invalid handling retains normal validation refusal");
}
void legacy_domain_case() {
    auto spec=stock_240sx_vehicle_spec();auto& h=spec.handling;
    const std::uint32_t idle_bits=1621981404U,redline_bits=1630370013U;
    std::memcpy(&h.idle_rpm,&idle_bits,sizeof(h.idle_rpm));
    std::memcpy(&h.redline_rpm,&redline_bits,sizeof(h.redline_rpm));
    h.max_rpm=h.redline_rpm;h.forward_gears=2;
    h.gear_ratios[2]=2.0F;h.gear_ratios[3]=1.0F;
    h.torque.fill(.2F);h.torque[4]=.1F;h.torque[5]=.02F;
    std::vector<VehicleHandlingIssue> issues;
    check(check_vehicle_handling(h,issues),"legacy nonadvance fixture has valid handling");
    std::array<float,kVehicleGearSlots> up{},down{};
    check(player_request_shift_tables(h,up,down),"source helper accepts legacy nonadvance fixture");
    const double rpm=(double(h.redline_rpm)+double(h.idle_rpm))*.5;
    const double here=vehicle_uniform_curve(h.torque.data(),h.torque.size(),h.idle_rpm,h.max_rpm,rpm);
    const double there=vehicle_uniform_curve(h.torque.data(),h.torque.size(),h.idle_rpm,h.max_rpm,rpm*.5)*.5;
    check(there<=here&&rpm<h.redline_rpm&&50.0+rpm==rpm,
          "legacy search actually requires an iteration without representable progress");
    const VehicleModel invalid(spec);
    check(!invalid.valid()&&invalid.error()==
          "legacy placement shift-table search cannot advance at double precision",
          "unsupported placement search has distinct precise refusal");
    VehicleState state;state.step=17;state.position.x=42;state.engine_rpm=1234;
    VehicleTelemetry t;t.step=99;
    check(!invalid.reset(state,{})&&!invalid.step(state,{},VehicleFlatGround{},t),
          "invalid legacy search model refuses reset and step");
    check(state.step==17&&state.position.x==42&&state.engine_rpm==1234&&t.step==99,
          "legacy domain refusal preserves caller state and telemetry");

    h.torque[4]=.02F;
    check(player_request_shift_tables(h,up,down),"large first crossing source helper completes");
    const VehicleModel crossing(spec);
    check(crossing.valid()&&crossing.upshift_rpm()[2]==rpm,
          "legacy first crossing accepted without an unused advance");
    h.idle_rpm=1073741568.0F;h.redline_rpm=h.max_rpm=1073741824.0F;
    h.torque.fill(.2F);
    const VehicleModel advancing(spec);
    check(advancing.valid()&&advancing.upshift_rpm()[2]==double(h.redline_rpm)-100.0,
          "completed advancing legacy placement search retains its result");
}
}
int main() {
    producer_cases();request_cases();compatibility_case();domain_cases();legacy_domain_case();
    std::cout<<"SUMMARY request precision failures="<<failures<<'\n';
    if(failures)return 1;
    std::cout<<"PASS original forward producer/request precision and explicit representability domain\n";
}
