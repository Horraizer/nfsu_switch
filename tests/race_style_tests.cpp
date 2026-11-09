#include "nfsu/race_style.h"
#include "nfsu/race_drift.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace nfsu;
static void check(bool value,const char* name){if(!value)throw std::runtime_error(name);}
int main(int argc,char** argv) {try {
    if(argc==2&&std::string(argv[1])=="--draft") {
        std::array<float,3> p,v,f;float speed;std::cout.precision(10);
        while(std::cin>>p[0]>>p[1]>>p[2]>>v[0]>>v[1]>>v[2]>>speed>>f[0]>>f[1]>>f[2])
            std::cout<<race_drafting_pair(p,v,speed,f)<<'\n';
        return 0;
    }
    check(argc==2,"retail root required");
    {
        // Original 4A83C0 still displays the selected record on the update
        // that removes it. An ordinary continuous notice keeps its timer.
        std::array<RaceStyleRule,kRaceStyleKinds> rules{};
        auto& continuous=rules[11];
        continuous.continuous=true;continuous.enabled=true;
        continuous.rate=100.F;continuous.timeout=.2F;
        for(bool drift:{false,true}) {
            RaceStyle notices;notices.configure(rules,drift);
            notices.trigger(11,1.F,4000);notices.update(4000,.05F);
            notices.trigger(11,1.F,4067);notices.update(4067,.05F);
            notices.update(4134,.05F);
            check(notices.notice()&&notices.notice()->kind==11,"continuous notice rendered on admission");
            notices.cancel(11);notices.update(4201,.05F);
            check(notices.notice()&&notices.notice()->failed,"failed continuous notice renders its last publication");
            notices.update(4268,.05F);
            check(notices.notice()&&notices.notice()->failed,"final continuous notice survives simulation catch-up");
            notices.consume_drift_notice();
            check(bool(notices.notice())!=drift,"only the displayed continuous drift notice retires");
            if(!drift) {
                notices.update(8301,0.F);
                check(notices.notice()&&notices.notice()->kind==11,"expired ordinary notice rendered on removal");
                notices.update(8368,0.F);
                check(!notices.notice(),"expired ordinary notice absent on next update");
            }
        }
    }
    RaceStyle score;std::string error;
    {
        RaceStyle zone;
        const bool loaded=zone.load(argv[1],error,true);check(loaded,error.c_str());
        RaceDrift drift;DriftDrivingSample input;
        input.now=4000;input.seconds=1.F/60.F;input.reference_speed=50;input.bonus_wheels=4;
        input.motion.speed=25;input.motion.slip=.6F;input.motion.yaw=.2F;input.motion.grade=3;
        const auto tick=[&] {input.now+=67;input.motion.progress+=.001F;drift.update(zone,input);};
        for(unsigned i=0;i<60;++i)tick();
        check(zone.item(1).phase==0,"retail drift zone reached scoring phase");
        input.bonus_wheels=0;input.motion.slip=0;
        for(unsigned i=0;i<80&&zone.item(1).phase!=3;++i)tick();
        check(zone.item(1).phase==3&&zone.item(1).total>0,"retail drift zone score banked");
        tick();tick();
        check(zone.notice()&&zone.notice()->kind==1&&zone.notice()->points==zone.item(1).total,
              "retail drift zone final award reaches the HUD after two simulation ticks");
        zone.consume_drift_notice();
        check(!zone.notice()||zone.notice()->kind!=1,"retail drift zone final award is consumed after presentation");
    }
    check(score.load(argv[1],error),error.c_str());
    check(score.rule(11).continuous&&score.rule(11).rate==100&&score.rule(11).minimum==50,"powerslide tuning");
    check(score.rule(17).multiplier_limit==3&&score.rule(13).count_limit==1,"multiplier and once tuning");
    check(!score.rule(0).enabled&&!score.rule(21).enabled,"other modes excluded");
    RaceStyleDriving drive;RaceStyleDrivingSample s;s.now=4000;s.seconds=1.F/60;
    s.speed=40;s.forward_speed=40;s.slip_degrees=20;
    for(int i=0;i<120;++i){s.now+=67;drive.update(score,s);}
    check(score.item(11).points==0,"countdown cannot score");
    s.racing=true;s.race_seconds=4;
    for(int i=0;i<45;++i){s.now+=67;drive.update(score,s);}
    check(score.item(11).phase==0&&score.item(11).points>50,"sustained grounded slide");
    s.speed=25;
    for(int i=0;i<40;++i){s.now+=67;drive.update(score,s);}
    check(score.item(11).total>50&&score.total()>0,"slide banks after timeout");
    const auto banked=score.item(11).total;
    s.slip_degrees=5;s.speed=40;
    for(int i=0;i<60;++i){s.now+=67;drive.update(score,s);}
    check(score.item(11).total==banked&&score.item(11).points==0,"strict angle threshold");
    // First line establishes clean-section state. A held timing flag is one edge.
    s.new_checkpoint=true;drive.update(score,s);
    check(score.item(17).total==0,"no clean section before first line");
    s.new_checkpoint=false;drive.update(score,s);s.new_checkpoint=true;drive.update(score,s);
    check(score.item(17).total==50,"first full clean section");
    for(int i=0;i<10;++i){s.now+=67;drive.update(score,s);}
    check(score.item(17).total==50,"held checkpoint cannot duplicate award");
    s.new_checkpoint=false;s.impact=true;drive.update(score,s);s.impact=false;
    s.new_checkpoint=true;drive.update(score,s);
    check(score.item(17).total==50&&score.item(17).multiplier==1,"impact spoils section and chain");
    s.finished=true;s.position=1;drive.update(score,s);
    check(score.item(13).total==500&&score.item(19).total==0,"victory without false clean race");
    const auto total=score.total();drive.update(score,s);check(score.total()==total,"finish idempotent");
    RaceStyle fresh;check(fresh.load(argv[1],error),error.c_str());
    check(fresh.total()==0&&!fresh.notice(),"fresh race clears total and queue");
    RaceStyleDriving air;RaceStyleDrivingSample a;a.racing=true;a.now=12000;a.seconds=1.F/60;
    a.support=0;a.ground_valid=true;a.height=1;a.up_z=1;a.race_seconds=3;
    for(int i=0;i<20;++i){a.now+=67;air.update(fresh,a);}
    a.support=4;
    for(int i=0;i<40;++i){a.now+=67;air.update(fresh,a);}
    check(fresh.item(12).total==0,"short hop below duration earns nothing");
    a.support=0;a.ground_valid=false;
    for(int i=0;i<60;++i){a.now+=67;air.update(fresh,a);}
    check(fresh.item(12).points==0,"missing ground is not unlimited hang time");
    a.ground_valid=true;
    for(int i=0;i<50;++i){a.now+=67;air.update(fresh,a);}
    check(fresh.item(12).phase==0,"actual jump qualifies");
    a.finished=true;a.position=2;air.update(fresh,a);
    check(fresh.item(12).total>50&&fresh.item(13).total==0,"finish banks jump but loser gets no victory");
    RaceStyle traffic;check(traffic.load(argv[1],error),error.c_str());
    traffic.trigger(15,.5F,4000);traffic.update(4000,1.F/60);
    check(traffic.item(15).total==-25&&traffic.total()==-25,"original HIT TRAFFIC factor debits style");
    RaceStyleNearMiss near;
    RaceNearPass pass{1,1,45,false};near.update(traffic,8000,30,&pass,1);
    check(traffic.item(22).total==0,"close approach alone cannot score");
    pass.hull_distance=2;near.update(traffic,8067,30,&pass,1);traffic.update(8067,1.F/60);
    check(traffic.item(22).total==100,"completed close fast-relative pass scores");
    pass={2,1,45,false};near.update(traffic,8200,30,&pass,1);pass.contact=true;
    near.update(traffic,8267,30,&pass,1);near.update(traffic,8334,30,nullptr,0);
    check(traffic.item(22).total==100,"collided pass cannot score near miss");
    near.impact(9000);pass={3,1,45,false};near.update(traffic,9067,30,&pass,1);
    near.update(traffic,9134,30,nullptr,0);check(traffic.item(22).total==100,"recent impact blocks near-miss acquisition");
    const std::array<float,3> zero{{0,0,0}},velocity{{50,0,0}};
    const auto close=race_drafting_pair({10,0,0},velocity,50,zero);
    check(close>0&&close<1,"moving leader gives aerodynamic draft behind it");
    check(race_drafting_pair({-10,0,0},velocity,50,zero)==0,"no draft in front of leader");
    check(race_drafting_pair({10,10,0},velocity,50,zero)==0,"outside original narrow cone");
    check(race_drafting_pair({60,0,0},velocity,50,zero)==0,"original 60m reach bound");
    check(race_drafting_pair({1,0,0},{0,0,0},0,zero)==0,"stationary leader gives no draft");
    std::cout<<"PASS style: retail tuning, countdown, slide boundaries, timeout, checkpoint edges, impact, finish, restart, jump\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
