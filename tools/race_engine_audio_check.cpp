#include "nfsu/race_audio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

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
int main(int argc,char** argv){
    try{
        require(argc==4,"usage: race_engine_audio_check RETAIL_ROOT TRACE_CSV FRESH_OUTPUT");
        const std::filesystem::path root=argv[1],out=argv[3];
        require(std::filesystem::create_directory(out),"output must be fresh");
        RaceEngineSound engine;std::string error;
        require(engine.load(root,"INTEGRA",1,error),error.c_str());
        require(engine.selection().group==2&&engine.selection().upgrade==1,"intro selection");
        require(engine_audio_selection("240SX",0).group==2,"240SX mapping");
        require(engine_audio_selection("SKYLINE",3).group==1,"Skyline mapping");
        require(engine_audio_selection("SUPRA",0).group==3,"Supra mapping");
        const auto source=bytes(root/engine.selection().engine);
        auto damaged=source;damaged.at(0xa8)=0xe9;
        bool refused=false;try{EngineAudioGraph g;g.load(damaged,2,1);}catch(const std::exception&){refused=true;}
        require(refused,"unexpected graph code must be refused");
        refused=false;try{EngineAudioGraph g;g.load(source,0,0);(void)g.tick(800,0);}catch(const std::exception&){refused=true;}
        require(refused,"wrong bank identity must request retirement, not make a sound");
        Mixer old_mix,new_mix;old_mix.set_peak_limiter(true);new_mix.set_peak_limiter(true);
        std::array<PcmClip,4> old_clips;
        const auto old_bank=bytes(root/"Sound/CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk");
        for(unsigned i=0;i<4;++i){old_clips[i]=clip(old_bank,i+1);old_mix.play(i,&old_clips[i],true,0);}
        std::ifstream trace(argv[2]);require(bool(trace),"missing trace");
        std::string line;std::getline(trace,line);
        std::array<std::vector<std::int16_t>,2> pcm;
        std::uint64_t count=0;double controls_us=0;
        while(std::getline(trace,line)){
            std::istringstream row(line);std::string cell;std::vector<double> v;
            while(std::getline(row,cell,','))v.push_back(std::stod(cell));
            require(v.size()==15,"unexpected trace schema");
            RaceAudioSnapshot s;s.tick=count;s.rpm=v[4];s.throttle=v[3];
            const auto begin=std::chrono::steady_clock::now();engine.update(s,new_mix);
            controls_us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
            const auto weight=engine_layer_weights(s.rpm);
            constexpr double centers[]={800,2500,4500,7000};
            for(unsigned i=0;i<4;++i)old_mix.control(i,
                std::uint32_t(weight[i]*(.22+.33*s.throttle)*32768),
                std::uint32_t(std::clamp(std::clamp(s.rpm,800.0,8000.0)/centers[i],.5,2.0)*65536));
            for(unsigned i=0;i<2;++i){auto& p=pcm[i];const auto off=p.size();p.resize(off+1600);
                (i?new_mix:old_mix).render(p.data()+off,800);}
            ++count;
        }
        require(count>60,"trace too short");
        require(pcm[0]!=pcm[1],"before/after must differ");
        require(std::any_of(pcm[1].begin(),pcm[1].end(),[](auto v){return v!=0;}),"new PCM silent");
        RaceAudioSnapshot done;done.tick=count;done.finished=true;engine.update(done,new_mix);
        for(unsigned i=0;i<8;++i)require(!new_mix.active(i<4?i:i+8),"finish stops all engine/exhaust slots");
        wav(out/"before.wav",pcm[0]);wav(out/"after.wav",pcm[1]);
        std::cout<<"PASS selected bank, malformed/identity negatives, actual trace PCM, finish lifecycle\n"
            <<"ticks="<<count<<" bytes="<<engine.resident_bytes()<<" mean_control_us="<<controls_us/count
            <<" before_limited="<<old_mix.limited_frames()<<" after_limited="<<new_mix.limited_frames()
            <<" after_clips="<<new_mix.clipped_samples()<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<"engine audio: "<<e.what()<<'\n';return 1;}
}
