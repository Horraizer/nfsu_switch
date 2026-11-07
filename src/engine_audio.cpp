#include "nfsu/engine_audio.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/traffic_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace nfsu::audio {
namespace {
#include "engine_balance.inc"
[[noreturn]] void bad() { throw std::runtime_error("unsupported engine audio graph"); }
std::uint32_t word(const std::vector<std::uint8_t>& b, unsigned at) {
    if (at>b.size() || b.size()-at<4) bad();
    return std::uint32_t(b[at]) | (std::uint32_t(b[at+1])<<8) |
        (std::uint32_t(b[at+2])<<16) | (std::uint32_t(b[at+3])<<24);
}
std::int32_t rounded(float f) {
    if (!std::isfinite(f) || double(f)<-2147483648.0 || double(f)>=2147483648.0) bad();
    return static_cast<std::int32_t>(std::nearbyint(double(f)));
}
}

float engine_audio_rpm(float rpm,float maximum)noexcept {
    if(!std::isfinite(rpm)||!std::isfinite(maximum)||rpm<0||maximum<=0)return 0;
    return (rpm/maximum)*10000.0F;
}

std::array<std::uint32_t,2> engine_audio_parameters(float rpm,float load) noexcept {
    if(!std::isfinite(rpm)||!std::isfinite(load))return {};
    //523722/52377E: game-thread binary32 multiplication precedes truncation.
    return {std::uint32_t(std::clamp(rpm*0.1024F,0.0F,1024.0F)),
            std::uint32_t(std::clamp(load*10.24F,0.0F,1024.0F))};
}
std::array<std::int32_t,2> remote_engine_audio_gains(int a,int b,int balance,float boost,
    float blend,int offset,float cross) noexcept {
    balance=std::clamp(balance,0,1024);
    if(!std::isfinite(boost))boost=0;
    const auto accent=[&](int gain){return gain+int(float(gain)*boost)+offset;};
    const auto scaled=[](int gain,std::int32_t factor){
        const std::uint32_t product=std::uint32_t(gain)*std::uint32_t(factor);
        return std::int32_t(product>>15)-(product&0x80000000U?131072:0);
    };
    auto first=scaled(accent(a),kEngineBalance[(1024-balance)/2]);
    const auto second=scaled(accent(b),kEngineBalance[balance/2]);
    //523673: the selected cross-channel boost follows the authored balance.
    if(blend!=0&&second>first)first+=int((float(second-first)*cross)*blend);
    return {first,second};
}
std::array<unsigned,2> engine_audio_gains(int a,int b,int balance,float boost)noexcept {
    const auto gains=remote_engine_audio_gains(a,b,balance,boost);
    return {{unsigned(std::clamp(gains[0],0,32767)),unsigned(std::clamp(gains[1],0,32767))}};
}
EngineEnvelopeTuning engine_envelope_tuning(const std::uint8_t* data,std::size_t size) {
    if(!data||size<0x52)throw std::runtime_error("engine envelope tuning unavailable");
    const auto integer=[&](unsigned at){return int(static_cast<std::int16_t>(
        unsigned(data[at])|(unsigned(data[at+1])<<8)));};
    const auto real=[&](unsigned at){const std::uint32_t word=std::uint32_t(data[at])|
        (std::uint32_t(data[at+1])<<8)|(std::uint32_t(data[at+2])<<16)|(std::uint32_t(data[at+3])<<24);
        float value=0;std::memcpy(&value,&word,4);
        if(!std::isfinite(value))throw std::runtime_error("nonfinite engine envelope tuning");
        return value;};
    return {integer(0x18),integer(0x1a),integer(0x1c),real(0x20),
        integer(0x2c),integer(0x2e),integer(0x30),integer(0x32),real(0x34),
        integer(0x14),integer(0x16),integer(0x28),integer(0x2a),real(0x24),real(0x38),integer(8),
        real(0x40),real(0x44),real(0x48),real(0x4c),integer(0x50)};
}
void EngineEnvelope::Curve::start(float a,float b,int ms,unsigned shape) noexcept {
    elapsed=0;duration=float(ms)*0.001F;if(duration<=0)duration=0.01F;
    from=a;to=b;value=a;mode=shape;done=false;
}
void EngineEnvelope::Curve::advance(float dt) noexcept {
    if(done)return;
    elapsed+=dt;
    if(elapsed>duration){value=to;done=true;return;}
    if(mode==1)return;
    const float difference=to-from;
    if(mode==3){const float u=((1.0F/duration)*elapsed)-1.0F;
        value=difference*(1.0F-u*u)+from;}
    else if(mode==2){const float u=elapsed/duration;value=((difference*u)*u)+from;}
    else value=(elapsed/duration)*difference+from;
}
void EngineEnvelope::GainProfile::rise(float from,const EngineEnvelopeTuning& t,const EngineEnvelopeInput& in)noexcept {
    mode=1;
    float ratio=std::abs(in.gear==1?in.first_gear_ratio:in.gear_ratio);
    if(ratio<.01F)ratio=in.first_gear_ratio;
    const float duration=(t.profile_rise_factor/ratio)*1000.F;
    curve.start(from,1,int(duration-duration*from));
}
void EngineEnvelope::GainProfile::up_done(float rpm,int gear)noexcept {
    if(mode!=2&&rpm>=(gear<4?5000.F:6000.F)){mode=2;curve.start(blend,blend,0);}
}
void EngineEnvelope::GainProfile::down_done()noexcept {
    if(mode!=5&&blend<=.65F){mode=5;curve.start(blend,.65F,215);}
}
void EngineEnvelope::GainProfile::tick(const EngineEnvelopeTuning& t,const EngineEnvelopeInput& in,
    float rpm,float now,float last_off,bool on)noexcept {
    // State0 deliberately returns before5326F0 and the18DC/18E4 publication.
    if(mode==0){rise(blend,t,in);return;}
    const float threshold=in.gear<4?5000.F:6000.F;
    if(mode==2)rise(std::clamp(blend-t.profile_shift_drop,0.F,1.F),t,in);
    else if(mode==5){if(curve.done)mode=0;}
    else if(mode!=3&&(rpm<threshold||(!on&&last_off+.5F<now))){
        mode=3;const float from=std::clamp(blend>.7F?blend-.1F:blend,0.F,1.F);
        curve.start(from,0,int(float(t.profile_fall_ms)*from));
    }else if(mode!=1&&rpm>=threshold&&on)rise(blend,t,in);
    curve.advance(in.dt);blend=curve.value;offset=int(blend*t.profile_gain_offset);
}
void EngineEnvelope::Average::fill(float input) noexcept {
    values.fill(input);sum=10.0F*input;value=input;count=10;
    //584310 intentionally retains the ring cursor.
}
float EngineEnvelope::Average::push(float input) noexcept {
    if(count<10)++count;
    sum+=input;sum-=values[cursor];values[cursor]=input;cursor=(cursor+1)%10;
    value=sum/10.0F;
    //584210 recomputes in physical ring order before publishing its mean.
    sum=0;for(float item:values)sum+=item;
    value=(1.0F/float(count))*sum;return value;
}
void EngineEnvelope::reset() noexcept {
    rpm_curve_={};load_curve_={};gain_curve_={};average_={};rpm_=physical_=torque_=load_=0;
    on_timer_=off_timer_=shift_rpm_=time_=last_off_=0;profile_={};gear_=0;shift_=0;initialized_=on_=false;
}
void EngineEnvelope::input(const EngineEnvelopeInput& in) noexcept {
    if(in.game_state==2||in.game_state==3){on_=load_>30.0F;if(!on_)last_off_=time_;}
    else {
        const bool requested=in.game_state==4?
            !(in.brake>=0.5F||in.handbrake):in.throttle*100.0F>30.0F;
        if(requested){on_timer_-=in.dt;off_timer_=0.5F;if(!on_&&on_timer_<0)on_=true;}
        else {off_timer_-=in.dt;on_timer_=0.5F;if(on_&&off_timer_<0){on_=false;last_off_=time_;}}
    }
    // Remote5234C0 overrides the divisor with160, retaining the original
    // gearbox+70 publication units. Do not substitute physical crankshaft Nm.
    torque_=(std::clamp(in.drive_load,0.0F,160.0F)/160.0F)*100.0F;
    physical_=engine_audio_rpm(in.rpm,in.maximum_rpm);
}
EngineEnvelopeOutput EngineEnvelope::tick(const EngineEnvelopeInput& in,RemoteEngineContext* context) noexcept {
    if(!std::isfinite(in.dt)||in.dt<=0||!std::isfinite(in.drive_load)||
       !std::isfinite(in.throttle)||!std::isfinite(in.brake))return {rpm_,load_,shift_,gain_curve_.value};
    time_+=in.dt;
    unsigned shift_cue=0,disengage=0;std::int32_t shift_gain=0;
    if(!initialized_){initialized_=true;gear_=in.gear;rpm_=in.rpm;}
    else if(in.gear!=gear_) {
        //523990 publishes529100 immediately at the preceding audible RPM.
        //523A70 has no corresponding downchange publication or human delay.
        shift_rpm_=rpm_;
        if(in.gear>gear_){shift_cue=1;shift_gain=human_shift_audio_gain(std::int16_t(tuning_.human_shift_gain),rpm_);}
        average_.fill(load_);shift_=in.gear>gear_?1U:3U;gear_=in.gear;
        load_curve_.start(load_,0,50);rpm_curve_.start(rpm_,rpm_,300);
    }
    //524620 updates audible load/RPM from the previous sampled inputs first.
    if(shift_>=1&&shift_<=5) {
        if(shift_==2)load_curve_.to=torque_;
        load_curve_.advance(in.dt);load_=load_curve_.value;
        if(shift_==4||shift_==5)average_.fill(load_);
    }else load_=average_.push(on_?100.0F:0.0F);
    if(shift_) {
        if(shift_==2||shift_==6)rpm_curve_.to=physical_;
        rpm_curve_.advance(in.dt);rpm_=rpm_curve_.value;
    }else rpm_=physical_;
    if(context && context->script && context->sound_random && context->world_random &&
       context->script->tick(in.game_state,context->countdown_seconds,physical_,torque_,in.dt,
           *context->sound_random,*context->world_random)) {
        rpm_=context->script->rpm;load_=context->script->load;
    }
    input(in);
    switch(shift_) {
    case 1:if(rpm_curve_.done){shift_=2;rpm_curve_.start(physical_+float(tuning_.up_offset),physical_,tuning_.up_ms);
        load_curve_.start(load_,torque_,10);gain_curve_.start(tuning_.up_extra,0,tuning_.up_extra_ms);
        profile_.up_done(rpm_,gear_);
        if(shift_rpm_>=7000.F)disengage=2; //523E30 ->528F80, fixed output pan0.
        }break;
    case 2:if(rpm_curve_.done){shift_=0;average_.fill(load_curve_.value);}break;
    case 3:if(rpm_curve_.done){shift_=4;rpm_curve_.start(rpm_,physical_+float(tuning_.down_rise),tuning_.down_rise_ms,3);
        load_curve_.start(load_,100.0F,215);profile_.down_done();}break;
    case 4:if(rpm_curve_.done){shift_=5;rpm_curve_.start(rpm_,rpm_-float(tuning_.down_dip),tuning_.down_dip_ms,2);
        load_curve_.start(100.0F,0,215);}break;
    case 5:if(rpm_curve_.done){shift_=6;const float duration=std::clamp(std::abs((rpm_-physical_)*tuning_.catch_ms_per_rpm),0.0F,800.0F);
        rpm_curve_.start(rpm_,physical_,int(duration));average_.fill(torque_);}break;
    case 6:if(rpm_curve_.done){shift_=0;average_.fill(load_);}break;
    default:break;
    }
    // The original scheduler samples523C30 twice per audio frame.
    input(in);
    profile_.tick(tuning_,in,rpm_,time_,last_off_,on_);
    gain_curve_.advance(in.dt);
    return {rpm_,load_,shift_,gain_curve_.value,shift_cue,shift_gain,disengage,
        profile_.blend,profile_.offset,tuning_.profile_balance_boost};
}

