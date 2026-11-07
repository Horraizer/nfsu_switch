#pragma once
#include "nfsu/player_powertrain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace nfsu::audio {
struct TrafficAudioRandom {
    std::uint32_t state=0x12345678;
    float last_horn=0;
    unsigned integer(unsigned n)noexcept {
        const unsigned value=state%n;
        const auto a=state^0x1d872b41U,b=(a>>5)^a;
        state=(b<<27)^b^a;return value;
    }
    float real(float extent)noexcept {
        const auto value=integer(0x7fffffffU);
        return (float(value)*0x1p-31F)*extent;
    }
};
struct TrafficAudioInput {
    std::array<float,3> position{},velocity{},human_position{},human_velocity{},human_forward{{1,0,0}};
    float human_speed=0,dt=1.F/60.F,doppler=1.F;
    int car_type=20,gain=0,pan=32768,category=32500,global_gain=30000;
    bool paused=false,drag=false,pass_complete=false;
};
struct TrafficAudioPosition {
    int gain=0,pan=0;float doppler=1,previous_distance=0;
    void tick(const std::array<float,3>& source,const std::array<float,3>& human,
              const std::array<float,3>& eye,const std::array<float,3>& forward,
              float milliseconds)noexcept;
};
// World/car collision profile6F47A0: near10, quadrant far80/30/30/30.
// Actor-distance attenuation and camera-angle pan are separate inputs.
struct CollisionAudioPosition {
    int gain=0,pan=0;
    bool tick(const std::array<float,3>& point,const std::array<float,3>& actor,
              const std::array<float,3>& eye,const std::array<float,3>& forward)noexcept;
};
// Turbo child profile6F42D0: camera distance, near1, far25/15/25/15.
struct TurboAudioPosition {
    int gain=0,pan=0;
    void tick(const std::array<float,3>& source,const std::array<float,3>& eye,
              const std::array<float,3>& forward)noexcept;
};
struct RemoteAudioPosition {
    int gain=0,pan=0;float doppler=1,previous_distance=0;
    void tick(const std::array<float,3>& source,const std::array<float,3>& human,
              const std::array<float,3>& eye,const std::array<float,3>& forward,
              float milliseconds)noexcept;
};
struct TrafficAudioVoice {
    unsigned event=0,gain=0,pitch=4096,pan=0,flags=0;
    bool live=false,start=false,stop=false;
};
// Original5251E0 and its horn/pass-by helpers. Spatial publication and PCM
// completion are inputs; allocator success is guaranteed by fixed voice slots.
struct TrafficAudioController {
    float time=0,last_horn_end=0,last_denied=0,last_pass=0,horn_start=0,duration=0;
    float fade_elapsed=0,fade_value=1;
    bool horn_requested=false,forced=false,fading=false,horn_allowed=true;
    TrafficAudioVoice horn,pass;
    static bool large_horn(int type)noexcept {
        return type==22||type==24||type==26||type==27||type==30||type==31||type==32;
    }
    static std::array<float,3> normalized(std::array<float,3> v)noexcept {
        const float size=std::sqrt((v[2]*v[2]+v[0]*v[0])+v[1]*v[1]);
        if(size==0)return {{1,0,0}};
        const float inverse=1.F/size;for(auto& x:v)x*=inverse;return v;
    }
    static bool horn_gate(const TrafficAudioInput& in)noexcept {
        std::array<float,3> delta{};
        for(unsigned i=0;i<3;++i)delta[i]=in.human_position[i]-in.position[i];
        if(std::sqrt((delta[2]*delta[2]+delta[0]*delta[0])+delta[1]*delta[1])>30.F)return false;
        delta[2]=0;delta=normalized(delta);
        auto velocity=in.velocity;velocity[2]=0;velocity=normalized(velocity);
        const float dot=(velocity[1]*delta[1]+velocity[0]*delta[0])+velocity[2]*delta[2];
        if(dot<player_angle_sin(in.drag?0x91c7:0x5c71))return false;
        const auto heading=normalized(in.human_forward);
        const float facing=(heading[1]*velocity[1]+heading[0]*velocity[0])+heading[2]*velocity[2];
        return in.drag||!(player_angle_sin(0x4e38)<facing);
    }
    void tick(const TrafficAudioInput& in,TrafficAudioRandom& rng)noexcept {
        horn.start=horn.stop=pass.start=pass.stop=false;
        const int category_gain=in.paused?0:in.category;
        if(!in.paused)time+=in.dt;
        const float vx=in.human_velocity[0]-in.velocity[0],vy=in.human_velocity[1]-in.velocity[1];
        const float relative=std::sqrt(vx*vx+vy*vy);
        if(!pass.live) {
            std::array<float,3> d{};for(unsigned i=0;i<3;++i)d[i]=in.human_position[i]-in.position[i];
            const float distance=std::sqrt((d[2]*d[2]+d[0]*d[0])+d[1]*d[1]);
            if(distance<5.F&&relative>25.F&&last_pass+3.F<time) {
                const float factor=std::clamp((relative-25.F)/75.F,0.F,1.F);
                const int raw=12000+int(13000.F*factor);
                const int gain=int((std::int64_t(raw)*in.gain)>>15);
                const int category=int((std::int64_t(gain)*category_gain)>>15);
                (void)rng.integer(3); //525480 consumes this draw before allocation.
                pass={rng.integer(3),unsigned(std::clamp(int((std::int64_t(category)*in.global_gain)>>15),0,32767)),
                    unsigned(std::clamp(3400-int(-1100.F*factor),0,8096)),unsigned(std::clamp(in.pan,0,65535)),0,true,true,false};
                last_pass=time;
            }
        }else if(relative<=25.F||in.pass_complete) {pass={};pass.stop=true;}
        else if(in.paused)pass.gain=0;
        if(fading) {
            fade_elapsed+=in.dt;
            fade_value=fade_elapsed>0.15F?0.F:1.F-(fade_elapsed/0.15F);
        }
        if(!horn.live) {
            if(last_horn_end+3.F<time&&last_denied+3.F<time&&horn_allowed&&in.human_speed>25.F&&horn_gate(in)) {
                const float draw=rng.real(1.F);
                if(draw<0.3F||rng.last_horn+5.F<time) {
                    const unsigned event=large_horn(in.car_type)?7:rng.integer(2)+5;
                    horn={event,0,4096,0,0,true,true,false};rng.last_horn=time;horn_start=time;
                    fade_elapsed=0;fade_value=1;fading=false;horn_requested=true;
                    duration=1.2F+rng.real(2.8F);
                }else last_denied=time;
            }
        }else if((time<horn_start+duration&&horn_requested)||forced||fading) {
            const int raw=int(fade_value*32500.F);
            const int gain=int((std::int64_t(raw)*in.gain)>>15);
            horn.gain=unsigned(std::clamp(int((std::int64_t(gain)*category_gain)>>15),0,32767));
            horn.pan=unsigned(std::clamp(in.pan,0,65535));horn.flags=0;
            horn.pitch=unsigned(std::clamp(int(in.doppler*4096.F),0,8096));
        }else {fading=true;fade_elapsed=0;fade_value=1;}
        if(fading&&fade_elapsed>0.15F) {
            horn={};horn.stop=true;forced=horn_requested=fading=false;last_horn_end=time;
        }
    }
};
}
