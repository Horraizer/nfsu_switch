#include "nfsu/frontend_music.h"
#include "nfsu/race_audio.h"
#include "nfsu/audio_output.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <thread>
// Exercise actual FrontendMusic and AST decoder with a deterministic device.
// Pumping submits PCM; a separate cursor acknowledges playback. No device or
// arbitrary wall-clock delay can hide a cue discarded by transition cleanup.
namespace {
nfsu::audio::Output::Render render;
std::uint64_t submitted=0,played=0;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::vector<std::int16_t> pump(std::size_t frames){
    std::vector<std::int16_t> pcm(frames*2);check(bool(render),"device callback present");
    render(pcm.data(),frames);submitted+=frames;return pcm;
}
}
namespace nfsu::audio {
struct Output::Impl {};
Output::Output():impl_(std::make_unique<Impl>()){}
Output::~Output(){stop();}
bool Output::start(Render callback,std::string& error){render=std::move(callback);submitted=played=0;error.clear();return true;}
void Output::stop(){render={};}
std::string Output::error()const{return {};}
Output::Stats Output::stats()const noexcept{return {0,played,static_cast<std::uint32_t>(submitted-played),0};}
}
int main(int argc,char** argv){
    using namespace nfsu::audio;
    try {
        if(argc!=2)throw std::runtime_error("retail root required");
        const std::filesystem::path root=argv[1];std::string error;
        for(const int entry:{14,6,33}){
            FrontendSpeech speech;check(speech.play(root,entry,error),error.c_str());
            bool audible=false;unsigned blocks=0;
            while(speech.active()&&blocks++<3000){
                std::vector<std::int16_t> pcm(1024*2);
                speech.mix(pcm.data(),1024);
                audible=audible||std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});
            }
            check(audible&&!speech.active(),"original speech produces finite audible PCM");
            check(speech.play(root,entry,error),error.c_str());speech.stop();
            std::vector<std::int16_t> base(2048,123);speech.mix(base.data(),1024);
            check(std::all_of(base.begin(),base.end(),[](auto v){return v==123;}),"stopped speech preserves music PCM");
            check(!speech.play(root,95,error)&&!speech.active(),"invalid speech request cannot retain stale voice");
        }
        for(const auto cue:{FrontendSound::select,FrontendSound::back}){
            FrontendMusic music;check(music.start(root,error),error.c_str());
            pump(1024);played=submitted; // Prime the actual music fade.
            std::ifstream input(root/"Sound/GLOBAL/FE_COMMON_MB.abk",std::ios::binary|std::ios::ate);
            check(bool(input),"reference cue bank opens");
            const auto length=input.tellg();check(length>0,"reference cue bank nonempty");
            std::vector<std::uint8_t> bank_bytes(static_cast<std::size_t>(length));input.seekg(0);
            check(bool(input.read(reinterpret_cast<char*>(bank_bytes.data()),length)),"reference cue bank reads");
            const auto bank=parse_abk(bank_bytes);
            const unsigned entry=cue==FrontendSound::select?4:5;
            check(bank.entries.size()>entry && bank.entries[entry].value>0 &&
                bank.entries[entry].value<=bank.samples.size(),"reference original cue entry");
            const auto& sample=bank.samples[bank.entries[entry].value-1];
            PcmClip clip;clip.rate=sample.rate;clip.channels=sample.channels;
            decode_bank_sample(bank_bytes,sample,[&](const std::uint8_t* pcm,std::size_t count){
                check(count%2==0,"reference original PCM framing");
                for(std::size_t i=0;i<count;i+=2)clip.pcm.push_back(static_cast<std::int16_t>(
                    std::uint16_t(pcm[i])|(std::uint16_t(pcm[i+1])<<8)));
            });
            Mixer reference;
            // Independent expected value from genuine 0x5364E0/0x523120,
            // not FrontendSfx's configuration or its output.
            check(reference.play(0,&clip,false,16357,65536),"reference native cue gain/unity pitch");
            std::vector<std::int16_t> expected(48000*2);
            reference.render(expected.data(),48000);
            check(std::any_of(expected.begin()+480,expected.end(),[](auto v){return v!=0;}),
                "original cue extends beyond music fade");
            music.sound(cue);music.begin_stop();
            check(!music.stop_ready(),"request does not discard cue immediately");
            std::vector<std::int16_t> actual;
            for(unsigned i=0;i<47;++i){auto block=pump(1024);actual.insert(actual.end(),block.begin(),block.end());}
            check(actual.size()>=expected.size(),"full original cue captured");
            check(std::equal(expected.begin()+480,expected.end(),actual.begin()+480),
                "original cue survives music fade with unchanged PCM through final sample");
            check(!music.stop_ready(),"submitted cue must actually play before device retirement");
            played=submitted;
            check(music.stop_ready(),"device playback acknowledgment completes bounded transition");
            music.stop();check(!render,"stop releases callback after audible cue");
        }
        {
            std::vector<std::int16_t> opening;
            for(unsigned bank=1;bank<=2;++bank) {
                RaceAudioService race;
                check(race.start(root,error,{},{},true,"CIVIC"),error.c_str());
                check(error.find("music_bank="+std::to_string(bank))!=std::string::npos,
                      "new race service resumes original unplayed music bank");
                std::vector<std::int16_t> pcm;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                for(unsigned block=0;block<375;++block) {
                    while(race.stats().ring_fill<1024&&std::chrono::steady_clock::now()<deadline)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    check(race.stats().ring_fill>=1024,"race decoder supplies bounded device demand");
                    auto samples=pump(1024);pcm.insert(pcm.end(),samples.begin(),samples.end());played=submitted;
                }
                check(std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;}),"selected original race music reaches device");
                check(race.stats().ring_underruns==0,"music comparison uses decoded PCM without underflow");
                if(bank==1)opening=pcm;else check(opening!=pcm,"next race actually decodes different song PCM");
                race.stop();race.stop();check(!render,"repeated race cleanup does not advance playlist twice");
            }
        }
        std::cout<<"PASS: successive race audio services play different original songs without PCM underflow\n";
        std::cout<<"PASS: actual music select/back transition preserves original cue and waits for playback cursor\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