std::int32_t human_shift_audio_gain(std::int16_t source,float rpm)noexcept {
    if(!std::isfinite(rpm)||rpm<=-2147483648.F||rpm>=2147483648.F)return 0;
    const float fraction=std::clamp((float(int(rpm))-1500.F)/(8000.F-1500.F),0.F,1.F);
    const int index=int(((1.F-.1F)*fraction+.1F)*-512.F+512.F);
    const auto gain=(source*32767)>>15;
    return (gain*kEngineBalance[index])>>15;
}
void HumanEngineEnvelope::reset()noexcept {
    rpm_curve_={};load_curve_={};gain_curve_={};average_={};rpm_=load_=time_=on_time_=off_time_=shift_time_=0;gear_=0;shift_=0;initialized_=throttle_on_=false;shift_cue_={};shift_rpm_=0;profile_={};
}
EngineEnvelopeOutput HumanEngineEnvelope::tick(const EngineEnvelopeInput& in)noexcept {
    if(!std::isfinite(in.dt)||in.dt<=0||!std::isfinite(in.drive_load)||!std::isfinite(in.throttle)||
       !std::isfinite(in.load_divisor)||in.load_divisor<=0.F)return {rpm_,load_,shift_,gain_curve_.value};
    unsigned disengage=0;
    if(!initialized_){initialized_=true;gear_=in.gear;rpm_=in.rpm;}
    else if(in.gear!=gear_) {
        const bool up=in.gear>gear_;gear_=in.gear;
        if(up||gear_!=1) {
            average_.fill(load_);shift_=0;
            if(!up||rpm_>=3000.F) {
                shift_time_=time_;shift_rpm_=rpm_;
                if(up&&rpm_>=7000.F)disengage|=1; //528D70 at notification.
                shift_cue_.notify(up,gear_,rpm_,time_,tuning_.human_shift_up_delay,tuning_.human_shift_down_delay);
                shift_=up?1U:3U;load_curve_.start(load_,0,50);
                rpm_curve_.start(rpm_,rpm_-float(up?tuning_.human_up_drop:tuning_.human_down_drop),
                    up?tuning_.human_up_drop_ms:tuning_.human_down_drop_ms);
            }
        }
    }
    time_+=in.dt;
    const auto shift_cue=shift_cue_.tick(time_);
    const auto shift_gain=shift_cue?human_shift_audio_gain(tuning_.human_shift_gain,rpm_):0;
    if(in.throttle*100.F>30.F){if(!throttle_on_){throttle_on_=true;on_time_=time_;}}
    else if(throttle_on_){throttle_on_=false;off_time_=time_;}
    const float physical=engine_audio_rpm(in.rpm,in.maximum_rpm);
    const float torque=(std::clamp(in.drive_load,0.F,in.load_divisor)/in.load_divisor)*100.F;
    const auto down_ms=[&](int ms){return gear_<3?int(float(ms)*0.7F):ms;};
    //529500/529530 runs before the human load/RPM curve publications.
    switch(shift_) {
    case 1:if(rpm_curve_.done){shift_=2;rpm_curve_.start(physical+float(tuning_.up_offset),physical,tuning_.up_ms);
        profile_.up_done(rpm_,gear_);
        if(shift_rpm_>=7000.F)disengage|=2; //529530 ->528F80 uses latched RPM.
        load_curve_.start(load_,torque,10);gain_curve_.start(tuning_.up_extra,0,tuning_.up_extra_ms);}break;
    case 2:if(rpm_curve_.done){shift_=0;average_.fill(load_curve_.value);}break;
    case 3:if(rpm_curve_.done){shift_=4;rpm_curve_.start(rpm_,physical+float(tuning_.down_rise),down_ms(tuning_.down_rise_ms),3);
        load_curve_.start(load_,100.F,215);profile_.down_done();}break;
    case 4:if(rpm_curve_.done){shift_=5;rpm_curve_.start(rpm_,rpm_-float(tuning_.down_dip),down_ms(tuning_.down_dip_ms),2);
        load_curve_.start(100.F,0,215);}break;
    case 5:if(rpm_curve_.done){shift_=6;const float duration=std::clamp(std::abs((rpm_-physical)*tuning_.catch_ms_per_rpm),0.F,800.F);
        rpm_curve_.start(rpm_,physical,int(duration));average_.fill(torque);}break;
    case 6:if(rpm_curve_.done){shift_=0;average_.fill(load_);}break;
    default:break;
    }
    if(shift_>=1&&shift_<=5) {
        if(shift_==2)load_curve_.to=torque;
        load_curve_.advance(in.dt);load_=load_curve_.value;
        if(shift_==4||shift_==5)average_.fill(load_);
    }else load_=average_.push(in.throttle*100.F);
    if(shift_) {
        if(shift_==2||shift_==6)rpm_curve_.to=physical;
        rpm_curve_.advance(in.dt);rpm_=rpm_curve_.value;
    }else rpm_=physical;
    profile_.tick(tuning_,in,rpm_,time_,off_time_,throttle_on_);
    gain_curve_.advance(in.dt);
    return {rpm_,load_,shift_,gain_curve_.value,shift_cue,shift_gain,disengage,
        profile_.blend,profile_.offset,tuning_.profile_balance_boost};
}
HumanEngineCameraBalance human_engine_camera_balance(const std::array<float,3>& source,
    const std::array<float,3>& forward,const std::array<float,3>& eye,int offset,int interior,
    unsigned mover,unsigned pov)noexcept {
    const auto normal=[](std::array<float,3> v){const float size=std::sqrt((v[2]*v[2]+v[0]*v[0])+v[1]*v[1]);
        if(size==0.F)return std::array<float,3>{{1,0,0}};
        const float scale=1.F/size;for(auto& x:v)x*=scale;return v;};
    std::array<float,3> delta{};for(unsigned i=0;i<3;++i)delta[i]=source[i]-eye[i];
    delta=normal(delta);delta[2]=0;delta=normal(delta);
    auto axis=forward;axis[2]=0;axis=normal(axis);
    const float dot=(delta[1]*axis[1]+delta[0]*axis[0])+delta[2]*axis[2];
    HumanEngineCameraBalance out;out.angle=std::uint16_t(0x4000-int(player_unit_asin(dot)));
    int phase=(out.angle>0x7fff?0xffff-int(out.angle):int(out.angle))>>5;
    if(mover==1||mover==0x11)phase=pov==2?interior:phase+offset;
    out.phase=unsigned(std::clamp(phase,0,1024));return out;
}
std::array<unsigned,2> human_engine_audio_gains(int a,int b,int balance,float boost,
    float blend,int offset,float cross,unsigned camera_pov)noexcept {
    balance=std::clamp(balance,0,1024);if(!std::isfinite(boost))boost=0;
    //529CD0 balances BEFORE adding the adaptive offset, unlike5235A0.
    int first=int((std::int64_t(a)*kEngineBalance[(1024-balance)/2])>>15)+offset;
    int second=int((std::int64_t(b)*kEngineBalance[balance/2])>>15)+offset;
    if(blend!=0){
        if(camera_pov==2&&first>second){
            const int extra=int((float(first-second)*cross)*blend);first+=extra;
            if(first!=0)second=int((float(extra)/float(first)+1.F)*float(second));
        }else if(camera_pov!=2&&second>first)first+=int((float(second-first)*cross)*blend);
    }
    // The common upshift accent follows offset and the mode-specific mix.
    first+=int(float(first)*boost);second+=int(float(second)*boost);
    return {{unsigned(first),unsigned(second)}};
}

