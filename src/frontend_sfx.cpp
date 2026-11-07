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
        //49CBD0 uses LEFT/RIGHT;4DC6E0 emits SUMMON_DIALOG(12).
        constexpr unsigned entries[]={0,1,4,5,2,3,10};
        for(unsigned i=0;i<std::size(entries);++i) {
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
    if(ready_) {mixer_.play(voice_,&clips_[index],false,kFrontendCueGainQ15,65536);voice_=(voice_+1)%kMixVoices;}
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
bool FrontendSpeech::play(const std::filesystem::path& root,int entry,std::string& error) {
    if(entry<0){stop();error.clear();return true;}
    try {
        std::ifstream input(root/"Sound/STRMBANK/STRM_English_Speech_MB.abk",std::ios::binary|std::ios::ate);
        if(!input||input.tellg()!=3912)throw std::runtime_error("original English speech bank size mismatch");
        std::vector<std::uint8_t> bytes(3912);input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("speech bank read failed");
        if(sha256_hex(sha256(bytes.data(),bytes.size()))!="EFF1A3B00EB923A7E47216AC24434401E97F6B2C7BDACFE829A3004B65CCD859")throw std::runtime_error("original English speech bank identity mismatch");
        const auto bank=parse_abk(bytes);
        // Original 0x51DCE0 supplies 95 groups at 0x6F6BC8; 0x539B85
        // sums preceding group sizes. A request is NOT a bank ordinal.
        constexpr unsigned counts[]={1,7,5,1,8,2,1,5,4,5,2,6,3,2,1,6,6,5,5,7,1,1,6,3,2,3,4,4,2,4,2,3,2,1,2,2,2,2,1,3,3,5,1,4,6,3,3,3,4,3,3,3,3,3,3,3,3,3,1,3,3,4,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,1,1,3,3,1,1,23,4,5,3,4,2};
        if(bank.entries.size()!=277||unsigned(entry)>=std::size(counts))throw std::runtime_error("speech request outside original groups");
        unsigned ordinal=0;for(int i=0;i<entry;++i)ordinal+=counts[i];
        // First campaign messages each have one variant. Later multi-variant
        // requests currently use their first genuine variant.
        const auto& selected=bank.entries[ordinal];
        if(selected.type!=1)throw std::runtime_error("speech request is not streamed");
        std::ifstream stream(root/"SpeechStreams/STRM_English_Speech_MB.ast",std::ios::binary|std::ios::ate);
        if(!stream||stream.tellg()<0)throw std::runtime_error("original English speech stream missing");
        std::uint64_t end=static_cast<std::uint64_t>(stream.tellg());
        for(const auto& value:bank.entries)if(value.value>selected.value&&value.value<end)end=value.value;
        const auto segment=index_ast_segment_by_boundary(stream,selected.value,end);
        if(segment.frames>std::uint64_t(segment.rate)*60)throw std::runtime_error("speech duration exceeds bounded message");
        PcmClip decoded;decoded.rate=segment.rate;decoded.channels=segment.channels;
        decoded.pcm.reserve(std::size_t(segment.frames)*segment.channels);
        decode_ast_segment(stream,segment,[&](const std::uint8_t* pcm,std::size_t count){
            if(count%2)throw std::runtime_error("partial speech PCM sample");
            for(std::size_t k=0;k<count;k+=2)decoded.pcm.push_back(static_cast<std::int16_t>(std::uint16_t(pcm[k])|(std::uint16_t(pcm[k+1])<<8)));
        });
        if(!decoded.valid()||decoded.frames()!=segment.frames)throw std::runtime_error("speech PCM length mismatch");
        std::lock_guard<std::mutex> lock(mutex_);mixer_=Mixer{};clip_=std::move(decoded);
        mixer_.play(0,&clip_,false,kFrontendCueGainQ15,65536);error.clear();return true;
    }catch(const std::exception& e){stop();error=e.what();return false;}
}
void FrontendSpeech::stop(){std::lock_guard<std::mutex> lock(mutex_);mixer_=Mixer{};}
bool FrontendSpeech::active(){std::lock_guard<std::mutex> lock(mutex_);return mixer_.active(0);}
void FrontendSpeech::mix(std::int16_t* pcm,std::size_t frames){std::lock_guard<std::mutex> lock(mutex_);mixer_.render(pcm,frames,pcm,32768);}

}
