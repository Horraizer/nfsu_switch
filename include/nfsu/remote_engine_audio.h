#pragma once
namespace nfsu::audio {
struct TrafficAudioRandom;
// Original524240 authored remote rev scripts. RNG owners are supplied.
struct RemoteRevScript {
    float total=0,wait=0,elapsed=0,rpm=0,load=0;
    unsigned pattern=0,cursor=0,from=0,to=0;
    bool active=false,returned=false;
    bool tick(int game,float remaining,float physical,float torque,float dt,
              TrafficAudioRandom& sound,TrafficAudioRandom& world)noexcept;
};
}
