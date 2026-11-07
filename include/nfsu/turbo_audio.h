#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <cstddef>
#include "nfsu/traffic_audio.h"
namespace nfsu::audio {
// Original528B60/528830/528470. Pressure is52C3F0-normalized engine+A4;
// PCM completion and the preceding category publication are explicit inputs.
struct TurboSoundTuning {int spool_gain=0,release_gain=0;float capacity=0;};
TurboSoundTuning turbo_sound_tuning(const std::uint8_t*,std::size_t);
std::string turbo_sound_bank(int induction,int variant=-1);
struct TurboSoundInput {
 float pressure=0,rpm=0,load=0,dt=1.F/60.F;
 int pan=0,balance=0,category=16358,position=11175;
 bool paused=false,release_complete=false;
};
struct TurboSoundOutput {std::array<std::int32_t,6> spool{},release{};bool start=false,blowoff=false;};
class TurboSoundController {
public:
 void configure(TurboSoundTuning tuning)noexcept {tuning_=tuning;reset();}
 void reset()noexcept;
 TurboSoundOutput tick(const TurboSoundInput&,TrafficAudioRandom&)noexcept;
 // Remote524130 crosses audible load20;523C30 supplies retained latch pressure.
 TurboSoundOutput tick_remote(const TurboSoundInput&,TrafficAudioRandom&)noexcept;
 float charge()const noexcept{return charge_;}
 float normalized()const noexcept{return normalized_;}
 float pressure_mean()const noexcept{return mean_;}
 unsigned state()const noexcept{return state_;}
private:
 void spool(const TurboSoundInput&,TurboSoundOutput&)noexcept;
 TurboSoundTuning tuning_;
 std::array<std::int32_t,6> spool_parameters_{};
 std::array<float,3> history_{};
 float charge_=0,normalized_=0,previous_=0,sum_=0,mean_=0,elapsed_=0,fade_=0;
 unsigned cursor_=0,state_=0,leg_=0;
 bool started_=false,fading_=false,done_=false;
};
}