EngineAudioSelection engine_audio_selection(const std::string& car, int upgrade) {
    static constexpr const char* names[]={"GOLF","FOCUS","MIATA","NEON","CIVIC",
        "PEUGOT","CELICA","ECLIPSE","RX7","SUPRA","S2000","RSX","IMPREZA",
        "LANCER","INTEGRA","TIBURON","350Z","SENTRA","240SX","SKYLINE"};
    static constexpr unsigned groups[]={2,0,0,0,0,0,2,2,1,3,2,2,3,2,2,3,1,0,2,1};
    const auto found=std::find(std::begin(names),std::end(names),car);
    if(found==std::end(names) || upgrade<0 || upgrade>3)
        throw std::runtime_error("unsupported car sound selection");
    EngineAudioSelection s; s.group=groups[found-std::begin(names)];s.upgrade=unsigned(upgrade);
    const auto dir="Sound/CAR_"+std::to_string(s.group)+"/ENG_UG_"+std::to_string(upgrade)+"/";
    const auto prefix="C"+std::to_string(s.group)+"U"+std::to_string(upgrade)+"_M0_";
    s.engine=dir+prefix+"ENG_MB.abk";s.exhaust=dir+prefix+"EXH_MB.abk";
    return s;
}

std::uint32_t EngineAudioGraph::read_word(unsigned at) const {return word(data_,at);}
std::int32_t EngineAudioGraph::integer(unsigned at) const {return static_cast<std::int32_t>(read_word(at));}
float EngineAudioGraph::real(unsigned at) const {auto v=read_word(at);float f;std::memcpy(&f,&v,4);return f;}
void EngineAudioGraph::write_word(unsigned at,std::uint32_t v) {
    (void)read_word(at);for(unsigned i=0;i<4;++i)data_[at+i]=std::uint8_t(v>>(i*8));
}
void EngineAudioGraph::real(unsigned at,float v) {std::uint32_t b;std::memcpy(&b,&v,4);write_word(at,b);}

