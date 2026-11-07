#include "nfsu/race_audio.h"
#include "nfsu/backfire_audio.h"
#include "backfire_audio_native.inc"
#include "bank_audio_random_native.inc"
#include "collision_bank_random_native.inc"
#include "collision_position_native.inc"
#include "nfsu/race_props.h"
#include "nfsu/traffic_audio.h"
#include "traffic_audio_native.inc"
#include "traffic_position_native.inc"
#include "remote_position_native.inc"
#include "remote_script_native.inc"
#include "nfsu/race_audio_mix.h"
#include "race_audio_mix_native.inc"
#include "audio_position_native.inc"
#include "nfsu/audio.h"
#include "nfsu/arg_path.h"

#include <cmath>
#include <cstring>
#include "career_engine_rpm_native.inc"
#include "engine_envelope_native.inc"
#include "human_engine_native.inc"
#include "human_profiles_native.inc"
#include "human_shift_feedback_native.inc"
#include "human_shift_gain_native.inc"
#include "shift_disengage_native.inc"
#include "shift_category_native.inc"
#include "remote_shift_native.inc"
#include "remote_gain_native.inc"
#include "human_gain_native.inc"
#include "nfsu/sha256.h"
#include "nfsu/car_assets.h"
#include "collision_intensity_native.inc"
#include "turbo_audio_native.inc"
#include "remote_turbo_native.inc"
#include "human_camera_balance_native.inc"
#include "engine_balance_native.inc"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_bank_audio_random() {
    using nfsu::audio::BankAudioRandom;
    for(const auto& row:kNativeBankAudioSeed) {
        BankAudioRandom random(row[0]);
        for(unsigned i=0;i<6;++i)check(random.state()[i]==row[1+i],"native SND bank RNG seed words");
    }
    BankAudioRandom random;
    for(const auto& row:kNativeBankAudioRandom) {
        std::array<std::uint32_t,6> before{},after{};
        for(unsigned i=0;i<6;++i){before[i]=row[2+i];after[i]=row[9+i];}
        if(row[1]==0)random.restore(before);
        check(random.state()==before,"consecutive original bank RNG input state");
        check(random.next()==row[8] && random.state()==after,"original bank RNG carry, return and rollover state");
    }
}
void test_collision_bank_random() {
    using namespace nfsu::audio;
    auto& shared=process_bank_audio_random();const auto saved=shared.state();
    CollisionScheduler scheduler;CollisionGraph graph;
    CollisionContact contact;contact.pair=7;contact.impulse={1,0,0};
    for(const auto& row:kNativeCollisionBankRandom) {
        std::array<std::uint32_t,6> before{},after{};
        for(unsigned k=0;k<6;++k){before[k]=row.before[k];after[k]=row.after[k];}
        if(!row.tick)shared.restore(before);
        check(shared.state()==before,"collision shares consecutive bank state before foreign callers");
        for(unsigned k=0;k<row.foreign;++k)shared.next();
        const auto before_clear=shared.state();scheduler.clear();
        check(shared.state()==before_clear,"collision replacement does not reseed SND bank state");
        contact.step=2*row.tick+2;
        const auto frame=scheduler.update(row.tick+1,contact.step,&contact,1,graph);
        check(frame.impacts.size()==1&&frame.impacts[0].pitch_q16==unsigned(4096+row.jitter)*16&&shared.state()==after,
              "original64CA50 jitter and generator state after supplied bank interleaving");
    }
    shared.restore(saved);
}
void test_collision_spatial_profile() {
    using namespace nfsu::audio;
    CollisionScheduler scheduler;BankAudioRandom random;scheduler.set_random_context(&random);
    CollisionGraph graph;graph.impact_gain.fill(16384);
    unsigned index=0;
    for(const auto& c:kNativeCollisionPosition) {
        std::array<float,3> point,actor,eye,forward;
        for(unsigned k=0;k<3;++k){std::memcpy(&point[k],&c.point[k],4);std::memcpy(&actor[k],&c.actor[k],4);std::memcpy(&eye[k],&c.eye[k],4);std::memcpy(&forward[k],&c.forward[k],4);}
        nfsu::audio::CollisionAudioPosition position;
        const bool ok=position.tick(point,actor,eye,forward);
        if(!ok||position.gain!=int(c.gain)||position.pan!=int(c.pan))
            std::cerr<<"collision spatial "<<index<<" gain "<<position.gain<<'/'<<c.gain<<" pan "<<position.pan<<'/'<<c.pan<<'\n';
        check(ok&&position.gain==int(c.gain)&&position.pan==int(c.pan),"original collision position profile/actor distance/camera pan");
        scheduler.clear();CollisionContact contact;contact.pair=7;contact.step=index+1;contact.impulse={0,1,0};
        contact.point={point[0],point[1],point[2]};contact.point_valid=true;
        const CollisionSpatialFrame spatial{actor,eye,forward,int(c.global_gain)};
        const auto frame=scheduler.update(index+1,contact.step,&contact,1,graph,false,32768,&spatial);
        check(frame.impacts.size()==1&&frame.impacts[0].positional&&frame.impacts[0].pan==c.pan&&frame.impacts[0].gain_q15==c.bank_gain,
              "original spatial to configured impact gain to binary32 bank output and pan");++index;
    }
}
void test_original_race_mix() {
    nfsu::audio::RaceAudioMix mix;
    unsigned index=0;
    using namespace nfsu::audio;
    CollisionScheduler collisions;BankAudioRandom collision_random;collisions.set_random_context(&collision_random);
    CollisionGraph graph;graph.impact_gain.fill(32767);
    CollisionContact contact;contact.pair=1;contact.impulse={0,0,1};
    for(unsigned j=0;j<12;++j)check(unsigned(RaceAudioMix::collision_targets[j])==kNativeCollisionCategoryTargets[j],"original category7 targets");
    for(const auto& r:kNativeRaceAudioMix) {
        if(r[0]==1)mix.request(r[1]);else if(r[0]==2)mix.clear(r[1]);
        float dt=0;std::memcpy(&dt,&r[2],4);mix.tick(dt);
        if(mix.music()!=int(r[3]) || mix.engine()!=int(r[4]) ||
           mix.music_output()!=r[5] || mix.apply(32767,mix.engine())!=r[6] || mix.profile()!=r[7] || mix.environment()!=int(r[8]) || mix.human_engine()!=int(r[9]) || mix.turbo()!=int(r[10]) || mix.shift()!=int(r[11]))
            throw std::runtime_error("original consecutive race mix row "+std::to_string(index)+" environment native="+std::to_string(r[8])+" port="+std::to_string(mix.environment()));
        check(mix.collision()==int(r[12])&&RaceAudioMix::apply(20000,mix.collision())==r[13],"original category7 publication and530740 consumer");
        collisions.clear();contact.step=index+1;
        const auto collision=collisions.update(index+1,contact.step,&contact,1,graph,false,mix.collision());
        check(collision.impacts.size()==1&&collision.impacts[0].gain_q15==r[14],"original530740 before binary32 bank35 gain product");
        ++index;
    }
    check(nfsu::audio::RaceAudioMix::default_human_position_gain==kNativeDefaultPositionGain[0][2],
          "original human context and fresh preferences reach its positional output");
    check(nfsu::audio::RaceAudioMix::default_impact_global_gain==int(kNativeDefaultImpactGlobalGain[0][0]),"fresh original FX preferences and5365E0 impact global output");
    for(const auto& r:kNativeEngineDeviceGain) {
        const auto actual=nfsu::audio::RaceAudioMix::engine_output(r[0],int(r[1]));
        if(actual!=r[2])std::cerr<<"device gain input="<<r[0]<<" category="<<r[1]<<" native="<<r[2]<<" port="<<actual<<'\n';
        check(actual==r[2],"original spatial then owner/category integer gain order");
    }
    for(const auto& r:kNativeMusicDeviceGain)
        check(nfsu::audio::RaceAudioMix::music_output(int(r[0]))==r[1],
              "original fresh music context, steady descriptor and profile gain order");
    nfsu::audio::RaceAudioMix race;
    for(unsigned i=0;i<180;++i)race.race(false,false);
    check(race.music_output()==32766 && race.engine()==16358,"original ordinary race/music target");
    race.race(true,false);race.race(true,true);race.race(true,true);
    check(race.engine()==0 && race.music_output()==32766,"crash category mutes engine, preserves music");
    race.race(false,true);race.race(false,false);race.race(false,false);
    check(race.engine()>0 && race.engine()<16358,"crash release restores engine through original curve");
    for(unsigned i=0;i<60;++i)race.race(false,false);
    check(race.engine()==16358 && race.profile()==3,"NOS cleared under crash does not restore a stale profile");
}
void test_original_human_profiles() {
    using nfsu::audio::RaceAudioMix;
    RaceAudioMix mix;mix.request(3);bool prior=false;
    const auto f=[](unsigned u){float v;std::memcpy(&v,&u,4);return v;};
    for(const auto& r:kNativeHumanProfiles) {
        mix.tick(1.F/60.F);
        const std::array<int,4> gains{{mix.human_engine(),mix.environment(),mix.engine(),mix.music()}};
        for(unsigned i=0;i<4;++i)check(gains[i]==int(r[10+i]),"original next-frame profile gain publication");
        const bool nos=bool(r[9]);
        if(nos!=prior){if(nos)mix.request(5);else mix.clear(5);}prior=nos;
        mix.human(f(r[1]),f(r[2]),f(r[3]),f(r[4]),f(r[5]),f(r[6]),int(r[7]),int(r[0]*4000U/60U),r[8]==6||r[8]==7);
        check(mix.profile()==r[14],"original human adaptive profile admission and restoration");
        check(gains==std::array<int,4>{{mix.human_engine(),mix.environment(),mix.engine(),mix.music()}},
              "requests do not publish category gains before the next536800 update");
    }
}
void test_original_engine_envelope() {
    using namespace nfsu::audio;
    const auto real=[](unsigned bits){float v=0;std::memcpy(&v,&bits,4);return v;};
    for(const auto& row:kNativeEngineBalance){const auto gain=engine_audio_gains(11911,32600,int(row[0]),0);
        check(gain[0]==row[1]&&gain[1]==row[2],"original523629 complete authored balance table");}
    EngineEnvelope control;unsigned previous=~0U,row_index=0;
    for(const auto& row:kNativeEngineEnvelopeRows) {
        if(row[0]!=previous){const auto& t=kNativeEngineEnvelopeConfig[row[0]];
            control.configure({int(t[0]),int(t[1]),int(t[2]),real(t[3]),
                int(t[4]),int(t[5]),int(t[6]),int(t[7]),real(t[8])});previous=row[0];}
        EngineEnvelopeInput in;in.rpm=real(row[1]);in.maximum_rpm=real(row[2]);
        in.drive_load=real(row[3]);in.throttle=real(row[4]);in.brake=real(row[5]);
        in.handbrake=row[6]!=0;in.gear=int(row[7]);in.game_state=int(row[8]);in.dt=real(row[9]);
        const auto result=control.tick(in);unsigned rpm=0,load=0;
        std::memcpy(&rpm,&result.rpm,4);std::memcpy(&load,&result.load,4);
        if(rpm!=row[10]||load!=row[11]||result.shift_state!=row[12]) {
            std::ostringstream msg;msg<<"original engine envelope row "<<row_index<<" rpm "<<rpm<<"/"<<row[10]
                <<" load "<<load<<"/"<<row[11]<<" state "<<result.shift_state<<"/"<<row[12];
            throw std::runtime_error(msg.str());
        }
        const auto& tuning=kNativeEngineEnvelopeConfig[row[0]];
        const auto gains=engine_audio_gains(int(tuning[9]),int(tuning[10]),int(tuning[11]),result.gain_boost);
        const auto raw_gains=remote_engine_audio_gains(int(tuning[9]),int(tuning[10]),int(tuning[11]),result.gain_boost);
        if(std::uint32_t(raw_gains[0])!=row[14]||std::uint32_t(raw_gains[1])!=row[15])std::cerr<<"remote gain row="<<row_index<<" raw="<<raw_gains[0]<<','<<raw_gains[1]<<" native="<<row[14]<<','<<row[15]<<" boost="<<result.gain_boost<<'\n';
        check(std::uint32_t(raw_gains[0])==row[14]&&std::uint32_t(raw_gains[1])==row[15],"original remote accent before balance, no premature device clamp");
        unsigned boost=0;std::memcpy(&boost,&result.gain_boost,4);
        if(boost!=row[13]||gains[0]!=unsigned(std::clamp(raw_gains[0],0,32767))||gains[1]!=unsigned(std::clamp(raw_gains[1],0,32767))) {
            throw std::runtime_error("original engine accent/balance row "+std::to_string(row_index));
        }
        const auto parameters=engine_audio_parameters(result.rpm,result.load);
        check(parameters[0]==row[16]&&parameters[1]==row[17],"original5236D0 bank parameter quantization");
        ++row_index;
    }
}
void test_human_gain_native() {
    using namespace nfsu::audio;
    const auto real=[](unsigned b){float v=0;std::memcpy(&v,&b,4);return v;};
    const auto bits=[](float v){unsigned b=0;std::memcpy(&b,&v,4);return b;};
    unsigned ci=0;
    for(const auto& c:kNativeHumanGain){const auto* t=c.config;std::array<std::uint8_t,100> raw{};
        for(unsigned w=0;w<25;++w)for(unsigned b=0;b<4;++b)raw[w*4+b]=std::uint8_t(c.tuning[w]>>(b*8));
        HumanEngineEnvelope envelope;envelope.configure(engine_envelope_tuning(raw.data(),raw.size()));nfsu::Sha256 digest;
        for(unsigned tick=0;tick<320;++tick){
            EngineEnvelopeInput input;input.rpm=tick>=50&&tick<90?1000.F:tick>=180&&tick<200?8800.F:real(t[26]);
            input.maximum_rpm=10000;input.drive_load=80;input.load_divisor=160;
            input.throttle=tick>=120&&tick<210?0.F:1.F;input.brake=tick>=120&&tick<210?1.F:0.F;
            input.gear=tick<6?2:tick<10?3:tick<100?4:tick<240?3:4;input.dt=real(t[27]);
            input.gear_ratio=real(t[16+input.gear]);input.first_gear_ratio=real(t[18]);
            const auto out=envelope.tick(input);const auto profile=envelope.gain_profile_state();
            const auto gains=human_engine_audio_gains(int(t[24]),int(t[25]),int(t[29]),out.gain_boost,
                out.adaptive_blend,out.adaptive_offset,out.adaptive_balance_boost,t[28]);
            const unsigned words[]={out.shift_state,bits(out.rpm),bits(out.load),profile.mode,bits(profile.blend),
                unsigned(profile.offset),gains[0],gains[1],bits(profile.time),bits(profile.last_off),unsigned(profile.throttle_open)};
            std::uint8_t bytes[44]{};for(unsigned k=0;k<11;++k)for(unsigned b=0;b<4;++b)bytes[4*k+b]=std::uint8_t(words[k]>>(b*8));
            digest.update(bytes,sizeof(bytes));
        }
        const auto actual=nfsu::sha256_hex(digest.finish());
        if(actual!=c.publication)throw std::runtime_error("original human gain stream case "+std::to_string(ci)+": "+actual+" / "+c.publication);
        ++ci;
    }
}
void test_remote_gain_native() {
    using namespace nfsu::audio;
    const auto real=[](unsigned b){float v=0;std::memcpy(&v,&b,4);return v;};
    const auto bits=[](float v){unsigned b=0;std::memcpy(&b,&v,4);return b;};
    unsigned case_index=0;
    for(const auto& c:kNativeRemoteGain){const auto* t=c.config;
        EngineEnvelopeTuning tuning{int(t[1]),int(t[2]),int(t[3]),real(t[4]),int(t[5]),int(t[6]),int(t[7]),int(t[8]),real(t[9])};
        tuning.human_shift_gain=int(t[10]);tuning.profile_rise_factor=real(t[11]);tuning.profile_shift_drop=real(t[12]);
        tuning.profile_gain_offset=real(t[13]);tuning.profile_balance_boost=real(t[14]);tuning.profile_fall_ms=int(t[15]);
        EngineEnvelope envelope;envelope.configure(tuning);nfsu::Sha256 digest;
        for(unsigned tick=0;tick<320;++tick){
            EngineEnvelopeInput input;input.rpm=tick>=50&&tick<90?1000.F:tick>=180&&tick<200?8800.F:real(t[26]);
            input.maximum_rpm=10000;input.drive_load=80;input.throttle=1;input.brake=tick>=120&&tick<210?1.F:0.F;
            input.gear=tick<6?2:tick<10?3:tick<100?4:tick<240?3:4;input.dt=real(t[27]);
            input.gear_ratio=real(t[16+input.gear]);input.first_gear_ratio=real(t[18]);
            const auto out=envelope.tick(input);const auto profile=envelope.profile_state();
            const auto gains=remote_engine_audio_gains(int(t[24]),int(t[25]),256,out.gain_boost,
                out.adaptive_blend,out.adaptive_offset,out.adaptive_balance_boost);
            const unsigned words[]={out.shift_state,bits(out.rpm),bits(out.load),profile.mode,bits(profile.blend),
                unsigned(profile.offset),unsigned(gains[0]),unsigned(gains[1]),bits(profile.time),bits(profile.last_off),unsigned(profile.throttle_open)};
            std::uint8_t bytes[44]{};
            for(unsigned k=0;k<11;++k)for(unsigned b=0;b<4;++b)bytes[4*k+b]=std::uint8_t(words[k]>>(b*8));
            digest.update(bytes,sizeof(bytes));
        }
        const auto actual=nfsu::sha256_hex(digest.finish());
        if(actual!=c.publication)throw std::runtime_error("original remote gain stream case "+std::to_string(case_index)+": "+actual+" / "+c.publication);
        ++case_index;
    }
    std::array<std::uint8_t,0x52> record{};bool refused=false;
    try{(void)engine_envelope_tuning(record.data(),record.size()-1);}catch(const std::exception&){refused=true;}
    check(refused,"selected sound profile refuses a truncated final signed field");
    (void)engine_envelope_tuning(record.data(),record.size());
}
void test_remote_gain_tuning(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    const auto table=nfsu::load_car_type_table(*root/"Global/GLOBALB.BUN");check(table.ok(),"selected sound type table");
    const char* cars[]={"CIVIC","RX7","SUPRA","INTEGRA"};
    std::ifstream records(*root/"Global/GLOBALB.BUN",std::ios::binary);check(bool(records),"selected sound records");
    const auto bits=[](float v){unsigned b=0;std::memcpy(&b,&v,4);return b;};
    unsigned ci=0;
    for(const auto* car:cars)for(unsigned level=0;level<4;++level){
        const auto index=nfsu::find_car_type(table,car);check(index!=nfsu::kNoCarType,"selected sound type exists");
        records.seekg(std::streamoff(table.records[index].offset+0x9c0+100*level));std::array<std::uint8_t,100> raw{};
        check(bool(records.read(reinterpret_cast<char*>(raw.data()),raw.size())),"selected sound row read");
        const auto tuning=engine_envelope_tuning(raw.data(),raw.size());const auto* expected=kNativeRemoteGain[ci].config;
        check(bits(tuning.profile_rise_factor)==expected[11]&&bits(tuning.profile_shift_drop)==expected[12]&&
            bits(tuning.profile_gain_offset)==expected[13]&&bits(tuning.profile_balance_boost)==expected[14]&&
            tuning.profile_fall_ms==int(expected[15]),"selected sound gain fields match actual native profile consumer");
        ci+=4;
    }
}
void test_remote_shift_native() {
    using namespace nfsu::audio;
    const auto real=[](unsigned b){float v=0;std::memcpy(&v,&b,4);return v;};
    const auto bits=[](float v){unsigned b=0;std::memcpy(&b,&v,4);return b;};
    EngineEnvelope envelope;unsigned config=UINT32_MAX,index=0;
    for(const auto& r:kNativeRemoteShift){
        if(config!=r[0]){config=r[0];const auto& t=kNativeRemoteShiftConfig[config];
            EngineEnvelopeTuning tuning{int(t[1]),int(t[2]),int(t[3]),real(t[4]),int(t[5]),int(t[6]),int(t[7]),int(t[8]),real(t[9])};
            tuning.human_shift_gain=int(t[10]);envelope.configure(tuning);
        }
        EngineEnvelopeInput input;input.rpm=real(r[2]);input.maximum_rpm=10000;input.drive_load=80;
        input.throttle=1;input.gear=int(r[3]);input.dt=real(r[4]);
        const auto out=envelope.tick(input);
        if(out.shift_state!=r[5]||bits(out.rpm)!=r[6]||bits(out.load)!=r[7]||out.shift_cue!=r[8]||
           (out.shift_cue&&RaceAudioMix::spatial_output(unsigned(out.shift_gain),19437,18000)!=r[9])||
           out.disengage!=(r[11]?2U:0U)||(out.disengage&&RaceAudioMix::spatial_output(20999,19437,23000)!=r[12]))
            throw std::runtime_error("original remote shift row "+std::to_string(index));
        ++index;
    }
}
void test_remote_shift_playback(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    for(int level:{0,3})for(unsigned ordinal=0;ordinal<3;++ordinal){
        RaceEngineSound engine;RaceRemoteShiftSound shifts;Mixer em,fm;std::string error;
        check(engine.load_remote(*root,"CIVIC",level,ordinal,error),error.c_str());
        check(shifts.load(*root,level,ordinal,error),error.c_str());
        const auto retained=shifts.resident_bytes();TrafficAudioRandom random,world;
        RaceAudioSnapshot snapshot;snapshot.listener_eye={{6,0,0}};
        auto& actor=snapshot.rivals[ordinal];actor.active=true;actor.position={{12,2,0}};
        actor.engine.rpm=8500;actor.engine.maximum_rpm=10000;actor.engine.drive_load=80;
        actor.engine.throttle=1;actor.engine.gear=2;
        RemoteAudioPosition previous;std::array<std::int16_t,1600> pcm{};
        bool heard=false;unsigned main=0,end=0;
        for(unsigned tick=0;tick<100;++tick){snapshot.tick=tick;
            if(tick==6){actor.engine.gear=3;actor.position={{100,40,0}};}
            const auto cue=engine.update_remote(actor,snapshot,em,0,random,world,18000,23000);
            if(cue.kind){check(tick==6&&cue.kind==1,"remote upshift is immediate without human delay");++main;
                // The selected source gain is also independently pinned by
                // the native fixture; read its configuration instead here.
                const auto source=int(kNativeRemoteShiftConfig[level==3?4:0][10]);
                check(cue.gain==int(RaceAudioMix::spatial_output(unsigned(human_shift_audio_gain(std::int16_t(source),8500)),previous.gain,18000))&&cue.pan==previous.pan,
                    "remote notification retains preceding position/category1/pan");
            }
            previous.tick(actor.position,snapshot.human_position,snapshot.listener_eye,snapshot.listener_forward,1000.F/60.F);
            if(cue.disengage){check(cue.disengage==2&&tick>6,"remote end has no human immediate-disengage actor");++end;
                check(cue.disengage_gain==int(RaceAudioMix::spatial_output(20999,previous.gain,23000)),"remote end uses refreshed position/category5");}
            shifts.update(snapshot,true,fm,cue);shifts.update(snapshot,true,fm,cue);
            fm.render(pcm.data(),800);heard|=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});
            for(unsigned slot=0;slot<kMixVoices;++slot)if(slot!=98+2*ordinal&&slot!=99+2*ordinal)
                check(!fm.active(slot),"remote shift owns exactly its two slots");
        }
        check(main==1&&end==1&&heard,"native rival notifications reach selected shift PCM");
        check(shifts.resident_bytes()==retained,"remote shift/end share resident PCM and retain graph memory");
        snapshot.finished=true;++snapshot.tick;shifts.update(snapshot,true,fm,{});fm.render(pcm.data(),800);
        check(std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}),"finish cancels both remote shift actors");
        snapshot.finished=false;++snapshot.tick;RaceShiftSoundCue cue;cue.kind=1;cue.gain=25000;
        shifts.update(snapshot,true,fm,cue);++snapshot.tick;shifts.update(snapshot,false,fm,{});fm.render(pcm.data(),800);
        check(std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}),"inactive rival cancels both shift actors");
    }
}
void test_original_engine_rpm() {
    for(const auto& row:kNativeEngineRpm) {
        float physical=0,maximum=0;std::memcpy(&physical,&row.rpm,4);std::memcpy(&maximum,&row.maximum,4);
        const float result=nfsu::audio::engine_audio_rpm(physical,maximum);unsigned bits=0;std::memcpy(&bits,&result,4);
        check(bits==row.output,"original523DDD normalized engine RPM");
    }
    check(nfsu::audio::engine_audio_rpm(8000,8000)==10000,"physical limiter reaches full bank range");
    check(nfsu::audio::engine_audio_rpm(4000,8000)==5000,"physical half RPM reaches half bank range");
}
void test_engine_envelope_playback(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    std::array<RaceEngineSound,3> sounds;std::array<Mixer,3> mixers;
    for(auto& sound:sounds){std::string error;check(sound.load(*root,"CIVIC",0,error),error.c_str());}
    std::array<std::array<std::int16_t,1600>,3> pcm{};
    bool heard=false,brake_changed=false,throttle_changed=false,shift_changed=false;
    for(unsigned tick=0;tick<220;++tick)for(unsigned variant=0;variant<3;++variant) {
        RaceAudioSnapshot in;in.tick=tick;in.rpm=4000;in.maximum_rpm=8000;
        in.throttle=1;in.engine_drive_load=.12;in.gear=2;
        if(tick>=80){if(variant==1)in.brake=1;if(variant==2)in.gear=3;}
        if(tick>=140&&variant==1)in.throttle=.2;
        sounds[variant].update(in,mixers[variant]);mixers[variant].render(pcm[variant].data(),800);
        heard|=std::any_of(pcm[variant].begin(),pcm[variant].end(),[](auto v){return v!=0;});
        if(variant==2&&tick>=80){if(tick<140)brake_changed|=pcm[0]!=pcm[1];else throttle_changed|=pcm[0]!=pcm[1];shift_changed|=pcm[0]!=pcm[2];}
    }
    check(heard,"actual engine/exhaust banks produce nonzero PCM");
    check(!brake_changed,"actual human input does not borrow the remote brake latch");
    check(throttle_changed,"actual human PCM consumes throttle-average load");
    check(shift_changed,"actual engine PCM consumes audible gear envelopes at unchanged physical RPM");
    RaceAudioSnapshot audible;audible.tick=220;audible.rpm=4000;audible.maximum_rpm=8000;
    audible.throttle=1;audible.engine_drive_load=.12;audible.gear=2;
    sounds[0].update(audible,mixers[0],0);
    std::array<std::int16_t,1600> silent{};
    mixers[0].render(silent.data(),800);mixers[0].render(silent.data(),800);
    check(std::all_of(silent.begin(),silent.end(),[](auto v){return v==0;}),
          "actual engine/exhaust output consumes the crash category mute");
    ++audible.tick;sounds[0].update(audible,mixers[0]);mixers[0].render(silent.data(),800);
    check(std::any_of(silent.begin(),silent.end(),[](auto v){return v!=0;}),
          "normal category resumes retained engine voices without reloading banks");
    for(unsigned k=0;k<3;++k){RaceAudioSnapshot done;done.tick=222;done.finished=true;
        sounds[k].update(done,mixers[k]);for(unsigned i=0;i<8;++i)
            check(!mixers[k].active(i<4?i:i+8),"finish retires every engine/exhaust voice");}
}
void test_engine_pan_playback(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    RaceEngineSound human,remote;Mixer local_mix,remote_mix;std::string error;
    check(human.load(*root,"CIVIC",0,error),error.c_str());
    check(remote.load_remote(*root,"CIVIC",0,0,error),error.c_str());
    TrafficAudioRandom sound_rng,world_rng;
    RaceAudioSnapshot input;input.listener_eye={{0,-6,0}};input.rpm=4000;
    input.maximum_rpm=8000;input.gear=2;input.throttle=1;input.engine_drive_load=100;
    auto& actor=input.rivals[0];actor.active=true;actor.position={{0,0,0}};
    actor.engine.rpm=4000;actor.engine.maximum_rpm=8000;actor.engine.gear=2;actor.engine.throttle=1;
    std::array<std::int16_t,1600> local{},other{};bool local_heard=false,remote_heard=false;
    for(unsigned tick=0;tick<60;++tick) {
        input.tick=tick;
        const auto cue=human.update(input,local_mix);
        check(human.turbo_feedback().pan==16384&&cue.pan==16384,
              "human camera angle reaches engine and delayed-shift feedback");
        remote.update_remote(actor,input,remote_mix,16358,sound_rng,world_rng);
        local_mix.render(local.data(),800);remote_mix.render(other.data(),800);
        for(unsigned frame=0;frame<800;++frame) {
            check(local[frame*2+1]==0,"human engine/exhaust consume bank left-cardinal pan");
            check(other[frame*2]==0,"remote engine/exhaust consume opposite spatial pan");
            local_heard|=local[frame*2]!=0;remote_heard|=other[frame*2+1]!=0;
        }
    }
    check(local_heard&&remote_heard,"both connected mono engines remain audible on their selected side");
}

