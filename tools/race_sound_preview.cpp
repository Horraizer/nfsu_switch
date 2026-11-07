#include "nfsu/race_audio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace nfsu::audio;
namespace {
void require(bool v,const char* what){if(!v)throw std::runtime_error(what);}
std::vector<std::uint8_t> bytes(const std::filesystem::path& p){
    std::ifstream f(p,std::ios::binary);require(bool(f),"missing bank");
    return {(std::istreambuf_iterator<char>(f)),{}};
}
PcmClip clip(const std::vector<std::uint8_t>& b,unsigned id){
    const auto bank=parse_abk(b);const auto i=std::find_if(bank.samples.begin(),bank.samples.end(),
        [&](const auto& v){return v.index==id;});require(i!=bank.samples.end(),"missing sample");
    PcmClip c;c.rate=i->rate;c.channels=i->channels;c.loop_start=i->loop_start;c.loop_end=i->loop_end;
    decode_bank_sample(b,*i,[&](const std::uint8_t* p,std::size_t n){
        for(std::size_t k=0;k<n;k+=2)c.pcm.push_back(static_cast<std::int16_t>(p[k]|(p[k+1]<<8)));
    });require(c.valid(),"invalid clip");return c;
}
void wav(const std::filesystem::path& path,const std::vector<std::int16_t>& pcm){
    std::ofstream f(path,std::ios::binary);require(bool(f),"WAV output unavailable");
    auto word=[&](std::uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)f.put(char(v>>(8*i)));};
    f.write("RIFF",4);word(36+std::uint32_t(pcm.size()*2),4);f.write("WAVEfmt ",8);
    word(16,4);word(1,2);word(2,2);word(48000,4);word(192000,4);word(4,2);word(16,2);
    f.write("data",4);word(std::uint32_t(pcm.size()*2),4);
    for(auto v:pcm)word(static_cast<std::uint16_t>(v),2);
    require(bool(f),"WAV write failed");
}
}
int main(int argc,char** argv){try{
    require(argc==3||argc==4,"usage: race_sound_preview RETAIL_ROOT FRESH_OUTPUT [--device]");
    const bool device=argc==4;require(!device||std::string(argv[3])=="--device","unknown option");
    const std::filesystem::path root=argv[1],out=argv[2];
    require(std::filesystem::create_directory(out),"output must be fresh");
    RaceAudioService service;
    if(device){std::string note;const bool ready=service.start(root,note,{}, {},false,"INTEGRA",1,3,1);
        std::cout<<note<<std::endl;require(ready&&note.find("effects:")==std::string::npos&&note.find("engine:")==std::string::npos,"production audio service start");}
    RaceEngineSound engine;RaceEffectsSound effects;Mixer mix;mix.set_peak_limiter(true);std::string error;
    require(engine.load(root,"INTEGRA",1,error),error.c_str());
    require(effects.load(root,"INTEGRA",1,3,1,error),error.c_str());
    const auto impact=bytes(root/"Sound/IG_GLOBAL/IMPACT_MB.abk");
    const auto scrape=bytes(root/"Sound/IG_GLOBAL/SCRAPE_MB.abk");
    auto graph=collision_graph(impact,scrape);CollisionScheduler contacts;
    std::array<PcmClip,13> impacts;for(unsigned i=0;i<13;++i)impacts[i]=clip(impact,30+i);
    auto scraping=clip(scrape,1);
    std::vector<std::int16_t> pcm;double control_us=0,mix_us=0;unsigned hitcount=0;
    for(unsigned t=0;t<1200;++t){
        RaceAudioSnapshot s;s.tick=t+1;s.physics_step=(t+1)*2;s.gear=t<180?0:(t<400?1:(t<580?2:3));
        s.rpm=t<180?900+5000*(t%90)/90.0:2500+4500*(t%180)/180.0;
        s.throttle=t<700?.9:0;s.speed_mps=t<180?0:25;s.nitrous_active=t>=400&&t<580;
        if(t>=700&&t<880){s.axle_long_slip={.65,.6};s.axle_lateral_slip={.4,.7};s.yaw_slip_radians=.2;}
        CollisionContact c;c.step=s.physics_step;c.pair=42;c.impulse={40,0,0};c.slide_mps=20;
        const bool touch=t==930||(t>=950&&t<970);s.finished=t>=1140;
        if(touch){s.contacts[0]=c;s.contact_count=1;}
        if(device){service.on_tick(s);std::this_thread::sleep_for(std::chrono::milliseconds(16));}
        auto begin=std::chrono::steady_clock::now();engine.update(s,mix);effects.update(s,mix);
        const auto voices=contacts.update(s.tick,s.physics_step,touch?&c:nullptr,touch?1:0,graph);
        for(const auto& v:voices.impacts){if(v.start){mix.play(5,&impacts.at(v.sample-30),false,v.gain_q15,v.pitch_q16,true);++hitcount;}
            else mix.control(5,v.gain_q15,v.pitch_q16);}
        if(!voices.scrapes.empty()){const auto& v=voices.scrapes[0];
            if(v.start)mix.play(9,&scraping,true,v.gain_q15,v.pitch_q16,true);else mix.control(9,v.gain_q15,v.pitch_q16);}
        else mix.control(9,0,65536);
        if(s.finished)for(unsigned slot=0;slot<kMixVoices;++slot)mix.stop(slot);
        control_us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
        const auto off=pcm.size();pcm.resize(off+1600);begin=std::chrono::steady_clock::now();mix.render(pcm.data()+off,800);
        mix_us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
    }
    require(hitcount==2,"separated collision episodes must retrigger");
    require(mix.clipped_samples()==0,"combined PCM clipping");
    require(std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;}),"combined PCM silent");
    wav(out/"combined.wav",pcm);
    if(device){const auto note=service.diagnostic();std::cout<<note<<std::endl;require(note.find("controls:")==std::string::npos,"production graph control failure");service.stop();}
    std::cout<<"PASS combined 20s: rev, launch, shifts, NOS, tyres, impacts, scrape, finish\n"
        <<"effect_bytes="<<effects.resident_bytes()<<" engine_bytes="<<engine.resident_bytes()
        <<" mean_control_us="<<control_us/1200<<" mean_mix_us_per_800_frames="<<mix_us/1200
        <<" limited_frames="<<mix.limited_frames()<<" clips="<<mix.clipped_samples()<<'\n';
    return 0;
}catch(const std::exception&e){std::cerr<<"sound preview: "<<e.what()<<'\n';return 1;}}
