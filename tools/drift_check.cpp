#include "nfsu/drift.h"
#include <cstdio>
#include <cmath>
#include <cstring>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto* f=std::fopen(argv[1],"r");if(!f)return 2;
    char kind;unsigned count=0,fail=0;
    while(std::fscanf(f," %c",&kind)==1) {
        if(kind=='S') {
            nfsu::DriftScoreSample s;float high,expected,after;int blocked,phase;
            if(std::fscanf(f,"%f %f %u %f %f %f %d %d %f %f",&s.speed,&s.slip,&s.grade,&s.yaw,&s.progress,&high,&blocked,&phase,&expected,&after)!=10)return 2;
            s.blocked=blocked!=0;s.phase=unsigned(phase);
            auto value=nfsu::drift_score_factor(s,high);
            if(value!=expected||high!=after){std::printf("FAIL score %u %.9g %.9g\n",count,value,expected);++fail;}
        }else if(kind=='F') {
            unsigned word,difficulty;float yaw,split,torque,grip;
            if(std::fscanf(f,"%u %f %f %u %f %f",&word,&yaw,&split,&difficulty,&torque,&grip)!=6)return 2;
            auto got=nfsu::drift_forces(std::uint16_t(word),yaw,split,difficulty);
            if(got.torque!=torque||got.wheel_scale!=grip){std::printf("FAIL forces %u torque %.9g %.9g grip %.9g %.9g\n",count,got.torque,torque,got.wheel_scale,grip);++fail;}
        }else return 2;
        ++count;
    }
    std::fclose(f);std::printf("drift differential: %u cases, %u failures\n",count,fail);
    return count&&fail==0?0:1;
}
