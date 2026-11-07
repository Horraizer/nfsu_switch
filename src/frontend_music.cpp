#include "nfsu/frontend_music.h"
#include "nfsu/audio_playback.h"
#include "nfsu/audio_output.h"
#include "nfsu/sha256.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <atomic>
namespace nfsu::audio {
struct FrontendMusic::Impl {
    // A stopped StereoRing is closed permanently: recreate on each menu entry.
    std::unique_ptr<AstStreamer> stream;
    Output output;
    OutputFade fade;
    SpeechMusicDuck speech_duck;
    FrontendSfx sfx;
    FrontendSpeech speech;
    std::filesystem::path root;
    bool active=false;
    std::atomic<bool> ending{false},silenced{false};
    std::uint64_t rendered_frames=0;
    std::atomic<std::uint64_t> drain_boundary{0};
    unsigned first=0,next=0;
    std::vector<std::uint64_t> lengths;
    unsigned current() const {
        auto frames=output.stats().played_frames;
        std::size_t i=0;
        if(lengths.empty()) return first;
        std::uint64_t total=0;for(auto n:lengths) total+=n;
        if(total) frames%=total;
        while(i+1<lengths.size() && frames>=lengths[i]) {frames-=lengths[i];++i;}
        return (first+static_cast<unsigned>(i))%7;
    }
};
FrontendMusic::FrontendMusic():impl_(std::make_unique<Impl>()) {}
FrontendMusic::~FrontendMusic() { stop(); }
bool FrontendMusic::start(const std::filesystem::path& root,std::string& error,
    const std::function<bool()>& thread_setup) {
    stop();
    auto& p=*impl_;
    try {
        p.root=root;
        if(!p.sfx.load(root,error)) throw std::runtime_error(error);
        std::ifstream bank_input(root/"Sound/STRMBANK/STRM_Music_UG0_MB.abk",std::ios::binary|std::ios::ate);
        if(!bank_input || bank_input.tellg()!=968) throw std::runtime_error("original music ABK identity/size mismatch");
        std::vector<std::uint8_t> bytes(968); bank_input.seekg(0);
        if(!bank_input.read(reinterpret_cast<char*>(bytes.data()),bytes.size())) throw std::runtime_error("music bank read failed");
        if(sha256_hex(sha256(bytes.data(),bytes.size()))!="0D7239D566A1B7EAF8A86C20494CF7B085E5285AA616A1508BF6EEAC32060B41") throw std::runtime_error("original music bank hash mismatch");
        const auto bank=parse_abk(bytes);
        auto input=std::make_unique<std::ifstream>(root/"SNDSTREAMS/STRM_Music_UG0_MB.ast",std::ios::binary);
        if(!*input) throw std::runtime_error("original menu music AST missing");
        input->seekg(0,std::ios::end); const auto end=input->tellg();
        if(end<0) throw std::runtime_error("music AST length unavailable");
        std::vector<std::uint64_t> offsets;
        for(const auto& entry:bank.entries) {
            if(entry.type!=1) throw std::runtime_error("invalid streamed music entry");
            offsets.push_back(entry.value);
        }
        std::sort(offsets.begin(),offsets.end());
        if(offsets.size()!=27 || offsets.front()!=0 || std::adjacent_find(offsets.begin(),offsets.end())!=offsets.end())
            throw std::runtime_error("invalid original song boundaries");
        std::vector<Segment> indexed;
        for(std::size_t i=0;i<offsets.size();++i)
            indexed.push_back(index_ast_segment_by_boundary(*input,offsets[i],
                i+1<offsets.size()?offsets[i+1]:static_cast<std::uint64_t>(end)));
        p.stream=std::make_unique<AstStreamer>(); p.fade=OutputFade{};p.speech_duck=SpeechMusicDuck{};p.ending=false;p.silenced=false;p.rendered_frames=0;p.drain_boundary=0;
        auto playlist=original_music_playlist(bank,indexed,true);
        p.first=p.next;
        std::rotate(playlist.begin(),playlist.begin()+p.first,playlist.end());
        p.lengths.clear();
        for(const auto& track:playlist) p.lengths.push_back((std::uint64_t(track.frames)*kOutputRate+track.rate-1)/track.rate);
        if(!p.stream->start_playlist(std::move(input),std::move(playlist),true,error,thread_setup)) {
            p.stream.reset(); return false;
        }
        for(unsigned i=0;i<100 && p.stream->ring().size()<3072 && !p.stream->finished();++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if(!p.stream->error().empty()) throw std::runtime_error(p.stream->error());
        if(!p.output.start([&p](std::int16_t* pcm,std::size_t count) {
            p.stream->pop(pcm,count);
            const bool ending=p.ending.load();
            const bool effect=p.sfx.active()||p.speech.active();
            const bool audible_music=!p.fade.silent();
            // Cue gain is independent of the music transition fade.
            p.fade.apply(pcm,count,ending);
            p.speech_duck.apply(pcm,count,p.speech.active());
            p.sfx.mix(pcm,count); p.speech.mix(pcm,count);
            p.rendered_frames+=count;
            if(effect || audible_music)p.drain_boundary=p.rendered_frames;
            if(ending && p.fade.silent() && !p.sfx.active() && !p.speech.active())p.silenced=true;
        },error)) { p.stream->stop(); p.stream.reset(); return false; }
        p.active=true; error.clear(); return true;
    } catch(const std::exception& e) { error=e.what(); stop(); return false; }
}
void FrontendMusic::begin_stop() {
    auto& p=*impl_;p.speech.stop();
    if(p.active && !p.ending.exchange(true))p.next=(p.current()+1)%7;
}
bool FrontendMusic::stop_ready() const {
    const auto& p=*impl_;
    if(!p.active || !p.output.error().empty() || !p.stream->error().empty())return true;
    return p.ending.load() && p.silenced.load() &&
        p.output.stats().played_frames>=p.drain_boundary.load();
}
void FrontendMusic::stop() {
    auto& p=*impl_;
    if(p.active && !p.ending.load())p.next=(p.current()+1)%7;
    p.output.stop();
    if(p.stream) { p.stream->stop(); p.stream.reset(); }
    p.sfx.reset();p.speech.stop();p.active=false;
}
bool FrontendMusic::playing() const { return impl_->active && !impl_->ending.load(); }
void FrontendMusic::sound(FrontendSound cue) {
    if(!impl_->ending.load())impl_->sfx.play(cue);
}
bool FrontendMusic::speech(int entry,std::string& error) {
    if(!playing()){error="frontend audio device is not active";return false;}
    return impl_->speech.play(impl_->root,entry,error);
}
void FrontendMusic::stop_speech(){impl_->speech.stop();}
std::string FrontendMusic::error() const {
    if(!impl_->active) return {};
    const auto device=impl_->output.error();
    return device.empty()?impl_->stream->error():device;
}
}

namespace nfsu::audio { MenuSong FrontendMusic::now_playing() const {return original_menu_song(impl_->current());} }