void test_remote_engine_playback(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    const char* cars[]={"CIVIC","SUPRA","MIATA"};
    for(unsigned ordinal=0;ordinal<3;++ordinal) {
        RaceEngineSound sound;Mixer mixer;std::string error;
        check(sound.load_remote(*root,cars[ordinal],int(ordinal),ordinal,error),error.c_str());
        check(sound.selection().upgrade==ordinal,"remote bank follows actual selected upgrade");
        const auto retained=sound.resident_bytes();TrafficAudioRandom rng,world;
        RaceAudioSnapshot snapshot;snapshot.listener_eye={{6,0,0}};
        auto& actor=snapshot.rivals[ordinal];actor.active=true;actor.position={{12,2,0}};
        actor.engine.rpm=4500;actor.engine.maximum_rpm=7500;actor.engine.throttle=1;actor.engine.gear=2;
        std::array<std::int16_t,1600> pcm{};bool heard=false;
        for(unsigned tick=0;tick<200;++tick) {
            snapshot.tick=tick;actor.engine.game_state=tick<90?2:4;
            sound.update_remote(actor,snapshot,mixer,16358,rng,world);mixer.render(pcm.data(),800);
            heard|=std::any_of(pcm.begin(),pcm.end(),[](auto x){return x!=0;});
        }
        check(heard,"actual selected rival engine/exhaust and rev controls produce PCM");
        check(sound.resident_bytes()==retained,"rival audio updates retain predecoded bank storage");
        for(unsigned slot=0;slot<61;++slot)check(!mixer.active(slot),"rival owns no human, effects or traffic voice");
        snapshot.finished=true;++snapshot.tick;sound.update_remote(actor,snapshot,mixer,16358,rng,world);
        for(unsigned slot=61;slot<kMixVoices;++slot)check(!mixer.active(slot),"finish retires all rival voices");
    }
}
void test_traffic_sound(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    using nfsu::kRaceTrafficCapacity;
    for(unsigned mode=0;mode<2;++mode) {
        RaceTrafficSound sound;Mixer mixer;std::string error;check(sound.load(*root,error),error.c_str());
        const auto resident=sound.resident_bytes();
        RaceAudioSnapshot snapshot;snapshot.listener_eye={{-6,0,0}};
        auto& actor=snapshot.traffic[0];actor.active=true;actor.generation=1;
        actor.input.car_type=22;actor.input.position={{0,0,0}};actor.input.velocity={{10,0,0}};
        actor.input.human_position=mode?std::array<float,3>{{3,0,0}}:std::array<float,3>{{10,0,0}};
        actor.input.human_forward=mode?std::array<float,3>{{1,0,0}}:std::array<float,3>{{-1,0,0}};
        actor.input.human_velocity={{mode?-40.F:10.F,0,0}};actor.input.human_speed=40;
        std::array<std::int16_t,1600> pcm{};bool heard=false;
        for(unsigned tick=0;tick<720;++tick){snapshot.tick=tick;sound.update(snapshot,mixer,32500);mixer.render(pcm.data(),800);
            heard|=std::any_of(pcm.begin(),pcm.end(),[](auto sample){return sample!=0;});}
        check(heard,mode?"actual traffic pass-by bank produces PCM":"actual traffic horn bank produces PCM");
        check(sound.resident_bytes()==resident,"traffic updates retain predecoded shared bank storage");
        actor.active=false;++snapshot.tick;sound.update(snapshot,mixer,32500);
        for(unsigned slot=29;slot<kMixVoices;++slot)check(!mixer.active(slot),"traffic despawn retires both bound sound voices");
        actor.active=true;++actor.generation;++snapshot.tick;sound.update(snapshot,mixer,32500);
        check(!mixer.active(29)&&!mixer.active(30),"traffic replacement restarts original admission timers");
        snapshot.finished=true;++snapshot.tick;sound.update(snapshot,mixer,32500);
        for(unsigned slot=29;slot<kMixVoices;++slot)check(!mixer.active(slot),"finish retires traffic sound without tail voices");
    }
    // Last actor in tournament20 must not steal remote-engine61 or turbo85.
    RaceTrafficSound expanded;Mixer mixer;std::string error;
    check(expanded.load(*root,error,kRaceTrafficCapacity),error.c_str());
    PcmClip sentinel;sentinel.rate=48000;sentinel.channels=1;sentinel.pcm.assign(480,100);
    mixer.play(61,&sentinel,true,1000,65536);mixer.play(67,&sentinel,true,1000,65536);mixer.play(85,&sentinel,true,1000,65536);
    RaceAudioSnapshot snapshot;snapshot.listener_eye={{-6,0,0}};
    auto& last=snapshot.traffic.back();last.active=true;last.generation=1;
    last.input.car_type=22;last.input.position={{0,0,0}};last.input.velocity={{10,0,0}};
    last.input.human_position={{10,0,0}};last.input.human_forward={{-1,0,0}};
    last.input.human_velocity={{10,0,0}};last.input.human_speed=40;
    bool heard=false;std::array<std::int16_t,1600> pcm{};
    const auto resident=expanded.resident_bytes();
    for(unsigned tick=0;tick<720;++tick) {
        snapshot.tick=tick;expanded.update(snapshot,mixer,32500);
        heard|=mixer.active(race_traffic_voice_slot(kRaceTrafficCapacity-1,0))||mixer.active(race_traffic_voice_slot(kRaceTrafficCapacity-1,1));
        check(mixer.active(61)&&mixer.active(67)&&mixer.active(85),"expanded traffic cannot stop or replace rival/turbo voices");
        mixer.render(pcm.data(),800);
    }
    check(heard&&expanded.resident_bytes()==resident,"last traffic actor is audible with resident-only graph storage");
    expanded.stop(mixer);check(mixer.active(61)&&mixer.active(67)&&mixer.active(85),"traffic retirement preserves independent remote and turbo voices");
    check(!expanded.load(*root,error,kRaceTrafficCapacity+1),"oversized traffic sound pool refused");
}

