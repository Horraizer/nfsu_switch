#include "nfsu/frame_spike_trace.h"
#include "nfsu/gpu_scene_profile.h"
#include <iostream>
int main(){
    unsigned failures=0;
    const auto check=[&](bool value){if(!value)++failures;};
    nfsu::FrameSpikeTrace<2> trace;
    nfsu::FrameSpikeRow row;row.frame=1;row.tick=10;row.times[0]=16;trace.add(row);
    trace.gpu(1,11);
    row.frame=2;row.times[0]=25;trace.add(row);
    row.frame=3;row.times[0]=16;trace.add(row);
    trace.gpu(2,13);trace.gpu(3,12);
    check(trace.count==1&&trace.events[0].rows[0].gpu_ms==11);
    check(trace.events[0].rows[1].gpu_ms==13&&trace.events[0].rows[2].gpu_ms==12);
    check(trace.events[0].rows[1].tick==trace.events[0].rows[2].tick);
    row.frame=4;row.times[0]=20;trace.add(row);check(trace.count==1);
    row.frame=5;row.times[1]=21;trace.add(row);check(trace.count==2);
    row.frame=6;trace.add(row);check(trace.dropped==1&&trace.events[1].rows[2].frame==6);
    trace.gpu(99,99);check(trace.events[0].rows[1].gpu_ms==13);
    trace.gpu(2,-1);check(trace.events[0].rows[1].gpu_ms==13);
    nfsu::DekoGpuProfile gpu;
    nfsu::DekoSceneFrame frame;frame.active=true;frame.tick=10;frame.diagnostic_id=2;
    gpu.add_scene(9,frame);frame.diagnostic_id=3;gpu.add_scene(10,frame);
    check(gpu.recent[0].frame==2&&gpu.recent[1].frame==3&&gpu.last_tick==10);
    trace={};check(trace.count==0&&trace.previous.frame==0);
    for(unsigned i=1;i<=12;++i){frame.diagnostic_id=i;gpu.add_scene(i,frame);}
    check(gpu.recent[(gpu.recent_cursor+7)%8].frame==12);
    std::cout<<(failures?"FAIL":"PASS")<<" spike context, bounded overflow, delayed GPU identity, reset failures="<<failures<<"\n";
    return failures?1:0;
}
