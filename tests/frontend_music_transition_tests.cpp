#include "nfsu/frontend_music.h"
#include "nfsu/audio_output.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
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
        for(const auto cue:{FrontendSound::select,FrontendSound::back}){
            FrontendMusic music;check(music.start(root,error),error.c_str());
            pump(1024);played=submitted; // Prime the actual music fade.
            FrontendSfx reference;check(reference.load(root,error),error.c_str());reference.play(cue);
            std::vector<std::int16_t> expected(48000*2);
            reference.mix(expected.data(),48000);
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
        std::cout<<"PASS: actual music select/back transition preserves original cue and waits for playback cursor\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