void test_prop_sound(const std::filesystem::path* root) {
    // Authenticated speed.exe531670 +595170 +532840, supplied J/inverse mass.
    using nfsu::audio::prop_collision_intensity;
    const double native[][3]={{0,200,0},{.01,200,12},{.17,200,127},{.25,100,127},
        {3.367,5.405405405405405,110},{13.54,1,81},{1,1,6},{1,2,12},{21,1,127}};
    for(const auto& row:native)check(prop_collision_intensity(float(row[0]),float(row[1]))==int(row[2]),"native smokeable intensity uses inverse mass and subtype7");
    //52F7B0 outputs from zeroed independent native family counters.
    const unsigned first[]={14,1,12,28,4,23,7,14,14,17,17,17,9,14,14,26,20,8};
    for(unsigned material=0;material<std::size(first);++material){std::array<unsigned,29> counters{};
        check(nfsu::audio::prop_collision_variant(material,counters)==first[material],"native smokeable material variant");}
    if(!root)return;
    using namespace nfsu::audio;
    RacePropSound sound;Mixer mixer;std::string error;
    check(sound.load(*root,error),error.c_str());
    RaceAudioSnapshot s;std::array<std::int16_t,1600> pcm{};
    const auto frame=[&](){++s.tick;s.physics_step+=4;sound.update(s,mixer);mixer.render(pcm.data(),800);
        return std::any_of(pcm.begin(),pcm.end(),[](auto x){return x!=0;});};
    check(!frame(),"props are silent before an observed hit");
    s.prop_contacts.count=1;s.prop_contacts.hits[0]={7,{12,0,0}};
    bool heard=false;for(unsigned i=0;i<20;++i)heard|=frame();
    check(heard,"kind1 original smokeable graph produces actual PCM");
    s.prop_contacts.count=0;for(unsigned i=0;i<240;++i)frame();
    check(!frame(),"one-shot smokeable voice retires");
    s.prop_contacts.count=1;s.prop_contacts.hits[0]={7,{20,0,0}};
    heard=false;for(unsigned i=0;i<10;++i)heard|=frame();
    check(heard,"later repeated prop hit produces PCM again");
    s.finished=true;frame();check(!frame(),"finish immediately silences prop voices");
    {
        const auto saved=process_bank_audio_random().state();
        std::array<std::array<std::uint32_t,6>,2> seen[2];
        std::array<std::array<std::int16_t,1600>,2> output{};
        std::array<std::uint32_t,6> final[2];
        for(unsigned pass=0;pass<2;++pass) {
            process_bank_audio_random().restore(saved);
            RacePropSound mixed;Mixer target;check(mixed.load(*root,error),error.c_str());
            RaceAudioSnapshot input;input.tick=1;input.physics_step=4;input.prop_contacts.count=2;
            input.prop_contacts.hits[0]={1,{12,0,0},1,0,20,12};
            input.prop_contacts.hits[1]={2,{15,0,0},1,1,40,15};
            check(mixed.prepare(input,target),"mixed impacts prepare once");
            CollisionVoice car,wall;car.pair=0;car.audio_order=10;wall.pair=1;wall.audio_order=30;
            const auto start=[&](CollisionVoice& voice) {
                seen[pass][voice.pair]=process_bank_audio_random().state();
                process_bank_audio_random().next();
            };
            if(pass==0) {
                start(car);mixed.render_voice(0,target,32768,32768);
                start(wall);mixed.render_voice(1,target,32768,32768);
            }else {
                CollisionVoice* starts[]={&wall,&car};
                dispatch_race_impacts(&mixed,target,starts,2,32768,32768,start);
            }
            final[pass]=process_bank_audio_random().state();
            mixed.render_voice(0,target,32768,32768);
            check(process_bank_audio_random().state()==final[pass],"same-tick prop graph render is idempotent");
            target.render(output[pass].data(),800);
            check(!mixed.prepare(input,target),"same-tick mixed admission is idempotent");
        }
        check(seen[0]==seen[1]&&final[0]==final[1]&&output[0]==output[1],
            "car/prop/wall/prop dispatch preserves real bank RNG and PCM order");
        check(seen[1][0]==saved&&seen[1][1]!=saved,"generic starts bracket prop bank callbacks");
        process_bank_audio_random().restore(saved);
    }
    {
        const auto saved=process_bank_audio_random().state();
        std::array<std::array<std::int16_t,1600*20>,2> audio{};
        for(unsigned pass=0;pass<2;++pass) {
            process_bank_audio_random().restore(saved);
            RacePropSound repeated;Mixer target;check(repeated.load(*root,error),error.c_str());
            RaceAudioSnapshot input;input.tick=1;input.physics_step=4;input.prop_contacts.count=pass?2:1;
            input.prop_contacts.hits[0]={1,{pass?2.0:1.0,0,0},1,0,10,pass?2.F:18.F};
            if(pass)input.prop_contacts.hits[1]={1,{1,0,0},1,0,11,18};
            for(unsigned frame=0;frame<20;++frame) {
                repeated.update(input,target);target.render(audio[pass].data()+frame*1600,800);
                ++input.tick;input.physics_step+=4;input.prop_contacts.count=0;
            }
        }
        check(audio[0]==audio[1]&&std::any_of(audio[0].begin(),audio[0].end(),[](auto v){return v!=0;}),
            "repeated prop responses use retained record magnitude without restarting its voice");
        process_bank_audio_random().restore(saved);
    }
    {
        RacePropSound pinned;Mixer target;check(pinned.load(*root,error),error.c_str());
        RaceAudioSnapshot input;input.tick=1;input.physics_step=4;input.prop_contacts.count=1;
        input.prop_contacts.hits[0]={1,{2,0,0},0,0,10,2};
        pinned.update(input,target);target.render(pcm.data(),800);
        check(std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}),"pinned response has zero audible delta velocity");
        ++input.tick;input.physics_step+=4;
        input.prop_contacts.hits[0]={1,{18,0,0},1,0,10,20};
        pinned.update(input,target);target.render(pcm.data(),800);
        bool audible=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});
        input.prop_contacts.count=0;
        for(unsigned frame=0;frame<10;++frame){++input.tick;input.physics_step+=4;pinned.update(input,target);target.render(pcm.data(),800);
            audible|=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});}
        check(audible,"silent pinned contact does not suppress the following activated prop impact");
    }
    using namespace nfsu;
    RaceProps props;check(props.load(*root,error),error.c_str());
    RacePropSound selected;check(selected.load(*root,error,props.sound_materials()),error.c_str());
    check(selected.resident_bytes()<sound.resident_bytes(),"course-only prop PCM omits unused material families");
    const auto spec=stock_240sx_vehicle_spec();const auto box=race_contact_box(stock_240sx_hull());
    VehicleModel model(spec);unsigned real_hits=0,audible_hits=0;
    for(std::size_t i=0;i<std::min<std::size_t>(props.objects().size(),80);++i) {
        const auto p=props.objects()[i];VehicleState car;
        check(model.reset(car,{p.state.position,0,20,VehicleGearbox::automatic}),"prop audio car reset");
        car.position=p.state.position;car.position.x-=2;car.rigid_body_ready=false;
        car.velocity={20,0,0};vehicle_import_rigid(spec.handling,car);
        VehiclePropContactFrame contacts;check(props.meet(car,spec,box,error,&contacts),error.c_str());
        if(!contacts.count)continue;
        ++real_hits;RacePropSound physical;Mixer output;check(physical.load(*root,error,props.sound_materials()),error.c_str());
        RaceAudioSnapshot observed;observed.physics_step=1000+100*i;observed.prop_contacts=contacts;
        bool audible=false;
        for(unsigned f=0;f<20;++f){++observed.tick;physical.update(observed,output);output.render(pcm.data(),800);
            audible|=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});observed.prop_contacts.count=0;}
        audible_hits+=audible;const auto j=contacts.hits[0].impulse;
        std::printf("PROP_AUDIO name=%s mass=%.6f activation=%.6f impulse=%.6f slowdown=%.6f audible=%u\n",p.name.c_str(),p.spec.handling.mass,p.impact.activation,std::sqrt(j.x*j.x+j.y*j.y+j.z*j.z),20-car.velocity.x,unsigned(audible));
    }
    check(real_hits>0 && audible_hits==real_hits,"actual authored prop contacts produce PCM without synthetic impulse gain");
}

