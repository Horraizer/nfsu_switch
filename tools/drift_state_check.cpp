#include "nfsu/race_drift.h"
#include "../tests/drift_comment_native.inc"
#include "nfsu/arg_path.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    std::ifstream input(argv[1]);if(!input)return 2;
    unsigned count=0,fail=0;std::string line;
    const auto check=[&](bool pass,const char* what){++count;if(!pass){++fail;std::cout<<"FAIL "<<what<<'\n';}};
    while(std::getline(input,line)) {
        if(line.empty()||line[0]=='#')continue;
        std::istringstream row(line);unsigned grade,phase;float speed,points;int block,timer,warn,recover,after_timer;
        nfsu::DriftCombo c;int next,target;std::int32_t now,after_then;
        if(!(row>>grade>>c.multiplier>>speed>>phase>>points>>block>>timer>>c.reduced_at>>now>>c.target
             >>next>>target>>warn>>recover>>after_timer>>after_then))return 2;
        c.timer=timer!=0;nfsu::drift_combo_update(c,speed,block!=0,grade,phase,points,now);
        const bool matched=c.multiplier==next&&c.target==target&&c.warning==bool(warn)&&c.recovering==bool(recover)&&
              c.timer==bool(after_timer)&&c.reduced_at==after_then;
        if(!matched)std::cout<<"COMBO input="<<line<<" actual="<<c.multiplier<<','<<c.target<<','
            <<c.warning<<','<<c.recovering<<','<<c.timer<<','<<c.reduced_at<<'\n';
        check(matched,"original combo differential");
    }
    std::filesystem::path root;std::string error;
    if(!nfsu::argument_to_path(argv[2],root,error))return 2;
    nfsu::RaceStyle score;if(!score.load(root,error,true)){std::cerr<<error;return 1;}
    nfsu::DriftComment comment;
    for(const auto& row:kNativeDriftComment){comment.show(4,10000);
        check(bool(comment.visible(10000+std::int32_t(row[0])))==bool(row[1]),"original independent drift comment lifetime");}
    comment.show(5,17000);check(comment.visible(18001)==5,"new achievement replaces and restarts comment");
    nfsu::RaceDrift drift;nfsu::DriftDrivingSample s;s.seconds=1.F/60;s.reference_speed=60;
    s.motion.speed=25;s.motion.slip=.6F;s.motion.grade=3;s.motion.yaw=.2F;
    int tick=0;
    const auto advance=[&](int frames){for(int i=0;i<frames;++i){s.now=4000+(++tick)*200/3;s.motion.progress+=.001F;drift.update(score,s);}};
    advance(180);check(score.item(0).phase==0&&score.item(0).points>1000&&drift.multiplier()>1,"qualified growing chain");
    check(score.drift_publication().sequence&&score.drift_publication().points>0&&!score.drift_publication().failed,
        "live item0 publishes to separate drift widget");
    const auto pending=score.item(0).points;s.motion.slip=0;advance(120);
    check(score.item(0).total>=pending&&score.item(0).phase==2,"timeout banks completed chain");
    check(score.total()<int(score.item(0).total),"drift competition points excluded from general style");
    s.motion.slip=.6F;advance(120);const int before=score.total();
    check(score.item(0).points>0,"second chain started");
    const auto last_displayed=score.drift_publication().points;
    s.impact_strength=15;s.wall_impact=true;advance(1);s.impact_strength=0;
    check(score.drift_publication().failed&&score.drift_publication().points==last_displayed,
        "failed chain preserves prior displayed points after scoring reset");
    check(!score.notice()||score.notice()->kind!=0,"drift item0 bypasses ordinary style queue");
    check(score.item(0).points==0&&score.total()<before&&drift.multiplier()==1,"wall loses unbanked chain and penalizes once");
    const int penalty_total=score.total();s.motion.slip=0;advance(20);
    check(score.total()==penalty_total,"wall latch prevents repeated penalty");
    advance(60);s.motion.slip=.6F;advance(180);
    const auto banked=score.item(0).total;
    const auto qualified=score.item(0).points;
    check(score.item(0).phase==0&&qualified>0,"finish fixture has a qualified chain");
    s.finished=true;advance(1);
    check(score.item(0).total==banked+qualified,"finish banks only the pre-line drift points");
    const int finished=score.total();advance(180);
    check(drift.finished()&&score.total()==finished&&finished>penalty_total,"finish banks once and stops accrual");
    nfsu::RaceStyle impact_score;
    if(!impact_score.load(root,error,true)){std::cerr<<error;return 1;}
    nfsu::RaceDrift impact_finish;
    s.finished=false;
    for(unsigned i=0;i<120;++i) {
        s.now+=67;s.motion.progress+=.001F;impact_finish.update(impact_score,s);
    }
    const auto pre_impact=impact_score.item(0).points;
    check(impact_score.item(0).phase==0&&pre_impact>0,"finish impact fixture has a qualified chain");
    s.finished=true;s.impact_strength=15;s.wall_impact=true;
    impact_finish.update(impact_score,s);
    check(impact_score.item(0).total==pre_impact&&impact_score.item(2).total==0,
        "finish cannot cancel the previous chain or create a new wall penalty");
    nfsu::RaceDrift wrong_way;nfsu::DriftDrivingSample wrong;
    wrong.motion.blocked=true;wrong.motion.grade=3;wrong.now=100;wrong_way.update(score,wrong);
    wrong.now=4100;wrong_way.update(score,wrong);check(wrong_way.reset_countdown()<0,"wrong-way grace inclusive boundary");
    wrong.now=4101;wrong_way.update(score,wrong);check(wrong_way.reset_countdown()>0,"wrong-way countdown after grace");
    wrong.now=16100;wrong_way.update(score,wrong);check(!wrong_way.reset_requested(),"wrong-way reset strict boundary");
    wrong.now=16101;wrong_way.update(score,wrong);check(wrong_way.reset_requested(),"wrong-way reset after four seconds");
    std::cout<<"drift state: "<<count<<" checks, "<<fail<<" failures\n";return fail?1:0;
}