void EngineAudioGraph::load(const std::vector<std::uint8_t>& bank,unsigned group,unsigned upgrade) {
    if(group>3 || upgrade>3)bad();
    compile(bank,false);group_=group;upgrade_=upgrade;
}
void EngineAudioGraph::load_effect(const std::vector<std::uint8_t>& bank) {compile(bank,true);}
void EngineAudioGraph::reset_effect() {if(!effect_)bad();data_=initial_;retired_=false;}
std::uint32_t EngineAudioGraph::random_word()noexcept {return (random_context_?*random_context_:process_bank_audio_random()).next();}
void EngineAudioGraph::compile(const std::vector<std::uint8_t>& bank,bool effect) {
    // Compile only the engine/exhaust straight-line graph, rejecting any
    // branch, foreign callback or unexpected prologue/epilogue.
    EngineAudioGraph next;next.random_context_=random_context_;
    next.effect_=effect;
    const auto module=word(bank,0x1c),begin=word(bank,module+40),end=word(bank,module+44);
    const auto limit=word(bank,0x20);
    if(begin>=end || end>=limit || limit>bank.size() || limit>65536)bad();
    next.data_.assign(bank.begin(),bank.begin()+limit);
    next.params_=end+0x40;
    unsigned p=begin,at=end+0x18,players=0;bool returned=false;
    auto byte=[&](){if(p>=end)bad();return bank[p++];};
    auto expect=[&](unsigned b){if(byte()!=b)bad();};
    auto imm=[&](){const auto v=word(bank,p);p+=4;return v;};
    auto displacement=[&](unsigned mode){
        if(mode==0x06)return 0U;
        if(mode==0x46)return unsigned(static_cast<std::int8_t>(byte()));
        if(mode==0x86)return imm();
        bad();
    };
    expect(0x56);expect(0x8b);expect(0x74);expect(0x24);expect(8);
    try {while(p<end) {
        const auto code=byte();
        if(code==0x56) {
            expect(0xe8);const auto callback_at=p,id=imm();
            const auto rel=word(bank,0x30);bool relocated=false;
            const auto count=word(bank,rel);if(count>1024)bad();
            for(unsigned i=0;i<count;++i)relocated|=word(bank,rel+4+4*i)==callback_at;
            if(!relocated)bad();
            switch(id){case 0:case 1:case 4:case 7:case 8:case 10:case 11:case 12:case 13:case 15:case 16:case 17:
                case 21:case 22:case 25:case 27:case 28:case 29:case 30:case 31:case 35:break;default:bad();}
            if(id==1){next.params_=at+20;next.parameter_count_=next.read_word(at+16);}
            next.ops_.push_back({3,at,id});players+=id==27;
        } else if(code==0xc6) {
            const auto mode=byte();const auto off=at+displacement(mode);expect(0);next.ops_.push_back({4,off,0});
        } else if(code==0x8b || code==0x89 || code==0x03) {
            const auto mode=byte();const auto off=at+displacement(mode);
            (void)next.read_word(off);
            next.ops_.push_back({code==0x8b?0U:code==0x89?1U:2U,off,0});
            if(code==0x8b && mode==0x06 && p+4<=end && bank[p]==0x0f && bank[p+1]==0xaf && bank[p+2]==0x46 && bank[p+3]==4) {
                p+=4;next.ops_.push_back({7,at+4,0});
            }
            // Authored inline signed max/min, fixed two-input form.
            if(code==0x8b && mode==0x06 && p+11<=end && bank[p]==0x8b && bank[p+1]==0x4e) {
                expect(0x8b);expect(0x4e);expect(4);expect(0x3b);expect(0xc1);
                const auto branch=byte();if(branch!=0x7f && branch!=0x7c)bad();
                expect(2);expect(0x8b);expect(0xc1);
                next.ops_.push_back({branch==0x7f?5U:6U,at+4,0});
            }
        } else if(code==0x83 || code==0x81) {
            const auto reg=byte();const auto value=code==0x83?unsigned(byte()):imm();
            if(reg==0xc6)at+=value;else if(reg!=0xc4)bad();
        } else if(code==0x5e) {
            expect(0xc3);
            // Some banks put another module code before this module template.
            // This graph ends at its own RET, as the original callback does.
            returned=true;
            break;
        }
        else bad();
        if(next.ops_.size()>1024 || at>=limit)bad();
    }}catch(const std::exception& e){throw std::runtime_error(std::string(e.what())+" code_offset="+std::to_string(p));}
    if((!effect && players!=4) || !players || players>4 || !returned || next.parameter_count_>32)bad();
    next.players_=players;if(effect)next.initial_=next.data_;
    *this=std::move(next);
}