void test_backfire_native() {
    using namespace nfsu::audio;
    BackfireSoundController controller;TrafficAudioRandom random;unsigned counter=0,config=~0U,index=0;
    const auto value=[](unsigned bits){float f;std::memcpy(&f,&bits,4);return f;};
    const auto word=[](float f){unsigned bits;std::memcpy(&bits,&f,4);return bits;};
    for(const auto& row:kNativeBackfireAudio){
        if(row[0]!=config){controller={};random.state=row[5];counter=row[6];config=row[0];}
        check(random.state==row[5]&&counter==row[6],"backfire consumes the shared sound RNG in native order");
        const auto cue=controller.tick(int(row[1]),value(row[3]),row[4]!=0,value(row[2]),random,counter);
        if(controller.state!=row[7]||word(controller.wait)!=row[8]||unsigned(controller.gain)!=row[9]||
           cue.emit!=bool(row[10])||(cue.emit&&(cue.variant!=row[11]||unsigned(cue.gain)!=row[12]))||random.state!=row[13]||counter!=row[14])
            throw std::runtime_error("original backfire consecutive control row "+std::to_string(index));
        ++index;
    }
}
void test_human_shift_feedback() {
    using namespace nfsu::audio;
    const auto value=[](unsigned bits){float f;std::memcpy(&f,&bits,4);return f;};
    const auto word=[](float f){unsigned bits;std::memcpy(&bits,&f,4);return bits;};
    HumanEngineShiftCue cue;
    for(const auto& row:kNativeHumanShiftCue) {
        if(row[1]==0)cue={};
        const auto& config=kNativeHumanShiftCueConfig[row[0]];
        if(row[2]!=0xffffffffU)cue.notify(row[2]!=0,int(row[3]),value(row[4]),value(row[5]),value(config[0]),value(config[1]));
        const auto event=cue.tick(value(row[5])+value(row[6]));
        check(event==row[7]&&cue.pending==bool(row[8])&&cue.kind==row[9]&&word(cue.due)==row[10],
            "original delayed shift cue, pending replacement and strict deadline");
    }
    for(const auto& row:kNativeShiftDisengage) {
        const bool admitted=row[1]&&value(row[2])>=7000.F;
        check(admitted==bool(row[3]),"original high-RPM actor admission");
        if(admitted)check(row[4]==(row[0]==0?1U:0U)&&row[5]==20999&&row[6]==4096&&row[7]==0&&row[8]==(row[0]==0?1U:0U)&&row[9]==0,
            "original separate shift actor bank parameters before category output");
    }
    for(const auto& row:kNativeShiftCategory) {
        const auto raw=row[0]==0?unsigned(human_shift_audio_gain(23000,value(row[1]))):20999U;
        const auto category=int(row[row[0]==0?2:3]);
        check(RaceAudioMix::engine_output(raw,category)==row[4],
            "original delayed category1 versus immediate category5 through526AA0/536C20");
    }
    for(const auto& row:kNativeHumanShiftGain)
        check(std::uint32_t(human_shift_audio_gain(std::int16_t(row[0]),value(row[1])))==row[2],
            "original shift audible RPM gain before category and bank");
}
void test_high_rpm_shift_binding(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    for(int level:{0,1,2,3}) for(int main_gain:{0,32767}) {
        RaceEngineSound engine;RaceEffectsSound effects;Mixer em,fm;std::string error;
        check(engine.load(*root,"INTEGRA",level,error),error.c_str());
        check(effects.load(*root,"INTEGRA",level,0,0,error),error.c_str());
        const auto retained=effects.resident_bytes();
        RaceAudioSnapshot s;s.gear=2;s.rpm=7500;s.maximum_rpm=10000;s.torque_maximum=160;s.throttle=1;s.engine_drive_load=100;
        std::array<std::int16_t,1600> pcm{};unsigned begin=0,end=0,delayed=0;bool heard=false;
        for(unsigned tick=0;tick<90;++tick){s.tick=tick+1;if(tick==2)s.gear=3;
            const auto cue=engine.update(s,em,unsigned(main_gain),32767);
            if(cue.disengage&1){check(tick==2,"high-RPM disengage starts at gear notification");++begin;}
            if(cue.disengage&2){check(tick>2,"engage waits for audible RPM envelope completion");++end;}
            if(cue.disengage)check(cue.disengage_gain==int(RaceAudioMix::engine_output(20999,32767)),"shift category remains independent of muted engine");
            if(cue.kind){check((cue.gain==0)==(main_gain==0),"delayed shift retains category1 independently of category5");++delayed;}
            effects.update(s,fm,cue);
            if(cue.disengage&1)check(!fm.active(95),"started native PCM Player dispatches stop when its module retires");
            fm.render(pcm.data(),800);
            heard|=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});
        }
        check(begin==1&&end==1&&delayed==1,"independent original shift notifications reach selected graphs");
        // The native bank has a nonzero kind0 sample only in LRG. The begin
        // event requests module retirement; do not invent a detached voice.
        check(heard==(main_gain!=0||level==3),"selected shift/end PCM respects category and native bank sample binding");
        check(effects.resident_bytes()==retained,"shift actors retain shared PCM and preallocated graph state");
        // Rewind cancels all pending events, as does race finish.
        s.tick=1;s.finished=true;effects.update(s,fm);fm.render(pcm.data(),800);
        check(std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}),"finish cancels immediate and delayed shift voices");
    }
}
void test_shift_audio_binding(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    for(int level:{0,3})for(unsigned pov:{0U,2U}) {
        RaceEngineSound engine;RaceEffectsSound effects;Mixer engine_mixer,fx_mixer;std::string error;
        check(engine.load(*root,"INTEGRA",level,error),error.c_str());
        check(effects.load(*root,"INTEGRA",level,0,0,error),error.c_str());
        RaceAudioSnapshot s;s.camera_pov=pov;s.gear=2;s.rpm=5000;s.maximum_rpm=8000;s.torque_maximum=160;s.throttle=1;s.engine_drive_load=100;
        std::array<std::int16_t,1600> pcm{};unsigned emitted=0;bool heard=false;
        for(unsigned tick=0;tick<60;++tick) {
            s.tick=tick+1;if(tick==2)s.gear=3;
            const auto cue=engine.update(s,engine_mixer,16358);
            if(cue.kind){check(cue.bank_variant==unsigned(pov==2),"actual camera POV reaches native main-shift bank variant");check(tick>4&&cue.kind==1&&cue.gain>0,"selected engine publishes delayed, scaled shift cue");++emitted;}
            effects.update(s,fx_mixer,cue);fx_mixer.render(pcm.data(),800);
            const bool sound=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});
            if(tick<=4)check(!sound,"gear edge alone does not immediately start a shift sample");
            heard|=sound;
        }
        check(emitted==1&&heard,"actual human engine cue reaches selected effect bank and PCM once");
        s.finished=true;++s.tick;const auto cue=engine.update(s,engine_mixer,16358);
        effects.update(s,fx_mixer,cue);fx_mixer.render(pcm.data(),800);
        check(std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}),"finish cancels delayed shift output");
    }
}
void test_backfire_playback(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    for(int level:{0,1,2,3}){
        RaceEngineSound engine;RaceEffectsSound effects;Mixer engine_mixer,mixer;TrafficAudioRandom random;std::string error;
        check(engine.load(*root,"INTEGRA",level,error),error.c_str());
        check(effects.load(*root,"INTEGRA",level,0,0,error),error.c_str());
        const auto retained=effects.resident_bytes();bool heard=false;
        RaceAudioSnapshot s;s.rpm=7500;s.maximum_rpm=10000;s.torque_maximum=160;s.gear=3;s.owner_speed=30;
        std::array<std::int16_t,1600> pcm{};
        for(unsigned tick=0;tick<420;++tick){s.tick=tick+1;s.throttle=tick<60?1:0;s.engine_drive_load=s.throttle*100;
            const auto shift=engine.update(s,engine_mixer);const auto human=engine.turbo_feedback();
            check(human.throttle_on==(tick<60),"actual engine publishes throttle state to backfire owner");
            effects.update(s,mixer,shift,&human,18000,&random);mixer.render(pcm.data(),800);
            const bool audible=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});
            if(tick<60)check(!audible,"open throttle does not trigger SparkChatter");
            heard|=audible;
        }
        check(heard==(level>=2),"native upgrade gate reaches selected backfire PCM on coast");
        check(effects.resident_bytes()==retained,"backfire retains predecoded samples and graph memory");
        s.finished=true;++s.tick;effects.update(s,mixer);mixer.render(pcm.data(),800);
        check(std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}),"finish silences backfire tails");
        s.finished=false;s.collision_reset=true;++s.tick;effects.update(s,mixer);
    }

}
void test_nitrous_control(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    for(int level=0;level<4;++level){
        RaceEffectsSound effects;Mixer mixer;mixer.set_peak_limiter(true);std::string error;
        const bool loaded=effects.load(*root,"INTEGRA",level,level,level,error);
        check(loaded,error.c_str());
        RaceAudioSnapshot s;s.gear=1;RaceShiftSoundCue cue;std::array<std::int16_t,1600> pcm{};
        const auto frame=[&](){++s.tick;effects.update(s,mixer,cue);cue={};mixer.render(pcm.data(),800);
            return std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});};
        check(!frame(),"inactive effects silent; initial gear makes no shift sound");
        s.nitrous_active=true;bool heard=false;
        for(unsigned i=0;i<120;++i)heard|=frame();
        check(heard,"selected NOS graph produces PCM");
        s.nitrous_active=false;bool tail=false;
        for(unsigned i=0;i<240;++i)tail|=frame();
        check(tail,"NOS release sample/tail is audible");
        check(!frame(),"NOS completes without a stuck voice");
        s.gear=2;cue={1,10000};heard=false;for(unsigned i=0;i<30;++i)heard|=frame();
        check(heard,"upshift graph produces PCM");
        s.gear=1;cue={2,10000};heard=false;for(unsigned i=0;i<30;++i)heard|=frame();
        check(heard,"downshift graph produces PCM");
        s.speed_mps=25;s.axle_long_slip={.6,.8};s.axle_lateral_slip={.4,.5};
        heard=false;for(unsigned i=0;i<120;++i)heard|=frame();
        check(heard,"two-axle skid graph produces PCM");
        s.axle_long_slip={};s.axle_lateral_slip={};s.speed_mps=0;
        for(unsigned i=0;i<240;++i)frame();
        check(!frame(),"no-slip/rest returns to silence");
        s.nitrous_active=true;heard=false;for(unsigned i=0;i<60;++i)heard|=frame();
        check(heard,"NOS restarts after completion");
        s.finished=true;for(unsigned i=0;i<10;++i)frame();
        check(!frame(),"finish silences all effect voices");
        check(mixer.clipped_samples()==0,"effects do not clip PCM");
    }
    std::vector<std::int16_t> low_level,high_level;
    for(int level:{1,3}) {
        RaceEffectsSound skyline,integra;Mixer a,b;std::string error;BankAudioRandom random_a,random_b;
        check(skyline.load(*root,"SKYLINE",0,level,0,error),error.c_str());
        check(integra.load(*root,"INTEGRA",1,level,1,error),error.c_str());
        skyline.set_random_context(&random_a);integra.set_random_context(&random_b);
        RaceAudioSnapshot s;s.gear=1;
        std::array<std::int16_t,1600> pa{},pb{};
        auto& output=level==1?low_level:high_level;
        for(unsigned tick=0;tick<180;++tick) {
            s.tick=tick+1;s.nitrous_active=tick>=10&&tick<100;
            skyline.update(s,a);integra.update(s,b);
            a.render(pa.data(),800);b.render(pb.data(),800);
            check(pa==pb,"same NOS setup and matched independent SND contexts produce identical PCM");
            output.insert(output.end(),pa.begin(),pa.end());
        }
    }
    check(low_level!=high_level,"original NOS level1 and level3 banks produce different PCM");
    std::cout<<"NOS mode audit: matched SND contexts and level/activation exact PCM; level1 vs level3 different banks\n";
    std::cout<<"race effects: all upgrade families, start/release/restart/shift/skid/finish PASS\n";
}

