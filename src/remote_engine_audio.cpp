#include "nfsu/remote_engine_audio.h"
#include "nfsu/traffic_audio.h"
#include <algorithm>
#include <cmath>
namespace nfsu::audio {
namespace {
static constexpr float kRemoteRevScript[2][31][3]={
    {
        {0x0.0p+0F,0x1.d838520000000p+9F,0x1.08f5c20000000p+1F},
        {0x1.0a3d700000000p-3F,0x1.166a3e0000000p+10F,0x1.17d70a0000000p+4F},
        {0x1.47ae140000000p-3F,0x1.4223340000000p+10F,0x1.8170a40000000p+4F},
        {0x1.47ae140000000p-2F,0x1.47870a0000000p+11F,0x1.e0cccc0000000p+5F},
        {0x1.19999a0000000p-1F,0x1.8e2ee20000000p+12F,0x1.67147a0000000p+6F},
        {0x1.5c28f60000000p-1F,0x1.10127a0000000p+13F,0x1.7f33340000000p+6F},
        {0x1.6b851e0000000p-1F,0x1.1b95340000000p+13F,0x1.7d66660000000p+6F},
        {0x1.8f5c280000000p-1F,0x1.31ed700000000p+13F,0x1.715c280000000p+6F},
        {0x1.a8f5c20000000p-1F,0x1.2f703e0000000p+13F,0x1.6570a40000000p+6F},
        {0x1.d1eb860000000p-1F,0x1.332dae0000000p+13F,0x1.3733340000000p+6F},
        {0x1.170a3e0000000p+0F,0x1.20c6cc0000000p+13F,0x1.ab1eb80000000p+5F},
        {0x1.1c28f60000000p+0F,0x1.1eae7a0000000p+13F,0x1.83999a0000000p+5F},
        {0x1.7851ec0000000p+0F,0x1.e4751e0000000p+12F,0x1.b30a3e0000000p+4F},
        {0x1.b5c2900000000p+0F,0x1.b0551e0000000p+12F,0x1.d6b8520000000p+4F},
        {0x1.dc28f60000000p+0F,0x1.9226e20000000p+12F,0x1.e000000000000p+4F},
        {0x1.e3d70a0000000p+0F,0x1.8ec4cc0000000p+12F,0x1.eccccc0000000p+4F},
        {0x1.028f5c0000000p+1F,0x1.8e86900000000p+12F,0x1.23999a0000000p+5F},
        {0x1.1333340000000p+1F,0x1.b0d7d80000000p+12F,0x1.76cccc0000000p+5F},
        {0x1.2b851e0000000p+1F,0x1.0c43480000000p+13F,0x1.00ae140000000p+6F},
        {0x1.4000000000000p+1F,0x1.2101480000000p+13F,0x1.f947ae0000000p+5F},
        {0x1.51eb860000000p+1F,0x1.1646140000000p+13F,0x1.82b8520000000p+5F},
        {0x1.6000000000000p+1F,0x1.016b700000000p+13F,0x1.eb5c280000000p+4F},
        {0x1.747ae20000000p+1F,0x1.bd36b80000000p+12F,0x1.2028f60000000p+4F},
        {0x1.8e147a0000000p+1F,0x1.6228280000000p+12F,0x1.f9eb860000000p+3F},
        {0x1.a7ae140000000p+1F,0x1.0f4a660000000p+12F,0x1.1e66660000000p+4F},
        {0x1.c28f5c0000000p+1F,0x1.aaba3e0000000p+11F,0x1.6eb8520000000p+4F},
        {0x1.d851ec0000000p+1F,0x1.651c280000000p+11F,0x1.adc2900000000p+4F},
        {0x1.eccccc0000000p+1F,0x1.cc5ccc0000000p+10F,0x1.5ae1480000000p+4F},
        {0x1.03d70a0000000p+2F,0x1.d838520000000p+9F,0x1.f5c2900000000p+2F},
        {0x1.0851ec0000000p+2F,0x1.d838520000000p+9F,0x1.30a3d80000000p+2F},
        {0x0.0p+0F,0x0.0p+0F,0x0.0p+0F},
    },
    {
        {0x0.0p+0F,0x1.d838520000000p+9F,0x1.5c28f60000000p-2F},
        {0x1.c28f5c0000000p-3F,0x1.d838520000000p+9F,0x1.8051ec0000000p+3F},
        {0x1.99999a0000000p-2F,0x1.61cd700000000p+10F,0x1.15c2900000000p+5F},
        {0x1.3851ec0000000p-1F,0x1.5d78520000000p+11F,0x1.0b28f60000000p+6F},
        {0x1.75c2900000000p-1F,0x1.da747a0000000p+11F,0x1.12a3d80000000p+6F},
        {0x1.c7ae140000000p-1F,0x1.2c6cf60000000p+12F,0x1.0bae140000000p+6F},
        {0x1.1eb8520000000p+0F,0x1.4a3dec0000000p+12F,0x1.b733340000000p+5F},
        {0x1.4000000000000p+0F,0x1.726dec0000000p+12F,0x1.a4cccc0000000p+5F},
        {0x1.5c28f60000000p+0F,0x1.8e24000000000p+12F,0x1.b4a3d80000000p+5F},
        {0x1.87ae140000000p+0F,0x1.7789700000000p+12F,0x1.6e7ae20000000p+5F},
        {0x1.b851ec0000000p+0F,0x1.2b1c280000000p+12F,0x1.b51eb80000000p+4F},
        {0x1.eb851e0000000p+0F,0x1.de02900000000p+11F,0x1.66e1480000000p+4F},
        {0x1.15c2900000000p+1F,0x1.ba10520000000p+11F,0x1.1f851e0000000p+5F},
        {0x1.2e147a0000000p+1F,0x1.0476900000000p+12F,0x1.7acccc0000000p+5F},
        {0x1.428f5c0000000p+1F,0x1.5e66140000000p+12F,0x1.eb33340000000p+5F},
        {0x1.5333340000000p+1F,0x1.d68d480000000p+12F,0x1.2ab8520000000p+6F},
        {0x1.75c2900000000p+1F,0x1.302fd80000000p+13F,0x1.81cccc0000000p+6F},
        {0x1.8000000000000p+1F,0x1.26a0280000000p+13F,0x1.8e851e0000000p+6F},
        {0x1.8e147a0000000p+1F,0x1.312b860000000p+13F,0x1.8eeb860000000p+6F},
        {0x1.947ae20000000p+1F,0x1.2d7b700000000p+13F,0x1.8ae1480000000p+6F},
        {0x1.c51eb80000000p+1F,0x1.2c71860000000p+13F,0x1.aae1480000000p+5F},
        {0x1.e666660000000p+1F,0x1.11c5d80000000p+13F,0x1.e70a3e0000000p+4F},
        {0x1.0666660000000p+2F,0x1.e4a8a40000000p+12F,0x1.d1999a0000000p+4F},
        {0x1.1d70a40000000p+2F,0x1.8db7340000000p+12F,0x1.c5c2900000000p+4F},
        {0x1.2e147a0000000p+2F,0x1.6f4ee20000000p+12F,0x1.1e7ae20000000p+5F},
        {0x1.37ae140000000p+2F,0x1.9a630a0000000p+12F,0x1.75ae140000000p+5F},
        {0x1.41eb860000000p+2F,0x1.dc00cc0000000p+12F,0x1.d7d70a0000000p+5F},
        {0x1.4ae1480000000p+2F,0x1.de26660000000p+12F,0x1.bc28f60000000p+5F},
        {0x1.5666660000000p+2F,0x1.965bd80000000p+12F,0x1.cf33340000000p+4F},
        {0x1.6333340000000p+2F,0x1.03159a0000000p+12F,0x1.7851ec0000000p+2F},
        {0x1.80a3d80000000p+2F,0x1.d838520000000p+9F,0x0.0p+0F},
    },
};
}
bool RemoteRevScript::tick(int game,float remaining,float physical,float torque,float dt,
    TrafficAudioRandom& sound,TrafficAudioRandom& world)noexcept {
        if(game!=2) {
            if(game!=3||remaining==1.F||returned)return false;
            if(remaining<1.F) {
                const auto approach=[](float value,float goal,float step){
                    return goal>value+step?value+step:goal<value-step?value-step:goal;};
                rpm=approach(rpm,physical,300.F);load=approach(load,torque,10.F);
                if(std::abs(rpm-physical)<400.F)returned=true;
                return true;
            }
        }
        total+=dt;
        if(!active) {
            wait-=dt;
            if(wait<0.F){active=true;elapsed=0;pattern=sound.integer(2);cursor=1;from=0;to=1;}
        }else {
            elapsed+=dt;
            if(kRemoteRevScript[pattern][to][0]<elapsed) {
                from=to;++to;++cursor;
                if(cursor>30){active=false;wait=world.real(1.3F)/std::clamp(total/2.1F,1.F,3.F)+0.5F;}
            }
        }
        if(active) {
            const auto* a=kRemoteRevScript[pattern][from];const auto* b=kRemoteRevScript[pattern][to];
            float end=b[0];
            if(std::abs(end-a[0])<0x1.0c6f7a0000000p-20F)end+=0x1.0c6f7a0000000p-20F;
            const float ratio=std::clamp((elapsed-a[0])/(end-a[0]),0.F,1.F);
            rpm=(b[1]-a[1])*ratio+a[1];load=(b[2]-a[2])*ratio+a[2];
        }
        return true;
    }
}
