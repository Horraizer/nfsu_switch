#include "nfsu/turbo_audio.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace nfsu::audio {
namespace {
#include "engine_balance.inc"
int product(int a,int b)noexcept {const auto p=std::uint32_t(a)*std::uint32_t(b);return int(p>>15)-(p&0x80000000U?131072:0);}
int gain(int raw,const TurboSoundInput& in)noexcept {return in.paused?0:product(product(raw,in.position),in.category);}
}
TurboSoundTuning turbo_sound_tuning(const std::uint8_t* bytes,std::size_t size) {
 if(!bytes||size<20)throw std::runtime_error("turbo sound tuning unavailable");
 const auto gain=[&](unsigned at){return int(static_cast<std::int16_t>(unsigned(bytes[at])|(unsigned(bytes[at+1])<<8)));};
 const std::uint32_t bits=std::uint32_t(bytes[16])|(std::uint32_t(bytes[17])<<8)|(std::uint32_t(bytes[18])<<16)|(std::uint32_t(bytes[19])<<24);
 float capacity=0;std::memcpy(&capacity,&bits,4);
 if(!std::isfinite(capacity)||capacity<=0)throw std::runtime_error("invalid turbo sound capacity");
 return {gain(0),gain(4),capacity};
}
std::string turbo_sound_bank(int induction,int variant) {
 if(induction<0||induction>3)throw std::runtime_error("invalid turbo sound upgrade");
 static constexpr const char* levels[]={"00","00","01","02"};
 static constexpr const char* variants[]={"","A","B","C"};
 return std::string("Sound/TURBO/TURBO_")+levels[induction]+variants[std::clamp(variant,0,3)]+"_MB.abk";
}
void TurboSoundController::reset()noexcept {spool_parameters_={};history_={};charge_=normalized_=previous_=sum_=mean_=elapsed_=fade_=0;cursor_=state_=leg_=0;started_=fading_=done_=false;}
void TurboSoundController::spool(const TurboSoundInput& in,TurboSoundOutput& out)noexcept {
 if(!started_){started_=true;out.start=true;out.spool={0,0,0,0,0,std::clamp(int(in.rpm),0,10000)};spool_parameters_=out.spool;return;}
 if(!in.paused)charge_+=in.pressure>.01F?in.pressure:-.5F;
 charge_=std::clamp(charge_,0.F,tuning_.capacity);
 const float u=std::clamp(float(int(in.rpm)-1000)*.01F,0.F,1.F);
 const float ceiling=((1.F-.4F)*u+.4F)*tuning_.capacity;
 charge_=std::clamp(charge_,0.F,ceiling);
 if(charge_>=ceiling&&!fading_){elapsed_=fade_=0;leg_=0;done_=false;fading_=true;}
 else if(charge_!=tuning_.capacity&&fading_)fading_=false;
 normalized_=charge_/tuning_.capacity;
 int volume=gain(product(int(normalized_*32767.F),tuning_.spool_gain),in);
 if(fading_){
  if(!done_){elapsed_+=in.dt;const float duration=leg_==0?1.F:2.F;
   if(elapsed_>duration){fade_=leg_==0?0.F:.6F*512.F;if(leg_==0){elapsed_-=duration;++leg_;}else done_=true;}
   else fade_=(elapsed_/duration)*(leg_==0?0.F:.6F*512.F);
  }
  volume=product(volume,kEngineBalance[std::clamp(int(fade_),0,512)]);
 }
 out.spool={0,std::clamp(volume,0,32767),std::clamp(int(normalized_*1024.F),0,8192),std::clamp(in.pan,0,65535),std::clamp(in.balance,0,1024),spool_parameters_[5]};spool_parameters_=out.spool;
}
TurboSoundOutput TurboSoundController::tick_remote(const TurboSoundInput& in,TrafficAudioRandom& random)noexcept {
 TurboSoundOutput out;
 if(!(tuning_.capacity>0)||!std::isfinite(in.pressure)||!std::isfinite(in.rpm)||
    !std::isfinite(in.load)||!std::isfinite(in.dt)||in.dt<0)return out;
 spool(in,out);
 if(state_==0){if(in.load>20.F)state_=1;}
 else if(state_==1&&in.load<20.F){
  state_=2;out.blowoff=true;
  const int mode=normalized_>.75F?2+int(random.integer(2)):1;
  //528470 derives the release gain and RPM from the current sound owner.
  out.release={mode,std::clamp(gain(int(float(tuning_.release_gain)*normalized_),in),0,32767),4095,
      std::clamp(in.pan,0,65535),std::clamp(in.balance,0,1024),std::clamp(int(in.rpm),0,10000)};
  charge_=0;spool(in,out);
 }else if(state_==2&&in.release_complete)state_=0;
 return out;
}
TurboSoundOutput TurboSoundController::tick(const TurboSoundInput& in,TrafficAudioRandom& random)noexcept {
 TurboSoundOutput out;
 if(!(tuning_.capacity>0)||!std::isfinite(in.pressure)||!std::isfinite(in.rpm)||!std::isfinite(in.dt)||in.dt<0)return out;
 const float difference=previous_-in.pressure;previous_=in.pressure;
 sum_+=difference;sum_-=history_[cursor_];history_[cursor_]=difference;cursor_=(cursor_+1)%3;mean_=sum_/3.F;
 spool(in,out);
 if(state_==0){if(in.load>20.F)state_=1;}
 else if(state_==1&&mean_>.2F){
  state_=2;out.blowoff=true;
  const int mode=normalized_>.75F?2+int(random.integer(2)):1;
  out.release={mode,std::clamp(gain(int(float(tuning_.release_gain)*normalized_),in),0,32767),4095,std::clamp(in.pan,0,65535),std::clamp(in.balance,0,1024),std::clamp(int(in.rpm),0,10000)};
  charge_=0;spool(in,out);
 }else if(state_==2&&in.release_complete)state_=0;
 return out;
}
}