void test_turbo_native() {
    using namespace nfsu::audio;
    const auto real=[](unsigned word){float v;std::memcpy(&v,&word,4);return v;};
    const auto bits=[](float v){unsigned word;std::memcpy(&word,&v,4);return word;};
    TurboSoundController controller;TrafficAudioRandom random;
    unsigned index=0;
    for(const auto& r:kNativeTurboControl) {
        if(r[0]){controller.configure({int(r[1]),int(r[2]),real(r[3])});random.state=0;}
        TurboSoundInput input;input.pressure=real(r[4]);input.rpm=real(r[5]);input.load=real(r[6]);input.dt=real(r[7]);
        input.pan=int(r[8]);input.balance=int(r[9]);input.category=int(r[10]);input.position=int(r[11]);input.paused=r[12]!=0;input.release_complete=r[13]!=0;
        const auto out=controller.tick(input,random);
        if(r[0])check(out.start&&!out.blowoff&&out.spool[5]==7000,"turbo primary initialization boundary");
        else {
            bool equal=bits(controller.charge())==r[14]&&bits(controller.normalized())==r[15]&&bits(controller.pressure_mean())==r[16]&&
                controller.state()==r[17]&&random.state==r[18]&&unsigned(out.start)==r[19]&&unsigned(out.blowoff)==r[20];
            for(unsigned i=0;i<6;++i)equal&=unsigned(out.spool[i])==r[21+i]&&unsigned(out.release[i])==r[27+i];
            if(!equal)throw std::runtime_error("original turbo control row "+std::to_string(index));
        }
        ++index;
    }
    for(const auto& r:kNativeTurboSelection)check(turbo_sound_bank(r.level,r.variant)==r.bank,"original turbo resource producer and filename resolution");
}
void test_remote_turbo_native() {
    using namespace nfsu::audio;
    const auto real=[](unsigned word){float v;std::memcpy(&v,&word,4);return v;};
    const auto bits=[](float v){unsigned word;std::memcpy(&word,&v,4);return word;};
    TurboSoundController controller;TrafficAudioRandom random;unsigned index=0;
    for(const auto& r:kNativeRemoteTurbo) {
        if(r[0]){controller.configure({int(r[1]),int(r[2]),real(r[3])});random.state=0;}
        TurboSoundInput in;in.pressure=real(r[4]);in.rpm=real(r[5]);in.load=real(r[6]);in.dt=real(r[7]);
        in.pan=int(r[8]);in.balance=int(r[9]);in.category=int(r[10]);in.position=int(r[11]);in.release_complete=r[12]!=0;
        const auto out=controller.tick_remote(in,random);
        if(r[0])check(out.start&&!out.blowoff&&out.spool[5]==7000,"remote primary initialization boundary");
        else {
            bool equal=bits(controller.charge())==r[13]&&bits(controller.normalized())==r[14]&&controller.state()==r[15]&&random.state==r[16]&&unsigned(out.blowoff)==r[17];
            for(unsigned k=0;k<6;++k)equal&=unsigned(out.spool[k])==r[18+k]&&unsigned(out.release[k])==r[24+k];
            if(!equal){std::cerr<<"remote Turbo row "<<index<<" charge "<<bits(controller.charge())<<'/'<<r[13]<<" normalized "<<bits(controller.normalized())<<'/'<<r[14]<<" rng "<<random.state<<'/'<<r[16]<<'\n';
                for(unsigned k=0;k<6;++k)std::cerr<<"field "<<k<<" spool "<<out.spool[k]<<'/'<<r[18+k]<<" release "<<out.release[k]<<'/'<<r[24+k]<<'\n';
                throw std::runtime_error("original remote Turbo row "+std::to_string(index));}
        }
        ++index;
    }
    for(const auto& r:kNativeTurboPosition) {
        std::array<float,3> source,eye,forward;for(unsigned k=0;k<3;++k){source[k]=real(r[k]);eye[k]=real(r[k+3]);forward[k]=real(r[k+6]);}
        TurboAudioPosition p;p.tick(source,eye,forward);
        if(unsigned(p.gain)!=r[9]||unsigned(p.pan)!=r[10])throw std::runtime_error("native Turbo spatial gain/pan "+std::to_string(p.gain)+"/"+std::to_string(r[9])+" "+std::to_string(p.pan)+"/"+std::to_string(r[10]));
    }
}
void test_remote_turbo_playback(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    Mixer mixer;TrafficAudioRandom random;std::string error;std::array<RaceTurboSound,3> turbos;
    for(unsigned i=0;i<3;++i)check(turbos[i].load_remote(*root,"INTEGRA",1,i,error),error.c_str());
    RaceAudioSnapshot s;s.listener_eye={{6,0,0}};s.listener_forward={{1,0,0}};
    std::array<std::int16_t,1600> pcm{};bool spool=false,release=false;
    for(unsigned frame=0;frame<240;++frame) {
        s.tick=frame+1;const bool on=frame<100;
        HumanTurboFeedback feedback{7000,on?100.F:0.F,32768,512,on};
        for(unsigned i=0;i<3;++i){s.rivals[i].active=true;s.rivals[i].turbo_installed=true;turbos[i].update_remote(s,s.rivals[i],feedback,mixer,10000,random);}
        mixer.render(pcm.data(),800);const bool heard=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});
        if(frame==90)check(mixer.active(104)&&mixer.active(106)&&mixer.active(108),"three remote Turbo primaries stay independent");
        if(frame<100)spool|=heard;else release|=heard;
    }
    check(spool&&release,"remote Turbo spool and load-release reach selected retail PCM");
    s.finished=true;++s.tick;
    for(unsigned i=0;i<3;++i)turbos[i].update_remote(s,s.rivals[i],{},mixer,10000,random);
    for(unsigned i=104;i<110;++i)check(!mixer.active(i),"finish retires every remote Turbo voice");
    check(!turbos[0].load_remote(*root,"INTEGRA",1,3,error),"remote Turbo roster guard");
    RaceTurboSound stock;check(stock.load_remote(*root,"CIVIC",0,0,error),error.c_str());
    s.finished=false;s.rivals[0].turbo_installed=false;++s.tick;stock.update_remote(s,s.rivals[0],{7000,100,32768,512,true},mixer,10000,random);
    check(!mixer.active(104)&&!mixer.active(105),"stock rival without induction cannot publish Turbo");
    check(stock.load_remote(*root,"SKYLINE",0,0,error),error.c_str());s.rivals[0].turbo_installed=true;
    bool stock_heard=false;
    for(unsigned tick=0;tick<100;++tick){++s.tick;stock.update_remote(s,s.rivals[0],{7000,100,32768,512,true},mixer,10000,random);mixer.render(pcm.data(),800);stock_heard|=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});}
    check(stock_heard&&mixer.active(104),"factory Turbo at upgrade0 uses the original level0 bank");
    stock.stop(mixer);
}
void test_turbo_playback(const std::filesystem::path* root) {
    if(!root)return;
    using namespace nfsu::audio;
    for(const auto& selected:std::array<std::pair<const char*,int>,2>{{{"INTEGRA",1},{"SKYLINE",3}}}) {
        RaceTurboSound turbo;RaceEngineSound engine;Mixer mixer,engine_mixer;TrafficAudioRandom random;std::string error;
        check(turbo.load(*root,selected.first,selected.second,error),error.c_str());
        check(engine.load(*root,selected.first,0,error),error.c_str());
        RaceAudioSnapshot s;s.turbo_installed=true;s.rpm=7000;s.maximum_rpm=8000;s.engine_drive_load=100;s.throttle=1;s.gear=3;
        s.listener_eye={{6,0,0}};s.human_forward={{1,0,0}};
        std::array<std::int16_t,1600> pcm{};bool spool=false,release=false;
        for(unsigned frame=0;frame<240;++frame) {
            s.tick=frame+1;s.turbo_normalized=frame<100?1.3F:0.F;
            (void)engine.update(s,engine_mixer);const auto human=engine.turbo_feedback();
            turbo.update(s,human,mixer,10000,random);mixer.render(pcm.data(),800);
            const bool heard=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});
            if(frame<100)spool|=heard;else release|=heard;
        }
        check(spool&&release,"human engine feedback reaches authored turbo spool and pressure-release PCM");
        s.finished=true;++s.tick;turbo.update(s,{},mixer,10000,random);mixer.render(pcm.data(),800);
        check(!mixer.active(85)&&!mixer.active(86)&&std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}),"finish stops both turbo voices");
        s.finished=false;s.turbo_installed=false;++s.tick;turbo.update(s,{},mixer,10000,random);
        check(!mixer.active(85)&&!mixer.active(86),"absent induction cannot start turbo output");
        check(mixer.clipped_samples()==0,"turbo PCM does not clip at the native race category");
    }
}

void test_human_camera_balance() {
    using namespace nfsu::audio;
    for(const auto& r:kNativeHumanCameraBalance) {
        std::array<float,3> source,axis;for(unsigned i=0;i<3;++i){std::memcpy(&source[i],&r[2+i],4);std::memcpy(&axis[i],&r[5+i],4);}
        const auto result=human_engine_camera_balance(source,axis,{{6,0,0}},-50,765,r[8],r[9]);
        if(result.angle!=r[10]||result.phase!=r[11])std::cerr<<"human camera "<<r[0]<<','<<r[1]<<" native="<<r[10]<<','<<r[11]<<" port="<<result.angle<<','<<result.phase<<'\n';
        check(result.angle==r[10]&&result.phase==r[11],"original human camera balance publication");
    }
}

void test_human_engine_native() {
    using namespace nfsu::audio;
    HumanEngineEnvelope envelope;unsigned config=UINT32_MAX,index=0;
    const auto real=[](unsigned bits){float v;std::memcpy(&v,&bits,4);return v;};
    for(const auto& row:kNativeHumanEngineRows) {
        const auto& c=kNativeHumanEngineConfig[row[0]];
        if(row[0]!=config){config=row[0];EngineEnvelopeTuning tuning;
            tuning.up_offset=int(c[0]);tuning.up_ms=int(c[1]);tuning.up_extra_ms=int(c[2]);tuning.up_extra=real(c[3]);
            tuning.down_rise=int(c[4]);tuning.down_rise_ms=int(c[5]);tuning.down_dip=int(c[6]);tuning.down_dip_ms=int(c[7]);tuning.catch_ms_per_rpm=real(c[8]);
            tuning.human_up_drop=int(c[9]);tuning.human_up_drop_ms=int(c[10]);tuning.human_down_drop=int(c[13]);tuning.human_down_drop_ms=int(c[14]);envelope.configure(tuning);}
        EngineEnvelopeInput in;in.rpm=real(row[1]);in.maximum_rpm=real(row[2]);in.drive_load=real(row[3]);in.throttle=real(row[4]);in.brake=real(row[5]);in.gear=int(row[6]);in.dt=real(row[7]);in.load_divisor=real(c[17]);
        const auto out=envelope.tick(in);const auto gain=human_engine_audio_gains(int(c[15]),int(c[16]),int(row[8]),out.gain_boost);
        const auto word=[](float v){unsigned u;std::memcpy(&u,&v,4);return u;};
        const bool equal=word(out.rpm)==row[9]&&word(out.load)==row[10]&&out.shift_state==row[11]&&word(out.gain_boost)==row[12]&&
            RaceAudioMix::engine_output(gain[0],16358)==row[13]&&RaceAudioMix::engine_output(gain[1],16358)==row[14];
        const auto state=envelope.profile_state();
        if(word(state.time)!=row[15]||word(state.throttle_on)!=row[16]||word(state.throttle_off)!=row[17]||word(state.last_shift)!=row[18]) {
            std::cerr<<"human profile row="<<index<<" time="<<word(state.time)<<'/'<<row[15]<<" on="<<word(state.throttle_on)<<'/'<<row[16]<<" off="<<word(state.throttle_off)<<'/'<<row[17]<<" shift="<<word(state.last_shift)<<'/'<<row[18]<<'\n';
            check(false,"original human local time, throttle edges and accepted shift timestamp");
        }
        if(!equal)std::cerr<<"human row="<<index<<" native="<<row[9]<<','<<row[10]<<','<<row[11]<<','<<row[12]<<','<<row[13]<<','<<row[14]<<" port="<<word(out.rpm)<<','<<word(out.load)<<','<<out.shift_state<<','<<word(out.gain_boost)<<','<<RaceAudioMix::engine_output(gain[0],16358)<<','<<RaceAudioMix::engine_output(gain[1],16358)<<'\n';
        check(equal,"actual consecutive human engine envelope and gain");++index;
    }
}

