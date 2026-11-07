#include "nfsu/collision_audio.h"
#include <iostream>
using namespace nfsu::audio;
#include "collision_voice_pool_native.inc"
#include "collision_admission_native.inc"
int main(){
    for(const auto& c:kNativeCollisionAdmissions)if(collision_world_admitted(c.current,c.previous,c.point,c.attached.data(),c.attached.size())!=c.admitted){
        std::cerr<<"FAIL original impact record admission\n";return 1;}

    for(const auto& c:kNativeCollisionQueues){const auto got=collision_impact_queue(c.pending,c.active);
        if(got.size()!=c.calls.size()){std::cerr<<"FAIL collision queue count\n";return 1;}
        for(unsigned i=0;i<got.size();++i)if(got[i].start!=c.calls[i].start||got[i].id!=c.calls[i].id){std::cerr<<"FAIL collision queue dispatch order\n";return 1;}}
    BankAudioRandom random;CollisionScheduler scheduler;scheduler.set_random_context(&random);CollisionGraph graph;
    CollisionContact contact;contact.step=1;contact.pair=7;contact.other_kind=0;contact.impulse={1,0,0};
    const auto before=random.state();auto frame=scheduler.update(1,1,&contact,1,graph,false,32768,nullptr,true);
    if(frame.impacts.size()!=1||random.state()!=before||!frame.impacts[0].start){std::cerr<<"FAIL pre-admission sound side effects\n";return 1;}
    if(!scheduler.start_impact(frame.impacts[0])||random.state()==before||scheduler.start_impact(frame.impacts[0])){std::cerr<<"FAIL actual-start side effects\n";return 1;}
    CollisionScheduler wall;std::array<CollisionContact,6> panels{};
    for(unsigned j=0;j<panels.size();++j){auto& c=panels[j];c.step=1;c.pair=j+10;c.impulse={0,1,0};c.slide_mps=20;}
    const auto batch=wall.update(1,1,panels.data(),panels.size(),graph,false,32768,nullptr,true);
    if(batch.impacts.size()!=1||batch.scrapes.size()!=6){std::cerr<<"FAIL wall admission suppresses competing impacts without muting scrapes\n";return 1;}
    std::cout<<"PASS full original collision queue cases and deferred actual-start RNG\n";
}