std::int32_t EngineAudioGraph::callback(unsigned id,unsigned n,float dt,
    std::array<EngineAudioVoice,4>& voices,unsigned& count) {
    switch(id) {
    case 0:{auto v=integer(n+16);write_word(n+16,0);return v;}
    case 1:return integer(n+20);
    case 4:if(read_word(n+12)){if(!effect_)bad();retired_=true;}return 0;
    case 7:{if(read_word(n+12)){const auto value=random_word();
        if(!read_word(n+4))bad();
        write_word(n+8,read_word(n)+value%read_word(n+4));}return integer(n+8);}
    case 8:{
        const unsigned trigger=read_word(n)&65535U;
        if(!read_word(n+trigger))return integer(n+12);
        unsigned index=read_word(n+8)&65535U,size=read_word(n+8)>>16,width=data_.at(n+2);
        const unsigned remaining=size-index-data_.at(n+3);
        if(!remaining || (width!=1&&width!=2))bad();
        const unsigned pick=index+random_word()%remaining;
        auto get=[&](unsigned i){return unsigned(data_.at(n+16+i*width))|(width==2?unsigned(data_.at(n+17+i*width))<<8:0);};
        const unsigned value=get(pick),swap=get(index);
        for(unsigned i=0;i<width;++i){data_.at(n+16+pick*width+i)=std::uint8_t(swap>>(i*8));data_.at(n+16+index*width+i)=std::uint8_t(value>>(i*8));}
        ++index;data_.at(n+3)=std::uint8_t(index>=size);if(index>=size)index=0;
        write_word(n+8,(size<<16)|index);write_word(n+12,read_word(n+4)+value);return integer(n+12);
    }
    case 10:{
        const auto v=integer(n+20);
        if(v>=integer(n) && v<=integer(n+4)) {
            if(!data_.at(n+16)){data_[n+16]=data_[n+17]=1;return 1;}
        }else if(v>=integer(n+8)&&v<=integer(n+12))data_.at(n+16)=0;
        data_.at(n+17)=0;return 0;
    }
    // Native64CBD0: reset, expiry at equality, then inactive negative timer.
    case 11:{
        if(read_word(n+8))real(n,0.F);
        else if(real(n)<0.F){data_.at(n+4)=0;return 0;}
        if(float(integer(n+12))<=real(n)){real(n,-1.F);data_.at(n+4)=1;return 1;}
        real(n,real(n)+dt);data_.at(n+4)=0;return 0;
    }
    case 13:for(unsigned i=0;i<data_.at(n);++i)if(read_word(n+4+4*i))return 1;return 0;
    case 12:{const unsigned at=n+(read_word(n)&65535U),size=data_.at(n+2);
        for(unsigned i=0;i<size;++i)if(read_word(at+4*i)){write_word(n+4,read_word(n+8+4*i));break;}
        return integer(n+4);}
    case 15:{
        auto v=integer(n+12);if(v==integer(n+4))return integer(n+8);
        write_word(n+4,std::uint32_t(v));const auto t=read_word(n);
        v=std::clamp(v,integer(t+4),integer(t+8))-integer(t+4);
        const unsigned width=data_.at(t);
        auto entry=[&](unsigned i)->std::int32_t {
            const auto off=t+16+i*width;
            if(width==1)return static_cast<std::int8_t>(data_.at(off));
            if(width==2){if(off+1>=data_.size())bad();return static_cast<std::int16_t>(data_[off]|(data_[off+1]<<8));}
            if(width==4)return integer(off);
            bad();
        };
        std::int32_t result;
        if(read_word(t+12)==0x3f800000)result=entry(unsigned(v));
        else {
            const double scaled=double(v)*double(real(t+12));
            const float position=static_cast<float>(scaled);
            const auto i=rounded(static_cast<float>(scaled-0.5));
            const auto size=unsigned(data_.at(t+2))|(unsigned(data_.at(t+3))<<8);
            if(i<0 || unsigned(i)>=size || !size)bad();
            const auto lo=entry(unsigned(i)),hi=entry(std::min(unsigned(i)+1,size-1));
            result=rounded(static_cast<float>(double(lo)+(double(position)-i)*(double(hi)-lo)));
        }
        write_word(n+8,std::uint32_t(result));return result;
    }
    case 16:{
        const auto h=read_word(n);const unsigned tail=n+(h&65535U),size=h>>16;
        if(!size)bad();
        if(integer(tail+4)!=integer(n+8)){
            write_word(n+8,read_word(tail+4));const int delay=std::max(0,integer(tail+4));write_word(tail+4,unsigned(delay));
            const int ticks=std::min(rounded(float(double(delay)/dt)),int(size)-1);
            const unsigned in=(read_word(n+4)>>16)+unsigned(ticks);
            write_word(n+4,(read_word(n+4)&0xffff0000U)|(in&65535U));
        }
        unsigned in=read_word(n+4)&65535U,out=read_word(n+4)>>16;if(in>=size)in-=size;if(out>=size)out=0;
        write_word(n+12+4*in,read_word(tail));const auto value=integer(n+12+4*out);
        write_word(n+4,((out+1)<<16)|(in+1));return value;
    }
    case 17:{auto i=integer(n+4);return i>0&&i<=data_.at(n)?integer(n+4+4*unsigned(i)):0;}
    case 21:{double v=integer(n+8);for(unsigned i=1;i<data_.at(n);++i)v*=integer(n+8+4*i);return rounded(float(v*real(n+4)));}
    // Native64D0D0: wrapping signed integer sum of the authored input list.
    case 22:{std::uint32_t v=read_word(n+4);for(unsigned i=1;i<data_.at(n);++i)v+=read_word(n+4+4*i);return static_cast<std::int32_t>(v);}
    case 25:{auto d=integer(n+4);if(d==-1 && integer(n)==INT32_MIN)bad();return d?integer(n)/d:0;}
    case 27:{
        if(count>=voices.size())bad();
        const auto t=read_word(n+4);const auto size=integer(t);
        if(size<=0)bad();
        const auto i=unsigned(std::clamp(integer(n+20),0,size-1));
        EngineAudioVoice voice{read_word(t+8+12*i),read_word(n+24),4096,32767,32768};
        const unsigned controls=data_.at(n+14);
        for(unsigned j=0;j<controls;++j){const auto key=read_word(n+28+12*j);const auto value=integer(n+36+12*j);
            if(key==0)voice.pitch_q12=value;else if(key==2)voice.gain_q15=value;else if(key==3)voice.pan=value;}
        if(effect_){if(!player_)bad();return player_(count++,voice);}
        voices[count++]=voice;
        return 0;
    }
    case 28:{
        const auto period=integer(n+8);if(period<=0)return 0;
        float phase=real(n+4);if(!std::isfinite(phase)||phase<0)bad();
        while(phase>=1)phase-=1;
        const auto shape=data_.at(n);const auto amplitude=integer(n+12);
        float value=0;
        if(shape==1)value=phase>=0.5F?float(amplitude):0;
        else if(shape==2)value=float(double(amplitude)*phase);
        else if(shape==3)value=float(double(amplitude)*(phase<=0.5F?phase:1.0-double(phase))*2);
        else bad();
        real(n+4,float(double(phase)+float(double(dt)/period)));
        return rounded(value);
    }
    case 29:{
        const auto target=integer(n+24);const double previous=real(n);
        if(double(target)==previous)return target;
        const auto duration=integer(n+16);
        if(target!=integer(n+8)||duration!=integer(n+12)) {
            write_word(n+8,std::uint32_t(target));write_word(n+12,std::uint32_t(duration));
            if(duration<=0){real(n,float(target));return target;}
            real(n+4,float((double(target)-previous)/duration*double(dt)*0.000244140625));
        }
        double next=previous+double(integer(n+20))*real(n+4);
        if(real(n+4)>=0 ? next>target : next<target)next=target;
        real(n,float(next));return rounded(real(n));
    }
    case 30:{std::uint32_t v=read_word(n+8);for(unsigned i=1;i<data_.at(n);++i)v+=read_word(n+8+4*i);return std::min(integer(n+4),static_cast<std::int32_t>(v));}
    case 31:return std::max(integer(n),static_cast<std::int32_t>(read_word(n+4)-read_word(n+8)));
    case 35:return rounded(float(double(integer(n+8))*integer(n+4)*double(real(n))));
    default:bad();
    }
}