void test_remote_script_native() {
    using namespace nfsu::audio;
    const auto f=[](unsigned u){float v;std::memcpy(&v,&u,4);return v;};
    const auto bits=[](float v){unsigned u;std::memcpy(&u,&v,4);return u;};
    for(unsigned c=0;c<2;++c) {
        RemoteRevScript script;TrafficAudioRandom sound,world;
        sound.state=0x12345678U+c;world.state=0xabcde134U+c;
        for(unsigned i=0;i<1260;++i) {
            const auto& row=kNativeRemoteScript[c*1260+i];
            const bool override=script.tick(int(row[3]),f(row[4]),f(row[5]),f(row[6]),f(row[2]),sound,world);
            check(sound.state==row[7]&&world.state==row[18]&&unsigned(script.active)==row[8]&&
                script.cursor==row[9]&&bits(script.elapsed)==row[10]&&bits(script.wait)==row[11]&&
                bits(script.total)==row[12]&&bits(script.rpm)==row[13]&&bits(script.load)==row[14]&&unsigned(script.returned)==row[15]&&
                bits(override?script.rpm:f(row[5]))==row[16]&&bits(override?script.load:f(row[6]))==row[17],
                "original consecutive remote rev script, countdown return and both RNGs");
        }
    }
}
void test_remote_position_native() {
    for(unsigned case_id=0;case_id<8;++case_id) {
        nfsu::audio::RemoteAudioPosition position;
        for(unsigned tick=0;tick<150;++tick) {
            const auto& r=kNativeRemotePosition[case_id*150+tick];
            std::array<float,3> xyz;for(unsigned i=0;i<3;++i)std::memcpy(&xyz[i],&r[2+i],4);
            position.tick(xyz,{{0,0,0}},{{6,0,0}},{{1,0,0}},1000.F/60.F);
            std::uint32_t doppler;std::memcpy(&doppler,&position.doppler,4);
            if(unsigned(position.gain)!=r[5]||unsigned(position.pan)!=r[6]||doppler!=r[7])
                std::cerr<<"remote position "<<case_id<<','<<tick<<" native="<<r[5]<<','<<r[6]<<','<<r[7]<<" port="<<position.gain<<','<<position.pan<<','<<doppler<<'\n';
            check(unsigned(position.gain)==r[5]&&unsigned(position.pan)==r[6]&&doppler==r[7],"original remote spatial publication");
        }
    }
}
void test_traffic_position_native() {
    for(unsigned case_id=0;case_id<4;++case_id) {
        nfsu::audio::TrafficAudioPosition position;
        for(unsigned tick=0;tick<150;++tick) {
            const auto& r=kNativeTrafficPosition[case_id*150+tick];
            std::array<float,3> xyz;for(unsigned i=0;i<3;++i)std::memcpy(&xyz[i],&r[2+i],4);
            position.tick(xyz,{{0,0,0}},{{6,0,0}},{{1,0,0}},1000.F/60.F);
            std::uint32_t doppler;std::memcpy(&doppler,&position.doppler,4);
            if(unsigned(position.gain)!=r[5]||unsigned(position.pan)!=r[6]||doppler!=r[7])
                std::cerr<<"traffic position "<<case_id<<','<<tick<<" native="<<r[5]<<','<<r[6]<<','<<r[7]<<" port="<<position.gain<<','<<position.pan<<','<<doppler<<'\n';
            check(unsigned(position.gain)==r[5]&&unsigned(position.pan)==r[6]&&doppler==r[7],"original traffic spatial publication");
        }
    }
}

void test_traffic_audio_native() {
    using namespace nfsu::audio;
    for(unsigned case_id=0;case_id<5;++case_id) {
        TrafficAudioController controller;TrafficAudioRandom rng;
        for(unsigned tick=0;tick<720;++tick) {
            TrafficAudioInput in;in.human_position={{10,0,0}};in.velocity={{10,0,0}};
            in.human_forward={{-1,0,0}};in.human_speed=40;in.gain=18000;in.category=16358;
            in.dt=tick%9?1.F/60.F:1.F/120.F;in.car_type=case_id==1?22:20;
            if(case_id==2){in.human_position[0]=3;in.human_velocity[0]=tick<400?-40.F:20.F;in.human_forward[0]=1;}
            if(case_id==3){in.human_position[0]=tick<200?31.F:-10.F;in.human_speed=tick<350?24.F:40.F;}
            if(case_id==4){in.drag=true;in.human_position[0]=-3;in.human_velocity[0]=-100;in.human_forward[0]=1;}
            in.paused=tick>=300&&tick<330;
            controller.tick(in,rng);
            const auto& row=kNativeTrafficAudio[case_id*720+tick];
            const auto word=[](float v){std::uint32_t u;std::memcpy(&u,&v,4);return u;};
            const auto controls=[](const auto& v){return std::array<unsigned,5>{{v.event,v.gain,v.pitch,v.pan,v.flags}};};
            bool equal=word(controller.time)==row[3]&&rng.state==row[4]&&controller.horn.live==bool(row[5])&&
                controller.pass.live==bool(row[6])&&word(controller.fade_value)==row[7]&&controller.fading==bool(row[8])&&controller.horn_requested==bool(row[9]);
            for(unsigned voice=0;voice<2;++voice) {
                const auto& v=voice?controller.pass:controller.horn;const auto values=controls(v);
                for(unsigned i=0;i<5;++i)if((v.live?values[i]:0)!=row[10+voice*5+i])equal=false;
            }
            if(!equal){std::cerr<<"traffic audio case="<<case_id<<" tick="<<tick<<" native=";
                for(auto n:row){std::cerr<<n<<',';}std::cerr<<" port="<<word(controller.time)<<','<<rng.state<<','<<controller.horn.live<<','<<controller.pass.live<<','<<word(controller.fade_value)<<','<<controller.fading<<','<<controller.horn_requested<<',';
                for(auto n:controls(controller.horn)){std::cerr<<n<<',';}for(auto n:controls(controller.pass)){std::cerr<<n<<',';}std::cerr<<'\n';}
            check(equal,"original consecutive traffic horn/pass-by scheduler");
        }
    }
}

void test_weights() {
    const auto low = nfsu::audio::engine_layer_weights(1000);
    check(std::abs(low[0] - 15.0 / 17.0) < 1e-12 &&
          std::abs(low[1] - 2.0 / 17.0) < 1e-12 &&
          low[2] == 0 && low[3] == 0, "engine crossfade direction");
    const auto high = nfsu::audio::engine_layer_weights(6000);
    check(high[0] == 0 && high[1] == 0 &&
          std::abs(high[2] - 0.4) < 1e-12 &&
          std::abs(high[3] - 0.6) < 1e-12,
          "engine high crossfade direction");
}

void test_playlist_order() {
    nfsu::audio::Bank bank;
    bank.entries = {{1, 200, 0}, {1, 0, 0}, {1, 100, 0}};
    std::vector<nfsu::audio::Segment> indexed(3);
    indexed[0].offset = 0;
    indexed[1].offset = 100;
    indexed[2].offset = 200;
    const auto playlist = nfsu::audio::race_music_playlist(bank, indexed);
    check(playlist.size() == 2 && playlist[0].offset == 200 &&
          playlist[1].offset == 100,
          "music table order with preview segment skipped");
    bank.entries[0].value = 300;
    bool rejected = false;
    try { (void)nfsu::audio::race_music_playlist(bank, indexed); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "music bank reference must resolve");
}

void test_missing_audio() {
    nfsu::audio::RaceAudioService service;
    std::string diagnostic;
    check(!service.start("nfsu-audio-missing-fixture-835148", diagnostic),
          "missing audio must not start");
    check(diagnostic.find("music: missing AST") != std::string::npos &&
          service.diagnostic() == diagnostic,
          "missing music fallback and visible diagnostic");
}

// The retail root comes only from the command line (issue #233): CTest
// registers the retail run, nfsu_race_audio_retail, with the configured
// NFSU_RETAIL_ROOT, and the plain run has no root and no retail half.
void test_retail_music_prefix(const std::filesystem::path* root) {
    if (!root) {
        std::cout << "race audio: retail check not run here, no root argument "
                     "(see nfsu_race_audio_retail)\n";
        return;
    }
    check(!root->empty(), "the retail root argument is not empty");
    std::ifstream input(*root /
        "SNDSTREAMS/STRM_Music_UG0_MB.ast", std::ios::binary);
    check(bool(input), "retail music stream available");
    unsigned polls = 0;
    const auto segments = nfsu::audio::scan_ast_prefix(input,
        6 * 1024 * 1024, 2, [&] { ++polls; });
    check(segments.size() == 2 && segments[1].end == 5343716 &&
          segments[1].rate == 22050 && segments[1].channels == 2 &&
          polls >= 20, "retail music bounded index and progress");

    std::ifstream bank_input(*root /
        "Sound/STRMBANK/STRM_Music_UG0_MB.abk", std::ios::binary);
    check(bool(bank_input), "retail music bank available");
    const std::vector<std::uint8_t> bank_bytes(
        (std::istreambuf_iterator<char>(bank_input)),
        std::istreambuf_iterator<char>());
    const auto bank = nfsu::audio::parse_abk(bank_bytes);
    std::vector<std::uint64_t> offsets;
    for (const auto& entry : bank.entries)
        if (entry.type == 1) offsets.push_back(entry.value);
    std::sort(offsets.begin(), offsets.end());
    check(offsets.size() == 27 && offsets.front() == 0 &&
          std::adjacent_find(offsets.begin(), offsets.end()) == offsets.end(),
          "music bank has distinct segment offsets");
    input.clear();
    input.seekg(0, std::ios::end);
    const auto stream_size = static_cast<std::uint64_t>(input.tellg());
    std::vector<nfsu::audio::Segment> indexed;
    for (std::size_t i = 0; i < offsets.size(); ++i)
        indexed.push_back(nfsu::audio::index_ast_segment_by_boundary(input,
            offsets[i], i + 1 < offsets.size() ? offsets[i + 1] : stream_size));
    const auto fully_scanned = nfsu::audio::scan_ast(input);
    check(fully_scanned.size() == indexed.size(),
          "retail music index count matches full scan");
    for (std::size_t i = 0; i < indexed.size(); ++i)
        check(indexed[i].offset == fully_scanned[i].offset &&
              indexed[i].end == fully_scanned[i].end &&
              indexed[i].frames == fully_scanned[i].frames &&
              indexed[i].rate == fully_scanned[i].rate &&
              indexed[i].channels == fully_scanned[i].channels &&
              indexed[i].blocks == fully_scanned[i].blocks &&
              indexed[i].codec == fully_scanned[i].codec,
              "bank-boundary index matches full retail AST scan");
    check(indexed[1].offset == segments[1].offset &&
          indexed[1].end == segments[1].end &&
          indexed[1].frames == segments[1].frames &&
          indexed[16].codec == nfsu::audio::Codec::ea_xa_v1 &&
          indexed[16].rate == 44100 && indexed[26].end <= stream_size,
          "all retail music segments indexed by bank boundaries");
    const auto playlist = nfsu::audio::race_music_playlist(bank, indexed);
    check(playlist.size() == 26 && playlist.front().offset == 717824 &&
          playlist.back().offset == 143246848,
          "retail playlist follows full song entries");
    std::cout << "race audio: retail music prefix pinned\n";
}

void test_remote_ratio_snapshot() {
    using namespace nfsu;
    VehicleTrackCourse course;std::string error;
    check(build_vehicle_proving_ground(course,error),"remote ratio course");
    RaceSession race(course);RaceRivalSetup setup;setup.spec=stock_240sx_vehicle_spec();setup.hull=stock_240sx_hull();
    setup.pose=course.start;setup.pose.ground.y+=8;setup.name="audio-ratio-rival";
    check(race.set_rivals({setup},error),error.c_str());check(race.start(error),error.c_str());
    const auto snapshot=nfsu::audio::race_audio_snapshot(race);const auto& input=snapshot.rivals[0].engine;
    const auto gear=race.rival(0).car().gear;
    check(snapshot.rivals[0].active&&input.gear==gear&&input.gear_ratio==setup.spec.handling.gear_ratios[unsigned(gear)]&&
        input.first_gear_ratio==setup.spec.handling.gear_ratios[2],"remote gain receives actual selected current/first gear ratios");
    check(input.first_gear_ratio!=1&&input.gear_ratio!=1,"live ratio publication differs from synthetic identity defaults");
    check(!snapshot.rivals[0].turbo_installed,"zero selected induction has no remote Turbo actor");
    setup.spec.induction[4]=.1F;RaceSession boosted(course);
    check(boosted.set_rivals({setup},error)&&boosted.start(error),error.c_str());
    check(nfsu::audio::race_audio_snapshot(boosted).rivals[0].turbo_installed,
        "remote Turbo admission uses positive selected RideInfo curve at upgrade0");
}
void test_grid_engine_snapshot() {
    using namespace nfsu;
    using namespace nfsu::audio;
    VehicleTrackCourse course;std::string error;
    check(build_vehicle_proving_ground(course,error),"grid audio course");
    RaceSession race(course);check(race.start(error),"grid audio start");
    check(race.advance(race_input_frame({1,0,0},0),error),"grid audio connect");
    const auto idle=race_audio_snapshot(race);
    const auto gas=gamepad_button_bit(GamepadButton::trigger_right);
    for(unsigned n=1;n<=60;++n)
        check(race.advance(race_input_frame({1,gas,0},n),error),"grid audio throttle");
    const auto rev=race_audio_snapshot(race);
    check(rev.engine_game_state==int(race.timing().stage)&&
          rev.engine_drive_load==race.telemetry().model.audio_transmitted_torque&&
          rev.brake==race.telemetry().model.brake&&rev.handbrake==race.telemetry().model.handbrake,
          "engine envelope receives actual torque, brake and native race stage");
    check(rev.gear_ratio==race.spec().handling.gear_ratios[unsigned(rev.gear)]&&
        rev.first_gear_ratio==race.spec().handling.gear_ratios[2]&&rev.first_gear_ratio!=1,
        "human sound profile receives selected current/first ratios instead of synthetic identity");
    check(race_car_held(race.timing())&&rev.rpm>idle.rpm+2000&&rev.throttle==1,
          "audio snapshot receives live countdown RPM and throttle");
    std::cout<<"GRID_AUDIT idle/held step="<<idle.physics_step<<"/"<<rev.physics_step<<" contacts="<<rev.contact_count<<" speed="<<rev.speed_mps<<" idle_speed="<<idle.speed_mps<<" nos="<<rev.nitrous_active<<"\n";
    // Grid preparation settles the body before countdown; revving must not
    // advance that epoch/velocity or emit a contact/NOS event. The settled
    // body retains its residual velocity while its placement is held.
    check(rev.physics_step==idle.physics_step&&rev.contact_count==0&&rev.speed_mps==idle.speed_mps&&!rev.nitrous_active,
          "engine revs do not emit collision or nitrous audio");
    check(engine_layer_weights(rev.rpm)!=engine_layer_weights(idle.rpm),
          "countdown RPM selects different engine sound layers");
    check(race.advance(race_input_frame({1,0,0},61),error),"grid audio release");
    const auto release=race_audio_snapshot(race);
    check(release.throttle>0.9&&release.throttle<rev.throttle,
          "audio receives original neutral throttle decay after release");
    for(unsigned n=62;n<=102;++n)
        check(race.advance(race_input_frame({1,0,0},n),error),"grid audio release decay");
    check(race_audio_snapshot(race).throttle==0,"neutral throttle eventually reaches zero");
    std::cout<<"grid audio: idle="<<idle.rpm<<" rev="<<rev.rpm<<" RPM\n";
}

