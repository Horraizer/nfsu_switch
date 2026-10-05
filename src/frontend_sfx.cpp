#include "nfsu/frontend_sfx.h"
#include "nfsu/sha256.h"
#include <fstream>
#include <stdexcept>
namespace nfsu::audio {
bool FrontendSfx::load(const std::filesystem::path& root,std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_); ready_=false;mixer_=Mixer{};
    try {
        std::ifstream input(root/"Sound/GLOBAL/FE_COMMON_MB.abk",std::ios::binary|std::ios::ate);
        if(!input || input.tellg()!=115832) throw std::runtime_error("original frontend common bank size mismatch");
        std::vector<std::uint8_t> bytes(115832); input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()) ||
           sha256_hex(sha256(bytes.data(),bytes.size()))!="21E64EFA6B5AB6CC4E490BC1BA7D3C0594CC8202BB3D7C1EF7922A25328FD3AF")
            throw std::runtime_error("original frontend common bank identity mismatch");
        const auto bank=parse_abk(bytes);
        if(bank.modules!=1 || bank.entries.size()!=12 || bank.samples.size()!=12)
            throw std::runtime_error("original frontend common bank layout mismatch");
        // speed.exe 0x4CCD20: FE_SOUND_MENU_UP(3)->0, DOWN(4)->1,
        // SELECT(5)->4, BACK(6)->5. 0x4CD180 sends these to the common voice.
        constexpr unsigned entries[4]={0,1,4,5};
        for(unsigned i=0;i<4;++i) {
            const auto& entry=bank.entries[entries[i]];
            if(entry.type!=0 || !entry.value || entry.value>bank.samples.size())
                throw std::runtime_error("original frontend cue entry invalid");
            const auto& sample=bank.samples[entry.value-1];
            PcmClip clip;clip.rate=sample.rate;clip.channels=sample.channels;
            decode_bank_sample(bytes,sample,[&](const std::uint8_t* pcm,std::size_t count){
                if(count%2) throw std::runtime_error("partial frontend PCM sample");
                for(std::size_t k=0;k<count;k+=2) {
                    const auto raw=std::uint16_t(pcm[k]) | (std::uint16_t(pcm[k+1])<<8);
                    clip.pcm.push_back(static_cast<std::int16_t>(raw));
                }
            });
            if(!clip.valid() || clip.frames()!=sample.frames || clip.frames()>48000)
                throw std::runtime_error("invalid original frontend cue PCM");
            clips_[i]=std::move(clip);
        }
        voice_=0;ready_=true;error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
void FrontendSfx::play(FrontendSound cue) {
    const auto index=static_cast<unsigned>(cue);if(index>=clips_.size()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if(ready_) {mixer_.play(voice_,&clips_[index],false);voice_=(voice_+1)%kMixVoices;}
}
bool FrontendSfx::active() {
    std::lock_guard<std::mutex> lock(mutex_);
    for(std::size_t i=0;i<kMixVoices;++i)if(mixer_.active(i))return true;
    return false;
}
void FrontendSfx::reset(){std::lock_guard<std::mutex> lock(mutex_);mixer_=Mixer{};voice_=0;}
void FrontendSfx::mix(std::int16_t* stereo,std::size_t frames) {
    std::lock_guard<std::mutex> lock(mutex_);mixer_.render(stereo,frames,stereo,32768);
}
}