std::array<EngineAudioVoice,4> EngineAudioGraph::tick(float rpm,float load,float dt,unsigned gain_a,unsigned gain_b,std::uint16_t pan) {
    if(data_.empty()||!std::isfinite(rpm)||!std::isfinite(load)||!std::isfinite(dt)||dt<=0||dt>1000)bad();
    const auto controls=engine_audio_parameters(rpm,load);
    const auto rpm_value=controls[0],load_value=controls[1];
    for(unsigned i=0;i<19;++i)write_word(params_+4*i,0);
    write_word(params_+4,group_);
    write_word(params_+8,upgrade_);
    write_word(params_+16,rpm_value);write_word(params_+20,rpm_value);
    write_word(params_+24,load_value);write_word(params_+28,load_value);
    write_word(params_+32,gain_a);write_word(params_+36,gain_b);
    write_word(params_+64,0);write_word(params_+68,pan);write_word(params_+72,0);
    std::array<EngineAudioVoice,4> voices{};unsigned count=0;std::uint32_t eax=0;
    for(const auto& op:ops_) {
        if(op.kind==0)eax=read_word(op.at);
        else if(op.kind==1)write_word(op.at,eax);
        else if(op.kind==2)eax+=read_word(op.at);
        else {
            try {eax=std::uint32_t(callback(op.value,op.at,dt,voices,count));}
            catch(const std::exception& e){throw std::runtime_error(std::string(e.what())+
                " callback="+std::to_string(op.value)+" node="+std::to_string(op.at));}
        }
    }
    if(count!=4)bad();
    return voices;
}
std::size_t EngineAudioGraph::resident_bytes() const noexcept {
    return data_.capacity()+initial_.capacity()+ops_.capacity()*sizeof(Op);
}
void EngineAudioGraph::effect_tick(const std::vector<std::int32_t>& parameters,
    const std::function<int(unsigned,const EngineAudioVoice&)>& player,float dt) {
    if(!effect_ || parameters.size()!=parameter_count_ || !std::isfinite(dt)||dt<=0||dt>1000)bad();
    if(retired_)return;
    for(unsigned i=0;i<parameters.size();++i)write_word(params_+4*i,std::uint32_t(parameters[i]));
    player_=player;std::array<EngineAudioVoice,4> voices{};unsigned count=0;std::uint32_t eax=0;
    try{for(const auto& op:ops_) {
        if(op.kind==0)eax=read_word(op.at);else if(op.kind==1)write_word(op.at,eax);
        else if(op.kind==2)eax+=read_word(op.at);else if(op.kind==4)data_.at(op.at)=0;
        else if(op.kind==5)eax=std::uint32_t(std::max(static_cast<std::int32_t>(eax),integer(op.at)));
        else if(op.kind==7)eax*=read_word(op.at);
        else if(op.kind==6)eax=std::uint32_t(std::min(static_cast<std::int32_t>(eax),integer(op.at)));
        else eax=std::uint32_t(callback(op.value,op.at,dt,voices,count));
    }}catch(...){player_={};throw;}
    player_={};if(count!=players_)bad();
}
} // namespace nfsu::audio