void test_live_collision_clock() {
    using namespace nfsu;
    using namespace nfsu::audio;
    VehicleTrackCourse course;
    std::string error;
    check(build_vehicle_proving_ground(course,error),"audio clock proving ground");
    VehicleTrackScenario scenario;
    check(vehicle_track_scenario(VehicleTrackCourseKind::proving_ground,"wall-square",scenario),
          "audio clock wall scenario");
    course.start=vehicle_track_scenario_pose(course.start,scenario);
    RaceSession race(course), silent(course);
    check(race.start(error) && silent.start(error),"audio clock races start");
    CollisionGraph graph;
    graph.impact_gain.fill(20000); graph.scrape_gain.fill(20000);
    graph.scrape_pitch.fill(4096); graph.scrape_gain_targets.fill(32767);
    CollisionScheduler scheduler;
    unsigned countdown=0, contacts=0, scrapes=0, impacts=0;
    std::map<std::uint32_t,std::uint64_t> last_contact;
    while (race_car_held(race.timing()) && countdown<1000) {
        const auto pad=race_input_frame({1,0,0},countdown++);
        check(race.advance(pad,error) && silent.advance(pad,error),"audio countdown advances");
        const auto snapshot=race_audio_snapshot(race);
        check(snapshot.tick==race.timing().step && race.telemetry().step==0 &&
              snapshot.physics_step==0 && snapshot.contact_count==0,"countdown advances race clock without physics contacts");
        const auto events=scheduler.update(snapshot.tick,snapshot.physics_step,snapshot.contacts.data(),snapshot.contact_count,
                                           graph,snapshot.collision_reset);
        check(events.impacts.empty() && events.scrapes.empty(),"countdown has no collision voices");
    }
    check(countdown>0 && countdown<1000,"audio countdown reaches GO");
    const auto gas=gamepad_button_bit(GamepadButton::trigger_right);
    for (unsigned n=0;n<2000;++n) {
        const bool reset=n==1700;
        const auto buttons=gas | (reset ? gamepad_button_bit(GamepadButton::select) : 0U);
        const auto pad=race_input_frame({1,buttons,0},countdown+n);
        check(race.advance(pad,error) && silent.advance(pad,error),"audio wall race advances");
        const auto snapshot=race_audio_snapshot(race);
        check(snapshot.physics_step==race.telemetry().step &&
              snapshot.tick>snapshot.physics_step/kDriveStepsPerFrame,
              "real countdown leaves distinct clock epochs");
        std::size_t copied=0;
        for (const auto& step : race.physics_telemetry()) {
            for (std::size_t k=0;k<step.audio_contact_count;++k) {
                const auto& input=step.audio_contacts[k];
                const auto& output=snapshot.contacts[copied++];
                check(output.step==step.step && output.pair==input.polygon &&
                      output.impulse.x==input.impulse.x && output.slide_mps==input.slide_mps &&
                      output.point_valid&&output.point.x==input.point.x&&output.point.y==input.point.y&&output.point.z==input.point.z,
                      "snapshot preserves physical contact identity, clock and value");
            }
        }
        check(copied==snapshot.contact_count,"snapshot copies all physics slices");
        const auto events=scheduler.update(snapshot.tick,snapshot.physics_step,snapshot.contacts.data(),snapshot.contact_count,
                                           graph,snapshot.collision_reset);
        if (reset) {
            check(snapshot.collision_reset && events.impacts.empty() && events.scrapes.empty(),
                  "real reset discards all contacts from its audio frame");
            last_contact.clear();
        } else {
            contacts+=unsigned(snapshot.contact_count); scrapes+=unsigned(events.scrapes.size());
            for (const auto& voice : events.impacts) {
                ++impacts;
                // Find this impact's observation in substep order. There can
                // be several updates for one polygon in the same audio frame.
                auto& last=last_contact[voice.pair];
                const CollisionContact* observed=nullptr;
                for (std::size_t k=0;k<snapshot.contact_count;++k)
                    if (snapshot.contacts[k].pair==voice.pair && snapshot.contacts[k].step>last &&
                        world_collision_subtype(snapshot.contacts[k])>=0) {
                        observed=&snapshot.contacts[k]; break;
                    }
                check(observed!=nullptr,"impact command belongs to copied real contact");
                if (last && observed->step==last+1) {
                    check(!voice.start,"continuous physical contact does not restart impact after countdown");
                }
                last=observed->step;
            }
        }
        if (!events.scrapes.empty()) {
            // Pause keeps the session still. Re-reading the snapshot must
            // neither replay an impact nor advance graph control state.
            const auto record=race.record();
            for (unsigned repeat=0;repeat<3;++repeat) {
                const auto paused=race_audio_snapshot(race);
                check(!scheduler.update(paused.tick,paused.physics_step,paused.contacts.data(),paused.contact_count,
                                         graph,paused.collision_reset).processed,
                      "paused or duplicate real snapshot does not advance collision graph");
            }
            check(race.record()==record,"paused snapshot leaves simulation record unchanged");
        }
        check(race.record()==silent.record(),"audio observation leaves real race replay unchanged");
    }
    check(contacts>0 && scrapes>0 && impacts>0,
          "real countdown wall run produces scrape and impact commands");
}

void test_collision_clock_contract() {
    using namespace nfsu::audio;
    CollisionGraph graph;
    graph.impact_gain.fill(20000); graph.scrape_gain.fill(20000);
    graph.scrape_pitch.fill(4096);
    // Four full hull batches for one continuous polygon episode. The old
    //512 cap lost steps3/4, then expired the scrape using newest step4.
    std::array<CollisionContact,1024> four_slices{};
    for(std::size_t i=0;i<four_slices.size();++i) {
        auto& c=four_slices[i];
        c.step=1+i/256;c.pair=7;c.impulse={1,0,0};c.slide_mps=20;
    }
    CollisionScheduler full_frame;
    const auto complete=full_frame.update(1,4,four_slices.data(),four_slices.size(),graph);
    check(complete.impacts.size()==4 && complete.scrapes.size()==1 &&
          complete.impacts.front().start && !complete.impacts.back().start,
          "contacts beyond512 preserve newest slice and continuous scrape episode");
    std::array<CollisionContact,kCollisionContactFrameLimit> roster_and_world{};
    constexpr unsigned roster=nfsu::kRaceRivalCapacity+nfsu::kRaceTrafficCapacity;
    for(unsigned i=0;i<roster;++i){auto& c=roster_and_world[i];c.step=1;c.pair=0x80000000U+i;c.other_kind=0;c.impulse={1,0,0};}
    std::copy(four_slices.begin(),four_slices.end(),roster_and_world.begin()+roster);
    CollisionScheduler mixed_frame;
    const auto mixed=mixed_frame.update(1,4,roster_and_world.data(),roster_and_world.size(),graph);
    check(mixed.impacts.size()==roster+4&&mixed.impacts.back().pair==7&&mixed.scrapes.size()==1,
          "full player/traffic/rival contacts do not truncate the final world physics slice");
    for(auto& c:four_slices) c.other_kind=1;
    four_slices.back().other_kind=3;
    CollisionScheduler last_contact;
    const auto last=last_contact.update(1,4,four_slices.data(),four_slices.size(),graph);
    check(last.impacts.size()==1 && last.scrapes.size()==1 && last.impacts[0].pair==7,
          "last bounded contact at index1023 reaches the scheduler");
    for (int i=0;i<10;++i) {
        graph.scrape_gain_targets[i]=std::int16_t(15000+i*1000);
        graph.scrape_pitch_targets[i]=std::int16_t(200+i*10);
    }
    const auto equal=[](const CollisionAudioFrame& a,const CollisionAudioFrame& b) {
        const auto voices=[](const auto& x,const auto& y) {
            if (x.size()!=y.size()) return false;
            for (std::size_t i=0;i<x.size();++i)
                if (x[i].pair!=y[i].pair || x[i].sample!=y[i].sample ||
                    x[i].gain_q15!=y[i].gain_q15 || x[i].pitch_q16!=y[i].pitch_q16 ||
                    x[i].start!=y[i].start || x[i].pan!=y[i].pan || x[i].positional!=y[i].positional) return false;
            return true;
        };
        return a.processed==b.processed && voices(a.impacts,b.impacts) && voices(a.scrapes,b.scrapes);
    };
    CollisionScheduler zero, offset;BankAudioRandom zero_random,offset_random;
    zero.set_random_context(&zero_random);offset.set_random_context(&offset_random);
    for (unsigned tick=1;tick<=180;++tick) {
        CollisionContact c[2];
        for (unsigned k=0;k<2;++k) {
            c[k].step=2*tick-1+k; c[k].pair=7;
            c[k].impulse={1,0,0}; c[k].slide_mps=20;
        }
        // A short empty frame retains the original 300ms contact lifetime.
        const auto count=tick==90 ? 0U : 2U;
        const auto a=zero.update(tick,2*tick,c,count,graph);
        const auto b=offset.update(tick+55,2*tick,c,count,graph);
        check(equal(a,b),"collision output independent of race clock epoch");
        if (tick==90) {
            check(a.processed && a.impacts.empty() && a.scrapes.size()==1,"empty short frame retains scrape");
        } else {
            check(a.impacts.size()==2 && a.scrapes.size()==1,"continuous contact retains scrape after clock offset");
            const bool start=tick==1;
            check(a.impacts[0].start==start && !a.impacts[1].start && a.scrapes[0].start==start,
                  "continuous episode starts only initially across short contact gaps");
        }
        check(!offset.update(tick+55,2*tick,c,count,graph).processed,
              "duplicate frame preserves graph control and random state");
    }
    CollisionContact c; c.step=362; c.pair=7; c.impulse={1,0,0}; c.slide_mps=20;
    // A changed race tick with an unchanged physics clock represents a held
    // simulation. It must not age contacts, replay impacts or advance ramps.
    check(!offset.update(236,360,&c,1,graph).processed,"held physics clock freezes graph");
    check(equal(zero.update(181,362,&c,1,graph),offset.update(237,362,&c,1,graph)),
          "resume matches uninterrupted graph state after duplicate and held frames");
    auto f=offset.update(238,364,&c,1,graph,true);
    check(f.processed && f.impacts.empty() && f.scrapes.empty(),"reset clears voices and discards copied contacts");
    check(!offset.update(238,364,&c,1,graph,true).processed,"duplicate reset is idempotent");
    c.step=366;
    f=offset.update(239,366,&c,1,graph);
    check(f.impacts.size()==1 && f.impacts[0].start && f.scrapes.size()==1 && f.scrapes[0].start,
          "contact after reset creates a fresh episode");
    CollisionScheduler repeat;
    c={};c.pair=7;c.step=2;c.impulse={20,0,0};
    check(repeat.update(1,2,&c,1,graph).impacts[0].start,"first rival impact starts");
    for(unsigned tick=2;tick<60;++tick){c.step=tick*2;c.impulse={0,0,0};repeat.update(tick,tick*2,&c,1,graph);}
    c.step=120;c.impulse={20,0,0};
    const auto second=repeat.update(60,120,&c,1,graph);
    check(second.impacts.size()==1&&second.impacts[0].start,"quiet touching pair permits another audible impact");
    CollisionScheduler fresh;BankAudioRandom fresh_random;fresh_random.restore(offset_random.state());
    fresh.set_random_context(&fresh_random);
    c.step=2;
    check(equal(fresh.update(1,2,&c,1,graph),offset.update(240,2,&c,1,graph)),
          "physical clock rewind clears episodes even when race tick moves forward");
}

