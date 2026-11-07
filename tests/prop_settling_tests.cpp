#include "nfsu/prop_settling.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <limits>
#include "prop_pool_native.inc"
int main(int argc,char** argv) {
    using V=std::array<float,3>;
    const V zero{};
    unsigned failures=0,rows=0;
    auto check=[&](bool ok){if(!ok)++failures;};
    for(const auto& c:kNativePropPoolCases)
        check(nfsu::prop_pool_reclaims(c.free_slots,c.awake!=0,c.exempt!=0,
            c.distance,100000,c.activation)==c.retire);
    for(unsigned frame=0;frame<256;++frame) {
        check(nfsu::prop_pool_maintenance_due(3,frame));
        check(nfsu::prop_pool_maintenance_due(4,frame)==(frame==51||frame==179));
        check(nfsu::prop_pool_maintenance_due(32,frame)==(frame==51||frame==179));
    }
    // Authenticated596649 and597614 instruction-range witnesses.
    const float lifts[][6]={
        {0x0.0p+0F,-0x1.0000000000000p+0F,0x0.0p+0F,0x1.0000000000000p+0F,0x0.0p+0F,0x1.0ccccc0000000p+0F},
        {0x0.0p+0F,-0x1.0000000000000p+0F,0x0.0p+0F,0x1.0000000000000p+0F,0x1.0000000000000p+1F,0x0.0p+0F},
        {0x1.4000000000000p+3F,-0x1.0000000000000p+0F,0x1.0000000000000p-2F,0x1.0000000000000p+0F,0x1.2cccccccccccdp+3F,0x1.66666c0000000p+0F},
        {0x1.4000000000000p+3F,-0x1.0000000000000p+0F,0x1.0000000000000p-2F,0x1.999999999999ap-1F,0x1.2cccccccccccdp+3F,0x1.40000c0000000p+0F},
        {0x1.4000000000000p+3F,-0x1.0000000000000p+0F,0x1.0000000000000p-2F,-0x1.999999999999ap-1F,0x1.2cccccccccccdp+3F,0x1.99999a0000000p-5F},
        {0x0.0p+0F,0x0.0p+0F,0x1.0000000000000p-2F,0x1.0000000000000p+0F,0x0.0p+0F,0x0.0p+0F},
        {0x0.0p+0F,-0x1.0000000000000p-2F,0x1.0000000000000p-2F,0x1.0000000000000p+0F,0x1.999999999999ap-5F,0x0.0p+0F},
        {0x0.0p+0F,-0x1.0000000000000p-2F,0x1.0000000000000p-2F,0x1.0000000000000p+0F,0x1.47ae147ae147bp-5F,0x1.47ae180000000p-7F},
        {0x0.0p+0F,-0x1.0000000000000p-2F,0x1.0000000000000p-2F,0x1.0000000000000p+0F,0x1.eb851eb851eb8p-5F,0x0.0p+0F}
    };
    for(const auto& v:lifts)check(nfsu::prop_conversion_lift(v[0],v[1],v[2],v[3],v[4])==v[5]);
    const float decays[][5]={
        {0x0.0p+0F,0x1.0000000000000p+0F,0x0.0p+0F,0x1.1111111111111p-6F,0x0.0p+0F},
        {0x0.0p+0F,0x0.0p+0F,0x0.0p+0F,0x1.1111111111111p-6F,0x0.0p+0F},
        {0x0.0p+0F,0x1.0000000000000p+0F,0x1.999999999999ap-3F,0x1.1111111111111p-6F,0x0.0p+0F},
        {0x1.0624dd2f1a9fcp-10F,0x1.0000000000000p+0F,0x0.0p+0F,0x1.1111111111111p-6F,0x0.0p+0F},
        {0x1.0624dd2f1a9fcp-10F,0x0.0p+0F,0x0.0p+0F,0x1.1111111111111p-6F,0x1.0624de0000000p-10F},
        {0x1.0624dd2f1a9fcp-10F,0x1.0000000000000p+0F,0x1.999999999999ap-3F,0x1.1111111111111p-6F,0x1.0624de0000000p-10F},
        {0x1.999999999999ap-5F,0x1.0000000000000p+0F,0x0.0p+0F,0x1.1111111111111p-6F,0x1.7e4b180000000p-5F},
        {0x1.999999999999ap-5F,0x0.0p+0F,0x0.0p+0F,0x1.1111111111111p-6F,0x1.99999a0000000p-5F},
        {0x1.999999999999ap-5F,0x1.0000000000000p+0F,0x1.999999999999ap-3F,0x1.1111111111111p-6F,0x1.99999a0000000p-5F},
        {0x1.3333333333333p-2F,0x1.0000000000000p+0F,0x0.0p+0F,0x1.1111111111111p-6F,0x1.2fc9640000000p-2F},
        {0x1.3333333333333p-2F,0x0.0p+0F,0x0.0p+0F,0x1.1111111111111p-6F,0x1.3333340000000p-2F},
        {0x1.3333333333333p-2F,0x1.0000000000000p+0F,0x1.999999999999ap-3F,0x1.1111111111111p-6F,0x1.3333340000000p-2F}
    };
    for(const auto& v:decays)check(nfsu::prop_next_lift(v[0],v[1]!=0,v[2],v[3])==v[4]);
    check(nfsu::prop_motion_state(zero,zero,false,0,1)==0);
    check(nfsu::prop_motion_state(zero,zero,false,2,1)==2);
    check(nfsu::prop_motion_state({{2,0,0}},zero,true,0,1)==1);
    check(nfsu::prop_motion_state({{std::nextafter(2.F,3.F),0,0}},zero,true,0,1)==0);
    check(nfsu::prop_motion_state({{.8F,0,0}},zero,true,0,.1F)==2);
    check(!nfsu::prop_can_settle(zero,zero,true,.1F,1));
    check(!nfsu::prop_can_settle(zero,zero,true,1,.025F));
    check(!nfsu::prop_can_settle({{.1F,0,0}},zero,true,1,1));
    check(nfsu::prop_can_settle(zero,zero,true,1,1));
    std::vector<nfsu::PropGroundContact> contacts;
    nfsu::prop_touch_ground(contacts,19,100);
    nfsu::prop_touch_ground(contacts,7,167);
    nfsu::prop_touch_ground(contacts,19,234);
    check(contacts.size()==2 && contacts[0].created==100 && contacts[0].touched==234);
    nfsu::prop_expire_ground(contacts,1366);
    check(contacts.size()==2); // 1199 ticks since polygon7 touch: retained.
    nfsu::prop_expire_ground(contacts,1367);
    check(contacts.size()==1 && contacts[0].polygon==19); // equality expires.
    nfsu::prop_expire_ground(contacts,1434);
    check(contacts.empty());
    nfsu::prop_touch_ground(contacts,19,1500);
    check(contacts.front().created==1500); // Recontact gets a new creation.
    if(argc==2) {
        std::ifstream in(argv[1]);if(!in){std::cerr<<"FAIL vectors unavailable\n";return 1;}
        V v,w;
        while(in>>v[0]>>v[1]>>v[2]>>w[0]>>w[1]>>w[2]) {
            unsigned contact,previous,state,settles,expired;float age,contact_age;
            in>>contact>>previous>>age>>contact_age>>state>>settles>>expired;
            V p{{-3.25F,2.75F,.123F}},l{{7.5F,-.375F,2.F}};
            const auto actual=nfsu::prop_motion_state(v,w,contact!=0,previous,age);
            check(actual==state);
            check(nfsu::prop_can_settle(v,w,contact!=0,age,contact_age)==bool(settles));
            check(nfsu::prop_contact_expired(contact_age)==bool(expired));
            nfsu::prop_rest_momenta(actual,p,l);
            for(float x:{p[0],p[1],p[2],l[0],l[1],l[2]}) {
                std::uint32_t expected,bits;in>>expected;std::memcpy(&bits,&x,4);check(bits==expected);
            }
            if(!in){std::cerr<<"FAIL incomplete vector\n";return 1;}++rows;
        }
        check(rows>0);
    }
    std::cout<<(failures?"FAIL":"PASS")<<" prop settling failures="<<failures<<" original_vectors="<<rows<<"\n";
    return failures?1:0;
}
