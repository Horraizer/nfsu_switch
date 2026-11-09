#include "nfsu/race_audio.h"
#include "nfsu/race_audio_mix.h"

#include "nfsu/audio_playback.h"
#include "nfsu/audio_output.h"
#include "nfsu/frontend_music.h"
#include "nfsu/car_assets.h"
#include "nfsu/sha256.h"

#include <algorithm>
#include <cstdio>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace nfsu::audio {
namespace {

// Process playlist state survives recreation of each race audio service.
// Native 53CBD0 consumes played entries instead of restarting bank1.
unsigned original_race_music_next=0;
//527480 global78EAE0 has no race/actor reset; all selected actors share it.
unsigned original_backfire_counter=0;

double finite_clamp(double value, double low, double high) noexcept {
    if (!std::isfinite(value)) return low;
    return std::max(low, std::min(high, value));
}

std::uint32_t q15(double gain) noexcept {
    return static_cast<std::uint32_t>(finite_clamp(gain, 0, 1) * 32768);
}

std::vector<std::uint8_t> bank_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("missing bank: "+path.generic_string());
    const auto length = input.tellg();
    if (length < 0 || length > 32 * 1024 * 1024)
        throw std::runtime_error("bank exceeds 32 MiB");
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length))
        throw std::runtime_error("bank read failed");
    return bytes;
}

PcmClip decode_clip(const std::vector<std::uint8_t>& bytes,
                    const Bank& bank, std::uint32_t index) {
    const auto found = std::find_if(bank.samples.begin(), bank.samples.end(),
        [&](const Sample& sample) { return sample.index == index; });
    if (found == bank.samples.end()) throw std::runtime_error("sample index absent");
    PcmClip clip;
    clip.rate = found->rate;
    clip.channels = found->channels;
    clip.loop_start = found->loop_start;
    clip.loop_end = found->loop_end;
    clip.pcm.reserve(static_cast<std::size_t>(found->frames) * found->channels);
    decode_bank_sample(bytes, *found, [&](const std::uint8_t* pcm, std::size_t size) {
        if (size % 2) throw std::runtime_error("odd PCM byte count");
        for (std::size_t i = 0; i < size; i += 2)
            clip.pcm.push_back(static_cast<std::int16_t>(
                static_cast<std::uint16_t>(pcm[i]) |
                (static_cast<std::uint16_t>(pcm[i + 1]) << 8)));
    });
    if (!clip.valid() || clip.frames() != found->frames)
        throw std::runtime_error("invalid decoded clip");
    return clip;
}

} // namespace