// Source-derived collision selectors and port episode policy are tested
// separately: these tests do not assert retail lifecycle/RNG parity.
void test_collision_graph(const std::filesystem::path* root) {
    using namespace nfsu::audio;
    for(const auto& row:kNativeCollisionIntensity){float input;std::memcpy(&input,&row[1],4);
        check(collision_intensity(input,int(row[0]))==int(row[2])&&collision_scrape_intensity(input)==int(row[3]),
              "original world impact/scrape binary32 curve and truncation");
    }
    CollisionDeck deck;
    unsigned previous=99;
    for (unsigned cycle=0;cycle<6;++cycle) {
        std::array<bool,10> seen{};
        for (unsigned i=0;i<10;++i) {
            const unsigned selected=deck.next(97*cycle+3*i+7);
            check(selected<10 && !seen[selected],"deck consumes every variant once per cycle");
            if (i==0) check(selected!=previous,"deck excludes previous cycle final choice at wrap");
            seen[selected]=true; previous=selected;
        }
    }
    CollisionContact c;
    c.impulse={1,0,0};
    check(world_collision_subtype(c)==4,"longitudinal subtype from greater branch");
    c.impulse={0,1,0};
    check(world_collision_subtype(c)==5,"transverse subtype from lesser branch");
    c.impulse={0,0,1};
    check(world_collision_subtype(c)==3,"upright vertical impulse");
    c.up.z=0; check(world_collision_subtype(c)==2,"vertical overturned branch");
    c.up.z=0.5; check(world_collision_subtype(c)==4,"vertical intermediate orientation");
    c.up.z=double(0.7070000171661377F);
    check(world_collision_subtype(c)==4,"vertical up boundary is strict");
    c.up.z=std::nextafter(c.up.z,1.0);
    check(world_collision_subtype(c)==3,"just above vertical up boundary");
    c.up={0,0,1}; c.impulse={1,0,0}; c.forward={0.707,0,0};
    check(world_collision_subtype(c)==5,"horizontal equality selects lesser family");
    c.forward.x=std::nextafter(0.707,1.0);
    check(world_collision_subtype(c)==4,"horizontal threshold greater selects family4");
    c.other_kind=4; check(world_collision_subtype(c)==4,"placed body uses world class");
    c.other_kind=1; check(world_collision_subtype(c)==-1,"smokeable not guessed as world");
    c.other_kind=0; check(world_collision_subtype(c)==-1,"car class not guessed as world");
    c.other_kind=3; c.impulse={};
    check(world_collision_subtype(c)==-1,"zero impulse cannot classify impact");
    check(collision_intensity(51,4)==127 && collision_intensity(39,5)==127 &&
          collision_intensity(3,3)==127,"subtype-scaled saturation boundaries");
    check(collision_intensity(51.0/2,4)==63 && collision_intensity(0,4)==0 &&
          collision_intensity(-1,4)==0 && collision_intensity(1,-1)==0,
          "intensity truncation and invalid input");
    check(collision_intensity(51.0/254,4)==0 && collision_intensity(3*51.0/254,4)==1,
          "half-integer intensity truncates instead of graph rounding");
    CollisionGraph graph;
    for (int i=0;i<128;++i) {
        graph.impact_gain[i]=std::int16_t(i*200);
        graph.impact_pitch[i]=std::int16_t(i*3-100);
        graph.scrape_gain[i]=std::int16_t(i*200);
        graph.scrape_pitch[i]=std::int16_t(2000+i*10);
    }
    graph.scrape_gain_targets.fill(32767); graph.scrape_pitch_targets.fill(400);
    //Multiple responses of one body pair on one physics step must preserve
    //465830's sum of magnitudes, not discard the second or sum vectors.
    CollisionScheduler same_slice;
    CollisionContact fragments[2];fragments[0].step=fragments[1].step=1;
    fragments[0].pair=fragments[1].pair=77;
    fragments[0].impulse={12.75,0,0};fragments[1].impulse={-12.75,0,0};
    const auto accumulated=same_slice.update(1,1,fragments,2,graph);
    const unsigned expected=unsigned(std::nearbyint(double(graph.impact_gain[collision_intensity(25.5,4)])*32767/32768));
    check(accumulated.impacts.size()==1&&accumulated.impacts[0].start&&
          accumulated.impacts[0].gain_q15==expected,"same-slice impact witnesses retain original magnitude accumulator");
    check(!same_slice.update(2,1,fragments,2,graph).processed,"held physical clock still rejects repeated witnesses");
    c={}; c.step=1; c.pair=7; c.impulse={51,0,0}; c.slide_mps=20;
    CollisionScheduler scheduler;
    auto f=scheduler.update(1,c.step,&c,1,graph);
    check(f.processed && f.impacts.size()==1 && f.impacts[0].sample==40 &&
          f.impacts[0].start,"increment-before-use three-family sample binding");
    const auto volume=std::uint32_t(std::nearbyint(127.0*200*32767/32768));
    check(f.impacts[0].gain_q15==volume,"graph gain product, not impulse guessed gain");
    const int base_pitch=4096+127*3-100;
    check(f.impacts[0].pitch_q16 >= unsigned(base_pitch-150)*16 &&
          f.impacts[0].pitch_q16 <= unsigned(base_pitch+150)*16,
          "pitch lookup plus separate bounded jitter");
    check(f.scrapes.size()==1 && f.scrapes[0].start && f.scrapes[0].gain_q15>0,
          "scrape starts and ramp leaves zero");
    const double ramp_gain=32767.0*(1000.0/60)/370;
    const int ramp_integer=int(std::nearbyint(ramp_gain));
    const int scrape_base=int(std::nearbyint(63.0*200*20000/32768));
    check(f.scrapes[0].gain_q15==std::uint32_t(std::nearbyint(
          double(scrape_base)*ramp_integer*double(0x1.0002p-15F))),
          "scrape gain curves, configured gain and native ramp product");
    check(!scheduler.update(1,c.step,&c,1,graph).processed,"same tick ignored without silence command");
    c.step=2; f=scheduler.update(2,c.step,&c,1,graph);
    check(f.impacts.size()==1 && !f.impacts[0].start && f.impacts[0].sample==40,
          "same episode updates parameters without restarting PCM");
    // Another polygon is an independent port episode. Opposing impulses do
    // not cancel and one event cannot impose an invented shared cooldown.
    CollisionContact simultaneous[2]{c,c};
    simultaneous[0].step=5; simultaneous[0].pair=8;
    simultaneous[1].step=5; simultaneous[1].pair=9; simultaneous[1].impulse={-51,0,0};
    f=scheduler.update(3,5,simultaneous,2,graph);
    check(f.impacts.size()==2 && f.impacts[0].sample==41 && f.impacts[1].sample==39,
          "simultaneous opposite contacts select independent round-robin variants");
    f=scheduler.update(4,8,nullptr,0,graph);
    check(!f.scrapes.empty(),"short contact gaps retain scrape until retail expiry");
    c.step=9; f=scheduler.update(5,c.step,&c,1,graph);
    check(f.impacts.size()==1 && !f.impacts[0].start,"short contact gap does not restart impact PCM");
    f=scheduler.update(6,12,&c,1,graph,true);
    check(f.impacts.empty() && f.scrapes.empty(),"reset discards contacts copied before reset");
    c.step=13; c.impulse={0,1,0}; f=scheduler.update(7,c.step,&c,1,graph);
    check(f.impacts.size()==1 && f.impacts[0].sample==36,"reset restores four-family counter");
    scheduler.clear(); c.step=1; c.impulse={25.5,0,0}; c.slide_mps=0;
    f=scheduler.update(1,c.step,&c,1,graph);
    const auto first_gain=f.impacts[0].gain_q15;
    c.step=2; c.impulse={-25.5,0,0};
    f=scheduler.update(2,c.step,&c,1,graph);
    check(f.impacts.size()==1 && !f.impacts[0].start && f.impacts[0].gain_q15>first_gain &&
          f.impacts[0].gain_q15==volume,"opposing impulses accumulate magnitudes within a pair");
    scheduler.clear(); c.step=1; c.impulse={0,0,3}; c.slide_mps=20;
    f=scheduler.update(1,c.step,&c,1,graph);
    check(f.impacts.size()==1 && f.impacts[0].sample==31,"landing family is independent");
    c.step=4; c.impulse={0,0,0}; c.slide_mps=20;
    f=scheduler.update(2,c.step,&c,1,graph);
    check(f.impacts.empty() && !f.scrapes.empty(),"slide observation need not manufacture an impulse");
    for (unsigned tick=3;tick<180;++tick) {
        CollisionContact steps[2]{c,c};
        steps[0].step=2*tick-1; steps[1].step=2*tick;
        f=scheduler.update(tick,2*tick,steps,2,graph);
        check(f.scrapes.size()==1 && !f.scrapes[0].start && f.scrapes[0].gain_q15<=20000,
              "sustained scrape bounded across oscillator/deck wraps");
    }
    const auto last=c.step=360;
    f=scheduler.update(180,last,&c,1,graph);
    f=scheduler.update(181,last+35,nullptr,0,graph);
    check(f.scrapes.size()==1,"last contact retained below 300ms");
    f=scheduler.update(182,last+44,nullptr,0,graph);
    check(f.scrapes.size()==1&&f.scrapes[0].gain_q15>0,"expiry fades instead of chopping scrape");
    f=scheduler.update(183,last+52,nullptr,0,graph);
    check(f.scrapes.empty(),"scrape silent after 300ms plus 133ms release");
    c.step=last+53;c.impulse={51,0,0};
    f=scheduler.update(184,c.step,&c,1,graph);
    check(f.impacts.size()==1&&f.impacts[0].start,"expired episode starts a fresh impact");
    scheduler.clear();c.step=1;c.other_kind=0;c.car_subtype=0;
    f=scheduler.update(1,1,&c,1,graph);
    check(f.impacts.size()==1&&f.impacts[0].sample==40,"car frontal impact uses original WORLDImpact family");
    c.pair=8;c.step=2;c.car_subtype=1;
    f=scheduler.update(2,2,&c,1,graph);
    check(f.impacts.size()==1&&f.impacts[0].sample==36,"car side impact uses separate four-sample counter");
    check(car_collision_subtype({0,0,0},{0,2,0},{10,0,0},{10,0,0},{0,1,0})==1,
          "parallel cars with side contact select scrape-impact family");
    check(car_collision_subtype({0,0,0},{2,0,0},{10,0,0},{-10,0,0},{1,0,0})==0,
          "frontal car contact selects heavy impact family");
    std::array<std::uint8_t,nfsu::kVehicleTrackTelemetryRecordBytes> before{},after{};
    nfsu::VehicleTrackTelemetry telemetry;
    nfsu::encode_vehicle_track_telemetry(telemetry,before);
    telemetry.audio_contact_count=1; telemetry.audio_contacts[0].impulse={1,2,3};
    nfsu::encode_vehicle_track_telemetry(telemetry,after);
    check(before==after,"output contact copy excluded from replay bytes");
    if (!root) return;
    const auto read=[&](const char* name) {
        std::ifstream in(*root/name,std::ios::binary);
        check(bool(in),"retail collision bank available");
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),{});
    };
    auto impact=read("Sound/IG_GLOBAL/IMPACT_MB.abk");
    const auto scrape=read("Sound/IG_GLOBAL/SCRAPE_MB.abk");
    const auto retail=collision_graph(impact,scrape);
    // Expected entries are read independently at traced source offsets.
    const auto signed16=[](const auto& b,std::size_t at) {
        const unsigned u=unsigned(b[at]) | (unsigned(b[at+1])<<8);
        return u<32768 ? int(u) : int(u)-65536;
    };
    for (unsigned i=0;i<128;++i)
        check(retail.impact_gain[i]==signed16(impact,0xA4C+2*i) &&
              retail.impact_pitch[i]==signed16(impact,0xB5C+2*i) &&
              retail.scrape_gain[i]==signed16(scrape,0x39C+2*i) &&
              retail.scrape_pitch[i]==signed16(scrape,0x4AC+2*i),
              "collision curves bind to identified graph nodes");
    const auto bank=parse_abk(impact);
    for (unsigned sample=30;sample<=42;++sample)
        check(std::any_of(bank.samples.begin(),bank.samples.end(),
              [&](const auto& entry){return entry.index==sample && entry.frames>0;}),
              "world Player table selects existing nonempty PCM");
    impact[0]^=1;
    bool rejected=false;
    try { (void)collision_graph(impact,scrape); } catch(const std::runtime_error&) {rejected=true;}
    check(rejected,"unsupported bank must fail, not guess a collision mapping");
    std::cout << "race audio: collision graph checked\n";
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: nfsu_race_audio_tests [RETAIL_ROOT]\n";
        return 1;
    }
    try {
        test_original_engine_rpm();
        test_bank_audio_random();
    test_collision_bank_random();
    test_collision_spatial_profile();
        test_original_race_mix();
        test_original_engine_envelope();
        test_remote_shift_native();
        test_remote_gain_native();
        test_human_gain_native();
        test_human_camera_balance();
        test_human_engine_native();
        test_human_shift_feedback();
        test_backfire_native();
        test_turbo_native();
        test_remote_turbo_native();
        test_original_human_profiles();
        test_traffic_position_native();
        test_remote_position_native();
        test_remote_script_native();
        test_traffic_audio_native();
        test_weights();
        test_playlist_order();
        test_missing_audio();
        test_grid_engine_snapshot();
        test_remote_ratio_snapshot();
        test_live_collision_clock();
        test_collision_clock_contract();
        std::string argument_error;
        check(nfsu::check_arguments(argc, argv, 1, argument_error),
              argument_error.c_str());
        const std::filesystem::path root =
            argc == 2 ? nfsu::argument_path(argv[1]) : std::filesystem::path();
        test_engine_envelope_playback(argc == 2 ? &root : nullptr);
        test_traffic_sound(argc == 2 ? &root : nullptr);
        test_engine_pan_playback(argc == 2 ? &root : nullptr);
        test_remote_engine_playback(argc == 2 ? &root : nullptr);
        test_remote_shift_playback(argc == 2 ? &root : nullptr);
        test_remote_gain_tuning(argc == 2 ? &root : nullptr);
        test_prop_sound(argc == 2 ? &root : nullptr);
        test_nitrous_control(argc == 2 ? &root : nullptr);
        test_backfire_playback(argc == 2 ? &root : nullptr);
        test_shift_audio_binding(argc == 2 ? &root : nullptr);
        test_high_rpm_shift_binding(argc == 2 ? &root : nullptr);
        test_turbo_playback(argc == 2 ? &root : nullptr);
        test_remote_turbo_playback(argc == 2 ? &root : nullptr);
        test_retail_music_prefix(argc == 2 ? &root : nullptr);
        test_collision_graph(argc == 2 ? &root : nullptr);
        std::cout << "race audio tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "race audio tests: " << e.what() << '\n';
        return 1;
    }
}
