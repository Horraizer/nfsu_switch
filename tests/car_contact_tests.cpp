#include "nfsu/car_contact.h"
#include <cstring>
#include <iostream>
#include <limits>

namespace {
using namespace nfsu;
struct ApplicationPin {
    const char* name;
    ContactBody before;
    ContactVector impulse,arm,after_linear,after_angular;
};
struct RecordPin {
    const char* name;
    ContactVector before;
    float before_sum;
    ContactVector current,after;
    float after_sum;
};
struct PairPin {
    const char* name;
    ContactBody a,b;
    ContactGeometry geometry;
    bool admitted;
    ContactVector current;
    float tangent_speed;
    ContactVector accumulated;
    float before_sum;
    ContactBody after_a,after_b;
    float after_sum;
    unsigned seed_before,seed_after;
    bool physical_bounds;
    std::array<std::array<float,2>,2> bounds;
    bool record_gate_before;
    ContactVector witness_a,witness_b;
};
#include "car_contact_pins.inc"
bool same(const ContactVector& a,const ContactVector& b) {
    return std::memcmp(a.data(),b.data(),sizeof(float)*3)==0;
}
bool same_boundary(const ContactBody& a,const ContactBody& b) {
    return same(a.linear_momentum,b.linear_momentum)&&same(a.angular_momentum,b.angular_momentum)
        &&same(a.cached_velocity,b.cached_velocity)&&same(a.cached_angular_velocity,b.cached_angular_velocity)
        &&same(a.position,b.position)&&a.collision_state==b.collision_state&&a.state_timer==b.state_timer;
}
int failures=0;
void check(bool pass,const char* name) {
    if(!pass) { ++failures;std::cerr<<"FAIL: "<<name<<'\n'; }
}
}
int main() {
    using namespace nfsu;
    for(const auto& p:kApplicationPins) {
        auto b=p.before;
        check(apply_car_contact_impulse(b,p.impulse,p.arm),p.name);
        check(same(b.linear_momentum,p.after_linear),p.name);
        check(same(b.angular_momentum,p.after_angular),p.name);
        check(same(b.cached_velocity,p.before.cached_velocity)
              &&same(b.cached_angular_velocity,p.before.cached_angular_velocity),"cache boundary");
        auto repeated=p.before;
        check(apply_car_contact_impulse(repeated,p.impulse,p.arm)&&same_boundary(repeated,b),"application repeat");
    }
    for(const auto& p:kRecordPins) {
        ContactRecord record;
        record.accumulated_impulse=p.before;record.magnitude_sum=p.before_sum;
        check(accumulate_contact_hit(record,p.current,{1,2,3},4,5),p.name);
        check(same(record.accumulated_impulse,p.after)&&record.magnitude_sum==p.after_sum,p.name);
        check(record.timestamp==5&&record.point==ContactVector{1,2,3}&&record.tangent_speed==4,"record metadata");
    }
    for(const auto& p:kPairPins) {
        ContactGeometry prepared;
        check(prepare_car_contact_geometry(p.witness_a,p.witness_b,p.a.position,p.b.position,prepared)
              &&same(prepared.normal,p.geometry.normal)&&same(prepared.point,p.geometry.point),p.name);
        ContactResponse response;
        if(p.admitted) {
            check(calculate_car_contact_response(p.a,p.b,p.geometry,response),p.name);
            check(same(response.current_impulse,p.current)&&response.tangent_speed==p.tangent_speed,p.name);
        }
        auto a=p.a,b=p.b;
        ContactRecord record;record.accumulated_impulse=p.accumulated;record.magnitude_sum=p.before_sum;
        record.state_gate=p.record_gate_before;
        const auto before_record=record;
        ContactCallbackContext context{p.seed_before};
        context.has_physical_bounds=p.physical_bounds;
        context.physical_lateral_bounds=p.bounds;
        const auto result=resolve_prepared_car_contact(a,b,record,p.admitted,p.geometry,5,response,&context);
        check(result!=ContactResolution::refused&&result!=ContactResolution::needs_state_callback,p.name);
        check(same_boundary(a,p.after_a)&&same_boundary(b,p.after_b),p.name);
        check(record.magnitude_sum==p.after_sum,p.name);
        if(p.seed_after) check(context.world_seed==p.seed_after,"callback random state");
        auto repeat_a=p.a,repeat_b=p.b;auto repeat_record=before_record;
        ContactCallbackContext repeat_context{p.seed_before};ContactResponse repeat_response;
        repeat_context.has_physical_bounds=p.physical_bounds;
        repeat_context.physical_lateral_bounds=p.bounds;
        check(resolve_prepared_car_contact(repeat_a,repeat_b,repeat_record,p.admitted,p.geometry,5,
              repeat_response,&repeat_context)==result&&same_boundary(a,repeat_a)
              &&same_boundary(b,repeat_b)&&context.world_seed==repeat_context.world_seed,"pair deterministic repeat");
        if(std::strcmp(p.name,"hard_hit")==0) {
            a=p.a;b=p.b;record=before_record;
            check(resolve_prepared_car_contact(a,b,record,true,p.geometry,5,response)
                  ==ContactResolution::needs_state_callback,"explicit hard callback gate");
            check(same_boundary(a,p.a)&&same_boundary(b,p.b)&&record.magnitude_sum==before_record.magnitude_sum,
                  "callback gate transactional");
        }
        if(std::strcmp(p.name,"retail_cv_nonopposing")==0) {
            a=p.a;b=p.b;record=before_record;
            ContactCallbackContext missing_bounds{p.seed_before};
            check(resolve_prepared_car_contact(a,b,record,true,p.geometry,5,response,&missing_bounds)
                  ==ContactResolution::needs_state_callback,"original physical bounds required");
            check(same_boundary(a,p.a)&&same_boundary(b,p.b)&&!record.state_gate
                  &&missing_bounds.world_seed==p.seed_before,"missing physical bounds transactional");
        }
    }
    // Unsupported existing record state never impersonates a complete solve.
    {
        const auto& p=kPairPins[0];auto a=p.a,b=p.b;
        ContactRecord record;record.state_gate=true;ContactResponse response;
        ContactCallbackContext context{12345};
        check(resolve_prepared_car_contact(a,b,record,true,p.geometry,5,response,&context)
              ==ContactResolution::needs_state_callback,"existing record callback gate");
        check(same_boundary(a,p.a)&&same_boundary(b,p.b),"existing record transactional");
        a.cached_velocity={50,50,0};b.cached_velocity={-50,-50,0};
        const auto before_a=a,before_b=b;record={};
        check(resolve_prepared_car_contact(a,b,record,true,{{1,0,0},{1,0,0}},5,response,&context)
              ==ContactResolution::needs_state_callback,"nonopposing callback gate");
        check(same_boundary(a,before_a)&&same_boundary(b,before_b)&&!record.state_gate,
              "unsupported callback transactional");
    }
    ContactBody a,b;a.identity=1;b.identity=2;a.inverse_mass=.5F;b.inverse_mass=1;
    auto before=a;
    check(!apply_contact_pair(a,a,{1,0,0},{},{-1,0,0},{}),"alias refused");
    check(same_boundary(a,before),"alias transactional");
    b.inverse_mass=0;
    check(!apply_contact_pair(a,b,{1,0,0},{},{-1,0,0},{}),"invalid second refused");
    check(same_boundary(a,before),"invalid pair transactional");
    check(!apply_car_contact_impulse(a,{std::numeric_limits<float>::infinity(),0,0},{}),"nonfinite refused");
    check(same_boundary(a,before),"nonfinite transactional");
    check(!apply_car_contact_impulse(a,{std::numeric_limits<float>::max(),0,0},{}),"unsupported overflow refused");
    check(same_boundary(a,before),"overflow transactional");
    std::cout<<(failures?"FAIL: ":"PASS: ")<<"prepared response, current hit, ordered paired momentum and state boundaries; "
             <<failures<<" failures\n";
    return failures?1:0;
}