bool RaceEngineSound::load(const std::filesystem::path& root,const std::string& car,
                          int upgrade,std::string& error) {
    try {
        RaceEngineSound next;next.selection_=engine_audio_selection(car,upgrade);
        CarAssets assets;
        if(!load_car_assets(root/"Global/GLOBALB.BUN",car,assets,error))return false;
        // 52649D selects CarTypeInfo+9C0+upgrade*64. 529CD0 reads the
        // signed gain pair at+A/+C; normal crossfade endpoints multiply32767.
        std::ifstream tuning(root/"Global/GLOBALB.BUN",std::ios::binary);
        tuning.seekg(static_cast<std::streamoff>(assets.record_offset+0x9c0+unsigned(upgrade)*0x64));
        std::array<std::uint8_t,100> record{};
        if(!tuning.read(reinterpret_cast<char*>(record.data()),record.size()))throw std::runtime_error("engine audio tuning unavailable");
        const auto level=[&](unsigned at){return unsigned(std::max(0,int(static_cast<std::int16_t>(record[at]|(record[at+1]<<8)))));};
        next.gain_a_=int(level(0xa));next.gain_b_=int(level(0xc));
        next.balance_=int(static_cast<std::int16_t>(unsigned(record[0x3c])|(unsigned(record[0x3d])<<8)));
        next.interior_balance_=int(static_cast<std::int16_t>(unsigned(record[0x3e])|(unsigned(record[0x3f])<<8)));
        next.envelope_.configure(engine_envelope_tuning(record.data(),record.size()));
        next.remote_envelope_.configure(engine_envelope_tuning(record.data(),record.size()));
        for(unsigned bank=0;bank<2;++bank) {
            const auto bytes=bank_bytes(root/(bank?next.selection_.exhaust:next.selection_.engine));
            next.graphs_[bank].load(bytes,next.selection_.group,next.selection_.upgrade);
            const auto parsed=parse_abk(bytes);
            for(unsigned i=0;i<4;++i)next.clips_[bank][i]=decode_clip(bytes,parsed,i+1);
        }
        next.loaded_=true;*this=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

void RaceEngineSound::stop(Mixer& mixer) noexcept {
    for(unsigned i=0;i<8;++i)mixer.stop(slot(i));
    state_.fill(0);sample_.fill(0);envelope_.reset();remote_envelope_.reset();remote_script_={};remote_position_={};human_turbo_={};have_tick_=false;
}

RaceShiftSoundCue RaceEngineSound::update(const RaceAudioSnapshot& snapshot,Mixer& mixer,std::uint32_t category_gain,int shift_category_gain) {
    if(!loaded_)return {};
    if(snapshot.finished){stop(mixer);return {};}
    if(have_tick_&&snapshot.tick==tick_)return {};
    if(have_tick_&&snapshot.tick<tick_)envelope_.reset();
    have_tick_=true;tick_=snapshot.tick;
    EngineEnvelopeInput input;input.rpm=float(snapshot.rpm);input.maximum_rpm=float(snapshot.maximum_rpm);
    input.drive_load=float(snapshot.engine_drive_load);input.load_divisor=snapshot.torque_maximum;input.throttle=float(snapshot.throttle);
    input.brake=float(snapshot.brake);input.handbrake=snapshot.handbrake;input.gear=snapshot.gear;
    input.game_state=snapshot.engine_game_state;input.gear_ratio=snapshot.gear_ratio;input.first_gear_ratio=snapshot.first_gear_ratio;
    const auto control=envelope_.tick(input);
    const float rpm=control.rpm,load=control.load;
    const auto balance=human_engine_camera_balance(snapshot.human_position,snapshot.human_forward,
        snapshot.listener_eye,balance_,interior_balance_,snapshot.camera_mover,snapshot.camera_pov);
    human_turbo_={control.rpm,control.load,int(balance.angle),int(balance.phase),envelope_.profile_state().throttle_open};
    auto gains=human_engine_audio_gains(gain_a_,gain_b_,int(balance.phase),control.gain_boost,
        control.adaptive_blend,control.adaptive_offset,control.adaptive_balance_boost,snapshot.camera_pov);
    for(auto& gain:gains)gain=RaceAudioMix::engine_output(gain,int(category_gain));
    publish(rpm,load,gains,1.F,balance.angle,mixer);
    //528D70/528F80 clamp their curve at7000 and multiply the initialized21000.
    return {control.shift_cue,std::int32_t(RaceAudioMix::engine_output(std::uint32_t(control.shift_gain),int(category_gain))),
        control.disengage,std::int32_t(RaceAudioMix::engine_output(20999U,shift_category_gain)),balance.angle,snapshot.camera_pov==2?1U:0U};
}
bool RaceEngineSound::load_remote(const std::filesystem::path& root,const std::string& car,
    int upgrade,unsigned ordinal,std::string& error) {
    if(ordinal>=3){error="remote sound ordinal exceeds roster";return false;}
    if(!load(root,car,upgrade,error))return false;
    remote_=true;remote_slot_=61+8*ordinal;return true;
}
RaceShiftSoundCue RaceEngineSound::update_remote(const RaceAudioSnapshot::Rival& actor,const RaceAudioSnapshot& snapshot,
    Mixer& mixer,int category,TrafficAudioRandom& sound_random,TrafficAudioRandom& world_random,int shift_category,int end_category) {
    if(!loaded_||!remote_)return {};
    if(snapshot.finished||!actor.active){stop(mixer);return {};}
    if(have_tick_&&snapshot.tick==tick_)return {};
    if(have_tick_&&snapshot.tick<tick_)stop(mixer);
    tick_=snapshot.tick;have_tick_=true;
    RemoteEngineContext context{&remote_script_,&sound_random,&world_random,snapshot.countdown_seconds};
    const auto control=remote_envelope_.tick(actor.engine,&context);
    // Physical notifications precede524620 positional refresh; retain that
    // preceding context for523990. The later523E30 end uses the new gain.
    const auto shift_gain=RaceAudioMix::spatial_output(std::uint32_t(control.shift_gain),remote_position_.gain,shift_category);
    const auto shift_pan=std::uint16_t(remote_position_.pan);
    remote_position_.tick(actor.position,snapshot.human_position,snapshot.listener_eye,snapshot.listener_forward,1000.F/60.F);
    const auto raw_gains=remote_engine_audio_gains(gain_a_,gain_b_,balance_,control.gain_boost,
        control.adaptive_blend,control.adaptive_offset,control.adaptive_balance_boost);
    std::array<unsigned,2> gains{};
    for(unsigned i=0;i<2;++i)gains[i]=RaceAudioMix::spatial_output(std::uint32_t(raw_gains[i]),remote_position_.gain,category);
    human_turbo_={control.rpm,control.load,remote_position_.pan,balance_,remote_envelope_.profile_state().throttle_open};
    publish(control.rpm,control.load,gains,remote_position_.doppler,std::uint16_t(remote_position_.pan),mixer);
    return {control.shift_cue,std::int32_t(shift_gain),control.disengage,
        std::int32_t(RaceAudioMix::spatial_output(20999U,remote_position_.gain,end_category)),shift_pan};
}
void RaceEngineSound::publish(float rpm,float load,const std::array<unsigned,2>& gains,float doppler,std::uint16_t pan,Mixer& mixer) {
    for(unsigned bank=0;bank<2;++bank) {
        const auto voices=graphs_[bank].tick(rpm,load,1000.0F/60.0F,gains[0],gains[1],pan);
        for(unsigned i=0;i<4;++i) {
            const auto& v=voices[i];const unsigned index=bank*4+i,voice_slot=slot(index);
            if(v.sample<1||v.sample>4||v.state>2)throw std::runtime_error("invalid engine player control");
            // The actor has already applied position/category BEFORE bank controls.
            const auto gain=std::uint32_t(std::clamp(v.gain_q15,0,32767));
            const double requested_pitch=double(v.pitch_q12)*16.0*double(doppler);
            const auto pitch=std::uint32_t(std::clamp(std::isnan(requested_pitch)?0.0:requested_pitch,1.0,262144.0));
            if(v.state==0)mixer.stop(voice_slot);
            else if(v.state==1) {
                if(state_[index]!=1||sample_[index]!=v.sample)
                    mixer.play(voice_slot,&clips_[bank][v.sample-1],true,gain,pitch);
                else mixer.control(voice_slot,gain,pitch);
                mixer.pan(voice_slot,std::uint16_t(v.pan));
            }else if(state_[index]!=2)mixer.release(voice_slot);
            state_[index]=v.state;sample_[index]=v.sample;
        }
    }
}

std::size_t RaceEngineSound::resident_bytes() const noexcept {
    std::size_t bytes=0;
    for(const auto& bank:clips_)for(const auto& clip:bank)bytes+=clip.pcm.capacity()*sizeof(std::int16_t);
    for(const auto& graph:graphs_)bytes+=graph.resident_bytes();
    return bytes;
}

void RaceEffectsSound::Effect::stop(Mixer& mixer) noexcept {
    for(unsigned i=0;i<graph.player_count();++i)mixer.stop(slots[i]);
    states.fill(0);samples.fill(0);
}
void RaceEffectsSound::Effect::reset(Mixer& mixer) {stop(mixer);graph.reset_effect();}
void RaceEffectsSound::Effect::run(Mixer& mixer,const std::array<PcmClip,11>* shared) {
    // Capture one context pointer, keeping std::function inside its small buffer.
    struct Context {Effect* effect;Mixer* mixer;const std::array<PcmClip,11>* clips;} context{this,&mixer,shared?shared:&clips};
    if(graph.retired()){stop(mixer);return;}
    graph.effect_tick(parameters,[&context](unsigned i,const EngineAudioVoice& v){
        auto& e=*context.effect;auto& mixer=*context.mixer;const auto& clips=*context.clips;
        auto& slots=e.slots;auto& states=e.states;auto& samples=e.samples;const auto gain_divisor=e.gain_divisor;
        if(i>=slots.size() || v.sample>=clips.size())throw std::runtime_error("effect sample binding invalid");
        const auto slot=slots[i];const auto state=unsigned(std::clamp(static_cast<std::int32_t>(v.state),0,2));
        const auto gain=unsigned(std::clamp(v.gain_q15,0,32767))/gain_divisor;
        const auto pitch=unsigned(std::clamp(std::int64_t(v.pitch_q12)*16,INT64_C(1),INT64_C(262144)));
        const bool changed=state!=states[i] || v.sample!=samples[i];
        if(state==0 || v.sample==0)mixer.stop(slot);
        else if(state==1) {
            if(changed)mixer.play(slot,&clips[v.sample],clips[v.sample].loop_end!=0,gain,pitch,true);
            else mixer.control(slot,gain,pitch);
            mixer.pan(slot,std::uint16_t(v.pan));
        }else if(changed)mixer.release(slot);
        states[i]=state;samples[i]=v.sample;
        return mixer.active(slot)?int(state):0;
    });
    if(graph.retired())stop(mixer);
}
bool RaceEffectsSound::load(const std::filesystem::path& root,const std::string& car,
    int engine,int nitrous,int tyres,std::string& error) {
    try {
        if(nitrous<0||nitrous>3||tyres<0||tyres>3)throw std::runtime_error("invalid effect upgrade");
        RaceEffectsSound next;(void)engine_audio_selection(car,engine);next.engine_level_=unsigned(engine);
        const auto load=[&](Effect& e,const std::string& path,unsigned samples,unsigned params){
            const auto bytes=bank_bytes(root/path);e.graph.load_effect(bytes);const auto bank=parse_abk(bytes);
            for(unsigned i=1;i<=samples;++i)e.clips[i]=decode_clip(bytes,bank,i);
            e.parameters.assign(params,0);
        };
        // Original521D52: row0 NOS, row2 shifts; row2 directory is CAR_2.
        load(next.nitrous_,std::string("Sound/NOS/Nitrous_0")+(nitrous>=2?"1":"0")+"_MB.abk",3,5);
        next.nitrous_.slots={11,23,24,25};next.nitrous_.parameters={0,22000,4096,0,0};
        static constexpr const char* shifts[]={"SML","MED","MED","LRG"};
        load(next.shift_,"Sound/CAR_2/ENG_UG_"+std::to_string(engine)+"/SHIFTING_"+shifts[engine]+"_MB.abk",engine==3?3:2,6);
        next.shift_.slots={26,0,0,0};next.shift_.parameters={1,0,4096,0,0,0};
        next.shift_.gain_divisor=1;
        // Separate original actor handles+1C/+18, sharing decoded shift PCM.
        for(unsigned i=0;i<2;++i){auto& e=next.disengage_[i];
            const auto bytes=bank_bytes(root/"Sound"/"CAR_2"/("ENG_UG_"+std::to_string(engine))/(std::string("SHIFTING_")+shifts[engine]+"_MB.abk"));
            e.graph.load_effect(bytes);e.slots={95+i,0,0,0};e.gain_divisor=1;
            e.parameters={i==0?1:0,0,4096,0,i==0?1:0,0};
        }
        if(engine>=2){
            load(next.backfire_,"Sound/CAR_1/ENG_UG_"+std::to_string(engine)+"/Sparkchatter_01.abk",10,8);
            next.backfire_.slots={97,0,0,0};next.backfire_.gain_divisor=1;
        }
        for(unsigned axle=0;axle<2;++axle){
            auto& e=next.skid_[axle];
            load(e,std::string("Sound/SKIDS/SKID_PAV")+(tyres>=2?"2":"")+"_MB.abk",4,14);
            e.slots=axle?std::array<unsigned,4>{19,20,21,22}:std::array<unsigned,4>{4,16,17,18};
            e.parameters={32766,32766,32766,0,0,0,0,0,4096,4096,4096,0,0,0};
        }
        next.loaded_=true;*this=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void RaceEffectsSound::set_random_context(BankAudioRandom* random)noexcept {
    for(auto* effect:{&nitrous_,&shift_,&backfire_,&disengage_[0],&disengage_[1],&skid_[0],&skid_[1]})effect->graph.set_random_context(random);
}
void RaceEffectsSound::stop(Mixer& mixer) noexcept {
    nitrous_.stop(mixer);shift_.stop(mixer);backfire_.stop(mixer);for(auto& e:disengage_)e.stop(mixer);for(auto& e:skid_)e.stop(mixer);
}
void RaceEffectsSound::update(const RaceAudioSnapshot& s,Mixer& mixer,const RaceShiftSoundCue& shift,const HumanTurboFeedback* human,int backfire_category,TrafficAudioRandom* random) {
    if(!loaded_)return;
    if(s.finished){stop(mixer);return;}
    if(have_tick_&&s.tick==tick_)return;
    if(s.collision_reset || (have_tick_&&s.tick<tick_)){
        nitrous_.reset(mixer);shift_.reset(mixer);for(auto& e:disengage_)e.reset(mixer);disengage_started_={};for(auto& e:skid_)e.reset(mixer);
        have_tick_=nitrous_active_=nitrous_started_=shift_started_=backfire_started_=false;
        if(engine_level_>=2)backfire_.reset(mixer);
        backfire_control_={};
    }
    if(engine_level_>=2&&human&&random){
        const auto cue=backfire_control_.tick(int(engine_level_),human->rpm,human->throttle_on,1.F/60.F,*random,original_backfire_counter);
        if(cue.emit){backfire_.reset(mixer);backfire_started_=true;
            const auto gain=RaceAudioMix::engine_output(unsigned(cue.gain),backfire_category);
            backfire_.parameters={int(cue.variant),int(std::min(gain,32767U)),4096,std::clamp(human->pan,0,65535),
                std::clamp(human->balance,0,1024),int(finite_clamp(human->rpm,0,10000)),
                int(finite_clamp(s.owner_speed*2.236999988555908F,0,250)),0};
        }
    }
    if(backfire_started_)backfire_.run(mixer);
    if(s.nitrous_active&&!nitrous_active_){nitrous_.reset(mixer);nitrous_started_=true;}
    nitrous_.parameters[4]=s.nitrous_active?0:1;
    if(nitrous_started_)nitrous_.run(mixer);
    nitrous_active_=s.nitrous_active;
    if(shift.kind==1||shift.kind==2){
        shift_.reset(mixer);shift_.parameters[0]=int(shift.kind);
        shift_.parameters[1]=shift.gain;shift_.parameters[3]=int(shift.pan);shift_.parameters[5]=int(std::min(shift.bank_variant,1U));shift_started_=true;
    }
    if(shift_started_)shift_.run(mixer);
    for(unsigned i=0;i<2;++i){auto& e=disengage_[i];
        if(shift.disengage&(1U<<i)){e.reset(mixer);e.parameters[1]=shift.disengage_gain;disengage_started_[i]=true;}
        if(disengage_started_[i])e.run(mixer,&shift_.clips);
    }
    for(unsigned axle=0;axle<2;++axle){
        auto& e=skid_[axle];
        // Original event consumes separate axle controls0..1024, yaw degrees
        // and mph. Raw port slip remains an adapter, not original live parity.
        const auto control=[&](double v){return int(finite_clamp(v,0,1)*1024);};
        e.parameters[3]=control(s.axle_long_slip[axle]);
        e.parameters[4]=control(s.axle_lateral_slip[axle]);
        e.parameters[5]=control(s.axle_lateral_slip[1-axle]);
        e.parameters[11]=int(finite_clamp(std::abs(s.yaw_slip_radians)*57.2957763671875,0,180));
        e.parameters[12]=int(finite_clamp(s.speed_mps*double(2.236999988555908F),0,1000));
        e.run(mixer);
    }
    tick_=s.tick;have_tick_=true;
}
std::size_t RaceEffectsSound::resident_bytes() const noexcept {
    const auto bytes=[](const Effect&e){std::size_t n=e.graph.resident_bytes()+e.parameters.capacity()*sizeof(std::int32_t);
        for(const auto& c:e.clips)n+=c.pcm.capacity()*sizeof(std::int16_t);
        return n;};
    return bytes(nitrous_)+bytes(shift_)+bytes(backfire_)+bytes(disengage_[0])+bytes(disengage_[1])+bytes(skid_[0])+bytes(skid_[1]);
}

bool RaceRemoteShiftSound::load(const std::filesystem::path& root,int level,unsigned ordinal,std::string& error) {
    try {
        if(level<0||level>3||ordinal>=3)throw std::runtime_error("invalid remote shift selection");
        RaceRemoteShiftSound next;static constexpr const char* names[]={"SML","MED","MED","LRG"};
        const auto bytes=bank_bytes(root/"Sound"/"CAR_2"/("ENG_UG_"+std::to_string(level))/(std::string("SHIFTING_")+names[level]+"_MB.abk"));
        next.shift_.graph.load_effect(bytes);next.end_.graph.load_effect(bytes);
        const auto bank=parse_abk(bytes);
        for(unsigned i=1;i<=(level==3?3U:2U);++i)next.shift_.clips[i]=decode_clip(bytes,bank,i);
        next.shift_.slots={98+2*ordinal,0,0,0};next.end_.slots={99+2*ordinal,0,0,0};
        next.shift_.gain_divisor=next.end_.gain_divisor=1;
        next.shift_.parameters={1,0,4096,0,0,0};next.end_.parameters={0,0,4096,0,0,0};
        next.loaded_=true;*this=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void RaceRemoteShiftSound::stop(Mixer& mixer)noexcept {
    shift_.stop(mixer);end_.stop(mixer);have_tick_=shift_started_=end_started_=false;
}
void RaceRemoteShiftSound::update(const RaceAudioSnapshot& s,bool active,Mixer& mixer,const RaceShiftSoundCue& cue) {
    if(!loaded_)return;
    if(s.finished||!active){stop(mixer);return;}
    if(have_tick_&&s.tick==tick_)return;
    if(have_tick_&&s.tick<tick_)stop(mixer);
    if(cue.kind==1){shift_.reset(mixer);shift_.parameters={1,cue.gain,4096,int(cue.pan),0,0};shift_started_=true;}
    if(shift_started_)shift_.run(mixer);
    if(cue.disengage&2U){end_.reset(mixer);end_.parameters={0,cue.disengage_gain,4096,0,0,0};end_started_=true;}
    if(end_started_)end_.run(mixer,&shift_.clips);
    tick_=s.tick;have_tick_=true;
}
std::size_t RaceRemoteShiftSound::resident_bytes()const noexcept {
    std::size_t bytes=shift_.graph.resident_bytes()+end_.graph.resident_bytes()+
        (shift_.parameters.capacity()+end_.parameters.capacity())*sizeof(std::int32_t);
    for(const auto& clip:shift_.clips)bytes+=clip.pcm.capacity()*sizeof(std::int16_t);
    return bytes;
}

bool RaceTurboSound::load(const std::filesystem::path& root,const std::string& car,
    int induction,std::string& error) {
    try {
        RaceTurboSound next;const auto bytes=bank_bytes(root/turbo_sound_bank(induction));
        next.spool_.graph.load_effect(bytes);next.release_.graph.load_effect(bytes);
        next.spool_.parameters.assign(6,0);next.release_.parameters.assign(6,0);
        next.spool_.slots={85,0,0,0};next.release_.slots={86,0,0,0};
        next.spool_.gain_divisor=next.release_.gain_divisor=1;
        const auto bank=parse_abk(bytes);
        for(const auto& sample:bank.samples) {
            if(!sample.index||sample.index>=next.spool_.clips.size())throw std::runtime_error("turbo sample binding invalid");
            next.spool_.clips[sample.index]=decode_clip(bytes,bank,sample.index);
        }
        CarAssets assets;if(!load_car_assets(root/"Global/GLOBALB.BUN",car,assets,error))return false;
        std::ifstream tuning(root/"Global/GLOBALB.BUN",std::ios::binary);
        tuning.seekg(static_cast<std::streamoff>(assets.record_offset+0x9c0+unsigned(induction)*100));
        std::array<std::uint8_t,100> record{};
        if(!tuning.read(reinterpret_cast<char*>(record.data()),record.size()))throw std::runtime_error("turbo tuning read failed");
        next.tuning_=turbo_sound_tuning(record.data(),record.size());next.control_.configure(next.tuning_);
        next.loaded_=true;*this=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void RaceTurboSound::stop(Mixer& mixer) noexcept {
    spool_.stop(mixer);release_.stop(mixer);control_.reset();have_tick_=release_started_=false;
}
void RaceTurboSound::update(const RaceAudioSnapshot& s,const HumanTurboFeedback& human,
    Mixer& mixer,int category,TrafficAudioRandom& random) {
    if(!loaded_)return;
    if(s.finished||!s.turbo_installed){stop(mixer);return;}
    if(have_tick_&&s.tick==tick_)return;
    if(s.collision_reset||(have_tick_&&s.tick<tick_)) {
        stop(mixer);spool_.graph.reset_effect();release_.graph.reset_effect();
    }
    TurboSoundInput input;input.pressure=s.turbo_normalized;input.rpm=human.rpm;input.load=human.load;
    input.pan=human.pan;input.balance=human.balance;input.category=category;
    input.release_complete=release_started_&&release_.graph.retired();
    const auto output=control_.tick(input,random);
    publish(output,mixer);tick_=s.tick;have_tick_=true;
}
bool RaceTurboSound::load_remote(const std::filesystem::path& root,const std::string& car,
    int induction,unsigned ordinal,std::string& error) {
    if(ordinal>=3){error="remote Turbo ordinal exceeds roster";return false;}
    if(!load(root,car,induction,error))return false;
    remote_=true;spool_.slots[0]=104+2*ordinal;release_.slots[0]=105+2*ordinal;return true;
}
void RaceTurboSound::update_remote(const RaceAudioSnapshot& s,const RaceAudioSnapshot::Rival& actor,
    const HumanTurboFeedback& feedback,Mixer& mixer,int category,TrafficAudioRandom& random) {
    if(!loaded_||!remote_)return;
    if(s.finished||!actor.active||!actor.turbo_installed){stop(mixer);return;}
    if(have_tick_&&s.tick==tick_)return;
    if(s.collision_reset||(have_tick_&&s.tick<tick_)) {
        stop(mixer);spool_.graph.reset_effect();release_.graph.reset_effect();
    }
    position_.tick(actor.position,s.listener_eye,s.listener_forward);
    TurboSoundInput input;input.pressure=feedback.throttle_on?1.38F:0.F;
    input.rpm=feedback.rpm;input.load=feedback.load;input.pan=feedback.pan;
    input.balance=feedback.balance;input.category=category;input.position=position_.gain;
    input.release_complete=release_started_&&release_.graph.retired();
    publish(control_.tick_remote(input,random),mixer);tick_=s.tick;have_tick_=true;
}
void RaceTurboSound::publish(const TurboSoundOutput& output,Mixer& mixer) {
    if(output.start)spool_.reset(mixer);
    std::copy(output.spool.begin(),output.spool.end(),spool_.parameters.begin());
    spool_.run(mixer);
    if(output.blowoff){release_.reset(mixer);std::copy(output.release.begin(),output.release.end(),release_.parameters.begin());release_started_=true;}
    if(release_started_)release_.run(mixer,&spool_.clips);
}
std::size_t RaceTurboSound::resident_bytes()const noexcept {
    std::size_t bytes=spool_.graph.resident_bytes()+release_.graph.resident_bytes();
    bytes+=(spool_.parameters.capacity()+release_.parameters.capacity())*sizeof(std::int32_t);
    for(const auto& clip:spool_.clips)bytes+=clip.pcm.capacity()*sizeof(std::int16_t);
    return bytes;
}

bool RacePropSound::load(const std::filesystem::path& root,std::string& error,std::uint32_t materials) {
    try {
        RacePropSound next;const auto bytes=bank_bytes(root/"Sound/IG_GLOBAL/IMPACT_MB.abk");
        // collision_graph also authenticates this bank's SHA in the service.
        if(sha256_hex(sha256(bytes.data(),bytes.size()))!=
            "C1EE8237C292E2934E296CAF10E7A78B25BDAE8326C517D1AA034379362CC6D5")
            throw std::runtime_error("unsupported smokeable impact bank");
        const auto bank=parse_abk(bytes);
        // Smokeable5314B0/52F7B0 selects the descriptor material variant.
        // The first module maps its29 variants to authored samples1..29.
        // Preload only families present in the course. Every variant remains
        // authored PCM; no decode, allocation or file access occurs on a hit.
        std::array<bool,30> selected{};
        for(unsigned material=0;material<32;++material)if(materials&(1U<<material)) {
            std::array<unsigned,29> counters{};
            for(unsigned variant=0;variant<3;++variant)
                selected[prop_collision_variant(material,counters)+1]=true;
        }
        for(unsigned i=1;i<next.clips_.size();++i)if(selected[i])next.clips_[i]=decode_clip(bytes,bank,i);
        for(auto& v:next.voices_)v.graph.load_effect(bytes);
        next.loaded_=true;*this=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void RacePropSound::stop(Mixer& mixer) noexcept {
    for(unsigned i=0;i<voices_.size();++i){mixer.stop(27+i);voices_[i].live=false;}
}
std::size_t RacePropSound::resident_bytes() const noexcept {
    std::size_t n=0;for(const auto& c:clips_)n+=c.pcm.capacity()*sizeof(std::int16_t);
    for(const auto& v:voices_)n+=v.graph.resident_bytes();
    return n;
}
bool RacePropSound::prepare(const RaceAudioSnapshot& s,Mixer& mixer) {
    if(!loaded_ || (have_tick_&&s.tick==tick_))return false;
    if(s.finished||s.collision_reset||(have_tick_&&s.tick<tick_)) {
        stop(mixer);pairs_={};
        if(s.finished){tick_=s.tick;have_tick_=true;return false;}
    }
    for(auto& v:voices_){v.order=0;v.rendered=false;}
    const auto count=s.collision_reset?0:std::min(s.prop_contacts.count,s.prop_contacts.hits.size());
    unsigned admitted=0;
    for(std::size_t i=0;i<count;++i) {
        const auto& hit=s.prop_contacts.hits[i];
        const double j=std::sqrt(hit.impulse.x*hit.impulse.x+hit.impulse.y*hit.impulse.y+hit.impulse.z*hit.impulse.z);
        if(!s.physics_step)continue;
        Pair* pair=nullptr;
        for(auto& p:pairs_)if(p.id==hit.id){pair=&p;break;}
        if(!pair)for(auto& p:pairs_)if(p.id==UINT32_MAX || s.physics_step>=p.step+36){pair=&p;p={hit.id,0,0};break;}
        if(!pair)continue;
        const bool begin=!pair->step||s.physics_step>=pair->step+36;
        if(begin)pair->magnitude=0;
        pair->step=s.physics_step;
        //531670 reads retained record+60, not only this response's impulse.
        pair->magnitude=hit.magnitude_sum>=0?hit.magnitude_sum:pair->magnitude+float(j);
        const int intensity=prop_collision_intensity(pair->magnitude,hit.inverse_mass);
        if(!begin) {
            for(auto& v:voices_)if(v.live&&v.pair==hit.id)v.intensity=intensity;
            continue;
        }
        //531670 still publishes a zero-intensity controller. A pinned first
        //response must not suppress the later activation response of that pair.
        if(admitted==voices_.size())continue;
        ++admitted;
        auto& v=voices_[next_];mixer.stop(27+next_);v.graph.reset_effect();
        const auto variant=prop_collision_variant(hit.material,variants_);
        v.live=true;v.sample=v.state=0;v.variant=variant;v.intensity=intensity;
        v.order=hit.audio_order;v.pair=hit.id;next_=(next_+1)%voices_.size();
    }
    tick_=s.tick;have_tick_=true;return true;
}
void RacePropSound::render_voice(unsigned i,Mixer& mixer,int category_gain,int global_gain) {
    if(i>=voices_.size())return;
    auto& v=voices_[i];if(!v.live||v.rendered)return;
    v.rendered=true;
    // Smokeable531760/52F2C0 parameter order; subtype7.
    v.graph.effect_tick({int((((INT64_C(22000)*category_gain)>>15)*global_gain)>>15),4096,32768,int(v.variant),v.intensity},[&](unsigned,const EngineAudioVoice& c) {
        if(c.sample==0||c.sample>=clips_.size()||c.state>2)throw std::runtime_error("smokeable Player binding invalid");
        const unsigned slot=27+i,gain=unsigned(std::clamp(c.gain_q15,0,32767))/2;
        const auto pitch=unsigned(std::clamp(std::int64_t(c.pitch_q12)*16,INT64_C(1),INT64_C(262144)));
        if(c.state==0)mixer.stop(slot);
        else if(c.state==1) {
            if(v.state!=1||v.sample!=c.sample)mixer.play(slot,&clips_[c.sample],false,gain,pitch,true);
            else mixer.control(slot,gain,pitch);
            mixer.pan(slot,std::uint16_t(c.pan));
        }else if(v.state!=2)mixer.release(slot);
        v.state=c.state;v.sample=c.sample;return mixer.active(slot)?int(c.state):0;
    });
    if(v.graph.retired()){mixer.stop(27+i);v.live=false;}
}
void RacePropSound::update(const RaceAudioSnapshot& s,Mixer& mixer,int category_gain,int global_gain) {
    if(prepare(s,mixer))for(unsigned i=0;i<voices_.size();++i)render_voice(i,mixer,category_gain,global_gain);
}
void dispatch_race_impacts(RacePropSound* props,Mixer& mixer,CollisionVoice* const* impacts,std::size_t count,
    int category,int global,const std::function<void(CollisionVoice&)>& start) {
    struct Command {std::uint32_t order;unsigned index;bool prop;};
    std::array<Command,kCollisionImpactCapacity+2> commands{};unsigned used=0;
    if(count>kCollisionImpactCapacity)throw std::runtime_error("impact dispatch exceeds backend capacity");
    for(unsigned i=0;i<count;++i)commands[used++]={impacts[i]->audio_order,i,false};
    if(props)for(unsigned i=0;i<2;++i)if(props->voice_order(i)!=UINT32_MAX)
        commands[used++]={props->voice_order(i),i,true};
    // Stable insertion sort allocates nothing and retains legacy unnumbered order.
    for(unsigned i=1;i<used;++i) {
        const auto value=commands[i];auto at=i;
        while(at&&value.order<commands[at-1].order){commands[at]=commands[at-1];--at;}
        commands[at]=value;
    }
    for(unsigned i=0;i<used;++i) {
        const auto& c=commands[i];
        if(c.prop)props->render_voice(c.index,mixer,category,global);
        else start(*impacts[c.index]);
    }
}

bool RaceTrafficSound::load(const std::filesystem::path& root,std::string& error,unsigned count) {
    try {
        if(count>kRaceTrafficCapacity)throw std::runtime_error("traffic sound pool exceeds capacity");
        RaceTrafficSound next;next.actors_.resize(count);const auto bytes=bank_bytes(root/"Sound/IG_GLOBAL/ENV_COMMON_MB.abk");
        if(sha256_hex(sha256(bytes.data(),bytes.size()))!="159A35D09F62AF5E13A5DE1BB8BCA3559ED822E10E6932356A42C93E7F1FFAF3")
            throw std::runtime_error("unsupported traffic environment bank");
        const auto bank=parse_abk(bytes);
        for(unsigned sample:{1U,2U,3U,6U,7U,8U})next.clips_[sample]=decode_clip(bytes,bank,sample);
        for(auto& actor:next.actors_)for(auto& voice:actor.voices){voice.graph.load_effect(bytes);voice.parameters.resize(5);}
        next.loaded_=true;*this=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void RaceTrafficSound::stop(Mixer& mixer)noexcept {
    for(unsigned i=0;i<actors_.size();++i){mixer.stop(race_traffic_voice_slot(i,0));mixer.stop(race_traffic_voice_slot(i,1));actors_[i].active=false;}
    have_tick_=false;
}
std::size_t RaceTrafficSound::resident_bytes()const noexcept {
    std::size_t bytes=actors_.capacity()*sizeof(Actor);for(const auto& c:clips_)bytes+=c.pcm.capacity()*sizeof(std::int16_t);
    for(const auto& a:actors_)for(const auto& v:a.voices)bytes+=v.graph.resident_bytes()+v.parameters.capacity()*sizeof(std::int32_t);
    return bytes;
}
void RaceTrafficSound::update(const RaceAudioSnapshot& s,Mixer& mixer,int environment_gain,TrafficAudioRandom* shared_random) {
    if(!loaded_||(have_tick_&&tick_==s.tick))return;
    if(s.finished){stop(mixer);return;}
    if(have_tick_&&s.tick<tick_){stop(mixer);random_={};}
    tick_=s.tick;have_tick_=true;
    for(unsigned i=0;i<actors_.size();++i) {
        auto& a=actors_[i];const auto& input=s.traffic[i];
        if(!input.active){if(a.active){mixer.stop(race_traffic_voice_slot(i,0));mixer.stop(race_traffic_voice_slot(i,1));}a.active=false;continue;}
        if(!a.active||a.generation!=input.generation) {
            a.controller={};a.position={};random_.last_horn=0;
            for(auto& v:a.voices){v.graph.reset_effect();v.state=v.sample=0;}
            mixer.stop(race_traffic_voice_slot(i,0));mixer.stop(race_traffic_voice_slot(i,1));a.active=true;a.generation=input.generation;
        }
        auto controls=input.input;
        a.position.tick(controls.position,controls.human_position,s.listener_eye,s.listener_forward,controls.dt*1000.F);
        controls.gain=a.position.gain;controls.pan=a.position.pan;controls.doppler=a.position.doppler;
        controls.category=environment_gain;
        // Original5365E0 fresh0.9 preference ->global pass-by level.
        controls.global_gain=22000+((29490*10767)>>15);
        controls.pass_complete=a.controller.pass.live&&!mixer.active(race_traffic_voice_slot(i,1));
        a.controller.tick(controls,shared_random?*shared_random:random_);
        for(unsigned side=0;side<2;++side) {
            auto& v=a.voices[side];const auto& c=side?a.controller.pass:a.controller.horn;const unsigned slot=race_traffic_voice_slot(i,side);
            if(c.stop){mixer.stop(slot);v.graph.reset_effect();v.state=v.sample=0;}
            if(!c.live)continue;
            if(c.start){mixer.stop(slot);v.graph.reset_effect();v.state=v.sample=0;}
            v.parameters[0]=int(c.event);v.parameters[1]=int(c.gain);v.parameters[2]=int(c.pitch);
            v.parameters[3]=int(c.pan);v.parameters[4]=int(c.flags);
            v.graph.effect_tick(v.parameters,[&](unsigned player,const EngineAudioVoice& out){
                if(player!=0||out.sample>=clips_.size()||!clips_[out.sample].valid())throw std::runtime_error("traffic Player binding invalid");
                const auto gain=unsigned(std::clamp(out.gain_q15,0,32767));
                const auto pitch=unsigned(std::clamp(std::int64_t(out.pitch_q12)*16,INT64_C(1),INT64_C(262144)));
                if(out.state==0)mixer.stop(slot);
                else if(out.state==1) {
                    if(v.state!=1||v.sample!=out.sample)mixer.play(slot,&clips_[out.sample],clips_[out.sample].loop_end!=0,gain,pitch);
                    else mixer.control(slot,gain,pitch);
                    mixer.pan(slot,std::uint16_t(out.pan));
                }else if(v.state!=2)mixer.release(slot);
                v.state=out.state;v.sample=out.sample;return mixer.active(slot)?int(out.state):0;
            });
        }
    }
}

RaceAudioSnapshot race_audio_snapshot(const RaceSession& session) noexcept {
    RaceAudioSnapshot snapshot;
    const auto& car = session.car();
    snapshot.rpm = finite_clamp(car.engine_rpm, 0, 20000);
    snapshot.maximum_rpm=session.spec().handling.max_rpm;
    float induction=0;for(float value:session.spec().induction)induction=std::max(induction,value);
    snapshot.turbo_installed=induction>0;
    snapshot.turbo_normalized=induction>0?session.turbo_pressure()/(induction*4.F):0.F;
    snapshot.torque_maximum=0.F;
    for(float torque:session.spec().handling.torque)snapshot.torque_maximum=std::max(snapshot.torque_maximum,torque);
    snapshot.gear=car.gear;
    snapshot.first_gear_ratio=session.spec().handling.gear_ratios[2];
    snapshot.gear_ratio=session.spec().handling.gear_ratios[unsigned(std::clamp(car.gear,0,int(kVehicleGearSlots)-1))];
    snapshot.owner_speed=float(car.owner_speeds.magnitude);
    snapshot.race_ticks=session.timing().race_ticks;
    const auto& telemetry = session.telemetry().model;
    snapshot.throttle = finite_clamp(telemetry.throttle, 0, 1);
    snapshot.engine_drive_load=finite_clamp(telemetry.audio_transmitted_torque,-100000,100000);
    snapshot.brake=finite_clamp(telemetry.brake,0,1);snapshot.handbrake=telemetry.handbrake;
    snapshot.engine_game_state=int(session.timing().stage);
    snapshot.speed_mps = finite_clamp(
        std::sqrt(car.velocity.x * car.velocity.x + car.velocity.y * car.velocity.y),
        0, 1000);
    for (const auto& wheel : telemetry.wheels)
        if (wheel.contact)
            snapshot.wheel_slip = std::max(snapshot.wheel_slip,
                std::max(std::abs(wheel.slip_ratio), std::abs(wheel.slip_lateral)));
    snapshot.wheel_slip = finite_clamp(snapshot.wheel_slip, 0, 100);
    for(unsigned i=0;i<telemetry.wheels.size();++i){const auto&w=telemetry.wheels[i];if(!w.contact)continue;
        snapshot.axle_long_slip[i/2]=std::max(snapshot.axle_long_slip[i/2],finite_clamp(std::abs(w.slip_ratio),0,1));
        snapshot.axle_lateral_slip[i/2]=std::max(snapshot.axle_lateral_slip[i/2],finite_clamp(std::abs(w.slip_lateral),0,1));}
    const double along=telemetry.forward.x*car.velocity.x+telemetry.forward.y*car.velocity.y;
    const double across=-telemetry.forward.y*car.velocity.x+telemetry.forward.x*car.velocity.y;
    if(snapshot.speed_mps>0.1)snapshot.yaw_slip_radians=std::atan2(across,std::abs(along));
    snapshot.tick = session.timing().step;
    snapshot.physics_step = session.telemetry().step;
    snapshot.prop_contacts=session.prop_contacts();
    const auto vector=[](const VehicleVec3& p){return std::array<float,3>{{float(p.x),float(p.y),float(p.z)}};};
    snapshot.listener_eye=vector(session.camera().eye);snapshot.listener_forward=vector(session.camera().forward);
    snapshot.human_position=vector(car.position);snapshot.human_forward=vector(telemetry.forward);
    for(unsigned i=0;i<session.rival_count()&&i<snapshot.rivals.size();++i) {
        auto& out=snapshot.rivals[i];const auto& rival=session.rival(i);const auto& model=rival.telemetry().model;
        out.active=true;out.nitrous=rival.nitrous().active;
        out.position=vector(rival.car().position);out.forward=vector(model.forward);
        out.engine.rpm=float(rival.car().engine_rpm);out.engine.maximum_rpm=rival.spec().handling.max_rpm;
        out.engine.drive_load=model.audio_transmitted_torque;out.engine.throttle=float(model.throttle);
        out.engine.brake=float(model.brake);out.engine.handbrake=model.handbrake;
        out.engine.gear=rival.car().gear;out.engine.game_state=int(session.timing().stage);
        out.engine.first_gear_ratio=rival.spec().handling.gear_ratios[2];
        const auto gear=std::clamp(out.engine.gear,0,int(kVehicleGearSlots)-1);
        out.engine.gear_ratio=rival.spec().handling.gear_ratios[unsigned(gear)];
        out.turbo_installed=std::any_of(rival.spec().induction.begin(),rival.spec().induction.end(),[](float value){return value>0.F;});
    }
    const auto& race_clock=session.timing();
    snapshot.countdown_seconds=race_clock.stage==RaceStage::countdown?
        session.timing_config().countdown_step_seconds*3.F-float(race_clock.step_ticks-race_clock.countdown_start_ticks)*.00025F:0.F;
    snapshot.camera_pov=unsigned(session.camera().pov);snapshot.camera_mover=session.crash_camera_event();
    if(!snapshot.camera_mover)snapshot.camera_mover=1;
    if(const auto* traffic=session.traffic())for(unsigned i=0;i<traffic->actors().size()&&i<snapshot.traffic.size();++i){
        const auto& actor=traffic->actors()[i];auto& out=snapshot.traffic[i];out.active=actor.active;out.generation=actor.activations;
        auto& sound=out.input;sound.position=vector(actor.car.position);sound.velocity=vector(actor.car.velocity);
        sound.human_position=vector(car.position);sound.human_velocity=vector(car.velocity);sound.human_forward=vector(telemetry.forward);
        sound.human_speed=car.owner_speeds.forward;sound.car_type=int(actor.setup.car_type);sound.drag=session.is_drag();
    }

    const auto& pairs=session.pair_contacts();
    // Only player pairs: remote impacts need spatial attenuation first.
    for(std::size_t i=0;i<pairs.count;++i){const auto& p=pairs.pairs[i];
        if(pairs.step+1!=snapshot.tick||p.first!=0||!p.contact.applied||!snapshot.physics_step||snapshot.contact_count==snapshot.contacts.size())continue;
        const auto& c=p.contact;
        auto& dst=snapshot.contacts[snapshot.contact_count++];
        dst.audio_order=p.audio_order;dst.step=snapshot.physics_step;dst.pair=0x80000000U|p.second;dst.other_kind=p.other_kind;
        if(p.other_kind==4){dst.forward=p.forward;dst.up=p.up;dst.slide_mps=p.slide_mps;}
        dst.impulse={c.impulse[0],c.impulse[1],c.impulse[2]};dst.point=c.point;dst.point_valid=true;
        dst.car_subtype=car_collision_subtype(c.position_a,c.position_b,c.velocity_a,c.velocity_b,c.point);
    }
    for (const auto& step : session.physics_telemetry()) {
        snapshot.collision_reset |= step.reset != VehicleTrackReset::none;
        for (std::size_t i = 0; i < step.audio_contact_count && i < step.audio_contacts.size(); ++i) {
            if (snapshot.contact_count == snapshot.contacts.size()) break;
            const auto& c = step.audio_contacts[i];
            auto& copied=snapshot.contacts[snapshot.contact_count++];
            copied={step.step,c.polygon,c.impulse,c.forward,c.up,c.slide_mps,3};
            copied.point=c.point;copied.point_valid=true;copied.audio_order=c.audio_order;
        }
    }
    // The production shared solver emits one order for world and car pairs.
    // Keep that order across copied telemetry groups instead of forcing every
    // car hit before every wall hit. Legacy unnumbered observations stay stable.
    std::stable_sort(snapshot.contacts.begin(),snapshot.contacts.begin()+snapshot.contact_count,
        [&](const auto& a,const auto& b){
            const auto ka=a.audio_order?snapshot.physics_step:a.step,kb=b.audio_order?snapshot.physics_step:b.step;
            return ka!=kb?ka<kb:a.audio_order<b.audio_order;
        });
    const auto& timing = session.timing();
    snapshot.countdown = timing.countdown_shown;
    if (!timing.racers.empty())
        snapshot.completed_laps = timing.racers[0].highest_lap;
    snapshot.finished = timing.outcome == RaceOutcome::finished;
    snapshot.nitrous_active = session.nitrous().active;
    snapshot.crash_cinematic = session.crash_camera_event()==6 || session.crash_camera_event()==7;
    return snapshot;
}

RaceInterfaceCue RaceCueScheduler::update(const RaceAudioSnapshot& snapshot) noexcept {
    RaceInterfaceCue cue = RaceInterfaceCue::none;
    if (snapshot.finished && !finished_)
        cue = RaceInterfaceCue::finish;
    else if (snapshot.completed_laps > completed_laps_)
        cue = RaceInterfaceCue::lap;
    else if (snapshot.countdown != countdown_) {
        if (snapshot.countdown >= 1 && snapshot.countdown <= 3)
            cue = RaceInterfaceCue::countdown;
        else if (snapshot.countdown == 4)
            cue = RaceInterfaceCue::go;
    }
    countdown_ = snapshot.countdown;
    completed_laps_ = snapshot.completed_laps;
    finished_ = snapshot.finished;
    return cue;
}

std::vector<Segment> race_music_playlist(const Bank& bank,
                                         const std::vector<Segment>& indexed) {
    if (indexed.size() < 2 || indexed.front().offset != 0)
        throw std::runtime_error("music segment 0 absent");
    std::vector<Segment> playlist;
    for (const auto& entry : bank.entries) {
        if (entry.type != 1 || entry.value == indexed.front().offset) continue;
        const auto found = std::find_if(indexed.begin(), indexed.end(),
            [&](const Segment& segment) { return segment.offset == entry.value; });
        if (found == indexed.end())
            throw std::runtime_error("music entry misses AST segment");
        playlist.push_back(*found);
    }
    if (playlist.empty()) throw std::runtime_error("music playlist empty");
    return playlist;
}

std::array<double, 4> engine_layer_weights(double rpm) noexcept {
    constexpr std::array<double, 4> centers{800, 2500, 4500, 7000};
    std::array<double, 4> weight{};
    rpm = finite_clamp(rpm, centers[0], 8000);
    if (rpm <= centers[0]) weight[0] = 1;
    else if (rpm >= centers[3]) weight[3] = 1;
    else for (std::size_t i = 0; i + 1 < centers.size(); ++i)
        if (rpm >= centers[i] && rpm < centers[i + 1]) {
            const double upper = (rpm - centers[i]) / (centers[i + 1] - centers[i]);
            weight[i] = 1 - upper;
            weight[i + 1] = upper;
            break;
        }
    return weight;
}

struct RaceAudioService::Impl {
    FrontendSfx frontend;FrontendSpeech post_voice;SpeechMusicDuck post_voice_duck;
    std::atomic<unsigned> ui_music{100},ui_effects{100};
    RaceEngineSound engine;
    std::array<RaceEngineSound,3> remote_engines;
    std::array<RaceRemoteShiftSound,3> remote_shifts;
    std::array<RaceTurboSound,3> remote_turbos;
    std::array<bool,3> has_remote_turbos{};
    std::array<bool,3> has_remote_shifts{};
    std::array<RaceAudioMix,3> remote_mix;
    std::array<bool,3> has_remote{};
    TrafficAudioRandom sound_random,feedback_world_random;
    RaceTrafficSound traffic;bool has_traffic=false;
    RaceEffectsSound effects;
    RaceTurboSound turbo;bool has_turbo=false;
    RacePropSound props;bool has_props=false;
    PcmClip scrape;
    bool has_effects=false;
    std::array<PcmClip, 13> impacts;
    CollisionGraph collision;
    CollisionScheduler collisions;
    static constexpr std::array<unsigned,kCollisionImpactCapacity> impact_slots{{5,8,110,111,112}};
    struct ImpactBinding {std::uint32_t pair=UINT32_MAX;std::uint64_t episode=0;float distance=0;};
    std::array<ImpactBinding,kCollisionImpactCapacity> impact_bindings{};
    std::array<std::uint32_t, 2> scrape_pairs{UINT32_MAX,UINT32_MAX};
    std::array<PcmClip, 4> cue_clips;
    bool has_engine=false;
    std::array<bool, 4> has_cue{};
    bool has_impact = false, has_music = false, active = false;
    std::size_t music_tracks = 0;
    bool original_music=false;unsigned music_first=0;
    std::vector<std::uint64_t> music_lengths;
    std::atomic<std::uint64_t> music_frames{0};
    Mixer mixer;
    RaceAudioMix mix;
    bool have_mix_tick=false;
    std::uint64_t mix_tick=0;
    RaceCueScheduler cues;
    AstStreamer stream;
    Output output;
    std::atomic<bool> suspended{false};
    std::atomic<bool> finished{false};
    mutable std::mutex mixer_mutex;
    std::string note;
    OutputFade fade;
    OutputFade music_fade;

    Impl() { mixer.set_peak_limiter(true); }

    void render(std::int16_t* stereo, std::size_t frames) {
        const bool muted = suspended.load();
        if (muted && fade.silent()) {
            std::fill_n(stereo, frames * 2, std::int16_t{0});
            frontend.mix(stereo,frames);post_voice.mix(stereo,frames);return;
        }
        std::array<std::int16_t, 1024 * 2> music{};
        std::size_t offset = 0;
        while (offset < frames) {
            const auto count = std::min<std::size_t>(1024, frames - offset);
            // Finish stops car effects through on_tick; the song continues through results.
            const bool ending = false;
            const bool stream_ready = has_music &&
                (!ending || !music_fade.silent()) &&
                (!stream.finished() || stream.ring().size());
            if (stream_ready) {
                music_frames.fetch_add(stream.pop(music.data(), count),std::memory_order_relaxed);
                music_fade.apply(music.data(), count, ending);
                // Preserve the race music's existing master; apply only the
                //original speech envelope's ratio while the post voice plays.
                post_voice_duck.apply(music.data(),count,post_voice.active(),true);
            }
            {
                std::lock_guard<std::mutex> lock(mixer_mutex);
                const auto music_gain = mix.music_device_gain();
                const auto user_music=ui_music.load(),user_effects=ui_effects.load();
                if(user_effects==100) {
                    mixer.render(stereo+offset*2,count,stream_ready?music.data():nullptr,music_gain*user_music/100);
                }else {
                    mixer.render(stereo+offset*2,count);
                    for(std::size_t sample=0;sample<count*2;++sample) {
                        const int value=int(stereo[offset*2+sample])*int(user_effects)/100+
                            (stream_ready?int(std::int64_t(music[sample])*music_gain*user_music/(32768*100)):0);
                        stereo[offset*2+sample]=std::int16_t(std::clamp(value,-32768,32767));
                    }
                }
            }
            fade.apply(stereo + offset * 2, count, muted);
            offset += count;
        }
        frontend.mix(stereo,frames);post_voice.mix(stereo,frames);
    }
};

RaceAudioService::RaceAudioService() : impl_(std::make_unique<Impl>()) {}
RaceAudioService::~RaceAudioService() { stop(); }

bool RaceAudioService::start(const std::filesystem::path& root,
                             std::string& diagnostic,
                             const std::function<void(const char*)>& progress,
                             const std::function<bool()>& decoder_thread_setup,
                             bool original_ig_playlist,const std::string& car,int engine_upgrade,int nitrous_upgrade,int tyre_upgrade,const std::vector<RaceRemoteEngineBinding>& rivals,int induction_upgrade,unsigned traffic_count,std::uint32_t prop_materials) {
    auto& p = *impl_;
    if (p.active) { diagnostic = "audio already running"; return false; }
    if(rivals.size()>p.remote_engines.size()){diagnostic="remote sound roster exceeds capacity";return false;}
    p.mix=RaceAudioMix{};p.have_mix_tick=false;p.mix_tick=0;p.post_voice.stop();p.post_voice_duck=SpeechMusicDuck{};
    p.sound_random={};p.feedback_world_random={};p.feedback_world_random.state=0x60000000U;
    p.has_remote.fill(false);p.has_remote_shifts.fill(false);p.has_remote_turbos.fill(false);p.remote_mix={};
    p.collisions.clear(); p.impact_bindings = {};
    p.scrape_pairs = {UINT32_MAX,UINT32_MAX};
    std::ostringstream notes;
    std::string ui_error;if(!p.frontend.load(root,ui_error))notes<<"frontend: "<<ui_error<<"; ";
    if(progress)progress("audio tyres, shifts and nitrous");
    std::string effect_error;
    p.has_effects=p.effects.load(root,car,engine_upgrade,nitrous_upgrade,tyre_upgrade,effect_error);
    if(!p.has_effects)notes<<"effects: "<<effect_error<<"; ";
    if(progress)progress("audio turbo spool and release");
    std::string turbo_error;p.has_turbo=p.turbo.load(root,car,induction_upgrade,turbo_error);
    if(!p.has_turbo)notes<<"turbo: "<<turbo_error<<"; ";
    if(progress)progress("audio engine and exhaust");
    std::string engine_error;
    p.has_engine=p.engine.load(root,car,engine_upgrade,engine_error);
    if(!p.has_engine)notes<<"engine: "<<engine_error<<"; ";
    for(unsigned i=0;i<rivals.size();++i) {
        if(progress)progress("audio rival engine and exhaust");
        std::string remote_error;
        p.has_remote[i]=p.remote_engines[i].load_remote(root,rivals[i].car,rivals[i].engine_upgrade,i,remote_error);
        if(!p.has_remote[i])notes<<"remote"<<i<<": "<<remote_error<<"; ";
        p.has_remote_shifts[i]=p.remote_shifts[i].load(root,rivals[i].engine_upgrade,i,remote_error);
        if(!p.has_remote_shifts[i])notes<<"remote shift"<<i<<": "<<remote_error<<"; ";
        if(rivals[i].turbo_installed) {
            p.has_remote_turbos[i]=p.remote_turbos[i].load_remote(root,rivals[i].car,rivals[i].induction_upgrade,i,remote_error);
            if(!p.has_remote_turbos[i])notes<<"remote Turbo"<<i<<": "<<remote_error<<"; ";
        }
    }
    // Decode the large FE input before retaining impact PCM. The temporary
    // bank then cannot overlap every already-loaded collision clip at peak.
    if (progress) progress("audio interface cues");
    try {
        const auto bytes = bank_bytes(root / "Sound/FE/FE_MB.abk");
        const auto bank = parse_abk(bytes);
        // Lap/finish FE samples29/30 were provisional unrelated menu effects.
        // Crossing a race line must not start these unverified audio cues.
        // Scheduler transitions remain available; only countdown/go are bound.
        constexpr std::array<std::uint32_t, 2> samples{25, 26};
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (progress) progress("audio interface cues");
            try {
                p.cue_clips[i] = decode_clip(bytes, bank, samples[i]);
                p.has_cue[i] = true;
            } catch (const std::exception& e) {
                notes << "cue sample " << samples[i] << ": " << e.what() << "; ";
            }
        }
    } catch (const std::exception& e) { notes << "cues: " << e.what() << "; "; }
    if(progress)progress("audio traffic horns and pass-by");
    std::string traffic_error;p.has_traffic=traffic_count&&p.traffic.load(root,traffic_error,traffic_count);
    if(traffic_count&&!p.has_traffic)notes<<"traffic: "<<traffic_error<<"; ";
    if(progress)progress("audio small props");
    std::string prop_error;p.has_props=p.props.load(root,prop_error,prop_materials);
    if(!p.has_props)notes<<"small props: "<<prop_error<<"; ";
    if (progress) progress("audio impact");
    try {
        const auto impact_bytes = bank_bytes(root / "Sound/IG_GLOBAL/IMPACT_MB.abk");
        const auto scrape_bytes = bank_bytes(root / "Sound/IG_GLOBAL/SCRAPE_MB.abk");
        p.collision = collision_graph(impact_bytes,scrape_bytes);
        const auto impact_bank = parse_abk(impact_bytes), scrape_bank = parse_abk(scrape_bytes);
        for (std::size_t i = 0; i < p.impacts.size(); ++i)
            p.impacts[i] = decode_clip(impact_bytes,impact_bank,30+std::uint32_t(i));
        p.scrape = decode_clip(scrape_bytes,scrape_bank,1);
        p.has_impact = true;
    } catch (const std::exception& e) { notes << "collision: " << e.what() << "; "; }
    if (progress) progress("audio music index");
    try {
        auto input = std::make_unique<std::ifstream>(
            root / "SNDSTREAMS/STRM_Music_UG0_MB.ast", std::ios::binary);
        if (!*input) throw std::runtime_error("missing AST");
        const auto bank = parse_abk(bank_bytes(
            root / "Sound/STRMBANK/STRM_Music_UG0_MB.abk"));
        std::vector<std::uint64_t> offsets;
        for (const auto& entry : bank.entries)
            if (entry.type == 1) offsets.push_back(entry.value);
        std::sort(offsets.begin(), offsets.end());
        if (offsets.size() < 2 || offsets.size() > 128 ||
            offsets.front() != 0 ||
            std::adjacent_find(offsets.begin(), offsets.end()) != offsets.end())
            throw std::runtime_error("invalid music bank offsets");
        input->seekg(0, std::ios::end);
        const auto end = input->tellg();
        if (end < 0) throw std::runtime_error("music AST size unavailable");
        const auto file_size = static_cast<std::uint64_t>(end);
        std::vector<Segment> indexed;
        indexed.reserve(offsets.size());
        for (std::size_t i = 0; i < offsets.size(); ++i) {
            if (progress) progress("audio playlist index");
            indexed.push_back(index_ast_segment_by_boundary(*input,
                offsets[i], i + 1 < offsets.size() ? offsets[i + 1] : file_size));
        }
        auto playlist = original_ig_playlist ? original_music_playlist(bank,indexed,false) : race_music_playlist(bank, indexed);
        p.music_first=original_ig_playlist?original_race_music_next%unsigned(playlist.size()):0;
        std::rotate(playlist.begin(),playlist.begin()+p.music_first,playlist.end());
        p.music_lengths.clear();p.music_frames=0;
        for(const auto& song:playlist)p.music_lengths.push_back((std::uint64_t(song.frames)*kOutputRate+song.rate-1)/song.rate);
        std::string error;
        const auto track_count = playlist.size();
        p.has_music = p.stream.start_playlist(std::move(input),
            std::move(playlist), true, error, decoder_thread_setup);
        if (!p.has_music) throw std::runtime_error(error);
        p.music_tracks = track_count;
    } catch (const std::exception& e) { notes << "music: " << e.what() << "; "; }
    for (unsigned attempt = 0; p.has_music && attempt < 100 &&
            p.stream.ring().size() < 3 * 1024 && !p.stream.finished(); ++attempt) {
        if (progress) progress("audio music prefill");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!p.has_traffic && !p.has_props && !p.has_effects && !p.has_impact && !p.has_music &&
        !p.has_engine &&
        std::none_of(p.has_cue.begin(), p.has_cue.end(), [](bool x) { return x; })) {
        diagnostic = "no supported retail audio: " + notes.str();
        p.note = diagnostic;
        return false;
    }
    if (progress) progress("audio output");
    std::string device_error;
    if (!p.output.start([&p](std::int16_t* pcm, std::size_t frames) {
            p.render(pcm, frames);
        }, device_error)) {
        p.stream.stop();
        diagnostic = "audio device unavailable: " + device_error + "; " + notes.str();
        p.note = diagnostic;
        return false;
    }
    p.original_music=original_ig_playlist&&p.has_music;
    p.active = true;
    p.note = "audio ready; tracks=" + std::to_string(p.music_tracks);
    if(p.original_music)p.note+="; music_bank="+std::to_string(p.music_first+1);
    p.note+="; rival_engines="+std::to_string(std::count(p.has_remote.begin(),p.has_remote.end(),true));
    if(p.has_engine)p.note+="; engine="+p.engine.selection().engine+"; exhaust=on";
    if(p.has_traffic)p.note+="; traffic=original_environment_graph";
    if(p.has_props)p.note+="; props=original_smokeable_graph";
    if(p.has_effects)p.note+="; tyres=graph; shifts=on; nitrous=graph";
    if (!notes.str().empty()) p.note += "; " + notes.str();
    diagnostic = p.note;
    return true;
}

void RaceAudioService::on_tick(const RaceAudioSnapshot& snapshot,std::uint32_t* world_random) {
    auto& p = *impl_;
    if (!p.active || p.suspended.load()) return;
    std::lock_guard<std::mutex> lock(p.mixer_mutex);
    if(!p.have_mix_tick || snapshot.tick!=p.mix_tick) {
        if(p.have_mix_tick && snapshot.tick<p.mix_tick){
            p.mix=RaceAudioMix{};p.remote_mix={};p.sound_random={};p.feedback_world_random={};
            p.feedback_world_random.state=0x60000000U;
        }
        p.mix.begin_frame(snapshot.nitrous_active);
        for(unsigned i=0;i<p.remote_mix.size();++i)p.remote_mix[i].race(snapshot.rivals[i].nitrous,false);
        p.have_mix_tick=true;p.mix_tick=snapshot.tick;
    }
    RaceShiftSoundCue shift_cue;
    if(p.has_engine) {
        try {shift_cue=p.engine.update(snapshot,p.mixer,std::uint32_t(p.mix.human_engine()),p.mix.shift());}
        catch(const std::exception& e){p.engine.stop(p.mixer);p.has_engine=false;p.note+="; engine controls: "+std::string(e.what());}
    }
    if(p.has_engine) {
        const auto state=p.engine.profile_state();
        p.mix.human(state.time,snapshot.owner_speed,state.throttle_on,state.throttle_off,
            state.last_shift,state.audible_rpm,state.gear,snapshot.race_ticks,snapshot.crash_cinematic);
    }
    if(p.has_effects) {
        try {const auto human=p.engine.turbo_feedback();p.effects.update(snapshot,p.mixer,shift_cue,p.has_engine?&human:nullptr,p.mix.human_engine(),&p.sound_random);}
        catch(const std::exception& e){p.effects.stop(p.mixer);p.has_effects=false;p.note+="; effect controls: "+std::string(e.what());}
    }
    if(p.has_turbo) {
        try {if(p.has_engine)p.turbo.update(snapshot,p.engine.turbo_feedback(),p.mixer,p.mix.turbo(),p.sound_random);else p.turbo.stop(p.mixer);}
        catch(const std::exception& e){p.turbo.stop(p.mixer);p.has_turbo=false;p.note+="; turbo controls: "+std::string(e.what());}
    }
    if(world_random)p.feedback_world_random.state=*world_random;
    for(unsigned i=0;i<p.remote_engines.size();++i)if(p.has_remote[i]) {
        try {
            const auto cue=p.remote_engines[i].update_remote(snapshot.rivals[i],snapshot,p.mixer,p.remote_mix[i].engine(),p.sound_random,p.feedback_world_random,p.remote_mix[i].human_engine(),p.remote_mix[i].shift());
            if(p.has_remote_shifts[i])p.remote_shifts[i].update(snapshot,snapshot.rivals[i].active,p.mixer,cue);
            if(p.has_remote_turbos[i])p.remote_turbos[i].update_remote(snapshot,snapshot.rivals[i],p.remote_engines[i].turbo_feedback(),p.mixer,p.remote_mix[i].turbo(),p.sound_random);
        }catch(const std::exception& e){p.remote_engines[i].stop(p.mixer);p.remote_shifts[i].stop(p.mixer);p.remote_turbos[i].stop(p.mixer);p.has_remote[i]=false;p.has_remote_shifts[i]=false;p.has_remote_turbos[i]=false;p.note+="; remote controls: "+std::string(e.what());}
    }
    if(world_random)*world_random=p.feedback_world_random.state;
    if(p.has_traffic) {
        try {p.traffic.update(snapshot,p.mixer,p.mix.environment(),&p.sound_random);}
        catch(const std::exception& e){p.traffic.stop(p.mixer);p.has_traffic=false;p.note+="; traffic controls: "+std::string(e.what());}
    }
    bool props_ready=false;
    if(p.has_props) {
        try {props_ready=p.props.prepare(snapshot,p.mixer);}
        catch(const std::exception& e){p.props.stop(p.mixer);p.has_props=false;p.note+="; prop controls: "+std::string(e.what());}
    }
    const auto render_props=[&]() {
        if(!props_ready)return;
        try {dispatch_race_impacts(&p.props,p.mixer,nullptr,0,p.mix.collision(),
            RaceAudioMix::default_impact_global_gain,[](CollisionVoice&){});}
        catch(const std::exception& e){p.props.stop(p.mixer);p.has_props=false;p.note+="; prop controls: "+std::string(e.what());}
    };
    const auto cue = p.cues.update(snapshot);
    if (cue != RaceInterfaceCue::none) {
        const std::size_t index = cue == RaceInterfaceCue::countdown ? 0 :
            cue == RaceInterfaceCue::go ? 1 :
            cue == RaceInterfaceCue::lap ? 2 : 3;
        if (p.has_cue[index])
            p.mixer.play(index < 2 ? 6 : 7, &p.cue_clips[index], false,
                         q15(index < 2 ? 0.55 : 0.65));
    }
    if (snapshot.finished) {
        p.finished = true;
        p.mixer.control(9,0,65536); p.mixer.control(10,0,65536);
        return;
    }
    if(!p.has_impact)render_props();
    if (p.has_impact) {
        const CollisionSpatialFrame spatial{snapshot.human_position,snapshot.listener_eye,snapshot.listener_forward,RaceAudioMix::default_impact_global_gain};
        auto events = p.collisions.update(snapshot.tick,snapshot.physics_step,snapshot.contacts.data(),
            std::min(snapshot.contact_count,snapshot.contacts.size()),p.collision,snapshot.collision_reset,p.mix.collision(),&spatial,true);
        if (!events.processed) {
            render_props();
            return;
        }
        if (snapshot.collision_reset) {
            for(auto slot:p.impact_slots)p.mixer.stop(slot);
            p.mixer.stop(9); p.mixer.stop(10);
            p.impact_bindings = {}; p.scrape_pairs = {UINT32_MAX,UINT32_MAX};
        }
        std::vector<CollisionQueueRecord> pending,playing;
        for(unsigned i=0;i<p.impact_bindings.size();++i) {
            auto& binding=p.impact_bindings[i];
            if(!p.mixer.active(p.impact_slots[i])){binding={};continue;}
            playing.push_back({0x80000000U|i,true,binding.distance});
        }
        for(unsigned j=0;j<events.impacts.size();++j) {
            const auto& e=events.impacts[j];
            if(e.start)pending.push_back({j,true,e.admission_distance});
            else for(unsigned i=0;i<p.impact_bindings.size();++i) {
                const auto& binding=p.impact_bindings[i];
                if(binding.pair==e.pair&&binding.episode==e.episode) {
                    p.mixer.control(p.impact_slots[i],e.gain_q15,e.pitch_q16);
                    if(e.positional)p.mixer.pan(p.impact_slots[i],e.pan);
                }
            }
        }
        // Inputs here are exclusively human-body contacts.4664B0 supplies
        // their priority bit; this is not a guessed loudest-hit preference.
        const unsigned active_count=unsigned(playing.size());
        for(const auto& record:pending)playing.push_back({0x40000000U|record.id,record.priority,record.distance});
        std::array<CollisionVoice*,kCollisionImpactCapacity> accepted{};std::size_t accepted_count=0;
        for(const auto& command:collision_impact_queue(pending,playing,kCollisionImpactCapacity,active_count)) {
            if(!command.start) {
                if(command.id&0x80000000U) {
                    const auto i=command.id&0x7fffffffU;p.mixer.stop(p.impact_slots[i]);p.impact_bindings[i]={};
                }
                continue;
            }
            if(accepted_count<accepted.size())accepted[accepted_count++]=&events.impacts[command.id];
        }
        const auto start_impact=[&](CollisionVoice& e) {
            for(unsigned i=0;i<p.impact_bindings.size();++i)if(p.impact_bindings[i].pair==UINT32_MAX) {
                if(!p.collisions.start_impact(e))break;
                p.impact_bindings[i]={e.pair,e.episode,e.admission_distance};
                p.mixer.play(p.impact_slots[i],&p.impacts[e.sample-30],false,e.gain_q15,e.pitch_q16,true);
                if(e.positional)p.mixer.pan(p.impact_slots[i],e.pan);
                break;
            }
        };
        try {dispatch_race_impacts(props_ready?&p.props:nullptr,p.mixer,accepted.data(),accepted_count,
            p.mix.collision(),RaceAudioMix::default_impact_global_gain,start_impact);}
        catch(const std::exception& e){p.props.stop(p.mixer);p.has_props=false;p.note+="; impact dispatch: "+std::string(e.what());}
        for (std::size_t i = 0; i < p.scrape_pairs.size(); ++i) {
            const auto found = std::find_if(events.scrapes.begin(),events.scrapes.end(),
                [&](const auto& e) { return e.pair == p.scrape_pairs[i]; });
            if (found == events.scrapes.end()) {
                p.mixer.control(9+i,0,65536); p.scrape_pairs[i] = UINT32_MAX;
            }
        }
        for (const auto& e : events.scrapes) {
            std::size_t slot = p.scrape_pairs.size();
            for (std::size_t i = 0; i < p.scrape_pairs.size(); ++i)
                if (p.scrape_pairs[i] == e.pair) { slot=i; break; }
            if (slot == p.scrape_pairs.size()) for (std::size_t i=0; i<p.scrape_pairs.size(); ++i)
                if (p.scrape_pairs[i] == UINT32_MAX) { slot=i; break; }
            if (slot == p.scrape_pairs.size()) continue;
            if (p.scrape_pairs[slot] != e.pair || e.start) {
                p.scrape_pairs[slot]=e.pair;
                p.mixer.play(9+slot,&p.scrape,true,e.gain_q15,e.pitch_q16,true);
            } else p.mixer.control(9+slot,e.gain_q15,e.pitch_q16);
            if(e.positional)p.mixer.pan(9+slot,e.pan);
        }
    }
}

void RaceAudioService::frontend_sound(FrontendSound cue) {impl_->frontend.play(cue);}
void RaceAudioService::set_ui_mix_percent(unsigned music,unsigned effects)noexcept {
    impl_->ui_music=std::min(100U,music);impl_->ui_effects=std::min(100U,effects);
}
bool RaceAudioService::frontend_sound_active(){return impl_->frontend.active();}
bool RaceAudioService::post_race_voice(const std::filesystem::path& root,unsigned event,bool won,std::string& error) {
    if(!impl_->active){error="race audio device is not active";return false;}
    return impl_->post_voice.play(root,post_race_speech(event,won),error);
}
void RaceAudioService::set_suspended(bool suspended) noexcept {
    impl_->suspended = suspended;
}

void RaceAudioService::stop() {
    auto& p = *impl_;
#ifdef __SWITCH__
    const bool report_music=p.active && p.has_music;
#endif
    p.active = false;
    p.output.stop();p.post_voice.stop();
#ifdef __SWITCH__
    if(report_music) std::fprintf(stderr,"SWITCH_MUSIC mode=race missing_frames=%llu buffered_frames=%zu\n",
        static_cast<unsigned long long>(p.stream.ring().underruns()),p.stream.ring().size());
#endif
    if(p.original_music) {
        original_race_music_next=original_music_next(p.music_first,p.music_lengths,p.music_frames.load());
        p.original_music=false;
    }
    p.stream.stop();
    std::lock_guard<std::mutex> lock(p.mixer_mutex);
    p.effects.stop(p.mixer);
    p.turbo.stop(p.mixer);
    p.props.stop(p.mixer);
    p.traffic.stop(p.mixer);
    p.engine.stop(p.mixer);
    for(auto& remote:p.remote_engines)remote.stop(p.mixer);
    for(auto& shift:p.remote_shifts)shift.stop(p.mixer);
    for(auto& turbo:p.remote_turbos)turbo.stop(p.mixer);
}

std::string RaceAudioService::diagnostic() const {
    const auto output_error = impl_->output.error();
    const auto stream_error = impl_->stream.error();
    std::string result = impl_->note;
    if (!output_error.empty()) result += "; " + output_error;
    if (!stream_error.empty()) result += "; " + stream_error;
    return result;
}

RaceAudioService::Stats RaceAudioService::stats() const noexcept {
    const auto& p = *impl_;
    const auto output = p.output.stats();
    std::lock_guard<std::mutex> lock(p.mixer_mutex);
    return {p.stream.ring().underruns(), p.stream.ring().size(),
            p.stream.ring().capacity(), output.late, output.queued_frames,
            output.work_us, p.mixer.clipped_samples(),
            p.mixer.overloaded_samples(), p.mixer.limited_frames()};
}

std::size_t RaceAudioService::resident_bytes() const noexcept {
    const auto& p = *impl_;
    std::size_t bytes = p.stream.ring().capacity() * 2 * sizeof(std::int16_t) + 3 * 4096;
    bytes+=p.engine.resident_bytes();
    for(const auto& remote:p.remote_engines)bytes+=remote.resident_bytes();
    for(const auto& shift:p.remote_shifts)bytes+=shift.resident_bytes();
    for(const auto& turbo:p.remote_turbos)bytes+=turbo.resident_bytes();
    bytes += p.effects.resident_bytes();
    bytes += p.turbo.resident_bytes();
    bytes += p.props.resident_bytes();
    bytes += p.traffic.resident_bytes();
    bytes += p.scrape.pcm.capacity() * sizeof(std::int16_t);
    for (const auto& clip : p.impacts) bytes += clip.pcm.capacity() * sizeof(std::int16_t);
    for (const auto& clip : p.cue_clips)
        bytes += clip.pcm.capacity() * sizeof(std::int16_t);
    return bytes;
}

} // namespace nfsu::audio
